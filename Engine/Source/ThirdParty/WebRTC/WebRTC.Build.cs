// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System;

public class WebRTC : ModuleRules
{
	public WebRTC(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		bool bShouldUseWebRTC = false;
		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
		{
			bShouldUseWebRTC = true;
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			bShouldUseWebRTC = true;
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) && Target.Architecture.StartsWith("x86_64"))
		{
			bShouldUseWebRTC = true;
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			bShouldUseWebRTC = true;
		}

		if (bShouldUseWebRTC)
		{string WebRtcSdkPath = Target.UEThirdPartySourceDirectory + "WebRTC/rev.31262"; // Revision 31262 is Release 84
			string VS2013Friendly_WebRtcSdkPath = Target.UEThirdPartySourceDirectory;

			string PlatformSubdir = Target.Platform.ToString();

			string ConfigPath = "";
			if (Target.Configuration == UnrealTargetConfiguration.Debug)
			{
				ConfigPath = "Debug";
			}
			else
			{
				ConfigPath = "Release";
			}

			if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
			{
				PublicDefinitions.Add("WEBRTC_WIN=1");

				string VisualStudioVersionFolder = "VS2015";

				string IncludePath = Path.Combine(WebRtcSdkPath, "Include", "Windows");
				PublicSystemIncludePaths.Add(IncludePath);
				string AbslthirdPartyIncludePath = Path.Combine(WebRtcSdkPath, "Include", "Windows", "third_party", "abseil-cpp");
				PublicSystemIncludePaths.Add(AbslthirdPartyIncludePath);

				string LibraryPath = Path.Combine(WebRtcSdkPath, "Lib", PlatformSubdir, VisualStudioVersionFolder, ConfigPath);
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "webrtc.lib"));

				// Additional System library
				PublicSystemLibraries.Add("Secur32.lib");

				// The version of webrtc we depend on, depends on an openssl that depends on zlib
				AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
				PublicDefinitions.Add("WEBRTC_LINUX=1");
				PublicDefinitions.Add("WEBRTC_POSIX=1");

				string IncludePath = Path.Combine(WebRtcSdkPath, "Include", "Linux");
				PublicSystemIncludePaths.Add(IncludePath);
				string AbslthirdPartyIncludePath = Path.Combine(WebRtcSdkPath, "Include", "Linux", "third_party", "abseil-cpp");
				PublicSystemIncludePaths.Add(AbslthirdPartyIncludePath);

				// This is slightly different than the other platforms
				string LibraryPath = Path.Combine(WebRtcSdkPath, "Lib/Linux", Target.Architecture, ConfigPath);

				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libwebrtc.a"));
				AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
			}
		}

		PublicDefinitions.Add("WEBRTC_VERSION=84");
		PublicDefinitions.Add("ABSL_ALLOCATOR_NOTHROW=1");
	}

}
