// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CrashReportCore : ModuleRules
{
	public CrashReportCore( ReadOnlyTargetRules Target ) : base(Target)
	{
		PrivateIncludePaths.AddRange(
		new string[] {
				"Runtime/CrashReportCore/Private/",
            	}
        );

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
                "XmlParser",
                "Analytics",
                "AnalyticsET",
				"HTTP",
                "Json",
           }
        );

		if (Target.Type != TargetType.Editor)
        {
            PrecompileForTargets = PrecompileTargetsType.None;
        }

        if (Target.Type == TargetType.Game || Target.Type == TargetType.Client || Target.Type == TargetType.Program)
        {
			PublicDependencyModuleNames.Add("CrashDebugHelper");
			PublicDefinitions.Add("WITH_CRASHREPORTER=1");
		}
		else
        {
			PublicDefinitions.Add("WITH_CRASHREPORTER=0");
		}
    }
}
