#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
class PluginProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    PluginProcessor();
    ~PluginProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    //==============================================================================
    static juce::String timeToTimecodeString (double seconds)
    {
        auto millisecs = juce::roundToInt (seconds * 1000.0);
        auto absMillisecs = std::abs (millisecs);

        return juce::String::formatted ("%02d:%02d:%02d.%03d",
                                        (millisecs / 3600000),
                                        (absMillisecs / 60000) % 60,
                                        (absMillisecs / 1000) % 60,
                                        (absMillisecs % 1000));
    }
    // quick-and-dirty function to format a bars/beats string
    static juce::String quarterNotePositionToBarsBeatsString (double quarterNotes, 
                                                                juce::AudioPlayHead::TimeSignature sig)
    {
        if (sig.numerator == 0 || sig.denominator == 0)
            return "1|1|000";

        auto quarterNotesPerBar = (sig.numerator * 4 / sig.denominator);
        auto beats  = (fmod (quarterNotes, quarterNotesPerBar) / quarterNotesPerBar) * sig.numerator;
        auto bar    = ((int) quarterNotes) / quarterNotesPerBar + 1;
        auto beat   = ((int) beats) + 1;
        auto ticks  = ((int) (fmod (beats, 1.0) * 960.0 + 0.5));

        return juce::String::formatted ("%d|%d|%03d", bar, beat, ticks);
    }


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
