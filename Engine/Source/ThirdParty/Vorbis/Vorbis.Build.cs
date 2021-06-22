// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class Vorbis : ModuleRules
{
	protected virtual string VorbisVersion { get { return "libvorbis-1.3.2"; } }
	protected virtual string IncRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string VorbisIncPath { get { return Path.Combine(IncRootDirectory, "Vorbis", VorbisVersion, "include"); } }
	protected virtual string VorbisLibPath { get { return Path.Combine(LibRootDirectory, "Vorbis", VorbisVersion, "lib"); } }

	public Vorbis(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicIncludePaths.Add(VorbisIncPath);
		PublicDefinitions.Add("WITH_OGGVORBIS=1");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(VorbisLibPath, "win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), "libvorbis_64.lib"));
			PublicDelayLoadDLLs.Add("libvorbis_64.dll");

			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Vorbis/Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/libvorbis_64.dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
            string PlatformSubpath = Target.Platform.ToString();
            string LibFileName = "libvorbis_64";

			string LibDir;
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                LibDir = System.String.Format("{0}/{1}/VS{2}/{3}/", VorbisLibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath() + "/");
                RuntimeDependencies.Add(
					System.String.Format("$(EngineDir)/Binaries/ThirdParty/Vorbis/{0}/VS{1}/{2}/{3}.dll",
                        Target.Platform,
						Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        Target.WindowsPlatform.GetArchitectureSubpath(),
                        LibFileName));
            }
            else
            {
                LibDir = System.String.Format("{0}/{1}/VS{2}/", VorbisLibPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/");
                RuntimeDependencies.Add(
                    System.String.Format("$(EngineDir)/Binaries/ThirdParty/Vorbis/{0}/VS{1}/{2}.dll",
                        Target.Platform,
                        Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        LibFileName));
            }

            PublicAdditionalLibraries.Add(Path.Combine(LibDir, LibFileName + ".lib"));
			PublicDelayLoadDLLs.Add(LibFileName + ".dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string DylibPath = Target.UEThirdPartyBinariesDirectory + "Vorbis/Mac/libvorbis.dylib";
			PublicDelayLoadDLLs.Add(DylibPath);
			RuntimeDependencies.Add(DylibPath);
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			// toolchain will filter
			string[] Architectures = new string[] {
				"ARM64",
				"x64",
			};

			foreach(string Architecture in Architectures)
			{
				PublicAdditionalLibraries.Add(Path.Combine(VorbisLibPath, "Android", Architecture, "libvorbis.a"));
			}
        }
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicAdditionalLibraries.Add(Path.Combine(VorbisLibPath, "IOS", "libvorbis.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VorbisLibPath, "IOS", "libvorbisenc.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VorbisLibPath, "IOS", "libvorbisfile.a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicAdditionalLibraries.Add(Path.Combine(VorbisLibPath, "Unix", Target.Architecture, "libvorbis.a"));
		}
	}
}
