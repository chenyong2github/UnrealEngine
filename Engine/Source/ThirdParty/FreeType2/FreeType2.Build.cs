// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class FreeType2 : ModuleRules
{
	public FreeType2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add("WITH_FREETYPE=1");

		string FreeType2Path;
		string FreeType2LibPath;

		if (Target.Platform == UnrealTargetPlatform.IOS ||
			Target.Platform == UnrealTargetPlatform.Mac ||
			Target.Platform == UnrealTargetPlatform.PS4 ||
			Target.Platform == UnrealTargetPlatform.Switch ||
			Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Win64 ||
			Target.Platform == UnrealTargetPlatform.XboxOne ||
			Target.IsInPlatformGroup(UnrealPlatformGroup.Android) ||
			Target.IsInPlatformGroup(UnrealPlatformGroup.Unix)
			)
		{
			// FreeType needs these to deal with bitmap fonts
			AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "UElibPNG");

			FreeType2Path = Target.UEThirdPartySourceDirectory + "FreeType2/FreeType2-2.10.0/";
			FreeType2LibPath = FreeType2Path + "lib/";
			PublicSystemIncludePaths.Add(FreeType2Path + "include");
			PublicDefinitions.Add("WITH_FREETYPE_V210=1"); // TODO: Remove this once everything is using FreeType 2.10.0
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			FreeType2Path = Target.UEThirdPartySourceDirectory + "FreeType2/FreeType2-2.4.12/";
			FreeType2LibPath = FreeType2Path + "Lib/";
			PublicSystemIncludePaths.Add(FreeType2Path + "include");
		}
		else
		{
			FreeType2Path = Target.UEThirdPartySourceDirectory + "FreeType2/FreeType2-2.6/";
			FreeType2LibPath = FreeType2Path + "Lib/";
			PublicSystemIncludePaths.Add(FreeType2Path + "Include");
		}

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			FreeType2LibPath += (Target.Platform == UnrealTargetPlatform.Win64) ? "Win64/" : "Win32/";
			FreeType2LibPath += "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
			FreeType2LibPath += "/";

			string FreeType2LibName = "freetype.lib";
			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				FreeType2LibPath += "Debug";
				FreeType2LibName = "freetyped.lib";
			}
			else
			{
				FreeType2LibPath += "Release";
				//FreeType2LibPath += "RelWithDebInfo";
			}

			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, FreeType2LibName));
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			string PlatformSubpath = Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x86 ? "Win32" : "Win64";

			string LibDir;
			if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
			{
				LibDir = System.String.Format("{0}Lib/{1}/VS{2}/{3}/", FreeType2Path, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath());
			}
			else
			{
				LibDir = System.String.Format("{0}Lib/{1}/VS{2}/", FreeType2Path, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			}

			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "freetype26MT.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			FreeType2LibPath += "Mac/";

			string FreeType2LibName = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "libfreetyped.a" : "libfreetype.a";
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, FreeType2LibName));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			FreeType2LibPath += "IOS/";

			string FreeType2LibName = "libfreetype.a";
			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				FreeType2LibPath += "Debug";
				FreeType2LibName = "libfreetyped.a";
			}
			else
			{
				FreeType2LibPath += "Release";
			}

			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, FreeType2LibName));
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			string LibDir;
			if (Target.Architecture == "-simulator")
			{
				LibDir = FreeType2LibPath + "TVOS/Simulator";
			}
			else
			{
				LibDir = FreeType2LibPath + "TVOS/Device";
			}

			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libfreetype2412.a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			FreeType2LibPath += "Android/";

			string FreeType2LibFolder = "Release";
			string FreeType2LibName = "libfreetype.a";
			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				FreeType2LibFolder = "Debug";
				FreeType2LibName = "libfreetyped.a";
			}

			// filtered out in the toolchain
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "ARMv7", FreeType2LibFolder, FreeType2LibName));
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "ARM64", FreeType2LibFolder, FreeType2LibName));
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "x86", FreeType2LibFolder, FreeType2LibName));
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "x64", FreeType2LibFolder, FreeType2LibName));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.Type == TargetType.Server)
			{
				string Err = string.Format("{0} dedicated server is made to depend on {1}. We want to avoid this, please correct module dependencies.", Target.Platform.ToString(), this.ToString());
				System.Console.WriteLine(Err);
				throw new BuildException(Err);
			}

			FreeType2LibPath += "Linux/";

			string FreeType2LibName = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "libfreetyped_fPIC.a" : "libfreetype_fPIC.a";
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, Target.Architecture, FreeType2LibName));
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
			PublicAdditionalLibraries.Add(FreeType2Path + "Lib/HTML5/libfreetype260" + OpimizationSuffix + ".bc");
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			FreeType2LibPath += "PS4/";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				FreeType2LibPath += "Debug";
			}
			else
			{
				FreeType2LibPath += "Release";
			}

			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "libfreetype.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);

				FreeType2LibPath += "XboxOne/";
				FreeType2LibPath += "VS" + VersionName.ToString();
				FreeType2LibPath += "/";

				string FreeType2LibName = "freetype.lib";
				if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
				{
					FreeType2LibPath += "Debug";
					FreeType2LibName = "freetyped.lib";
				}
				else
				{
					FreeType2LibPath += "Release";
				}

				PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, FreeType2LibName));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			FreeType2LibPath += "Switch/";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				FreeType2LibPath += "Debug";
			}
			else
			{
				FreeType2LibPath += "Release";
			}

			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "libfreetype.a"));
		}
	}
}
