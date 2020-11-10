// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ControlRigSpline : ModuleRules
	{
		public ControlRigSpline(ReadOnlyTargetRules Target) : base(Target)
		{
			bEnableUndefinedIdentifierWarnings = false;

			PublicDefinitions.Add("TINYSPLINE_FLOAT_PRECISION=1");
			PrivateIncludePaths.Add("ControlRigSpline/ThirdParty/TinySpline");

			PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RigVM",
				"ControlRig",
			});
		}
	}
}
