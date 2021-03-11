
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Flite : ModuleRules
{
	public Flite(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string FlitePath = Path.Combine(ModuleDirectory, "Flite-e0a3d25");
		string FliteBinaryPath = Path.Combine(ModuleDirectory, "lib");

		PublicIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(FlitePath, "include")
			}
		);
		PublicDefinitions.Add("USING_FLITE=1");
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Only VS2019 supported as of now
			string VSVersion = "VS2019";
			bool bUseDebugLibs = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT;
			PublicAdditionalLibraries.AddRange(
				new string[]
				{
					Path.Combine(FliteBinaryPath, "Win64", VSVersion , bUseDebugLibs ? "Debug" : "Release", "libFlite.lib")
				}
			);
		}
	}
}
