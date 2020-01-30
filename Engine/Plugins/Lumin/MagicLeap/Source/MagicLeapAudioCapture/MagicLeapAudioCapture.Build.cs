// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MagicLeapAudioCapture : ModuleRules
{
    public MagicLeapAudioCapture(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.Add("Core");
        PublicDependencyModuleNames.Add("MLSDK");
        PrivateDependencyModuleNames.Add("AudioCaptureCore");
        PublicDefinitions.Add("WITH_AUDIOCAPTURE=1");
    }
}
