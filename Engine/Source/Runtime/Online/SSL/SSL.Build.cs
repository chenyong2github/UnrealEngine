// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SSL : ModuleRules
{
	protected virtual bool PlatformSupportsSSL
	{
		get
		{
			return
				Target.Platform == UnrealTargetPlatform.Mac ||
				Target.Platform == UnrealTargetPlatform.Win32 ||
				Target.Platform == UnrealTargetPlatform.Win64 ||
				Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) ||
	            Target.Platform == UnrealTargetPlatform.IOS ||
	            Target.Platform == UnrealTargetPlatform.Android ||
				Target.Platform == UnrealTargetPlatform.Lumin ||
	            Target.Platform == UnrealTargetPlatform.PS4;
		}
	}

    public SSL(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDefinitions.Add("SSL_PACKAGE=1");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
			}
		);

		if (PlatformSupportsSSL)
		{
			PublicDefinitions.Add("WITH_SSL=1");

			PrivateIncludePaths.AddRange(
				new string[] {
					"Runtime/Online/SSL/Private",
				}
			);

			AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

			if (Target.Platform == UnrealTargetPlatform.Win32 ||
				Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicSystemLibraries.Add("crypt32.lib");
			}
		}
		else
		{
			PublicDefinitions.Add("WITH_SSL=0");
		}
    }
}
