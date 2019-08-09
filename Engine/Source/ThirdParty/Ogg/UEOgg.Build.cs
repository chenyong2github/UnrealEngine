// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UEOgg : ModuleRules
{
	public UEOgg(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string OggPath = Target.UEThirdPartySourceDirectory + "Ogg/libogg-1.2.2/";

		PublicSystemIncludePaths.Add(OggPath + "include");

		string OggLibPath = OggPath + "lib/";

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			OggLibPath += "Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";

			PublicAdditionalLibraries.Add(OggLibPath + "libogg_64.lib");

			PublicDelayLoadDLLs.Add("libogg_64.dll");

			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Ogg/Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/libogg_64.dll");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32 )
		{
			OggLibPath += "Win32/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";

			PublicAdditionalLibraries.Add(OggLibPath + "libogg.lib");

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
                LibDir = System.String.Format("{0}lib/{1}/VS{2}/{3}/", OggPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath());
                RuntimeDependencies.Add(
					System.String.Format("$(EngineDir)/Binaries/ThirdParty/Ogg/{0}/VS{1}/{2}/{3}.dll",
                        Target.Platform,
						Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
                        Target.WindowsPlatform.GetArchitectureSubpath(),
                        LibFileName));
            }
            else
            {
                LibDir = System.String.Format("{0}lib/{1}/VS{2}/", OggPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
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
			string DylibPath = Target.UEThirdPartyBinariesDirectory + "Ogg/Mac/libogg.dylib";
			PublicDelayLoadDLLs.Add(DylibPath);
			RuntimeDependencies.Add(DylibPath);
		}
		else if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
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
			PublicAdditionalLibraries.Add(OggLibPath + "HTML5/libogg" + OpimizationSuffix + ".bc");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			PublicAdditionalLibraries.Add(OggLibPath + "Android/ARMv7/libogg.a");
			PublicAdditionalLibraries.Add(OggLibPath + "Android/ARM64/libogg.a");
			PublicAdditionalLibraries.Add(OggLibPath + "Android/x86/libogg.a");
			PublicAdditionalLibraries.Add(OggLibPath + "Android/x64/libogg.a");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.LinkType == TargetLinkType.Monolithic)
			{
				PublicAdditionalLibraries.Add(OggLibPath + "Linux/" + Target.Architecture + "/libogg.a");
			}
			else
			{
				PublicAdditionalLibraries.Add(OggLibPath + "Linux/" + Target.Architecture + "/libogg_fPIC.a");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				PublicAdditionalLibraries.Add(OggLibPath + "XboxOne/VS" + VersionName.ToString() + "/libogg_static.lib");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicAdditionalLibraries.Add(OggLibPath + "ios" + "/libogg.a");
        }
        else if (Target.Platform == UnrealTargetPlatform.TVOS)
        {
            PublicAdditionalLibraries.Add(OggLibPath + "tvos" + "/libogg.a");
        }
		else if (Target.Platform == UnrealTargetPlatform.PS4)
        {
            PublicAdditionalLibraries.Add(OggLibPath + "PS4/ORBIS_Release" + "/libogg-1.2.2_PS4_Static.a");
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            PublicAdditionalLibraries.Add(OggLibPath + "Switch/NX64" + "/Ogg_Switch_Static.a");
        }
    }
}
