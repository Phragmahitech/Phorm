#pragma once

/**
    Phorm : générateur de wavetables aléatoires (sans JUCE, testable seul).

    Une table = N frames de 2048 échantillons, tirées d'une graine.
    Mêmes réglages + même graine = même table, sur macOS comme sous Windows
    (générateur aléatoire et tirages écrits ici, pas ceux de la bibliothèque standard).

    Chaque table est préparée pour la lecture : pour chaque frame, 10 versions de moins
    en moins riches en harmoniques (une par octave), pour jouer sans repliement à toutes les hauteurs.
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace phorm
{
    constexpr int frameSize = 2048;                 // format Serum
    constexpr int maxHarmonic = frameSize / 2 - 1;  // 1023
    constexpr int minFrames = 2, maxFrames = 256;
    constexpr double pi = 3.14159265358979323846;
    constexpr double tau = 2.0 * pi;

    enum Style : int { styleAdditive, styleFormant, styleFm, styleFold, styleSync, styleDigital, styleChaos, numStyles };
    enum Morph : int { morphSmooth, morphLinear, morphStep, numMorphs };

    inline const char* styleName (int s)
    {
        static const char* names[numStyles] = { "ADDITIVE", "FORMANT", "FM", "FOLD", "SYNC", "DIGITAL", "CHAOS" };
        return names[std::clamp (s, 0, numStyles - 1)];
    }

    inline const char* styleSlug (int s)
    {
        static const char* names[numStyles] = { "additive", "formant", "fm", "fold", "sync", "digital", "chaos" };
        return names[std::clamp (s, 0, numStyles - 1)];
    }

    inline const char* morphName (int m)
    {
        static const char* names[numMorphs] = { "SMOOTH", "LINEAR", "STEP" };
        return names[std::clamp (m, 0, numMorphs - 1)];
    }

    //==========================================================================
    /** Générateur pseudo-aléatoire xoshiro256** (graine étalée par splitmix64). */
    class Rng
    {
    public:
        explicit Rng (uint64_t seed) noexcept
        {
            uint64_t z = seed;
            for (auto& s : state)
            {
                z += 0x9E3779B97F4A7C15ull;
                uint64_t x = z;
                x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
                x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
                s = x ^ (x >> 31);
            }
        }

        uint64_t next() noexcept
        {
            const uint64_t result = rotl (state[1] * 5, 7) * 9;
            const uint64_t t = state[1] << 17;
            state[2] ^= state[0];
            state[3] ^= state[1];
            state[1] ^= state[2];
            state[0] ^= state[3];
            state[2] ^= t;
            state[3] = rotl (state[3], 45);
            return result;
        }

        /** [0, 1) */
        double uniform() noexcept                     { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
        double uniform (double a, double b) noexcept  { return a + (b - a) * uniform(); }
        /** Entier dans [lo, hi). */
        int integer (int lo, int hi) noexcept
        {
            const int n = std::max (1, hi - lo);
            return lo + std::min (n - 1, (int) (uniform() * n));
        }
        bool chance (double p) noexcept               { return uniform() < p; }

    private:
        static uint64_t rotl (uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }
        uint64_t state[4] {};
    };

    //==========================================================================
    /** FFT complexe radix 2 sur 2048 points. */
    class Fft
    {
    public:
        using Complex = std::complex<double>;

        Fft()
        {
            twiddle.resize (frameSize / 2);
            for (int k = 0; k < frameSize / 2; ++k)
                twiddle[(size_t) k] = std::polar (1.0, -tau * k / frameSize);

            reversed.resize (frameSize);
            int bits = 0;
            while ((1 << bits) < frameSize) ++bits;
            for (int i = 0; i < frameSize; ++i)
            {
                int r = 0;
                for (int b = 0; b < bits; ++b)
                    if (i & (1 << b)) r |= 1 << (bits - 1 - b);
                reversed[(size_t) i] = r;
            }
        }

        void transform (Complex* a, bool inverse) const noexcept
        {
            for (int i = 0; i < frameSize; ++i)
                if (i < reversed[(size_t) i])
                    std::swap (a[i], a[reversed[(size_t) i]]);

            for (int len = 2; len <= frameSize; len <<= 1)
            {
                const int half = len / 2, step = frameSize / len;
                for (int i = 0; i < frameSize; i += len)
                    for (int j = 0; j < half; ++j)
                    {
                        Complex w = twiddle[(size_t) (j * step)];
                        if (inverse) w = std::conj (w);
                        const Complex u = a[i + j], v = a[i + j + half] * w;
                        a[i + j] = u + v;
                        a[i + j + half] = u - v;
                    }
            }

            if (inverse)
                for (int i = 0; i < frameSize; ++i)
                    a[i] /= (double) frameSize;
        }

        /** Spectre (cases 0..1024) d'une frame réelle. */
        void forwardReal (const double* x, Complex* half) const
        {
            std::vector<Complex> a ((size_t) frameSize);
            for (int i = 0; i < frameSize; ++i) a[(size_t) i] = x[i];
            transform (a.data(), false);
            for (int k = 0; k <= frameSize / 2; ++k) half[k] = a[(size_t) k];
        }

        /** Frame réelle à partir des cases 0..1024 d'un spectre. */
        void inverseReal (const Complex* half, double* out) const
        {
            std::vector<Complex> a ((size_t) frameSize);
            a[0] = Complex (half[0].real(), 0.0);
            a[frameSize / 2] = Complex (half[frameSize / 2].real(), 0.0);
            for (int k = 1; k < frameSize / 2; ++k)
            {
                a[(size_t) k] = half[k];
                a[(size_t) (frameSize - k)] = std::conj (half[k]);
            }
            transform (a.data(), true);
            for (int i = 0; i < frameSize; ++i) out[i] = a[(size_t) i].real();
        }

    private:
        std::vector<Complex> twiddle;
        std::vector<int> reversed;
    };

    inline const Fft& fft()
    {
        static const Fft instance;
        return instance;
    }

    //==========================================================================
    /** Réglages d'une table. `style` est le style réellement utilisé (jamais « random »). */
    struct TableSettings
    {
        int style = styleFm;
        uint32_t seed = 100001;
        int frames = 64;
        int morph = morphSmooth;
        float bright = 0.75f;      // 0 = 16 harmoniques, 1 = 1023

        bool operator== (const TableSettings& o) const noexcept
        {
            return style == o.style && seed == o.seed && frames == o.frames && morph == o.morph
                && std::abs (bright - o.bright) < 1.0e-6f;
        }
        bool operator!= (const TableSettings& o) const noexcept { return ! (*this == o); }

        TableSettings sanitised() const noexcept
        {
            TableSettings s = *this;
            s.style = std::clamp (s.style, 0, numStyles - 1);
            s.frames = std::clamp (s.frames, minFrames, maxFrames);
            s.morph = std::clamp (s.morph, 0, numMorphs - 1);
            s.bright = std::isfinite (s.bright) ? std::clamp (s.bright, 0.0f, 1.0f) : 0.75f;
            return s;
        }
    };

    /** Harmonique la plus haute pour un réglage BRIGHT (16 à 1023, échelle logarithmique). */
    inline int harmonicsForBright (float bright) noexcept
    {
        const double b = std::clamp ((double) bright, 0.0, 1.0);
        const int h = (int) std::lround (std::pow (2.0, 4.0 + b * (std::log2 ((double) maxHarmonic) - 4.0)));
        return std::clamp (h, 16, maxHarmonic);
    }

    /** Style tiré pour une graine quand on demande « random ». */
    inline int randomStyleFor (uint32_t seed) noexcept
    {
        Rng r ((uint64_t) seed * 7919ull + 0x5157ull);
        return r.integer (0, numStyles);
    }

    /** Nom de fichier (sans extension) : fm-123456, plus les réglages qui s'écartent du défaut. */
    inline std::string fileStem (const TableSettings& in)
    {
        const auto s = in.sanitised();
        std::string name = std::string (styleSlug (s.style)) + "-" + std::to_string (s.seed);
        const int brightPct = (int) std::lround (s.bright * 100.0f);
        if (s.frames != 64)      name += "-" + std::to_string (s.frames) + "f";
        if (brightPct != 75)     name += "-b" + std::to_string (brightPct);
        if (s.morph == morphLinear) name += "-lin";
        if (s.morph == morphStep)   name += "-step";
        return name;
    }

    //==========================================================================
    /** Une table prête : frames (export, affichage) et versions par octave (lecture). */
    struct Wavetable
    {
        static constexpr int numLevels = 10;
        static constexpr int stride = frameSize + 3;   // 1 échantillon avant, 2 après (lecture cubique)

        TableSettings settings;
        int numFrames = 0;
        int harmonics = maxHarmonic;
        std::vector<float> frames;                     // numFrames * frameSize
        std::vector<float> levels;                     // numLevels * numFrames * stride
        std::array<int, numLevels> levelLimit {};      // harmonique la plus haute de chaque version

        const float* frame (int f) const noexcept      { return frames.data() + (size_t) f * frameSize; }

        /** Frame f de la version L ; les indices -1 à frameSize + 1 sont lisibles. */
        const float* levelFrame (int level, int f) const noexcept
        {
            return levels.data() + ((size_t) level * (size_t) numFrames + (size_t) f) * stride + 1;
        }
    };

    namespace detail
    {
        using Frames = std::vector<std::vector<double>>;
        using Key = std::vector<double>;

        struct Keys
        {
            std::vector<double> pos;
            std::vector<Key> value;
        };

        template <typename Fn>
        Keys makeKeys (Rng& r, Fn&& makeKey, int kmin = 2, int kmax = 5)
        {
            const int k = r.integer (kmin, kmax + 1);
            Keys keys;
            keys.pos.push_back (0.0);
            std::vector<double> middle;
            for (int i = 0; i < k - 2; ++i) middle.push_back (r.uniform (0.08, 0.92));
            std::sort (middle.begin(), middle.end());
            keys.pos.insert (keys.pos.end(), middle.begin(), middle.end());
            keys.pos.push_back (1.0);
            for (size_t i = 0; i < keys.pos.size(); ++i) keys.value.push_back (makeKey());
            return keys;
        }

        inline Key interp (const Keys& keys, double t, int morph)
        {
            const auto n = (int) keys.pos.size();
            int i = (int) (std::upper_bound (keys.pos.begin(), keys.pos.end(), t) - keys.pos.begin()) - 1;
            i = std::clamp (i, 0, n - 2);
            const double p0 = keys.pos[(size_t) i], p1 = keys.pos[(size_t) i + 1];
            double u = p1 > p0 ? std::clamp ((t - p0) / (p1 - p0), 0.0, 1.0) : 0.0;
            if (morph == morphStep)        u = u >= 1.0 ? 1.0 : 0.0;
            else if (morph == morphSmooth) u = u * u * (3.0 - 2.0 * u);

            const auto& a = keys.value[(size_t) i];
            const auto& b = keys.value[(size_t) i + 1];
            Key out (a.size());
            for (size_t j = 0; j < a.size(); ++j) out[j] = a[j] * (1.0 - u) + b[j] * u;
            return out;
        }

        inline std::vector<double> phaseAxis()
        {
            std::vector<double> x ((size_t) frameSize);
            for (int i = 0; i < frameSize; ++i) x[(size_t) i] = (double) i / frameSize;
            return x;
        }

        inline const std::vector<double>& X()
        {
            static const auto x = phaseAxis();
            return x;
        }

        inline double triFold (double v) noexcept
        {
            const double m = (v - 1.0) / 4.0;
            return 4.0 * std::abs (m - std::floor (m) - 0.5) - 1.0;
        }

        inline std::vector<double> fromSpectrum (const std::vector<double>& amp, const std::vector<double>& phase)
        {
            std::vector<Fft::Complex> half ((size_t) frameSize / 2 + 1);
            for (size_t h = 0; h < amp.size() && h + 1 < half.size(); ++h)
                half[h + 1] = std::polar (amp[h], phase[h]);
            std::vector<double> out ((size_t) frameSize);
            fft().inverseReal (half.data(), out.data());
            return out;
        }

        inline std::vector<double> randomPhases (Rng& r, int count, double probability)
        {
            std::vector<double> phase ((size_t) count, 0.0);
            if (r.chance (probability))
                for (auto& p : phase) p = r.uniform (0.0, tau);
            return phase;
        }

        //----------------------------------------------------------------------
        inline Frames additive (Rng& r, const std::vector<double>& t, int morph, int H)
        {
            const auto phase = randomPhases (r, H, 0.75);

            auto key = [&]
            {
                const double slope = r.uniform (0.3, 1.8);
                const double density = r.uniform (0.04, 1.0);
                const double bias = r.uniform (-0.9, 0.9);          // harmoniques paires / impaires
                Key amp ((size_t) H);
                for (int h = 1; h <= H; ++h)
                {
                    const bool on = h == 1 ? r.chance (0.85) : r.chance (density);
                    const double level = r.uniform (0.2, 1.0);
                    const double oddEven = (h % 2 == 1) ? 1.0 + bias : 1.0 - bias;
                    amp[(size_t) h - 1] = on ? std::pow ((double) h, -slope) * level * oddEven : 0.0;
                }
                return amp;
            };

            const auto keys = makeKeys (r, key);
            Frames out;
            for (double tj : t) out.push_back (fromSpectrum (interp (keys, tj, morph), phase));
            return out;
        }

        inline Frames formant (Rng& r, const std::vector<double>& t, int morph, int H)
        {
            const int nf = r.integer (1, 5);
            const auto phase = randomPhases (r, H, 0.5);
            const double top = std::log2 ((double) std::max (4, std::min (220, H)));

            auto key = [&]
            {
                Key k ((size_t) (3 * nf + 1));
                for (int i = 0; i < nf; ++i)
                {
                    k[(size_t) i] = r.uniform (0.8, top);                 // centre (log2 de l'harmonique)
                    k[(size_t) (nf + i)] = r.uniform (0.08, 0.6);         // largeur en octaves
                    k[(size_t) (2 * nf + i)] = r.uniform (0.2, 1.0);      // gain
                }
                k[(size_t) (3 * nf)] = r.uniform (0.3, 1.1);              // pente
                return k;
            };

            const auto keys = makeKeys (r, key);
            Frames out;
            std::vector<double> amp ((size_t) H);
            for (double tj : t)
            {
                const auto p = interp (keys, tj, morph);
                for (int h = 1; h <= H; ++h)
                {
                    const double lh = std::log2 ((double) h);
                    double env = 0.0;
                    for (int i = 0; i < nf; ++i)
                    {
                        const double d = (lh - p[(size_t) i]) / p[(size_t) (nf + i)];
                        env += p[(size_t) (2 * nf + i)] * std::exp (-0.5 * d * d);
                    }
                    amp[(size_t) h - 1] = std::pow ((double) h, -p[(size_t) (3 * nf)]) * (env + 0.02);
                }
                out.push_back (fromSpectrum (amp, phase));
            }
            return out;
        }

        inline Frames fm (Rng& r, const std::vector<double>& t, int morph, int)
        {
            const int c = r.integer (1, 4), m1 = r.integer (1, 9), m2 = r.integer (1, 13);
            const bool ramp = r.chance (0.5);

            auto key = [&]
            {
                const double i1 = r.uniform (0.0, 7.0);
                const double i2 = r.chance (0.6) ? r.uniform (0.0, 4.0) : 0.0;
                const double fb = r.chance (0.4) ? r.uniform (0.0, 1.2) : 0.0;
                return Key { i1, i2, fb };
            };

            auto keys = makeKeys (r, key);
            if (ramp) keys.value[0] = { 0.0, 0.0, 0.0 };   // part d'un sinus pur

            const auto& x = X();
            Frames out;
            for (double tj : t)
            {
                const auto p = interp (keys, tj, morph);
                std::vector<double> y ((size_t) frameSize);
                for (int n = 0; n < frameSize; ++n)
                {
                    const double ph = tau * x[(size_t) n];
                    const double mod2 = p[1] * std::sin (m2 * ph);
                    double mod1 = std::sin (m1 * ph + mod2);
                    mod1 = std::sin (m1 * ph + mod2 + p[2] * mod1);      // pseudo feedback
                    y[(size_t) n] = std::sin (c * ph + p[0] * mod1);
                }
                out.push_back (std::move (y));
            }
            return out;
        }

        inline Frames fold (Rng& r, const std::vector<double>& t, int morph, int)
        {
            const auto& x = X();
            const int kind = r.integer (0, 3);
            std::vector<double> base ((size_t) frameSize, 0.0);

            if (kind == 0)
                for (int n = 0; n < frameSize; ++n) base[(size_t) n] = std::sin (tau * x[(size_t) n]);
            else if (kind == 1)
                for (int n = 0; n < frameSize; ++n) base[(size_t) n] = triFold (4.0 * x[(size_t) n] - 1.0);
            else
            {
                const int parts = r.integer (2, 6);
                for (int k = 0; k < parts; ++k)
                {
                    const double a = r.uniform (0.2, 1.0);
                    const int harmonic = r.integer (1, 6);
                    const double ph = r.uniform (0.0, tau);
                    for (int n = 0; n < frameSize; ++n) base[(size_t) n] += a * std::sin (tau * harmonic * x[(size_t) n] + ph);
                }
                double peak = 1.0e-9;
                for (double v : base) peak = std::max (peak, std::abs (v));
                for (auto& v : base) v /= peak;
            }

            const bool sineFolder = r.integer (0, 2) == 0;

            auto key = [&]
            {
                const double drive = r.uniform (1.0, 14.0);
                const double off = r.chance (0.6) ? r.uniform (-0.6, 0.6) : 0.0;
                return Key { drive, off };
            };

            auto keys = makeKeys (r, key);
            keys.value[0][0] = 1.0;                         // part du son de base

            Frames out;
            for (double tj : t)
            {
                const auto p = interp (keys, tj, morph);
                std::vector<double> y ((size_t) frameSize);
                for (int n = 0; n < frameSize; ++n)
                {
                    const double v = p[0] * (base[(size_t) n] + p[1]);
                    y[(size_t) n] = sineFolder ? std::sin (v * pi / 2.0) : triFold (v);
                }
                out.push_back (std::move (y));
            }
            return out;
        }

        inline Frames sync (Rng& r, const std::vector<double>& t, int morph, int)
        {
            // Mélange sinus / dent de scie / triangle (tirage de Dirichlet)
            double w[3], sum = 0.0;
            for (auto& v : w) { v = -std::log (1.0 - r.uniform()); sum += v; }
            for (auto& v : w) v /= sum;
            const bool windowed = r.chance (0.4);

            auto key = [&] { return Key { std::pow (2.0, r.uniform (0.0, 4.2)) }; };
            auto keys = makeKeys (r, key);
            keys.value[0][0] = 1.0;

            const auto& x = X();
            Frames out;
            for (double tj : t)
            {
                const double ratio = interp (keys, tj, morph)[0];
                std::vector<double> y ((size_t) frameSize);
                for (int n = 0; n < frameSize; ++n)
                {
                    double ph = ratio * x[(size_t) n];
                    ph -= std::floor (ph);
                    const double env = windowed ? 0.5 - 0.5 * std::cos (tau * x[(size_t) n]) : 1.0;
                    y[(size_t) n] = env * (w[0] * std::sin (tau * ph) + w[1] * (2.0 * ph - 1.0) + w[2] * triFold (4.0 * ph - 1.0));
                }
                out.push_back (std::move (y));
            }
            return out;
        }

        inline Frames digital (Rng& r, const std::vector<double>& t, int morph, int)
        {
            static const int choices[] = { 4, 8, 16, 32, 64 };
            const int steps = choices[r.integer (0, 5)];

            auto key = [&]
            {
                Key k ((size_t) steps + 2);
                for (int i = 0; i < steps; ++i) k[(size_t) i] = r.uniform (-1.0, 1.0);
                k[(size_t) steps] = r.uniform (1.5, 8.0);         // bits
                k[(size_t) steps + 1] = r.uniform (0.0, 0.6);     // sinus mêlé
                return k;
            };

            const auto keys = makeKeys (r, key);
            const auto& x = X();
            Frames out;
            for (double tj : t)
            {
                const auto p = interp (keys, tj, morph);
                std::vector<double> y ((size_t) frameSize);
                double peak = 1.0e-9;
                for (int n = 0; n < frameSize; ++n)
                {
                    const int idx = std::min (steps - 1, (int) (x[(size_t) n] * steps));
                    y[(size_t) n] = p[(size_t) idx] + p[(size_t) steps + 1] * std::sin (tau * x[(size_t) n]);
                    peak = std::max (peak, std::abs (y[(size_t) n]));
                }
                const double q = std::pow (2.0, p[(size_t) steps]) / 2.0;
                for (auto& v : y) v = std::round (v / peak * q) / q;
                out.push_back (std::move (y));
            }
            return out;
        }

        inline Frames generate (int style, Rng& r, const std::vector<double>& t, int morph, int H);

        inline void normalisePerFrame (Frames& frames)
        {
            for (auto& f : frames)
            {
                double peak = 1.0e-9;
                for (double v : f) peak = std::max (peak, std::abs (v));
                for (auto& v : f) v /= peak;
            }
        }

        inline Frames chaos (Rng& r, const std::vector<double>& t, int morph, int H)
        {
            const int a = r.integer (0, numStyles - 1);
            int b = r.integer (0, numStyles - 2);
            if (b >= a) ++b;

            auto A = generate (a, r, t, morph, H);
            auto B = generate (b, r, t, morph, H);
            normalisePerFrame (A);
            normalisePerFrame (B);

            const int how = r.integer (0, 3);
            Frames out;

            if (how == 0)        // fondu de A vers B
            {
                for (size_t j = 0; j < t.size(); ++j)
                {
                    std::vector<double> y ((size_t) frameSize);
                    for (int n = 0; n < frameSize; ++n) y[(size_t) n] = A[j][(size_t) n] * (1.0 - t[j]) + B[j][(size_t) n] * t[j];
                    out.push_back (std::move (y));
                }
            }
            else if (how == 1)   // ring mod
            {
                for (size_t j = 0; j < t.size(); ++j)
                {
                    std::vector<double> y ((size_t) frameSize);
                    for (int n = 0; n < frameSize; ++n) y[(size_t) n] = A[j][(size_t) n] * B[j][(size_t) n];
                    out.push_back (std::move (y));
                }
            }
            else                 // distorsion de phase : B tord la lecture de A
            {
                const double depth = r.uniform (0.05, 0.5);
                const auto& x = X();
                for (size_t j = 0; j < t.size(); ++j)
                {
                    std::vector<double> y ((size_t) frameSize);
                    for (int n = 0; n < frameSize; ++n)
                    {
                        double p = x[(size_t) n] + depth * t[j] * B[j][(size_t) n];
                        p = (p - std::floor (p)) * frameSize;
                        const int i0 = std::min (frameSize - 1, (int) p);
                        const int i1 = (i0 + 1) % frameSize;
                        const double frac = p - i0;
                        y[(size_t) n] = A[j][(size_t) i0] * (1.0 - frac) + A[j][(size_t) i1] * frac;
                    }
                    out.push_back (std::move (y));
                }
            }
            return out;
        }

        inline Frames generate (int style, Rng& r, const std::vector<double>& t, int morph, int H)
        {
            switch (style)
            {
                case styleAdditive: return additive (r, t, morph, H);
                case styleFormant:  return formant (r, t, morph, H);
                case styleFm:       return fm (r, t, morph, H);
                case styleFold:     return fold (r, t, morph, H);
                case styleSync:     return sync (r, t, morph, H);
                case styleDigital:  return digital (r, t, morph, H);
                default:            return chaos (r, t, morph, H);
            }
        }
    }

    //==========================================================================
    /** Finition (pas de continu, limite de bande, crête à 0,95 par frame) et versions par octave. */
    inline std::unique_ptr<Wavetable> fromFrames (const TableSettings& settings, const detail::Frames& raw, int harmonics)
    {
        using C = Fft::Complex;
        const auto& transform = fft();
        constexpr int bins = frameSize / 2 + 1;
        const int F = (int) raw.size();
        harmonics = std::clamp (harmonics, 1, maxHarmonic);

        auto table = std::make_unique<Wavetable>();
        table->settings = settings;
        table->numFrames = F;
        table->harmonics = harmonics;
        table->frames.assign ((size_t) F * frameSize, 0.0f);

        std::vector<C> spectra ((size_t) F * bins);
        std::vector<double> y ((size_t) frameSize);

        for (int f = 0; f < F; ++f)
        {
            C* half = spectra.data() + (size_t) f * bins;
            bool finite = true;
            for (double v : raw[(size_t) f]) finite = finite && std::isfinite (v);

            if (finite)
                transform.forwardReal (raw[(size_t) f].data(), half);
            else
                std::fill (half, half + bins, C());

            half[0] = 0.0;
            for (int k = harmonics + 1; k < bins; ++k) half[k] = 0.0;
            transform.inverseReal (half, y.data());

            double peak = 0.0;
            for (double v : y) peak = std::max (peak, std::abs (v));

            if (peak < 1.0e-6)   // frame muette : on reprend la précédente (ou un sinus)
            {
                if (f > 0)
                    std::copy (spectra.data() + (size_t) (f - 1) * bins, spectra.data() + (size_t) f * bins, half);
                else
                {
                    std::fill (half, half + bins, C());
                    half[1] = C (0.0, -0.5 * frameSize);       // sinus
                }
                transform.inverseReal (half, y.data());
                peak = 0.0;
                for (double v : y) peak = std::max (peak, std::abs (v));
            }

            const double gain = 0.95 / std::max (peak, 1.0e-12);
            for (int k = 0; k < bins; ++k) half[k] *= gain;
            for (int n = 0; n < frameSize; ++n)
                table->frames[(size_t) f * frameSize + (size_t) n] = (float) (y[(size_t) n] * gain);
        }

        // Versions par octave
        table->levels.assign ((size_t) Wavetable::numLevels * (size_t) F * Wavetable::stride, 0.0f);
        std::vector<C> limited ((size_t) bins);
        int previousLimit = -1;

        for (int L = 0; L < Wavetable::numLevels; ++L)
        {
            const int limit = std::min (harmonics, std::max (1, maxHarmonic >> L));
            table->levelLimit[(size_t) L] = limit;

            for (int f = 0; f < F; ++f)
            {
                float* dst = table->levels.data() + ((size_t) L * (size_t) F + (size_t) f) * Wavetable::stride;

                if (limit == previousLimit)
                {
                    const float* src = dst - (size_t) F * Wavetable::stride;
                    std::copy (src, src + Wavetable::stride, dst);
                    continue;
                }

                if (limit >= harmonics)
                {
                    std::copy (table->frame (f), table->frame (f) + frameSize, dst + 1);
                }
                else
                {
                    const C* half = spectra.data() + (size_t) f * bins;
                    std::copy (half, half + bins, limited.begin());
                    for (int k = limit + 1; k < bins; ++k) limited[(size_t) k] = 0.0;
                    transform.inverseReal (limited.data(), y.data());
                    for (int n = 0; n < frameSize; ++n) dst[n + 1] = (float) y[(size_t) n];
                }

                dst[0] = dst[frameSize];                    // échantillon -1
                dst[frameSize + 1] = dst[1];                // échantillon N
                dst[frameSize + 2] = dst[2];                // échantillon N + 1
            }

            previousLimit = limit;
        }

        return table;
    }

    /** Fabrique la table décrite par les réglages. */
    inline std::unique_ptr<Wavetable> build (const TableSettings& in)
    {
        const auto s = in.sanitised();
        const int H = harmonicsForBright (s.bright);
        Rng r (((uint64_t) s.seed << 8) ^ ((uint64_t) s.style * (uint64_t) 0x9E37));

        std::vector<double> t ((size_t) s.frames);
        for (int j = 0; j < s.frames; ++j) t[(size_t) j] = (double) j / (double) (s.frames - 1);

        const auto frames = detail::generate (s.style, r, t, s.morph, H);
        return fromFrames (s, frames, H);
    }

    //==========================================================================
    /** Fichier WAV de la table : 32 bits float, 44,1 kHz, avec le chunk 'clm ' lu par Serum. */
    inline std::vector<uint8_t> toWav (const Wavetable& table)
    {
        std::vector<uint8_t> out;
        auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back ((uint8_t) (v >> (8 * i))); };
        auto put16 = [&] (uint16_t v) { out.push_back ((uint8_t) v); out.push_back ((uint8_t) (v >> 8)); };
        auto tag = [&] (const char* id) { out.insert (out.end(), id, id + 4); };

        const uint32_t sampleRate = 44100;
        const auto numSamples = (uint32_t) table.frames.size();
        const std::string clm = "<!>2048 00000000 wavetable (Phragma Phorm)";
        const uint32_t clmPadded = (uint32_t) (clm.size() + (clm.size() & 1));
        const uint32_t dataBytes = numSamples * 4;
        const uint32_t riffSize = 4 + (8 + 18) + (8 + 4) + (8 + clmPadded) + (8 + dataBytes);

        out.reserve (riffSize + 8);
        tag ("RIFF"); put32 (riffSize); tag ("WAVE");
        tag ("fmt "); put32 (18); put16 (3); put16 (1); put32 (sampleRate); put32 (sampleRate * 4); put16 (4); put16 (32); put16 (0);
        tag ("fact"); put32 (4); put32 (numSamples);
        tag ("clm "); put32 ((uint32_t) clm.size()); out.insert (out.end(), clm.begin(), clm.end()); if (clm.size() & 1) out.push_back (0);
        tag ("data"); put32 (dataBytes);

        for (float v : table.frames)
        {
            uint32_t bits;
            std::memcpy (&bits, &v, 4);
            put32 (bits);
        }
        return out;
    }
}
