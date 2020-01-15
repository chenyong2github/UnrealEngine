// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class MeshDescriptionOperations : ModuleRules
    {
        public MeshDescriptionOperations(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.Add("Developer/MeshDescriptionOperations/Private");
            PublicIncludePaths.Add("Developer/MeshDescriptionOperations/Public");

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "RenderCore",
					"MeshDescription",
                    "StaticMeshDescription",
                    "MeshUtilitiesCommon",
                    "RawMesh",
                    "RHI"
                }
            );

            AddEngineThirdPartyPrivateStaticDependencies(Target, "MikkTSpace");
        }
    }
}
