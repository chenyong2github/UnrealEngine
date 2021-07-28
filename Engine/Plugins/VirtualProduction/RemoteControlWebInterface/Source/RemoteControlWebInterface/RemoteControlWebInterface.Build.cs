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

		PrivateDependencyModuleNames.AddRange(
			new string[] 
			{
				"RemoteControlCommon",
			}
		);


        if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] 
				{
					"DeveloperSettings",
					"EditorStyle",
					"EditorWidgets",
					"PropertyEditor",
					"RemoteControl",
					"RemoteControlUI",
					"Settings",
					"UnrealEd"
				}
			);
		}
	}
}
