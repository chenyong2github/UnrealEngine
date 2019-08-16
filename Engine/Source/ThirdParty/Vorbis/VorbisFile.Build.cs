// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class VorbisFile : ModuleRules
{
	public VorbisFile(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string VorbisPath = Target.UEThirdPartySourceDirectory + "Vorbis/libvorbis-1.3.2/";
		PublicIncludePaths.Add(VorbisPath + "include");
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string VorbisLibPath = VorbisPath + "Lib/win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";
			PublicLibraryPaths.Add(VorbisLibPath);
			PublicAdditionalLibraries.Add("libvorbisfile_64.lib");
			PublicDelayLoadDLLs.Add("libvorbisfile_64.dll");
			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Vorbis/Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/libvorbisfile_64.dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32 )
		{
			string VorbisLibPath = VorbisPath + "Lib/win32/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";
			PublicLibraryPaths.Add(VorbisLibPath);
			PublicAdditionalLibraries.Add("libvorbisfile.lib");
			PublicDelayLoadDLLs.Add("libvorbisfile.dll");
			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Vorbis/Win32/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/libvorbisfile.dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
			string PlatformSubpath = Target.Platform.ToString();
            string LibFileName = "libvorbisfile";
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x64)
            {
                LibFileName += "_64";
            }

            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                PublicLibraryPaths.Add(System.String.Format("{0}lib/{1}/VS{2}/{3}/", VorbisPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath()));
                RuntimeDependencies.Add(
                    System.String.Format("$(EngineDir)/Binaries/ThirdParty/Vorbis/{0}/VS{1}/{2}/{3}.dll",
                        Target.Platform,
                        Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        Target.WindowsPlatform.GetArchitectureSubpath(),
                        LibFileName));
            }
            else
            {
                PublicLibraryPaths.Add(System.String.Format("{0}lib/{1}/VS{2}/", VorbisPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName()));
                RuntimeDependencies.Add(
                    System.String.Format("$(EngineDir)/Binaries/ThirdParty/Vorbis/{0}/VS{1}/{2}.dll",
                        Target.Platform,
                        Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        LibFileName));
            }

            PublicAdditionalLibraries.Add(LibFileName + ".lib");
            PublicDelayLoadDLLs.Add(LibFileName + ".dll");
        }
		else if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
			string VorbisLibPath = VorbisPath + "lib/HTML5/";
			PublicLibraryPaths.Add(VorbisLibPath);

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
			PublicAdditionalLibraries.Add(VorbisLibPath + "libvorbisfile" + OpimizationSuffix + ".bc");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			// filtered in toolchain
			PublicLibraryPaths.Add(VorbisPath + "Lib/Android/ARMv7");
			PublicLibraryPaths.Add(VorbisPath + "Lib/Android/ARM64");
			PublicLibraryPaths.Add(VorbisPath + "Lib/Android/x86");
			PublicLibraryPaths.Add(VorbisPath + "Lib/Android/x64");

			PublicAdditionalLibraries.Add("vorbisfile");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicAdditionalLibraries.Add(VorbisPath + "lib/Linux/" + Target.Architecture + "/libvorbisfile.a");
			PublicAdditionalLibraries.Add(VorbisPath + "lib/Linux/" + Target.Architecture + "/libvorbisenc.a");
		}
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicAdditionalLibraries.Add(VorbisPath + "lib/IOS/libvorbis.a");
            PublicAdditionalLibraries.Add(VorbisPath + "lib/IOS/libvorbisfile.a");
            PublicAdditionalLibraries.Add(VorbisPath + "lib/IOS/libvorbisenc.a");
        }
        else if (Target.Platform == UnrealTargetPlatform.TVOS)
        {
            PublicAdditionalLibraries.Add(VorbisPath + "lib/TVOS/libvorbis.a");
            PublicAdditionalLibraries.Add(VorbisPath + "lib/TVOS/libvorbisfile.a");
            PublicAdditionalLibraries.Add(VorbisPath + "lib/TVOS/libvorbisenc.a");
        }
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				PublicLibraryPaths.Add(VorbisPath + "lib/XboxOne/VS" + VersionName.ToString());
				PublicAdditionalLibraries.Add("libvorbisfile_static.lib");
			}
		}
	}
}

