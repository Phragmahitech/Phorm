#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    float textToNumber (const juce::String& text)
    {
        return text.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue();
    }

    float textToTime (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
        return (t.endsWithChar ('s') && ! t.endsWith ("ms")) ? value * 1000.0f : value;
    }

    /** Plage exponentielle (temps, fréquences) : le milieu de la course est la moyenne géométrique. */
    juce::NormalisableRange<float> makeLogRange (float lowest, float highest, float step)
    {
        return { lowest, highest,
                 [] (float start, float end, float normalised)
                 {
                     return start * std::pow (end / start, juce::jlimit (0.0f, 1.0f, normalised));
                 },
                 [] (float start, float end, float value)
                 {
                     return juce::jlimit (0.0f, 1.0f, std::log (juce::jlimit (start, end, value) / start) / std::log (end / start));
                 },
                 [step] (float start, float end, float value)
                 {
                     return juce::jlimit (start, end, std::round (value / step) * step);
                 } };
    }

    std::unique_ptr<juce::AudioParameterFloat> makePercent (const char* id, const char* name, float defaultPercent)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), defaultPercent,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value)) + " %"; })
                                                                .withValueFromStringFunction (textToNumber));
    }

    std::unique_ptr<juce::AudioParameterFloat> makeTime (const char* id, const char* name, float lowest, float highest, float defaultMs)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            makeLogRange (lowest, highest, 0.1f), defaultMs,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float ms, int) { return PhormAudioProcessor::msToText (ms); })
                                                                .withValueFromStringFunction (textToTime));
    }
}

//==============================================================================
/** Fabrique les tables demandées, une à la fois (seule la dernière demande compte). */
class PhormAudioProcessor::Builder final : public juce::Thread
{
public:
    explicit Builder (PhormAudioProcessor& o) : juce::Thread ("Phorm table builder"), owner (o)
    {
        startThread (juce::Thread::Priority::low);
    }

    ~Builder() override
    {
        stopThread (5000);
    }

    void request (const phorm::TableSettings& s)
    {
        {
            const juce::ScopedLock lock (requestLock);
            wanted = s;
            hasRequest = true;
        }
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            phorm::TableSettings s;
            bool have = false;

            {
                const juce::ScopedLock lock (requestLock);
                if (hasRequest)
                {
                    s = wanted;
                    hasRequest = false;
                    have = true;
                }
            }

            if (have)
            {
                std::shared_ptr<const phorm::Wavetable> table (phorm::build (s));
                if (threadShouldExit())
                    break;
                owner.publish (std::move (table));
            }

            owner.collectGarbage();

            if (! have)
                wait (50);
        }
    }

private:
    PhormAudioProcessor& owner;
    juce::CriticalSection requestLock;
    phorm::TableSettings wanted;
    bool hasRequest = false;
};

//==============================================================================
juce::String PhormAudioProcessor::msToText (float ms)
{
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 2) + " s";
    if (ms < 10.0f)
        return juce::String (ms, 1) + " ms";

    return juce::String (juce::roundToInt (ms)) + " ms";
}

juce::AudioProcessorValueTreeState::ParameterLayout PhormAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makePercent (positionId, "Position", 0.0f));
    layout.add (makePercent (scanId, "Scan", 100.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { rateId, 1 }, "Scan rate",
                                                             makeLogRange (0.05f, 10.0f, 0.001f), 0.2f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float hz, int)
                                                                 {
                                                                     return juce::String (hz, hz < 1.0f ? 2 : 1) + " Hz";
                                                                 })
                                                                 .withValueFromStringFunction (textToNumber)));

    layout.add (makeTime (attackId, "Attack", 0.5f, 2000.0f, 2.0f));
    layout.add (makeTime (releaseId, "Release", 5.0f, 5000.0f, 200.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { levelId, 1 }, "Level",
                                                             juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), -6.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float db, int)
                                                                 {
                                                                     return (db > 0.04f ? "+" : "") + juce::String (db, 1) + " dB";
                                                                 })
                                                                 .withValueFromStringFunction (textToNumber)));
    return layout;
}

