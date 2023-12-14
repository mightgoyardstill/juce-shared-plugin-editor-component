#include "StandaloneFilterApp.h"

extern juce::JUCEApplicationBase* juce_CreateApplication();

juce::JUCEApplicationBase* juce_CreateApplication() 
{
    return new StandaloneFilterApp(); 
}