// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class MassEntity : ModuleRules
	{
		public MassEntity(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicIncludePaths.AddRange(
				new string[] {
				ModuleDirectory + "/Public",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"StructUtils",
					"DeveloperSettings",
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("UnrealEd");
			}

			if (Target.bBuildDeveloperTools == true)
			{
				PrivateDependencyModuleNames.Add("MessageLog");
			}

			// @todo will be enables once we're confident in MASS_DO_PARALLEL implementation 
			// PublicDefinitions.Add("MASS_DO_PARALLEL=" + ((Target.Type != TargetType.Server) ? 1 : 0));
		}
	}
}