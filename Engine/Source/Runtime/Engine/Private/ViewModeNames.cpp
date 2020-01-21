// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"

#define LOCTEXT_NAMESPACE "UViewModeUtils"

TArray<FText> FillViewModeDisplayNames()
{
	// Allocate size
	TArray<FText> ViewModeDisplayNames;
	ViewModeDisplayNames.Reserve(VMI_Unknown);
	// Fill ViewModeIndex, VMI_Unknown+1 to include VMI_Unknown too
	for (int32 Index = 0; Index < VMI_Unknown + 1; ++Index)
	{
		const EViewModeIndex ViewModeIndex = (EViewModeIndex)Index;

		// Wireframe w/ brushes
		if (ViewModeIndex == VMI_BrushWireframe)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_BrushWireframe", "Brush Wireframe"));
		}
		// Wireframe w/ BSP
		else if (ViewModeIndex == VMI_Wireframe)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_Wireframe", "Wireframe"));
		}
		// Unlit
		else if (ViewModeIndex == VMI_Unlit)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_Unlit", "Unlit"));
		}
		// Lit
		else if (ViewModeIndex == VMI_Lit)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_Lit", "Lit"));
		}
		else if (ViewModeIndex == VMI_Lit_DetailLighting)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_Lit_DetailLighting", "Detail Lighting"));
		}
		// Lit wo/ materials
		else if (ViewModeIndex == VMI_LightingOnly)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_LightingOnly", "Lighting Only"));
		}
		// Colored according to light count
		else if (ViewModeIndex == VMI_LightComplexity)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_LightComplexity", "Light Complexity"));
		}
		// Colored according to shader complexity
		else if (ViewModeIndex == VMI_ShaderComplexity)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_ShaderComplexity", "Shader Complexity"));
		}
		// Colored according to world-space LightMap texture density
		else if (ViewModeIndex == VMI_LightmapDensity)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_LightmapDensity", "Lightmap Density"));
		}
		// Colored according to light count - showing lightmap texel density on texture mapped objects
		else if (ViewModeIndex == VMI_LitLightmapDensity)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_LitLightmapDensity", "Lit Lightmap Density"));
		}
		else if (ViewModeIndex == VMI_ReflectionOverride)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_ReflectionOverride", "Reflections"));
		}
		else if (ViewModeIndex == VMI_VisualizeBuffer)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_VisualizeBuffer", "Buffer Visualization5"));
		}
		//	VMI_VoxelLighting = 13,
	
		// Colored according to stationary light overlap
		else if (ViewModeIndex == VMI_StationaryLightOverlap)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_StationaryLightOverlap", "Stationary Light Overlap"));
		}
	
		else if (ViewModeIndex == VMI_CollisionPawn)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_CollisionPawn", "Player Collision"));
		}
		else if (ViewModeIndex == VMI_CollisionVisibility)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_CollisionVisibility", "Visibility Collision"));
		}
		//VMI_UNUSED = 17,
		// Colored according to the current LOD index
		else if (ViewModeIndex == VMI_LODColoration)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_LODColoration", "Mesh LOD Coloration"));
		}
		// Colored according to the quad coverage
		else if (ViewModeIndex == VMI_QuadOverdraw)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_QuadOverdraw", "Quad Overdraw"));
		}
		// Visualize the accuracy of the primitive distance computed for texture streaming
		else if (ViewModeIndex == VMI_PrimitiveDistanceAccuracy)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_PrimitiveDistanceAccuracy", "Primitive Distance"));
		}
		// Visualize the accuracy of the mesh UV densities computed for texture streaming
		else if (ViewModeIndex == VMI_MeshUVDensityAccuracy)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_MeshUVDensityAccuracy", "Mesh UV Density"));
		}
		// Colored according to shader complexity, including quad overdraw
		else if (ViewModeIndex == VMI_ShaderComplexityWithQuadOverdraw)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_ShaderComplexityWithQuadOverdraw", "Shader Complexity & Quads"));
		}
		// Colored according to the current HLOD index
		else if (ViewModeIndex == VMI_HLODColoration)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_HLODColoration", "Hierarchical LOD Coloration"));
		}
		// Group item for LOD and HLOD coloration*/
		else if (ViewModeIndex == VMI_GroupLODColoration)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_GroupLODColoration", "Group LOD Coloration"));
		}
		// Visualize the accuracy of the material texture scales used for texture streaming
		else if (ViewModeIndex == VMI_MaterialTextureScaleAccuracy)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_MaterialTextureScaleAccuracy", "Material Texture Scales"));
		}
		// Compare the required texture resolution to the actual resolution
		else if (ViewModeIndex == VMI_RequiredTextureResolution)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_RequiredTextureResolution", "Required Texture Resolution"));
		}
	
		// Ray tracing modes
		// Run path tracing pipeline
		else if (ViewModeIndex == VMI_PathTracing)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_PathTracing", "Path Tracing"));
		}
		// Run ray tracing debug pipeline 
		else if (ViewModeIndex == VMI_RayTracingDebug)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_RayTracingDebug", "Ray Tracing Debug"));
		}

		// VMI_Max
		else if (ViewModeIndex == VMI_Max)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_Max", "Max EViewModeIndex value"));
		}
		// VMI_Unknown
		else if (ViewModeIndex == VMI_Unknown)
		{
			ViewModeDisplayNames.Emplace(LOCTEXT("UViewModeUtils_VMI_Unknown", "Unknown EViewModeIndex value"));
		}
		// Not considered case
		else
		{
			ViewModeDisplayNames.Emplace(FText::GetEmpty());
		}
	}
	// Return ViewModeIndex
	return ViewModeDisplayNames;
}

const static TArray<FText> GViewModeDisplayNames = FillViewModeDisplayNames();

FText UViewModeUtils::GetViewModeDisplayName(const EViewModeIndex ViewModeIndex)
{
	const FText ViewModeName = GViewModeDisplayNames[ViewModeIndex];
	ensureMsgf(!ViewModeName.IsEmpty(), TEXT("Used an unknown value of EViewModeIndex (with value %d). Consider adding this new value in UViewModeUtils::GetViewModeName"), ViewModeIndex);
	return ViewModeName;
}

#undef LOCTEXT_NAMESPACE
