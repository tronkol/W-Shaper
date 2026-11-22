#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;

namespace
{
String pct (float v, int)  { return String (roundToInt (v)) + " %"; }
String ms (float v, int)   { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; }
String db (float v, int)   { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; }
String dbo (float v, int)  { return String (roundToInt (v)) + " dB/oct"; }
String st (float v, int)   { return (v > 0.05f ? "+" : "") + String (v, 1) + " st"; }
String hz (float v, int)   { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz"; }

NormalisableRange<float> skewed (float lo, float hi, float centre)
{
    NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}
}

AudioProcessorValueTreeState::ParameterLayout WShaperProcessor::createLayout()
{
    using F = AudioParameterFloat;
    using B = AudioParameterBool;
    using C = AudioParameterChoice;
    using A = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto fl = [&p] (const char* id, const char* name, NormalisableRange<float> r, float def, String (*fmt) (float, int))
    {
        p.push_back (std::make_unique<F> (ParameterID { id, 1 }, name, r, def, A().withStringFromValueFunction (fmt)));
    };

    // TIMBRE
    p.push_back (std::make_unique<C> (ParameterID { "res", 1 }, "Resolution",
                                      StringArray { "LIVE (zero latency)", "513 bands", "1025 bands", "2049 bands", "4097 bands", "8193 bands" }, 2));
    fl ("start",    "Start",    skewed (20.0f, 20000.0f, 1000.0f), 80.0f, hz);
    fl ("end",      "End",      skewed (20.0f, 20000.0f, 1000.0f), 16000.0f, hz);
    fl ("slope",    "Slope",    skewed (6.0f, 96.0f, 24.0f), 24.0f, dbo);
    fl ("amount",   "Amount",   { 0.0f, 100.0f, 0.1f }, 90.0f, pct);
    fl ("makeup",   "Makeup",   { -24.0f, 24.0f, 0.1f }, 0.0f, db);
    fl ("attack",   "Attack",   skewed (0.1f, 500.0f, 20.0f), 10.0f, ms);
    fl ("release",  "Release",  skewed (1.0f, 2000.0f, 150.0f), 60.0f, ms);
    fl ("detail",   "Detail",   { 0.0f, 100.0f, 0.1f }, 60.0f, pct);
    fl ("match",    "Match",    { 0.0f, 100.0f, 0.1f }, 85.0f, pct);
    fl ("loudness", "Loudness", { 0.0f, 100.0f, 0.1f }, 30.0f, pct);
    fl ("formant",  "Formant",  { -12.0f, 12.0f, 0.1f }, 0.0f, st);
    fl ("smooth",   "Smooth",   skewed (1.0f, 1000.0f, 60.0f), 30.0f, ms);
    fl ("dynamic",  "Gate",     { 0.0f, 100.0f, 0.1f }, 50.0f, pct);

    // piede
    fl ("mix",    "Mix",    { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
    fl ("output", "Output", { -24.0f, 24.0f, 0.1f }, 0.0f, db);
    p.push_back (std::make_unique<B> (ParameterID { "delta", 1 },  "Delta", false));

    return { p.begin(), p.end() };
}

//==============================================================================
WShaperProcessor::WShaperProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "WSHAPER", createLayout())
{
    formats.registerBasicFormats();
    engine = std::make_unique<wsk::Engine> (visData);
    engine->prepare (48000.0, currentRes());
    apvts.addParameterListener ("res", this);
}

WShaperProcessor::~WShaperProcessor()
{
    apvts.removeParameterListener ("res", this);
    cancelPendingUpdate();
    alive.reset();
}

bool WShaperProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

void WShaperProcessor::prepareToPlay (double sampleRate, int)
{
    currentSR = sampleRate;
    {
        auto e = std::make_unique<wsk::Engine> (visData);
        e->prepare (sampleRate, currentRes());
        const ScopedLock sl (engineLock);
        engine = std::move (e);
    }
    setLatencySamples (engine->latency());
    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
    requestRebuild(); // Logic puo' chiamare prepareToPlay da un altro thread: il modello si ricostruisce sul thread dei messaggi
}

void WShaperProcessor::requestRebuild()
{
    if (MessageManager::existsAndIsCurrentThread()) { rebuildReference(); return; }
    std::weak_ptr<bool> w = alive;
    MessageManager::callAsync ([this, w] { if (! w.expired()) rebuildReference(); });
}

void WShaperProcessor::handleAsyncUpdate()
{
    const int r = currentRes();
    if (engine != nullptr && engine->resolution() == r) return;
    auto e = std::make_unique<wsk::Engine> (visData);
    e->prepare (currentSR, r);
    {
        const ScopedLock sl (engineLock);
        std::swap (engine, e);
    }
    setLatencySamples (engine->latency());
    rebuildReference(); // il modello va ricalcolato alla nuova risoluzione
}

void WShaperProcessor::readParams (wsk::Params& p)
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    p.nsOn        = true;
    p.nsFull      = true;
    p.nsStart     = v ("start");
    p.nsEnd       = v ("end");
    p.nsSlope     = v ("slope");
    p.nsAmount    = v ("amount") * 0.01f;
    p.nsMakeupDb  = v ("makeup");
    p.nsAttackMs  = v ("attack");
    p.nsReleaseMs = v ("release");
    p.nsSpeedMs   = 8.0f;
    p.nsDetail    = v ("detail") * 0.01f;
    p.nsDynamic   = v ("dynamic") * 0.01f;
    p.nsFormantSt = v ("formant");
    p.nsSmoothMs  = v ("smooth");
    p.tmMatch     = v ("match") * 0.01f;
    p.tmLoud      = v ("loudness") * 0.01f;
    p.link        = 1.0f;
    p.enS = p.enF = p.enC = false;

    p.mix    = v ("mix") * 0.01f;
    p.delta  = v ("delta") > 0.5f;
    p.listen = false;
    p.texOn  = false;
}

void WShaperProcessor::run (AudioBuffer<float>& buffer, bool bypass)
{
    ScopedNoDenormals noDenormals;
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();
    for (int c = nIn; c < nOut; ++c) buffer.clear (c, 0, buffer.getNumSamples());

    wsk::Params p;
    readParams (p);
    p.bypass = bypass;

    const int nc = jmin (2, buffer.getNumChannels());
    if (nc == 0) return;
    {
        const ScopedTryLock sl (engineLock);
        if (! sl.isLocked() || engine == nullptr) { buffer.clear(); return; }
        const RefData* r = currentRef.load();
        engine->setParams (p);
        engine->setModel (r != nullptr ? &r->model : nullptr);
        engine->process (buffer.getArrayOfWritePointers(), nc, buffer.getNumSamples());
    }

    outGain.setTargetValue (bypass ? 1.0f : Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));
    if (outGain.isSmoothing() || std::abs (outGain.getTargetValue() - 1.0f) > 1.0e-6f)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float g = outGain.getNextValue();
            for (int c = 0; c < nc; ++c) buffer.getWritePointer (c)[i] *= g;
        }
}

