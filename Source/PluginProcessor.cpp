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

    // SHAPER
    p.push_back (std::make_unique<C> (ParameterID { "res", 1 }, "Resolution",
                                      StringArray { "LIVE (zero latency)", "513 bands", "1025 bands", "2049 bands", "4097 bands", "8193 bands" }, 2));
    p.push_back (std::make_unique<C> (ParameterID { "target", 1 }, "Target", StringArray { "Sibilants", "Full Signal" }, 1));
    fl ("start",   "Start",   skewed (20.0f, 20000.0f, 1000.0f), 120.0f, hz);
    fl ("end",     "End",     skewed (20.0f, 20000.0f, 1000.0f), 16000.0f, hz);
    fl ("slope",   "Slope",   skewed (6.0f, 96.0f, 24.0f), 24.0f, dbo);
    fl ("amount",  "Amount",  { 0.0f, 100.0f, 0.1f }, 70.0f, pct);
    fl ("makeup",  "Makeup",  { -24.0f, 24.0f, 0.1f }, 0.0f, db);
    fl ("attack",  "Attack",  skewed (0.1f, 500.0f, 20.0f), 10.0f, ms);
    fl ("release", "Release", skewed (1.0f, 2000.0f, 150.0f), 120.0f, ms);
    fl ("detail",  "Detail",  { 0.0f, 100.0f, 0.1f }, 40.0f, pct);
    fl ("dynamic", "Dynamic", { 0.0f, 100.0f, 0.1f }, 50.0f, pct);
    fl ("formant", "Formant", { -12.0f, 12.0f, 0.1f }, 0.0f, st);

    // SOURCE
    p.push_back (std::make_unique<C> (ParameterID { "source", 1 }, "Source", StringArray { "Noise", "File Profile", "File Follow" }, 2));
    {
        StringArray noises;
        for (int i = 0; i < wsk::kNumNoises; ++i) noises.add (wsk::noiseName (i));
        p.push_back (std::make_unique<C> (ParameterID { "noise", 1 }, "Noise", noises, 1));
    }
    fl ("rate",    "Follow Rate",   skewed (10.0f, 400.0f, 100.0f), 100.0f, pct);
    fl ("smooth",  "Follow Smooth", skewed (1.0f, 2000.0f, 80.0f), 60.0f, ms);
    p.push_back (std::make_unique<B> (ParameterID { "freeze", 1 }, "Freeze", false));

    // DETECT
    p.push_back (std::make_unique<B> (ParameterID { "detS", 1 }, "Detect S/Z/SH", true));
    p.push_back (std::make_unique<B> (ParameterID { "detF", 1 }, "Detect F/V", true));
    p.push_back (std::make_unique<B> (ParameterID { "detC", 1 }, "Detect CH/J", true));
    fl ("sensS", "Sens S", { 0.0f, 100.0f, 0.1f }, 50.0f, pct);
    fl ("sensF", "Sens F", { 0.0f, 100.0f, 0.1f }, 50.0f, pct);
    fl ("sensC", "Sens C", { 0.0f, 100.0f, 0.1f }, 50.0f, pct);
    fl ("hold",  "Hold",   skewed (5.0f, 250.0f, 40.0f), 40.0f, ms);
    fl ("guard", "Vowel Guard", { 0.0f, 100.0f, 0.1f }, 60.0f, pct);

    // piede
    fl ("mix",    "Mix",    { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
    fl ("output", "Output", { -24.0f, 24.0f, 0.1f }, 0.0f, db);
    p.push_back (std::make_unique<B> (ParameterID { "delta", 1 },  "Delta", false));
    p.push_back (std::make_unique<B> (ParameterID { "listen", 1 }, "Consonants Only", false));

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
    rebuildReference();
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
    rebuildReference(); // il timbro medio va ricalcolato alla nuova risoluzione
}

void WShaperProcessor::readParams (wsk::Params& p)
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    p.nsOn        = true;
    p.nsFull      = v ("target") > 0.5f;
    p.nsNoise     = (int) v ("noise");
    p.nsStart     = v ("start");
    p.nsEnd       = v ("end");
    p.nsSlope     = v ("slope");
    p.nsAmount    = v ("amount") * 0.01f;
    p.nsMakeupDb  = v ("makeup");
    p.nsAttackMs  = v ("attack");
    p.nsReleaseMs = v ("release");
    p.nsSpeedMs   = 8.0f; // analisi dell'ingresso: la velocita' la decidono ATTACK e RELEASE
    p.nsDetail    = v ("detail") * 0.01f;
    p.nsDynamic   = v ("dynamic") * 0.01f;
    p.nsFormantSt = v ("formant");
    p.nsSource    = (int) v ("source");
    p.nsRate      = v ("rate") * 0.01f;
    p.nsSmoothMs  = v ("smooth");
    p.nsFreeze    = v ("freeze") > 0.5f;

    p.enS = v ("detS") > 0.5f; p.enF = v ("detF") > 0.5f; p.enC = v ("detC") > 0.5f;
    p.sensS = v ("sensS") * 0.01f; p.sensF = v ("sensF") * 0.01f; p.sensC = v ("sensC") * 0.01f;
    p.holdMs = v ("hold");
    p.guard  = v ("guard") * 0.01f;
    p.link   = 1.0f;

    p.mix    = v ("mix") * 0.01f;
    p.delta  = v ("delta") > 0.5f;
    p.listen = v ("listen") > 0.5f;
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
        engine->setReference (r != nullptr ? &r->audio : nullptr,
                              (r != nullptr && r->profileN == engine->fftSize()) ? &r->profile : nullptr);
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
    {
        const ScopedLock sl (refLock);
        refSource = std::move (b);
        refSourceRate = reader->sampleRate;
        refName = file.getFileName();
    }
    apvts.state.setProperty ("referencePath", file.getFullPathName(), nullptr);
    rebuildReference();
    return true;
}

