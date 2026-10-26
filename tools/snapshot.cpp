// AlptugDUB snapshot: plays a voice through the plugin, then renders its editor to a PNG.
//
//   AlptugDUBSnapshot out.png [voice.wav] [character index] [scale] [--about]
#include "../src/PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

/** Every sample rate, block size, layout and character, switching analysis and engine mid-stream. */
static int stress() {
    juce::Random rng(1);
    int failures = 0;
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int channels : {1, 2})
            for (int maxBlock : {64, 512, 2048}) {
                AlptugDUBProcessor proc;
                proc.setPlayConfigDetails(channels, channels, sr, maxBlock);
                proc.prepareToPlay(sr, maxBlock);
                juce::AudioBuffer<float> buf(channels, maxBlock);
                juce::MidiBuffer midi;
                double phase = 0, t = 0;
                float worst = 0;
                int worstStep = 0, worstChar = 0;
                bool finite = true;
                const int steps = (int) (sr * 8.0 / (maxBlock / 2.0)); // about 8 seconds of audio
                const bool wideOnly = std::getenv("STRESS_WIDE") != nullptr;
                if (wideOnly) proc.state.getParameter(ids::engine)->setValueNotifyingHost(1.0f);
                for (int step = 0; step < steps; ++step) {
                    const int n = 1 + rng.nextInt(maxBlock); // hosts may send any size up to the maximum
                    for (int i = 0; i < n; ++i, t += 1.0 / sr) {
                        // a gliding, buzzy "voice" with silences and noise bursts
                        const double f0 = 90 + 80 * std::sin(t * 1.3) + 40 * std::sin(t * 7.0);
                        phase += f0 / sr;
                        const bool voiced = std::fmod(t, 1.7) < 1.2;
                        float v = voiced ? (float) (0.4 * (std::fmod(phase, 1.0) - 0.5)) : (float) (0.05 * (rng.nextFloat() - 0.5f));
                        if (std::fmod(t, 3.1) < 0.2) v = 0.0f; // digital silence
                        for (int c = 0; c < channels; ++c) buf.setSample(c, i, v);
                    }
                    juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), channels, n);
                    proc.processBlock(view, midi);
                    for (int c = 0; c < channels; ++c)
                        for (int i = 0; i < n; ++i) {
                            const float v = view.getSample(c, i);
                            if (!std::isfinite(v)) finite = false;
                            if (std::abs(v) > worst) {
                                worst = std::abs(v);
                                worstStep = step;
                                worstChar = juce::roundToInt(proc.state.getRawParameterValue(ids::character)->load());
                            }
                        }
                    if (step % (steps / 11) == 0) proc.applyCharacter(std::getenv("STRESS_CHAR") ? std::atoi(std::getenv("STRESS_CHAR")) : rng.nextInt((int) AlptugDUBProcessor::characters().size()));
                    if (step == steps * 3 / 8 || step == steps * 3 / 4) { // switch the analysis, as the message thread would
                        auto* ai = proc.state.getParameter(ids::ai);
                        ai->setValueNotifyingHost(ai->getValue() > 0.5f ? 0.0f : 1.0f);
                        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
                    }
                    if (!wideOnly && step == steps * 11 / 20) proc.state.getParameter(ids::engine)->setValueNotifyingHost(1.0f);
                    if (!wideOnly && step == steps * 13 / 20) proc.state.getParameter(ids::engine)->setValueNotifyingHost(0.0f);
                }
                const bool ok = finite && worst < 4.0f;
                failures += ok ? 0 : 1;
                std::printf("%s  %6.0f Hz  %d ch  blocks up to %4d  peak %.2f (at %.0f%%, character %d)  latency %d\n", ok ? "ok  " : "FAIL", sr,
                            channels, maxBlock, worst, 100.0 * worstStep / steps, worstChar, proc.getLatencySamples());
            }
    return failures == 0 ? 0 : 1;
}

