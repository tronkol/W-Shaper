// W-SHAPER - motore DSP (derivato da SMOKER) (C++17 puro, senza JUCE: testabile ovunque)
//
//  STFT  : Hann, overlap 8x, 2048 punti a 44.1/48 kHz (4096 a 88/96, 8192 sopra)
//          -> 1025 bande indipendenti (2049 / 4097 alle frequenze alte)
//  DETECT: classificatore per frame (S/Z/SC, F/V, C/G dolci) sul mid, con lookahead
//  DYN   : spectral dynamics per banda, tipo soothe: picco rispetto all'inviluppo
//          locale (DETAIL) + energia larga rispetto allo spettro delle vocali (BROAD).
//          Nessun limite di gain reduction.
//  TEX   : texture caricata da file, rimodellata sullo spettro tolto.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace wsk
{
using cpx = std::complex<float>;
constexpr float kPi = 3.14159265358979f;

inline float dbToLin (float db)  { return std::pow (10.0f, db * 0.05f); }
inline float powToDb (float p)   { return 10.0f * std::log10 (p + 1.0e-20f); }
inline float ramp (float x, float a, float b)
{
    const float t = (x - a) / (b - a);
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}
inline float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float coefFor (float ms, float periodSec)
{
    if (ms <= 0.0f) return 1.0f;
    return 1.0f - std::exp (-periodSec / (ms * 0.001f));
}

//==============================================================================
class FFT
{
public:
    void init (int size)
    {
        n = size;
        tw.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / n;
            tw[(size_t) k] = { (float) std::cos (a), (float) std::sin (a) };
        }
        rev.resize ((size_t) n);
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    // in place, non normalizzata
    void transform (cpx* d, bool inverse) const
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i]) std::swap (d[i], d[rev[(size_t) i]]);

        float* f = reinterpret_cast<float*> (d);
        const float sg = inverse ? -1.0f : 1.0f;
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const cpx w = tw[(size_t) (j * step)];
                    const float wr = w.real(), wi = sg * w.imag();
                    float* a = f + 2 * (i + j);
                    float* b = f + 2 * (i + j + half);
                    const float vr = b[0] * wr - b[1] * wi;
                    const float vi = b[0] * wi + b[1] * wr;
                    b[0] = a[0] - vr; b[1] = a[1] - vi;
                    a[0] += vr;       a[1] += vi;
                }
        }
    }

    int size() const { return n; }

private:
    int n = 0;
    std::vector<cpx> tw;
    std::vector<int> rev;
};

//==============================================================================
struct TextureData
{
    std::vector<float> ch[2];  // gia' al sample rate dell'host
    int numCh = 0;
    int length = 0;
    double sampleRate = 0.0;
    std::string name;
    std::vector<float> overview; // 512 picchi per la forma d'onda in UI
};

struct FocusNode
{
    bool on = false;
    float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
};

enum NodeType { LowCut = 0, LowShelf, Bell1, Bell2, HighShelf, HighCut, kNumNodes };

// curva di focus in dB (0 = neutro). Pubblica: la usa anche l'interfaccia.
inline float focusDb (const std::array<FocusNode, kNumNodes>& nodes, float f)
{
    float db = 0.0f;
    for (int i = 0; i < kNumNodes; ++i)
    {
        const auto& nd = nodes[(size_t) i];
        if (! nd.on) continue;
        const float r = f / std::max (1.0f, nd.freq);
        switch (i)
        {
            case LowCut:    db += 10.0f * std::log10 (1.0f / (1.0f + std::pow (1.0f / std::max (r, 1.0e-4f), 8.0f))); break;
            case HighCut:   db += 10.0f * std::log10 (1.0f / (1.0f + std::pow (r, 8.0f))); break;
            case LowShelf:  db += nd.gainDb / (1.0f + r * r); break;
            case HighShelf: db += nd.gainDb / (1.0f + 1.0f / std::max (r * r, 1.0e-8f)); break;
            default:
            {
                const float oct = std::log2 (std::max (r, 1.0e-6f));
                const float bw  = 1.0f / std::max (0.1f, nd.q);
                db += nd.gainDb * std::exp (-0.5f * (oct / (0.5f * bw)) * (oct / (0.5f * bw)));
            }
        }
    }
    return std::max (-60.0f, db);
}

// Rumori di riferimento per N-SHAPER: densita' di potenza relativa in dB (la forma, non il livello)
constexpr int kNumNoises = 10;
inline const char* noiseName (int i)
{
    static const char* n[kNumNoises] = { "WHITE", "PINK", "BROWN", "BLUE", "VIOLET", "GREY", "BREATH", "SILK", "SMOKE", "TAPE" };
    return n[std::clamp (i, 0, kNumNoises - 1)];
}
inline float noiseTargetDb (int type, float f)
{
    f = std::max (f, 10.0f);
    const float o = std::log10 (f / 1000.0f);
    auto gauss = [] (float x, float w) { return std::exp (-0.5f * (x / w) * (x / w)); };
    switch (type)
    {
        case 0: return 0.0f;                                   // WHITE: piatto
        case 1: return -10.0f * o;                             // PINK: -3 dB/ott
        case 2: return -20.0f * o;                             // BROWN: -6 dB/ott
        case 3: return 10.0f * o;                              // BLUE: +3 dB/ott
        case 4: return 20.0f * o;                              // VIOLET: +6 dB/ott
        case 5:                                                // GREY: inverso della curva A (piatto all'orecchio)
        {
            const double f2 = (double) f * f;
            const double ra = (148693636.0 * f2 * f2)
                            / ((f2 + 424.36) * std::sqrt ((f2 + 11599.29) * (f2 + 544496.41)) * (f2 + 148693636.0));
            return -(float) (20.0 * std::log10 (ra) + 2.0);
        }
        case 6: return -10.0f * o + 8.0f * gauss (std::log2 (f / 2500.0f), 0.8f);   // BREATH: rosa con la gobba dell'aspirazione
        case 7: return -5.0f * o - 10.0f * std::log10 (1.0f + std::pow (f / 10000.0f, 4.0f)); // SILK: quasi piatto, aria morbida
        case 8: return -20.0f * o + 10.0f * std::log10 (1.0f + (f / 4000.0f) * (f / 4000.0f)); // SMOKE: scuro, ma con fruscio piatto sopra 4k
        default: return 3.0f * gauss (std::log2 (f / 6000.0f), 0.7f) - 10.0f * std::log10 (1.0f + std::pow (f / 14000.0f, 6.0f)); // TAPE
    }
}

// peso della banda di N-SHAPER (0..1) con i confini a pendenza SLOPE
inline float nsBandWeight (float f, float start, float end, float slopeDbOct)
{
    if (end < start) std::swap (start, end);
    float oct = 0.0f;
    if (f < start)    oct = std::log2 (start / std::max (f, 1.0f));
    else if (f > end) oct = std::log2 (f / end);
    return std::pow (10.0f, -slopeDbOct * oct / 20.0f);
}

struct Params
{
    // SMOKER
    float depth = 0.6f;     // 0..2  (1 = picchi riportati esattamente all'inviluppo)
    float detail = 0.5f;    // 0..1
    float attackMs = 3.0f, releaseMs = 60.0f;
    float broad = 0.5f;     // 0..1
    float threshDb = -6.0f; // soglia di BROAD rispetto allo spettro delle vocali
    float makeupDb = 0.0f;  // solo sulle consonanti rilevate
    float link = 1.0f;
    bool  soft = true;
    float mix = 1.0f;
    bool  delta = false, listen = false, bypass = false;
    std::array<FocusNode, kNumNodes> nodes {};

    // DETECT
    bool  enS = true, enF = true, enC = true;
    float sensS = 0.5f, sensF = 0.5f, sensC = 0.5f;
    float holdMs = 40.0f;
    float guard = 0.6f;

    // TEXTURE
    bool  texOn = false;
    float texVolDb = 0.0f, texMix = 0.5f, texAttMs = 5.0f, texRelMs = 80.0f, texSpectral = 1.0f;
    float texHP = 20.0f, texLP = 20000.0f; // filtri della texture (24 dB/ott)
    float texFollow = 0.0f;                 // 0..1: quanto i filtri inseguono la banda della consonante

    // N-SHAPER: algoritmo alternativo, porta lo spettro della banda verso la forma di un rumore
    bool  nsOn = false;
    bool  nsFull = false;        // false = solo consonanti rilevate, true = tutto il segnale
    int   nsNoise = 1;           // vedi noiseTargetDb
    float nsStart = 3000.0f, nsEnd = 12000.0f;
    float nsSlope = 24.0f;       // dB/ottava ai due confini
    float nsAmount = 0.6f;       // 0..1
    float nsMakeupDb = 0.0f;     // solo nella banda
    float nsSpeedMs = 15.0f;
    float nsDetail = 0.5f;       // 0..1
    // W-SHAPER: riferimento da file
    int   nsSource = 0;          // 0 NOISE, 1 FILE PROFILE (timbro medio), 2 FILE FOLLOW (timbro del momento, il file scorre)
    float nsFormantSt = 0.0f;    // sposta il timbro di riferimento (semitoni)
    float nsRate = 1.0f;         // velocita' di scorrimento del file in FOLLOW
    float nsSmoothMs = 60.0f;    // morbidezza dei cambi di timbro in FOLLOW
    bool  nsFreeze = false;      // FOLLOW: blocca il timbro attuale
    float nsAttackMs = 10.0f, nsReleaseMs = 120.0f; // velocita' della correzione
    float nsDynamic = 0.0f;      // 0..1: quanto l'intervento segue la presenza del segnale nella banda
    int   texMode = 0;  // 0 HOLD (riprende da dove era), 1 RETRIG (riparte da capo), 2 LOOP (gira sempre, il detector apre il cancello)
};

enum DetClass { None = 0, Sib, Fric, Affr };

// Dati per l'interfaccia, scritti dall'audio thread (lock-free)
struct Vis
{
    static constexpr int kPts = 240;
    std::array<std::atomic<float>, kPts> inDb, outDb, grDb, remDb, texDb, mkDb, nsS, nsT, nsG;
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, grNow { 0.0f };
    std::atomic<float> det { 0.0f }, sib { 0.0f }, fv { 0.0f }, cg { 0.0f }, texEnv { 0.0f }, texPos { 0.0f };
    std::atomic<float> texHP { 20.0f }, texLP { 20000.0f };
    std::atomic<float> nsLevel { -120.0f }, nsRef { -60.0f }, nsAmtNow { 0.0f };
    std::atomic<float> refPos { 0.0f };
    std::atomic<int>   refActive { 0 };   // 1 = il riferimento da file e' in uso
    std::atomic<int>   cls { 0 };

