// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ProxyLODMeshReduction : ModuleRules
	{
		public ProxyLODMeshReduction(ReadOnlyTargetRules Target) : base(Target)
		{

			// For boost:: and TBB:: code
			bUseRTTI = true;

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
                    "UVAtlas",
                    "DirectXMesh",
                    "OpenVDB",
                    "UEOpenExr",
                    "TraceLog",
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "InputCore",
                    "UnrealEd",
                    "RawMesh",
                    "MeshDescription",
					"StaticMeshDescription",
                    "MeshUtilities",
                    "MaterialUtilities",
                    "PropertyEditor",
                    "SlateCore",
                    "Slate",
                    "EditorStyle",
                    "RenderCore",
                    "RHI",
                    "QuadricMeshReduction"
					// ... add other public dependencies that you statically link with here ...
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "Core",
                    "CoreUObject",
					"EditorFramework",
                    "Engine",
                    "UnrealEd",
					// ... add private dependencies that you statically link with here ...
				}
				);

			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					// ... add any modules that your module loads dynamically here ...
				}
				);

			// OpenVDB pulls in a dependency for tbb.dll, make sure we specify it as necessary so the dll is sure to be where we need it to be
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				string IntelTBBLibs = Target.UEThirdPartyBinariesDirectory + "Intel/TBB/";
				RuntimeDependencies.Add("$(TargetOutputDir)/tbb.dll", IntelTBBLibs + "Win64/tbb.dll");
			}
		}
	}
}
