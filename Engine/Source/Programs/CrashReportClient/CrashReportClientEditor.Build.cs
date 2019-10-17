// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CrashReportClientEditor : CrashReportClient
{
	public CrashReportClientEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDefinitions.AddRange(
			new string[]
			{
				"CRASH_REPORT_WITH_RECOVERY=1",
				"CRASH_REPORT_WITH_MTBF=1",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Concert",
				"EditorAnalyticsSession",
				"Messaging",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"ConcertSyncCore",
				"ConcertSyncServer",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"ConcertSyncServer",
				"UdpMessaging",
			}
		);
	}
}
