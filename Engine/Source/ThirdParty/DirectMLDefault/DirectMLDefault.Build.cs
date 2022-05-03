// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class DirectMLDefault : ModuleRules
{
    public DirectMLDefault(ReadOnlyTargetRules Target) : base(Target)
    {
		Type = ModuleType.External;
		// Win64
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// PublicSystemIncludePaths
			string IncPath = Path.Combine(ModuleDirectory, "include/");
			PublicSystemIncludePaths.Add(IncPath);
			// PublicAdditionalLibraries
			string PlatformDir = Target.Platform.ToString();
			string LibDirPath = Path.Combine(ModuleDirectory, "lib", PlatformDir);
			string[] LibFileNames = new string[] {
				"DirectML",
			};
			foreach(string LibFileName in LibFileNames)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibDirPath, LibFileName + ".lib"));
			}
			// PublicDelayLoadDLLs
			string DLLReleaseFileName = LibFileNames[0] + ".dll";
			string DLLDebugFileName = LibFileNames[0] + ".Debug.dll";

			bool ReleaseMode = true;
			string DLLFileName;

			if (ReleaseMode == true)
            {
				DLLFileName = DLLReleaseFileName;
			}
            else
            {
				DLLFileName = DLLDebugFileName;
			}

			PublicDelayLoadDLLs.Add(DLLFileName);
			// RuntimeDependencies
			string BinaryThirdPartyDirPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "bin", PlatformDir));
			string DLLFullPath = Path.Combine(BinaryThirdPartyDirPath, DLLFileName);
			RuntimeDependencies.Add(DLLFullPath);

			// PublicDefinitions
			PublicDefinitions.Add("DIRECTML_USE_DLLS");
			PublicDefinitions.Add("WITH_DIRECTML");
			PublicDefinitions.Add("DIRECTML_PLATFORM_PATH=bin/" + PlatformDir);
			PublicDefinitions.Add("DIRECTML_DLL_NAME=" + DLLFileName);
		}
		
	}
}
