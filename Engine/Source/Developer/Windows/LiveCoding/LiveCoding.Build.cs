// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class LiveCoding : ModuleRules
{
	public LiveCoding(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("CoreUObject");
		PrivateDependencyModuleNames.Add("Settings");

		if(Target.bUseDebugLiveCodingConsole)
        {
            PrivateDefinitions.Add("USE_DEBUG_LIVE_CODING_CONSOLE=1");
        }
		else
        {
            PrivateDefinitions.Add("USE_DEBUG_LIVE_CODING_CONSOLE=0");
        }

		PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Private", "External", "LC_JumpToSelf.lib"));
	}
}
