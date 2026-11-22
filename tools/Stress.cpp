// Stress test (solo Linux): audio su un thread, sul thread dei messaggi carica file, cambia risoluzione,
// salva/ricarica lo stato, apre/chiude l'editor. Compilato con AddressSanitizer.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <thread>
#include <fstream>

using namespace juce;

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    std::vector<float> x;
    { std::ifstream f (argv[1], std::ios::binary); float v; while (f.read ((char*) &v, 4)) x.push_back (v); }
    const File refA (argv[2]), refB (argv[3]);
    const int seconds = argc > 4 ? atoi (argv[4]) : 20;

    WShaperProcessor proc;
    const double sr = argc > 5 ? atof (argv[5]) : 48000.0;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    std::atomic<bool> run { true }, pause { false }, paused { false };
    std::atomic<long> blocks { 0 };
    std::thread audio ([&]
    {
        AudioBuffer<float> buf (2, 512);
        MidiBuffer midi;
        size_t pos = 0;
        Random rnd (3);
        while (run)
        {
            if (pause) { paused = true; std::this_thread::sleep_for (std::chrono::milliseconds (1)); continue; }
            paused = false;
            const int n = 32 + rnd.nextInt (480);   // blocchi di dimensione variabile, come in Logic
            buf.setSize (2, n, false, false, true);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i) buf.setSample (c, i, x[(pos + (size_t) i) % x.size()]);
            pos += (size_t) n;
            if (rnd.nextInt (50) == 0) proc.processBlockBypassed (buf, midi); else proc.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    if (! std::isfinite (buf.getSample (c, i))) { printf ("NaN/Inf in uscita!\n"); std::abort(); }
            ++blocks;
        }
    });

    // come Logic: salvataggi automatici dello stato da un altro thread, in continuazione
    std::thread saver ([&]
    {
        while (run)
        {
            MemoryBlock mb;
            proc.getStateInformation (mb);
            std::this_thread::sleep_for (std::chrono::milliseconds (7));
        }
    });
    Random rnd (11);
    std::unique_ptr<AudioProcessorEditor> ed;
    const auto t0 = Time::getMillisecondCounter();
    int actions = 0;
    while (Time::getMillisecondCounter() - t0 < (uint32) seconds * 1000u)
    {
        switch (rnd.nextInt (7))
        {
            case 0: proc.loadReference (rnd.nextBool() ? refA : refB); break;
            case 1: { auto* p = proc.apvts.getParameter ("res"); p->setValueNotifyingHost (p->convertTo0to1 ((float) rnd.nextInt (6))); break; }
            case 2: { MemoryBlock mb; proc.getStateInformation (mb); proc.setStateInformation (mb.getData(), (int) mb.getSize()); break; }
            case 3: if (ed == nullptr) ed.reset (proc.createEditor()); else ed.reset(); break;
            case 4: proc.applyPreset (rnd.nextInt (proc.getNumPrograms())); break;
            case 5:
                if (rnd.nextInt (3) == 0) proc.clearReference();
                else
                {
                    // prepareToPlay da un thread che non e' quello dei messaggi, con l'audio fermo
                    pause = true;
                    while (! paused) std::this_thread::sleep_for (std::chrono::milliseconds (1));
                    std::thread prep ([&] { proc.prepareToPlay (rnd.nextBool() ? 44100.0 : 48000.0, 512); });
                    prep.join();
                    pause = false;
                }
                break;
            default:
                for (auto* id : { "amount", "detail", "match", "formant", "smooth", "start", "end" })
                    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (rnd.nextFloat());
        }
        ++actions;
        MessageManager::getInstance()->runDispatchLoopUntil (20 + rnd.nextInt (60));
    }
    run = false;
    audio.join();
    saver.join();
    ed.reset();
    printf ("ok: %d azioni, %ld blocchi audio\n", actions, blocks.load());
    return 0;
}
