// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Party : ModuleRules
{
	public Party(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDefinitions.Add("PARTY_PACKAGE=1");

		PublicDefinitions.Add("PARTY_PLATFORM_SESSIONS_PSN=" + (bUsesPSNSessions ? "1" : "0"));
		PublicDefinitions.Add("PARTY_PLATFORM_SESSIONS_XBL=" + (bUsesXBLSessions ? "1" : "0"));
		PublicDefinitions.Add("PARTY_PLATFORM_INVITE_PERMISSIONS=" + (bUsesPlatformInvitePermissions? "1" : "0"));

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"OnlineSubsystem",
				"OnlineSubsystemUtils",
			}
			);

		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
			}
			);

		if (Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Win64)
		{
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
		}
	}

	protected virtual bool bUsesPSNSessions { get { return false; } }
	protected virtual bool bUsesXBLSessions { get { return false; } }
	protected virtual bool bUsesPlatformInvitePermissions { get { return false; } }
}
