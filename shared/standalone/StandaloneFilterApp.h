#pragma once
#include <JuceHeader.h>

#include "../PluginEditorComponent.h"
#include "TransportPlayer.h"


//==================================================================================
class StandalonePluginInstance
{
    std::unique_ptr<AudioProcessor>     processor;
    AudioDeviceManager                  manager;
    MidiKeyboardState                   midiState;
    AudioTransportPlayer                player;

    //==============================================================================
    AudioProcessor* getPluginFilter() const
    {
        return processor != nullptr ? processor.get() : createPluginFilter();
    }

public:
    StandalonePluginInstance() 
    {
        processor .reset (getPluginFilter());

        manager.initialiseWithDefaultDevices (processor->getTotalNumInputChannels(), 
                                              processor->getTotalNumOutputChannels());
        manager.addAudioCallback (&player);
    }
    
    ~StandalonePluginInstance() 
    {
        stopPlaying();

        processor->editorBeingDeleted (getActiveEditor());
        processor.reset (nullptr);

		manager.removeAudioCallback(&player);
		manager.closeAudioDevice();
    }

    //==============================================================================
    void showAudioDeviceSettingsDialog()
    {
        int maxInputs {0}, maxOutputs {0};

        if (auto* bus = processor->getBus (true, 0)) 
            maxInputs  = jmax (0, bus->getDefaultLayout().size());
            
        if (auto* bus = processor->getBus (false, 0)) 
            maxOutputs = jmax (0, bus->getDefaultLayout().size());
        
        auto showMidiOutSel = processor->acceptsMidi() || processor->producesMidi();

        DialogWindow::LaunchOptions options;
        options.content.setOwned (new AudioDeviceSelectorComponent
        (
            manager,            // AudioDeviceManager
            0,                  // minAudioInputChannels,
            maxInputs,          // maxAudioInputChannels,
            0,                  // minAudioOutputChannels
            maxOutputs,         // maxAudioOutputChannels
            true,               // showMidiInputOptions
            showMidiOutSel,     // showMidiOutputSelector
            true,               // showChannelsAsStereoPairs
            false               // hideAdvancedOptionsWithButton
        ));
        
        auto title = translate("Audio/MIDI Settings");
        auto bg = options.content->getLookAndFeel().findColour (
            ResizableWindow::backgroundColourId);

        options.content->setSize(300, 500);
        options.dialogTitle = title;
        options.dialogBackgroundColour = bg;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    //==============================================================================
    void startPlaying()             { player.setProcessor (processor.get()); }
    void stopPlaying()              { player.setProcessor (nullptr); }

    //==============================================================================
    String getName() const                  { return processor->getName(); }
    AudioProcessor* getProcessor() const    { return processor.get(); }

    //==============================================================================
    MidiKeyboardState& getMidiState()                { return midiState; }
    void SetBPM(double bpm)
    {
        // player sets this thread safely, using a lock, though...
        player.setBPM(bpm);
    }
    

    //==============================================================================
    AudioProcessorEditor* createEditor() const
    {
        auto ed = processor->hasEditor() ? processor->createEditorIfNeeded() 
                                         : nullptr;
        jassert (ed != nullptr);
        return ed;
    }
    
    AudioProcessorEditor* getActiveEditor() const
    {
        // MessageManagerLock mmLock; // locking here seems to get rid of the bad access error?
        auto ed = processor->getActiveEditor(); // check with breakpoint here...?
        jassert (ed != nullptr);
        return ed;
    }
};


extern juce::JUCEApplicationBase* juce_CreateApplication();

//==================================================================================
class StandaloneFilterApp   : public JUCEApplication
{
    std::unique_ptr<StandalonePluginInstance>       pluginProcessor;
    std::unique_ptr<PluginEditorComponent>          editorComponent;
    std::unique_ptr<ScaledDocumentWindow>           pluginWindow;

    std::unique_ptr<juce::MidiKeyboardComponent>    midiKeyboard;
    juce::TextButton                                settingsButton  { translate("Audio/MIDI Settings") };
    juce::Slider                                    tempoSlider     { Slider::LinearBar, Slider::TextBoxLeft };

    //==============================================================================
    void cleanUp()
    {
        midiKeyboard.reset (nullptr);
        pluginProcessor.reset (nullptr);
        editorComponent.reset (nullptr);
        pluginWindow.reset (nullptr);
    }

public:
    //==============================================================================
    StandaloneFilterApp()
    {
        PluginHostType::jucePlugInClientCurrentWrapperType 
            = AudioProcessor::wrapperType_Standalone;
    }

    //==============================================================================
    void systemRequestedQuit() override                     { quit(); }
    void shutdown() override                                { cleanUp(); }

    //==============================================================================
    const String getApplicationName() override              { return JucePlugin_Name; }
    const String getApplicationVersion() override           { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override              { return false; }
    void anotherInstanceStarted (const String&) override    {}

    //==============================================================================
    void initialise (const String&) override
    {
        pluginProcessor.reset (new StandalonePluginInstance());

        midiKeyboard.reset (new MidiKeyboardComponent (pluginProcessor->getMidiState(), 
                                                        MidiKeyboardComponent::horizontalKeyboard));

        settingsButton.onClick = [&] () { pluginProcessor->showAudioDeviceSettingsDialog(); };

        tempoSlider.setRange (0.0, 500.0, 0.01);
        tempoSlider.setValue (120.0);
        tempoSlider.setSkewFactorFromMidPoint (120.0);
        tempoSlider.setTextValueSuffix(" BPM");
        tempoSlider.onValueChange = [&] () 
        { 
            pluginProcessor->SetBPM(tempoSlider.getValue());
        };

        editorComponent.reset (new PluginEditorComponent 
        (
            rawToUniquePtr (pluginProcessor->createEditor()), 
            [this] (juce::Component* editor) -> juce::Grid
            {
                using Tr = Grid::TrackInfo; 
                juce::Grid grid;

                grid.templateColumns     = { Tr (05_fr), Tr (05_fr) };
                grid.templateRows        = { Tr (25_px), Tr (1_fr), Tr (60_px) };
                
                grid.templateAreas       = { "HeaderOne HeaderTwo",
                                             "Main Main",
                                             "Footer Footer" };

                grid.items = {  GridItem(settingsButton).withArea ("HeaderOne"),
                                GridItem(tempoSlider).withArea ("HeaderTwo"),
                                GridItem(editor).withArea ("Main"),
                                GridItem(midiKeyboard.get()).withArea ("Footer"), };
                return grid;
            }
        ));

        auto name = pluginProcessor->getName();
        auto bg = LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId);
        auto scale = Desktop::getInstance().getGlobalScaleFactor();

        pluginWindow.reset (new ScaledDocumentWindow (name, bg, scale));

        pluginWindow->setUsingNativeTitleBar (true);
        pluginWindow->setContentOwned (editorComponent.get(), true);
        pluginWindow->onCloseButtonPressed = [&] { quit(); };
        pluginWindow->setVisible (true);
        pluginWindow->setAlwaysOnTop (true);

        pluginProcessor->startPlaying();
    }
};
