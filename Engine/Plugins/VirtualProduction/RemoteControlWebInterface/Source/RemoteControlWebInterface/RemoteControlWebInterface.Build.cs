// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemoteControlWebInterface : ModuleRules
{
	public RemoteControlWebInterface(ReadOnlyTargetRules Target) : base(Target)
	{
		ShortName = "RCWebIntf";

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"Projects",
				"WebRemoteControl"
			}
			);

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"EditorStyle",
					"RemoteControlUI",
					"Settings",
					"UnrealEd"
				}
			);
		}
	}
}
