// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VirtualCamera : ModuleRules
{
	public VirtualCamera(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AugmentedReality",
				"CinematicCamera",
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"LevelSequence",
				"LiveLinkInterface",
				"MovieScene",
				"RemoteSession",
				"TimeManagement",
				"VPUtilities",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
			}
		);

		if (Target.Type == TargetType.Editor || Target.Type == TargetType.Program)
		{
			PrivateDefinitions.Add("VIRTUALCAMERA_WITH_CONCERT=1");
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Concert",
					"ConcertSyncClient",
					"MultiUserClient",
				}
			);
		}
		else
		{
			PrivateDefinitions.Add("VIRTUALCAMERA_WITH_CONCERT=0");
		}


		if (Target.bBuildEditor == true)
		{
			PublicDependencyModuleNames.Add("LevelSequenceEditor");
			PublicDependencyModuleNames.Add("Sequencer");
			PublicDependencyModuleNames.Add("SlateCore");
			PublicDependencyModuleNames.Add("TakeRecorder");
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
