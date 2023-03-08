// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChangelistReview : ModuleRules
{
	public ChangelistReview(ReadOnlyTargetRules Target) : base(Target)
	{
		// The Perforce API on Mac leaks a bunch of heavily used terms
		// so to avoid unity builds failing, compile the module as non-unity
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			bUseUnity = false;
		}

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"CoreUObject",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"SlateCore",
				"Slate",
				"EditorFramework",
				"UnrealEd",
				"Kismet",
				"EditorStyle",
				"ToolMenus",
				"SourceControl",
				"InputCore",
				"HTTP",
				"Json"
			}
		);

		AddEngineThirdPartyPrivateStaticDependencies(Target, "Perforce");

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Mac)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
		}
	}
}