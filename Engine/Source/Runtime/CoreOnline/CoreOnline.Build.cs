// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using EpicGames.Core;

public class CoreOnline : ModuleRules
{
	[ConfigFile(ConfigHierarchyType.Engine, "CoreOnline")]
	bool bEnableThreadSafeUniqueNetIds = false;

	public CoreOnline(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject"
			}
		);

		ConfigCache.ReadSettings(DirectoryReference.FromFile(Target.ProjectFile), Target.Platform, this);
	}

}
