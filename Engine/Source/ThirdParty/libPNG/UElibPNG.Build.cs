// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class UElibPNG : ModuleRules
{
    protected virtual string LibRootDirectory { get { return ModuleDirectory; } }
    protected virtual string IncRootDirectory { get { return ModuleDirectory; } }

	protected virtual string LibPNGVersion {  get { return "libPNG-1.5.2"; } }

    protected virtual string LibPNGPath { get { return Path.Combine(LibRootDirectory, LibPNGVersion); } }
    protected virtual string IncPNGPath { get { return Path.Combine(IncRootDirectory, LibPNGVersion); } }

    public UElibPNG(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibFileName = "libpng" + (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "d" : "") + "_64.lib";
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "lib", "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), LibFileName));
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			string LibFileName = "libpng" + (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "d" : "") + ".lib";
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "lib", "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), LibFileName));
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
			string LibDir;
            string PlatformSubpath = Target.Platform.ToString();
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                LibDir = System.String.Format("{0}/lib/{1}/VS{2}/{3}/", LibPNGPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath());
            }
            else
            {
                LibDir = System.String.Format("{0}/lib/{1}/VS{2}/", LibPNGPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
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
            PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "lib", "Mac", "libpng.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			string LibDir;
			if (Target.Architecture == "-simulator")
			{
				LibDir = Path.Combine(LibPNGPath, "lib", "ios", "Simulator");
			}
			else
			{
				LibDir = Path.Combine(LibPNGPath, "lib", "ios", "Device");
			}

			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libpng152.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			string LibDir;
			if (Target.Architecture == "-simulator")
			{
				LibDir = Path.Combine(LibPNGPath, "lib", "TVOS", "Simulator");
			}
			else
			{
				LibDir = Path.Combine(LibPNGPath, "lib", "TVOS", "Device");
			}

			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libpng152.a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			string libPNGPath = Path.Combine(Target.UEThirdPartySourceDirectory, "libPNG", "libPNG-1.5.27", "lib", "Android");

			PublicAdditionalLibraries.Add(Path.Combine(libPNGPath, "ARMv7", "libpng.a"));
			PublicAdditionalLibraries.Add(Path.Combine(libPNGPath, "ARM64", "libpng.a"));
			PublicAdditionalLibraries.Add(Path.Combine(libPNGPath, "x86", "libpng.a"));
			PublicAdditionalLibraries.Add(Path.Combine(libPNGPath, "x64", "libpng.a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
            string libPNGPath = LibPNGPath;

            // migrate all architectures to the newer binary
            if (Target.Architecture.StartsWith("aarch64") || Target.Architecture.StartsWith("i686"))
			{
                libPNGPath = Path.Combine(Target.UEThirdPartySourceDirectory, "libPNG", "libPNG-1.5.27");
			}

			PublicAdditionalLibraries.Add(Path.Combine(libPNGPath, "lib", "Linux", Target.Architecture, "libpng.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
			string OptimizationSuffix = "";
			if (Target.bCompileForSize)
			{
				OptimizationSuffix = "_Oz";
			}
			else
			{
				if (Target.Configuration == UnrealTargetConfiguration.Development)
				{
					OptimizationSuffix = "_O2";
				}
				else if (Target.Configuration == UnrealTargetConfiguration.Shipping)
				{
					OptimizationSuffix = "_O3";
				}
			}
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "lib", "HTML5", "libpng" + OptimizationSuffix + ".bc"));
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
                PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "lib", "XboxOne", "VS" + VersionName.ToString(), "libpng125_XboxOne.lib"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "lib", "Switch", "libPNG.a"));
		}

		PublicIncludePaths.Add(IncPNGPath);
	}
}
