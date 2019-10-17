// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class FreeImage : ModuleRules
{
    public FreeImage(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

        PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Source"));

		string LibPath = Path.Combine(ModuleDirectory, "lib", Target.Platform.ToString());

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
		{
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "FreeImage.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "LibRaw.lib"));

			PublicDelayLoadDLLs.Add("FreeImage.dll");
			PublicDelayLoadDLLs.Add("LibRaw.dll");

			string DllPath = Path.Combine(Target.UEThirdPartyBinariesDirectory, "FreeImage");
			RuntimeDependencies.Add(DllPath + Target.Platform.ToString() + "/FreeImage.dll");
			RuntimeDependencies.Add(DllPath + Target.Platform.ToString() + "/LibRaw.dll");
		}
	}
}

