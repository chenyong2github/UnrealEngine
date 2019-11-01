// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class UElibPNG : ModuleRules
{
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string IncRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string LibPNGVersion
	{
		get
		{
			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android) ||
				Target.Architecture.StartsWith("aarch64") ||
				Target.Architecture.StartsWith("i686"))
			{
				return "libPNG-1.5.27";
			}
			else
			{
				return "libPNG-1.5.2";
			}
		}
	}

	protected virtual string IncPNGPath { get { return Path.Combine(IncRootDirectory, "libPNG", LibPNGVersion); } }
	protected virtual string LibPNGPath { get { return Path.Combine(LibRootDirectory, "libPNG", LibPNGVersion, "lib"); } }

	public UElibPNG(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string LibDir;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			LibDir = Path.Combine(LibPNGPath, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			string LibFileName = "libpng" + (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "d" : "") + "_64.lib";
			PublicAdditionalLibraries.Add(Path.Combine(LibDir, LibFileName));
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			LibDir = Path.Combine(LibPNGPath, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			string LibFileName = "libpng" + (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "d" : "") + ".lib";
			PublicAdditionalLibraries.Add(Path.Combine(LibDir, LibFileName));
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			string PlatformSubpath = Target.Platform.ToString();
			LibDir = Path.Combine(LibPNGPath, PlatformSubpath, "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
			{
				LibDir = Path.Combine(LibDir, Target.WindowsPlatform.GetArchitectureSubpath());
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
			PublicAdditionalLibraries.Add(Path.Combine(LibDir, LibFileName + ".lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "Mac", "libpng.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			LibDir = (Target.Architecture == "-simulator")
				? "Simulator"
				: "Device";

			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "ios", LibDir, "libpng152.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			LibDir = (Target.Architecture == "-simulator")
				? "Simulator"
				: "Device";

			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "TVOS", LibDir, "libpng152.a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "Android", "ARMv7", "libpng.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "Android", "ARM64", "libpng.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "Android", "x86", "libpng.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "Android", "x64", "libpng.a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "Linux", Target.Architecture, "libpng.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "XboxOne", "VS" + VersionName.ToString(), "libpng125_XboxOne.lib"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "Switch", "libPNG.a"));
		}

		PublicIncludePaths.Add(IncPNGPath);
	}
}