//==============================================================================
PhormAudioProcessor::PhormAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Phorm", createParameterLayout())
{
    positionParam = apvts.getRawParameterValue (positionId);
    scanParam     = apvts.getRawParameterValue (scanId);
    rateParam     = apvts.getRawParameterValue (rateId);
    attackParam   = apvts.getRawParameterValue (attackId);
    releaseParam  = apvts.getRawParameterValue (releaseId);
    levelParam    = apvts.getRawParameterValue (levelId);

    // Première table : prête tout de suite, pour qu'une note sonne dès l'ouverture
    phorm::TableSettings first;
    history.push_back (first);
    historyIndex = 0;

    std::shared_ptr<const phorm::Wavetable> table (phorm::build (first));
    liveTable = table;
    synth.setTable (liveTable.get());
    displayTable = std::move (table);
    tableVersion = 1;

    builder = std::make_unique<Builder> (*this);
}

PhormAudioProcessor::~PhormAudioProcessor()
{
    builder = nullptr;
}

phorm::Synth::Params PhormAudioProcessor::readParams() const
{
    phorm::Synth::Params p;
    p.position  = positionParam->load() * 0.01f;
    p.scan      = scanParam->load() * 0.01f;
    p.rateHz    = rateParam->load();
    p.attackMs  = attackParam->load();
    p.releaseMs = releaseParam->load();
    p.gain      = juce::Decibels::decibelsToGain (levelParam->load());
    return p;
}

void PhormAudioProcessor::prepareToPlay (double sampleRate, int)
{
    synth.prepare (sampleRate);
}

void PhormAudioProcessor::reset()
{
    synth.reset();
}

bool PhormAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.inputBuses.size() > 0 && ! layouts.getMainInputChannelSet().isDisabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PhormAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Nouvelle table : échange sans allouer ni libérer (la libération se fait dans le fil des tables)
    {
        const juce::SpinLock::ScopedTryLockType lock (swapLock);
        if (lock.isLocked() && pendingTable != nullptr && retiredTable == nullptr)
        {
            retiredTable = std::move (liveTable);
            liveTable = std::move (pendingTable);
            synth.setTable (liveTable.get());
        }
    }

    const int numSamples = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    buffer.clear();

    if (channels == 0)
        return;

    const auto params = readParams();
    float* left = buffer.getWritePointer (0);
    float* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;
    int position = 0;

    auto renderUpTo = [&] (int end)
    {
        if (end > position)
        {
            synth.render (left + position, right != nullptr ? right + position : nullptr, end - position, params);
            position = end;
        }
    };

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        renderUpTo (juce::jlimit (position, numSamples, metadata.samplePosition));

        if (message.isNoteOn())
            synth.noteOn (message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isNoteOff())
            synth.noteOff (message.getNoteNumber());
        else if (message.isAllNotesOff())
            synth.allNotesOff();
        else if (message.isAllSoundOff())
            synth.reset();
    }

    renderUpTo (numSamples);

    playedPosition.store (synth.getCurrentPosition(), std::memory_order_relaxed);
    activeVoices.store (synth.activeVoiceCount(), std::memory_order_relaxed);
}

void PhormAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    synth.reset();
    activeVoices.store (0, std::memory_order_relaxed);
}

//==============================================================================
void PhormAudioProcessor::requestBuild (const phorm::TableSettings& s)
{
    if (builder != nullptr)
        builder->request (s);
}

void PhormAudioProcessor::publish (std::shared_ptr<const phorm::Wavetable> table)
{
    std::shared_ptr<const phorm::Wavetable> oldPending, oldRetired;

    {
        const juce::SpinLock::ScopedLockType lock (swapLock);
        oldPending = std::move (pendingTable);
        pendingTable = table;
        oldRetired = std::move (retiredTable);
    }

    {
        const juce::ScopedLock lock (displayLock);
        displayTable = std::move (table);
    }

    ++tableVersion;
    // oldPending et oldRetired sont libérés ici, hors du fil audio
}

void PhormAudioProcessor::collectGarbage()
{
    std::shared_ptr<const phorm::Wavetable> old;
    const juce::SpinLock::ScopedLockType lock (swapLock);
    old = std::move (retiredTable);
}

std::shared_ptr<const phorm::Wavetable> PhormAudioProcessor::getDisplayTable() const
{
    const juce::ScopedLock lock (displayLock);
    return displayTable;
}

bool PhormAudioProcessor::waitForCurrentTable (int timeoutMs) const
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (juce::Time::getMillisecondCounter() < end)
    {
        if (auto t = getDisplayTable())
            if (t->settings == getCurrentSettings().sanitised())
                return true;

        juce::Thread::sleep (5);
    }

    return false;
}

