// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemoteControl : ModuleRules
{
	public RemoteControl(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"RemoteControlCommon",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Cbor",
				"Engine",
				"RemoteControlInterception",
				"Serialization",
				"SlateCore"
			}
		);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"DeveloperSettings",
					"MessageLog",
					"UnrealEd",
				}
			);
		}
	}
}
