// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class RemoteControlProtocolWidgets : ModuleRules
{
	public RemoteControlProtocolWidgets(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
            new string[] {
				Path.Combine(ModuleDirectory, "Private"),
				Path.Combine(ModuleDirectory, "Private", "DetailCustomizations"),
			}
        );

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"EditorStyle",
			"EditorSubsystem",
			"EditorWidgets",
			"Engine",
			"GraphEditor",
			"InputCore",
			"Projects",
			"PropertyEditor",
			"RemoteControl",
			"RemoteControlProtocol",
			"Slate",
			"SlateCore",
			"UnrealEd"
		});
	}
}