    static constexpr int kHist = 1024;
    struct Frame { std::atomic<float> s, f, c, d, lvl; };
    std::array<Frame, kHist> hist;
    std::atomic<uint32_t> histW { 0 };

    Vis()
    {
        for (int i = 0; i < kPts; ++i)
        {
            inDb[(size_t) i] = -120.0f; outDb[(size_t) i] = -120.0f; grDb[(size_t) i] = 0.0f;
            remDb[(size_t) i] = -120.0f; texDb[(size_t) i] = -120.0f; mkDb[(size_t) i] = 0.0f;
            nsS[(size_t) i] = -120.0f; nsT[(size_t) i] = -120.0f; nsG[(size_t) i] = 0.0f;
        }
        for (auto& h : hist) { h.s = 0; h.f = 0; h.c = 0; h.d = 0; h.lvl = 0; }
    }
};

//==============================================================================
class Engine
{
public:
    static constexpr int kLook = 4;    // frame di lookahead del detector
    static constexpr int kNumRes = 6;  // LIVE, 513, 1025, 2049, 4097, 8193 bande (a 44.1/48 kHz)
    static constexpr int kDetHist = 64;

    explicit Engine (Vis& visRef) : v (visRef) {}

    // bande della risoluzione scelta (a questo sample rate) e latenza in campioni
    static int fftSizeFor (double sampleRate, int res)
    {
        const int m = sampleRate > 100000.0 ? 4 : (sampleRate > 50000.0 ? 2 : 1);
        res = std::clamp (res, 0, kNumRes - 1);
        return (res == 0 ? 1024 : (1024 << (res - 1))) * m; // LIVE analizza a 513 bande
    }
    static int hopFor (double sampleRate, int res)
    {
        const int m = sampleRate > 100000.0 ? 4 : (sampleRate > 50000.0 ? 2 : 1);
        if (res <= 0) return 128 * m; // LIVE: il filtro si aggiorna ogni ~2.7 ms
        // passo fisso ~5 ms: la reazione nel tempo non cambia con la risoluzione
        return std::max (256 * m, fftSizeFor (sampleRate, res) / 32);
    }
    static int latencyFor (double sampleRate, int res)
    {
        return res <= 0 ? 0 : fftSizeFor (sampleRate, res) + kLook * hopFor (sampleRate, res);
    }

    void prepare (double sampleRate, int resolution = 2)
    {
        sr = (float) sampleRate;
        resIdx = std::clamp (resolution, 0, kNumRes - 1);
        const int m = sampleRate > 100000.0 ? 4 : (sampleRate > 50000.0 ? 2 : 1);
        N = fftSizeFor (sampleRate, resIdx);
        hop = hopFor (sampleRate, resIdx);
        K = N / 2 + 1;
        hopSec = (float) hop / sr;
        binHz = sr / (float) N;
        live = resIdx == 0;
        detHopSec = live ? 2.0f * hopSec : hopSec; // il detector gira sempre ogni ~5 ms

        fft.init (N);
        win.resize ((size_t) N);
        for (int j = 0; j < N; ++j)
            win[(size_t) j] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) j / (float) N);
        olaScale = 1.0f / (0.375f * (float) N / (float) hop); // somma di w^2 delle finestre sovrapposte

        // il detector usa sempre la sua finestra da ~43 ms, qualunque sia la risoluzione:
        // stessa precisione nel tempo e stesse soglie
        ND = (live ? 1024 : 2048) * m; // LIVE: finestra del detector piu' corta (21 ms) per reagire prima
        KD = ND / 2 + 1;
        binHzD = sr / (float) ND;
        fftD.init (ND);
        winD.resize ((size_t) ND);
        for (int j = 0; j < ND; ++j)
            winD[(size_t) j] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) j / (float) ND);
        // allineamento: il centro della finestra lunga e' (N-ND)/2 campioni piu' indietro
        detOffset = (N - ND) / (2 * hop);

        // autocorrelazione della finestra (per normalizzare l'armonicita')
        winAc.assign ((size_t) ND / 2, 0.0f);
        {
            double e0 = 0.0;
            for (int j = 0; j < ND; ++j) e0 += (double) winD[(size_t) j] * winD[(size_t) j];
            for (int l = 0; l < ND / 2; ++l)
            {
                double s = 0.0;
                for (int j = 0; j + l < ND; j += 2) s += (double) winD[(size_t) j] * winD[(size_t) (j + l)];
                winAc[(size_t) l] = (float) (2.0 * s / e0);
            }
        }

        R = std::max (N, ND);
        buf.assign ((size_t) N, cpx {});
        tbuf.assign ((size_t) N, cpx {});
        acbuf.assign ((size_t) ND, cpx {});
        dbuf.assign ((size_t) ND, cpx {});
        for (int c = 0; c < 2; ++c)
        {
            inRing[c].assign ((size_t) R, 0.0f);
            outAcc[c].assign ((size_t) R, 0.0f);
            Psm[c].assign ((size_t) K, 0.0f);
            g[c].assign ((size_t) K, 0.0f);
            gainLin[c].assign ((size_t) K, 1.0f);
            Y[c].assign ((size_t) K, cpx {});
            T[c].assign ((size_t) K, cpx {});
            for (auto& h : hist[c]) h.assign ((size_t) K, cpx {});
        }
        Ls.assign ((size_t) K, 0.0f); E.assign ((size_t) K, 0.0f); tmp.assign ((size_t) K, 0.0f);
        tmp2.assign ((size_t) K, 0.0f); Lb.assign ((size_t) K, 0.0f); tgt.assign ((size_t) K, 0.0f);
        prefix.assign ((size_t) K + 1, 0.0);
        Vref.assign ((size_t) K, -200.0f); vrefLearned = false; vrefAcc.assign ((size_t) K, 0.0f);
        focusW.assign ((size_t) K, 1.0f);
        hw3.assign ((size_t) K, 1); hwD.assign ((size_t) K, 1); hwG.assign ((size_t) K, 0);
        for (int k = 0; k < K; ++k)
            hw3[(size_t) k] = std::max (1, (int) std::lround ((float) k * (std::pow (2.0f, 1.0f / 6.0f) - 1.0f)));
        for (int c = 0; c < 2; ++c) { mkLin[c].assign ((size_t) K, 1.0f); mkDb[c].assign ((size_t) K, 0.0f); }
        texFilt.assign ((size_t) K, 1.0f);
        nsP.assign ((size_t) K, 0.0f); nsSm.assign ((size_t) K, -120.0f); nsG.assign ((size_t) K, 0.0f);
        nsTgt.assign ((size_t) K, 0.0f); nsW.assign ((size_t) K, 0.0f); hwN.assign ((size_t) K, 1);
        curTgt.assign ((size_t) K, 0.0f); tgtTmp.assign ((size_t) K, 0.0f); followDb.assign ((size_t) K, -120.0f);
        rbuf.assign ((size_t) N, cpx {}); refPos = 0.0; followValid = false;
        nsLastDetail = -1.0f; nsLastNoise = -1; nsLastStart = nsLastEnd = nsLastSlope = -1.0f; nsOffset = 0.0f; nsPeakDb = -100.0f;
        bandValid = false;
        // LIVE: filtri FIR a fase minima, ricalcolati a ogni passo e interpolati
        L = 256 * m;
        mpbuf.assign ((size_t) N, cpx {});
        magTmp.assign ((size_t) K, 1.0f);
        for (int c = 0; c < 2; ++c)
        {
            for (auto* h : { &hOld[c], &hNew[c], &hCur[c], &tOld[c], &tNew[c], &tCur[c] }) h->assign ((size_t) L, 0.0f);
            hOld[c][(size_t) L - 1] = hNew[c][(size_t) L - 1] = hCur[c][(size_t) L - 1] = 1.0f; // impulso (coefficienti al contrario)
            xHist[c].assign ((size_t) (2 * L), 0.0f);
            tHist[c].assign ((size_t) (2 * L), 0.0f);
        }
        hw6.assign ((size_t) K, 1);
        for (int k = 0; k < K; ++k)
            hw6[(size_t) k] = std::max (1, (int) std::lround ((float) k * (std::pow (2.0f, 1.0f / 12.0f) - 1.0f)));
        lw = 0; sub = 0; liveRaw = 0.0f; liveVow = 0.0f;
        mkA = mkB = 1.0f; dA = dB = 0.0f; teA = teB = 0.0f; texGainLive = 0.0f; texRunLive = false; texPosLive = 0;

        lastDetail = -1.0f;
        lastNodes = {};
        lastNodesValid = false;

        // punti log per la visualizzazione
        visLo.resize (Vis::kPts); visHi.resize (Vis::kPts); visFrac.resize (Vis::kPts);
        for (int i = 0; i < Vis::kPts; ++i)
        {
            const float f0 = 20.0f * std::pow (1000.0f, (float) i / (float) Vis::kPts);
            const float f1 = 20.0f * std::pow (1000.0f, (float) (i + 1) / (float) Vis::kPts);
            int b0 = (int) std::floor (f0 / binHz), b1 = (int) std::ceil (f1 / binHz);
            b0 = std::clamp (b0, 1, K - 1); b1 = std::clamp (b1, b0, K - 1);
            visLo[(size_t) i] = b0; visHi[(size_t) i] = b1;
            // sotto una banda per punto: interpolazione tra le due bande vicine (niente scalini sui bassi)
            const float kc = std::sqrt (f0 * f1) / binHz;
            visFrac[(size_t) i] = (f1 - f0) / binHz < 1.5f ? std::clamp (kc, 1.0f, (float) (K - 2)) : -1.0f;
        }

        pos = 0; hopCount = 0; frameIdx = 0;
        dEnv = 0.0f; texEnv = 0.0f; texPos = 0; texArmed = true;
        dHist.fill (0.0f); vowHist.fill (0.0f); detIdx = 0;
        hfHist.fill (-120.0f); totHist.fill (-120.0f);
        refDb = -50.0f; cgTimer = 0.0f; fvRun = 0.0f; sinceVoice = 10.0f; noiseRun = 0.0f; nearVoice = true;
        smS = smF = smC = 0.0f;
        bandMax.fill (-200.0f); topHz = 16000.0f;
    }

    int latency() const { return live ? 0 : N + kLook * hop; }
    bool isLive() const { return live; }
    int resolution() const { return resIdx; }
    int numBands() const { return K; }
    int fftSize() const { return N; }
    float binWidth() const { return binHz; }

    void setParams (const Params& p) { prm = p; }
    void setTexture (const TextureData* t) { tex = t; }
    // riferimento W-SHAPER: audio (per FOLLOW) e profilo medio in dB per banda (per PROFILE, lungo K)
    void setReference (const TextureData* audio, const std::vector<float>* profile) { refAudio = audio; refProfile = profile; }
    Vis& vis() { return v; }

    // audio in place. numCh 1 o 2
    void process (float* const* io, int numCh, int n)
    {
        numChannels = std::min (2, std::max (1, numCh));
        if (live) { processLive (io, n); return; }
        float pkIn = 0.0f, pkOut = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                const float x = io[c][i];
                pkIn = std::max (pkIn, std::abs (x));
                inRing[c][(size_t) pos] = x;
                const float y = outAcc[c][(size_t) pos];
                outAcc[c][(size_t) pos] = 0.0f;
                io[c][i] = y;
                pkOut = std::max (pkOut, std::abs (y));
            }
            pos = (pos + 1) & (R - 1);
            if (++hopCount == hop)
            {
                hopCount = 0;
                processFrame();
            }
        }

        v.inPeak  = pkIn;
        v.outPeak = pkOut;
    }

    // per i test
    float lastDetect() const { return dEnv; }
    int frames() const { return frameIdx; }
    struct Feat { float sib, fv, cg, raw, vowel, r, dom, dom2, flat, cent, relTot, relHF, slope, peak, flatS, flatSh, flatFV, top; };
    Feat lastFeat {};

