// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class VivoxCoreSDK : ModuleRules
	{
		public VivoxCoreSDK(ReadOnlyTargetRules Target) : base(Target)
		{
			Type = ModuleType.External;

			string VivoxSDKPath = ModuleDirectory;
			string PlatformSubdir = Target.Platform.ToString();
			string VivoxLibPath = Path.Combine(VivoxSDKPath, "Lib", PlatformSubdir) + "/";
			string VivoxIncludePath = Path.Combine(VivoxSDKPath, "Include");
			PublicIncludePaths.Add(VivoxIncludePath);

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicAdditionalLibraries.Add(VivoxLibPath + "vivoxsdk_x64.lib");
				PublicDelayLoadDLLs.Add("ortp_x64.dll");
				PublicDelayLoadDLLs.Add("vivoxsdk_x64.dll");
				RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "ortp_x64.dll"), Path.Combine(VivoxLibPath, "ortp_x64.dll"));
				RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "vivoxsdk_x64.dll"), Path.Combine(VivoxLibPath, "vivoxsdk_x64.dll"));
			}
			else if(Target.Platform == UnrealTargetPlatform.Win32)
			{
				PublicAdditionalLibraries.Add(VivoxLibPath + "vivoxsdk.lib");
				PublicDelayLoadDLLs.Add("ortp.dll");
				PublicDelayLoadDLLs.Add("vivoxsdk.dll");
				RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "ortp.dll"), Path.Combine(VivoxLibPath, "ortp.dll"));
				RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "vivoxsdk.dll"), Path.Combine(VivoxLibPath, "vivoxsdk.dll"));
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				string TargetDir = Path.Combine(Target.UEThirdPartyBinariesDirectory, "Vivox", "Mac");
				PublicDelayLoadDLLs.Add(Path.Combine(TargetDir, "libortp.dylib"));
				PublicDelayLoadDLLs.Add(Path.Combine(TargetDir, "libvivoxsdk.dylib"));
				RuntimeDependencies.Add(Path.Combine(TargetDir, "libortp.dylib"));
				RuntimeDependencies.Add(Path.Combine(TargetDir, "libvivoxsdk.dylib"));
			}
			else if (Target.Platform == UnrealTargetPlatform.IOS)
			{
 				PublicAdditionalLibraries.Add(VivoxLibPath + "libvivoxsdk.a");
				PublicFrameworks.Add("CFNetwork");
			}
			else if (Target.Platform == UnrealTargetPlatform.Android)
			{ 
				PublicAdditionalLibraries.Add(Path.Combine(VivoxLibPath, "armeabi-v7a","libvivox-sdk.so"));
				PublicAdditionalLibraries.Add(Path.Combine(VivoxLibPath, "arm64-v8a","libvivox-sdk.so"));

				string PluginPath = Utils.MakePathRelativeTo(VivoxSDKPath, Target.RelativeEnginePath);
				AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "VivoxCoreSDK_UPL.xml"));
			}
		}
	}
}
