#pragma once
#include <JuceHeader.h>
#include "PluginEditorComponent.h"


//==================================================================================
class StandalonePluginInstance
{
    // might just be easier to create a custom standalone player class with
    // accessible playhead info...?
    struct AudioTransportPlayHead   : public AudioPlayHead
    {
        Optional<AudioPlayHead::PositionInfo> getPosition() const override
        {
            return info;
        }
        AudioPlayHead::PositionInfo info;
    };

    //==============================================================================
    std::unique_ptr<AudioProcessor> processor;
    AudioDeviceManager manager;
    MidiKeyboardState midiState;
    AudioProcessorPlayer player;
    AudioTransportPlayHead playHead;

    //==============================================================================
    AudioProcessor* getPluginFilter() const
    {
        return processor != nullptr ? processor.get() : createPluginFilter();
    }

public:
    StandalonePluginInstance() 
    { 
        processor.reset (getPluginFilter());
        processor->setPlayHead (&playHead); 

        auto ins  = processor->getTotalNumInputChannels();
        auto outs = processor->getTotalNumOutputChannels();

        // currently doing it like this just for ease, will need to change it to
        // suit more of the standalone workflow of avoiding feedback loops etc...
        manager.initialiseWithDefaultDevices (ins, outs);
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

        DialogWindow::LaunchOptions o;
        o.content.setOwned (new AudioDeviceSelectorComponent
        (
            manager, 
            0, maxInputs, 
            0, maxOutputs, 
            true, 
            (processor->acceptsMidi() || processor->acceptsMidi()), 
            true, 
            false
        ));
        
        o.content->setSize(300, 500);
        o.dialogTitle = translate("Audio/MIDI Settings");
        o.dialogBackgroundColour = o.content->getLookAndFeel().findColour(
            ResizableWindow::backgroundColourId);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    //==============================================================================
    void startPlaying()             { player.setProcessor (processor.get()); }
    void stopPlaying()              { player.setProcessor (nullptr); }

    //==============================================================================
    String getName() const                  { return processor->getName(); }
    AudioProcessor* getProcessor() const    { return processor.get(); }

    //==============================================================================
    MidiKeyboardState& getMidiState()                { return midiState; }
    AudioPlayHead::PositionInfo& getPlayHeadInfo()   { return playHead.info; }
    AudioProcessorPlayer& getPlayer()                { return player; }

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
        MessageManagerLock mmLock; // locking here seems to get rid of the bad access error
        auto ed = processor->getActiveEditor(); // check with breakpoint here...
        jassert (ed != nullptr);
        return ed;
    }
};



//==================================================================================
class StandaloneFilterApp   : public JUCEApplication
{
    std::unique_ptr<StandalonePluginInstance>       pluginProcessor;
    std::unique_ptr<PluginEditorComponent>          editorComponent;
    std::unique_ptr<ScaledDocumentWindow>           pluginWindow;    
    juce::TextButton                                settingsBtn   { translate("Audio/MIDI Settings") };
    juce::Slider                                    volumeSldr    { Slider::LinearBarVertical, 
                                                                       Slider::NoTextBox };
    juce::Slider                                    tempoSldr     { Slider::LinearBar, 
                                                                       Slider::TextBoxLeft };
    std::unique_ptr<juce::MidiKeyboardComponent>    midiKeybrd;

    //==============================================================================
    void cleanUp()
    {
        midiKeybrd.reset (nullptr);
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

        midiKeybrd.reset (new MidiKeyboardComponent (pluginProcessor->getMidiState(), 
                                                     MidiKeyboardComponent::horizontalKeyboard));
        volumeSldr.setRange (0.0, 1.0, 0.01);
        tempoSldr.setRange (0.0, 500.0, 0.01);
        tempoSldr.setValue (120.0);
        tempoSldr.setSkewFactorFromMidPoint (120.0);
        tempoSldr.setTextValueSuffix(" BPM");

        editorComponent.reset (new PluginEditorComponent 
        (
            rawToUniquePtr (pluginProcessor->createEditor()), 
            [this] (juce::Component* procEditor) -> juce::Grid
            {
                using Tr = Grid::TrackInfo; 
                juce::Grid grid;

                grid.templateColumns     = { Tr (30_px), Tr (05_fr), Tr (05_fr) };
                grid.templateRows        = { Tr (25_px), Tr (1_fr), Tr (60_px) };

                grid.templateAreas       = { "sidebar button slider",
                                             "sidebar main main",
                                             "sidebar footer footer" };

                grid.items.add (GridItem(settingsBtn).withArea        ("button"));
                grid.items.add (GridItem(tempoSldr).withArea          ("slider"));
                grid.items.add (GridItem(procEditor).withArea         ("main"));
                grid.items.add (GridItem(volumeSldr).withArea         ("sidebar"));
                grid.items.add (GridItem(midiKeybrd.get()).withArea   ("footer"));

                return grid;
            }
        ));

        settingsBtn.onClick      = [&] () { pluginProcessor->showAudioDeviceSettingsDialog(); };

                                            // maybe this should be scope locked using player's getAudioCallbackLock()
        tempoSldr.onValueChange  = [&] () { pluginProcessor->getPlayHeadInfo().setBpm (tempoSldr.getValue()); };

        pluginWindow.reset (new ScaledDocumentWindow 
        (
            pluginProcessor->getName(),
            LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
            Desktop::getInstance().getGlobalScaleFactor()
        ));

        pluginWindow->onCloseButtonPressed = [&] { quit(); };
        pluginWindow->setUsingNativeTitleBar (true);
        pluginWindow->setContentOwned (editorComponent.get(), true);
        pluginWindow->setVisible (true);
        pluginWindow->setAlwaysOnTop (true);

        pluginProcessor->startPlaying();
    }
};
