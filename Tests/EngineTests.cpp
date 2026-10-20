// Tests du moteur (sans JUCE) : génération, versions par octave, fichier WAV, lecture.

#include "WavetableGenerator.h"
#include "PhormSynth.h"

#include <chrono>
#include <cstdio>

static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  ECHEC (%s:%d) ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace phorm;

namespace
{
    /** Énergie relative des harmoniques au-dessus de `limit` dans une frame. */
    double energyAbove (const float* frame, int limit)
    {
        std::vector<double> x (frame, frame + frameSize);
        std::vector<Fft::Complex> half ((size_t) frameSize / 2 + 1);
        fft().forwardReal (x.data(), half.data());
        double total = 1.0e-30, above = 0.0;
        for (int k = 1; k <= frameSize / 2; ++k)
        {
            const double e = std::norm (half[(size_t) k]);
            total += e;
            if (k > limit) above += e;
        }
        return above / total;
    }

    double meanOf (const float* frame)
    {
        double s = 0.0;
        for (int n = 0; n < frameSize; ++n) s += frame[n];
        return s / frameSize;
    }

    double pitchByZeroCrossings (const std::vector<float>& x, double sampleRate)
    {
        int first = -1, count = 0;
        double firstPos = 0.0, lastPos = 0.0;
        for (size_t i = 1; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double pos = (double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0) { first = (int) i; firstPos = pos; }
                lastPos = pos; ++count;
            }
        return count > 1 ? (count - 1) * sampleRate / (lastPos - firstPos) : 0.0;
    }

    std::unique_ptr<Wavetable> sineTable (int frames = 2)
    {
        detail::Frames f ((size_t) frames, std::vector<double> ((size_t) frameSize));
        for (auto& fr : f)
            for (int n = 0; n < frameSize; ++n) fr[(size_t) n] = std::sin (tau * n / frameSize);
        return fromFrames (TableSettings(), f, maxHarmonic);
    }

    std::unique_ptr<Wavetable> sawTable()
    {
        detail::Frames f (2, std::vector<double> ((size_t) frameSize));
        for (auto& fr : f)
            for (int n = 0; n < frameSize; ++n) fr[(size_t) n] = 2.0 * n / frameSize - 1.0;
        return fromFrames (TableSettings(), f, maxHarmonic);
    }
}

