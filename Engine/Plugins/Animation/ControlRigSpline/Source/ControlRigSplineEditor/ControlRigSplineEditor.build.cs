// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ControlRigSplineEditor : ModuleRules
	{
		public ControlRigSplineEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			bEnableUndefinedIdentifierWarnings = false;

			if (Target.bLWCDisabled)
			{
				PublicDefinitions.Add("TINYSPLINE_FLOAT_PRECISION=1");
			}
			PrivateIncludePaths.Add("ControlRigSpline/ThirdParty/TinySpline");

			PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ControlRigDeveloper",
				"ControlRigSpline"
			});
		}
	}
}
