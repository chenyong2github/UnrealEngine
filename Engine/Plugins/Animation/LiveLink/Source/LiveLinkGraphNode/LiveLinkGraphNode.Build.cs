// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class LiveLinkGraphNode : ModuleRules
	{
		public LiveLinkGraphNode(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{

				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AnimationCore",
					"AnimGraphRuntime",
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"KismetCompiler",
					"LiveLink",
					"LiveLinkInterface",
					"Persona",
					"SlateCore",
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd",
						"Kismet",
						"AnimGraph",
						"BlueprintGraph",
					}
				);
			}
		}
	}
}