void WShaperProcessor::processBlock (AudioBuffer<float>& b, MidiBuffer&)         { run (b, false); }
void WShaperProcessor::processBlockBypassed (AudioBuffer<float>& b, MidiBuffer&) { run (b, true); }

//==============================================================================
// RIFERIMENTO DA FILE
bool WShaperProcessor::loadReference (const File& file)
{
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;
    const int maxLen = (int) jmin<int64> (reader->lengthInSamples, (int64) (reader->sampleRate * 300.0)); // max 5 minuti
    AudioBuffer<float> b ((int) jmin<unsigned int> (2u, reader->numChannels), maxLen);
    reader->read (&b, 0, maxLen, 0, true, b.getNumChannels() > 1);
    std::vector<float> mono ((size_t) maxLen, 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < maxLen; ++i) mono[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
    {
        const ScopedLock sl (refLock);
        refSource = std::move (mono);
        refSourceRate = reader->sampleRate;
        refName = file.getFileName();
        refPath = file.getFullPathName();
    }
    rebuildReference();
    return true;
}

void WShaperProcessor::clearReference()
{
    {
        const ScopedLock sl (refLock);
        refSource.clear();
        refName = {};
        refPath = {};
    }
    currentRef.store (nullptr);
    ++refVersion;
}

void WShaperProcessor::rebuildReference()
{
    jassert (MessageManager::existsAndIsCurrentThread());
    const ScopedLock sl (refLock);
    if (refSource.empty() || refSourceRate <= 0.0) return;

    auto r = std::make_unique<RefData>();
    auto& t = r->audio;
    const double ratio = refSourceRate / currentSR;
    const int inLen = (int) refSource.size();
    const int outLen = jmax (1, (int) std::floor ((double) (inLen - 8) / ratio));
    std::vector<float> mono ((size_t) outLen, 0.0f);
    {
        // copia con un po' di zeri in coda: l'interpolatore non legge mai oltre la fine
        std::vector<float> padded (refSource);
        padded.resize (refSource.size() + 64, 0.0f);
        LagrangeInterpolator interp;
        interp.process (ratio, padded.data(), mono.data(), outLen);
    }
    t.numCh = 1;
    t.length = outLen;
    t.sampleRate = currentSR;
    t.name = refName.toStdString();
    float peak = 0.0f;
    for (auto v : mono) peak = jmax (peak, std::abs (v));
    t.overview.assign (512, 0.0f);
    for (int i = 0; i < 512; ++i)
    {
        const int a = (int) ((int64) i * outLen / 512), b = jmax (a + 1, (int) ((int64) (i + 1) * outLen / 512));
        float m = 0.0f;
        for (int k = a; k < b && k < outLen; ++k) m = jmax (m, std::abs (mono[(size_t) k]));
        t.overview[(size_t) i] = peak > 0.0f ? m / peak : 0.0f;
    }
    // modello del timbro alla risoluzione attuale; l'audio non resta in memoria (basta il modello)
    r->model = wsk::buildTimbreModel (mono.data(), outLen, (float) currentSR, wsk::Engine::fftSizeFor (currentSR, currentRes()));

    currentRef.store (r.get());
    refStore.push_back (std::move (r));
    // i modelli vecchi restano vivi ancora un po': li puo' usare l'audio o l'interfaccia
    while (refStore.size() > 6) refStore.erase (refStore.begin());
    ++refVersion;
}

//==============================================================================
int WShaperProcessor::getNumPrograms() { return (int) wsp::presets().size(); }

const String WShaperProcessor::getProgramName (int index)
{
    const auto& l = wsp::presets();
    return isPositiveAndBelow (index, (int) l.size()) ? String (l[(size_t) index].name) : String();
}

void WShaperProcessor::setCurrentProgram (int index) { applyPreset (index); }

void WShaperProcessor::applyPreset (int index)
{
    const auto& l = wsp::presets();
    if (! isPositiveAndBelow (index, (int) l.size())) return;
    currentPreset = index;
    static const StringArray keep { "delta", "output", "res", "mix" };
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
            if (! keep.contains (r->getParameterID()))
                r->setValueNotifyingHost (r->getDefaultValue());
    for (const auto& kv : l[(size_t) index].values)
        if (auto* r = apvts.getParameter (kv.first))
            r->setValueNotifyingHost (r->convertTo0to1 (kv.second));
}

void WShaperProcessor::getStateInformation (MemoryBlock& destData)
{
    // le informazioni dell'interfaccia vengono aggiunte alla COPIA dello stato: nessuno scrive nello stato
    // condiviso da thread diversi (Logic salva da un suo thread)
    auto state = apvts.copyState();
    {
        const ScopedLock sl (refLock);
        state.setProperty ("referencePath", refPath, nullptr);
    }
    state.setProperty ("skin", uiSkin.load(), nullptr);
    state.setProperty ("uiPage", uiPage.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void WShaperProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto tree = ValueTree::fromXml (*xml);
            uiSkin = (int) tree.getProperty ("skin", 0);
            uiPage = (int) tree.getProperty ("uiPage", 0);
            const String path = tree.getProperty ("referencePath", String()).toString();
            apvts.replaceState (tree);
            if (path.isNotEmpty())
            {
                std::weak_ptr<bool> w = alive;
                MessageManager::callAsync ([this, w, path]
                {
                    if (w.expired()) return;
                    const File f (path);
                    if (f.existsAsFile()) loadReference (f);
                });
            }
        }
}

AudioProcessorEditor* WShaperProcessor::createEditor() { return new WShaperEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WShaperProcessor(); }
