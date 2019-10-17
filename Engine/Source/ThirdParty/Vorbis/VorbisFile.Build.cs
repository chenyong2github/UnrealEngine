// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class VorbisFile : ModuleRules
{
	protected virtual string VorbisVersion { get { return "libvorbis-1.3.2"; } }
	protected virtual string IncRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string VorbisFileIncPath { get { return Path.Combine(IncRootDirectory, "Vorbis", VorbisVersion, "include"); } }
	protected virtual string VorbisFileLibPath { get { return Path.Combine(LibRootDirectory, "Vorbis", VorbisVersion, "lib"); } }

	public VorbisFile(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicIncludePaths.Add(VorbisFileIncPath);
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), "libvorbisfile_64.lib"));
			PublicDelayLoadDLLs.Add("libvorbisfile_64.dll");
			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Vorbis/Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/libvorbisfile_64.dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32 )
		{
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), "libvorbisfile.lib"));
			PublicDelayLoadDLLs.Add("libvorbisfile.dll");
			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Vorbis/Win32/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/libvorbisfile.dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            string LibFileName = "libvorbisfile";
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x64)
            {
                LibFileName += "_64";
            }

            string PlatformSubpath = Target.Platform.ToString();
			string LibDir;
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                LibDir = System.String.Format("{0}/{1}/VS{2}/{3}/", VorbisFileLibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath());
                RuntimeDependencies.Add(
                    System.String.Format("$(EngineDir)/Binaries/ThirdParty/Vorbis/{0}/VS{1}/{2}/{3}.dll",
                        Target.Platform,
                        Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        Target.WindowsPlatform.GetArchitectureSubpath(),
                        LibFileName));
            }
            else
            {
                LibDir = System.String.Format("{0}/{1}/VS{2}/", VorbisFileLibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
                RuntimeDependencies.Add(
                    System.String.Format("$(EngineDir)/Binaries/ThirdParty/Vorbis/{0}/VS{1}/{2}.dll",
                        Target.Platform,
                        Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        LibFileName));
            }

            PublicAdditionalLibraries.Add(Path.Combine(LibDir, LibFileName + ".lib"));
            PublicDelayLoadDLLs.Add(LibFileName + ".dll");
        }
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "Android", "ARM64", "libvorbisfile.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "Android", "ARMv7", "libvorbisfile.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "Android", "x64", "libvorbisfile.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "Android", "x86", "libvorbisfile.a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "Linux", Target.Architecture, "libvorbisfile.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "Linux", Target.Architecture, "libvorbisenc.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "IOS", "libvorbis.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "IOS", "libvorbisfile.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "IOS", "libvorbisenc.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "TVOS", "libvorbis.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "TVOS", "libvorbisfile.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "TVOS", "libvorbisenc.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				PublicAdditionalLibraries.Add(Path.Combine(VorbisFileLibPath, "XboxOne/VS" + VersionName.ToString(), "libvorbisfile_static.lib"));
			}
		}
	}
}
