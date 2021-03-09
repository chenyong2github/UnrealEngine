// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OnlineSubsystem : ModuleRules
{
	public OnlineSubsystem(ReadOnlyTargetRules Target) : base(Target)
    {
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Json",
				"SignalProcessing"
			}
		);

		PublicIncludePaths.Add(ModuleDirectory);

        PublicDefinitions.Add("ONLINESUBSYSTEM_PACKAGE=1");
		PublicDefinitions.Add("DEBUG_LAN_BEACON=0");
		PublicDefinitions.Add("PLATFORM_MAX_LOCAL_PLAYERS=" + GetPlatformMaxLocalPlayers(Target));

		// OnlineSubsystem cannot depend on Engine!
		PrivateDependencyModuleNames.AddRange(
			new string[] { 
				"Core", 
				"CoreUObject",
				"ImageCore",
				"Sockets",
				"JsonUtilities",
				"AudioMixerCore",
				"SignalProcessing",
			}
		);
	}

	protected virtual int GetPlatformMaxLocalPlayers(ReadOnlyTargetRules Target)
	{
		// 0 indicates no platform override
		return 0;
	}
}
