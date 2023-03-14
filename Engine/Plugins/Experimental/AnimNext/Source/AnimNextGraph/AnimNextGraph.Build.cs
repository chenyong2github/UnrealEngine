// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimNextGraph : ModuleRules
	{
		public AnimNextGraph(ReadOnlyTargetRules Target) : base(Target)
		{
		
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"RigVM",
					"ControlRig",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"AnimNext",
					"Engine"
				}
			);
		}
	}
}