private:
    //--------------------------------------------------------------------------
    // media mobile (box) su indice di banda con mezza larghezza variabile
    void boxSmooth (const float* in, float* out, const std::vector<int>& hw)
    {
        prefix[0] = 0.0;
        for (int k = 0; k < K; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + in[k];
        for (int k = 0; k < K; ++k)
        {
            const int a = std::max (0, k - hw[(size_t) k]);
            const int b = std::min (K - 1, k + hw[(size_t) k]);
            out[k] = (float) ((prefix[(size_t) b + 1] - prefix[(size_t) a]) / (double) (b - a + 1));
        }
    }

    void updateTables()
    {
        if (std::abs (prm.detail - lastDetail) > 1.0e-4f)
        {
            lastDetail = prm.detail;
            // DETAIL 0 -> inviluppo largo 1.5 ottave (prende anche le gobbe larghe)
            // DETAIL 1 -> 1/24 di ottava (solo risonanze strettissime)
            const float oct = 1.5f * std::pow ((1.0f / 24.0f) / 1.5f, prm.detail);
            const float r = std::pow (2.0f, oct * 0.5f) - 1.0f;
            for (int k = 0; k < K; ++k)
            {
                const int h = std::max (1, (int) std::lround ((float) k * r));
                hwD[(size_t) k] = h;
                hwG[(size_t) k] = std::max (1, (int) std::lround ((float) k * r * 0.25f));
            }
        }

        bool changed = ! lastNodesValid;
        for (int i = 0; i < kNumNodes && ! changed; ++i)
        {
            const auto& a = prm.nodes[(size_t) i];
            const auto& b = lastNodes[(size_t) i];
            changed = a.on != b.on || std::abs (a.freq - b.freq) > 1.0e-4f
                   || std::abs (a.gainDb - b.gainDb) > 1.0e-4f || std::abs (a.q - b.q) > 1.0e-5f;
        }
        if (changed)
        {
            lastNodes = prm.nodes;
            lastNodesValid = true;
            for (int k = 0; k < K; ++k)
            {
                const float f = std::max (1.0f, (float) k * binHz);
                // +6 dB di curva = doppio intervento, -6 dB = meta'; sotto -48 dB spento del tutto
                const float fdb = focusDb (prm.nodes, f);
                focusW[(size_t) k] = fdb < -48.0f ? 0.0f : std::min (16.0f, std::pow (2.0f, fdb / 6.0f));
            }
        }
    }

    float bandPow (const std::vector<float>& P, float f0, float f1) const
    {
        f1 = std::min (f1, topHz); f0 = std::min (f0, f1 * 0.8f);
        const int a = std::clamp ((int) (f0 / binHzD), 1, KD - 1);
        const int b = std::clamp ((int) (f1 / binHzD), a, KD - 1);
        double s = 0.0;
        for (int k = a; k <= b; ++k) s += P[(size_t) k];
        return (float) s;
    }

    float bandFlat (const std::vector<float>& P, float f0, float f1) const
    {
        f1 = std::min (f1, topHz); f0 = std::min (f0, f1 * 0.6f);
        const int a = std::clamp ((int) (f0 / binHzD), 1, KD - 1);
        const int b = std::clamp ((int) (f1 / binHzD), a + 1, KD - 1);
        double sl = 0.0, s = 0.0;
        for (int k = a; k <= b; ++k)
        {
            const double p = (double) P[(size_t) k] + 1.0e-14;
            sl += std::log (p); s += p;
        }
        const double m = (double) (b - a + 1);
        return (float) (std::exp (sl / m) / (s / m));
    }

    float bandCentroid (const std::vector<float>& P, float f0, float f1) const
    {
        f1 = std::min (f1, topHz);
        const int a = std::clamp ((int) (f0 / binHzD), 1, KD - 1);
        const int b = std::clamp ((int) (f1 / binHzD), a, KD - 1);
        double s = 0.0, sw = 0.0;
        for (int k = a; k <= b; ++k) { s += P[(size_t) k]; sw += (double) P[(size_t) k] * k; }
        return s > 0.0 ? (float) (sw / s) * binHzD : 0.0f;
    }

    //--------------------------------------------------------------------------
    // DETECTOR: un frame (il piu' recente), restituisce il punteggio combinato 0..1
    float detect (float& vowelness)
    {
        // finestra propria del detector sugli ultimi ND campioni, sul mid
        for (int j = 0; j < ND; ++j)
        {
            const int idx = (pos - ND + j) & (R - 1);
            const float x = numChannels > 1 ? 0.5f * (inRing[0][(size_t) idx] + inRing[1][(size_t) idx]) : inRing[0][(size_t) idx];
            dbuf[(size_t) j] = { x * winD[(size_t) j], 0.0f };
        }
        fftD.transform (dbuf.data(), false);
        auto& P = detP;
        P.resize ((size_t) KD);
        const float norm = 16.0f / ((float) ND * (float) ND); // seno a fondo scala ~ 0 dB
        for (int k = 0; k < KD; ++k)
            P[(size_t) k] = std::norm (dbuf[(size_t) k]) * norm;

        // banda utile del materiale (MP3 o registrazioni a 16/22 kHz finiscono prima):
        // massimo che decade piano per fasce da 1 kHz; i rapporti si calcolano solo fin li'
        {
            topHz = 16000.0f;
            const float refNow = refDb;
            int top = 4;
            for (int b = 4; b < 16; ++b)
            {
                const float lo = (float) b * 1000.0f, hi = lo + 1000.0f;
                if (hi > sr * 0.48f) break;
                const int a0 = (int) (lo / binHzD), a1 = (int) (hi / binHzD);
                double e = 0.0;
                for (int k = a0; k < a1 && k < KD; ++k) e += P[(size_t) k];
                const float db = powToDb ((float) e);
                bandMax[(size_t) b] = std::max (db, bandMax[(size_t) b] - 1.0f * detHopSec);
                if (bandMax[(size_t) b] > refNow - 58.0f) top = b + 1;
            }
            topHz = std::max (5000.0f, (float) top * 1000.0f);
        }
        const float tot   = bandPow (P, 60.0f, 16000.0f);
        const float totDb = powToDb (tot);
        const float lowDb = powToDb (bandPow (P, 200.0f, 2000.0f));
        const float vowDb = powToDb (bandPow (P, 300.0f, 2000.0f));
        const float hfDb  = powToDb (bandPow (P, 2000.0f, 12000.0f));
        const float sDb   = powToDb (bandPow (P, 3500.0f, 12000.0f));
        const float shDb  = powToDb (bandPow (P, 2000.0f, 6000.0f));
        const float shLow = powToDb (bandPow (P, 300.0f, 1500.0f));
        const float fvHi  = powToDb (bandPow (P, 1500.0f, 9000.0f));
        const float fvLo  = powToDb (bandPow (P, 100.0f, 1500.0f));
        // pendenza in densita' (dB per banda di pari larghezza)
        const float slope = powToDb (bandPow (P, 4000.0f, 10000.0f) / 6000.0f) - powToDb (bandPow (P, 1000.0f, 3000.0f) / 2000.0f);
        const float humpSh = powToDb (bandPow (P, 2000.0f, 6000.0f) / 4000.0f) - powToDb (bandPow (P, 6500.0f, 11000.0f) / 4500.0f);
        // picco della fricativa: la terza d'ottava piu' forte sopra i 3 kHz rispetto alla densita' 1-3 kHz
        float peak = -100.0f;
        {
            const float ref13 = powToDb (bandPow (P, 1000.0f, 3000.0f) / 2000.0f);
            for (float fc : { 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f })
            {
                const float lo = fc * 0.891f, hi = fc * 1.122f;
                if (hi > topHz) break;
                peak = std::max (peak, powToDb (bandPow (P, lo, hi) / (hi - lo)) - ref13);
            }
        }
        const float flatHF = bandFlat (P, 2500.0f, 11000.0f);
        const float flatS  = bandFlat (P, 4000.0f, 11000.0f);
        const float flatSh = bandFlat (P, 1800.0f, 6500.0f);
        const float flatFV = bandFlat (P, 1000.0f, 9000.0f);
        const float cent   = bandCentroid (P, 2000.0f, 11000.0f);

        // livello di riferimento della voce: segue i picchi, scende piano (3 dB/s)
        if (totDb > refDb) refDb = totDb;
        else refDb = std::max (-70.0f, refDb - 3.0f * detHopSec);
        const float relTot = totDb - refDb;
        const float relHF  = hfDb - refDb;
        const float relLow = lowDb - refDb;

        // armonicita': autocorrelazione dello spettro sotto 4 kHz
        float r = 0.0f;
        {
            const int kmax = std::min (KD - 1, (int) (4000.0f / binHzD));
            std::fill (acbuf.begin(), acbuf.end(), cpx {});
            for (int k = 1; k <= kmax; ++k)
            {
                acbuf[(size_t) k] = P[(size_t) k];
                acbuf[(size_t) (ND - k)] = P[(size_t) k];
            }
            fftD.transform (acbuf.data(), true);
            const float r0 = acbuf[0].real();
            if (r0 > 1.0e-12f)
            {
                const int l0 = std::max (2, (int) (sr / 450.0f));
                const int l1 = std::min (ND / 2 - 1, (int) (sr / 65.0f));
                for (int l = l0; l <= l1; ++l)
                    r = std::max (r, acbuf[(size_t) l].real() / r0 / std::max (0.2f, winAc[(size_t) l]));
            }
            r = clamp01 (r);
        }

        const bool audible = totDb > -75.0f;
        const float shS = (prm.sensS - 0.5f) * 14.0f;
        const float shF = (prm.sensF - 0.5f) * 14.0f;
        const float shC = (prm.sensC - 0.5f) * 14.0f;

        vowelness = ramp (r, 0.55f, 0.85f) * ramp (relLow, -18.0f, -7.0f);

        // --- S / Z / SC: alte che dominano la zona delle vocali, rumorose
        const float dom  = sDb - vowDb;
        const float dom2 = shDb - shLow;
        //     /s/: alte in salita (pendenza) e gobba sopra i 4 kHz
        //     /sh/: gobba 2-6 kHz che sovrasta sia le vocali sia l'aria sopra
        const float sibS  = ramp (dom + shS, -10.0f, 3.0f) * ramp (peak + shS * 0.5f, 3.0f, 9.0f)
                          * ramp (cent, 3000.0f, 4300.0f) * ramp (flatS, 0.10f, 0.24f);
        const float sibSh = ramp (dom2 + shS, -3.0f, 9.0f) * ramp (humpSh + shS * 0.5f, 2.0f, 8.0f)
                          * ramp (cent, 2100.0f, 2900.0f) * (1.0f - ramp (cent, 5200.0f, 6200.0f))
                          * ramp (flatSh, 0.05f, 0.14f);
        const float sibDom = std::max (sibS, sibSh);
        // le Z sono sonore: la sonorita' penalizza solo se le alte non dominano
        float sib = sibDom
                  * (1.0f - 0.6f * ramp (r, 0.65f, 0.92f) * (1.0f - ramp (std::max (dom, dom2), 0.0f, 12.0f)))
                  * ramp (relHF + shS, -46.0f, -36.0f);

        // --- F / V: rumore piatto e debole, niente gobba sibilante, non vocale
        float fv = ramp (flatFV, 0.14f, 0.30f)
                 * (1.0f - ramp (std::abs (slope), 7.0f, 15.0f))
                 * ramp (relTot + shF, -58.0f, -48.0f)
                 * (1.0f - ramp (relTot, -8.0f, -2.0f))
                 * ramp (fvHi - fvLo + shF, -20.0f, -9.0f)
                 * (1.0f - vowelness)
                 * (1.0f - 0.85f * sibDom);
        // anti-respiro: una consonante vera e' attaccata a una vocale e dura poco.
        // Un rumore che parte lontano dalla voce (respiro, fruscio) ha una finestra piu' corta.
        if (vowelness > 0.5f) sinceVoice = 0.0f; else sinceVoice += detHopSec;
        const bool noisy = std::max (sib, fv) > 0.4f;
        if (noisy)
        {
            if (noiseRun <= 0.0f) nearVoice = sinceVoice < 0.15f;
            noiseRun += detHopSec;
        }
        else noiseRun = std::max (0.0f, noiseRun - 3.0f * detHopSec);
        const float maxS = nearVoice ? 0.40f : 0.18f;
        const float maxF = nearVoice ? 0.24f : 0.14f;
        sib *= 1.0f - ramp (noiseRun, maxS, maxS + 0.08f);
        fv  *= 1.0f - ramp (noiseRun, maxF, maxF + 0.08f);

        // --- C / G dolci: chiusura, esplosione, poi fricativa postalveolare
        float minPrev = 0.0f, hfPrevMin = 0.0f;
        {
            minPrev = 0.0f; hfPrevMin = 0.0f;
            float mn = 1.0e9f, mh = 1.0e9f;
            for (int j = 1; j <= 4; ++j)  mh = std::min (mh, hfHist[(size_t) ((hIdx - j + 16) % 16)]);
            for (int j = 3; j <= 10; ++j) mn = std::min (mn, totHist[(size_t) ((hIdx - j + 16) % 16)]);
            minPrev = mn; hfPrevMin = mh;
        }
        const bool burst = audible
                        && (hfDb - hfPrevMin) > 14.0f - shC * 0.5f
                        && (minPrev - refDb) < -26.0f + shC
                        && relHF > -40.0f - shC;
        // l'esplosione deve essere rumore (un attacco di vocale dopo il silenzio no)
        if (burst && flatHF > 0.08f) { cgTimer = 0.16f; cgRun = 0.0f; }
        float cg = 0.0f;
        if (cgTimer > 0.0f)
        {
            cgTimer -= detHopSec;
            const float post = ramp (dom2 + shC, -8.0f, 4.0f) * ramp (cent, 1900.0f, 2700.0f) * (1.0f - ramp (cent, 5200.0f, 6500.0f))
                             * ramp (flatHF, 0.05f, 0.14f);
            // confermato solo se la frizione postalveolare dura: una T o una K
            // hanno l'esplosione ma non la coda "sc"
            if (post > 0.4f) cgRun += detHopSec;
            cg = cgRun >= (live ? 0.01f : 0.02f) ? post : 0.0f; // LIVE: conferma piu' rapida
            if (post < 0.2f && cgRun > 0.0f) cgTimer = 0.0f; // finita la frizione, finito l'affricato
        }
        hfHist[(size_t) hIdx] = hfDb;
        totHist[(size_t) hIdx] = totDb;
        hIdx = (hIdx + 1) % 16;

        if (! audible) { sib = 0.0f; fv = 0.0f; cg = 0.0f; }
        sib *= 1.0f - cg; // SC con esplosione davanti = C/G

        // un filo di lisciatura (1 frame) contro i buchi
        smS = std::max (sib, smS * 0.55f);
        smF = std::max (fv,  smF * 0.55f);
        smC = std::max (cg,  smC * 0.55f);

        float raw = std::max ({ prm.enS ? smS : 0.0f, prm.enF ? smF : 0.0f, prm.enC ? smC : 0.0f });
        raw *= 1.0f - prm.guard * vowelness;

        v.sib = smS; v.fv = smF; v.cg = smC;
        int cls = None;
        const float mx = std::max ({ smS, smF, smC });
        if (mx > 0.35f) cls = (smS >= smF && smS >= smC) ? Sib : (smF >= smC ? Fric : Affr);
        v.cls = cls;

        lastFeat = { smS, smF, smC, raw, vowelness, r, dom, dom2, flatHF, cent, relTot, relHF, slope, peak, flatS, flatSh, flatFV, topHz };

        auto& h = v.hist[(size_t) (v.histW.load() % Vis::kHist)];
        h.s = prm.enS ? smS : 0.0f; h.f = prm.enF ? smF : 0.0f; h.c = prm.enC ? smC : 0.0f;
        h.d = raw; h.lvl = clamp01 ((totDb + 72.0f) / 72.0f);
        v.histW = v.histW.load() + 1;
        return clamp01 (raw);
    }

    //--------------------------------------------------------------------------
    // N-SHAPER: nella banda START-END lo spettro (lisciato nel tempo con SPEED e in frequenza
    // con DETAIL) viene portato verso la forma del rumore scelto, allineata al livello medio
    // della banda. Taglia e alza; i boost sono limitati e spenti sul silenzio.
    void updateNsTables()
    {
        if (std::abs (prm.nsDetail - nsLastDetail) > 1.0e-4f)
        {
            nsLastDetail = prm.nsDetail;
            const float oct = 1.0f * std::pow ((1.0f / 24.0f) / 1.0f, prm.nsDetail); // da 1 ottava a 1/24
            const float r = std::pow (2.0f, oct * 0.5f) - 1.0f;
            for (int k = 0; k < K; ++k) hwN[(size_t) k] = std::max (1, (int) std::lround ((float) k * r));
        }
        if (prm.nsNoise != nsLastNoise || std::abs (prm.nsStart - nsLastStart) > 1.0e-3f
            || std::abs (prm.nsEnd - nsLastEnd) > 1.0e-3f || std::abs (prm.nsSlope - nsLastSlope) > 1.0e-4f)
        {
            nsLastNoise = prm.nsNoise; nsLastStart = prm.nsStart; nsLastEnd = prm.nsEnd; nsLastSlope = prm.nsSlope;
            for (int k = 0; k < K; ++k)
            {
                const float f = std::max (1.0f, (float) k * binHz);
                nsTgt[(size_t) k] = noiseTargetDb (prm.nsNoise, f);
                nsW[(size_t) k] = k == 0 ? 0.0f : nsBandWeight (f, prm.nsStart, prm.nsEnd, prm.nsSlope);
            }
        }
    }

    // FOLLOW: il file scorre (in silenzio) e a ogni passo se ne misura il timbro nel punto attuale
    void followStep()
    {
        const TextureData* r = refAudio;
        const int len = r->length;
        if (refPos >= (double) len) refPos = 0.0;
        const int p0 = (int) refPos;
        const float* c0 = r->ch[0].data();
        const float* c1 = r->numCh > 1 ? r->ch[1].data() : c0;
        for (int j = 0; j < N; ++j)
        {
            const int idx = (p0 + j) % len;
            rbuf[(size_t) j] = { 0.5f * (c0[idx] + c1[idx]) * win[(size_t) j], 0.0f };
        }
        fft.transform (rbuf.data(), false);
        const float pnorm = 16.0f / ((float) N * (float) N);
        double tot = 0.0;
        for (int k = 0; k < K; ++k)
        {
            const float pw = std::norm (rbuf[(size_t) k]) * pnorm;
            tot += pw;
            tmp[(size_t) k] = powToDb (pw);
        }
        if (! prm.nsFreeze)
        {
            refPos += (double) hop * (double) std::max (0.05f, prm.nsRate);
            while (refPos >= (double) len) refPos -= (double) len;
        }
        v.refPos = (float) (refPos / (double) len);
        // i silenzi del file non diventano un bersaglio: si tiene l'ultimo timbro valido
        if (prm.nsFreeze || powToDb ((float) tot) < -70.0f) return;
        boxSmooth (tmp.data(), tmp2.data(), hwN);
        const float a = coefFor (prm.nsSmoothMs, hopSec);
        if (! followValid) { std::copy (tmp2.begin(), tmp2.end(), followDb.begin()); followValid = true; }
        else for (int k = 0; k < K; ++k) followDb[(size_t) k] += a * (tmp2[(size_t) k] - followDb[(size_t) k]);
    }

    void computeNShaper (const cpx* const* OX, int nc)
    {
        updateNsTables();
        const float a = coefFor (prm.nsSpeedMs, hopSec);
        const float aAtt = coefFor (prm.nsAttackMs, hopSec), aRel = coefFor (prm.nsReleaseMs, hopSec);
        const float pnorm = 16.0f / ((float) N * (float) N);

        for (int k = 0; k < K; ++k)
        {
            float p = 0.0f;
            for (int c = 0; c < nc; ++c) p += std::norm (OX[c][k]);
            p *= pnorm / (float) nc;
            nsP[(size_t) k] += a * (p - nsP[(size_t) k]);
            Ls[(size_t) k] = powToDb (nsP[(size_t) k]);
        }
        boxSmooth (Ls.data(), nsSm.data(), hwN);

        // bersaglio: rumore, timbro medio del file, o timbro del file nel punto in cui sta scorrendo
        const float* base = nsTgt.data();
        bool fileOk = false;
        if (prm.nsSource == 1 && refProfile != nullptr && (int) refProfile->size() == K)
        {
            boxSmooth (refProfile->data(), tgtTmp.data(), hwN);
            base = tgtTmp.data();
            fileOk = true;
        }
        else if (prm.nsSource == 2 && refAudio != nullptr && refAudio->length > N)
        {
            followStep();
            if (followValid) { base = followDb.data(); fileOk = true; }
        }
        v.refActive = fileOk ? 1 : 0;
        // FORMANT: il timbro di riferimento viene spostato in frequenza
        if (std::abs (prm.nsFormantSt) > 0.01f)
        {
            const float ratio = std::pow (2.0f, prm.nsFormantSt / 12.0f);
            for (int k = 0; k < K; ++k)
            {
                const float src = std::min ((float) (K - 1), (float) k / ratio);
                const int k0 = (int) src;
                const int k1 = std::min (K - 1, k0 + 1);
                const float fr = src - (float) k0;
                curTgt[(size_t) k] = base[k0] + (base[k1] - base[k0]) * fr;
            }
        }
        else
            std::copy (base, base + K, curTgt.begin());

        // allineamento: il riferimento viene messo al livello medio (in dB) della banda
        double sw = 0.0, sd = 0.0, pe = 0.0;
        for (int k = 1; k < K; ++k)
        {
            const float w = nsW[(size_t) k];
            if (w < 0.01f) continue;
            sw += w; sd += w * (nsSm[(size_t) k] - curTgt[(size_t) k]); pe += w * nsP[(size_t) k];
        }
        const float off = sw > 0.0 ? (float) (sd / sw) : 0.0f;
        nsOffset = off;
        // DYNAMIC: presenza del segnale = livello della banda rispetto ai suoi picchi recenti,
        // graduale su 60 dB (1 sul picco, 0 sessanta dB sotto o nel silenzio).
        // A DYNAMIC 0 l'intervento e' sempre AMOUNT; a 100% scala con la presenza.
        const float bandDb = sw > 0.0 ? powToDb ((float) pe) : -150.0f;
        nsPeakDb = bandDb > nsPeakDb ? bandDb : std::max (-100.0f, nsPeakDb - 3.0f * hopSec);
        const float presence = clamp01 ((bandDb - (nsPeakDb - 60.0f)) / 60.0f);
        const float amt = prm.nsAmount * (1.0f - prm.nsDynamic * (1.0f - presence));
        v.nsRef = nsPeakDb;
        const float when = prm.nsFull ? 1.0f : dEnv;             // solo consonanti, o sempre
        v.nsLevel = bandDb;
        v.nsAmtNow = amt * when;
        const bool silent = bandDb < -140.0f;                    // zero digitale: niente da rimodellare

        for (int k = 0; k < K; ++k)
        {
            const float w = nsW[(size_t) k];
            float d = silent ? 0.0f : amt * (curTgt[(size_t) k] + off - nsSm[(size_t) k]);
            d = d > 0.0f ? std::min (d, 18.0f) : std::max (d, -40.0f);
            const float tg = w * (d + prm.nsMakeupDb) * when;
            // ATTACK quando la correzione scende (taglia di piu'), RELEASE quando torna su
            float& gs = nsG[(size_t) k];
            gs += (tg < gs ? aAtt : aRel) * (tg - gs);
            const float gdb = gs;
            const float lin = dbToLin (gdb);
            for (int c = 0; c < nc; ++c)
            {
                gainLin[c][(size_t) k] = lin;
                g[c][(size_t) k] = std::max (0.0f, -gdb);
            }
        }
        for (int c = 0; c < nc; ++c)
        {
            std::fill (mkLin[c].begin(), mkLin[c].end(), 1.0f);
            std::fill (mkDb[c].begin(), mkDb[c].end(), 0.0f);
        }
    }

    //--------------------------------------------------------------------------
    // riduzione per banda (dB in g[], lineare con link in gainLin[]) sul frame OX
    void computeGains (const cpx* const* OX, int nc, float oldVow)
    {
        if (prm.nsOn) { computeNShaper (OX, nc); return; }
        const float aA = coefFor (prm.attackMs, hopSec);
        const float aR = coefFor (prm.releaseMs, hopSec);
        const float aP = coefFor (6.0f, hopSec);
        const float pnorm = 16.0f / ((float) N * (float) N);

        // spettro di riferimento delle vocali (per BROAD), imparato fuori dalle consonanti
        const bool learn = oldVow > 0.5f && dEnv < 0.05f;

        for (int c = 0; c < nc; ++c)
        {
            const cpx* X = OX[c];
            auto& ps = Psm[c];
            for (int k = 0; k < K; ++k)
            {
                const float p0 = std::norm (X[std::max (0, k - 1)]);
                const float p1 = std::norm (X[k]);
                const float p2 = std::norm (X[std::min (K - 1, k + 1)]);
                const float pf = (p0 + 2.0f * p1 + p2) * 0.25f * pnorm;
                ps[(size_t) k] += aP * (pf - ps[(size_t) k]);
                Ls[(size_t) k] = powToDb (ps[(size_t) k]);
            }

            // inviluppo locale (doppio box = triangolo)
            boxSmooth (Ls.data(), tmp.data(), hwD);
            boxSmooth (tmp.data(), E.data(), hwD);
            boxSmooth (Ls.data(), Lb.data(), hw3);

            if (learn)
            {
                for (int k = 0; k < K; ++k) vrefAcc[(size_t) k] += Lb[(size_t) k] / (float) nc;
                if (c == nc - 1)
                {
                    if (! vrefLearned) { Vref = vrefAcc; vrefLearned = true; }
                    else for (int k = 0; k < K; ++k) Vref[(size_t) k] += 0.01f * (vrefAcc[(size_t) k] - Vref[(size_t) k]);
                    std::fill (vrefAcc.begin(), vrefAcc.end(), 0.0f);
                }
            }

            for (int k = 0; k < K; ++k)
            {
                const float e = std::max (0.0f, Ls[(size_t) k] - E[(size_t) k]);
                const float res = prm.soft ? e * e / (e + 6.0f) : e;
                const float e2 = vrefLearned ? std::max (0.0f, Lb[(size_t) k] - Vref[(size_t) k] - prm.threshDb) : 0.0f;
                const float br = prm.soft ? e2 * e2 / (e2 + 8.0f) : e2;
                tmp2[(size_t) k] = std::min (150.0f, prm.depth * (res + prm.broad * br) * focusW[(size_t) k] * dEnv);
            }
            boxSmooth (tmp2.data(), tgt.data(), hwG);

            auto& gc = g[c];
            for (int k = 0; k < K; ++k)
            {
                const float t = tgt[(size_t) k];
                gc[(size_t) k] += (t > gc[(size_t) k] ? aA : aR) * (t - gc[(size_t) k]);
            }
        }

        // link stereo
        for (int k = 0; k < K; ++k)
        {
            if (nc > 1)
            {
                const float m = std::max (g[0][(size_t) k], g[1][(size_t) k]);
                for (int c = 0; c < 2; ++c)
                    gainLin[c][(size_t) k] = dbToLin (-(g[c][(size_t) k] + prm.link * (m - g[c][(size_t) k])));
            }
            else
                gainLin[0][(size_t) k] = dbToLin (-g[0][(size_t) k]);
        }

        // MAKEUP mirato: l'inverso della riduzione, solo nella zona dove SMOKER ha lavorato.
        //  - zona: riduzione media su un'ottava intorno alla banda (0 dove non ha toccato niente)
        //  - inverso: le bande non ridotte salgono del tutto, quelle schiacciate restano dove sono
        const float mkTop = prm.makeupDb * dEnv;
        for (int c = 0; c < nc; ++c)
        {
            auto& mk = mkLin[c];
            auto& md = mkDb[c];
            if (std::abs (mkTop) < 1.0e-3f)
            {
                std::fill (mk.begin(), mk.end(), 1.0f);
                std::fill (md.begin(), md.end(), 0.0f);
                continue;
            }
            for (int k = 0; k < K; ++k)
            {
                const float gk = -20.0f * std::log10 (std::max (gainLin[c][(size_t) k], 1.0e-6f));
                tmp[(size_t) k] = gk;
                const float g2 = gk * gk;
                Ls[(size_t) k] = g2 * g2;
            }
            boxSmooth (tmp.data(), tmp2.data(), hw3); // riduzione media nel terzo d'ottava
            boxSmooth (Ls.data(), Lb.data(), hw3);    // massimo "morbido" nel terzo d'ottava
            for (int k = 0; k < K; ++k)
            {
                const float gk = tmp[(size_t) k], gw = tmp2[(size_t) k];
                const float gmax = std::pow (std::max (0.0f, Lb[(size_t) k]), 0.25f);
                const float zone = ramp (gw, 0.5f, 3.0f);
                const float inv  = clamp01 (1.0f - gk / (gmax + 0.3f));
                md[(size_t) k] = mkTop * zone * inv;
                mk[(size_t) k] = dbToLin (md[(size_t) k]);
            }
        }
    }

    //--------------------------------------------------------------------------
    // HP / LP della texture. Con FOLLOW i tagli si stringono verso la banda che SMOKER
    // sta togliendo (10%-90% dell'energia tolta), ma non escono mai dai limiti HP/LP.
    void updateTexFilter (const cpx* const* OX, int nc)
    {
        double tot = 0.0;
        for (int k = 1; k < K; ++k)
        {
            float r = 0.0f;
            for (int c = 0; c < nc; ++c)
            {
                const float gk = gainLin[c][(size_t) k];
                r += std::norm (OX[c][k]) * std::max (0.0f, 1.0f - gk * gk);
            }
            tmp[(size_t) k] = r;
            tot += r;
        }
        if (tot > 1.0e-12 && dEnv > 0.05f)
        {
            double acc = 0.0;
            float fl = -1.0f, fh = -1.0f;
            for (int k = 1; k < K; ++k)
            {
                acc += tmp[(size_t) k];
                if (fl < 0.0f && acc >= 0.1 * tot) fl = (float) k * binHz;
                if (fh < 0.0f && acc >= 0.9 * tot) { fh = (float) k * binHz; break; }
            }
            if (fl > 0.0f && fh > 0.0f)
            {
                const float a = coefFor (30.0f, hopSec);
                const float l0 = std::log (std::max (20.0f, fl)), h0 = std::log (std::max (40.0f, fh));
                if (! bandValid) { followLo = l0; followHi = h0; bandValid = true; }
                followLo += a * (l0 - followLo);
                followHi += a * (h0 - followHi);
            }
        }
        const float f = clamp01 (prm.texFollow);
        const float lo20 = std::log (20.0f), hi20k = std::log (20000.0f);
        float hp = prm.texHP, lp = prm.texLP;
        if (bandValid && f > 0.0f)
        {
            hp = std::max (hp, std::exp (lo20 + (followLo - lo20) * f));
            lp = std::min (lp, std::exp (hi20k + (followHi - hi20k) * f));
        }
        if (lp < hp * 1.05f) lp = hp * 1.05f;
        v.texHP = hp; v.texLP = lp;
        for (int k = 0; k < K; ++k)
        {
            const float fr = std::max (1.0f, (float) k * binHz);
            const float h = hp > 21.0f ? 1.0f / std::sqrt (1.0f + std::pow (hp / fr, 8.0f)) : 1.0f;
            const float l = lp < 19900.0f ? 1.0f / std::sqrt (1.0f + std::pow (fr / lp, 8.0f)) : 1.0f;
            texFilt[(size_t) k] = h * l;
        }
    }

    void updateVis (const cpx* const* OX, int nc, bool texRun, float s, float texGain, float pnorm)
    {
        // visualizzazione (un frame su 2 basta: l'interfaccia va a 60 Hz)
        if ((frameIdx & 1) == 0)
        {
            const float nrm = pnorm;
            // valori di una banda (media dei canali per pi/po/rm/tx, massimo per la riduzione)
            auto binVals = [&] (int k, float& pi, float& po, float& gr, float& rm, float& tx)
            {
                pi = po = gr = rm = tx = 0.0f;
                for (int c = 0; c < nc; ++c)
                {
                    const float p = std::norm (OX[c][k]) * nrm;
                    const float gk = gainLin[c][(size_t) k];
                    const float rem = std::max (0.0f, 1.0f - gk * gk);
                    pi = std::max (pi, p);
                    po = std::max (po, std::norm (Y[c][(size_t) k]) * nrm);
                    gr = std::max (gr, -20.0f * std::log10 (std::max (gk, 1.0e-6f)));
                    rm = std::max (rm, p * rem);
                    if (texRun)
                    {
                        const float envT = std::sqrt (tmp2[(size_t) k]) + 1.0e-9f;
                        const float t = std::abs (T[c][(size_t) k]);
                        const float val = (t * levelM[c] * (1.0f - s) * std::sqrt (nrm) + t / envT * std::sqrt (p * rem) * s) * texGain * texFilt[(size_t) k];
                        tx = std::max (tx, val * val);
                    }
                }
            };
            for (int i = 0; i < Vis::kPts; ++i)
            {
                float pi = 0.0f, po = 0.0f, gr = 0.0f, rm = 0.0f, tx = 0.0f;
                const float kf = visFrac[(size_t) i];
                if (kf >= 0.0f)
                {
                    // sotto una banda per punto: interpolazione lineare tra le due bande vicine
                    const int ka = (int) kf;
                    const float fr = kf - (float) ka;
                    float a[5], b[5];
                    binVals (ka, a[0], a[1], a[2], a[3], a[4]);
                    binVals (ka + 1, b[0], b[1], b[2], b[3], b[4]);
                    pi = a[0] + (b[0] - a[0]) * fr; po = a[1] + (b[1] - a[1]) * fr; gr = a[2] + (b[2] - a[2]) * fr;
                    rm = a[3] + (b[3] - a[3]) * fr; tx = a[4] + (b[4] - a[4]) * fr;
                }
                else
                {
                    for (int k = visLo[(size_t) i]; k <= visHi[(size_t) i]; ++k)
                    {
                        float q[5];
                        binVals (k, q[0], q[1], q[2], q[3], q[4]);
                        pi = std::max (pi, q[0]); po = std::max (po, q[1]); gr = std::max (gr, q[2]);
                        rm = std::max (rm, q[3]); tx = std::max (tx, q[4]);
                    }
                }
                {
                    const float kf2 = visFrac[(size_t) i];
                    const int kc = kf2 >= 0.0f ? (int) std::lround (kf2) : (visLo[(size_t) i] + visHi[(size_t) i]) / 2;
                    float m = mkDb[0][(size_t) kc];
                    if (nc > 1 && std::abs (mkDb[1][(size_t) kc]) > std::abs (m)) m = mkDb[1][(size_t) kc];
                    v.mkDb[(size_t) i] = m;
                    if (prm.nsOn)
                    {
                        v.nsS[(size_t) i] = nsSm[(size_t) kc];
                        v.nsT[(size_t) i] = curTgt[(size_t) kc] + nsOffset;
                        v.nsG[(size_t) i] = nsG[(size_t) kc];
                    }
                }
                v.inDb[(size_t) i]  = powToDb (pi);
                v.outDb[(size_t) i] = powToDb (po);
                v.grDb[(size_t) i]  = gr;
                v.remDb[(size_t) i] = powToDb (rm);
                v.texDb[(size_t) i] = powToDb (tx);
            }
        }

    }

    //--------------------------------------------------------------------------
    void processFrame()
    {
        updateTables();
        const int nc = numChannels;

        // analisi: due canali reali in una FFT complessa
        for (int j = 0; j < N; ++j)
        {
            const int idx = (pos - N + j) & (R - 1);
            const float w = win[(size_t) j];
            buf[(size_t) j] = { inRing[0][(size_t) idx] * w, nc > 1 ? inRing[1][(size_t) idx] * w : 0.0f };
        }
        fft.transform (buf.data(), false);

        const int slot = frameIdx % (kLook + 1);
        cpx* XL = hist[0][(size_t) slot].data();
        cpx* XR = hist[1][(size_t) slot].data();
        for (int k = 0; k < K; ++k)
        {
            const cpx a = buf[(size_t) k];
            const cpx b = std::conj (buf[(size_t) ((N - k) & (N - 1))]);
            XL[k] = (a + b) * 0.5f;
            const cpx d = (a - b) * 0.5f;
            XR[k] = { d.imag(), -d.real() };
        }

        // detector sul frame piu' recente
        float vow = 0.0f;
        const float raw = prm.bypass ? 0.0f : detect (vow);
        dHist[(size_t) (detIdx % kDetHist)] = raw;
        vowHist[(size_t) (detIdx % kDetHist)] = vow;

        // frame in uscita = quello di kLook hop fa
        const int oslot = (frameIdx + 1) % (kLook + 1);
        const cpx* OX[2] = { hist[0][(size_t) oslot].data(), hist[1][(size_t) oslot].data() };

        // lookahead: il detector "vede" la consonante prima che arrivi e sale a rampa.
        // I frame del detector vengono allineati al centro della finestra in uscita.
        auto detAt = [this] (const std::array<float, kDetHist>& h, int j)
        {
            int f = detIdx - detOffset - (kLook - j);
            f = std::clamp (f, std::max (0, detIdx - kDetHist + 1), detIdx);
            return h[(size_t) (f % kDetHist)];
        };
        float dApply = 0.0f;
        for (int j = 0; j <= kLook; ++j)
        {
            const float w = 1.0f - (float) j / (float) (kLook + 1);
            dApply = std::max (dApply, detAt (dHist, j) * w);
        }
        const float holdCoef = std::exp (-hopSec / std::max (0.005f, prm.holdMs * 0.001f));
        dEnv = dApply > dEnv ? dApply : dApply + (dEnv - dApply) * holdCoef;
        const float oldVow = detAt (vowHist, 0);
        ++detIdx;
        v.det = dEnv;

        ++frameIdx;

        if (prm.bypass)
        {
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < K; ++k) Y[c][(size_t) k] = OX[c][k];
            synth();
            return;
        }

        computeGains (OX, nc, oldVow);
        const float pnorm = 16.0f / ((float) N * (float) N);

        // texture
        const bool texActive = tex != nullptr && prm.texOn && tex->length > hop && prm.texMix > 0.0f;
        {
            const float ta = coefFor (prm.texAttMs, hopSec), tr = coefFor (prm.texRelMs, hopSec);
            texEnv += (dEnv > texEnv ? ta : tr) * (dEnv - texEnv);
            v.texEnv = texEnv;
        }
        if (texActive)
        {
            if (prm.texMode == 1)
            {
                if (texArmed && dEnv > 0.25f) { texPos = 0; texArmed = false; }
                if (dEnv < 0.08f) texArmed = true;
            }
            if (texPos >= tex->length) texPos = 0;
        }
        const bool texRun = texActive && texEnv > 1.0e-4f;
        if (texRun)
        {
            const int len = tex->length;
            const float* t0 = tex->ch[0].data();
            const float* t1 = tex->numCh > 1 ? tex->ch[1].data() : t0;
            for (int j = 0; j < N; ++j)
            {
                const int idx = (texPos + j) % len;
                const float w = win[(size_t) j];
                tbuf[(size_t) j] = { t0[idx] * w, t1[idx] * w };
            }
            fft.transform (tbuf.data(), false);
            for (int k = 0; k < K; ++k)
            {
                const cpx a = tbuf[(size_t) k];
                const cpx b = std::conj (tbuf[(size_t) ((N - k) & (N - 1))]);
                T[0][(size_t) k] = (a + b) * 0.5f;
                const cpx d = (a - b) * 0.5f;
                T[1][(size_t) k] = { d.imag(), -d.real() };
            }
            texPos = (texPos + hop) % len;
            v.texPos = (float) texPos / (float) len;
        }
        else if (texActive && prm.texMode == 2)
        {
            // LOOP: il file continua a scorrere anche a cancello chiuso
            texPos = (texPos + hop) % tex->length;
            v.texPos = (float) texPos / (float) tex->length;
        }

        if (texRun) updateTexFilter (OX, nc);
        const float texGain = dbToLin (prm.texVolDb) * prm.texMix * texEnv;
        const float s = prm.texSpectral;
        double sumIn = 0.0, sumOut = 0.0;

        for (int c = 0; c < nc; ++c)
        {
            const cpx* X = OX[c];
            const auto& gl = gainLin[c];
            auto& Yc = Y[c];

            float levelMatch = 0.0f;
            if (texRun)
            {
                double sr2 = 0.0, st2 = 0.0;
                for (int k = 1; k < K; ++k)
                {
                    const float gk = gl[(size_t) k];
                    const float rm = std::abs (X[k]) * std::sqrt (std::max (0.0f, 1.0f - gk * gk));
                    sr2 += (double) rm * rm;
                    const float tm = std::abs (T[c][(size_t) k]);
                    tmp[(size_t) k] = tm * tm;
                    st2 += (double) tm * tm;
                }
                tmp[0] = 0.0f;
                levelMatch = st2 > 1.0e-20 ? (float) std::sqrt (sr2 / st2) : 0.0f;
                levelM[c] = levelMatch;
                boxSmooth (tmp.data(), tmp2.data(), hw3); // inviluppo (potenza) della texture
            }

            for (int k = 0; k < K; ++k)
            {
                const cpx x = X[k];
                const float gk = gl[(size_t) k];
                cpx core = x * (gk * mkLin[c][(size_t) k]);
                cpx tx {};
                if (texRun && k > 0)
                {
                    // complementare in potenza: |core|^2 + |tex|^2 = |x|^2 con MIX al 100%
                    const float rm = std::abs (x) * std::sqrt (std::max (0.0f, 1.0f - gk * gk));
                    const float envT = std::sqrt (tmp2[(size_t) k]) + 1.0e-9f;
                    const cpx t = T[c][(size_t) k];
                    tx = (t * (levelMatch * (1.0f - s)) + t * (rm / envT * s)) * (texGain * texFilt[(size_t) k]);
                }

                cpx y;
                if (prm.delta)       y = x * (1.0f - gk);
                else if (prm.listen) y = (core + tx) * dEnv;
                else                 y = x * (1.0f - prm.mix) + (core + tx) * prm.mix;
                Yc[(size_t) k] = y;

                const float pin = std::norm (x);
                sumIn += pin; sumOut += std::norm (x * gk);
            }
        }

        v.grNow = (sumIn > 1.0e-12 && sumOut > 1.0e-20) ? (float) (10.0 * std::log10 (sumIn / sumOut)) : 0.0f;

        updateVis (OX, nc, texRun, s, texGain, pnorm);

        synth();
    }

    //==========================================================================
    // LIVE: zero latenza.
    // L'analisi (detector + riduzione per banda) e' la stessa, sul frame piu' recente e senza
    // lookahead. La riduzione non viene applicata nello spettro ma con un FIR a fase minima
    // (256 tap a 48 kHz) ricalcolato ogni ~2.7 ms e interpolato ogni 16 campioni:
    // l'uscita non ha nessun ritardo aggiunto.
    static float dot (const float* a, const float* b, int n)
    {
        float s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0;
        int i = 0;
        for (; i + 8 <= n; i += 8)
        {
            s0 += a[i] * b[i];         s1 += a[i + 1] * b[i + 1];
            s2 += a[i + 2] * b[i + 2]; s3 += a[i + 3] * b[i + 3];
            s4 += a[i + 4] * b[i + 4]; s5 += a[i + 5] * b[i + 5];
            s6 += a[i + 6] * b[i + 6]; s7 += a[i + 7] * b[i + 7];
        }
        for (; i < n; ++i) s0 += a[i] * b[i];
        return ((s0 + s1) + (s2 + s3)) + ((s4 + s5) + (s6 + s7));
    }

    // modulo per banda (K valori) -> FIR a fase minima di L tap, salvato al contrario
    void minPhase (const float* mag, std::vector<float>& outRev)
    {
        for (int k = 0; k < K; ++k)
        {
            const float lm = std::log (std::max (mag[k], 1.0e-4f));
            mpbuf[(size_t) k] = { lm, 0.0f };
            if (k > 0 && k < N / 2) mpbuf[(size_t) (N - k)] = { lm, 0.0f };
        }
        fft.transform (mpbuf.data(), true); // cepstro reale (x N)
        const float inv = 1.0f / (float) N;
        for (int n = 0; n < N; ++n)
        {
            float c = mpbuf[(size_t) n].real() * inv;
            if (n == 0 || n == N / 2) {}
            else if (n < N / 2) c *= 2.0f;
            else c = 0.0f;
            mpbuf[(size_t) n] = { c, 0.0f };
        }
        fft.transform (mpbuf.data(), false);
        for (int k = 0; k < N; ++k)
        {
            const cpx z = mpbuf[(size_t) k];
            const float e = std::exp (std::min (z.real(), 20.0f));
            mpbuf[(size_t) k] = { e * std::cos (z.imag()), e * std::sin (z.imag()) };
        }
        fft.transform (mpbuf.data(), true);
        const int taper = L / 4;
        for (int n = 0; n < L; ++n)
        {
            float h = mpbuf[(size_t) n].real() * inv;
            if (n >= L - taper)
                h *= 0.5f + 0.5f * std::cos (kPi * (float) (n - (L - taper)) / (float) taper);
            outRev[(size_t) (L - 1 - n)] = h;
        }
    }

    void processLive (float* const* io, int n)
    {
        const int nc = numChannels;
        float pkIn = 0.0f, pkOut = 0.0f;
        const int len = tex != nullptr ? tex->length : 0;

        for (int i = 0; i < n; ++i)
        {
            // coefficienti interpolati ogni 16 campioni verso il filtro nuovo
            if (sub == 0)
            {
                const float fr = std::min (1.0f, (float) (hopCount + 16) / (float) hop);
                for (int c = 0; c < nc; ++c)
                {
                    for (int j = 0; j < L; ++j)
                        hCur[c][(size_t) j] = hOld[c][(size_t) j] + (hNew[c][(size_t) j] - hOld[c][(size_t) j]) * fr;
                    if (texRunLive)
                        for (int j = 0; j < L; ++j)
                            tCur[c][(size_t) j] = tOld[c][(size_t) j] + (tNew[c][(size_t) j] - tOld[c][(size_t) j]) * fr;
                }
            }
            sub = (sub + 1) & 15;

            const float fr = (float) hopCount / (float) hop;
            const float mk = mkA + (mkB - mkA) * fr;
            const float de = dA + (dB - dA) * fr;
            const float te = (teA + (teB - teA) * fr) * texGainLive;

            for (int c = 0; c < nc; ++c)
            {
                const float x = io[c][i];
                pkIn = std::max (pkIn, std::abs (x));
                inRing[c][(size_t) pos] = x;
                xHist[c][(size_t) lw] = x;
                xHist[c][(size_t) (lw + L)] = x;

                float y = x;
                if (! prm.bypass)
                {
                    const float core = dot (hCur[c].data(), xHist[c].data() + lw + 1, L);
                    float tx = 0.0f;
                    if (texAdvLive && len > 0)
                    {
                        // in LOOP la storia del filtro si riempie sempre: quando il cancello si apre e' gia' pronta
                        const float ts = tex->ch[tex->numCh > 1 ? c : 0][(size_t) texPosLive];
                        tHist[c][(size_t) lw] = ts;
                        tHist[c][(size_t) (lw + L)] = ts;
                        if (texRunLive)
                            tx = dot (tCur[c].data(), tHist[c].data() + lw + 1, L) * te;
                    }
                    if (prm.delta)       y = x - core;
                    else if (prm.listen) y = (core * mk + tx) * de;
                    else                 y = x * (1.0f - prm.mix) + (core * mk + tx) * prm.mix;
                }
                io[c][i] = y;
                pkOut = std::max (pkOut, std::abs (y));
            }
            if (texAdvLive && len > 0) texPosLive = (texPosLive + 1) % len;
            lw = (lw + 1 == L) ? 0 : lw + 1;
            pos = (pos + 1) & (R - 1);
            if (++hopCount == hop)
            {
                hopCount = 0;
                sub = 0;
                processFrameLive();
            }
        }
        v.inPeak = pkIn;
        v.outPeak = pkOut;
    }

    void processFrameLive()
    {
        updateTables();
        const int nc = numChannels;

        // il filtro attuale diventa il punto di partenza dell'interpolazione
        for (int c = 0; c < nc; ++c) { hOld[c] = hCur[c]; tOld[c] = tCur[c]; }
        mkA = mkB; dA = dB; teA = teB;

        for (int j = 0; j < N; ++j)
        {
            const int idx = (pos - N + j) & (R - 1);
            const float w = win[(size_t) j];
            buf[(size_t) j] = { inRing[0][(size_t) idx] * w, nc > 1 ? inRing[1][(size_t) idx] * w : 0.0f };
        }
        fft.transform (buf.data(), false);
        cpx* XL = hist[0][0].data();
        cpx* XR = hist[1][0].data();
        for (int k = 0; k < K; ++k)
        {
            const cpx a = buf[(size_t) k];
            const cpx b = std::conj (buf[(size_t) ((N - k) & (N - 1))]);
            XL[k] = (a + b) * 0.5f;
            const cpx d = (a - b) * 0.5f;
            XR[k] = { d.imag(), -d.real() };
        }
        const cpx* OX[2] = { XL, XR };

        // detector ogni ~5 ms, nessun lookahead
        if ((frameIdx & 1) == 0)
            liveRaw = prm.bypass ? 0.0f : detect (liveVow);
        const float holdCoef = std::exp (-hopSec / std::max (0.005f, prm.holdMs * 0.001f));
        dEnv = liveRaw > dEnv ? liveRaw : liveRaw + (dEnv - liveRaw) * holdCoef;
        v.det = dEnv;
        ++frameIdx;
        if (prm.bypass) return;

        computeGains (OX, nc, liveVow);
        const float pnorm = 16.0f / ((float) N * (float) N);

        for (int c = 0; c < nc; ++c)
        {
            if (prm.delta) minPhase (gainLin[c].data(), hNew[c]);
            else
            {
                for (int k = 0; k < K; ++k) magTmp[(size_t) k] = gainLin[c][(size_t) k] * mkLin[c][(size_t) k];
                minPhase (magTmp.data(), hNew[c]);
            }
        }

        mkB = 1.0f; // il makeup ora e' dentro il filtro, banda per banda
        dB = dEnv;

        // texture: secondo FIR che da' alla texture la forma dello spettro tolto
        const bool texActive = tex != nullptr && prm.texOn && tex->length > N && prm.texMix > 0.0f;
        {
            const float ta = coefFor (prm.texAttMs, hopSec), tr = coefFor (prm.texRelMs, hopSec);
            texEnv += (dEnv > texEnv ? ta : tr) * (dEnv - texEnv);
            v.texEnv = texEnv;
        }
        if (texActive && prm.texMode == 1)
        {
            if (texArmed && dEnv > 0.25f) { texPosLive = 0; texArmed = false; }
            if (dEnv < 0.08f) texArmed = true;
        }
        const bool wasRunning = texRunLive;
        texRunLive = texActive && texEnv > 1.0e-4f;
        texAdvLive = texRunLive || (texActive && prm.texMode == 2);
        if (texAdvLive && texPosLive >= tex->length) texPosLive = 0;
        if (texAdvLive && ! texRunLive) v.texPos = (float) texPosLive / (float) tex->length;
        teB = texRunLive ? texEnv : 0.0f;
        texGainLive = dbToLin (prm.texVolDb) * prm.texMix;
        const float s = prm.texSpectral;
        if (texRunLive)
        {
            updateTexFilter (OX, nc);
            if (texPosLive >= tex->length) texPosLive = 0;
            const int len = tex->length;
            const float* t0 = tex->ch[0].data();
            const float* t1 = tex->numCh > 1 ? tex->ch[1].data() : t0;
            for (int j = 0; j < N; ++j)
            {
                const int idx = (texPosLive + j) % len;
                const float w = win[(size_t) j];
                tbuf[(size_t) j] = { t0[idx] * w, t1[idx] * w };
            }
            fft.transform (tbuf.data(), false);
            for (int k = 0; k < K; ++k)
            {
                const cpx a = tbuf[(size_t) k];
                const cpx b = std::conj (tbuf[(size_t) ((N - k) & (N - 1))]);
                T[0][(size_t) k] = (a + b) * 0.5f;
                const cpx d = (a - b) * 0.5f;
                T[1][(size_t) k] = { d.imag(), -d.real() };
            }
            v.texPos = (float) texPosLive / (float) len;

            for (int c = 0; c < nc; ++c)
            {
                const cpx* X = OX[c];
                const auto& gl = gainLin[c];
                double sr2 = 0.0, st2 = 0.0;
                for (int k = 0; k < K; ++k)
                {
                    const float gk = gl[(size_t) k];
                    const float px = std::norm (X[k]);
                    const float pt = std::norm (T[c][(size_t) k]);
                    sr2 += (double) px * std::max (0.0f, 1.0f - gk * gk);
                    st2 += pt;
                    tmp[(size_t) k] = pt;
                    Ls[(size_t) k] = px;
                }
                levelM[c] = st2 > 1.0e-20 ? (float) std::sqrt (sr2 / st2) : 0.0f;
                boxSmooth (tmp.data(), tmp2.data(), hw3);  // inviluppo texture
                boxSmooth (Ls.data(), E.data(), hw6);      // inviluppo ingresso
                for (int k = 0; k < K; ++k)
                {
                    const float gk = gl[(size_t) k];
                    const float shaped = std::sqrt (std::max (0.0f, 1.0f - gk * gk) * E[(size_t) k] / (tmp2[(size_t) k] + 1.0e-18f));
                    magTmp[(size_t) k] = std::min (1000.0f, levelM[c] * (1.0f - s) + shaped * s) * texFilt[(size_t) k];
                }
                minPhase (magTmp.data(), tNew[c]);
                if (! wasRunning) tOld[c] = tNew[c], tCur[c] = tNew[c];
            }
        }

        // visualizzazione: uscita stimata = ingresso x riduzione x makeup
        double sumIn = 0.0, sumOut = 0.0;
        for (int c = 0; c < nc; ++c)
            for (int k = 0; k < K; ++k)
            {
                const float gk = gainLin[c][(size_t) k];
                Y[c][(size_t) k] = OX[c][k] * (gk * mkLin[c][(size_t) k]);
                const float pin = std::norm (OX[c][k]);
                sumIn += pin; sumOut += pin * gk * gk;
            }
        v.grNow = (sumIn > 1.0e-12 && sumOut > 1.0e-20) ? (float) (10.0 * std::log10 (sumIn / sumOut)) : 0.0f;
        updateVis (OX, nc, texRunLive, s, texGainLive * texEnv, pnorm);
    }

    void synth()
    {
        // sintesi: due spettri hermitiani in una IFFT complessa
        for (int k = 0; k < K; ++k)
        {
            const cpx a = Y[0][(size_t) k];
            const cpx b = numChannels > 1 ? Y[1][(size_t) k] : cpx {};
            buf[(size_t) k] = { a.real() - b.imag(), a.imag() + b.real() };
            if (k > 0 && k < N / 2)
            {
                const cpx ac = std::conj (a), bc = std::conj (b);
                buf[(size_t) (N - k)] = { ac.real() - bc.imag(), ac.imag() + bc.real() };
            }
        }
        fft.transform (buf.data(), true);
        const float sc = olaScale / (float) N;
        for (int j = 0; j < N; ++j)
        {
            const int idx = (pos + j) & (R - 1);
            const float w = win[(size_t) j] * sc;
            outAcc[0][(size_t) idx] += buf[(size_t) j].real() * w;
            if (numChannels > 1) outAcc[1][(size_t) idx] += buf[(size_t) j].imag() * w;
        }
    }

    //--------------------------------------------------------------------------
    Params prm;
    Vis& v;
    const TextureData* tex = nullptr;

    float sr = 48000.0f, hopSec = 0.0f, binHz = 1.0f, olaScale = 1.0f;
    int N = 2048, hop = 256, K = 1025, numChannels = 2, resIdx = 1, R = 2048;
    int ND = 2048, KD = 1025, detOffset = 0, detIdx = 0;
    bool live = false;
    float detHopSec = 0.0f;
    int L = 256, lw = 0, sub = 0, texPosLive = 0;
    float liveRaw = 0.0f, liveVow = 0.0f, mkA = 1.0f, mkB = 1.0f, dA = 0.0f, dB = 0.0f, teA = 0.0f, teB = 0.0f, texGainLive = 0.0f;
    bool texRunLive = false, texAdvLive = false;
    std::vector<cpx> mpbuf;
    std::vector<float> magTmp, hOld[2], hNew[2], hCur[2], tOld[2], tNew[2], tCur[2], xHist[2], tHist[2];
    std::vector<int> hw6;
    float binHzD = 1.0f;
    FFT fftD;
    std::vector<float> winD;
    std::vector<cpx> dbuf;
    int pos = 0, hopCount = 0, frameIdx = 0;

    FFT fft;
    std::vector<float> win, winAc;
    std::vector<cpx> buf, tbuf, acbuf;
    std::vector<float> inRing[2], outAcc[2], Psm[2], g[2], gainLin[2];
    std::vector<cpx> Y[2], T[2];
    std::array<std::vector<cpx>, kLook + 1> hist[2];
    std::vector<float> Ls, E, tmp, tmp2, Lb, tgt, Vref, vrefAcc, focusW, detP;
    std::vector<double> prefix;
    std::vector<int> hw3, hwD, hwG, visLo, visHi;
    std::vector<float> mkLin[2], mkDb[2], texFilt;
    std::vector<float> nsP, nsSm, nsG, nsTgt, nsW, curTgt, tgtTmp, followDb;
    std::vector<cpx> rbuf;
    const TextureData* refAudio = nullptr;
    const std::vector<float>* refProfile = nullptr;
    double refPos = 0.0;
    bool followValid = false;
    std::vector<int> hwN;
    float nsLastDetail = -1.0f, nsLastStart = -1.0f, nsLastEnd = -1.0f, nsLastSlope = -1.0f, nsOffset = 0.0f;
    int nsLastNoise = -1;
    float nsPeakDb = -100.0f;
    float followLo = 0.0f, followHi = 0.0f;
    bool bandValid = false;
    std::vector<float> visFrac;
    bool vrefLearned = false;
    float lastDetail = -1.0f;
    std::array<FocusNode, kNumNodes> lastNodes {};
    bool lastNodesValid = false;

    std::array<float, kDetHist> dHist {}, vowHist {};
    std::array<float, 16> hfHist {}, totHist {};
    int hIdx = 0;
    float dEnv = 0.0f, refDb = -50.0f, cgTimer = 0.0f, cgRun = 0.0f, fvRun = 0.0f, sinceVoice = 10.0f, noiseRun = 0.0f;
    bool nearVoice = true;
    float topHz = 16000.0f;
    std::array<float, 16> bandMax {};
    float smS = 0.0f, smF = 0.0f, smC = 0.0f;
    float texEnv = 0.0f;
    float levelM[2] = { 0.0f, 0.0f };
    int texPos = 0;
    bool texArmed = true;
};
} // namespace wsk
