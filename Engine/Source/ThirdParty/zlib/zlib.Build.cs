// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class zlib : ModuleRules
{
	public zlib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string zlibPath = Target.UEThirdPartySourceDirectory + "zlib/v1.2.8/";

		// TODO: recompile for consoles and mobile platforms
		string OldzlibPath = Target.UEThirdPartySourceDirectory + "zlib/zlib-1.2.5/";

        // @ATG_CHANGE : BEGIN HoloLens support
        if ((Target.Platform == UnrealTargetPlatform.Win64) ||
            (Target.Platform == UnrealTargetPlatform.Win32) ||
            (Target.Platform == UnrealTargetPlatform.HoloLens))
        {
            string PlatformSubpath = Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x86 ? "Win32" : "Win64";
            PublicIncludePaths.Add(System.String.Format("{0}include/Win32/VS{1}", zlibPath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName()));
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                PublicLibraryPaths.Add(System.String.Format("{0}lib/{1}/VS{2}/{3}/", zlibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath()));
            }
            else
            {
                PublicLibraryPaths.Add(System.String.Format("{0}lib/{1}/VS{2}/{3}/", zlibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.Configuration == UnrealTargetConfiguration.Debug ? "Debug" : "Release"));
            }
            PublicAdditionalLibraries.Add("zlibstatic.lib");
        }
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string platform = "/Mac/";
			PublicIncludePaths.Add(zlibPath + "include" + platform);
			// OSX needs full path
			PublicAdditionalLibraries.Add(zlibPath + "lib" + platform + "libz.a");
		}

		else if (Target.Platform == UnrealTargetPlatform.IOS||
				 Target.Platform == UnrealTargetPlatform.TVOS)
		{
			PublicIncludePaths.Add(OldzlibPath + "Inc");
			PublicAdditionalLibraries.Add("z");
		}

		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			PublicIncludePaths.Add(OldzlibPath + "Inc");
			PublicAdditionalLibraries.Add("z");
		}

		else if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
			string OpimizationSuffix = "";
			if (Target.bCompileForSize)
			{
				OpimizationSuffix = "_Oz";
			}
			else
			{
				if (Target.Configuration == UnrealTargetConfiguration.Development)
				{
					OpimizationSuffix = "_O2";
				}
				else if (Target.Configuration == UnrealTargetConfiguration.Shipping)
				{
					OpimizationSuffix = "_O3";
				}
			}
			PublicIncludePaths.Add(OldzlibPath + "Inc");
			PublicAdditionalLibraries.Add(OldzlibPath + "Lib/HTML5/zlib" + OpimizationSuffix + ".bc");
		}

		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string platform = "/Linux/" + Target.Architecture;
			PublicIncludePaths.Add(zlibPath + "include" + platform);
			PublicAdditionalLibraries.Add(zlibPath + "/lib/" + platform + ((Target.LinkType == TargetLinkType.Monolithic) ? "/libz" : "/libz_fPIC") + ".a");
		}

		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			PublicIncludePaths.Add(OldzlibPath + "Inc");
			PublicLibraryPaths.Add(OldzlibPath + "Lib/PS4");
			PublicAdditionalLibraries.Add("z");
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				PublicIncludePaths.Add(OldzlibPath + "Inc");
				PublicLibraryPaths.Add(OldzlibPath + "Lib/XboxOne/VS" + VersionName.ToString());
				PublicAdditionalLibraries.Add("zlib125_XboxOne.lib");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			PublicIncludePaths.Add(OldzlibPath + "inc");
			PublicAdditionalLibraries.Add(System.IO.Path.Combine(OldzlibPath, "Lib/Switch/libz.a"));
		}
	}
}
