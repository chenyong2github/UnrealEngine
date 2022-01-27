// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MassAIBehavior : ModuleRules
	{
		public MassAIBehavior(ReadOnlyTargetRules Target) : base(Target)
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
					"MassEntity",
					"Core",
					"CoreUObject",
					"Engine",
					"MassActors",
					"MassCommon",
					"MassLOD",
					"MassMovement",
					"MassNavigation",
					"MassZoneGraphNavigation",
					"MassRepresentation",
					"MassSignals",
					"MassSmartObjects",
					"MassSpawner",
					"MassSimulation",
					"NavigationSystem",
					"SmartObjectsModule",
					"StateTreeModule",
					"StructUtils",
					"ZoneGraph",
					"ZoneGraphAnnotations"
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
