// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MLDeformerEditor : ModuleRules
	{
		public MLDeformerEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"AdvancedPreviewScene",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"InputCore",
					"EditorFramework",
					"UnrealEd",
					"ToolMenus",
					"CoreUObject",
					"Engine",
					"Slate",
					"SlateCore",
					"EditorWidgets",
					"Kismet",
					"KismetWidgets",
					"EditorStyle",
					"Persona",
					"SkeletonEditor",
					"GeometryCache",

					"PropertyEditor",
					"BlueprintGraph",
					"AnimGraph",
					"AnimGraphRuntime",

					"NeuralNetworkInference",

					"MLDeformer",
					"ToolWidgets",
					"ComputeFramework",
					"OptimusDeveloper",

					"OptimusCore",
					"RenderCore",
					"RHI",
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[]
				{
				}
			);
		}
	}
}
