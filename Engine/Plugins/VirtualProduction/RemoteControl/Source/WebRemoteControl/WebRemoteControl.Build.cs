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
				"AssetRegistry",
				"HTTPServer",
				"RemoteControl",
				"Serialization",
				"WebSocketNetworking"
			}
        );

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"ImageWrapper",
					"Settings",
					"SlateCore",
					"UnrealEd",
				}
			);
		}
	}
}
