// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System;

public class NVAftermath : ModuleRules
{
    public NVAftermath(ReadOnlyTargetRules Target)
        : base(Target)
	{
		Type = ModuleType.External;

        if (Target.Platform == UnrealTargetPlatform.Win64)
		{
            String NVAftermathPath = System.IO.Path.Combine(Target.UEThirdPartySourceDirectory, "NVIDIA", "NVaftermath");

			PublicSystemIncludePaths.Add(System.IO.Path.Combine(NVAftermathPath, "include"));
            PublicAdditionalLibraries.Add(System.IO.Path.Combine(NVAftermathPath, "lib", "x64", "GFSDK_Aftermath_Lib.x64.lib"));

			AddAftermathDll("GFSDK_Aftermath_Lib.x64.dll");
			AddAftermathDll("llvm_7_0_1.dll");

			PublicDefinitions.Add("NV_AFTERMATH=1");
        }
		else
        {
            PublicDefinitions.Add("NV_AFTERMATH=0");
        }
	}

	private void AddAftermathDll(string InDllName)
	{
		string BasePath = System.IO.Path.Combine("$(EngineDir)", "Binaries", "ThirdParty", "NVIDIA", "NVAftermath", "Win64");

		PublicDelayLoadDLLs.Add(InDllName);
		RuntimeDependencies.Add(System.IO.Path.Combine(BasePath, InDllName));
	}
}