void WShaperProcessor::clearReference()
{
    {
        const ScopedLock sl (refLock);
        refSource.setSize (0, 0);
        refName = {};
    }
    apvts.state.setProperty ("referencePath", String(), nullptr);
    currentRef.store (nullptr);
    ++refVersion;
}

void WShaperProcessor::rebuildReference()
{
    const ScopedLock sl (refLock);
    if (refSource.getNumSamples() == 0 || refSourceRate <= 0.0) return;

    auto r = std::make_unique<RefData>();
    auto& t = r->audio;
    const double ratio = refSourceRate / currentSR;
    const int outLen = jmax (1, (int) std::floor (refSource.getNumSamples() / ratio));
    t.numCh = refSource.getNumChannels();
    t.length = outLen;
    t.sampleRate = currentSR;
    t.name = refName.toStdString();
    float peak = 0.0f;
    for (int c = 0; c < t.numCh; ++c)
    {
        t.ch[c].assign ((size_t) outLen, 0.0f);
        LagrangeInterpolator interp;
        interp.process (ratio, refSource.getReadPointer (c), t.ch[c].data(), outLen);
        for (auto s : t.ch[c]) peak = jmax (peak, std::abs (s));
    }
    t.overview.assign (512, 0.0f);
    for (int i = 0; i < 512; ++i)
    {
        const int a = (int) ((int64) i * outLen / 512), b = jmax (a + 1, (int) ((int64) (i + 1) * outLen / 512));
        float m = 0.0f;
        for (int c = 0; c < t.numCh; ++c)
            for (int k = a; k < b && k < outLen; ++k) m = jmax (m, std::abs (t.ch[c][(size_t) k]));
        t.overview[(size_t) i] = peak > 0.0f ? m / peak : 0.0f;
    }

    // timbro medio: media delle potenze dei frame che non sono silenzio (fino a 40 dB sotto il frame piu' forte)
    const int N = wsk::Engine::fftSizeFor (currentSR, currentRes());
    const int K = N / 2 + 1;
    r->profileN = N;
    r->profile.assign ((size_t) K, -120.0f);
    if (outLen >= N)
    {
        wsk::FFT fft;
        fft.init (N);
        std::vector<wsk::cpx> buf ((size_t) N);
        std::vector<float> win ((size_t) N);
        for (int j = 0; j < N; ++j) win[(size_t) j] = 0.5f - 0.5f * std::cos (2.0f * wsk::kPi * (float) j / (float) N);
        const int hop = N / 2;
        const int frames = jmin (4000, (outLen - N) / hop + 1);
        const int step = jmax (1, ((outLen - N) / hop + 1) / frames);
        std::vector<std::vector<float>> spec;
        std::vector<float> lvl;
        for (int f = 0; f < frames; ++f)
        {
            const int s0 = f * step * hop;
            if (s0 + N > outLen) break;
            for (int j = 0; j < N; ++j)
            {
                float x = 0.0f;
                for (int c = 0; c < t.numCh; ++c) x += t.ch[c][(size_t) (s0 + j)];
                buf[(size_t) j] = { x / (float) t.numCh * win[(size_t) j], 0.0f };
            }
            fft.transform (buf.data(), false);
            std::vector<float> pw ((size_t) K);
            double tot = 0.0;
            for (int k = 0; k < K; ++k) { pw[(size_t) k] = std::norm (buf[(size_t) k]); tot += pw[(size_t) k]; }
            spec.push_back (std::move (pw));
            lvl.push_back ((float) tot);
        }
        const float mx = lvl.empty() ? 0.0f : *std::max_element (lvl.begin(), lvl.end());
        std::vector<double> acc ((size_t) K, 0.0);
        int used = 0;
        for (size_t f = 0; f < spec.size(); ++f)
            if (lvl[f] > mx * 1.0e-4f)
            {
                for (int k = 0; k < K; ++k) acc[(size_t) k] += spec[f][(size_t) k];
                ++used;
            }
        const float pnorm = 16.0f / ((float) N * (float) N);
        if (used > 0)
            for (int k = 0; k < K; ++k)
                r->profile[(size_t) k] = wsk::powToDb ((float) (acc[(size_t) k] / used) * pnorm);
    }

    currentRef.store (r.get());
    refStore.push_back (std::move (r));
    while (refStore.size() > 2) refStore.erase (refStore.begin());
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
    static const StringArray keep { "delta", "listen", "output", "res", "mix" };
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
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void WShaperProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            const String path = apvts.state.getProperty ("referencePath", String()).toString();
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
