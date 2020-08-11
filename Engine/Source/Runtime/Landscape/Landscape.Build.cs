// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Landscape : ModuleRules
{
	public Landscape(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				"Runtime/Engine/Private", // for Engine/Private/Collision/PhysXCollision.h
				"Runtime/Landscape/Private",
                "../Shaders/Shared"
            }
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"TargetPlatform",
				"DerivedDataCache",
				"Foliage",
				"Renderer",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"Engine",
				"RenderCore", 
				"RHI",
				"Renderer",
				"Foliage",
				"DeveloperSettings"
			}
		);

		SetupModulePhysicsSupport(Target);
		if (Target.bCompilePhysX && Target.bBuildEditor)
		{
			DynamicallyLoadedModuleNames.Add("PhysXCooking");
		}

		if (Target.Type == TargetType.Editor || Target.Type == TargetType.Program)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"MeshDescription",
					"StaticMeshDescription",
                    "MeshUtilitiesCommon"
				}
			);
		}

		if (Target.bBuildEditor == true)
		{
			// TODO: Remove all landscape editing code from the Landscape module!!!
			PrivateIncludePathModuleNames.Add("LandscapeEditor");

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"UnrealEd",
					"MaterialUtilities", 
					"SlateCore",
					"Slate",
				}
			);

			CircularlyReferencedDependentModules.AddRange(
				new string[] {
					"UnrealEd",
					"MaterialUtilities",
				}
			);
		}
	}
}
