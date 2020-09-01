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

		PublicDefinitions.Add("MAX_LOCAL_PLAYERS=" + GetMaxLocalPlayers(Target));

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

	protected virtual int GetMaxLocalPlayers(ReadOnlyTargetRules Target)
	{
		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			return 4;
		}
		return 1;
	}
}
