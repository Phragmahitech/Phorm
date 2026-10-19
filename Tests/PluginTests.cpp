// Tests du plugin complet (paramètres, génération, MIDI, état, export, interface).

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <cstdio>

static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  ECHEC (%s:%d) ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    struct Event { int at; int note; float velocity; };   // velocity 0 = note off

    juce::AudioBuffer<float> run (PhormAudioProcessor& p, int samples, const std::vector<Event>& events, int block = blockSize, int channels = 2)
    {
        juce::AudioBuffer<float> out (channels, samples);
        out.clear();
        juce::AudioBuffer<float> buffer (channels, block);

        for (int pos = 0; pos < samples; pos += block)
        {
            const int n = juce::jmin (block, samples - pos);
            buffer.setSize (channels, n, false, false, true);
            juce::MidiBuffer midi;

            for (auto& e : events)
                if (e.at >= pos && e.at < pos + n)
                    midi.addEvent (e.velocity > 0.0f ? juce::MidiMessage::noteOn (1, e.note, e.velocity)
                                                     : juce::MidiMessage::noteOff (1, e.note), e.at - pos);

            p.processBlock (buffer, midi);

            for (int ch = 0; ch < channels; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }

        return out;
    }

    double rms (const juce::AudioBuffer<float>& b, int from, int to, int channel = 0)
    {
        to = juce::jmin (to, b.getNumSamples());
        if (to <= from) return 0.0;
        double s = 0;
        auto* d = b.getReadPointer (channel);
        for (int i = from; i < to; ++i) s += (double) d[i] * d[i];
        return std::sqrt (s / (to - from));
    }

    void setParam (PhormAudioProcessor& p, const char* id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    void prepare (PhormAudioProcessor& p)
    {
        p.setRateAndBufferSizeDetails (sampleRate, blockSize);
        p.prepareToPlay (sampleRate, blockSize);
    }

    /** Laisse le fil des tables finir puis fait passer la table au fil audio. */
    bool settle (PhormAudioProcessor& p)
    {
        const bool ok = p.waitForCurrentTable (30000);
        run (p, blockSize * 2, {});
        return ok;
    }

    std::vector<uint8_t> readFile (const juce::File& f)
    {
        juce::MemoryBlock mb;
        f.loadFileAsData (mb);
        const auto* d = static_cast<const uint8_t*> (mb.getData());
        return { d, d + mb.getSize() };
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    std::printf ("Paramètres\n");
    {
        PhormAudioProcessor p;
        CHECK (p.getParameters().size() == 6, "%d paramètres au lieu de 6", p.getParameters().size());

        for (auto* id : { PhormAudioProcessor::positionId, PhormAudioProcessor::scanId, PhormAudioProcessor::rateId,
                          PhormAudioProcessor::attackId, PhormAudioProcessor::releaseId, PhormAudioProcessor::levelId })
            CHECK (p.apvts.getParameter (id) != nullptr, "paramètre %s absent", id);

        auto* pos = p.apvts.getParameter (PhormAudioProcessor::positionId);
        CHECK (pos->getText (pos->convertTo0to1 (37.0f), 32) == "37 %", "%s", pos->getText (pos->convertTo0to1 (37.0f), 32).toRawUTF8());
        auto* rate = p.apvts.getParameter (PhormAudioProcessor::rateId);
        CHECK (std::abs (rate->convertFrom0to1 (rate->getValueForText ("2 Hz")) - 2.0f) < 0.01f, "texte -> rate");
        auto* rel = p.apvts.getParameter (PhormAudioProcessor::releaseId);
        CHECK (std::abs (rel->convertFrom0to1 (rel->getValueForText ("1.5 s")) - 1500.0f) < 1.0f, "texte -> release");
        CHECK (rel->getText (rel->convertTo0to1 (200.0f), 32) == "200 ms", "%s", rel->getText (rel->convertTo0to1 (200.0f), 32).toRawUTF8());
        auto* lvl = p.apvts.getParameter (PhormAudioProcessor::levelId);
        CHECK (lvl->getText (lvl->getDefaultValue(), 32) == "-6.0 dB", "%s", lvl->getText (lvl->getDefaultValue(), 32).toRawUTF8());

        CHECK (p.acceptsMidi() && ! p.producesMidi() && p.getName() == "Phorm", "propriétés");
        CHECK (p.getTotalNumInputChannels() == 0 && p.getTotalNumOutputChannels() == 2, "bus");
    }

    std::printf ("Table de départ, son\n");
    {
        PhormAudioProcessor p;
        prepare (p);
        CHECK (p.getDisplayTable() != nullptr, "pas de table au départ");
        CHECK (p.getCurrentSettings() == p.getDisplayTable()->settings, "table de départ différente des réglages");

        auto silent = run (p, 4800, {});
        CHECK (silent.getMagnitude (0, 4800) == 0.0f, "son sans note");

        // Une note sonne tout de suite, sans attendre le fil des tables
        auto out = run (p, 48000, { { 0, 60, 1.0f }, { 24000, 60, 0.0f } });
        CHECK (rms (out, 2000, 24000) > 0.05, "pas de son (%.4f)", rms (out, 2000, 24000));
        CHECK (rms (out, 2000, 24000, 0) == rms (out, 2000, 24000, 1), "gauche et droite différents");
        CHECK (out.getMagnitude (0, 48000) < 1.0f, "crête %g", out.getMagnitude (0, 48000));
        CHECK (rms (out, 47000, 48000) < 1.0e-4, "relâchement trop long");
        CHECK (p.getActiveVoices() == 0, "voix encore active");

        // Mono
        p.releaseResources();
        auto mono = run (p, 9600, { { 0, 64, 0.8f } }, blockSize, 1);
        CHECK (rms (mono, 1000, 9600) > 0.05, "pas de son en mono");

        // Plusieurs notes, blocs bizarres
        auto chord = run (p, 48000, { { 0, 48, 1.0f }, { 7, 55, 1.0f }, { 13, 60, 1.0f }, { 333, 64, 1.0f }, { 40000, 48, 0.0f } }, 37);
        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 48000; ++i) finite = finite && std::isfinite (chord.getSample (ch, i));
        CHECK (finite, "valeurs non finies");
    }

    std::printf ("Génération, historique\n");
    {
        PhormAudioProcessor p;
        prepare (p);
        const auto first = p.getCurrentSettings();

        p.setStyleChoice (phorm::styleSync);
        p.generateWithSeed (424242);
        auto s = p.getCurrentSettings();
        CHECK (s.style == phorm::styleSync && s.seed == 424242u, "réglages %d %u", s.style, s.seed);
        CHECK (s.frames == first.frames && s.morph == first.morph && s.bright == first.bright, "les réglages de table ne suivent pas");
        CHECK (settle (p), "table pas fabriquée");
        CHECK (p.getDisplayTable()->settings == s, "table affichée différente");
        CHECK (p.getHistorySize() == 2 && p.getHistoryIndex() == 1, "historique %d / %d", p.getHistoryIndex(), p.getHistorySize());

        p.setStyleChoice (PhormAudioProcessor::styleRandom);
        p.generateWithSeed (555555);
        CHECK (p.getCurrentSettings().style == phorm::randomStyleFor (555555), "style au hasard");
        p.generate();
        CHECK (p.getHistorySize() == 4, "historique %d", p.getHistorySize());
        CHECK (p.getCurrentSettings().seed >= 100000u && p.getCurrentSettings().seed < 1000000u, "graine %u", p.getCurrentSettings().seed);

        CHECK (p.stepHistory (-1) && p.getCurrentSettings().seed == 555555u, "retour en arrière");
        CHECK (p.stepHistory (-2) && p.getCurrentSettings() == first, "retour au début");
        CHECK (! p.stepHistory (-1), "avant le début");
        CHECK (p.stepHistory (+3) && ! p.stepHistory (+1), "fin de l'historique");

        // Générer depuis le milieu de l'historique coupe la suite
        p.stepHistory (-2);
        p.generateWithSeed (777777);
        CHECK (p.getHistorySize() == 3 && p.getHistoryIndex() == 2, "historique coupé %d / %d", p.getHistoryIndex(), p.getHistorySize());

        // Limite de taille
        for (int i = 0; i < 150; ++i) p.generateWithSeed ((uint32_t) (200000 + i));
        CHECK (p.getHistorySize() == PhormAudioProcessor::maxHistory, "historique %d", p.getHistorySize());
        CHECK (p.getCurrentSettings().seed == 200149u, "dernière graine");
        CHECK (settle (p), "table pas fabriquée après une rafale");
    }

    std::printf ("Réglages de la table en cours\n");
    {
        PhormAudioProcessor p;
        prepare (p);
        p.setStyleChoice (phorm::styleFm);
        p.generateWithSeed (123123);
        const auto before = p.getCurrentSettings();

        p.setBright (0.2f);
        p.setFrames (256);
        p.setMorph (phorm::morphStep);
        const auto after = p.getCurrentSettings();
        CHECK (after.seed == before.seed && after.style == before.style, "la table a changé de graine");
        CHECK (after.frames == 256 && after.morph == phorm::morphStep && std::abs (after.bright - 0.2f) < 1.0e-6f, "réglages non pris");
        CHECK (p.getHistorySize() == 2, "un réglage ne doit pas ajouter de table à l'historique");
        CHECK (settle (p), "table pas refaite");
        CHECK (p.getDisplayTable()->numFrames == 256, "%d frames", p.getDisplayTable()->numFrames);
        CHECK (p.getDisplayTable()->harmonics == phorm::harmonicsForBright (0.2f), "harmoniques");

        // La table suivante reprend ces réglages
        p.generateWithSeed (321321);
        CHECK (p.getCurrentSettings().frames == 256 && p.getCurrentSettings().morph == phorm::morphStep, "réglages non repris");

        // Hors limites
        p.setFrames (100000);
        p.setBright (-3.0f);
        p.setMorph (17);
        const auto clamped = p.getCurrentSettings();
        CHECK (clamped.frames == phorm::maxFrames && clamped.bright == 0.0f && clamped.morph == phorm::numMorphs - 1, "bornes");
        p.setStyleChoice (99);
        CHECK (p.getStyleChoice() == phorm::numStyles - 1, "choix de style borné");
    }

    std::printf ("Le son suit la table\n");
    {
        PhormAudioProcessor p;
        prepare (p);
        setParam (p, PhormAudioProcessor::scanId, 0.0f);
        setParam (p, PhormAudioProcessor::attackId, 0.5f);

        auto render = [&]
        {
            auto out = run (p, 9600, { { 0, 57, 1.0f } });
            run (p, 48000, { { 0, 57, 0.0f } });
            return out;
        };

        p.setStyleChoice (phorm::styleDigital);
        p.generateWithSeed (111);
        settle (p);
        const auto a = render();
        p.generateWithSeed (222);
        settle (p);
        const auto b = render();
        double diff = 0.0;
        for (int i = 0; i < 9600; ++i) diff += std::abs (a.getSample (0, i) - b.getSample (0, i));
        CHECK (diff > 10.0, "deux tables, même son (%g)", diff);

        p.stepHistory (-1);
        settle (p);
        const auto c = render();
        double same = 0.0;
        for (int i = 0; i < 9600; ++i) same += std::abs (a.getSample (0, i) - c.getSample (0, i));
        CHECK (same < 1.0e-3, "revenir à une table ne redonne pas le même son (%g)", same);
    }

    std::printf ("Sauvegarde et rappel\n");
    {
        PhormAudioProcessor p;
        prepare (p);
        p.setStyleChoice (phorm::styleFormant);
        p.generateWithSeed (909090);
        p.setBright (0.33f);
        p.setFrames (128);
        p.setMorph (phorm::morphLinear);
        p.generateWithSeed (808080);
        p.stepHistory (-1);
        setParam (p, PhormAudioProcessor::positionId, 42.0f);
        setParam (p, PhormAudioProcessor::releaseId, 900.0f);
        p.apvts.state.setProperty ("editorWidth", 800, nullptr);

        juce::MemoryBlock state;
        p.getStateInformation (state);
        // Deux sauvegardes de suite : pas de doublon de l'historique
        p.getStateInformation (state);

        PhormAudioProcessor q;
        prepare (q);
        q.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (q.getCurrentSettings() == p.getCurrentSettings(), "réglages de la table non rappelés");
        CHECK (q.getHistorySize() == p.getHistorySize() && q.getHistoryIndex() == p.getHistoryIndex(), "historique non rappelé");
        CHECK (q.getStyleChoice() == phorm::styleFormant, "choix de style non rappelé");
        CHECK (std::abs (q.apvts.getRawParameterValue (PhormAudioProcessor::positionId)->load() - 42.0f) < 0.01f, "position");
        CHECK (std::abs (q.apvts.getRawParameterValue (PhormAudioProcessor::releaseId)->load() - 900.0f) < 1.0f, "release");
        CHECK ((int) q.apvts.state.getProperty ("editorWidth") == 800, "taille de fenêtre");
        CHECK (settle (q) && settle (p), "table pas refaite");
        CHECK (q.getDisplayTable()->frames == p.getDisplayTable()->frames, "table différente après rappel");

        // États abîmés ou vides
        PhormAudioProcessor r;
        const auto before = r.getCurrentSettings();
        r.setStateInformation (nullptr, 0);
        const char junk[] = "not a state";
        r.setStateInformation (junk, (int) sizeof (junk));
        CHECK (r.getCurrentSettings() == before && r.getHistorySize() == 1, "un état abîmé a changé la table");

        // État sans générateur (ou avec des valeurs folles)
        juce::ValueTree t ("Phorm");
        juce::ValueTree gen ("Generator");
        juce::ValueTree tab ("Table");
        tab.setProperty ("style", "nope", nullptr);
        tab.setProperty ("seed", -5, nullptr);
        tab.setProperty ("frames", 100000, nullptr);
        tab.setProperty ("morph", -3, nullptr);
        tab.setProperty ("bright", 7.0, nullptr);
        gen.appendChild (tab, nullptr);
        gen.setProperty ("index", 50, nullptr);
        t.appendChild (gen, nullptr);
        juce::MemoryBlock weird;
        juce::AudioProcessor::copyXmlToBinary (*t.createXml(), weird);
        r.setStateInformation (weird.getData(), (int) weird.getSize());
        const auto w = r.getCurrentSettings();
        CHECK (w.frames == phorm::maxFrames && w.morph == 0 && w.bright == 1.0f && w.style == phorm::TableSettings().style, "valeurs folles mal bornées");
        CHECK (r.getHistorySize() == 1 && r.getHistoryIndex() == 0, "index borné");
        CHECK (settle (r), "table pas faite");
    }

    std::printf ("Export\n");
    {
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("phorm-tests-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        root.deleteRecursively();

        PhormAudioProcessor p;
        prepare (p);
        p.setStyleChoice (phorm::styleFold);
        p.generateWithSeed (246810);
        settle (p);

        // Sans Serum : Documents/Phorm/Tables
        {
            const auto docs = root.getChildFile ("NoSerum");
            docs.createDirectory();
            const auto result = p.exportCurrentTo (docs);
            CHECK (result.ok && ! result.serumFound && result.files.size() == 1, "export sans Serum");
            const auto expected = docs.getChildFile ("Phorm/Tables/fold-246810.wav");
            CHECK (result.files.size() == 1 && result.files[0] == expected, "%s", result.files.isEmpty() ? "" : result.files[0].getFullPathName().toRawUTF8());
            CHECK (readFile (expected) == phorm::toWav (*p.getDisplayTable()), "contenu du fichier");
        }

        // Serum 1 et Serum 2 : un fichier dans chaque dossier Tables
        {
            const auto docs = root.getChildFile ("Serum");
            docs.getChildFile ("Xfer/Serum Presets/Tables").createDirectory();
            docs.getChildFile ("Xfer/Serum 2 Presets/Tables").createDirectory();
            docs.getChildFile ("Xfer/Other").createDirectory();
            const auto found = PhormAudioProcessor::findSerumTableFolders (docs);
            CHECK (found.size() == 2, "%d dossiers Tables", found.size());

            p.setBright (0.4f);
            const auto result = p.exportCurrentTo (docs);   // la table n'est peut-être pas encore refaite : l'export la fabrique
            CHECK (result.ok && result.serumFound && result.files.size() == 2, "export Serum (%d fichiers, %s)", result.files.size(), result.error.toRawUTF8());
            CHECK (docs.getChildFile ("Xfer/Serum Presets/Tables/Phorm/fold-246810-b40.wav").existsAsFile(), "fichier Serum 1");
            CHECK (docs.getChildFile ("Xfer/Serum 2 Presets/Tables/Phorm/fold-246810-b40.wav").existsAsFile(), "fichier Serum 2");

            settle (p);
            CHECK (readFile (result.files[0]) == phorm::toWav (*p.getDisplayTable()), "contenu différent de la table");
            CHECK (p.getLastExport().files.size() == 2, "dernier export");

            // Réexporter remplace le fichier
            CHECK (p.exportCurrentTo (docs).ok, "réexport");
            CHECK (docs.getChildFile ("Xfer/Serum Presets/Tables/Phorm").getNumberOfChildFiles (juce::File::findFiles) == 1, "doublon");
        }

        // Dossier impossible à créer
        {
            const auto blocker = root.getChildFile ("Blocked");
            blocker.getParentDirectory().createDirectory();
            blocker.replaceWithText ("un fichier, pas un dossier");
            const auto result = p.exportCurrentTo (blocker);
            CHECK (! result.ok && result.error.isNotEmpty(), "échec non signalé");
        }

        root.deleteRecursively();
    }

    std::printf ("Bypass, reset\n");
    {
        PhormAudioProcessor p;
        prepare (p);
        run (p, 4800, { { 0, 60, 1.0f } });
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        p.processBlockBypassed (buffer, midi);
        CHECK (buffer.getMagnitude (0, 512) == 0.0f, "bypass non silencieux");
        auto after = run (p, 4800, {});
        CHECK (after.getMagnitude (0, 4800) == 0.0f, "note qui reprend après le bypass");

        run (p, 4800, { { 0, 60, 1.0f } });
        p.reset();
        auto afterReset = run (p, 4800, {});
        CHECK (afterReset.getMagnitude (0, 4800) == 0.0f, "son après reset");
    }

    std::printf ("Changements de table pendant le jeu\n");
    {
        PhormAudioProcessor p;
        prepare (p);
        bool finite = true;
        juce::AudioBuffer<float> buffer (2, 128);
        for (int i = 0; i < 400; ++i)
        {
            juce::MidiBuffer midi;
            if (i % 20 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 40 + (i / 20) % 40, 0.9f), 3);
            if (i % 20 == 15) midi.addEvent (juce::MidiMessage::noteOff (1, 40 + (i / 20) % 40), 0);
            if (i % 7 == 0) p.generateWithSeed ((uint32_t) (300000 + i));
            if (i % 11 == 0) p.setFrames (PhormAudioProcessor::frameChoices[(i / 11) % 5]);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < 128; ++k) finite = finite && std::isfinite (buffer.getSample (ch, k)) && std::abs (buffer.getSample (ch, k)) < 4.0f;
            if (i % 50 == 0) juce::Thread::sleep (20);
        }
        CHECK (finite, "valeurs folles");
        CHECK (settle (p), "table finale pas faite");
    }

    std::printf ("Interface\n");
    {
        PhormAudioProcessor p;
        prepare (p);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
        CHECK (editor != nullptr, "pas d'éditeur");
        auto* ed = dynamic_cast<PhormAudioProcessorEditor*> (editor.get());
        CHECK (ed != nullptr, "type d'éditeur");

        if (ed != nullptr)
        {
            CHECK (ed->getWidth() == PhormFacade::width && ed->getHeight() == PhormFacade::height, "taille %d x %d", ed->getWidth(), ed->getHeight());
            auto& facade = ed->getFacade();

            facade.getStyleButton (1 + phorm::styleSync).triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            CHECK (p.getStyleChoice() == phorm::styleSync, "bouton de style");

            const int size = p.getHistorySize();
            facade.getGenerateButton().triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            CHECK (p.getHistorySize() == size + 1 && p.getCurrentSettings().style == phorm::styleSync, "GENERATE");

            facade.getMorphButton (phorm::morphStep).triggerClick();
            facade.getFramesButton (4).triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            CHECK (p.getCurrentSettings().morph == phorm::morphStep && p.getCurrentSettings().frames == 256, "MORPH / FRAMES");

            facade.getBrightSlider().setValue (30.0, juce::sendNotificationSync);
            CHECK (std::abs (p.getCurrentSettings().bright - 0.3f) < 1.0e-6f, "BRIGHT");

            facade.getPreviousButton().triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            CHECK (p.getHistoryIndex() == size - 1, "‹");
            facade.refreshNow();
            CHECK (std::abs (facade.getBrightSlider().getValue() - std::round (p.getCurrentSettings().bright * 100.0f)) < 0.5, "le curseur ne suit pas la table");

            settle (p);
            facade.refreshNow();
            auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            CHECK (image.isValid(), "capture");

            // Redimensionnement
            editor->setSize (700, 504);
            CHECK ((int) p.apvts.state.getProperty ("editorWidth") == 700, "taille non gardée");
            CHECK (editor->getConstrainer()->getMaximumWidth() == juce::roundToInt (PhormFacade::width * 1.5), "taille max");
            CHECK (editor->getConstrainer()->getMinimumWidth() == juce::roundToInt (PhormFacade::width * 0.6), "taille min");
        }

        p.editorBeingDeleted (editor.get());
        editor = nullptr;
    }

    std::printf ("\n%d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
