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
		if (Target.Platform == UnrealTargetPlatform.Win64)
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

		if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
		{
			ConfigPath = "Debug";
		}
		else
		{
			ConfigPath = "Release";
		}

		// HACK change back to 4.26 webrtc to prevent build errors on certain platforms this will mean PixelStreaming Plugin will not work or compile and must be disabled on all platforms
		/*
		if (bShouldUseWebRTC)
		{
			string WebRtcSdkPath = Target.UEThirdPartySourceDirectory + "WebRTC/rev.31262"; // Revision 31262 is Release 84
			string VS2013Friendly_WebRtcSdkPath = Target.UEThirdPartySourceDirectory;

			string PlatformSubdir = Target.Platform.ToString();

			if (Target.Platform == UnrealTargetPlatform.Win64)
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
		}*/

		if (bShouldUseWebRTC)
		{
			string WebRtcSdkPath = Target.UEThirdPartySourceDirectory + "WebRTC/rev.24472"; // Revision 24472 is Release 70

			string PlatformSubdir = Target.Platform.ToString();

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicDefinitions.Add("WEBRTC_WIN=1");

				string VisualStudioVersionFolder = "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();

				string IncludePath = Path.Combine(WebRtcSdkPath, "Include", PlatformSubdir, VisualStudioVersionFolder);
				PublicSystemIncludePaths.Add(IncludePath);
				string AbslthirdPartyIncludePath = Path.Combine(WebRtcSdkPath, "Include", PlatformSubdir, VisualStudioVersionFolder, "third_party", "abseil-cpp");
				PublicSystemIncludePaths.Add(AbslthirdPartyIncludePath);

				string LibraryPath = Path.Combine(WebRtcSdkPath, "Lib", PlatformSubdir, VisualStudioVersionFolder, ConfigPath);

				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "webrtc.lib"));

				// Additional System library
				PublicSystemLibraries.Add("Secur32.lib");

				// The version of webrtc we depend on, depends on an openssl that depends on zlib
				AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicDefinitions.Add("WEBRTC_MAC=1");
				PublicDefinitions.Add("WEBRTC_POSIX=1");

				string IncludePath = Path.Combine(WebRtcSdkPath, "Include", PlatformSubdir);
				PublicSystemIncludePaths.Add(IncludePath);
				string AbslthirdPartyIncludePath = Path.Combine(WebRtcSdkPath, "Include", PlatformSubdir, "third_party", "abseil-cpp");
				PublicSystemIncludePaths.Add(AbslthirdPartyIncludePath);

				string LibraryPath = Path.Combine(WebRtcSdkPath, "Lib", PlatformSubdir, ConfigPath);

				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libprotobuf_full.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libprotobuf_lite.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libprotoc_lib.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libsystem_wrappers.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libwebrtc.a"));
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
				PublicDefinitions.Add("WEBRTC_LINUX=1");
				PublicDefinitions.Add("WEBRTC_POSIX=1");

				string IncludePath = Path.Combine(WebRtcSdkPath, "Include/Linux");
				PublicSystemIncludePaths.Add(IncludePath);
				string AbslthirdPartyIncludePath = Path.Combine(WebRtcSdkPath, "Include", PlatformSubdir, "third_party", "abseil-cpp");
				PublicSystemIncludePaths.Add(AbslthirdPartyIncludePath);

				// This is slightly different than the other platforms
				string LibraryPath = Path.Combine(WebRtcSdkPath, "Lib/Linux", Target.Architecture, ConfigPath);

				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libprotobuf_full.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libprotobuf_lite.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libprotoc_lib.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libsystem_wrappers.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libwebrtc.a"));
			}

			AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
		}

		PublicDefinitions.Add("WEBRTC_VERSION=84");
		PublicDefinitions.Add("ABSL_ALLOCATOR_NOTHROW=1");
	}
}