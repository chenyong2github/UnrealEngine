// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemoteControlProtocolDMXEditor : ModuleRules
{
	public RemoteControlProtocolDMXEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {}
		);

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"DMXRuntime",
				"InputCore",
				"PropertyEditor",
				"RemoteControl",
				"RemoteControlProtocol",
				"RemoteControlProtocolDMX"
			}
		);
    }
}
