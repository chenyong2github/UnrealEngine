// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System;
using System.IO;

public class ICU : ModuleRules
{
	enum EICULinkType
	{
		None,
		Static,
		Dynamic
	}

	public ICU(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// Note: If you change the version of ICU used for your platform, you may also need to update the ICU data staging code inside CopyBuildToStagingDirectory.Automation.cs

		bool bNeedsDlls = false;

		string ICUVersion = "icu4c-53_1";
		string ICULibSubPath = "";
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
			ICUVersion = "icu4c-64_1";
			ICULibSubPath = "lib/";
			PublicDefinitions.Add("WITH_ICU_V64=1"); // TODO: Remove this once everything is using ICU 64
		}

		string ICURootPath = Target.UEThirdPartySourceDirectory + "ICU/" + ICUVersion + "/";
		string ICUIncludePath = ICURootPath + "include/";
		string ICULibPath = ICURootPath + ICULibSubPath;

		// Includes
		PublicSystemIncludePaths.Add(ICUIncludePath);

		// Libs
		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			string VSVersionFolderName = "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
			ICULibPath += (Target.Platform == UnrealTargetPlatform.Win64) ? "Win64/" : "Win32/";
			ICULibPath += VSVersionFolderName + "/";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				ICULibPath += "Debug";
			}
			else
			{
				ICULibPath += "Release";
				//ICULibPath += "RelWithDebInfo";
			}

			PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, "icu.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			string VSVersionFolderName = "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
			ICULibPath += "HoloLens/";
			ICULibPath += VSVersionFolderName + "/";
			ICULibPath += Target.WindowsPlatform.GetArchitectureSubpath() + "/";

			string[] LibraryNameStems =
			{
				"dt",   // Data
				"uc",   // Unicode Common
				"in",   // Internationalization
				"le",   // Layout Engine
				"lx",   // Layout Extensions
				"io"	// Input/Output
			};
			string LibraryNamePostfix = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT) ?
				"d" : string.Empty;

			// Library Paths
			string LibDir = ICULibPath + "lib" + "/";
			foreach (string Stem in LibraryNameStems)
			{
				string LibraryName = "sicu" + Stem + LibraryNamePostfix + "." + "lib";
				PublicAdditionalLibraries.Add(Path.Combine(LibDir, LibraryName));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			ICULibPath += "Mac/";

			string ICULibName = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "libicud.a" : "libicu.a";
			PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, ICULibName));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			ICULibPath += "IOS/";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				ICULibPath += "Debug";
			}
			else
			{
				ICULibPath += "Release";
			}

			PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, "libicu.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			ICULibPath += "TVOS/";

			string[] LibraryNameStems =
			{
				"data", // Data
				"uc",   // Unicode Common
				"i18n", // Internationalization
				"le",   // Layout Engine
				"lx",   // Layout Extensions
				"io"	// Input/Output
			};
			string LibraryNamePostfix = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT) ?
				"d" : string.Empty;

			foreach (string Stem in LibraryNameStems)
			{
				string LibraryName = "libicu" + Stem + LibraryNamePostfix + ".a";
				PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, "lib", LibraryName));
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			ICULibPath += "Android/";

			string ICULibFolder = "Release";
			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				ICULibFolder = "Debug";
			}

			// filtered out in the toolchain
			string ICULibName = "libicu.a";
			PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, "ARMv7", ICULibFolder, ICULibName));
			PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, "ARM64", ICULibFolder, ICULibName));
			PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, "x86", ICULibFolder, ICULibName));
			PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, "x64", ICULibFolder, ICULibName));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			ICULibPath += "Linux/";

			string ICULibName = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "libicud_fPIC.a" : "libicu_fPIC.a";
			PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, Target.Architecture, ICULibName));
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			PublicSystemIncludePaths.Add(ICUIncludePath + "PS4/");

			ICULibPath += "PS4/";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				ICULibPath += "Debug";
			}
			else
			{
				ICULibPath += "Release";
			}

			PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, "libicu.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);

				ICULibPath += "XboxOne/";
				ICULibPath += "VS" + VersionName.ToString();
				ICULibPath += "/";

				if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
				{
					ICULibPath += "Debug";
				}
				else
				{
					ICULibPath += "Release";
				}

				PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, "icu.lib"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			ICULibPath += "Switch/";

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				ICULibPath += "Debug";
			}
			else
			{
				ICULibPath += "Release";
			}

			PublicAdditionalLibraries.Add(Path.Combine(ICULibPath, "libicu.a"));
		}

		// DLL Definitions
		if (bNeedsDlls)
		{
			PublicDefinitions.Add("NEEDS_ICU_DLLS=1");
		}
		else
		{
			PublicDefinitions.Add("NEEDS_ICU_DLLS=0");
			PublicDefinitions.Add("U_STATIC_IMPLEMENTATION"); // Necessary for linking to ICU statically.
		}

		// Common Definitions
		PublicDefinitions.Add("U_USING_ICU_NAMESPACE=0"); // Disables a using declaration for namespace "icu".
		PublicDefinitions.Add("UNISTR_FROM_CHAR_EXPLICIT=explicit"); // Makes UnicodeString constructors for ICU character types explicit.
		PublicDefinitions.Add("UNISTR_FROM_STRING_EXPLICIT=explicit"); // Makes UnicodeString constructors for "char"/ICU string types explicit.
		PublicDefinitions.Add("UCONFIG_NO_TRANSLITERATION=1"); // Disables declarations and compilation of unused ICU transliteration functionality.
	}
}
