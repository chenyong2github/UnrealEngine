// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DataprepGeometryOperations : ModuleRules
	{
		public DataprepGeometryOperations(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"DataprepCore",
					"DataprepLibraries",
					"DynamicMesh",
					"EditorScriptingUtilities",
					"Engine",
					"GeometricObjects",
					"MeshConversion",
					"MeshDescription",
					"MeshMergeUtilities",
					"MeshModelingTools",
					"MeshUtilitiesCommon",
					"MeshProcessingLibrary",
					"ModelingOperators",
					"ModelingOperatorsEditorOnly",
					"MeshReductionInterface",
					"StaticMeshDescription",
					"StaticMeshEditorExtension",
					"UnrealEd",
				}
			);

			bool bWithProxyLOD = Target.Platform == UnrealTargetPlatform.Win64;
			PrivateDefinitions.Add("WITH_PROXYLOD=" + (bWithProxyLOD ? '1' : '0'));
			if (bWithProxyLOD)
			{
				// For boost:: and TBB:: code
				bEnableUndefinedIdentifierWarnings = false;
				bUseRTTI = true;

				PrivateDependencyModuleNames.AddRange(
					new string[] {
						"ProxyLODMeshReduction",
					}
				);
			}
		}
	}
}
