// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class MaterialX : ModuleRules
{
	public MaterialX(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);

		string DeploymentDirectory = Path.Combine(ModuleDirectory, "Deploy", "MaterialX-1.38.1");

		PublicIncludePaths.Add(Path.Combine(DeploymentDirectory, "include"));

		string[] MaterialXLibraries = {
			"MaterialXCore",
			"MaterialXFormat",
			"MaterialXGenGlsl",
			"MaterialXGenMdl",
			"MaterialXGenOsl",
			"MaterialXGenShader",
			"MaterialXRender",
			"MaterialXRenderGlsl",
			"MaterialXRenderHw",
			"MaterialXRenderOsl"
		};

		string LibPostfix = bDebug ? "_d" : "";

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
				Target.WindowsPlatform.GetArchitectureSubpath(),
				"lib");

			foreach (string MaterialXLibrary in MaterialXLibraries)
			{
				string StaticLibName = MaterialXLibrary + LibPostfix + ".lib";
				PublicAdditionalLibraries.Add(
					Path.Combine(LibDirectory, StaticLibName));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// Not yet supported
			/*
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"Mac",
				"lib");

			foreach (string MaterialXLibrary in MaterialXLibraries)
			{
				string StaticLibName = MaterialXLibrary + LibPostfix + ".a";
				PublicAdditionalLibraries.Add(
					Path.Combine(LibDirectory, StaticLibName));
			}
			*/
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			// Not yet supported
			/*
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"Unix",
				Target.Architecture,
				"lib");

			foreach (string MaterialXLibrary in MaterialXLibraries)
			{
				string StaticLibName = MaterialXLibrary + LibPostfix + ".a";
				PublicAdditionalLibraries.Add(
					Path.Combine(LibDirectory, StaticLibName));
			}
			*/
		}
	}
}
