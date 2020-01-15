// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms("Win64", "Mac", "Linux")]
[SupportedConfigurations(UnrealTargetConfiguration.Debug, UnrealTargetConfiguration.Development, UnrealTargetConfiguration.Shipping)]
public class CrashReportClientEditorTarget : CrashReportClientTarget
{
    public CrashReportClientEditorTarget(TargetInfo Target) : base(Target)
    {
        LaunchModuleName = "CrashReportClientEditor";

		AdditionalPlugins.Add("UdpMessaging");
		AdditionalPlugins.Add("ConcertSyncServer");
		bBuildWithEditorOnlyData = false;
		bCompileWithPluginSupport = true; // Enable Developer plugins (like Concert!)

		bBuildDeveloperTools = true;

		if (Target.Configuration == UnrealTargetConfiguration.Shipping && LinkType == TargetLinkType.Monolithic)
		{
			// DisasterRecovery/Concert needs message bus to run. If not enabled, Recovery Service will self-disable as well. In Shipping
			// message bus is turned off by default but for a monolithic build, it can be turned on just for this executable.
			GlobalDefinitions.Add("PLATFORM_SUPPORTS_MESSAGEBUS=1");
		}
	}
}
