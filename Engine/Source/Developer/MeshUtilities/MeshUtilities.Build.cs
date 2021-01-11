// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class MeshUtilities : ModuleRules
{
	public MeshUtilities(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicDependencyModuleNames.AddRange(
            new string[] {
				"MaterialUtilities",

			}
        );

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"RawMesh",
				"RenderCore", // For FPackedNormal
				"SlateCore",
				"Slate",
				"MaterialUtilities",
				"MeshBoneReduction",
				"UnrealEd",
				"RHI",
				"HierarchicalLODUtilities",
				"Landscape",
				"LevelEditor",
				"PropertyEditor",
				"EditorStyle",
                "GraphColor",
                "MeshBuilderCommon",
                "MeshUtilitiesCommon",
                "MeshDescription",
				"StaticMeshDescription",
				"ToolMenus",
            }
		);

        PublicIncludePathModuleNames.AddRange(
            new string[] {
                "MeshMergeUtilities"
            }
        );

        PrivateIncludePathModuleNames.AddRange(
          new string[] {
				"AnimationBlueprintEditor",
				"AnimationEditor",
                "MeshMergeUtilities",
                "MaterialBaking",
				"Persona",
				"SkeletalMeshEditor",
          }
      );

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
				"AnimationBlueprintEditor",
				"AnimationEditor",
                "MeshMergeUtilities",
                "MaterialBaking",
				"SkeletalMeshEditor",
            }
        );

        AddEngineThirdPartyPrivateStaticDependencies(Target, "nvTriStrip");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "ForsythTriOptimizer");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "QuadricMeshReduction");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "MikkTSpace");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "nvTessLib");

		if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Win32))
		{
            AddEngineThirdPartyPrivateStaticDependencies(Target, "DX9");
		}

        // Always use the official version of IntelTBB
        string IntelTBBLibs = Target.UEThirdPartyBinariesDirectory + "Intel/TBB/";

        // EMBREE
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string SDKDir = Target.UEThirdPartySourceDirectory + "Intel/Embree/Embree2140/Win64/";

            PublicIncludePaths.Add(SDKDir + "include");
            PublicAdditionalLibraries.Add(SDKDir + "lib/embree.2.14.0.lib");
            RuntimeDependencies.Add("$(TargetOutputDir)/embree.2.14.0.dll", SDKDir + "lib/embree.2.14.0.dll");
            RuntimeDependencies.Add("$(TargetOutputDir)/tbb.dll", IntelTBBLibs + "Win64/tbb.dll");
            RuntimeDependencies.Add("$(TargetOutputDir)/tbbmalloc.dll", IntelTBBLibs + "Win64/tbbmalloc.dll");
            PublicDefinitions.Add("USE_EMBREE=1");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            string SDKDir = Target.UEThirdPartySourceDirectory + "Intel/Embree/Embree2140/MacOSX/";

            PublicIncludePaths.Add(SDKDir + "include");
            PublicAdditionalLibraries.Add(SDKDir + "lib/libembree.2.14.0.dylib");
            PublicAdditionalLibraries.Add(IntelTBBLibs + "Mac/libtbb.dylib");
            PublicAdditionalLibraries.Add(IntelTBBLibs + "Mac/libtbbmalloc.dylib");
            RuntimeDependencies.Add("$(TargetOutputDir)/libembree.2.14.0.dylib", SDKDir + "lib/libembree.2.14.0.dylib");
            RuntimeDependencies.Add("$(TargetOutputDir)/libtbb.dylib", IntelTBBLibs + "Mac/libtbb.dylib");
            RuntimeDependencies.Add("$(TargetOutputDir)/libtbbmalloc.dylib", IntelTBBLibs + "Mac/libtbbmalloc.dylib");
            PublicDefinitions.Add("USE_EMBREE=1");
        }
        else
        {
            PublicDefinitions.Add("USE_EMBREE=0");
        }
	}
}
