// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WebSockets : ModuleRules
{
	protected virtual bool PlatformSupportsLibWebsockets
	{
		get
		{
			return
				Target.Platform == UnrealTargetPlatform.Win32 ||
				Target.Platform == UnrealTargetPlatform.Win64 ||
				Target.Platform == UnrealTargetPlatform.Android ||
				Target.Platform == UnrealTargetPlatform.Mac ||
				Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) ||
				Target.Platform == UnrealTargetPlatform.IOS ||
				Target.Platform == UnrealTargetPlatform.Switch;
		}
	}

	protected virtual bool bPlatformSupportsWinHttpWebSockets
	{
		get
		{
			// Availability requires Windows 8.1 or greater, as this is the min version of WinHttp that supports WebSockets
			return Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.WindowsPlatform.TargetWindowsVersion >= 0x0603;
		}
	}

	protected virtual bool UsePlatformSSL
	{
		get
		{
			return Target.Platform == UnrealTargetPlatform.Switch;
		}
	}

	protected virtual bool ShouldUseModule
	{
		get
		{
			bool bPlatformSupportsWinRTWebsockets = Target.Platform == UnrealTargetPlatform.HoloLens;

			return PlatformSupportsLibWebsockets || bPlatformSupportsWinRTWebsockets || bPlatformSupportsWinHttpWebSockets;
		}
	}

	public WebSockets(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"HTTP"
			}
		);

		bool bWithWebSockets = false;
		bool bWithLibWebSockets = false;
		bool bWithWinHttpWebSockets = false;

		if (ShouldUseModule)
		{
			bWithWebSockets = true;

			PrivateIncludePaths.AddRange(
				new string[] {
					"Runtime/Online/WebSockets/Private",
				}
			);

			if (PlatformSupportsLibWebsockets)
			{
				bWithLibWebSockets = true;

				if (UsePlatformSSL)
				{
					PrivateDefinitions.Add("WITH_SSL=0");
					AddEngineThirdPartyPrivateStaticDependencies(Target, "libWebSockets");
				}
				else
				{
					AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL", "libWebSockets", "zlib");
					PrivateDependencyModuleNames.Add("SSL");
				}
			}
			if (bPlatformSupportsWinHttpWebSockets)
			{
				// Enable WinHttp Support
				bWithWinHttpWebSockets = true;

				AddEngineThirdPartyPrivateStaticDependencies(Target, "WinHttp");

				// We need to access the WinHttp folder in HTTP
				PrivateIncludePaths.AddRange(
					new string[] {
						"Runtime/Online/HTTP/Private",
					}
				);
			}
		}

		PublicDefinitions.Add("WEBSOCKETS_PACKAGE=1");
		PublicDefinitions.Add("WITH_WEBSOCKETS=" + (bWithWebSockets ? "1" : "0"));
		PublicDefinitions.Add("WITH_LIBWEBSOCKETS=" + (bWithLibWebSockets ? "1" : "0"));
		PublicDefinitions.Add("WITH_WINHTTPWEBSOCKETS=" + (bWithWinHttpWebSockets ? "1" : "0"));
	}
}
