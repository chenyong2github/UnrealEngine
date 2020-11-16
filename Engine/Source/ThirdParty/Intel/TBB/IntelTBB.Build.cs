// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class IntelTBB : ModuleRules
{
	public IntelTBB(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		
		string IntelTBBPath = Path.Combine(Target.UEThirdPartySourceDirectory, "Intel/TBB/IntelTBB-2019u8");
		string IntelTBBIncludePath = Path.Combine(IntelTBBPath, "Include");
		string IntelTBBLibPath = Path.Combine(IntelTBBPath, "Lib");

		PublicSystemIncludePaths.Add(IntelTBBIncludePath);		

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
			(Target.Platform == UnrealTargetPlatform.HoloLens))
		{
			string PlatformSubPath = (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x86) ? "Win32" : "Win64";
			string LibDir = Path.Combine(IntelTBBLibPath, PlatformSubPath, "vc14");
			if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
			{
				LibDir = Path.Combine(LibDir, Target.WindowsPlatform.GetArchitectureSubpath());
			}

			PublicSystemLibraryPaths.Add(LibDir);

			// Disable the #pragma comment(lib, ...) used by default in TBB & MallocTBB...
			// We want to explicitly include the libraries.
			PublicDefinitions.Add("__TBBMALLOC_NO_IMPLICIT_LINKAGE=1");
			PublicDefinitions.Add("__TBB_NO_IMPLICIT_LINKAGE=1");

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibDir, "tbb_debug.lib"));
				PublicAdditionalLibraries.Add(Path.Combine(LibDir, "tbbmalloc_debug.lib"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibDir, "tbb.lib"));
				PublicAdditionalLibraries.Add(Path.Combine(LibDir, "tbbmalloc.lib"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string LibDir = Path.Combine(IntelTBBLibPath, "Mac");
			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libtbb.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libtbbmalloc.a"));
		}
	}
}
