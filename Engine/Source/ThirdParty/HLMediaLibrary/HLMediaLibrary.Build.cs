// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class HLMediaLibrary : ModuleRules
{
	public const string LibraryName = "HLMediaLibrary";

	public HLMediaLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string BaseDir = Target.UEThirdPartySourceDirectory + LibraryName;

		string BinariesDir = Target.UEThirdPartyBinariesDirectory + LibraryName;

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		string Platform = (Target.Platform == UnrealTargetPlatform.HoloLens) ? "HoloLens" : "Windows";
		string Config = Target.Configuration == UnrealTargetConfiguration.Debug ? "Debug" : "Release";
		string Arch = Target.WindowsPlatform.GetArchitectureSubpath();
		string SubPath = Path.Combine(Platform, Config, Arch);

		string LibPath = Path.Combine(BaseDir, "lib", SubPath);
		string BinariesPath = Path.Combine(BinariesDir, SubPath);
		string dll = String.Format("{0}.dll", LibraryName);

		// windows desktop x64 target
		if (Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Win64 ||
			Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			// Add the import library
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, String.Format("{0}.lib", LibraryName)));

			// Delay-load the DLL, so we can load it from the right place first
			PublicDelayLoadDLLs.Add(dll);

			// Ensure that the DLL is staged along with the executable
			RuntimeDependencies.Add(Path.Combine(BinariesPath, dll));
		}
	}
}
