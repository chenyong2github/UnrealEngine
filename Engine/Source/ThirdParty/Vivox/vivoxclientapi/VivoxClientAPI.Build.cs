// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class VivoxClientAPI : ModuleRules
{
	public VivoxClientAPI(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDependencyModuleNames.Add("VivoxCoreSDK");

		string VivoxClientAPIPath = ModuleDirectory;
		string PlatformSubdir = Target.Platform.ToString();

		bool bUseDebugBuild = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);
		string ConfigurationSubdir = bUseDebugBuild ? "Debug" : "Release";

		PublicIncludePaths.Add(Path.Combine(VivoxClientAPIPath, "vivoxclientapi", "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64
			|| Target.Platform == UnrealTargetPlatform.Win32)
		{
			string LibDir = Path.Combine(VivoxClientAPIPath, PlatformSubdir, "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), ConfigurationSubdir);
			PublicAdditionalLibraries.Add(LibDir + "/vivoxclientapi.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac
			|| Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicAdditionalLibraries.Add(Path.Combine(VivoxClientAPIPath, PlatformSubdir, ConfigurationSubdir, "libvivoxclientapi.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicAdditionalLibraries.Add(Path.Combine(VivoxClientAPIPath, Target.Platform.ToString(), ConfigurationSubdir, "ARMv7", "libvivoxclientapi.a"));
			PublicAdditionalLibraries.Add(Path.Combine(VivoxClientAPIPath, Target.Platform.ToString(), ConfigurationSubdir, "ARM64", "libvivoxclientapi.a"));
		}
	}
}
