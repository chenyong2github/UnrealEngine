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
				"HTTPServer"
			}
		);

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
				"RemoteControl",
				"RemoteControlCommon",
				"Serialization",
				"Sockets",
				"WebSocketNetworking"
			}
        );

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"DeveloperSettings",
					"Engine",
					"ImageWrapper",
					"RemoteControlUI",
					"Settings",
					"Slate",
					"SlateCore",
					"UnrealEd",
				}
			);
		}
	}
}
