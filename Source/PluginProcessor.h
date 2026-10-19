#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "WavetableGenerator.h"
#include "PhormSynth.h"

/**
    Phorm : générateur de wavetables aléatoires pour Serum.

    GENERATE tire une nouvelle table (style, graine). On l'écoute en jouant des notes MIDI,
    puis on l'exporte en .wav dans le dossier Tables de Serum (ou on la glisse sur un oscillateur).

    La table est décrite par ses réglages (style, graine, frames, morph, bright) : c'est eux que le projet
    garde, la table est refaite à l'ouverture. Les tables sont fabriquées par un fil à part ;
    le fil audio ne fait que les échanger, sans allouer ni libérer.
*/
class PhormAudioProcessor final : public juce::AudioProcessor
{
public:
    //==========================================================================
    // Identifiants des paramètres (ne pas les renommer : les projets DAW sauvegardés en dépendent)
    static constexpr const char* positionId = "position";
    static constexpr const char* scanId     = "scan";
    static constexpr const char* rateId     = "rate";
    static constexpr const char* attackId   = "attack";
    static constexpr const char* releaseId  = "release";
    static constexpr const char* levelId    = "level";

    /** Choix de style : -1 = RANDOM, sinon un style. */
    static constexpr int styleRandom = -1;
    static constexpr int frameChoices[5] = { 16, 32, 64, 128, 256 };
    static constexpr int maxHistory = 100;

    //==========================================================================
    PhormAudioProcessor();
    ~PhormAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "Phorm"; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 5.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    //==========================================================================
    // Générateur (fil de l'interface)

    /** Nouvelle table du style choisi, nouvelle graine. */
    void generate();
    /** Même chose avec une graine donnée (tests). */
    void generateWithSeed (uint32_t seed);
    /** Table précédente / suivante de l'historique. */
    bool stepHistory (int delta);

    int getStyleChoice() const;
    void setStyleChoice (int choice);

    /** Change un réglage de la table en cours (elle est refaite avec la même graine). */
    void setBright (float bright);
    void setMorph (int morph);
    void setFrames (int frames);

    phorm::TableSettings getCurrentSettings() const;
    int getHistoryIndex() const;
    int getHistorySize() const;

    /** Dernière table fabriquée (pour l'afficheur) ; peut être en retard d'un instant sur les réglages. */
    std::shared_ptr<const phorm::Wavetable> getDisplayTable() const;
    /** Augmente à chaque table fabriquée. */
    uint32_t getTableVersion() const noexcept               { return tableVersion.load(); }
    /** Attend que la table affichée corresponde aux réglages en cours (tests). */
    bool waitForCurrentTable (int timeoutMs) const;

    /** Position jouée (0..1) et nombre de notes tenues, pour l'afficheur. */
    float getPlayedPosition() const noexcept                { return playedPosition.load (std::memory_order_relaxed); }
    int getActiveVoices() const noexcept                    { return activeVoices.load (std::memory_order_relaxed); }

    //==========================================================================
    // Export
    struct ExportResult
    {
        bool ok = false;
        bool serumFound = false;
        juce::Array<juce::File> files;     // un fichier par dossier Tables de Serum trouvé
        juce::String error;
    };

    /** Dossiers « Tables » de Serum (Serum 1 et 2) sous Documents/Xfer. */
    static juce::Array<juce::File> findSerumTableFolders (const juce::File& documents);
    /** Où vont les tables : <Tables>/Phorm pour chaque Serum trouvé, sinon Documents/Phorm/Tables. */
    static juce::Array<juce::File> exportFolders (const juce::File& documents, bool& serumFound);

    /** Écrit la table en cours dans les dossiers d'export. */
    ExportResult exportCurrent();
    ExportResult exportCurrentTo (const juce::File& documents);

    /** Dernier export (pour l'interface). */
    ExportResult getLastExport() const;

    static juce::String msToText (float ms);

private:
    class Builder;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void requestBuild (const phorm::TableSettings&);
    void publish (std::shared_ptr<const phorm::Wavetable>);
    void collectGarbage();
    void pushHistory (const phorm::TableSettings&);
    void updateCurrent (const std::function<void (phorm::TableSettings&)>&);
    phorm::Synth::Params readParams() const;
    static juce::ValueTree settingsToTree (const phorm::TableSettings&);
    static phorm::TableSettings treeToSettings (const juce::ValueTree&);

    // Historique et choix (fil de l'interface ; getState peut venir d'un autre fil)
    mutable juce::CriticalSection stateLock;
    std::vector<phorm::TableSettings> history;
    int historyIndex = 0;
    int styleChoice = styleRandom;

    // Tables : `pending` (prête), `live` (jouée), `retired` (à libérer hors du fil audio)
    juce::SpinLock swapLock;
    std::shared_ptr<const phorm::Wavetable> pendingTable, liveTable, retiredTable;

    mutable juce::CriticalSection displayLock;
    std::shared_ptr<const phorm::Wavetable> displayTable;
    std::atomic<uint32_t> tableVersion { 0 };

    std::unique_ptr<Builder> builder;

    phorm::Synth synth;
    std::atomic<float> playedPosition { 0.0f };
    std::atomic<int> activeVoices { 0 };

    mutable juce::CriticalSection exportLock;
    ExportResult lastExport;

    std::atomic<float>* positionParam = nullptr;
    std::atomic<float>* scanParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* levelParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhormAudioProcessor)
};
