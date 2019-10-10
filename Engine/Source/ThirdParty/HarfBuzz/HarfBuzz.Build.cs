// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System;
using System.IO;

public class HarfBuzz : ModuleRules
{
	public HarfBuzz(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// Can't be used without our dependencies
		if (!Target.bCompileFreeType || !Target.bCompileICU)
		{
			PublicDefinitions.Add("WITH_HARFBUZZ=0");
			return;
		}

		string HarfBuzzVersion = "harfbuzz-1.2.4";
		string HarfBuzzLibSubPath = "";
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
			HarfBuzzVersion = "harfbuzz-2.4.0";
			HarfBuzzLibSubPath = "lib/";
			PublicDefinitions.Add("WITH_HARFBUZZ_V24=1"); // TODO: Remove this once everything is using HarfBuzz 2.4.0
			PublicDefinitions.Add("WITH_HARFBUZZ=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
			PublicDefinitions.Add("WITH_HARFBUZZ=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_HARFBUZZ=0");
		}
		string HarfBuzzRootPath = Target.UEThirdPartySourceDirectory + "HarfBuzz/" + HarfBuzzVersion + "/";
		string HarfBuzzLibPath = HarfBuzzRootPath + HarfBuzzLibSubPath;

		// Includes
		PublicSystemIncludePaths.Add(HarfBuzzRootPath + "src" + "/");

		// Libs
		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			HarfBuzzLibPath += (Target.Platform == UnrealTargetPlatform.Win64) ? "Win64/" : "Win32/";
			HarfBuzzLibPath += "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
			HarfBuzzLibPath += "/";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				HarfBuzzLibPath += "Debug";
			}
			else
			{
				HarfBuzzLibPath += "Release";
				//HarfBuzzLibPath += "RelWithDebInfo";
			}

			PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, "harfbuzz.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			HarfBuzzLibPath += "Mac/";

			string HarfBuzzLibName = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "libharfbuzzd.a" : "libharfbuzz.a";
			PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, HarfBuzzLibName));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			HarfBuzzLibPath += "IOS/";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				HarfBuzzLibPath += "Debug";
			}
			else
			{
				HarfBuzzLibPath += "Release";
			}

			PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, "libharfbuzz.a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			HarfBuzzLibPath += "Android/";

			string HarfBuzzLibFolder = "Release";
			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				HarfBuzzLibFolder = "Debug";
			}

			// filtered out in the toolchain
			string HarfBuzzLibName = "libharfbuzz.a";
			PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, "ARMv7", HarfBuzzLibFolder, HarfBuzzLibName));
			PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, "ARM64", HarfBuzzLibFolder, HarfBuzzLibName));
			PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, "x86", HarfBuzzLibFolder, HarfBuzzLibName));
			PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, "x64", HarfBuzzLibFolder, HarfBuzzLibName));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.Type == TargetType.Server)
			{
				string Err = string.Format("{0} dedicated server is made to depend on {1}. We want to avoid this, please correct module dependencies.", Target.Platform.ToString(), this.ToString());
				System.Console.WriteLine(Err);
				throw new BuildException(Err);
			}

			HarfBuzzLibPath += "Linux/";

			string FreeType2LibName = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "libharfbuzzd_fPIC.a" : "libharfbuzz_fPIC.a";
			PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, Target.Architecture, FreeType2LibName));
		}
		else if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
			HarfBuzzLibPath += "HTML5/";

			string OptimizationSuffix = "_Oz"; // i.e. bCompileForSize
			if (!Target.bCompileForSize)
			{
				switch (Target.Configuration)
				{
					case UnrealTargetConfiguration.Development:
						OptimizationSuffix = "_O2";
						break;
					case UnrealTargetConfiguration.Shipping:
						OptimizationSuffix = "_O3";
						break;
					default:
						OptimizationSuffix = "";
						break;
				}
			}
			PublicAdditionalLibraries.Add(HarfBuzzLibPath + "libharfbuzz" + OptimizationSuffix + ".bc");
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			HarfBuzzLibPath += "PS4/";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				HarfBuzzLibPath += "Debug";
			}
			else
			{
				HarfBuzzLibPath += "Release";
			}

			PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, "libharfbuzz.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);

				HarfBuzzLibPath += "XboxOne/";
				HarfBuzzLibPath += "VS" + VersionName.ToString();
				HarfBuzzLibPath += "/";

				if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
				{
					HarfBuzzLibPath += "Debug";
				}
				else
				{
					HarfBuzzLibPath += "Release";
				}

				PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, "harfbuzz.lib"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			HarfBuzzLibPath += "Switch/";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				HarfBuzzLibPath += "Debug";
			}
			else
			{
				HarfBuzzLibPath += "Release";
			}

			PublicAdditionalLibraries.Add(Path.Combine(HarfBuzzLibPath, "libharfbuzz.a"));
		}
	}
}
