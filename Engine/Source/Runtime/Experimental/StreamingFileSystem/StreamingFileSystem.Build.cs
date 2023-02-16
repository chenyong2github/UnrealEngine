// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class StreamingFileSystem : ModuleRules
{
	public StreamingFileSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"BuildPatchServices",
				"InstallBundleManager",
				"VirtualFileCache",
				"Json"
			}
		);
	}
}
