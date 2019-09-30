// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms("Win64", "Mac", "Linux")]
[SupportedConfigurations(UnrealTargetConfiguration.Debug, UnrealTargetConfiguration.Development, UnrealTargetConfiguration.Shipping)]
public class CrashReportClientEditorTarget : CrashReportClientTarget
{
    public CrashReportClientEditorTarget(TargetInfo Target) : base(Target)
    {
        LaunchModuleName = "CrashReportClientEditor";
    }
}
