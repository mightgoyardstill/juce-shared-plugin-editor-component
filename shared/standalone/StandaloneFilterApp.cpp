#include "StandaloneFilterApp.h"

juce::JUCEApplicationBase* juce_CreateApplication() 
{
    return new StandaloneFilterApp(); 
}