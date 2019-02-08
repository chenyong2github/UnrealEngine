// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class Steamworks : ModuleRules
{
	public Steamworks(ReadOnlyTargetRules Target) : base(Target)
	{
		/** Mark the current version of the Steam SDK */
		string SteamVersion = "v142";
		Type = ModuleType.External;

		PublicDefinitions.Add("STEAM_SDK_VER=TEXT(\"1.42\")");
		PublicDefinitions.Add("STEAM_SDK_VER_PATH=TEXT(\"Steam" + SteamVersion + "\")");

		string SdkBase = Target.UEThirdPartySourceDirectory + "Steamworks/Steam" + SteamVersion + "/sdk";
		if (!Directory.Exists(SdkBase))
		{
			string Err = string.Format("steamworks SDK not found in {0}", SdkBase);
			System.Console.WriteLine(Err);
			throw new BuildException(Err);
		}
		
		string SteamBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/Steamworks/Steam{0}/", SteamVersion);
		// We do not need to explicitly link to these dlls however if they are provided in these directories, then we must take these versions.
		if(Target.Type == TargetType.Server && (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64))
		{
			string PlatformPrefix = "";
			if(Target.Platform == UnrealTargetPlatform.Win64)
			{
				PlatformPrefix = "64";
				SteamBinariesDir += "Win64/";
			}
			else
			{
				SteamBinariesDir += "Win32/";
			}
			
			string SteamClientDll = SteamBinariesDir + String.Format("steamclient{0}.dll", PlatformPrefix);
			string SteamTier0Dll = SteamBinariesDir + String.Format("tier0_s{0}.dll", PlatformPrefix);
			string SteamVstDll = SteamBinariesDir + String.Format("vstdlib_s{0}.dll", PlatformPrefix);
			
			if(File.Exists(SteamClientDll) && File.Exists(SteamTier0Dll) && File.Exists(SteamVstDll))
			{
                System.Console.WriteLine("Linking with bundled steamclient binaries");
                RuntimeDependencies.Add(SteamClientDll);
				RuntimeDependencies.Add(SteamTier0Dll);
				RuntimeDependencies.Add(SteamVstDll);
			}
		}

		PublicIncludePaths.Add(SdkBase + "/public");

		string LibraryPath = SdkBase + "/redistributable_bin/";
		if(Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicLibraryPaths.Add(LibraryPath);
			PublicAdditionalLibraries.Add("steam_api.lib");
			PublicDelayLoadDLLs.Add("steam_api.dll");

			RuntimeDependencies.Add(SteamBinariesDir + "steam_api.dll");

			if(Target.Type != TargetType.Server)
			{
				// assume SteamController is needed
				RuntimeDependencies.Add("$(EngineDir)/Config/controller.vdf");
			}
		}
		else if(Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicLibraryPaths.Add(LibraryPath + "win64");
			PublicAdditionalLibraries.Add("steam_api64.lib");
			PublicDelayLoadDLLs.Add("steam_api64.dll");

			RuntimeDependencies.Add(SteamBinariesDir + "steam_api64.dll");

			if(Target.Type != TargetType.Server)
			{
				// assume SteamController is needed
				RuntimeDependencies.Add("$(EngineDir)/Config/controller.vdf");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string SteamBinariesPath = String.Format(Target.UEThirdPartyBinariesDirectory + "Steamworks/Steam{0}/Mac/", SteamVersion);
			LibraryPath = SteamBinariesPath + "libsteam_api.dylib";
			PublicDelayLoadDLLs.Add(LibraryPath);
			RuntimeDependencies.Add(LibraryPath);
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			if (Target.LinkType == TargetLinkType.Monolithic)
			{
				LibraryPath += "linux64";
				PublicLibraryPaths.Add(LibraryPath);
				PublicAdditionalLibraries.Add("steam_api");
			}
			else
			{
				LibraryPath += "linux64/libsteam_api.so";
				PublicDelayLoadDLLs.Add(LibraryPath);
			}
			string SteamBinariesPath = String.Format(Target.UEThirdPartyBinariesDirectory + "Steamworks/Steam{0}/{1}", SteamVersion, Target.Architecture);
			PrivateRuntimeLibraryPaths.Add(SteamBinariesPath);
			PublicAdditionalLibraries.Add(SteamBinariesPath + "/libsteam_api.so");
			RuntimeDependencies.Add(SteamBinariesPath + "/libsteam_api.so");
		}
	}
}
