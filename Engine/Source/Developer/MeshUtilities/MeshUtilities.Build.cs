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
				"EditorFramework",
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

		if (Target.Platform == UnrealTargetPlatform.Win64)
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
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) && Target.Architecture.StartsWith("x86_64")) // no support for arm64 yet
		{
			string SDKDir = Target.UEThirdPartySourceDirectory + "Intel/Embree/Embree2140/Linux/x86_64-unknown-linux-gnu/";

            PublicIncludePaths.Add(Path.Combine(SDKDir, "include"));
            PublicAdditionalLibraries.Add(Path.Combine(SDKDir, "lib/libembree.so"));
            PublicAdditionalLibraries.Add(Path.Combine(IntelTBBLibs, "Linux/libtbb.so"));
			// disabled for Linux atm due to a bug in libtbbmalloc on exit
            // PublicAdditionalLibraries.Add(Path.Combine(IntelTBBLibs, "Linux/libtbbmalloc.so"));
			RuntimeDependencies.Add(Path.Combine(IntelTBBLibs, "Linux/libtbb.so"));
			RuntimeDependencies.Add(Path.Combine(IntelTBBLibs, "Linux/libtbb.so.2"));
			// disabled for Linux atm due to a bug in libtbbmalloc on exit
            // RuntimeDependencies.Add(Path.Combine(IntelTBBLibs, "Linux/libtbbmalloc.so"));
            PublicDefinitions.Add("USE_EMBREE=1");
		}
        else
        {
            PublicDefinitions.Add("USE_EMBREE=0");
        }
	}
}
