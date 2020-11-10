// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms("Win64", "Linux", "Mac")]
public class UnrealInsightsTarget : TargetRules
{
	public UnrealInsightsTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Modular; // TargetLinkType.Monolithic;

		LaunchModuleName = "UnrealInsights";
		ExtraModuleNames.Add("EditorStyle");

		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = true;
		bForceBuildTargetPlatforms = true;
		bCompileWithStatsWithoutEngine = true;
		bCompileWithPluginSupport = true;

		// For source code editor access & regex (crossplatform)
		bIncludePluginsForTargetPlatforms = true;
		bCompileICU = true;

		// For UI functionality
		bBuildDeveloperTools = true;

		bHasExports = false;
	}
}
