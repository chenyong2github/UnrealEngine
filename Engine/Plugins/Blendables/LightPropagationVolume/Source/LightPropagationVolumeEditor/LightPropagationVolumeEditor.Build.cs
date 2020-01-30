// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LightPropagationVolumeEditor : ModuleRules
{
    public LightPropagationVolumeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {	
				"Core",
				"CoreUObject",
                "UnrealEd",         // for UAssetEditorSubsystem
				"AssetTools",       // class FAssetTypeActions_Base
				"LightPropagationVolumeRuntime",
			}
		);

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
				"UnrealEd",
			}
        );
	}
}
