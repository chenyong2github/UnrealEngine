// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SmartObjectsModule : ModuleRules
	{
		public SmartObjectsModule(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicIncludePaths.AddRange(
			new string[] {
			}
			);

			PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"AIModule",
				"GameplayBehaviorsModule",
				"GameplayTasks",
				"GameplayTags",
				"GameplayAbilities"
			}
			);

			PrivateDependencyModuleNames.AddRange(
			new string[] {
				"RenderCore"
			}
			);
		}
	}
}
