// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class FlatBuffers : ModuleRules
{
	public FlatBuffers(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		// Win64, Linux and PS5
		if (Target.Platform == UnrealTargetPlatform.Win64 ||
			Target.Platform == UnrealTargetPlatform.Linux ||
			Target.Platform == UnrealTargetPlatform.PS5)
		{
			// PublicSystemIncludePaths
			string IncPath = Path.Combine(ModuleDirectory, "include/");
			PublicSystemIncludePaths.Add(IncPath);
			
			// PublicAdditionalLibraries
			string PlatformDir = Target.Platform.ToString();
			string LibDirPath = Path.Combine(ModuleDirectory, "lib", PlatformDir);
			string[] LibFileNames = new string[] {
				"flatbuffers"
			};

			foreach (string LibFileName in LibFileNames)
			{
				if(Target.Platform == UnrealTargetPlatform.Win64)
				{
					PublicAdditionalLibraries.Add(Path.Combine(LibDirPath, LibFileName + ".lib"));
				} 
				else if(Target.Platform == UnrealTargetPlatform.Linux)
				{
					PublicAdditionalLibraries.Add(Path.Combine(LibDirPath, "lib" + LibFileName + ".a"));
				}
				else if(Target.Platform == UnrealTargetPlatform.PS5)
				{
					string LibDirPathPS5 = Path.Combine(ModuleDirectory, "lib", "Playstation5");
					PublicAdditionalLibraries.Add(Path.Combine(LibDirPathPS5, "lib" + LibFileName + ".a"));
				}
			}
		}
	}
}
