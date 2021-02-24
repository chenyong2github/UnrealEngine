// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemoteControlEditor : ModuleRules
{
	public RemoteControlEditor(ReadOnlyTargetRules Target) : base(Target)
    {
		PublicDependencyModuleNames.AddRange(
			new string[] {
                "Core",
                "CoreUObject",
				"RemoteControl",
				"Engine",
				"Json",
			}
        );

		PrivateDependencyModuleNames.AddRange(
            new string[] {
			}
        );

	}
}
