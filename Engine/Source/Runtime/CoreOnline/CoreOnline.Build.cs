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
		PublicDefinitions.Add("UNIQUENETID_ESPMODE=ESPMode::" + (bEnableThreadSafeUniqueNetIds ? "ThreadSafe" : "Fast"));
		// This is to ease migration to ESPMode::ThreadSafe. We have deprecated public FUniqueNetId constructors, by including it in the
		// ESPMode::Fast deprecation mechanism. The constructors are public when the ESPMode is Fast, and protected when it is ThreadSafe.
		PublicDefinitions.Add("UNIQUENETID_CONSTRUCTORVIS=" + (bEnableThreadSafeUniqueNetIds ? "protected" : "public"));
	}

}
