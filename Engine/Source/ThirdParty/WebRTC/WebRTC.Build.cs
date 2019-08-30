// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
		{
			string WebRtcSdkPath = Target.UEThirdPartySourceDirectory + "WebRTC/rev.24472"; // Revision 24472 is Release 70
			string VS2013Friendly_WebRtcSdkPath = Target.UEThirdPartySourceDirectory + "WebRTC/VS2013_friendly";

			string PlatformSubdir = Target.Platform.ToString();
			string ConfigPath = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT) ? "Debug" :"Release";

			if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
			{
				PublicDefinitions.Add("WEBRTC_WIN=1");

				string VisualStudioVersionFolder = "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();

				string IncludePath = Path.Combine(WebRtcSdkPath, "Include", PlatformSubdir, VisualStudioVersionFolder);
				PublicSystemIncludePaths.Add(IncludePath);
				string AbslthirdPartyIncludePath = Path.Combine(WebRtcSdkPath, "Include", PlatformSubdir, VisualStudioVersionFolder, "third_party", "abseil-cpp");
				PublicSystemIncludePaths.Add(AbslthirdPartyIncludePath);

				string LibraryPath = Path.Combine(WebRtcSdkPath, "Lib", PlatformSubdir, VisualStudioVersionFolder, ConfigPath);

				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "protobuf_full.lib"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "protobuf_lite.lib"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "protoc_lib.lib"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "system_wrappers.lib"));
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
			else if (Target.Platform == UnrealTargetPlatform.PS4)
			{
				PublicDefinitions.Add("WEBRTC_ORBIS");
				PublicDefinitions.Add("FEATURE_ENABLE_SSL");
				PublicDefinitions.Add("SSL_USE_OPENSSL");
				PublicDefinitions.Add("EXPAT_RELATIVE_PATH");

				string IncludePath = Path.Combine(VS2013Friendly_WebRtcSdkPath, "include", PlatformSubdir);
				PublicSystemIncludePaths.Add(IncludePath);

				string LibraryPath = Path.Combine(VS2013Friendly_WebRtcSdkPath, "lib", PlatformSubdir, ConfigPath);

				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "rtc_base.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "rtc_base_approved.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "rtc_xmllite.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "rtc_xmpp.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "expat.a"));
			}

			AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
		}

	}
}
