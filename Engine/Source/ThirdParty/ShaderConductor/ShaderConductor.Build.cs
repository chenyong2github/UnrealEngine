// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShaderConductor : ModuleRules
{
	public ShaderConductor(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(Target.UEThirdPartySourceDirectory + "ShaderConductor/ShaderConductor/Include");

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string[] DynamicLibrariesMac = new string[] {
				"/libdxcompiler.dylib",
                "/libShaderConductor.dylib",
			};

			string BinariesDir = Target.UEThirdPartyBinariesDirectory + "../Mac";
            foreach (string Lib in DynamicLibrariesMac)
			{
				string LibraryPath = BinariesDir + Lib;
				PublicDelayLoadDLLs.Add(LibraryPath);
				RuntimeDependencies.Add(LibraryPath);
				PublicAdditionalLibraries.Add(LibraryPath);
			}
			RuntimeDependencies.Add(BinariesDir + "/libdxcompiler.3.7.dylib");
		}
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string[] DynamicLibraries = new string[] {
                "/dxcompiler.dll",
                "/ShaderConductor.dll",
            };

            string BinariesDir = Target.UEThirdPartyBinariesDirectory + "../Win64";
            foreach (string Lib in DynamicLibraries)
            {
                string LibraryPath = BinariesDir + Lib;
                PublicDelayLoadDLLs.Add(LibraryPath);
                RuntimeDependencies.Add(LibraryPath);
            }
            string LibPath = Target.UEThirdPartyBinariesDirectory + "ShaderConductor/Win64/ShaderConductor.lib";
            PublicAdditionalLibraries.Add(LibPath);
        }
        else
		{
			string Err = string.Format("Attempt to build against ShaderConductor on unsupported platform {0}", Target.Platform);
			System.Console.WriteLine(Err);
			throw new BuildException(Err);
		}
	}
}

