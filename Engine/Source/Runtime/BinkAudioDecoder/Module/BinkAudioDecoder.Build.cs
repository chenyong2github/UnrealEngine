// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Collections.Generic;
using System.Security.Cryptography;
using UnrealBuildTool;

public class BinkAudioDecoder : ModuleRules
{
    // virtual so that NDA platforms can hide secrets
    protected virtual string GetLibrary(ReadOnlyTargetRules Target)
    {
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            return Path.Combine(ModuleDirectory, "..", "SDK", "BinkAudio", "Lib", "binka_ue_decode_win64_static.lib");
        }
        if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            return Path.Combine(ModuleDirectory, "..", "SDK", "BinkAudio", "Lib", "libbinka_ue_decode_lnx64_static.a");
        }
        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            return Path.Combine(ModuleDirectory, "..", "SDK", "BinkAudio", "Lib", "libbinka_ue_decode_osxfat_static.a");
        }
        if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            return Path.Combine(ModuleDirectory, "..", "SDK", "BinkAudio", "Lib", "libbinka_ue_decode_ios_static.a");
        }
        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            return Path.Combine(ModuleDirectory, "..", "SDK", "BinkAudio", "Lib", "libbinka_ue_decode_androidarm64_static.a");
        }        
        return null;
    }

    public BinkAudioDecoder(ReadOnlyTargetRules Target) : base(Target)
    {
        ShortName = "BinkAudioDecoder";

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            }
        );

        PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "SDK", "BinkAudio", "Include"));

        string Library = GetLibrary(Target);
        if (Library != null)
        {
			// We have to have this because some audio mixers are shared across platforms
			// that we don't have libs for (e.g. hololens).
            PublicDefinitions.Add("WITH_BINK_AUDIO=1");
            PublicAdditionalLibraries.Add(Library);
        }
        else
        {
            PublicDefinitions.Add("WITH_BINK_AUDIO=0");
        }
    }
}
