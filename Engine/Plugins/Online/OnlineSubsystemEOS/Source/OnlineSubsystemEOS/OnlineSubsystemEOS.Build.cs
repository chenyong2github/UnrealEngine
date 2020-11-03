// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OnlineSubsystemEOS : ModuleRules
{
	public OnlineSubsystemEOS(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDefinitions.Add("ONLINESUBSYSTEMEOS_PACKAGE=1");

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"OnlineSubsystemUtils"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Sockets",
				"OnlineSubsystem",
				"Json"
			}
		);

		// Change this to true once you have the SDK installed
		bool bSDKInstalled = false;

		// Update this to your SDK path once it is installed
		string EOSSDKVersion = "EOS-SDK-1.9";
		string EOSPath = Path.Combine(ModuleDirectory, "..", "EOS", EOSSDKVersion, "SDK");
		string EOSIncludePath = Path.Combine(EOSPath, "Include");
		if (bSDKInstalled)
		{
			PrivateDefinitions.Add("WITH_EOS_SDK=1");

			PublicIncludePaths.Add(EOSIncludePath);

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				string EOSLibPath = Path.Combine(EOSPath, "Lib");
				PublicAdditionalLibraries.Add(Path.Combine(EOSLibPath, "EOSSDK-Win64-Shipping.lib"));

				PublicDelayLoadDLLs.Add("EOSSDK-Win64-Shipping.dll");

				string EOSDLLPath = Path.Combine(EOSPath, "Bin", "EOSSDK-Win64-Shipping.dll");
				RuntimeDependencies.Add(EOSDLLPath);
			}
			else if (Target.Platform == UnrealTargetPlatform.Win32)
			{
				string EOSLibPath = Path.Combine(EOSPath, "Lib");
				PublicAdditionalLibraries.Add(Path.Combine(EOSLibPath, "EOSSDK-Win32-Shipping.lib"));

				PublicDelayLoadDLLs.Add("EOSSDK-Win32-Shipping.dll");

				string EOSDLLPath = Path.Combine(EOSPath, "Bin", "EOSSDK-Win32-Shipping.dll");
				RuntimeDependencies.Add(EOSDLLPath);
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				string DylibName = Path.Combine(EOSPath, "Bin", "libEOSSDK-Mac-Shipping.dylib");

				PublicDelayLoadDLLs.Add(DylibName);

				RuntimeDependencies.Add(DylibName);
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				string SoPath = Path.Combine(EOSPath, "Bin");
				string SoName = Path.Combine(EOSPath, "libEOSSDK-Linux-Shipping.so");

				PrivateRuntimeLibraryPaths.Add(SoPath);

				PublicAdditionalLibraries.Add(SoName);

				RuntimeDependencies.Add(SoName);
			}
		}
		else
		{
			PrivateDefinitions.Add("WITH_EOS_SDK=0");
		}
	}
}
