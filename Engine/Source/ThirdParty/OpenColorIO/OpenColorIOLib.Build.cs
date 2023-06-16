// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using UnrealBuildTool;

public class OpenColorIOLib : ModuleRules
{
	public OpenColorIOLib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		bool bIsPlatformAdded = false;

		string PlatformDir = Target.Platform.ToString();
		string BinaryDir = "$(EngineDir)/Binaries/ThirdParty/OpenColorIO";
		string DeployDir = Path.Combine(ModuleDirectory, "Deploy/OpenColorIO-2.2.0");

		PublicSystemIncludePaths.Add(Path.Combine(DeployDir, "include"));

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			string Arch = Target.Architecture.WindowsLibDir;
			string DLLName = "OpenColorIO_2_2.dll";
			string LibDirectory = Path.Combine(BinaryDir, PlatformDir, Arch);

			PublicAdditionalLibraries.Add(Path.Combine(DeployDir, "lib", PlatformDir, Arch, "OpenColorIO.lib"));
			PublicDelayLoadDLLs.Add(DLLName);
			RuntimeDependencies.Add(Path.Combine(LibDirectory, DLLName));
			PublicDefinitions.Add("OCIO_DLL_NAME=" + DLLName);

			bIsPlatformAdded = true;
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string Arch = Target.Architecture.LinuxName;
			string SOName = "libOpenColorIO.so";
			string LibDirectory = Path.Combine(BinaryDir, "Unix", Arch);

			PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, SOName));
			RuntimeDependencies.Add(Path.Combine(LibDirectory, SOName));
			RuntimeDependencies.Add(Path.Combine(LibDirectory, "libOpenColorIO.so.2.2"));

			bIsPlatformAdded = true;
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string LibDirectory = Path.Combine(BinaryDir, PlatformDir);

			PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "libOpenColorIO.2.2.dylib"));
			RuntimeDependencies.Add(Path.Combine(LibDirectory, "libOpenColorIO.2.2.dylib"));

			bIsPlatformAdded = true;
		}
		
		PublicDefinitions.Add("WITH_OCIO=" + (bIsPlatformAdded ? "1" : "0"));
	}
}
