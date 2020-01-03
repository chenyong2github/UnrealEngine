// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class EditableMesh : ModuleRules
	{
        public EditableMesh(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
                new string[] {
					"RHI",
				    "Core",
				    "CoreUObject",
				    "Engine",
                    "MeshDescription",
					"StaticMeshDescription",
                    "GeometryCollectionCore",
                    "GeometryCollectionEngine",
                    "RenderCore"	// @todo mesheditor: For FlushRenderingCommands()
				}
			);

			AddEngineThirdPartyPrivateStaticDependencies(Target, new string[] { "MikkTSpace", "OpenSubdiv" });
        }
    }
}