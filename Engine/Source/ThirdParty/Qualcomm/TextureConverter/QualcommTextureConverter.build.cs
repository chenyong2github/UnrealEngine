// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class QualcommTextureConverter : ModuleRules
{
	public QualcommTextureConverter(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if ((Target.Platform == UnrealTargetPlatform.Win64) ||
			(Target.Platform == UnrealTargetPlatform.Mac) ||
			(Target.Platform == UnrealTargetPlatform.Linux && Target.Architecture.StartsWith("x86_64")))
		{
			PublicIncludePaths.Add(Target.UEThirdPartySourceDirectory + "Qualcomm/TextureConverter/Include");

			string LibraryPath = Target.UEThirdPartySourceDirectory + "Qualcomm/TextureConverter/Lib/";
			string LibraryName = "TextureConverter";
			string LibraryExtension = ".lib";

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				LibraryPath += "vs" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/x64/";
				PublicDelayLoadDLLs.Add("TextureConverter.dll");

				PrivateRuntimeLibraryPaths.Add("$(EngineDir)/Binaries/ThirdParty/AppLocalDependencies/Win64/Microsoft.VC.CRT");

				RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Qualcomm/Win64/TextureConverter.dll");
				RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/AppLocalDependencies/Win64/Microsoft.VC.CRT/msvcp100.dll");
				RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/AppLocalDependencies/Win64/Microsoft.VC.CRT/msvcr100.dll");
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				string DylibPath = Target.UEThirdPartyBinariesDirectory + "Qualcomm/Mac/libTextureConverter.dylib";
				PublicDelayLoadDLLs.Add(DylibPath);
				RuntimeDependencies.Add(DylibPath);
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				LibraryPath += "linux_x64";	// FIXME: change to proper architecture
				LibraryExtension = ".so";
				LibraryName = "/lib" + LibraryName;

				PrivateRuntimeLibraryPaths.Add("$(EngineDir)/Binaries/ThirdParty/Qualcomm/Linux");
				RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Qualcomm/Linux/libTextureConverter.so");
			}

			if (Target.Platform != UnrealTargetPlatform.Mac)
			{
				PublicAdditionalLibraries.Add(LibraryPath + LibraryName + LibraryExtension);
			}
		}
	}
}
