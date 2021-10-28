// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MassAIMovement : ModuleRules
	{
		public MassAIMovement(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicIncludePaths.AddRange(
				new string[] {
				"Runtime/AIModule/Public",
				ModuleDirectory + "/Public",
				}
			);


			PrivateIncludePaths.AddRange(
				new string[] {
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"AIModule",
					"MassEntity",
					"Core",
					"CoreUObject",
					"Engine",
					"MassCommon",
					"MassLOD",
					"MassSignals",
					"MassSimulation",
					"MassSpawner",
					"MassMovement",
					"NavigationSystem",
					"StructUtils",
					"ZoneGraph",
					"ZoneGraphAnnotations",
					"DeveloperSettings"
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("EditorFramework");
				PrivateDependencyModuleNames.Add("UnrealEd");
			}
		}
	}
}
