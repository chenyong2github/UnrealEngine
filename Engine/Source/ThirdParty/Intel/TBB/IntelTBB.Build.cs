// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IntelTBB : ModuleRules
{
	public IntelTBB(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
			(Target.Platform == UnrealTargetPlatform.HoloLens))
		{
			string IntelTBBPath = Target.UEThirdPartySourceDirectory + "Intel/TBB/IntelTBB-2019u8/";
			string PlatformSubpath = (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x86) ? "Win32" : "Win64";

			string LibDir;
			if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
			{
				LibDir = System.String.Format("{0}lib/{1}/vc14/{2}/", IntelTBBPath, PlatformSubpath, Target.WindowsPlatform.GetArchitectureSubpath());
			}
			else
			{
				LibDir = System.String.Format("{0}lib/{1}/vc14/", IntelTBBPath, PlatformSubpath);
			}

			PublicSystemLibraryPaths.Add(LibDir);

			PublicSystemIncludePaths.Add(IntelTBBPath + "Include");

			// Disable the #pragma comment(lib, ...) used by default in MallocTBB...
			// We want to explicitly include the library.
			PublicDefinitions.Add("__TBBMALLOC_BUILD=1");

			string LibName = "tbbmalloc";
			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				LibName += "_debug";
			}
			LibName += ".lib";
			PublicAdditionalLibraries.Add(LibDir + LibName);
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicSystemIncludePaths.AddRange(
				new string[] {
					Target.UEThirdPartySourceDirectory + "Intel/TBB/IntelTBB-2019u8/include",
				}
			);

			PublicAdditionalLibraries.AddRange(
				new string[] {
					Target.UEThirdPartySourceDirectory + "Intel/TBB/IntelTBB-2019u8/lib/Mac/libtbb.a",
					Target.UEThirdPartySourceDirectory + "Intel/TBB/IntelTBB-2019u8/lib/Mac/libtbbmalloc.a",
				}
			);
		}
	}
}
