// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Imath : ModuleRules
{
	public Imath(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);

		string DeploymentDirectory = Path.Combine(ModuleDirectory, "Deploy", "Imath-3.1.3");

		PublicIncludePaths.Add(Path.Combine(DeploymentDirectory, "include"));

		// XXX: OpenEXR and Alembic include some Imath headers without the
		// leading "Imath/..."
		PublicIncludePaths.Add(Path.Combine(DeploymentDirectory, "include", "Imath"));

		string LibPostfix = bDebug ? "_d" : "";

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			string WindowsPlatformDirectory = Path.Combine(
				DeploymentDirectory,
				"VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
				Target.WindowsPlatform.GetArchitectureSubpath());

			string BinDirectory = Path.Combine(WindowsPlatformDirectory, "bin");
			string LibDirectory = Path.Combine(WindowsPlatformDirectory, "lib");

			string DynamicLibName = "Imath-3_1" + LibPostfix + ".dll";
			string StaticLibName = "Imath-3_1" + LibPostfix + ".lib";

			RuntimeDependencies.Add(
				Path.Combine("$(TargetOutputDir)", DynamicLibName),
				Path.Combine(BinDirectory, DynamicLibName));

			PublicAdditionalLibraries.Add(
				Path.Combine(LibDirectory, StaticLibName));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"Mac",
				"lib");

			string StaticLibName = "libImath-3_1" + LibPostfix + ".a";

			PublicAdditionalLibraries.Add(
				Path.Combine(LibDirectory, StaticLibName));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"Unix",
				Target.Architecture,
				"lib");

			string StaticLibName = "libImath-3_1" + LibPostfix + ".a";

			PublicAdditionalLibraries.Add(
				Path.Combine(LibDirectory, StaticLibName));
		}
	}
}