//==============================================================================
void PhormAudioProcessor::pushHistory (const phorm::TableSettings& s)
{
    if (! history.empty())
        history.erase (history.begin() + historyIndex + 1, history.end());

    history.push_back (s.sanitised());

    if ((int) history.size() > maxHistory)
        history.erase (history.begin(), history.begin() + ((int) history.size() - maxHistory));

    historyIndex = (int) history.size() - 1;
}

void PhormAudioProcessor::generate()
{
    generateWithSeed ((uint32_t) juce::Random::getSystemRandom().nextInt (juce::Range<int> (100000, 1000000)));
}

void PhormAudioProcessor::generateWithSeed (uint32_t seed)
{
    phorm::TableSettings s;

    {
        const juce::ScopedLock lock (stateLock);
        s = history[(size_t) historyIndex];
        s.seed = seed;
        s.style = styleChoice == styleRandom ? phorm::randomStyleFor (seed) : styleChoice;
        pushHistory (s);
        s = history[(size_t) historyIndex];
    }

    requestBuild (s);
}

bool PhormAudioProcessor::stepHistory (int delta)
{
    phorm::TableSettings s;

    {
        const juce::ScopedLock lock (stateLock);
        const int next = historyIndex + delta;
        if (! juce::isPositiveAndBelow (next, (int) history.size()))
            return false;

        historyIndex = next;
        s = history[(size_t) historyIndex];
    }

    requestBuild (s);
    return true;
}

void PhormAudioProcessor::updateCurrent (const std::function<void (phorm::TableSettings&)>& change)
{
    phorm::TableSettings s;

    {
        const juce::ScopedLock lock (stateLock);
        auto& current = history[(size_t) historyIndex];
        const auto before = current;
        change (current);
        current = current.sanitised();
        if (current == before)
            return;
        s = current;
    }

    requestBuild (s);
}

void PhormAudioProcessor::setBright (float bright)   { updateCurrent ([bright] (phorm::TableSettings& s) { s.bright = bright; }); }
void PhormAudioProcessor::setMorph (int morph)       { updateCurrent ([morph] (phorm::TableSettings& s) { s.morph = morph; }); }
void PhormAudioProcessor::setFrames (int frames)     { updateCurrent ([frames] (phorm::TableSettings& s) { s.frames = frames; }); }

int PhormAudioProcessor::getStyleChoice() const
{
    const juce::ScopedLock lock (stateLock);
    return styleChoice;
}

void PhormAudioProcessor::setStyleChoice (int choice)
{
    const juce::ScopedLock lock (stateLock);
    styleChoice = juce::jlimit ((int) styleRandom, phorm::numStyles - 1, choice);
}

phorm::TableSettings PhormAudioProcessor::getCurrentSettings() const
{
    const juce::ScopedLock lock (stateLock);
    return history[(size_t) historyIndex];
}

int PhormAudioProcessor::getHistoryIndex() const
{
    const juce::ScopedLock lock (stateLock);
    return historyIndex;
}

int PhormAudioProcessor::getHistorySize() const
{
    const juce::ScopedLock lock (stateLock);
    return (int) history.size();
}

//==============================================================================
juce::Array<juce::File> PhormAudioProcessor::findSerumTableFolders (const juce::File& documents)
{
    juce::Array<juce::File> found;
    const auto xfer = documents.getChildFile ("Xfer");

    if (! xfer.isDirectory())
        return found;

    auto children = xfer.findChildFiles (juce::File::findDirectories, false);
    std::sort (children.begin(), children.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });

    for (const auto& dir : children)
    {
        const auto tables = dir.getChildFile ("Tables");
        if (tables.isDirectory())
            found.add (tables);
    }

    return found;
}

juce::Array<juce::File> PhormAudioProcessor::exportFolders (const juce::File& documents, bool& serumFound)
{
    juce::Array<juce::File> folders;

    for (const auto& tables : findSerumTableFolders (documents))
        folders.add (tables.getChildFile ("Phorm"));

    serumFound = ! folders.isEmpty();

    if (! serumFound)
        folders.add (documents.getChildFile ("Phorm").getChildFile ("Tables"));

    return folders;
}

