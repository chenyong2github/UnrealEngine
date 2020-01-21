// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class zlib : ModuleRules
{
	protected string LatestZlibVersion;
	protected string CurrentZlibVersion;
	protected string OldZlibVersion;

	public zlib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		LatestZlibVersion = "zlib-1.2.11";
		CurrentZlibVersion = "v1.2.8";
		OldZlibVersion = "zlib-1.2.5";

		string LatestzlibPath = Target.UEThirdPartySourceDirectory + "zlib/" + LatestZlibVersion;
		string zlibPath = Target.UEThirdPartySourceDirectory + "zlib/" + CurrentZlibVersion;
		// TODO: recompile for consoles and mobile platforms
		string OldzlibPath = Target.UEThirdPartySourceDirectory + "zlib/" + OldZlibVersion;

		// On Windows x64, use the llvm compiled version which is quite a bit faster than the MSVC compiled version.
		if (Target.Platform == UnrealTargetPlatform.Win64 &&
		    Target.WindowsPlatform.Architecture == WindowsArchitecture.x64)
		{
			string LibDir  = System.String.Format("{0}/lib/Win64-llvm/{1}/", LatestzlibPath, Target.Configuration != UnrealTargetConfiguration.Debug ? "Release" : "Debug");
			string LibName = System.String.Format("zlibstatic{0}.lib", Target.Configuration != UnrealTargetConfiguration.Debug ? "" : "d");
			PublicAdditionalLibraries.Add(LibDir + LibName);

			// continue to include the same include as before, still valid as the new zlib is ABI compatible
			string PlatformSubpath = Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x86 ? "Win32" : "Win64";
			PublicIncludePaths.Add(System.String.Format("{0}/include/{1}/VS{2}", zlibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName()));
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64 ||
				 Target.Platform == UnrealTargetPlatform.Win32 ||
				 Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			string PlatformSubpath = Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x86 ? "Win32" : "Win64";
			PublicIncludePaths.Add(System.String.Format("{0}/include/{1}/VS{2}", zlibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName()));
			string LibDir;

			if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                LibDir = System.String.Format("{0}/lib/{1}/VS{2}/{3}/", zlibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath());
            }
            else
            {
                LibDir = System.String.Format("{0}/lib/{1}/VS{2}/{3}/", zlibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.Configuration == UnrealTargetConfiguration.Debug ? "Debug" : "Release");
            }
            PublicAdditionalLibraries.Add(LibDir + "zlibstatic.lib");
        }
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string platform = "/Mac/";
			PublicIncludePaths.Add(zlibPath + "/include" + platform);
			// OSX needs full path
			PublicAdditionalLibraries.Add(zlibPath + "/lib" + platform + "libz.a");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS ||
				 Target.Platform == UnrealTargetPlatform.TVOS)
		{
			PublicIncludePaths.Add(OldzlibPath + "/Inc");
			PublicSystemLibraries.Add("z");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			PublicIncludePaths.Add(OldzlibPath + "/Inc");
			PublicSystemLibraries.Add("z");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string platform = "/Linux/" + Target.Architecture;
			PublicIncludePaths.Add(zlibPath + "/include" + platform);
			PublicAdditionalLibraries.Add(zlibPath + "/lib/" + platform + "/libz_fPIC.a");
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				PublicIncludePaths.Add(OldzlibPath + "/Inc");
				PublicAdditionalLibraries.Add(OldzlibPath + "/Lib/XboxOne/VS" + VersionName.ToString() + "/zlib125_XboxOne.lib");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			PublicIncludePaths.Add(OldzlibPath + "/inc");
			PublicAdditionalLibraries.Add(Path.Combine(OldzlibPath, "Lib/Switch/libz.a"));
		}
	}
}
