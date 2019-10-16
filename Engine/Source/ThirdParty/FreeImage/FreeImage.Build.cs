// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class FreeImage : ModuleRules
{
	public FreeImage(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Source"));

		string BinaryLibraryFolder = Path.Combine(Target.UEThirdPartyBinariesDirectory, "FreeImage", Target.Platform.ToString());
		string LibraryFileName = "";
		bool bWithFreeImage = false;
		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
		{
			LibraryFileName = "FreeImage.dll";
			string DynLibPath = Path.Combine(BinaryLibraryFolder, LibraryFileName);

			string LibPath = Path.Combine(ModuleDirectory, "lib", Target.Platform.ToString());
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "FreeImage.lib"));

			PublicDelayLoadDLLs.Add(LibraryFileName);
			RuntimeDependencies.Add(DynLibPath);
			bWithFreeImage = true;
		}



		PublicDefinitions.Add("WITH_FREEIMAGE_LIB=" + (bWithFreeImage ? '1' : '0'));
		if (LibraryFileName != "")
		{
			PublicDefinitions.Add("FREEIMAGE_LIB_FILENAME=\"" + LibraryFileName + "\"");
		}
	}
}
