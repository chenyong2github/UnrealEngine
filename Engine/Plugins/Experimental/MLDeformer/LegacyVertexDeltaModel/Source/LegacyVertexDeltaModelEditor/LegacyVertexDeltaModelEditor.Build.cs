// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class LegacyVertexDeltaModelEditor : ModuleRules
	{
		public LegacyVertexDeltaModelEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"EditorFramework",
					"UnrealEd",
					"CoreUObject",
					"Engine",
					"Slate",
					"SlateCore",
					"EditorWidgets",
					"EditorStyle",
					"GeometryCache",
					"MLDeformerFramework",
					"MLDeformerFrameworkEditor",
					"LegacyVertexDeltaModel",
					"PropertyEditor",
					"NeuralNetworkInference",
					"ToolWidgets",
					"ComputeFramework"
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
