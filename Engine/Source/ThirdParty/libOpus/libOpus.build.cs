// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class libOpus : ModuleRules
{
	protected virtual string OpusVersion	  { get { return "opus-1.1"; } }
	protected virtual string IncRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string OpusIncPath { get { return Path.Combine(IncRootDirectory, "libOpus", OpusVersion, "include"); } }
	protected virtual string OpusLibPath { get { return Path.Combine(LibRootDirectory, "libOpus", OpusVersion); } }

	public libOpus(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicIncludePaths.Add(OpusIncPath);
		string LibraryPath = OpusLibPath + "/";

		if ((Target.Platform == UnrealTargetPlatform.Win64) ||
			(Target.Platform == UnrealTargetPlatform.Win32))
		{
			LibraryPath += "Windows/VS2012/";
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				LibraryPath += "x64/";
			}
			else
			{
				LibraryPath += "win32/";
			}

			LibraryPath += "Release/";

 			PublicAdditionalLibraries.Add(LibraryPath + "silk_common.lib");
 			PublicAdditionalLibraries.Add(LibraryPath + "silk_float.lib");
 			PublicAdditionalLibraries.Add(LibraryPath + "celt.lib");
			PublicAdditionalLibraries.Add(LibraryPath + "opus.lib");
			PublicAdditionalLibraries.Add(LibraryPath + "speex_resampler.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			if (Target.WindowsPlatform.Architecture == WindowsArchitecture.x64)
			{
				LibraryPath += "Windows/VS2012/";
				LibraryPath += "x64/";
			}
			else if (Target.WindowsPlatform.Architecture == WindowsArchitecture.x86)
			{
				LibraryPath += "Windows/VS2012/";
				LibraryPath += "win32/";
			}
			else if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32)
			{
				LibraryPath += "Windows/VS" + (Target.WindowsPlatform.Compiler >= WindowsCompiler.VisualStudio2015_DEPRECATED ? "2015" : "2012");
				LibraryPath += "/ARM/";
			}
			else if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
			{
				LibraryPath += "Windows/VS" + (Target.WindowsPlatform.Compiler >= WindowsCompiler.VisualStudio2015_DEPRECATED ? "2015" : "2012");
				LibraryPath += "/ARM64/";
			}
			

			LibraryPath += "Release/";

 			PublicAdditionalLibraries.Add(LibraryPath + "silk_common.lib");
 			PublicAdditionalLibraries.Add(LibraryPath + "silk_float.lib");
 			PublicAdditionalLibraries.Add(LibraryPath + "celt.lib");
			PublicAdditionalLibraries.Add(LibraryPath + "opus.lib");
			PublicAdditionalLibraries.Add(LibraryPath + "speex_resampler.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string OpusPath = LibraryPath + "/Mac/libopus.a";
			string SpeexPath = LibraryPath + "/Mac/libspeex_resampler.a";

			PublicAdditionalLibraries.Add(OpusPath);
			PublicAdditionalLibraries.Add(SpeexPath);
		}
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            string OpusPath = LibraryPath + "/IOS/libOpus.a";
            PublicAdditionalLibraries.Add(OpusPath);
        }
	else if (Target.Platform == UnrealTargetPlatform.TVOS)
        {
            string OpusPath = LibraryPath + "/TVOS/libOpus.a";
            PublicAdditionalLibraries.Add(OpusPath);
        }
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
            if (Target.LinkType == TargetLinkType.Monolithic)
            {
                PublicAdditionalLibraries.Add(LibraryPath + "Linux/" + Target.Architecture + "/libopus.a");
            }
            else
            {
                PublicAdditionalLibraries.Add(LibraryPath + "Linux/" + Target.Architecture + "/libopus_fPIC.a");
            }

			if (Target.Architecture.StartsWith("x86_64"))
			{
				if (Target.LinkType == TargetLinkType.Monolithic)
				{
					PublicAdditionalLibraries.Add(LibraryPath + "Linux/" + Target.Architecture + "/libresampler.a");
				}
				else
				{
					PublicAdditionalLibraries.Add(LibraryPath + "Linux/" + Target.Architecture + "/libresampler_fPIC.a");
				}
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			string[] Architectures = new string[] {
				"ARMv7",
				"ARM64",
				"x64",
			};

			foreach(string Architecture in Architectures)
			{
				PublicAdditionalLibraries.Add(LibraryPath + "Android/" + Architecture + "/libopus.a");
				PublicAdditionalLibraries.Add(LibraryPath + "Android/" + Architecture + "/libspeex_resampler.a");
			}
		}
        else if (Target.Platform == UnrealTargetPlatform.XboxOne)
        {
            LibraryPath += "XboxOne/VS2015/Release/";

            PublicAdditionalLibraries.Add(LibraryPath + "silk_common.lib");
            PublicAdditionalLibraries.Add(LibraryPath + "silk_float.lib");
            PublicAdditionalLibraries.Add(LibraryPath + "celt.lib");
            PublicAdditionalLibraries.Add(LibraryPath + "opus.lib");
            PublicAdditionalLibraries.Add(LibraryPath + "speex_resampler.lib");
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            PublicAdditionalLibraries.Add(LibraryPath +  "Switch/libOpus-1.1/NX64/Release/" + "libOpus-1.1.a");
        }
    }
}
