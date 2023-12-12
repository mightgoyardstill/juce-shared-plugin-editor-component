#include <JuceHeader.h>
#include "Plugin/PluginProcessor.h"
#include "Shared/StandaloneFilterApp.h"

extern JUCEApplicationBase* juce_CreateApplication();
extern AudioProcessor* JUCE_CALLTYPE createPluginFilter();

//==============================================================================
// this creates new instances of the standalone filter app..
JUCEApplicationBase* juce_CreateApplication()
{
    return new StandaloneFilterApp();
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter() 
{
    return new PluginProcessor();
}