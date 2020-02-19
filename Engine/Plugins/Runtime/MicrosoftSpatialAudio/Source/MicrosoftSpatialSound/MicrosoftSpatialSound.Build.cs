// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System;
public class MicrosoftSpatialSound : ModuleRules
{
	public MicrosoftSpatialSound(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"MicrosoftSpatialSound/Private"
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"MixedRealityInteropLibrary",
                "AudioExtensions"
            }
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
			}
			);
        PrivateIncludePathModuleNames.Add("TargetPlatform");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

        PublicDefinitions.Add("WITH_WINDOWS_MIXED_REALITY=1");
    }
}