// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UElibPNG : ModuleRules
{
	public UElibPNG(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string libPNGPath = Target.UEThirdPartySourceDirectory + "libPNG/libPNG-1.5.2";
		string LibDir = libPNGPath;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			LibDir = libPNGPath + "/lib/Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";

			string LibFileName = "libpng" + (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "d" : "") + "_64.lib";
			PublicAdditionalLibraries.Add(LibDir + LibFileName);
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			LibDir = libPNGPath + "/lib/Win32/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";

			string LibFileName = "libpng" + (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "d" : "") + ".lib";
			PublicAdditionalLibraries.Add(LibDir + LibFileName);
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            string PlatformSubpath = Target.Platform.ToString();
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                LibDir = System.String.Format("{0}/lib/{1}/VS{2}/{3}/", libPNGPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath()) + "/";
            }
            else
            {
                LibDir = System.String.Format("{0}/lib/{1}/VS{2}/", libPNGPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName()) + "/";
            }

            string LibFileName = "libpng";
			if(Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
            {
                LibFileName += "d";
            }
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x64)
            {
                LibFileName += "_64";
            }
            PublicAdditionalLibraries.Add(LibDir + LibFileName + ".lib");
        }
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(LibDir + "/lib/Mac/libpng.a");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			if (Target.Architecture == "-simulator")
			{
				LibDir = libPNGPath + "/lib/ios/Simulator/";
			}
			else
			{
				LibDir = libPNGPath + "/lib/ios/Device/";
			}

			PublicAdditionalLibraries.Add(LibDir + "libpng152.a");
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			if (Target.Architecture == "-simulator")
			{
				LibDir = libPNGPath + "/lib/TVOS/Simulator/";
			}
			else
			{
				LibDir = libPNGPath + "/lib/TVOS/Device/";
			}

			PublicAdditionalLibraries.Add(LibDir + "libpng152.a");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			libPNGPath = Target.UEThirdPartySourceDirectory + "libPNG/libPNG-1.5.27";

			PublicAdditionalLibraries.Add(libPNGPath + "/lib/Android/ARM64/libpng.a");
			PublicAdditionalLibraries.Add(libPNGPath + "/lib/Android/x86/libpng.a");
			PublicAdditionalLibraries.Add(libPNGPath + "/lib/Android/x64/libpng.a");
			PublicAdditionalLibraries.Add(libPNGPath + "/lib/Android/ARMv7/libpng.a");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			// migrate all architectures to the newer binary
			if (Target.Architecture.StartsWith("aarch64") || Target.Architecture.StartsWith("i686"))
			{
				libPNGPath = Target.UEThirdPartySourceDirectory + "libPNG/libPNG-1.5.27";
			}

			PublicAdditionalLibraries.Add(libPNGPath + "/lib/Linux/" + Target.Architecture + "/libpng.a");
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
			PublicAdditionalLibraries.Add(LibDir + "/lib/HTML5/libpng" + OpimizationSuffix + ".bc");
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			LibDir = libPNGPath + "/lib/PS4/";
			PublicAdditionalLibraries.Add(LibDir + "libpng152.a");
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				LibDir = libPNGPath + "/lib/XboxOne/VS" + VersionName.ToString() + "/";
				PublicAdditionalLibraries.Add(LibDir + "libpng125_XboxOne.lib");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			PublicAdditionalLibraries.Add(System.IO.Path.Combine(libPNGPath, "lib/Switch/libPNG.a"));
		}

		PublicIncludePaths.Add(libPNGPath);
	}
}
