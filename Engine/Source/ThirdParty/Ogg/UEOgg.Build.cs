// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class UEOgg : ModuleRules
{
    protected virtual string OggVersion { get { return "libogg-1.2.2"; } }
	protected virtual string IncRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string OggIncPath { get { return Path.Combine(IncRootDirectory, "Ogg", OggVersion, "include"); } }
	protected virtual string OggLibPath { get { return Path.Combine(LibRootDirectory, "Ogg", OggVersion, "lib"); } }

    public UEOgg(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		
		PublicSystemIncludePaths.Add(OggIncPath);

		string LibDir;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			LibDir = Path.Combine(OggLibPath, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libogg_64.lib"));

			PublicDelayLoadDLLs.Add("libogg_64.dll");

			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Ogg/Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/libogg_64.dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			LibDir = Path.Combine(OggLibPath, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libogg.lib"));

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

            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                LibDir = System.String.Format("{0}/{1}/VS{2}/{3}/", OggLibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath());
                RuntimeDependencies.Add(
					System.String.Format("$(EngineDir)/Binaries/ThirdParty/Ogg/{0}/VS{1}/{2}/{3}.dll",
                        Target.Platform,
						Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        Target.WindowsPlatform.GetArchitectureSubpath(),
                        LibFileName));
            }
            else
            {
                LibDir = System.String.Format("{0}/{1}/VS{2}/", OggLibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
                RuntimeDependencies.Add(
                    System.String.Format("$(EngineDir)/Binaries/ThirdParty/Ogg/{0}/VS{1}/{2}.dll",
                        Target.Platform,
                        Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        LibFileName));
            }

            PublicAdditionalLibraries.Add(LibDir + LibFileName + ".lib");
			PublicDelayLoadDLLs.Add(LibFileName + ".dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string DylibPath = Path.Combine(Target.UEThirdPartyBinariesDirectory, "Ogg", "Mac", "libogg.dylib");
			PublicDelayLoadDLLs.Add(DylibPath);
			RuntimeDependencies.Add(DylibPath);
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
			string fPIC = (Target.LinkType == TargetLinkType.Monolithic)
				? ""
				: "_fPIC";
			PublicAdditionalLibraries.Add(Path.Combine(OggLibPath, "Linux", Target.Architecture, "libogg" + fPIC + ".a"));
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
