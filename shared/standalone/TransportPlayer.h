#pragma once
#include <JuceHeader.h>


//==============================================================================
class AudioTransportPlayer  : public AudioIODeviceCallback,
                              public MidiInputCallback
{
public:
    template <typename Value>
    struct ChannelInfo
    {
        ChannelInfo() = default;
        ChannelInfo (Value* const* dataIn, int numChannelsIn)
            : data (dataIn), numChannels (numChannelsIn)  {}

        Value* const*       data = nullptr;
        int                 numChannels = 0;
    };

    struct NumChannels
    {
        NumChannels() = default;
        NumChannels (int numIns, int numOuts) : ins (numIns), outs (numOuts) {}

        explicit NumChannels (const AudioProcessor::BusesLayout& layout)
            : ins (layout.getNumChannels (true, 0)), outs (layout.getNumChannels (false, 0)) {}

        AudioProcessor::BusesLayout toLayout() const
        {
            return { { AudioChannelSet::canonicalChannelSet (ins) },
                     { AudioChannelSet::canonicalChannelSet (outs) } };
        }

        int ins = 0, outs = 0;
    };

    struct PlayHead : public AudioPlayHead
    {
        PlayHead() = default;

        ~PlayHead() override
        {
            if (processor->getPlayHead() == this)
                processor->setPlayHead (nullptr);
        }

        void advance(AudioProcessor* proc, Optional<uint64_t> hostTimeIn, 
                    uint64_t sampleCountIn, double sampleRateIn)
        {
            // this check should be unnecessary here, i think...
            if (proc)
                proc->setPlayHead(this);
            
            hostTimeNs = hostTimeIn;
            sampleCount = sampleCountIn;
            seconds = static_cast<double>(sampleCountIn) / sampleRateIn;
            
            info.setHostTimeNs(hostTimeNs);
            info.setTimeInSamples(static_cast<int64_t>(sampleCount));
            info.setTimeInSeconds(seconds);

            // Calculate the length of one beat in samples
            double samplesPerBeat = (60.0 / *info.getBpm()) * sampleRateIn;
            // Calculate the current position in beats
            double currentBeat = static_cast<double>(sampleCountIn) / samplesPerBeat;
            // Set the PPQ position (assuming 1 beat = 1 quarter note)
            // what do we do if the time signature is not 4/4?
            info.setPpqPosition(Optional<double>(currentBeat));
            
        }


        Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;

    private:
        AudioProcessor*                 processor;
        Optional<uint64_t>              hostTimeNs;
        uint64_t                        sampleCount;
        double                          seconds;     
    };

    //==============================================================================
    AudioTransportPlayer (bool doDoublePrecisionProcessing = false) 
        : isDoublePrecision (doDoublePrecisionProcessing) {}

    ~AudioTransportPlayer() override { setProcessor (nullptr); }
    void setBPM(double bpm)
    {
        const ScopedLock sl (lock);
        playHead.info.setBpm(bpm);
    }
    //==============================================================================
    void setProcessor (AudioProcessor* processorToPlay)
    {
        const ScopedLock sl (lock);

        if (processor == processorToPlay)
            return;

        sampleCount = 0;

        if (processorToPlay != nullptr && sampleRate > 0 && blockSize > 0)
        {
            defaultProcessorChannels = NumChannels { processorToPlay->getBusesLayout() };
            actualProcessorChannels  = findMostSuitableLayout (*processorToPlay);

            if (processorToPlay->isMidiEffect())
                processorToPlay->setRateAndBufferSizeDetails (sampleRate, blockSize);
            else
                processorToPlay->setPlayConfigDetails (actualProcessorChannels.ins,
                                                    actualProcessorChannels.outs,
                                                    sampleRate,
                                                    blockSize);

            auto supportsDouble = processorToPlay->supportsDoublePrecisionProcessing() && isDoublePrecision;

            processorToPlay->setProcessingPrecision (supportsDouble ? AudioProcessor::doublePrecision
                                                                    : AudioProcessor::singlePrecision);
            processorToPlay->prepareToPlay (sampleRate, blockSize);
        }

        AudioProcessor* oldOne = nullptr;

        oldOne = isPrepared ? processor : nullptr;
        processor = processorToPlay;
        isPrepared = true;
        resizeChannels();

        if (oldOne != nullptr)
            oldOne->releaseResources();
    }

    AudioProcessor* getCurrentProcessor() const noexcept            { return processor; }
    MidiMessageCollector& getMidiMessageCollector() noexcept        { return messageCollector; }
    

    void setMidiOutput (MidiOutput* midiOutputToUse)
    {
        if (midiOutput != midiOutputToUse)
        {
            const ScopedLock sl (lock);
            midiOutput = midiOutputToUse;
        }
    }

    void setDoublePrecisionProcessing (bool doublePrecision)
    {
        if (doublePrecision != isDoublePrecision)
        {
            const ScopedLock sl (lock);

            if (processor != nullptr)
            {
                processor->releaseResources();

                auto supportsDouble = processor->supportsDoublePrecisionProcessing() && doublePrecision;

                processor->setProcessingPrecision (supportsDouble ? AudioProcessor::doublePrecision
                                                                  : AudioProcessor::singlePrecision);
                processor->prepareToPlay (sampleRate, blockSize);
            }

            isDoublePrecision = doublePrecision;
        }
    }

    inline bool getDoublePrecisionProcessing() { return isDoublePrecision; }

    //==============================================================================
    void audioDeviceIOCallbackWithContext (const float* const* const inputChannelData,
                                            const int numInputChannels,
                                            float* const* const outputChannelData,
                                            const int numOutputChannels,
                                            const int numSamples,
                                            const AudioIODeviceCallbackContext& context) override
    {
        const ScopedLock sl (lock);
        jassert (sampleRate > 0 && blockSize > 0); // These should have been prepared by audioDeviceAboutToStart()...

        incomingMidi.clear();
        messageCollector.removeNextBlockOfMessages (incomingMidi, numSamples);

        initialiseIoBuffers 
        (
            {inputChannelData,  numInputChannels},
            {outputChannelData, numOutputChannels},
            numSamples,
            actualProcessorChannels.ins,
            actualProcessorChannels.outs,
            tempBuffer,
            channels
        );

        const auto totalNumChannels = jmax (actualProcessorChannels.ins, actualProcessorChannels.outs);
        AudioBuffer<float> buffer (channels.data(), (int) totalNumChannels, numSamples);

        if (processor != nullptr)
        {
            // The processor should be prepared to deal with the same number of output channels
            // as our output device.
            jassert (processor->isMidiEffect() || numOutputChannels == actualProcessorChannels.outs);

            const ScopedLock sl2 (processor->getCallbackLock());

            playHead.advance (processor, 
                              context.hostTimeNs != nullptr ? makeOptional (*context.hostTimeNs) : nullopt, 
                              sampleCount, 
                              sampleRate);

            sampleCount += (uint64_t) numSamples;

            if (! processor->isSuspended())
            {
                if (processor->isUsingDoublePrecision())
                {
                    conversionBuffer.makeCopyOf (buffer, true);
                    processor->processBlock (conversionBuffer, incomingMidi);
                    buffer.makeCopyOf (conversionBuffer, true);
                }
                else
                {
                    processor->processBlock (buffer, incomingMidi);
                }

                if (midiOutput != nullptr)
                {
                    if (midiOutput->isBackgroundThreadRunning())
                        midiOutput->sendBlockOfMessages (incomingMidi, Time::getMillisecondCounterHiRes(), sampleRate);
                    else
                        midiOutput->sendBlockOfMessagesNow (incomingMidi);
                }

                return;
            }
        }

        for (int i = 0; i < numOutputChannels; ++i)
            FloatVectorOperations::clear (outputChannelData[i], numSamples);
    }

    void audioDeviceAboutToStart (AudioIODevice* device) override
    {
        auto newSampleRate  = device->getCurrentSampleRate();
        auto newBlockSize   = device->getCurrentBufferSizeSamples();
        auto numChansIn     = device->getActiveInputChannels().countNumberOfSetBits();
        auto numChansOut    = device->getActiveOutputChannels().countNumberOfSetBits();

        const ScopedLock sl (lock);

        sampleRate          = newSampleRate;
        blockSize           = newBlockSize;
        deviceChannels      = {numChansIn, numChansOut};

        resizeChannels();

        messageCollector.reset (sampleRate);

        if (processor != nullptr)
        {
            if (isPrepared) processor->releaseResources();

            auto* oldProcessor = processor;
            setProcessor (nullptr);
            setProcessor (oldProcessor);
        }
    }

    void audioDeviceStopped() override
    {
        const ScopedLock sl (lock);

        if (processor != nullptr && isPrepared) processor->releaseResources();

        sampleRate          = 0.0;
        blockSize           = 0;
        isPrepared          = false;
        tempBuffer.setSize  (1, 1);
    }

    void handleIncomingMidiMessage (MidiInput*, const MidiMessage& message) override
    {
        messageCollector.addMessageToQueue (message);
    }

private:
    NumChannels findMostSuitableLayout (const AudioProcessor& proc) const
    {
        if (proc.isMidiEffect())
            return {};

        std::vector<NumChannels> layouts { deviceChannels };

        if (deviceChannels.ins == 0 || deviceChannels.ins == 1)
        {
            layouts.emplace_back (defaultProcessorChannels.ins, deviceChannels.outs);
            layouts.emplace_back (deviceChannels.outs, deviceChannels.outs);
        }

        const auto it = std::find_if (layouts.begin(), layouts.end(), [&] (const NumChannels& chans)
        {
            return proc.checkBusesLayoutSupported (chans.toLayout());
        });

        return it != std::end (layouts) ? *it : layouts[0];
    }

    void resizeChannels()
    {
        const auto maxChannels = jmax (deviceChannels.ins,
                                    deviceChannels.outs,
                                    actualProcessorChannels.ins,
                                    actualProcessorChannels.outs);
        channels.resize ((size_t) maxChannels);
        tempBuffer.setSize (maxChannels, blockSize);
    }

    /** Sets up `channels` so that it contains channel pointers suitable for passing to
        an AudioProcessor's processBlock.

        On return, `channels` will hold `max (processorIns, processorOuts)` entries.
        The first `processorIns` entries will point to buffers holding input data.
        Any entries after the first `processorIns` entries will point to zeroed buffers.

        In the case that the system only provides a single input channel, but the processor
        has been initialised with multiple input channels, the system input will be copied
        to all processor inputs.

        In the case that the system provides no input channels, but the processor has
        been initialise with multiple input channels, the processor's input channels will
        all be zeroed.

        @param ins            the system inputs.
        @param outs           the system outputs.
        @param numSamples     the number of samples in the system buffers.
        @param processorIns   the number of input channels requested by the processor.
        @param processorOuts  the number of output channels requested by the processor.
        @param tempBuffer     temporary storage for inputs that don't have a corresponding output.
        @param channels       holds pointers to each of the processor's audio channels.
    */
    static void initialiseIoBuffers (ChannelInfo<const float> ins,
                                    ChannelInfo<float> outs,
                                    const int numSamples,
                                    int processorIns,
                                    int processorOuts,
                                    AudioBuffer<float>& tempBuffer,
                                    std::vector<float*>& channels)
    {
        jassert ((int) channels.size() >= jmax (processorIns, processorOuts));

        size_t totalNumChans = 0;
        const auto numBytes = (size_t) numSamples * sizeof (float);

        const auto prepareInputChannel = [&] (int index)
        {
            if (ins.numChannels == 0)
                zeromem (channels[totalNumChans], numBytes);
            else
                memcpy (channels[totalNumChans], ins.data[index % ins.numChannels], numBytes);
        };

        if (processorIns > processorOuts)
        {
            // If there aren't enough output channels for the number of
            // inputs, we need to use some temporary extra ones (can't
            // use the input data in case it gets written to).
            jassert (tempBuffer.getNumChannels() >= processorIns - processorOuts);
            jassert (tempBuffer.getNumSamples() >= numSamples);

            for (int i = 0; i < processorOuts; ++i)
            {
                channels[totalNumChans] = outs.data[i];
                prepareInputChannel (i);
                ++totalNumChans;
            }

            for (auto i = processorOuts; i < processorIns; ++i)
            {
                channels[totalNumChans] = tempBuffer.getWritePointer (i - processorOuts);
                prepareInputChannel (i);
                ++totalNumChans;
            }
        }
        else
        {
            for (int i = 0; i < processorIns; ++i)
            {
                channels[totalNumChans] = outs.data[i];
                prepareInputChannel (i);
                ++totalNumChans;
            }

            for (auto i = processorIns; i < processorOuts; ++i)
            {
                channels[totalNumChans] = outs.data[i];
                zeromem (channels[totalNumChans], (size_t) numSamples * sizeof (float));
                ++totalNumChans;
            }
        }
    }

    //==============================================================================

    AudioProcessor*              processor = nullptr;
    CriticalSection              lock;
    double                       sampleRate = 0;
    int                          blockSize = 0;
    bool                         isPrepared = false, 
                                 isDoublePrecision = false;

    NumChannels                  deviceChannels, 
                                 defaultProcessorChannels, 
                                 actualProcessorChannels;

    std::vector<float*>          channels;
    AudioBuffer<float>           tempBuffer;
    AudioBuffer<double>          conversionBuffer;

    MidiBuffer                   incomingMidi;
    MidiMessageCollector         messageCollector;
    MidiOutput*                  midiOutput = nullptr;
    uint64_t                     sampleCount = 0;

    PlayHead                     playHead;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioTransportPlayer)
};