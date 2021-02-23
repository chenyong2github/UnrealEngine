// Some copyright should be here...

using UnrealBuildTool;

public class InstanceLevelCollision : ModuleRules
{
	public InstanceLevelCollision(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"UnrealEd"
				}
			);
		}
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InteractiveToolsFramework",
				"DynamicMesh",
				"MeshConversion",
				"ModelingOperators",
				"ModelingOperatorsEditorOnly",
				"DynamicMesh",
				"MeshModelingTools",
				"ModelingComponents",
				"GeometricObjects",
				"GeometryAlgorithms",
				"AssetTools",
				"MeshUtilitiesCommon",
				"MeshDescription",
				"StaticMeshDescription",
				"MeshReductionInterface"
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
