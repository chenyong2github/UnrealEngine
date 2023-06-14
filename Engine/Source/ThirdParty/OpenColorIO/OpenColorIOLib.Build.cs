// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using UnrealBuildTool;

public class OpenColorIOLib : ModuleRules
{
	public OpenColorIOLib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string DeployDir = "Deploy/OpenColorIO-2.2.0";
		bool bIsPlatformAdded = false;

		if(Target.bBuildEditor)
		{
			string PlatformDir = Target.Platform.ToString();
			string LibPath = Path.Combine(ModuleDirectory, DeployDir, "lib", PlatformDir);
			string BinaryPath = Path.Combine("$(EngineDir)/Binaries/ThirdParty/OpenColorIO", PlatformDir);

			PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, DeployDir, "include"));

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				string DLLName = "OpenColorIO_2_2.dll";
				PublicAdditionalLibraries.Add(Path.Combine(LibPath, "OpenColorIO.lib"));
				PublicDelayLoadDLLs.Add(DLLName);
				RuntimeDependencies.Add(Path.Combine(BinaryPath, DLLName));
				PublicDefinitions.Add("OCIO_DLL_NAME=" + DLLName);

				bIsPlatformAdded = true;
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				string SOName = "libOpenColorIO.so";
				PublicAdditionalLibraries.Add(Path.Combine(BinaryPath, SOName));
				RuntimeDependencies.Add(Path.Combine(BinaryPath, SOName));
				RuntimeDependencies.Add(Path.Combine(BinaryPath, "libOpenColorIO.so.2.2"));

				bIsPlatformAdded = true;
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicAdditionalLibraries.Add(Path.Combine(BinaryPath, "libOpenColorIO.2.2.dylib"));
				RuntimeDependencies.Add(Path.Combine(BinaryPath, "libOpenColorIO.2.2.dylib"));

				bIsPlatformAdded = true;
			}
		}
		
		PublicDefinitions.Add("WITH_OCIO=" + (bIsPlatformAdded ? "1" : "0"));
	}
}
