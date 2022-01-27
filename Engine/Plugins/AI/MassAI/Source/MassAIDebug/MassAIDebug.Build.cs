// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MassAIDebug : ModuleRules
	{
		public MassAIDebug(ReadOnlyTargetRules Target) : base(Target)
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
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"MassEntity",
					"NavigationSystem",
					"StateTreeModule",
					"MassGameplayDebug",
					"MassActors",
					"MassAIBehavior",
					"MassCommon",
					"MassMovement",
					"MassNavigation",
					"MassZoneGraphNavigation",
					"MassAIReplication",
					"MassSmartObjects",
					"MassSpawner",
					"MassSimulation",
					"MassRepresentation",
					"MassSignals",
					"MassLOD",
					"StructUtils",
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("UnrealEd");
				PublicDependencyModuleNames.Add("MassEntityEditor");
			}

			if (Target.bBuildDeveloperTools || (Target.Configuration != UnrealTargetConfiguration.Shipping && Target.Configuration != UnrealTargetConfiguration.Test))
			{
				PrivateDependencyModuleNames.Add("GameplayDebugger");
				PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=0");
			}
		}
	}
}
