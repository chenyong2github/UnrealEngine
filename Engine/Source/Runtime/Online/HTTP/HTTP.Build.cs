// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using Tools.DotNETCommon;

public class HTTP : ModuleRules
{
	protected virtual bool bPlatformSupportsWinHttp
	{
		get
		{
			return Target.Platform.IsInGroup(UnrealPlatformGroup.Windows);
		}
	}

	protected virtual bool bPlatformSupportsLibCurl
	{
		get
		{
			return Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
				Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) ||
				Target.IsInPlatformGroup(UnrealPlatformGroup.Android) ||
				Target.Platform == UnrealTargetPlatform.Switch;
		}
	}

	protected virtual bool bPlatformRequiresOpenSSL
	{
		get
		{
			return Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
				Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) ||
				Target.IsInPlatformGroup(UnrealPlatformGroup.Android);
		}
	}

	public HTTP(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDefinitions.Add("HTTP_PACKAGE=1");

		PrivateIncludePaths.AddRange(
			new string[] {
				"Runtime/Online/HTTP/Private",
			}
			);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core"
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"SSL",
			}
			);

		if (bPlatformSupportsLibCurl)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Sockets",
				}
			);

			PublicDefinitions.Add("CURL_ENABLE_DEBUG_CALLBACK=1");
			if (Target.Configuration != UnrealTargetConfiguration.Shipping)
			{
				PublicDefinitions.Add("CURL_ENABLE_NO_TIMEOUTS_OPTION=1");
			}
		}
		else
		{
			PublicDefinitions.Add("WITH_LIBCURL=0");
		}

		// Use Curl over WinHttp on platforms that support it (until WinHttp client security is in a good place at the least)
		if (bPlatformSupportsWinHttp)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "WinHttp");
			PublicDefinitions.Add("WITH_WINHTTP=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_WINHTTP=0");
		}

		if (bPlatformRequiresOpenSSL)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
		}

		if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.TVOS || Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("Security");
		}
	}
}
