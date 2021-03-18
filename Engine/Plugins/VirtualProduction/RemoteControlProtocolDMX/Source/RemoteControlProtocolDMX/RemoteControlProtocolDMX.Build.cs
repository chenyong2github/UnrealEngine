// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemoteControlProtocolDMX : ModuleRules
{
	public RemoteControlProtocolDMX(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"DMXRuntime",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"DMXProtocol",
				"Engine",
				"RemoteControl",
				"RemoteControlProtocol",
			}
		);
	}
}