PhormAudioProcessor::ExportResult PhormAudioProcessor::exportCurrent()
{
    return exportCurrentTo (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
}

PhormAudioProcessor::ExportResult PhormAudioProcessor::exportCurrentTo (const juce::File& documents)
{
    ExportResult result;
    const auto settings = getCurrentSettings().sanitised();

    std::shared_ptr<const phorm::Wavetable> table = getDisplayTable();
    if (table == nullptr || table->settings != settings)
        table = std::shared_ptr<const phorm::Wavetable> (phorm::build (settings));

    const auto bytes = phorm::toWav (*table);
    const auto fileName = juce::String (phorm::fileStem (settings)) + ".wav";

    for (const auto& folder : exportFolders (documents, result.serumFound))
    {
        const auto file = folder.getChildFile (fileName);

        if (folder.createDirectory().failed() || ! file.replaceWithData (bytes.data(), bytes.size()))
        {
            result.error = "COULD NOT WRITE " + file.getFullPathName();
            continue;
        }

        result.files.add (file);
    }

    result.ok = ! result.files.isEmpty() && result.error.isEmpty();

    const juce::ScopedLock lock (exportLock);
    lastExport = result;
    return result;
}

PhormAudioProcessor::ExportResult PhormAudioProcessor::getLastExport() const
{
    const juce::ScopedLock lock (exportLock);
    return lastExport;
}

//==============================================================================
juce::ValueTree PhormAudioProcessor::settingsToTree (const phorm::TableSettings& s)
{
    juce::ValueTree t ("Table");
    t.setProperty ("style", phorm::styleSlug (s.style), nullptr);
    t.setProperty ("seed", (juce::int64) s.seed, nullptr);
    t.setProperty ("frames", s.frames, nullptr);
    t.setProperty ("morph", s.morph, nullptr);
    t.setProperty ("bright", (double) s.bright, nullptr);
    return t;
}

phorm::TableSettings PhormAudioProcessor::treeToSettings (const juce::ValueTree& t)
{
    phorm::TableSettings s;
    const auto styleText = t.getProperty ("style").toString();
    for (int i = 0; i < phorm::numStyles; ++i)
        if (styleText == phorm::styleSlug (i))
            s.style = i;

    const auto seed = (juce::int64) t.getProperty ("seed", (juce::int64) s.seed);
    if (seed >= 0 && seed <= (juce::int64) 0xffffffffu)
        s.seed = (uint32_t) seed;

    s.frames = (int) t.getProperty ("frames", s.frames);
    s.morph = (int) t.getProperty ("morph", s.morph);
    s.bright = (float) (double) t.getProperty ("bright", (double) s.bright);
    return s.sanitised();
}

void PhormAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Generator"), nullptr);

    juce::ValueTree generator ("Generator");
    {
        const juce::ScopedLock lock (stateLock);
        generator.setProperty ("styleChoice", styleChoice == styleRandom ? juce::String ("random") : juce::String (phorm::styleSlug (styleChoice)), nullptr);
        generator.setProperty ("index", historyIndex, nullptr);
        for (const auto& s : history)
            generator.appendChild (settingsToTree (s), nullptr);
    }
    state.appendChild (generator, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PhormAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto generator = tree.getChildWithName ("Generator").createCopy();
    tree.removeChild (tree.getChildWithName ("Generator"), nullptr);
    apvts.replaceState (tree);

    if (! generator.isValid())
        return;

    std::vector<phorm::TableSettings> restored;
    for (const auto& child : generator)
        if (child.hasType ("Table"))
            restored.push_back (treeToSettings (child));

    if ((int) restored.size() > maxHistory)
        restored.erase (restored.begin(), restored.begin() + ((int) restored.size() - maxHistory));

    phorm::TableSettings current;

    {
        const juce::ScopedLock lock (stateLock);

        const auto choiceText = generator.getProperty ("styleChoice").toString();
        styleChoice = styleRandom;
        for (int i = 0; i < phorm::numStyles; ++i)
            if (choiceText == phorm::styleSlug (i))
                styleChoice = i;

        if (! restored.empty())
        {
            history = std::move (restored);
            historyIndex = juce::jlimit (0, (int) history.size() - 1, (int) generator.getProperty ("index", (int) history.size() - 1));
        }

        current = history[(size_t) historyIndex];
    }

    requestBuild (current);
}

juce::AudioProcessorEditor* PhormAudioProcessor::createEditor()
{
    return new PhormAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhormAudioProcessor();
}
