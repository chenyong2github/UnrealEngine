// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class FreeType2 : ModuleRules
{
    protected virtual string FreeType2Version
    {
        get
        {
            if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.XboxOne ||
				Target.Platform == UnrealTargetPlatform.Switch || Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.HTML5 ||
				Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) || Target.Platform == UnrealTargetPlatform.HoloLens)
            {
                return "FreeType2-2.6";
            }
            else
            {
                return "FreeType2-2.4.12";
            }
        }
    }

    protected virtual string IncRootDirectory { get { return ModuleDirectory; } }
    protected virtual string LibRootDirectory { get { return ModuleDirectory; } }

    protected virtual string FreeType2IncPath { get { return Path.Combine(IncRootDirectory, FreeType2Version, "Include"); } }
    protected virtual string FreeType2LibPath { get { return Path.Combine(LibRootDirectory, FreeType2Version, "Lib"); } }

    public FreeType2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

        PublicDefinitions.Add("WITH_FREETYPE=1");
		
        PublicSystemIncludePaths.Add(FreeType2IncPath);

		if (Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Win64 ||
			Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			string PlatformSubpath = Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x86 ? "Win32" : "Win64";
			string BasePath = Path.Combine(FreeType2LibPath, PlatformSubpath, "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(BasePath, Target.WindowsPlatform.GetArchitectureSubpath(), "freetype26MT.lib"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(BasePath, "freetype26MT.lib"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "Mac", "libfreetype2412.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			if (Target.Architecture == "-simulator")
			{
				PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "ios", "Simulator", "libfreetype2412.a"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "ios", "Device", "libfreetype2412.a"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			if (Target.Architecture == "-simulator")
			{
				PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "TVOS", "Simulator", "libfreetype2412.a"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "TVOS", "Device", "libfreetype2412.a"));
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			const string LibName = "libfreetype2412.a";
			// filtered out in the toolchain
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "Android", "ARMv7", LibName));
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "Android", "ARM64", LibName));
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "Android", "x86", LibName));
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "Android", "x64", LibName));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.Type == TargetType.Server)
			{
				string Err = string.Format("{0} dedicated server is made to depend on {1}. We want to avoid this, please correct module dependencies.", Target.Platform.ToString(), this.ToString());
				System.Console.WriteLine(Err);
				throw new BuildException(Err);
			}

			if (Target.LinkType == TargetLinkType.Monolithic)
			{
				PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "Linux", Target.Architecture, "libfreetype.a"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "Linux", Target.Architecture, "libfreetype_fPIC.a"));
			}
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
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "HTML5", "libfreetype260" + OptimizationSuffix + ".bc"));
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "XboxOne", "VS" + VersionName.ToString(), "freetype26.lib"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "Switch", "libFreetype.a"));
		}
	}
}
