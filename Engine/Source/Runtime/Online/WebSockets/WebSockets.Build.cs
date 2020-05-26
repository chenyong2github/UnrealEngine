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
			return Target.Platform.IsInGroup(UnrealPlatformGroup.Windows);
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
				AddEngineThirdPartyPrivateStaticDependencies(Target, "WinHttp");

				// We need to access the WinHttp folder in HTTP
				PrivateIncludePaths.AddRange(
					new string[] {
						"Runtime/Online/HTTP/Private",
					}
				);
			}
			else
			{
				PublicDefinitions.Add("WITH_WINHTTP=0");
			}
		}

		PublicDefinitions.Add("WEBSOCKETS_PACKAGE=1");
		PublicDefinitions.Add("WITH_WEBSOCKETS=" + (bWithWebSockets ? "1" : "0"));
		PublicDefinitions.Add("WITH_LIBWEBSOCKETS=" + (bWithLibWebSockets ? "1" : "0"));
	}
}
