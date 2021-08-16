// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteShared.h"
#include "RHI.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "Rendering/NaniteStreamingManager.h"

DEFINE_LOG_CATEGORY(LogNanite);
DEFINE_GPU_STAT(NaniteDebug);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FNaniteUniformParameters, "Nanite");

extern float GNaniteMaxPixelsPerEdge;
extern float GNaniteMinPixelsPerEdgeHW;

// Optimized compute dual depth export pass on supported platforms.
int32 GNaniteExportDepth = 1;
static FAutoConsoleVariableRef CVarNaniteExportDepth(
	TEXT("r.Nanite.ExportDepth"),
	GNaniteExportDepth,
	TEXT("")
);

namespace Nanite
{

void FPackedView::UpdateLODScales()
{
	const float ViewToPixels = 0.5f * ViewToClip.M[1][1] * ViewSizeAndInvSize.Y;

	const float LODScale = ViewToPixels / GNaniteMaxPixelsPerEdge;
	const float LODScaleHW = ViewToPixels / GNaniteMinPixelsPerEdgeHW;

	LODScales = FVector2D(LODScale, LODScaleHW);
}

FPackedView CreatePackedView( const FPackedViewParams& Params )
{
	// NOTE: There is some overlap with the logic - and this should stay consistent with - FSceneView::SetupViewRectUniformBufferParameters
	// Longer term it would be great to refactor a common place for both of this logic, but currently FSceneView has a lot of heavy-weight
	// stuff in it beyond the relevant parameters to SetupViewRectUniformBufferParameters (and Nanite has a few of its own parameters too).

	FPackedView PackedView;

	PackedView.TranslatedWorldToView		= Params.ViewMatrices.GetOverriddenTranslatedViewMatrix();
	PackedView.TranslatedWorldToClip		= Params.ViewMatrices.GetTranslatedViewProjectionMatrix();
	PackedView.ViewToClip					= Params.ViewMatrices.GetProjectionMatrix();
	PackedView.ClipToWorld					= Params.ViewMatrices.GetInvViewProjectionMatrix();
	PackedView.PreViewTranslation			= Params.ViewMatrices.GetPreViewTranslation();
	PackedView.WorldCameraOrigin			= FVector4(Params.ViewMatrices.GetViewOrigin(), 0.0f);
	PackedView.ViewForwardAndNearPlane		= FVector4(Params.ViewMatrices.GetOverriddenTranslatedViewMatrix().GetColumn(2), Params.ViewMatrices.ComputeNearPlane());

	PackedView.PrevTranslatedWorldToView	= Params.PrevViewMatrices.GetOverriddenTranslatedViewMatrix();
	PackedView.PrevTranslatedWorldToClip	= Params.PrevViewMatrices.GetTranslatedViewProjectionMatrix();
	PackedView.PrevViewToClip				= Params.PrevViewMatrices.GetProjectionMatrix();
	PackedView.PrevClipToWorld				= Params.PrevViewMatrices.GetInvViewProjectionMatrix();
	PackedView.PrevPreViewTranslation		= Params.PrevViewMatrices.GetPreViewTranslation();

	const FIntRect &ViewRect = Params.ViewRect;
	const FVector4 ViewSizeAndInvSize(ViewRect.Width(), ViewRect.Height(), 1.0f / float(ViewRect.Width()), 1.0f / float(ViewRect.Height()));

	PackedView.ViewRect = FIntVector4(ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
	PackedView.ViewSizeAndInvSize = ViewSizeAndInvSize;

	// Transform clip from full screen to viewport.
	FVector2D RcpRasterContextSize = FVector2D(1.0f / Params.RasterContextSize.X, 1.0f / Params.RasterContextSize.Y);
	PackedView.ClipSpaceScaleOffset = FVector4(	ViewSizeAndInvSize.X * RcpRasterContextSize.X,
												ViewSizeAndInvSize.Y * RcpRasterContextSize.Y,
												 ( ViewSizeAndInvSize.X + 2.0f * ViewRect.Min.X) * RcpRasterContextSize.X - 1.0f,
												-( ViewSizeAndInvSize.Y + 2.0f * ViewRect.Min.Y) * RcpRasterContextSize.Y + 1.0f );

	const float Mx = 2.0f * ViewSizeAndInvSize.Z;
	const float My = -2.0f * ViewSizeAndInvSize.W;
	const float Ax = -1.0f - 2.0f * ViewRect.Min.X * ViewSizeAndInvSize.Z;
	const float Ay = 1.0f + 2.0f * ViewRect.Min.Y * ViewSizeAndInvSize.W;

	PackedView.SVPositionToTranslatedWorld =
		FMatrix(FPlane(Mx, 0, 0, 0),
			FPlane(0, My, 0, 0),
			FPlane(0, 0, 1, 0),
			FPlane(Ax, Ay, 0, 1)) * Params.ViewMatrices.GetInvTranslatedViewProjectionMatrix();
	PackedView.ViewToTranslatedWorld = Params.ViewMatrices.GetOverriddenInvTranslatedViewMatrix();

	check(Params.StreamingPriorityCategory <= STREAMING_PRIORITY_CATEGORY_MASK);
	PackedView.StreamingPriorityCategory_AndFlags = (Params.Flags << NUM_STREAMING_PRIORITY_CATEGORY_BITS) | Params.StreamingPriorityCategory;
	PackedView.MinBoundsRadiusSq = Params.MinBoundsRadius * Params.MinBoundsRadius;
	PackedView.UpdateLODScales();

	PackedView.LODScales.X *= Params.LODScaleFactor;

	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.X = Params.TargetLayerIndex;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Y = Params.TargetMipLevel;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z = Params.TargetMipCount;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.W = Params.PrevTargetLayerIndex;

	PackedView.HZBTestViewRect = FIntVector4(Params.HZBTestViewRect.Min.X, Params.HZBTestViewRect.Min.Y, Params.HZBTestViewRect.Max.X, Params.HZBTestViewRect.Max.Y);
	
	return PackedView;
}

FPackedView CreatePackedViewFromViewInfo
(
	const FViewInfo& View,
	FIntPoint RasterContextSize,
	uint32 Flags,
	uint32 StreamingPriorityCategory,
	float MinBoundsRadius,
	float LODScaleFactor
)
{
	FPackedViewParams Params;
	Params.ViewMatrices = View.ViewMatrices;
	Params.PrevViewMatrices = View.PrevViewInfo.ViewMatrices;
	Params.ViewRect = View.ViewRect;
	Params.RasterContextSize = RasterContextSize;
	Params.Flags = Flags;
	Params.StreamingPriorityCategory = StreamingPriorityCategory;
	Params.MinBoundsRadius = MinBoundsRadius;
	Params.LODScaleFactor = LODScaleFactor;
	Params.HZBTestViewRect = View.PrevViewInfo.ViewRect;
	return CreatePackedView(Params);
}

} // namespace Nanite

bool ShouldRenderNanite(const FScene* Scene, const FViewInfo& View, bool bCheckForAtomicSupport)
{
	// Does the platform support Nanite (with 64bit image atomics), and is it enabled?
	if (Scene && UseNanite(Scene->GetShaderPlatform(), bCheckForAtomicSupport))
	{
		// Any resources registered to the streaming manager?
		if (Nanite::GStreamingManager.HasResourceEntries())
		{
			// Is the view family showing Nanite meshes?
			return View.Family->EngineShowFlags.NaniteMeshes;
		}
	}

	// Nanite should not render for this view
	return false;
}

bool WouldRenderNanite(const FScene* Scene, const FViewInfo& View, bool bCheckForAtomicSupport, bool bCheckForProjectSetting)
{
	// Does the platform support Nanite (with 64bit image atomics), and is it enabled?
	if (Scene && UseNanite(Scene->GetShaderPlatform(), bCheckForAtomicSupport, bCheckForProjectSetting))
	{
		// Is the view family showing would-be Nanite meshes?
		return View.Family->EngineShowFlags.NaniteMeshes;
	}

	// Nanite would not render for this view
	return false;
}


bool UseComputeDepthExport()
{
	return (GRHISupportsDepthUAV && GRHISupportsExplicitHTile && GNaniteExportDepth != 0);
}
