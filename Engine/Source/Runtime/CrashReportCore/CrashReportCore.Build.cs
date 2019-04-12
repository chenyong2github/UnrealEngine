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
                "CrashDebugHelper",
                "XmlParser",
                "Analytics",
                "AnalyticsET",
                 "HTTP",
                "Json",
           }
        );

        PrecompileForTargets = PrecompileTargetsType.None;

        if (Target.Type == TargetType.Game || Target.Type == TargetType.Client)
        {
            IsRedistributableOverride = true;
        }
        else
        {
            PublicDependencyModuleNames.Add("SourceControl");
        }
    }
}
