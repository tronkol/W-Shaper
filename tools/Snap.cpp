// Strumento di prova (solo Linux): fa suonare W-Shaper su un file e salva l'interfaccia vera in PNG
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <fstream>

using namespace juce;

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc < 5) { printf ("uso: WShaperSnap voce.f32 riferimento.wav stopSec outdir\n"); return 1; }
    const File outDir (argv[4]);
    outDir.createDirectory();
    std::vector<float> x;
    {
        std::ifstream f (argv[1], std::ios::binary);
        float v; while (f.read ((char*) &v, 4)) x.push_back (v);
    }
    WShaperProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    printf ("riferimento %s\n", proc.loadReference (File (argv[2])) ? "ok" : "FAIL");

    AudioBuffer<float> buf (2, 512);
    MidiBuffer midi;
    const int stop = (int) (atof (argv[3]) * 48000.0);
    for (int i = 0; i + 512 <= stop && i + 512 <= (int) x.size(); i += 512)
    {
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, x.data() + i, 512);
        proc.processBlock (buf, midi);
    }
    const char* pages[] = { "shaper", "source", "detect" };
    for (int skin = 0; skin < (int) ui::skins().size(); ++skin)
        for (int pg = 0; pg < 3; ++pg)
        {
            if (skin > 0 && pg > 0) continue;
            proc.apvts.state.setProperty ("skin", skin, nullptr);
            proc.apvts.state.setProperty ("uiPage", pg, nullptr);
            std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
            MessageManager::getInstance()->runDispatchLoopUntil (400);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            const File f = outDir.getChildFile (String ("W-Shaper-") + ui::skins()[(size_t) skin].name + "-" + pages[pg] + ".png");
            f.deleteFile();
            FileOutputStream os (f);
            PNGImageFormat().writeImageToStream (img, os);
            printf ("%s\n", f.getFullPathName().toRawUTF8());
        }
    return 0;
}
