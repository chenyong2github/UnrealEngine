// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Diagnostics;
using System.Collections.Generic;

public class AlembicLib : ModuleRules
{
	public AlembicLib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.Linux)
		{
			bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);

			string LibDir = ModuleDirectory + "/AlembicDeploy/";
			string Platform;
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				Platform = "x64";
				LibDir += "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				Platform = "Mac";
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				Platform = "Linux";
			}
			else
			{
				// unsupported
				return;
			}
			LibDir += Platform + "/lib/";

			string Hdf5LibPostFix = bDebug ? "_debug" : "";
			string AlembicLibPostFix = bDebug ? "d" : "";
			string LibExtension = (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.Linux) ? ".a" : ".lib";

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicDefinitions.Add("H5_BUILT_AS_DYNAMIC_LIB");

				// The Windows lib post-fix for HDF5 is different from Mac and Linux.
				Hdf5LibPostFix = bDebug ? "_D" : "";

				List<string> ReqLibraryNames = new List<string>();
				ReqLibraryNames.AddRange
				(
					new string[] {
						"hdf5" + Hdf5LibPostFix,
						"Alembic" + AlembicLibPostFix
				});
				foreach (string LibraryName in ReqLibraryNames)
				{
					PublicAdditionalLibraries.Add(LibDir + LibraryName + LibExtension);
				}

				if (Target.bDebugBuildsActuallyUseDebugCRT && bDebug)
				{
					RuntimeDependencies.Add("$(BinaryOutputDir)/zlibd1.dll", "$(ModuleDir)/Binaries/Win64/zlibd1.dll", StagedFileType.NonUFS);
					RuntimeDependencies.Add("$(BinaryOutputDir)/hdf5_D.dll", "$(ModuleDir)/Binaries/Win64/hdf5_D.dll", StagedFileType.NonUFS);
				}
				else
				{
					RuntimeDependencies.Add("$(BinaryOutputDir)/hdf5.dll", "$(ModuleDir)/Binaries/Win64/hdf5.dll", StagedFileType.NonUFS);
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				List<string> ReqLibraryNames = new List<string>();
				ReqLibraryNames.AddRange
				(
					new string[] {
						"libhdf5" + Hdf5LibPostFix,
						"libAlembic" + AlembicLibPostFix
				});
				foreach (string LibraryName in ReqLibraryNames)
				{
					PublicAdditionalLibraries.Add(LibDir + LibraryName + LibExtension);
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				List<string> ReqLibraryNames = new List<string>();
				ReqLibraryNames.AddRange
				(
					new string[] {
						"libhdf5-static",
						"libAlembic"
				});
				foreach (string LibraryName in ReqLibraryNames)
				{
					PublicAdditionalLibraries.Add(LibDir + Target.Architecture + "/" + LibraryName + LibExtension);
				}
			}

			PublicIncludePaths.Add(ModuleDirectory + "/AlembicDeploy/include/");

			PublicDependencyModuleNames.Add("UEOpenExr");
		}
	}
}
