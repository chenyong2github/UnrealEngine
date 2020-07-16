// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class LiveStreamAnimationEditor : ModuleRules
	{
		public LiveStreamAnimationEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PrivateDependencyModuleNames.AddRange(
				new string[] {
				"LiveStreamAnimation",
				"Core",
				"CoreUObject",
				"UnrealEd",
				"AssetTools",
				"LiveLinkInterface",
				"Engine"
				}
			);
		}
	}
}
