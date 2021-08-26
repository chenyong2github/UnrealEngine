// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InterchangePipelines : ModuleRules
	{
		public InterchangePipelines(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InterchangeCore",
					"InterchangeEngine",
					"InterchangeNodes"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CinematicCamera",
				}
			);

			if (Target.Type == TargetType.Editor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"PhysicsUtilities",
						"TextureUtilitiesCommon",
					}
				);
			}
		}
	}
}
