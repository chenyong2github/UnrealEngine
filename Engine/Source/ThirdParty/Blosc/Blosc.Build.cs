// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Blosc : ModuleRules
{
	public Blosc(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);

		string DeploymentDirectory = Path.Combine(ModuleDirectory, "Deploy", "c-blosc-1.21.0");

		PublicIncludePaths.Add(Path.Combine(DeploymentDirectory, "include"));

		string LibPostfix = bDebug ? "_d" : "";

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
				Target.WindowsPlatform.GetArchitectureSubpath(),
				"lib");

			PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "libblosc" + LibPostfix + ".lib"));
		}
		else
		{
			string Err = "Platform " + Target.Platform + " not supported!";
			System.Console.WriteLine(Err);
			throw new BuildException(Err);
		}
	}
}
