a plugin template project i'm working on which aims to demystify creating custom standalone plugin filter apps (as juce's one is a bit chaotic and hard to navigate).

in addition to this, i'm working on a custom transport processor player class (in the standalone folder) which should give some sort of notion of time, bpm, and control of, etc in the standalone app.. i'm doing this so its easier to test plugins that have tempo sync, sequencers, delays etc.. i also like making my life harder for myself lol.

(its nothing special, just a modified version of juce's processor player that holds a private member of a derived audio play head and doesn't reinitialise it everytime the audio io callback is called - hopefully i'll make some more improvements on it when people tell me where i'm going wrong)

enjoy, pls be nice to me (i should probably put up some licensing stuff but i cba).
