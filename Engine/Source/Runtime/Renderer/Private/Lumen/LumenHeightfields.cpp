// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenHeightfields.h"
#include "RendererPrivate.h"

static TAutoConsoleVariable<int32> CVarLumenSceneHeightfield(
	TEXT("r.LumenScene.Heightfield"),
	0,
	TEXT("Enables heightfield (Landscape) software ray tracing (default = 0)"),
	ECVF_RenderThreadSafe
);

bool Lumen::UseHeightfields(const FLumenSceneData& LumenSceneData)
{
	bool bHeightfieldEnabled = CVarLumenSceneHeightfield.GetValueOnRenderThread() != 0;
	bool bHasHeightfields = LumenSceneData.Heightfields.Num() > 0;
	return bHeightfieldEnabled && bHasHeightfields;
}

void FLumenHeightfieldGPUData::FillData(const FLumenHeightfield& RESTRICT Heightfield, const TSparseSpanArray<FLumenMeshCards>& MeshCards, FVector4f* RESTRICT OutData)
{
	// Note: layout must match GetLumenHeightfieldData in usf

	FVector3f BoundsCenter = FVector3f(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector3f BoundsExtent = FVector3f(0.0f, 0.0f, 0.0f);
	uint32 MeshCardsIndex = UINT32_MAX;

	if (Heightfield.MeshCardsIndex >= 0)
	{
		MeshCardsIndex = Heightfield.MeshCardsIndex;
		const FBox WorldSpaceBounds = MeshCards[Heightfield.MeshCardsIndex].GetWorldSpaceBounds();
		BoundsCenter = WorldSpaceBounds.GetCenter();
		BoundsExtent = WorldSpaceBounds.GetExtent();
	}

	OutData[0] = BoundsCenter;
	OutData[1] = BoundsExtent;
	OutData[0].W = *((float*)&MeshCardsIndex);

	static_assert(DataStrideInFloat4s == 2, "Data stride doesn't match");
}