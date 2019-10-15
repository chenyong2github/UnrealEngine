// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
			bool bPlatformSupportsXboxWebsockets = Target.Platform == UnrealTargetPlatform.XboxOne;
			bool bPlatformSupportsWinRTWebsockets = Target.Platform == UnrealTargetPlatform.HoloLens;

			return PlatformSupportsLibWebsockets || bPlatformSupportsXboxWebsockets || bPlatformSupportsWinRTWebsockets;
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
		}

		PublicDefinitions.Add("WEBSOCKETS_PACKAGE=1");
		PublicDefinitions.Add("WITH_WEBSOCKETS=" + (bWithWebSockets ? "1" : "0"));
		PublicDefinitions.Add("WITH_LIBWEBSOCKETS=" + (bWithLibWebSockets ? "1" : "0"));
	}
}
