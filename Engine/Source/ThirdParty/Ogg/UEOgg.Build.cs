// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class UEOgg : ModuleRules
{
    protected virtual string OggVersion { get { return "libogg-1.2.2"; } }

    protected virtual string IncRootDirectory { get { return ModuleDirectory; } }
    protected virtual string LibRootDirectory { get { return ModuleDirectory; } }

    protected virtual string OggIncPath { get { return Path.Combine(IncRootDirectory, OggVersion, "include"); } }
    protected virtual string OggLibPath { get { return Path.Combine(LibRootDirectory, OggVersion, "lib"); } }

    public UEOgg(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		
		PublicSystemIncludePaths.Add(OggIncPath);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), "libogg_64.lib"));

			PublicDelayLoadDLLs.Add("libogg_64.dll");

			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Ogg/Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/libogg_64.dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
            PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), "libogg.lib"));

			PublicDelayLoadDLLs.Add("libogg.dll");

			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Ogg/Win32/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/libogg.dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            string LibFileName = "libogg";
            string PlatformSubpath = Target.Platform.ToString();
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x64)
            {
                LibFileName += "_64";
            }

			string LibDir;
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                LibDir = Path.Combine(OggLibPath, PlatformSubpath, "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath());
                RuntimeDependencies.Add(
					string.Format("$(EngineDir)/Binaries/ThirdParty/Ogg/{0}/VS{1}/{2}/{3}.dll",
                        Target.Platform,
						Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        Target.WindowsPlatform.GetArchitectureSubpath(),
                        LibFileName));
            }
            else
            {
                LibDir = Path.Combine(OggLibPath, PlatformSubpath, "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
                RuntimeDependencies.Add(
                    string.Format("$(EngineDir)/Binaries/ThirdParty/Ogg/{0}/VS{1}/{2}.dll",
                        Target.Platform,
                        Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        LibFileName));
            }

            PublicAdditionalLibraries.Add(LibDir + LibFileName + ".lib");
			PublicDelayLoadDLLs.Add(LibFileName + ".dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string DylibPath = Target.UEThirdPartyBinariesDirectory + "Ogg/Mac/libogg.dylib";
			PublicDelayLoadDLLs.Add(DylibPath);
			RuntimeDependencies.Add(DylibPath);
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
			PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "HTML5", "libogg" + OptimizationSuffix + ".bc"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "Android", "ARMv7", "libogg.a"));
			PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "Android", "ARM64", "libogg.a"));
			PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "Android", "x86", "libogg.a"));
			PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "Android", "x64", "libogg.a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.LinkType == TargetLinkType.Monolithic)
			{
				PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "Linux", Target.Architecture, "libogg.a"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "Linux", Target.Architecture, "libogg_fPIC.a"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "XboxOne", "VS" + VersionName.ToString(), "libogg_static.lib"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "ios", "libogg.a"));
        }
        else if (Target.Platform == UnrealTargetPlatform.TVOS)
        {
            PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "tvos", "libogg.a"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "Switch", "NX64", "Ogg_Switch_Static.a"));
        }
    }
}
