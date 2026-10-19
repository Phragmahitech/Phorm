#pragma once

/**
    Lecture d'une table pour l'écoute (sans JUCE) : 8 voix, position dans la table,
    balayage par un LFO triangle (SCAN / RATE), attaque et relâchement.

    Chaque voix lit la version de la table qui n'a aucune harmonique au-dessus de la moitié
    de la fréquence d'échantillonnage (une version par octave) : pas de repliement.
    Interpolation cubique dans la frame, linéaire entre deux frames voisines.
*/

#include "WavetableGenerator.h"

namespace phorm
{
    class Synth
    {
    public:
        static constexpr int numVoices = 8;

        struct Params
        {
            float position = 0.0f;     // 0..1
            float scan = 1.0f;         // 0..1 : part de la course de POSITION à la fin de la table
            float rateHz = 0.2f;
            float attackMs = 2.0f;
            float releaseMs = 200.0f;
            float gain = 0.5f;
        };

        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
            positionCoef = (float) std::exp (-1.0 / (0.015 * sampleRate));
            reset();
        }

        void reset() noexcept
        {
            for (auto& v : voices) v = Voice();
            lfoPhase = 0.0;
            positionReady = false;
        }

        /** Nouvelle table (ou aucune). Les voix en cours continuent, sur la nouvelle table. */
        void setTable (const Wavetable* newTable) noexcept
        {
            table = newTable;
            for (auto& v : voices)
                if (v.active) v.level = levelFor (v.frequency);
        }

        const Wavetable* getTable() const noexcept      { return table; }

        void noteOn (int note, float velocity) noexcept
        {
            if (activeVoiceCount() == 0)
                lfoPhase = 0.0;                          // le balayage repart du début

            Voice* voice = nullptr;
            for (auto& v : voices)
                if (v.active && v.note == note) { voice = &v; break; }

            if (voice == nullptr)
                for (auto& v : voices)
                    if (! v.active) { voice = &v; break; }

            if (voice == nullptr)                        // vol de la voix la plus ancienne
            {
                voice = &voices[0];
                for (auto& v : voices)
                    if (v.age < voice->age) voice = &v;
            }

            const bool wasActive = voice->active;
            voice->active = true;
            voice->releasing = false;
            voice->note = note;
            voice->velocityGain = 0.35f + 0.65f * std::clamp (velocity, 0.0f, 1.0f);
            voice->frequency = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            voice->increment = voice->frequency * frameSize / sampleRate;
            voice->level = levelFor (voice->frequency);
            voice->age = ++ageCounter;
            if (! wasActive)
            {
                voice->env = 0.0f;
                voice->phase = 0.0;
            }
        }

        void noteOff (int note) noexcept
        {
            for (auto& v : voices)
                if (v.active && v.note == note) v.releasing = true;
        }

        void allNotesOff() noexcept
        {
            for (auto& v : voices)
                if (v.active) v.releasing = true;
        }

        int activeVoiceCount() const noexcept
        {
            int n = 0;
            for (auto& v : voices) n += v.active ? 1 : 0;
            return n;
        }

        /** Position jouée au dernier échantillon (0..1), pour l'afficheur. */
        float getCurrentPosition() const noexcept        { return lastPosition; }

        /** Version de la table pour une fréquence : harmonique la plus haute sous 0,48 fs. */
        int levelFor (double frequency) const noexcept
        {
            if (table == nullptr) return 0;
            const double limit = 0.48 * sampleRate;
            for (int L = 0; L < Wavetable::numLevels; ++L)
                if (table->levelLimit[(size_t) L] * frequency < limit)
                    return L;
            return Wavetable::numLevels - 1;
        }

        /** Écrit n échantillons (remplace le contenu de left / right ; right peut être nul). */
        void render (float* left, float* right, int n, const Params& p) noexcept
        {
            const float attackStep = 1.0f / std::max (1.0f, p.attackMs * 0.001f * (float) sampleRate);
            const float releaseCoef = (float) std::exp (std::log (0.001) / std::max (1.0, p.releaseMs * 0.001 * sampleRate));
            const double lfoStep = std::max (0.0f, p.rateHz) / sampleRate;
            const float target = std::clamp (p.position, 0.0f, 1.0f);
            const float scan = std::clamp (p.scan, 0.0f, 1.0f);

            if (! positionReady)
            {
                smoothedPosition = target;
                positionReady = true;
            }

            for (int i = 0; i < n; ++i)
            {
                smoothedPosition = target + positionCoef * (smoothedPosition - target);
                const double tri = 1.0 - std::abs (1.0 - 2.0 * lfoPhase);   // 0 -> 1 -> 0
                lfoPhase += lfoStep;
                if (lfoPhase >= 1.0) lfoPhase -= 1.0;

                const float pos = std::clamp (smoothedPosition + scan * (1.0f - smoothedPosition) * (float) tri, 0.0f, 1.0f);
                lastPosition = pos;

                float out = 0.0f;

                for (auto& v : voices)
                {
                    if (! v.active) continue;

                    if (v.releasing)
                    {
                        v.env *= releaseCoef;
                        if (v.env < 1.0e-4f) { v.active = false; continue; }
                    }
                    else if (v.env < 1.0f)
                    {
                        v.env = std::min (1.0f, v.env + attackStep);
                    }

                    if (table != nullptr)
                        out += readAt (v.level, pos, v.phase) * v.env * v.velocityGain;

                    v.phase += v.increment;
                    if (v.phase >= frameSize) v.phase -= frameSize * std::floor (v.phase / frameSize);
                }

                out *= p.gain;
                left[i] = out;
                if (right != nullptr) right[i] = out;
            }
        }

    private:
        struct Voice
        {
            bool active = false, releasing = false;
            int note = -1, level = 0;
            float env = 0.0f, velocityGain = 1.0f;
            double frequency = 440.0, phase = 0.0, increment = 0.0;
            uint64_t age = 0;
        };

        static float cubic (const float* d, double phase) noexcept
        {
            const int i = (int) phase;
            const float t = (float) (phase - i);
            const float y0 = d[i - 1], y1 = d[i], y2 = d[i + 1], y3 = d[i + 2];
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        }

        float readAt (int level, float pos, double phase) const noexcept
        {
            const int F = table->numFrames;
            const float fp = pos * (float) (F - 1);
            const int f0 = std::min (F - 1, (int) fp);
            const int f1 = std::min (F - 1, f0 + 1);
            const float frac = fp - (float) f0;
            const float a = cubic (table->levelFrame (level, f0), phase);
            if (frac <= 0.0f || f1 == f0) return a;
            const float b = cubic (table->levelFrame (level, f1), phase);
            return a + (b - a) * frac;
        }

        const Wavetable* table = nullptr;
        std::array<Voice, numVoices> voices {};
        double sampleRate = 44100.0;
        double lfoPhase = 0.0;
        float positionCoef = 0.99f, smoothedPosition = 0.0f, lastPosition = 0.0f;
        bool positionReady = false;
        uint64_t ageCounter = 0;
    };
}
