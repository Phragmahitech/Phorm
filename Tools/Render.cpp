// Rendu hors ligne : captures de l'interface (PNG) et extraits audio (WAV 24 bits).
// Usage : PhormRender <dossier de sortie>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    constexpr double sampleRate = 48000.0;

    void settle (PhormAudioProcessor& p)
    {
        p.waitForCurrentTable (5000);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        p.processBlock (b, m);
    }

    void snapshot (juce::AudioProcessorEditor& editor, const juce::File& file)
    {
        auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            png.writeImageToStream (image, *stream);
    }

    /** Une phrase de 4 notes tenues, avec le balayage de la table. */
    void renderPhrase (PhormAudioProcessor& p, const juce::File& file)
    {
        p.setRateAndBufferSizeDetails (sampleRate, 512);
        p.prepareToPlay (sampleRate, 512);
        settle (p);

        const int length = (int) (sampleRate * 9.0);
        juce::AudioBuffer<float> out (2, length);
        out.clear();
        juce::AudioBuffer<float> block (2, 512);
        const int notes[4] = { 36, 48, 43, 55 };

        for (int pos = 0; pos < length; pos += 512)
        {
            const int n = juce::jmin (512, length - pos);
            block.setSize (2, n, false, false, true);
            juce::MidiBuffer midi;
            for (int k = 0; k < 4; ++k)
            {
                const int on = (int) (k * 2.0 * sampleRate), off = on + (int) (1.8 * sampleRate);
                if (on >= pos && on < pos + n) midi.addEvent (juce::MidiMessage::noteOn (1, notes[k], 0.9f), on - pos);
                if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, notes[k]), off - pos);
            }
            p.processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, block, ch, 0, n);
        }

        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, options))
            writer->writeFromAudioSampleBuffer (out, 0, length);

        std::printf ("%s : crête %.1f dBFS\n", file.getFileName().toRawUTF8(), juce::Decibels::gainToDecibels (out.getMagnitude (0, length)));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();

    // Interface
    {
        PhormAudioProcessor p;
        p.setRateAndBufferSizeDetails (sampleRate, 512);
        p.prepareToPlay (sampleRate, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
        auto& facade = dynamic_cast<PhormAudioProcessorEditor&> (*editor).getFacade();

        auto shot = [&] (const char* name)
        {
            settle (p);
            facade.refreshNow();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            snapshot (*editor, dir.getChildFile (name));
        };

        shot ("interface-1-default.png");

        p.setStyleChoice (phorm::styleFormant);
        p.generateWithSeed (482913);
        p.apvts.getParameter (PhormAudioProcessor::positionId)->setValueNotifyingHost (0.45f);
        shot ("interface-2-formant.png");

        p.setStyleChoice (phorm::styleDigital);
        p.generateWithSeed (100002);
        p.setFrames (256);
        p.apvts.getParameter (PhormAudioProcessor::positionId)->setValueNotifyingHost (0.8f);
        shot ("interface-3-digital-256.png");

        p.setStyleChoice (PhormAudioProcessor::styleRandom);
        p.generateWithSeed (100003);
        p.setBright (0.3f);
        shot ("interface-4-random-dark.png");

        p.setStyleChoice (phorm::styleFm);
        p.generateWithSeed (2718281828u);
        p.setBright (0.75f);
        p.setFrames (64);
        p.setSymmetry (phorm::symmetryMirror);
        p.apvts.getParameter (PhormAudioProcessor::positionId)->setValueNotifyingHost (0.6f);
        shot ("interface-5-fm-mirror.png");

        p.setSymmetry (phorm::symmetryFlip);
        shot ("interface-6-fm-flip.png");
        p.editorBeingDeleted (editor.get());
    }

    // Audio
    const struct { int style; uint32_t seed; const char* name; } demos[] = {
        { phorm::styleFm, 100001, "01-fm.wav" },
        { phorm::styleFormant, 482913, "02-formant.wav" },
        { phorm::styleSync, 100000, "03-sync.wav" },
        { phorm::styleDigital, 100002, "04-digital.wav" },
    };

    for (const auto& d : demos)
    {
        PhormAudioProcessor p;
        p.setStyleChoice (d.style);
        p.generateWithSeed (d.seed);
        p.apvts.getParameter (PhormAudioProcessor::rateId)->setValueNotifyingHost (
            p.apvts.getParameter (PhormAudioProcessor::rateId)->convertTo0to1 (0.5f));
        renderPhrase (p, dir.getChildFile (d.name));
    }

    return 0;
}
