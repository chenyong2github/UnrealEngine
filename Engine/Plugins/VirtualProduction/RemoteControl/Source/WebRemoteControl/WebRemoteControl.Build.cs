// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WebRemoteControl : ModuleRules
{
	public WebRemoteControl(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
			}
		);

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"HTTPServer",
				"RemoteControl",
				"Serialization",
			}
        );
    }
}
