// Dev tool: renders the editor offscreen in several states and saves PNGs.
//   sediments_snapshot model.ts outdir file1.wav [...]
#include <juce_gui_extra/juce_gui_extra.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

static void pump (SedimentsProcessor& proc, int ms)
{
    juce::AudioBuffer<float> b (2, 512);
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
        b.clear();
        proc.engine.render (b, 512, 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
}

static void shoot (SedimentsProcessor& proc, juce::AudioProcessorEditor& ed, const juce::File& out, int ms = 300)
{
    pump (proc, ms);
    if (auto* e = dynamic_cast<SedimentsEditor*> (&ed)) e->refreshNow();
    pump (proc, 60);
    auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("saved %s (%dx%d)\n", out.getFileName().toRawUTF8(), img.getWidth(), img.getHeight());
}

static void setParam (SedimentsProcessor& p, const char* id, float plainValue)
{
    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p.params.getParameter (id)))
        r->setValueNotifyingHost (r->convertTo0to1 (plainValue));
}

// the engine reads parameters in processBlock; mimic it
static void syncEngine (SedimentsProcessor& p)
{
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    b.clear();
    p.processBlock (b, m);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 4) return 1;
    auto cwd = juce::File::getCurrentWorkingDirectory();
    auto outDir = cwd.getChildFile (argv[2]);
    outDir.createDirectory();

    juce::SharedResourcePointer<lt::UiSettings> settings;
    settings->resetToDefaults();

    // 1. First run: nothing loaded
    {
        SedimentsProcessor fresh;
        fresh.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (fresh.createEditor());
        ed->setVisible (true);
        shoot (fresh, *ed, outDir.getChildFile ("01_welcome.png"));
        // model loaded, no sounds yet
        fresh.engine.loadModelAsync (cwd.getChildFile (argv[1]));
        while (fresh.engine.isBusy()) juce::Thread::sleep (20);
        shoot (fresh, *ed, outDir.getChildFile ("02_welcome_model.png"));
        ed.reset();
        fresh.releaseResources();
    }

    SedimentsProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.engine.loadModelAsync (cwd.getChildFile (argv[1]));
    juce::StringArray files;
    for (int i = 3; i < argc; ++i) files.add (cwd.getChildFile (argv[i]).getFullPathName());
    proc.engine.addFilesAsync (files);
    while (proc.engine.isBusy()) juce::Thread::sleep (20);
    juce::Thread::sleep (100);
    while (proc.engine.isTraining()) juce::Thread::sleep (20);

    setParam (proc, "terrain", 0.7f);
    setParam (proc, "flow", 0.45f);
    setParam (proc, "blur", 0.2f);
    setParam (proc, "x", 0.42f);
    setParam (proc, "y", 0.62f);
    syncEngine (proc);

    std::unique_ptr<juce::AudioProcessorEditor> edBase (proc.createEditor());
    auto* ed = dynamic_cast<SedimentsEditor*> (edBase.get());
    ed->setVisible (true);

    ed->setUiMode (0);
    shoot (proc, *ed, outDir.getChildFile ("03_explore.png"), 600);

    // 4. Gesture: a figure-of-eight, locked to 2 bars
    {
        std::vector<LatentEngine::PathPoint> pts;
        for (int i = 0; i <= 120; ++i)
        {
            const float t = i / 120.0f, a = 6.2831853f * t;
            pts.push_back ({ t * 2.0f, 0.5f + 0.2f * std::sin (a), 0.5f + 0.12f * std::sin (2.0f * a) });
        }
        proc.engine.setPath (pts);
        setParam (proc, "pathOn", 1.0f);
        setParam (proc, "pathSync", 4.0f);
        syncEngine (proc);
        ed->setUiMode (1);
        shoot (proc, *ed, outDir.getChildFile ("04_gesture.png"), 500);
        setParam (proc, "pathOn", 0.0f);
        syncEngine (proc);
    }

    // 5. Keys: MIDI on, one note held
    {
        setParam (proc, "midiMode", 1.0f);
        syncEngine (proc);
        ed->setUiMode (2);
        proc.engine.pushNote (64, 0.8f, true);
        shoot (proc, *ed, outDir.getChildFile ("05_keys.png"), 600);
        proc.engine.pushNote (64, 0.0f, false);
        setParam (proc, "midiMode", 0.0f);
        syncEngine (proc);
    }

    // 6. Settings panel
    ed->setUiMode (0);
    ed->showSettings (true);
    shoot (proc, *ed, outDir.getChildFile ("06_settings.png"));
    ed->showSettings (false);

    auto applySettings = [&] (std::function<void (lt::UiSettings&)> f)
    {
        f (*settings);
        settings->save();
        pump (proc, 150);
    };

    applySettings ([] (lt::UiSettings& s) { s.themeId = "daylight"; });
    shoot (proc, *ed, outDir.getChildFile ("07_daylight.png"));

    applySettings ([] (lt::UiSettings& s) { s.themeId = "contrast"; s.paletteId = "redgreen"; });
    shoot (proc, *ed, outDir.getChildFile ("08_contrast_redgreen.png"));

    applySettings ([] (lt::UiSettings& s) { s.themeId = "night"; s.paletteId = "standard"; });
    ed->showSettings (true);
    shoot (proc, *ed, outDir.getChildFile ("09_night_settings.png"));
    ed->showSettings (false);

    applySettings ([] (lt::UiSettings& s) { s.themeId = "dusk"; s.fontId = "opendyslexic"; s.textScale = 1.25f; s.spacing = 0.025f; });
    shoot (proc, *ed, outDir.getChildFile ("10_large_opendyslexic.png"));

    applySettings ([] (lt::UiSettings& s) { s.textScale = 1.0f; s.spacing = 0.0f; });
    shoot (proc, *ed, outDir.getChildFile ("12_opendyslexic_nospacing.png"));

    applySettings ([] (lt::UiSettings& s) { s.fontId = "atkinson"; s.textScale = 1.0f; s.spacing = 0.0f; });
    ed->showHelp (true);
    shoot (proc, *ed, outDir.getChildFile ("11_help.png"));
    ed->showHelp (false);

    // 13. Originals blended with the model: Source and Grain size appear
    setParam (proc, "source", 0.6f);
    syncEngine (proc);
    shoot (proc, *ed, outDir.getChildFile ("13_source_mix.png"), 400);

    // 14. Map layout chooser (rendered on its own)
    {
        lt::LayoutChooser lc (proc);
        auto img = lc.createComponentSnapshot (lc.getLocalBounds(), true, 1.0f);
        juce::Image framed (juce::Image::RGB, img.getWidth() + 32, img.getHeight() + 32, true);
        { juce::Graphics g (framed); g.fillAll (settings->theme().panel); g.drawImageAt (img, 16, 16); }
        auto f = outDir.getChildFile ("14_layout_chooser.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (framed, os);
    }
    // 16-18. Space: 8 speakers placed by the map, each sound by hand, Ambisonics dome
    {
        setParam (proc, "source", 1.0f);
        setParam (proc, "output", 5.0f);
        setParam (proc, "place", 1.0f);
        setParam (proc, "scatter", 0.15f);
        syncEngine (proc);
        ed->setUiMode (3);
        shoot (proc, *ed, outDir.getChildFile ("16_space_ring.png"), 900);

        setParam (proc, "place", 2.0f);
        setParam (proc, "scatter", 0.0f);
        proc.setSoundDirection (files[0], -60.0f);
        syncEngine (proc);
        shoot (proc, *ed, outDir.getChildFile ("17_space_each.png"), 900);

        setParam (proc, "output", 11.0f);
        setParam (proc, "place", 3.0f);
        setParam (proc, "height", 1.0f);
        setParam (proc, "scatter", 0.25f);
        setParam (proc, "rotate", 0.1f);
        syncEngine (proc);
        shoot (proc, *ed, outDir.getChildFile ("18_space_ambisonics.png"), 900);

        setParam (proc, "output", 0.0f);
        syncEngine (proc);
        shoot (proc, *ed, outDir.getChildFile ("19_space_stereo.png"), 500);

        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        SedimentsProcessor other;
        other.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("direction round trip: %d sound(s), first %.1f\n", (int) other.soundDirections.size(),
                     other.soundDirections.empty() ? 0.0f : other.soundDirections.begin()->second);
        while (other.engine.isBusy()) juce::Thread::sleep (20);
        setParam (proc, "height", 0.0f);
        setParam (proc, "rotate", 0.0f);
        setParam (proc, "scatter", 0.0f);
        setParam (proc, "place", 0.0f);
        ed->setUiMode (0);
    }
    setParam (proc, "source", 0.0f);
    syncEngine (proc);

    // 15. No model at all: descriptors, pitch / brightness axes
    {
        SedimentsProcessor nm;
        nm.prepareToPlay (48000.0, 512);
        nm.engine.restoreAsync ({}, {}, true, 1, 2);
        nm.engine.addFilesAsync (files);
        juce::Thread::sleep (50);
        while (nm.engine.isBusy()) juce::Thread::sleep (20);
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (nm.params.getParameter ("x"))) r->setValueNotifyingHost (0.3f);
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (nm.createEditor());
        ed2->setVisible (true);
        shoot (nm, *ed2, outDir.getChildFile ("15_no_model_axes.png"), 500);
        // state round trip keeps the no-model mode and axes
        juce::MemoryBlock mb;
        nm.getStateInformation (mb);
        SedimentsProcessor other;
        other.setStateInformation (mb.getData(), (int) mb.getSize());
        while (other.engine.isBusy()) juce::Thread::sleep (20);
        std::printf ("no-model round trip: withoutModel %d, axes %d/%d, frames %d\n", (int) other.engine.isWithoutModel(),
                     other.engine.getLayoutX(), other.engine.getLayoutY(), other.engine.getCorpus() ? other.engine.getCorpus()->size() : 0);
        ed2.reset();
        nm.releaseResources();
    }

    // custom colour for one sound, then state round trip
    proc.soundColours[files[0]] = juce::Colour (0xFF8AB4F8);
    {
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        SedimentsProcessor other;
        other.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("state round trip: colours %d -> %d, uiMode %d -> %d\n", (int) proc.soundColours.size(),
                     (int) other.soundColours.size(), proc.uiMode, other.uiMode);
        while (other.engine.isBusy()) juce::Thread::sleep (20);
    }

    // audition: hear a file and check that it is reported as playing
    proc.engine.auditionAsync (files[0], 0.0, 1.0);
    pump (proc, 200);
    std::printf ("audition playing: %s\n", proc.engine.getAuditionPath().isNotEmpty() ? "yes" : "no");

    settings->resetToDefaults();
    edBase.reset();
    proc.releaseResources();
    return 0;
}
