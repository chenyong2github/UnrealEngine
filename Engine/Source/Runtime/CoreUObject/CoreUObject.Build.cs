// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using EpicGames.Core;

public class CoreUObject : ModuleRules
{
	[ConfigFile(ConfigHierarchyType.Engine, "CoreUObject")]
	bool bEnableThreadSafeUniqueNetIds = false;

	public CoreUObject(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivatePCHHeaderFile = "Private/CoreUObjectPrivatePCH.h";

		SharedPCHHeaderFile = "Public/CoreUObjectSharedPCH.h";

		PrivateIncludePaths.Add("Runtime/CoreUObject/Private");

        PrivateIncludePathModuleNames.AddRange(
                new string[] 
			    {
				    "TargetPlatform",
			    }
            );

		PublicDependencyModuleNames.Add("Core");
        PublicDependencyModuleNames.Add("TraceLog");

		PrivateDependencyModuleNames.Add("Projects");
        PrivateDependencyModuleNames.Add("Json");

		ConfigCache.ReadSettings(DirectoryReference.FromFile(Target.ProjectFile), Target.Platform, this);
		bool bUseThreadSafeUniqueNetIds = bEnableThreadSafeUniqueNetIds && Target.Type != TargetType.Program;
		PublicDefinitions.Add("UNIQUENETID_ESPMODE=ESPMode::" + (bUseThreadSafeUniqueNetIds ? "ThreadSafe" : "Fast"));
		// This is to ease migration to ESPMode::ThreadSafe. We have deprecated public FUniqueNetId constructors, by including it in the
		// ESPMode::Fast deprecation mechanism. The constructors are public when the ESPMode is Fast, and protected when it is ThreadSafe.
		PublicDefinitions.Add("UNIQUENETID_CONSTRUCTORVIS=" + (bUseThreadSafeUniqueNetIds ? "protected" : "public"));
	}

}
