// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class IntelTBB : ModuleRules
{
	public IntelTBB(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		
		string IntelTBBPath = Path.Combine(Target.UEThirdPartySourceDirectory, "Intel/TBB/IntelTBB-2019u8");
		string IntelTBBIncludePath = Path.Combine(IntelTBBPath, "include");
		string IntelTBBLibPath = Path.Combine(IntelTBBPath, "lib");

		PublicSystemIncludePaths.Add(IntelTBBIncludePath);		

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
			(Target.Platform == UnrealTargetPlatform.HoloLens))
		{
			string PlatformSubPath = (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x86) ? "Win32" : "Win64";
			
			string LibDirTBB = Path.Combine(IntelTBBLibPath, PlatformSubPath, "vc14");
			string LibDirTBBMalloc = LibDirTBB;
			
			if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
			{
				LibDirTBBMalloc = Path.Combine(LibDirTBBMalloc, Target.WindowsPlatform.GetArchitectureSubpath());
			}

			PublicSystemLibraryPaths.Add(LibDirTBB);
			PublicSystemLibraryPaths.Add(LibDirTBBMalloc);

			// Disable the #pragma comment(lib, ...) used by default in TBB & MallocTBB...
			// We want to explicitly include the libraries.
			PublicDefinitions.Add("__TBBMALLOC_NO_IMPLICIT_LINKAGE=1");
			PublicDefinitions.Add("__TBB_NO_IMPLICIT_LINKAGE=1");

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibDirTBB, "tbb_debug.lib"));
				PublicAdditionalLibraries.Add(Path.Combine(LibDirTBBMalloc, "tbbmalloc_debug.lib"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibDirTBB, "tbb.lib"));
				PublicAdditionalLibraries.Add(Path.Combine(LibDirTBBMalloc, "tbbmalloc.lib"));
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
