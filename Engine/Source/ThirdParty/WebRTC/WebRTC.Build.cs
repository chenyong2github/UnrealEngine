// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System;

public class WebRTC : ModuleRules
{
	protected string ConfigPath {get; private set; }

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

		// WebRTC binaries with debug symbols are huge hence the Release binaries do not have any
		// if you want to have debug symbols with shipping you will need to build with debug instead  
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			ConfigPath = "Release";
		}
		else
		{
			ConfigPath = "Release";
		}

		if (bShouldUseWebRTC)
		{
			string WebRtcSdkPath = Target.UEThirdPartySourceDirectory + "WebRTC/4147"; // Branch head 4147 is Release 84
			string VS2013Friendly_WebRtcSdkPath = Target.UEThirdPartySourceDirectory;

			string PlatformSubdir = Target.Platform.ToString();

			string IncludePath = Path.Combine(WebRtcSdkPath, "Include");
			PublicSystemIncludePaths.Add(IncludePath);

			string AbslthirdPartyIncludePath = Path.Combine(WebRtcSdkPath, "Include", "third_party", "abseil-cpp");
			PublicSystemIncludePaths.Add(AbslthirdPartyIncludePath);

			if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
			{
				PublicDefinitions.Add("WEBRTC_WIN=1");

				string LibraryPath = Path.Combine(WebRtcSdkPath, "Lib", PlatformSubdir, ConfigPath);
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "webrtc.lib"));

				// Additional System library
				PublicSystemLibraries.Add("Secur32.lib");

				// The version of webrtc we depend on, depends on an openssl that depends on zlib
				AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "libOpus");

			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
				PublicDefinitions.Add("WEBRTC_LINUX=1");
				PublicDefinitions.Add("WEBRTC_POSIX=1");

				// This is slightly different than the other platforms
				string LibraryPath = Path.Combine(WebRtcSdkPath, "Lib", PlatformSubdir, Target.Architecture, ConfigPath);
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libwebrtc.a"));
				
				AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "libOpus");
			}
		}

		PublicDefinitions.Add("WEBRTC_VERSION=84");
		PublicDefinitions.Add("ABSL_ALLOCATOR_NOTHROW=1");
	}

}