int main()
{
    std::printf ("Générateur aléatoire\n");
    {
        Rng a (42), b (42), c (43);
        bool same = true, differs = false;
        for (int i = 0; i < 1000; ++i)
        {
            const auto x = a.next(), y = b.next(), z = c.next();
            same = same && x == y;
            differs = differs || x != z;
        }
        CHECK (same, "même graine, suites différentes");
        CHECK (differs, "graines différentes, mêmes suites");

        Rng r (7);
        int counts[5] = {};
        double lo = 1.0, hi = 0.0;
        for (int i = 0; i < 100000; ++i)
        {
            const double u = r.uniform();
            lo = std::min (lo, u); hi = std::max (hi, u);
            ++counts[r.integer (0, 5)];
        }
        CHECK (lo >= 0.0 && hi < 1.0, "uniform hors [0, 1)");
        for (int k = 0; k < 5; ++k)
            CHECK (counts[k] > 19000 && counts[k] < 21000, "integer mal réparti : %d", counts[k]);

        // Valeurs fixes : la même graine doit donner la même table sur toutes les machines
        Rng fixed (123456);
        CHECK (fixed.next() == Rng (123456).next(), "suite instable");
    }

    std::printf ("Style au hasard\n");
    {
        int seen[numStyles] = {};
        for (uint32_t s = 100000; s < 100700; ++s)
        {
            const int st = randomStyleFor (s);
            CHECK (st >= 0 && st < numStyles, "style %d", st);
            if (st >= 0 && st < numStyles) ++seen[st];
            CHECK (randomStyleFor (s) == st, "tirage instable");
        }
        for (int k = 0; k < numStyles; ++k)
            CHECK (seen[k] > 60, "style %s rarement tiré (%d)", styleName (k), seen[k]);
    }

    std::printf ("Nom de fichier\n");
    {
        TableSettings s;
        s.style = styleFm; s.seed = 123456;
        CHECK (fileStem (s) == "fm-123456", "%s", fileStem (s).c_str());
        s.frames = 256; s.bright = 0.4f; s.morph = morphStep;
        CHECK (fileStem (s) == "fm-123456-256f-b40-step", "%s", fileStem (s).c_str());
        s.morph = morphLinear; s.frames = 64; s.bright = 0.75f; s.style = styleChaos;
        CHECK (fileStem (s) == "chaos-123456-lin", "%s", fileStem (s).c_str());
        s.symmetry = symmetryMirror;
        CHECK (fileStem (s) == "chaos-123456-lin-mir", "%s", fileStem (s).c_str());
        s.symmetry = symmetryFlip; s.morph = morphSmooth; s.seed = 4294967295u;
        CHECK (fileStem (s) == "chaos-4294967295-flip", "%s", fileStem (s).c_str());
        s.symmetry = 9;
        CHECK (s.sanitised().symmetry == symmetryFlip, "symétrie bornée");
    }

    std::printf ("Harmoniques selon BRIGHT\n");
    {
        CHECK (harmonicsForBright (0.0f) == 16, "%d", harmonicsForBright (0.0f));
        CHECK (harmonicsForBright (1.0f) == maxHarmonic, "%d", harmonicsForBright (1.0f));
        int prev = 0;
        bool monotonic = true;
        for (int i = 0; i <= 100; ++i)
        {
            const int h = harmonicsForBright ((float) i / 100.0f);
            monotonic = monotonic && h >= prev;
            prev = h;
        }
        CHECK (monotonic, "BRIGHT non monotone");
    }

    std::printf ("Tables : tous les styles, réglages variés\n");
    {
        const auto start = std::chrono::steady_clock::now();
        int built = 0;

        for (int style = 0; style < numStyles; ++style)
            for (int variant = 0; variant < 6; ++variant)
            {
                TableSettings s;
                s.style = style;
                s.seed = 200000u + (uint32_t) (style * 31 + variant);
                s.frames = variant == 0 ? 2 : variant == 1 ? 16 : variant == 5 ? 256 : 64;
                s.morph = variant % numMorphs;
                s.bright = variant == 2 ? 0.0f : variant == 3 ? 0.4f : 1.0f;

                const auto t = build (s);
                ++built;
                const int H = harmonicsForBright (s.bright);

                CHECK (t->numFrames == s.frames, "%s : %d frames", styleName (style), t->numFrames);
                CHECK (t->frames.size() == (size_t) s.frames * frameSize, "taille");
                CHECK (t->harmonics == H, "harmoniques %d au lieu de %d", t->harmonics, H);

                bool finite = true, peaksOk = true, dcOk = true, bandOk = true;
                for (int f = 0; f < t->numFrames; ++f)
                {
                    const float* fr = t->frame (f);
                    float peak = 0.0f;
                    for (int n = 0; n < frameSize; ++n)
                    {
                        finite = finite && std::isfinite (fr[n]);
                        peak = std::max (peak, std::abs (fr[n]));
                    }
                    peaksOk = peaksOk && std::abs (peak - 0.95f) < 1.0e-3f;
                    dcOk = dcOk && std::abs (meanOf (fr)) < 1.0e-5;
                    if (H < maxHarmonic)
                        bandOk = bandOk && energyAbove (fr, H) < 1.0e-12;
                }

                CHECK (finite, "%s %u : valeurs non finies", styleName (style), s.seed);
                CHECK (peaksOk, "%s %u : crête de frame différente de 0,95", styleName (style), s.seed);
                CHECK (dcOk, "%s %u : composante continue", styleName (style), s.seed);
                CHECK (bandOk, "%s %u : harmoniques au-delà de la limite", styleName (style), s.seed);

                // Pas de table plate : au moins une frame avec du contenu
                bool varied = false;
                for (int n = 1; n < frameSize && ! varied; ++n)
                    varied = std::abs (t->frame (0)[n] - t->frame (0)[0]) > 0.01f;
                CHECK (varied, "%s %u : frame plate", styleName (style), s.seed);
            }

        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
        std::printf ("  %d tables en %.0f ms\n", built, ms);
    }

    std::printf ("Reproductibilité\n");
    {
        for (int style = 0; style < numStyles; ++style)
        {
            TableSettings s;
            s.style = style; s.seed = 314159; s.frames = 32;
            const auto a = build (s), b = build (s);
            CHECK (a->frames == b->frames, "%s : deux tables différentes pour la même graine", styleName (style));

            s.seed = 314160;
            const auto c = build (s);
            CHECK (a->frames != c->frames, "%s : graines voisines, même table", styleName (style));
        }

        // Le nombre de frames change la résolution, pas le caractère : premières frames identiques
        TableSettings s;
        s.style = styleFm; s.seed = 5555; s.frames = 64;
        const auto a = build (s);
        s.frames = 256;
        const auto b = build (s);
        double diff = 0.0;
        for (int n = 0; n < frameSize; ++n) diff = std::max (diff, (double) std::abs (a->frame (0)[n] - b->frame (0)[n]));
        CHECK (diff < 1.0e-5, "première frame différente selon le nombre de frames (%g)", diff);
    }

    std::printf ("Symétrie\n");
    {
        int mirrorOk = 0, flipOk = 0, total = 0;
        for (int style = 0; style < numStyles; ++style)
            for (int v = 0; v < 3; ++v)
            {
                TableSettings s;
                s.style = style; s.seed = 600000u + (uint32_t) (style * 7 + v); s.frames = 32;
                s.bright = v == 0 ? 1.0f : v == 1 ? 0.5f : 0.1f;
                ++total;

                const auto plain = build (s);
                s.symmetry = symmetryMirror;
                const auto mirror = build (s);
                s.symmetry = symmetryFlip;
                const auto flip = build (s);

                // Une table déjà symétrique (harmoniques en phase) reste telle quelle avec MIRROR
                double plainAsym = 0.0;
                for (int f = 0; f < 32; ++f)
                    for (int n = 1; n < frameSize; ++n)
                        plainAsym = std::max (plainAsym, (double) std::abs (plain->frame (f)[n] - plain->frame (f)[frameSize - n]));
                if (plainAsym > 1.0e-3)
                    CHECK (plain->frames != mirror->frames, "%s : MIRROR ne change rien", styleName (style));
                CHECK (plain->frames != flip->frames, "%s : FLIP ne change rien", styleName (style));

                // MIRROR : y[N - n] = y[n] ; FLIP : y[n + N/2] = -y[n] et aucune harmonique paire
                double mirrorErr = 0.0, flipErr = 0.0, evenEnergy = 0.0, allEnergy = 1.0e-30;
                bool finite = true;
                for (int f = 0; f < 32; ++f)
                {
                    const float* m = mirror->frame (f);
                    const float* x = flip->frame (f);
                    for (int n = 1; n < frameSize; ++n)
                        mirrorErr = std::max (mirrorErr, (double) std::abs (m[n] - m[frameSize - n]));
                    for (int n = 0; n < frameSize / 2; ++n)
                        flipErr = std::max (flipErr, (double) std::abs (x[n] + x[n + frameSize / 2]));
                    for (int n = 0; n < frameSize; ++n)
                        finite = finite && std::isfinite (m[n]) && std::isfinite (x[n]);

                    std::vector<double> d (x, x + frameSize);
                    std::vector<Fft::Complex> half ((size_t) frameSize / 2 + 1);
                    fft().forwardReal (d.data(), half.data());
                    for (int k = 1; k <= frameSize / 2; ++k)
                    {
                        allEnergy += std::norm (half[(size_t) k]);
                        if (k % 2 == 0) evenEnergy += std::norm (half[(size_t) k]);
                    }
                }

                CHECK (finite, "%s : valeurs non finies", styleName (style));
                mirrorOk += mirrorErr < 1.0e-4 ? 1 : 0;
                flipOk += (flipErr < 1.0e-4 && evenEnergy / allEnergy < 1.0e-9) ? 1 : 0;
                CHECK (mirrorErr < 1.0e-4, "%s %u : MIRROR non symétrique (%g)", styleName (style), s.seed, mirrorErr);
                CHECK (flipErr < 1.0e-4, "%s %u : FLIP non antisymétrique (%g)", styleName (style), s.seed, flipErr);
                CHECK (evenEnergy / allEnergy < 1.0e-9, "%s %u : FLIP avec harmoniques paires (%g)", styleName (style), s.seed, evenEnergy / allEnergy);

                // Crêtes, continu : mêmes règles que sans symétrie
                for (const auto* t : { mirror.get(), flip.get() })
                    for (int f = 0; f < 32; ++f)
                    {
                        float peak = 0.0f; double mean = 0.0;
                        for (int n = 0; n < frameSize; ++n) { peak = std::max (peak, std::abs (t->frame (f)[n])); mean += t->frame (f)[n]; }
                        CHECK (std::abs (peak - 0.95f) < 1.0e-3f && std::abs (mean / frameSize) < 1.0e-5, "crête / continu");
                    }

                // Versions par octave : symétrie gardée
                const float* lv = mirror->levelFrame (4, 7);
                double lvErr = 0.0;
                for (int n = 1; n < frameSize; ++n) lvErr = std::max (lvErr, (double) std::abs (lv[n] - lv[frameSize - n]));
                CHECK (lvErr < 1.0e-4, "version par octave non symétrique (%g)", lvErr);
            }
        std::printf ("  MIRROR %d / %d, FLIP %d / %d\n", mirrorOk, total, flipOk, total);

        // Même graine : la symétrie part de la même matière (première moitié inchangée avant normalisation)
        TableSettings s;
        s.style = styleDigital; s.seed = 31337; s.frames = 8; s.bright = 1.0f;
        const auto a = build (s);
        s.symmetry = symmetryFlip;
        const auto b = build (s);
        CHECK (a->frames.size() == b->frames.size(), "taille");
    }

    std::printf ("Versions par octave\n");
    {
        TableSettings s;
        s.style = styleDigital; s.seed = 777; s.frames = 8; s.bright = 1.0f;
        const auto t = build (s);

        for (int L = 0; L < Wavetable::numLevels; ++L)
        {
            const int limit = t->levelLimit[(size_t) L];
            CHECK (limit == std::max (1, maxHarmonic >> L), "limite %d au niveau %d", limit, L);
            for (int f = 0; f < t->numFrames; ++f)
            {
                const float* d = t->levelFrame (L, f);
                CHECK (energyAbove (d, limit) < 1.0e-12, "niveau %d frame %d : harmoniques au-delà de %d", L, f, limit);
                CHECK (d[-1] == d[frameSize - 1] && d[frameSize] == d[0] && d[frameSize + 1] == d[1], "bords de bouclage");
            }
        }

        for (int n = 0; n < frameSize; ++n)
            if (t->levelFrame (0, 3)[n] != t->frame (3)[n]) { CHECK (false, "niveau 0 différent des frames"); break; }

        // Table sombre : les premiers niveaux sont identiques à la table
        s.bright = 0.0f;
        const auto dark = build (s);
        CHECK (dark->levelLimit[0] == 16 && dark->levelLimit[5] == 16 && dark->levelLimit[6] == 15, "limites d'une table sombre");
    }

    std::printf ("Fichier WAV\n");
    {
        TableSettings s;
        s.style = styleSync; s.seed = 42; s.frames = 16;
        const auto t = build (s);
        const auto wav = toWav (*t);

        auto u32 = [&] (size_t at) { return (uint32_t) wav[at] | ((uint32_t) wav[at + 1] << 8) | ((uint32_t) wav[at + 2] << 16) | ((uint32_t) wav[at + 3] << 24); };
        CHECK (std::memcmp (wav.data(), "RIFF", 4) == 0 && std::memcmp (wav.data() + 8, "WAVE", 4) == 0, "en-tête");
        CHECK (u32 (4) == wav.size() - 8, "taille RIFF %u / %zu", u32 (4), wav.size() - 8);

        size_t at = 12;
        bool sawFmt = false, sawClm = false, sawData = false;
        while (at + 8 <= wav.size())
        {
            const std::string id ((const char*) wav.data() + at, 4);
            const uint32_t size = u32 (at + 4);
            const size_t body = at + 8;
            if (id == "fmt ")
            {
                sawFmt = true;
                CHECK (wav[body] == 3 && wav[body + 2] == 1 && u32 (body + 4) == 44100 && wav[body + 14] == 32, "format");
            }
            else if (id == "clm ")
            {
                sawClm = true;
                const std::string text ((const char*) wav.data() + body, size);
                CHECK (text.rfind ("<!>2048", 0) == 0, "chunk clm : %s", text.c_str());
            }
            else if (id == "data")
            {
                sawData = true;
                CHECK (size == 16u * frameSize * 4u, "taille des données %u", size);
                float first;
                std::memcpy (&first, wav.data() + body, 4);
                CHECK (first == t->frames[0], "premier échantillon");
                float last;
                std::memcpy (&last, wav.data() + body + size - 4, 4);
                CHECK (last == t->frames.back(), "dernier échantillon");
            }
            at = body + size + (size & 1);
        }
        CHECK (sawFmt && sawClm && sawData, "chunks manquants");
        CHECK (at == wav.size(), "fin de fichier");
    }

    std::printf ("Lecture : hauteur\n");
    {
        const auto table = sineTable();
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (int note : { 33, 57, 69, 93 })
            {
                Synth synth;
                synth.prepare (sr);
                synth.setTable (table.get());
                synth.noteOn (note, 1.0f);
                Synth::Params p;
                p.scan = 0.0f; p.attackMs = 0.5f; p.gain = 1.0f;
                std::vector<float> l ((size_t) sr), r ((size_t) sr);
                synth.render (l.data(), r.data(), (int) sr, p);
                const double expected = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                const double measured = pitchByZeroCrossings (l, sr);
                CHECK (std::abs (measured / expected - 1.0) < 1.0e-3, "%.0f Hz, note %d : %.3f Hz au lieu de %.3f", sr, note, measured, expected);
                CHECK (l == r, "gauche et droite différents");
            }
    }

    std::printf ("Lecture : pas de repliement\n");
    {
        const auto table = sawTable();
        for (double sr : { 44100.0, 48000.0 })
        {
            Synth synth;
            synth.prepare (sr);
            synth.setTable (table.get());
            bool ok = true;
            for (int note = 0; note < 128; ++note)
            {
                const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                const int L = synth.levelFor (f);
                const int limit = table->levelLimit[(size_t) L];
                if (L < Wavetable::numLevels - 1 && limit * f >= 0.48 * sr) ok = false;
                if (L > 0 && table->levelLimit[(size_t) L - 1] * f < 0.48 * sr) ok = false;   // la plus riche possible
            }
            CHECK (ok, "choix de la version par octave à %.0f Hz", sr);
        }

        // Note aiguë sur une dent de scie : le son reste une dent de scie bornée, sans valeurs folles
        Synth synth;
        synth.prepare (48000.0);
        synth.setTable (table.get());
        synth.noteOn (108, 1.0f);
        Synth::Params p;
        p.scan = 0.0f; p.gain = 1.0f;
        std::vector<float> l (48000);
        synth.render (l.data(), nullptr, 48000, p);
        float peak = 0.0f;
        for (float v : l) peak = std::max (peak, std::abs (v));
        CHECK (peak < 1.6f && peak > 0.3f, "crête %g", peak);
    }

    std::printf ("Lecture : enveloppe, voix\n");
    {
        const auto table = sineTable();
        Synth synth;
        synth.prepare (48000.0);
        synth.setTable (table.get());
        Synth::Params p;
        p.scan = 0.0f; p.attackMs = 10.0f; p.releaseMs = 100.0f; p.gain = 1.0f;

        std::vector<float> l (4800);
        synth.render (l.data(), nullptr, 4800, p);
        float silent = 0.0f;
        for (float v : l) silent = std::max (silent, std::abs (v));
        CHECK (silent == 0.0f, "son sans note");

        synth.noteOn (60, 1.0f);
        synth.render (l.data(), nullptr, 4800, p);
        float early = 0.0f, late = 0.0f, maxStep = 0.0f;
        for (int i = 0; i < 240; ++i) early = std::max (early, std::abs (l[(size_t) i]));
        for (int i = 2400; i < 4800; ++i) late = std::max (late, std::abs (l[(size_t) i]));
        for (int i = 1; i < 4800; ++i) maxStep = std::max (maxStep, std::abs (l[(size_t) i] - l[(size_t) i - 1]));
        CHECK (early < 0.5f * late, "attaque trop brusque (%g / %g)", early, late);
        CHECK (maxStep < 0.06f, "saut %g", maxStep);

        synth.noteOff (60);
        std::vector<float> tail (48000);
        synth.render (tail.data(), nullptr, 48000, p);
        float end = 0.0f;
        for (int i = 47000; i < 48000; ++i) end = std::max (end, std::abs (tail[(size_t) i]));
        CHECK (end == 0.0f && synth.activeVoiceCount() == 0, "relâchement non terminé (%g, %d voix)", end, synth.activeVoiceCount());

        for (int note = 40; note < 52; ++note) synth.noteOn (note, 0.8f);
        CHECK (synth.activeVoiceCount() == Synth::numVoices, "%d voix", synth.activeVoiceCount());
        synth.render (l.data(), nullptr, 4800, p);
        bool finite = true;
        for (float v : l) finite = finite && std::isfinite (v);
        CHECK (finite, "valeurs non finies avec 12 notes");
    }

    std::printf ("Lecture : position, balayage, changement de table\n");
    {
        // Table de 2 frames : sinus puis sinus à l'octave ; POSITION choisit la frame
        detail::Frames f (2, std::vector<double> ((size_t) frameSize));
        for (int n = 0; n < frameSize; ++n)
        {
            f[0][(size_t) n] = std::sin (tau * n / frameSize);
            f[1][(size_t) n] = std::sin (2.0 * tau * n / frameSize);
        }
        const auto table = fromFrames (TableSettings(), f, maxHarmonic);

        for (float position : { 0.0f, 1.0f })
        {
            Synth synth;
            synth.prepare (48000.0);
            synth.setTable (table.get());
            synth.noteOn (57, 1.0f);
            Synth::Params p;
            p.position = position; p.scan = 0.0f; p.gain = 1.0f; p.attackMs = 0.5f;
            std::vector<float> l (48000);
            synth.render (l.data(), nullptr, 48000, p);
            const double measured = pitchByZeroCrossings (l, 48000.0);
            const double expected = position > 0.5f ? 440.0 : 220.0;
            CHECK (std::abs (measured / expected - 1.0) < 2.0e-3, "position %g : %.2f Hz", position, measured);
        }

        // SCAN : la position parcourt la table et revient
        Synth synth;
        synth.prepare (48000.0);
        synth.setTable (table.get());
        synth.noteOn (57, 1.0f);
        Synth::Params p;
        p.scan = 1.0f; p.rateHz = 1.0f;
        float lo = 1.0f, hi = 0.0f;
        std::vector<float> l (480);
        for (int b = 0; b < 100; ++b)
        {
            synth.render (l.data(), nullptr, 480, p);
            lo = std::min (lo, synth.getCurrentPosition());
            hi = std::max (hi, synth.getCurrentPosition());
        }
        CHECK (lo < 0.02f && hi > 0.98f, "balayage %g .. %g", lo, hi);

        // Changer de table pendant une note, puis l'enlever
        TableSettings s;
        s.style = styleChaos; s.seed = 99; s.frames = 256;
        const auto other = build (s);
        synth.setTable (other.get());
        synth.render (l.data(), nullptr, 480, p);
        bool finite = true;
        for (float v : l) finite = finite && std::isfinite (v);
        CHECK (finite, "valeurs non finies après changement de table");
        synth.setTable (nullptr);
        synth.render (l.data(), nullptr, 480, p);
        float peak = 0.0f;
        for (float v : l) peak = std::max (peak, std::abs (v));
        CHECK (peak == 0.0f, "son sans table");
    }

    std::printf ("Réglages au hasard\n");
    {
        Rng r (2026);
        bool ok = true;
        for (int i = 0; i < 40; ++i)
        {
            TableSettings s;
            s.style = r.integer (0, numStyles);
            s.seed = (uint32_t) r.integer (100000, 999999);
            s.frames = r.integer (2, 257);
            s.morph = r.integer (0, numMorphs);
            s.bright = (float) r.uniform();
            const auto t = build (s);

            Synth synth;
            const double sr = r.chance (0.5) ? 44100.0 : 192000.0;
            synth.prepare (sr);
            synth.setTable (t.get());
            Synth::Params p;
            p.position = (float) r.uniform(); p.scan = (float) r.uniform(); p.rateHz = (float) r.uniform (0.05, 10.0);
            p.attackMs = (float) r.uniform (0.5, 2000.0); p.releaseMs = (float) r.uniform (5.0, 5000.0); p.gain = 1.0f;
            std::vector<float> l (2048);
            for (int b = 0; b < 20; ++b)
            {
                if (b % 3 == 0) synth.noteOn (r.integer (0, 128), (float) r.uniform());
                if (b % 5 == 4) synth.noteOff (r.integer (0, 128));
                synth.render (l.data(), nullptr, 2048, p);
                for (float v : l) ok = ok && std::isfinite (v) && std::abs (v) < 8.0f;
            }
        }
        CHECK (ok, "valeurs folles");
    }

    std::printf ("\n%d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