/** --render in.wav out.wav character [engine]: the whole plug-in on a file, written as 32-bit float WAV. */
static int render(const char* in, const char* out, int character, bool wide) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(juce::File::getCurrentWorkingDirectory().getChildFile(in)));
    if (!reader) return 1;
    const double sr = reader->sampleRate;
    const int block = 512;
    AlptugDUBProcessor proc;
    proc.setPlayConfigDetails(2, 2, sr, block);
    proc.prepareToPlay(sr, block);
    if (wide) proc.state.getParameter(ids::engine)->setValueNotifyingHost(1.0f);
    // let it learn the voice first (as in a real session), then apply the character
    juce::AudioBuffer<float> src((int) 2, (int) reader->lengthInSamples);
    reader->read(&src, 0, (int) reader->lengthInSamples, 0, true, true);
    if (reader->numChannels == 1) src.copyFrom(1, 0, src, 0, 0, src.getNumSamples());
    const int total = src.getNumSamples(), tail = proc.getLatencySamples() + 4096;
    juce::AudioBuffer<float> result(2, total + tail);
    result.clear();
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, block);
    for (int pos = 0; pos < total + tail; pos += block) {
        const int n = std::min(block, total + tail - pos);
        buf.clear();
        for (int c = 0; c < 2; ++c)
            if (pos < total) buf.copyFrom(c, 0, src, c, pos, std::min(n, total - pos));
        juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 2, n);
        if (pos == 0) {
            proc.applyCharacter(character);
            // DUB_SET="breath=0,age=0": set parameters (real values) after the character, for experiments
            if (const char* e = std::getenv("DUB_SET")) {
                juce::StringArray items = juce::StringArray::fromTokens(e, ",", "");
                for (auto& it : items)
                    if (auto* prm = proc.state.getParameter(it.upToFirstOccurrenceOf("=", false, false)))
                        prm->setValueNotifyingHost(prm->convertTo0to1(it.fromFirstOccurrenceOf("=", false, false).getFloatValue()));
            }
        }
        proc.processBlock(view, midi);
        for (int c = 0; c < 2; ++c) result.copyFrom(c, pos, view, c, 0, n);
    }
    // line the output up with the input
    const int lat = proc.getLatencySamples();
    juce::AudioBuffer<float> aligned(2, total);
    for (int c = 0; c < 2; ++c) aligned.copyFrom(c, 0, result, c, lat, total);
    juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile(out);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(f), sr, 2, 32, {}, 0));
    if (!writer) return 1;
    writer->writeFromAudioSampleBuffer(aligned, 0, total);
    return 0;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 4 && juce::String(argv[1]) == "--render") return render(argv[2], argv[3], std::atoi(argv[4]), argc > 5);
    if (argc > 1 && juce::String(argv[1]) == "--stress") return stress();
    if (argc < 2) {
        std::fprintf(stderr, "usage: AlptugDUBSnapshot out.png [voice.wav] [character] [scale] [--about]\n");
        return 2;
    }
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    const int character = argc > 3 ? std::atoi(argv[3]) : 0;
    const float scale = argc > 4 ? (float) std::atof(argv[4]) : 1.0f;
    const bool about = argc > 5 && juce::String(argv[5]) == "--about";

    AlptugDUBProcessor proc;
    proc.setPlayConfigDetails(2, 2, 48000, 512);
    proc.prepareToPlay(48000, 512);
    std::unique_ptr<AlptugDUBEditor> editor(static_cast<AlptugDUBEditor*>(proc.createEditor()));
    editor->setSize(AlptugDUBEditor::W, AlptugDUBEditor::H);

    if (argc > 2) {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2])));
        if (reader) {
            juce::AudioBuffer<float> buf(2, 512);
            juce::MidiBuffer midi;
            const int perFrame = 48000 / 30;
            int since = 0;
            float peakIn = 0, peakOut = 0;
            if (std::getenv("SNAP_WIDE")) proc.state.getParameter(ids::engine)->setValueNotifyingHost(1.0f);
            for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += 512) {
                reader->read(&buf, 0, 512, pos, true, true);
                if (reader->numChannels == 1) buf.copyFrom(1, 0, buf, 0, 0, 512);
                peakIn = std::max(peakIn, buf.getMagnitude(0, 512));
                proc.processBlock(buf, midi);
                peakOut = std::max(peakOut, buf.getMagnitude(0, 512));
                if ((since += 512) >= perFrame) {
                    since -= perFrame;
                    editor->refresh();
                }
                if (pos == 512 * 8) proc.applyCharacter(character);
            }
            std::printf("peak in %.3f  out %.3f\n", peakIn, peakOut);
        }
    }
    proc.applyCharacter(character);
    editor->refresh();
    if (about)
        if (auto* page = editor->getChildComponent(0)->findChildWithID("about")) {
            page->setVisible(true);
            page->toFront(false);
        }
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, scale);
    juce::PNGImageFormat png;
    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (!stream.openedOk() || !png.writeImageToStream(image, stream)) return 1;
    std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
