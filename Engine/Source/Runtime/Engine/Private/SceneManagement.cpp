// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SceneManagement.h"
#include "Misc/App.h"
#include "Engine/StaticMesh.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "StaticMeshResources.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "Runtime/Renderer/Private/SceneCore.h"
#include "Async/ParallelFor.h"
#include "LightMap.h"
#include "ShadowMap.h"
#include "Engine/Engine.h"
#include "Engine/LightMapTexture2D.h"
#include "Engine/ShadowMapTexture2D.h"
#include "VT/VirtualTexture.h"
#include "UnrealEngine.h"

static TAutoConsoleVariable<float> CVarLODTemporalLag(
	TEXT("lod.TemporalLag"),
	0.5f,
	TEXT("This controls the the time lag for temporal LOD, in seconds."),
	ECVF_Scalability | ECVF_Default);

void FTemporalLODState::UpdateTemporalLODTransition(const FViewInfo& View, float LastRenderTime)
{
	bool bOk = false;
	if (!View.bDisableDistanceBasedFadeTransitions)
	{
		bOk = true;
		TemporalLODLag = CVarLODTemporalLag.GetValueOnRenderThread();
		if (TemporalLODTime[1] < LastRenderTime - TemporalLODLag)
		{
			if (TemporalLODTime[0] < TemporalLODTime[1])
			{
				TemporalLODViewOrigin[0] = TemporalLODViewOrigin[1];
				TemporalDistanceFactor[0] = TemporalDistanceFactor[1];
				TemporalLODTime[0] = TemporalLODTime[1];
			}
			TemporalLODViewOrigin[1] = View.ViewMatrices.GetViewOrigin();
			TemporalDistanceFactor[1] = View.GetLODDistanceFactor();
			TemporalLODTime[1] = LastRenderTime;
			if (TemporalLODTime[1] <= TemporalLODTime[0])
			{
				bOk = false; // we are paused or something or otherwise didn't get a good sample
			}
		}
	}
	if (!bOk)
	{
		TemporalLODViewOrigin[0] = View.ViewMatrices.GetViewOrigin();
		TemporalLODViewOrigin[1] = View.ViewMatrices.GetViewOrigin();
		TemporalDistanceFactor[0] = View.GetLODDistanceFactor();
		TemporalDistanceFactor[1] = TemporalDistanceFactor[0];
		TemporalLODTime[0] = LastRenderTime;
		TemporalLODTime[1] = LastRenderTime;
		TemporalLODLag = 0.0f;
	}
}



FSimpleElementCollector::FSimpleElementCollector() :
	FPrimitiveDrawInterface(nullptr)
{
	static auto* MobileHDRCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
	bIsMobileHDR = (MobileHDRCvar->GetValueOnAnyThread() == 1);
}

FSimpleElementCollector::~FSimpleElementCollector()
{
	// Cleanup the dynamic resources.
	for(int32 ResourceIndex = 0;ResourceIndex < DynamicResources.Num();ResourceIndex++)
	{
		//release the resources before deleting, they will delete themselves
		DynamicResources[ResourceIndex]->ReleasePrimitiveResource();
	}
}

void FSimpleElementCollector::SetHitProxy(HHitProxy* HitProxy)
{
	if (HitProxy)
	{
		HitProxyId = HitProxy->Id;
	}
	else
	{
		HitProxyId = FHitProxyId();
	}
}

void FSimpleElementCollector::DrawSprite(
	const FVector& Position,
	float SizeX,
	float SizeY,
	const FTexture* Sprite,
	const FLinearColor& Color,
	uint8 DepthPriorityGroup,
	float U,
	float UL,
	float V,
	float VL,
	uint8 BlendMode
	)
{
	FBatchedElements& Elements = DepthPriorityGroup == SDPG_World ? BatchedElements : TopBatchedElements;

	Elements.AddSprite(
		Position,
		SizeX,
		SizeY,
		Sprite,
		Color,
		HitProxyId,
		U,
		UL,
		V,
		VL,
		BlendMode
		);
}

void FSimpleElementCollector::DrawLine(
	const FVector& Start,
	const FVector& End,
	const FLinearColor& Color,
	uint8 DepthPriorityGroup,
	float Thickness/* = 0.0f*/,
	float DepthBias/* = 0.0f*/,
	bool bScreenSpace/* = false*/
	)
{
	FBatchedElements& Elements = DepthPriorityGroup == SDPG_World ? BatchedElements : TopBatchedElements;

	Elements.AddLine(
		Start,
		End,
		Color,
		HitProxyId,
		Thickness,
		DepthBias,
		bScreenSpace
		);
}

void FSimpleElementCollector::DrawPoint(
	const FVector& Position,
	const FLinearColor& Color,
	float PointSize,
	uint8 DepthPriorityGroup
	)
{
	FBatchedElements& Elements = DepthPriorityGroup == SDPG_World ? BatchedElements : TopBatchedElements;

	Elements.AddPoint(
		Position,
		PointSize,
		Color,
		HitProxyId
		);
}

void FSimpleElementCollector::RegisterDynamicResource(FDynamicPrimitiveResource* DynamicResource)
{
	// Add the dynamic resource to the list of resources to cleanup on destruction.
	DynamicResources.Add(DynamicResource);

	// Initialize the dynamic resource immediately.
	DynamicResource->InitPrimitiveResource();
}

void FSimpleElementCollector::DrawBatchedElements(FRHICommandList& RHICmdList, const FMeshPassProcessorRenderState& DrawRenderState, const FSceneView& InView, EBlendModeFilter::Type Filter, ESceneDepthPriorityGroup DepthPriorityGroup) const
{
	// Mobile HDR does not execute post process, so does not need to render flipped
	const bool bNeedToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(InView.GetShaderPlatform()) && !bIsMobileHDR;

	const FBatchedElements& Elements = DepthPriorityGroup == SDPG_World ? BatchedElements : TopBatchedElements;

	// Draw the batched elements.
	Elements.Draw(
		RHICmdList,
		DrawRenderState,
		InView.GetFeatureLevel(),
		bNeedToSwitchVerticalAxis,
		InView,
		InView.Family->EngineShowFlags.HitProxies,
		1.0f,
		Filter
		);
}

FMeshBatchAndRelevance::FMeshBatchAndRelevance(const FMeshBatch& InMesh, const FPrimitiveSceneProxy* InPrimitiveSceneProxy, ERHIFeatureLevel::Type FeatureLevel) :
	Mesh(&InMesh),
	PrimitiveSceneProxy(InPrimitiveSceneProxy)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMeshBatchAndRelevance);
	const FMaterial* Material = InMesh.MaterialRenderProxy->GetMaterial(FeatureLevel);
	EBlendMode BlendMode = Material->GetBlendMode();
	bHasOpaqueMaterial = (BlendMode == BLEND_Opaque);
	bHasMaskedMaterial = (BlendMode == BLEND_Masked);
	bRenderInMainPass = PrimitiveSceneProxy->ShouldRenderInMainPass();
}

static TAutoConsoleVariable<int32> CVarUseParallelGetDynamicMeshElementsTasks(
	TEXT("r.UseParallelGetDynamicMeshElementsTasks"),
	0,
	TEXT("If > 0, and if FApp::ShouldUseThreadingForPerformance(), then parts of GetDynamicMeshElements will be done in parallel."));

FMeshElementCollector::FMeshElementCollector(ERHIFeatureLevel::Type InFeatureLevel) :
	PrimitiveSceneProxy(NULL),
	DynamicIndexBuffer(nullptr),
	DynamicVertexBuffer(nullptr),
	DynamicReadBuffer(nullptr),
	FeatureLevel(InFeatureLevel),
	bUseAsyncTasks(FApp::ShouldUseThreadingForPerformance() && CVarUseParallelGetDynamicMeshElementsTasks.GetValueOnAnyThread() > 0)
{	
}


void FMeshElementCollector::ProcessTasks()
{
	check(IsInRenderingThread());
	check(!ParallelTasks.Num() || bUseAsyncTasks);

	if (ParallelTasks.Num())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FMeshElementCollector_ProcessTasks);
		TArray<TFunction<void()>*, SceneRenderingAllocator>& LocalParallelTasks(ParallelTasks);
		ParallelFor(ParallelTasks.Num(), 
			[&LocalParallelTasks](int32 Index)
			{
				TFunction<void()>* Func = LocalParallelTasks[Index];
				(*Func)();
				Func->~TFunction<void()>();
			}
			);
		ParallelTasks.Empty();
	}
}

void FMeshElementCollector::AddMesh(int32 ViewIndex, FMeshBatch& MeshBatch)
{
	DEFINE_LOG_CATEGORY_STATIC(FMeshElementCollector_AddMesh, Warning, All);

	//checkSlow(MeshBatch.GetNumPrimitives() > 0);
	checkSlow(MeshBatch.VertexFactory && MeshBatch.MaterialRenderProxy);
	checkSlow(PrimitiveSceneProxy);

	PrimitiveSceneProxy->VerifyUsedMaterial(MeshBatch.MaterialRenderProxy);

	if (MeshBatch.bCanApplyViewModeOverrides)
	{
		FSceneView* View = Views[ViewIndex];

		ApplyViewModeOverrides(
			ViewIndex,
			View->Family->EngineShowFlags,
			View->GetFeatureLevel(),
			PrimitiveSceneProxy,
			MeshBatch.bUseWireframeSelectionColoring,
			MeshBatch,
			*this);
	}

	MeshBatch.PreparePrimitiveUniformBuffer(PrimitiveSceneProxy, FeatureLevel);

	for (int32 Index = 0; Index < MeshBatch.Elements.Num(); ++Index)
	{
		UE_CLOG(MeshBatch.Elements[Index].IndexBuffer && !MeshBatch.Elements[Index].IndexBuffer->IndexBufferRHI, FMeshElementCollector_AddMesh, Fatal,
			TEXT("FMeshElementCollector::AddMesh - On MeshBatchElement %d, Material '%s', index buffer object has null RHI resource"),
			Index, MeshBatch.MaterialRenderProxy ? *MeshBatch.MaterialRenderProxy->GetFriendlyName() : TEXT("null"));
	}

	// If we are maintaining primitive scene data on the GPU, copy the primitive uniform buffer data to a unified array so it can be uploaded later
	if (UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel) && MeshBatch.VertexFactory->GetPrimitiveIdStreamIndex(EVertexInputStreamType::Default) >= 0)
	{
		for (int32 Index = 0; Index < MeshBatch.Elements.Num(); ++Index)
		{
			const TUniformBuffer<FPrimitiveUniformShaderParameters>* PrimitiveUniformBufferResource = MeshBatch.Elements[Index].PrimitiveUniformBufferResource;

			if (PrimitiveUniformBufferResource)
			{
				TArray<FPrimitiveUniformShaderParameters>* DynamicPrimitiveShaderData = DynamicPrimitiveShaderDataPerView[ViewIndex];

				const int32 DataIndex = DynamicPrimitiveShaderData->AddUninitialized(1);
				MeshBatch.Elements[Index].PrimitiveIdMode = PrimID_DynamicPrimitiveShaderData;
				MeshBatch.Elements[Index].DynamicPrimitiveShaderDataIndex = DataIndex;
				FPlatformMemory::Memcpy(&(*DynamicPrimitiveShaderData)[DataIndex], PrimitiveUniformBufferResource->GetContents(), sizeof(FPrimitiveUniformShaderParameters));
			}
		}
	}

	MeshBatch.MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(Views[ViewIndex]->GetFeatureLevel());

	MeshBatch.MeshIdInPrimitive = MeshIdInPrimitivePerView[ViewIndex];
	++MeshIdInPrimitivePerView[ViewIndex];

	NumMeshBatchElementsPerView[ViewIndex] += MeshBatch.Elements.Num();

	TArray<FMeshBatchAndRelevance,SceneRenderingAllocator>& ViewMeshBatches = *MeshBatches[ViewIndex];
	new (ViewMeshBatches) FMeshBatchAndRelevance(MeshBatch, PrimitiveSceneProxy, FeatureLevel);	
}

void FDynamicPrimitiveUniformBuffer::Set(
	const FMatrix& LocalToWorld,
	const FMatrix& PreviousLocalToWorld,
	const FBoxSphereBounds& WorldBounds,
	const FBoxSphereBounds& LocalBounds,
	const FBoxSphereBounds& PreSkinnedLocalBounds,
	bool bReceivesDecals,
	bool bHasPrecomputedVolumetricLightmap,
	bool bDrawsVelocity,
	bool bOutputVelocity)
{
	check(IsInRenderingThread());
	UniformBuffer.SetContents(
		GetPrimitiveUniformShaderParameters(LocalToWorld, PreviousLocalToWorld, WorldBounds.Origin, WorldBounds, LocalBounds, PreSkinnedLocalBounds, bReceivesDecals, false, false, false, bHasPrecomputedVolumetricLightmap, bDrawsVelocity, GetDefaultLightingChannelMask(), 1.0f, INDEX_NONE, INDEX_NONE, bOutputVelocity, nullptr));
	UniformBuffer.InitResource();
}

void FDynamicPrimitiveUniformBuffer::Set(
	const FMatrix& LocalToWorld,
	const FMatrix& PreviousLocalToWorld,
	const FBoxSphereBounds& WorldBounds,
	const FBoxSphereBounds& LocalBounds,
	bool bReceivesDecals,
	bool bHasPrecomputedVolumetricLightmap,
	bool bDrawsVelocity,
	bool bOutputVelocity)
{
	Set(LocalToWorld, PreviousLocalToWorld, WorldBounds, LocalBounds, LocalBounds, bReceivesDecals, bHasPrecomputedVolumetricLightmap, bDrawsVelocity, bOutputVelocity);
}

FLightMapInteraction FLightMapInteraction::Texture(
	const class ULightMapTexture2D* const* InTextures,
	const ULightMapTexture2D* InSkyOcclusionTexture,
	const ULightMapTexture2D* InAOMaterialMaskTexture,
	const FVector4* InCoefficientScales,
	const FVector4* InCoefficientAdds,
	const FVector2D& InCoordinateScale,
	const FVector2D& InCoordinateBias,
	bool bUseHighQualityLightMaps)
{
	FLightMapInteraction Result;
	Result.Type = LMIT_Texture;

#if ALLOW_LQ_LIGHTMAPS && ALLOW_HQ_LIGHTMAPS
	// however, if simple and directional are allowed, then we must use the value passed in,
	// and then cache the number as well
	Result.bAllowHighQualityLightMaps = bUseHighQualityLightMaps;
	if (bUseHighQualityLightMaps)
	{
		Result.NumLightmapCoefficients = NUM_HQ_LIGHTMAP_COEF;
	}
	else
	{
		Result.NumLightmapCoefficients = NUM_LQ_LIGHTMAP_COEF;
	}
#endif

	//copy over the appropriate textures and scales
	if (bUseHighQualityLightMaps)
	{
#if ALLOW_HQ_LIGHTMAPS
		Result.HighQualityTexture = InTextures[0];
		Result.SkyOcclusionTexture = InSkyOcclusionTexture;
		Result.AOMaterialMaskTexture = InAOMaterialMaskTexture;
		for(uint32 CoefficientIndex = 0;CoefficientIndex < NUM_HQ_LIGHTMAP_COEF;CoefficientIndex++)
		{
			Result.HighQualityCoefficientScales[CoefficientIndex] = InCoefficientScales[CoefficientIndex];
			Result.HighQualityCoefficientAdds[CoefficientIndex] = InCoefficientAdds[CoefficientIndex];
		}
#endif
	}

	// NOTE: In PC editor we cache both Simple and Directional textures as we may need to dynamically switch between them
	if( GIsEditor || !bUseHighQualityLightMaps )
	{
#if ALLOW_LQ_LIGHTMAPS
		Result.LowQualityTexture = InTextures[1];
		for(uint32 CoefficientIndex = 0;CoefficientIndex < NUM_LQ_LIGHTMAP_COEF;CoefficientIndex++)
		{
			Result.LowQualityCoefficientScales[CoefficientIndex] = InCoefficientScales[ LQ_LIGHTMAP_COEF_INDEX + CoefficientIndex ];
			Result.LowQualityCoefficientAdds[CoefficientIndex] = InCoefficientAdds[ LQ_LIGHTMAP_COEF_INDEX + CoefficientIndex ];
		}
#endif
	}

	Result.CoordinateScale = InCoordinateScale;
	Result.CoordinateBias = InCoordinateBias;
	return Result;
}

FLightMapInteraction FLightMapInteraction::InitVirtualTexture(
	const ULightMapVirtualTexture2D* VirtualTexture,
	const FVector4* InCoefficientScales,
	const FVector4* InCoefficientAdds,
	const FVector2D& InCoordinateScale,
	const FVector2D& InCoordinateBias,
	bool bAllowHighQualityLightMaps)
{
	FLightMapInteraction Result;
	Result.Type = LMIT_Texture;
	check(bAllowHighQualityLightMaps == true);

#if ALLOW_LQ_LIGHTMAPS && ALLOW_HQ_LIGHTMAPS
	// however, if simple and directional are allowed, then we must use the value passed in,
	// and then cache the number as well
	Result.bAllowHighQualityLightMaps = bAllowHighQualityLightMaps;
	if (bAllowHighQualityLightMaps)
	{
		Result.NumLightmapCoefficients = NUM_HQ_LIGHTMAP_COEF;
	}
	else
	{
		Result.NumLightmapCoefficients = NUM_LQ_LIGHTMAP_COEF;
	}
#endif

	//copy over the appropriate textures and scales
	if (bAllowHighQualityLightMaps)
	{
#if ALLOW_HQ_LIGHTMAPS
		Result.VirtualTexture = VirtualTexture;
		for (uint32 CoefficientIndex = 0; CoefficientIndex < NUM_HQ_LIGHTMAP_COEF; CoefficientIndex++)
		{
			Result.HighQualityCoefficientScales[CoefficientIndex] = InCoefficientScales[CoefficientIndex];
			Result.HighQualityCoefficientAdds[CoefficientIndex] = InCoefficientAdds[CoefficientIndex];
		}
#endif
	}

	// NOTE: In PC editor we cache both Simple and Directional textures as we may need to dynamically switch between them
	if (GIsEditor || !bAllowHighQualityLightMaps)
	{
#if ALLOW_LQ_LIGHTMAPS
		for (uint32 CoefficientIndex = 0; CoefficientIndex < NUM_LQ_LIGHTMAP_COEF; CoefficientIndex++)
		{
			Result.LowQualityCoefficientScales[CoefficientIndex] = InCoefficientScales[LQ_LIGHTMAP_COEF_INDEX + CoefficientIndex];
			Result.LowQualityCoefficientAdds[CoefficientIndex] = InCoefficientAdds[LQ_LIGHTMAP_COEF_INDEX + CoefficientIndex];
		}
#endif
	}

	Result.CoordinateScale = InCoordinateScale;
	Result.CoordinateBias = InCoordinateBias;
	return Result;
}

float ComputeBoundsScreenRadiusSquared(const FVector4& BoundsOrigin, const float SphereRadius, const FVector4& ViewOrigin, const FMatrix& ProjMatrix)
{
	// ignore perspective foreshortening for orthographic projections
	const float DistSqr = FVector::DistSquared(BoundsOrigin, ViewOrigin) * ProjMatrix.M[2][3];

	// Get projection multiple accounting for view scaling.
	const float ScreenMultiple = FMath::Max(0.5f * ProjMatrix.M[0][0], 0.5f * ProjMatrix.M[1][1]);

	// Calculate screen-space projected radius
	return FMath::Square(ScreenMultiple * SphereRadius) / FMath::Max(1.0f, DistSqr);
}

/** Runtime comparison version of ComputeTemporalLODBoundsScreenSize that avoids a square root */
static float ComputeTemporalLODBoundsScreenRadiusSquared(const FVector& Origin, const float SphereRadius, const FSceneView& View, int32 SampleIndex)
{
	return ComputeBoundsScreenRadiusSquared(Origin, SphereRadius, View.GetTemporalLODOrigin(SampleIndex), View.ViewMatrices.GetProjectionMatrix());
}

float ComputeBoundsScreenRadiusSquared(const FVector4& Origin, const float SphereRadius, const FSceneView& View)
{
	return ComputeBoundsScreenRadiusSquared(Origin, SphereRadius, View.ViewMatrices.GetViewOrigin(), View.ViewMatrices.GetProjectionMatrix());
}

float ComputeBoundsScreenSize( const FVector4& Origin, const float SphereRadius, const FSceneView& View )
{
	return ComputeBoundsScreenSize(Origin, SphereRadius, View.ViewMatrices.GetViewOrigin(), View.ViewMatrices.GetProjectionMatrix());
}

float ComputeTemporalLODBoundsScreenSize( const FVector& Origin, const float SphereRadius, const FSceneView& View, int32 SampleIndex )
{
	return ComputeBoundsScreenSize(Origin, SphereRadius, View.GetTemporalLODOrigin(SampleIndex), View.ViewMatrices.GetProjectionMatrix());
}

float ComputeBoundsScreenSize(const FVector4& BoundsOrigin, const float SphereRadius, const FVector4& ViewOrigin, const FMatrix& ProjMatrix)
{
	const float Dist = FVector::Dist(BoundsOrigin, ViewOrigin);

	// Get projection multiple accounting for view scaling.
	const float ScreenMultiple = FMath::Max(0.5f * ProjMatrix.M[0][0], 0.5f * ProjMatrix.M[1][1]);

	// Calculate screen-space projected radius
	const float ScreenRadius = ScreenMultiple * SphereRadius / FMath::Max(1.0f, Dist);

	// For clarity, we end up comparing the diameter
	return ScreenRadius * 2.0f;
}

float ComputeBoundsDrawDistance(const float ScreenSize, const float SphereRadius, const FMatrix& ProjMatrix)
{
	// Get projection multiple accounting for view scaling.
	const float ScreenMultiple = FMath::Max(0.5f * ProjMatrix.M[0][0], 0.5f * ProjMatrix.M[1][1]);

	// ScreenSize is the projected diameter, so halve it
	const float ScreenRadius = FMath::Max(SMALL_NUMBER, ScreenSize * 0.5f);

	// Invert the calcs in ComputeBoundsScreenSize
	return (ScreenMultiple * SphereRadius) / ScreenRadius;
}

int8 ComputeTemporalStaticMeshLOD( const FStaticMeshRenderData* RenderData, const FVector4& Origin, const float SphereRadius, const FSceneView& View, int32 MinLOD, float FactorScale, int32 SampleIndex )
{
	const int32 NumLODs = MAX_STATIC_MESH_LODS;

	const float ScreenRadiusSquared = ComputeTemporalLODBoundsScreenRadiusSquared(Origin, SphereRadius, View, SampleIndex) * FactorScale * FactorScale * View.LODDistanceFactor * View.LODDistanceFactor;

	// Walk backwards and return the first matching LOD
	for(int32 LODIndex = NumLODs - 1 ; LODIndex >= 0 ; --LODIndex)
	{
		if(FMath::Square(RenderData->ScreenSize[LODIndex].GetValueForFeatureLevel(View.GetFeatureLevel()) * 0.5f) > ScreenRadiusSquared)
		{
			return FMath::Max(LODIndex, MinLOD);
		}
	}

	return MinLOD;
}

// Ensure we always use the left eye when selecting lods to avoid divergent selections in stereo
const FSceneView& GetLODView(const FSceneView& InView)
{
	if (IStereoRendering::IsASecondaryView(InView) && InView.Family)
	{
		return *InView.Family->Views[0];
	}
	else
	{
		return InView;
	}
}

int8 ComputeStaticMeshLOD( const FStaticMeshRenderData* RenderData, const FVector4& Origin, const float SphereRadius, const FSceneView& View, int32 MinLOD, float FactorScale )
{
	if (RenderData)
	{
		const int32 NumLODs = MAX_STATIC_MESH_LODS;
		const FSceneView& LODView = GetLODView(View);
		const float ScreenRadiusSquared = ComputeBoundsScreenRadiusSquared(Origin, SphereRadius, LODView) * FactorScale * FactorScale * LODView.LODDistanceFactor * LODView.LODDistanceFactor;

		// Walk backwards and return the first matching LOD
		for (int32 LODIndex = NumLODs - 1; LODIndex >= 0; --LODIndex)
		{
			if (FMath::Square(RenderData->ScreenSize[LODIndex].GetValueForFeatureLevel(View.GetFeatureLevel()) * 0.5f) > ScreenRadiusSquared)
			{
				return FMath::Max(LODIndex, MinLOD);
			}
		}
	}

	return MinLOD;
}

FLODMask ComputeLODForMeshes(const TArray<class FStaticMeshBatchRelevance>& StaticMeshRelevances, const FSceneView& View, const FVector4& Origin, float SphereRadius, int32 ForcedLODLevel, float& OutScreenRadiusSquared, int8 CurFirstLODIdx, float ScreenSizeScale, bool bDitheredLODTransition)
{
	FLODMask LODToRender;
	const FSceneView& LODView = GetLODView(View);

	const int32 NumMeshes = StaticMeshRelevances.Num();

	// Handle forced LOD level first
	if (ForcedLODLevel >= 0)
	{
		OutScreenRadiusSquared = 0.0f;

		int32 MinLOD = 127, MaxLOD = 0;
		for (int32 MeshIndex = 0; MeshIndex < StaticMeshRelevances.Num(); ++MeshIndex)
		{
			const FStaticMeshBatchRelevance& Mesh = StaticMeshRelevances[MeshIndex];
			if (Mesh.ScreenSize > 0.0f)
			{
				MinLOD = FMath::Min(MinLOD, (int32)Mesh.LODIndex);
				MaxLOD = FMath::Max(MaxLOD, (int32)Mesh.LODIndex);
			}
		}
		MinLOD = FMath::Max(MinLOD, (int32)CurFirstLODIdx);
		LODToRender.SetLOD(FMath::Clamp(ForcedLODLevel, MinLOD, MaxLOD));
	}
	else if (LODView.Family->EngineShowFlags.LOD && NumMeshes)
	{
		if (bDitheredLODTransition && StaticMeshRelevances[0].bDitheredLODTransition)
		{
			for (int32 SampleIndex = 0; SampleIndex < 2; SampleIndex++)
			{
				int32 MinLODFound = INT_MAX;
				bool bFoundLOD = false;
				OutScreenRadiusSquared = ComputeTemporalLODBoundsScreenRadiusSquared(Origin, SphereRadius, LODView, SampleIndex);

				for (int32 MeshIndex = NumMeshes - 1; MeshIndex >= 0; --MeshIndex)
				{
					const FStaticMeshBatchRelevance& Mesh = StaticMeshRelevances[MeshIndex];
					if (Mesh.ScreenSize > 0.0f)
					{
						float MeshScreenSize = Mesh.ScreenSize * ScreenSizeScale;

						if (FMath::Square(MeshScreenSize * 0.5f) >= OutScreenRadiusSquared)
						{
							LODToRender.SetLODSample(Mesh.LODIndex, SampleIndex);
							bFoundLOD = true;
							break;
						}

						MinLODFound = FMath::Min<int32>(MinLODFound, Mesh.LODIndex);
					}
				}
				// If no LOD was found matching the screen size, use the lowest in the array instead of LOD 0, to handle non-zero MinLOD
				if (!bFoundLOD)
				{
					LODToRender.SetLODSample(MinLODFound, SampleIndex);
				}
			}
		}
		else
		{
			int32 MinLODFound = INT_MAX;
			bool bFoundLOD = false;
			OutScreenRadiusSquared = ComputeBoundsScreenRadiusSquared(Origin, SphereRadius, LODView);

			for (int32 MeshIndex = NumMeshes - 1; MeshIndex >= 0; --MeshIndex)
			{
				const FStaticMeshBatchRelevance& Mesh = StaticMeshRelevances[MeshIndex];

				float MeshScreenSize = Mesh.ScreenSize * ScreenSizeScale;

				if (FMath::Square(MeshScreenSize * 0.5f) >= OutScreenRadiusSquared)
				{
					LODToRender.SetLOD(Mesh.LODIndex);
					bFoundLOD = true;
					break;
				}

				MinLODFound = FMath::Min<int32>(MinLODFound, Mesh.LODIndex);
			}
			// If no LOD was found matching the screen size, use the lowest in the array instead of LOD 0, to handle non-zero MinLOD
			if (!bFoundLOD)
			{
				LODToRender.SetLOD(MinLODFound);
			}
		}
		LODToRender.ClampToFirstLOD(CurFirstLODIdx);
	}
	return LODToRender;
}

FMobileDirectionalLightShaderParameters::FMobileDirectionalLightShaderParameters()
{
	FMemory::Memzero(*this);

	// light, default to black
	DirectionalLightColor = FLinearColor::Black;
	DirectionalLightDirectionAndShadowTransition = FVector4(EForceInit::ForceInitToZero);

	// white texture should act like a shadowmap cleared to the farplane.
	DirectionalLightShadowTexture = GWhiteTexture->TextureRHI;
	DirectionalLightShadowSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	DirectionalLightShadowSize = FVector4(EForceInit::ForceInitToZero);
	DirectionalLightDistanceFadeMADAndSpecularScale = FVector4(EForceInit::ForceInitToZero);
	for (int32 i = 0; i < MAX_MOBILE_SHADOWCASCADES; ++i)
	{
		DirectionalLightScreenToShadow[i].SetIdentity();
		DirectionalLightShadowDistances[i] = 0.0f;
	}
}

FViewUniformShaderParameters::FViewUniformShaderParameters()
{
	FMemory::Memzero(*this);

	FRHITexture* BlackVolume = (GBlackVolumeTexture &&  GBlackVolumeTexture->TextureRHI) ? GBlackVolumeTexture->TextureRHI : GBlackTexture->TextureRHI; // for es2, this might need to be 2d
	FRHITexture* BlackUintVolume = (GBlackUintVolumeTexture &&  GBlackUintVolumeTexture->TextureRHI) ? GBlackUintVolumeTexture->TextureRHI : GBlackTexture->TextureRHI; // for es2, this might need to be 2d
	check(GBlackVolumeTexture);

	MaterialTextureBilinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	MaterialTextureBilinearWrapedSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	VolumetricLightmapIndirectionTexture = BlackUintVolume;
	VolumetricLightmapBrickAmbientVector = BlackVolume;
	VolumetricLightmapBrickSHCoefficients0 = BlackVolume;
	VolumetricLightmapBrickSHCoefficients1 = BlackVolume;
	VolumetricLightmapBrickSHCoefficients2 = BlackVolume;
	VolumetricLightmapBrickSHCoefficients3 = BlackVolume;
	VolumetricLightmapBrickSHCoefficients4 = BlackVolume;
	VolumetricLightmapBrickSHCoefficients5 = BlackVolume;
	SkyBentNormalBrickTexture = BlackVolume;
	DirectionalLightShadowingBrickTexture = BlackVolume;

	VolumetricLightmapBrickAmbientVectorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	VolumetricLightmapTextureSampler0 = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	VolumetricLightmapTextureSampler1 = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	VolumetricLightmapTextureSampler2 = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	VolumetricLightmapTextureSampler3 = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	VolumetricLightmapTextureSampler4 = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	VolumetricLightmapTextureSampler5 = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	SkyBentNormalTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	DirectionalLightShadowingTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	AtmosphereTransmittanceTexture = GWhiteTexture->TextureRHI;
	AtmosphereTransmittanceTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	AtmosphereIrradianceTexture = GWhiteTexture->TextureRHI;
	AtmosphereIrradianceTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	AtmosphereInscatterTexture = BlackVolume;
	AtmosphereInscatterTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

	PerlinNoiseGradientTexture = GWhiteTexture->TextureRHI;
	PerlinNoiseGradientTextureSampler = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	PerlinNoise3DTexture = BlackVolume;
	PerlinNoise3DTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	SobolSamplingTexture = GWhiteTexture->TextureRHI;

	GlobalDistanceFieldTexture0 = BlackVolume;
	GlobalDistanceFieldSampler0 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	GlobalDistanceFieldTexture1 = BlackVolume;
	GlobalDistanceFieldSampler1 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	GlobalDistanceFieldTexture2 = BlackVolume;
	GlobalDistanceFieldSampler2 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	GlobalDistanceFieldTexture3 = BlackVolume;
	GlobalDistanceFieldSampler3 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	SharedPointWrappedSampler = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	SharedPointClampedSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	SharedBilinearWrappedSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	SharedBilinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	SharedTrilinearWrappedSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	SharedTrilinearClampedSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	PreIntegratedBRDF = GWhiteTexture->TextureRHI;
	PreIntegratedBRDFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	TransmittanceLutTexture = GWhiteTexture->TextureRHI;
	TransmittanceLutTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

	SkyViewLutTexture = GBlackTexture->TextureRHI;
	SkyViewLutTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

	DistantSkyLightLutTexture = GBlackTexture->TextureRHI;
	DistantSkyLightLutTextureSampler = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap>::GetRHI();

	CameraAerialPerspectiveVolume = GBlackAlpha1VolumeTexture->TextureRHI;
	CameraAerialPerspectiveVolumeSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

	PrimitiveSceneDataTexture = OrBlack2DIfNull(GIdentityPrimitiveBuffer.PrimitiveSceneDataTextureRHI);
	PrimitiveSceneData = GIdentityPrimitiveBuffer.PrimitiveSceneDataBufferSRV;
	LightmapSceneData = GIdentityPrimitiveBuffer.LightmapSceneDataBufferSRV;

	//this can be deleted once sm4 support is removed.
	if (!PrimitiveSceneData)
	{
		PrimitiveSceneData = GBlackTextureWithSRV->ShaderResourceViewRHI;
	}
	if (!LightmapSceneData)
	{
		LightmapSceneData = GBlackTextureWithSRV->ShaderResourceViewRHI;
	}
}

FInstancedViewUniformShaderParameters::FInstancedViewUniformShaderParameters()
{
	FMemory::Memzero(*this);
}

void FSharedSamplerState::InitRHI()
{
	const float MipMapBias = UTexture2D::GetGlobalMipMapLODBias();

	FSamplerStateInitializerRHI SamplerStateInitializer
	(
	(ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter(TEXTUREGROUP_World),
		bWrap ? AM_Wrap : AM_Clamp,
		bWrap ? AM_Wrap : AM_Clamp,
		bWrap ? AM_Wrap : AM_Clamp,
		MipMapBias
	);
	SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
}

FSharedSamplerState* Wrap_WorldGroupSettings = NULL;
FSharedSamplerState* Clamp_WorldGroupSettings = NULL;

void InitializeSharedSamplerStates()
{
	if (!Wrap_WorldGroupSettings)
	{
		Wrap_WorldGroupSettings = new FSharedSamplerState(true);
		Clamp_WorldGroupSettings = new FSharedSamplerState(false);
		BeginInitResource(Wrap_WorldGroupSettings);
		BeginInitResource(Clamp_WorldGroupSettings);
	}
}

void FLightCacheInterface::CreatePrecomputedLightingUniformBuffer_RenderingThread(ERHIFeatureLevel::Type FeatureLevel)
{
	if (LightMap || ShadowMap)
	{
		FPrecomputedLightingUniformParameters Parameters;
		GetPrecomputedLightingParameters(FeatureLevel, Parameters, this);
		if (PrecomputedLightingUniformBuffer)
		{
			// Don't recreate the buffer if it already exists
			RHIUpdateUniformBuffer(PrecomputedLightingUniformBuffer, &Parameters);
		}
		else
		{
			PrecomputedLightingUniformBuffer = FPrecomputedLightingUniformParameters::CreateUniformBuffer(Parameters, UniformBuffer_MultiFrame);
		}
	}
}

bool FLightCacheInterface::GetVirtualTextureLightmapProducer(ERHIFeatureLevel::Type FeatureLevel, FVirtualTextureProducerHandle& OutProducerHandle)
{
	const FLightMapInteraction LightMapInteraction = GetLightMapInteraction(FeatureLevel);
	if (LightMapInteraction.GetType() == LMIT_Texture)
	{
		const ULightMapVirtualTexture2D* VirtualTexture = LightMapInteraction.GetVirtualTexture();
		if (VirtualTexture)
		{
			FVirtualTexture2DResource* Resource = (FVirtualTexture2DResource*)VirtualTexture->Resource;
			OutProducerHandle = Resource->GetProducerHandle();
			return true;
		}
	}
	return false;
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLightmapResourceClusterShaderParameters, "LightmapResourceCluster");

void GetLightmapClusterResourceParameters(
	ERHIFeatureLevel::Type FeatureLevel, 
	const FLightmapClusterResourceInput& Input,
	IAllocatedVirtualTexture* AllocatedVT,
	FLightmapResourceClusterShaderParameters& Parameters)
{
	const bool bAllowHighQualityLightMaps = AllowHighQualityLightmaps(FeatureLevel);

	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTexturedLightmaps"));
	const bool bUseVirtualTextures = bAllowHighQualityLightMaps && (CVar->GetValueOnRenderThread() != 0) && UseVirtualTexturing(FeatureLevel);

	if (bUseVirtualTextures)
	{
		// this is sometimes called with NULL input to initialize default buffer
		const ULightMapVirtualTexture2D* VirtualTexture = Input.LightMapVirtualTexture;
		if (VirtualTexture && AllocatedVT)
		{
			// Bind VT here
			Parameters.LightMapTexture = AllocatedVT->GetPhysicalTexture((uint32)ELightMapVirtualTextureType::HqLayer0);
			Parameters.LightMapTexture_1 = AllocatedVT->GetPhysicalTexture((uint32)ELightMapVirtualTextureType::HqLayer1);

			if (VirtualTexture->HasLayerForType(ELightMapVirtualTextureType::SkyOcclusion))
			{
				Parameters.SkyOcclusionTexture = AllocatedVT->GetPhysicalTexture((uint32)ELightMapVirtualTextureType::SkyOcclusion);
			}
			else
			{
				Parameters.SkyOcclusionTexture = GWhiteTexture->TextureRHI;
			}

			if (VirtualTexture->HasLayerForType(ELightMapVirtualTextureType::AOMaterialMask))
			{
				Parameters.AOMaterialMaskTexture = AllocatedVT->GetPhysicalTexture((uint32)ELightMapVirtualTextureType::AOMaterialMask);
			}
			else
			{
				Parameters.AOMaterialMaskTexture = GBlackTexture->TextureRHI;
			}

			if (VirtualTexture->HasLayerForType(ELightMapVirtualTextureType::ShadowMask))
			{
				Parameters.StaticShadowTexture = AllocatedVT->GetPhysicalTexture((uint32)ELightMapVirtualTextureType::ShadowMask);
			}
			else
			{
				Parameters.StaticShadowTexture = GWhiteTexture->TextureRHI;
			}

			FRHITexture* PageTable0 = AllocatedVT->GetPageTableTexture(0u);
			Parameters.LightmapVirtualTexturePageTable0 = PageTable0;
			if (AllocatedVT->GetNumPageTableTextures() > 1u)
			{
				check(AllocatedVT->GetNumPageTableTextures() == 2u);
				Parameters.LightmapVirtualTexturePageTable1 = AllocatedVT->GetPageTableTexture(1u);
			}
			else
			{
				Parameters.LightmapVirtualTexturePageTable1 = PageTable0;
			}

			const uint32 MaxAniso = 4;
			Parameters.LightMapSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, MaxAniso>::GetRHI();
			Parameters.SkyOcclusionSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, MaxAniso>::GetRHI();
			Parameters.AOMaterialMaskSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, MaxAniso>::GetRHI();
			Parameters.StaticShadowTextureSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, MaxAniso>::GetRHI();
		}
		else
		{
			Parameters.LightMapTexture = GBlackTexture->TextureRHI;
			Parameters.LightMapTexture_1 = GBlackTexture->TextureRHI;
			Parameters.SkyOcclusionTexture = GWhiteTexture->TextureRHI;
			Parameters.AOMaterialMaskTexture = GBlackTexture->TextureRHI;
			Parameters.StaticShadowTexture = GWhiteTexture->TextureRHI;
			Parameters.LightmapVirtualTexturePageTable0 = GBlackTexture->TextureRHI;
			Parameters.LightmapVirtualTexturePageTable1 = GBlackTexture->TextureRHI;
			Parameters.LightMapSampler = GBlackTexture->SamplerStateRHI;
			Parameters.SkyOcclusionSampler = GWhiteTexture->SamplerStateRHI;
			Parameters.AOMaterialMaskSampler = GBlackTexture->SamplerStateRHI;
			Parameters.StaticShadowTextureSampler = GWhiteTexture->SamplerStateRHI;
		}
	}
	else
	{
		const UTexture2D* LightMapTexture = Input.LightMapTextures[bAllowHighQualityLightMaps ? 0 : 1];

		Parameters.LightMapTexture = LightMapTexture ? LightMapTexture->TextureReference.TextureReferenceRHI.GetReference() : GBlackTexture->TextureRHI;
		Parameters.LightMapTexture_1 = GBlackTexture->TextureRHI;
		Parameters.SkyOcclusionTexture = Input.SkyOcclusionTexture ? Input.SkyOcclusionTexture->TextureReference.TextureReferenceRHI.GetReference() : GWhiteTexture->TextureRHI;
		Parameters.AOMaterialMaskTexture = Input.AOMaterialMaskTexture ? Input.AOMaterialMaskTexture->TextureReference.TextureReferenceRHI.GetReference() : GBlackTexture->TextureRHI;

		Parameters.LightMapSampler = (LightMapTexture && LightMapTexture->Resource) ? LightMapTexture->Resource->SamplerStateRHI : GBlackTexture->SamplerStateRHI;
		Parameters.SkyOcclusionSampler = (Input.SkyOcclusionTexture && Input.SkyOcclusionTexture->Resource) ? Input.SkyOcclusionTexture->Resource->SamplerStateRHI : GWhiteTexture->SamplerStateRHI;
		Parameters.AOMaterialMaskSampler = (Input.AOMaterialMaskTexture && Input.AOMaterialMaskTexture->Resource) ? Input.AOMaterialMaskTexture->Resource->SamplerStateRHI : GBlackTexture->SamplerStateRHI;

		Parameters.StaticShadowTexture = Input.ShadowMapTexture ? Input.ShadowMapTexture->TextureReference.TextureReferenceRHI.GetReference() : GWhiteTexture->TextureRHI;
		Parameters.StaticShadowTextureSampler = (Input.ShadowMapTexture && Input.ShadowMapTexture->Resource) ? Input.ShadowMapTexture->Resource->SamplerStateRHI : GWhiteTexture->SamplerStateRHI;

		Parameters.LightmapVirtualTexturePageTable0 = GBlackTexture->TextureRHI;
		Parameters.LightmapVirtualTexturePageTable1 = GBlackTexture->TextureRHI;
	}
}

void FDefaultLightmapResourceClusterUniformBuffer::InitDynamicRHI()
{
	FLightmapResourceClusterShaderParameters Parameters;
	GetLightmapClusterResourceParameters(GMaxRHIFeatureLevel, FLightmapClusterResourceInput(), nullptr, Parameters);
	SetContents(Parameters);
	Super::InitDynamicRHI();
}

/** Global uniform buffer containing the default precomputed lighting data. */
TGlobalResource< FDefaultLightmapResourceClusterUniformBuffer > GDefaultLightmapResourceClusterUniformBuffer;

FLightMapInteraction FLightCacheInterface::GetLightMapInteraction(ERHIFeatureLevel::Type InFeatureLevel) const
{
	if (bGlobalVolumeLightmap)
	{
		return FLightMapInteraction::GlobalVolume();
	}

	return LightMap ? LightMap->GetInteraction(InFeatureLevel) : FLightMapInteraction();
}

FShadowMapInteraction FLightCacheInterface::GetShadowMapInteraction(ERHIFeatureLevel::Type InFeatureLevel) const
{
	if (bGlobalVolumeLightmap)
	{
		return FShadowMapInteraction::GlobalVolume();
	}

	FShadowMapInteraction Interaction;
	if (LightMap)
	{
		// Lightmap gets the first chance to provide shadow interaction,
		// this is used if VT lightmaps are enabled, and shadowmap is packed into the same VT stack as other lightmap textures
		Interaction = LightMap->GetShadowInteraction(InFeatureLevel);
	}
	if (Interaction.GetType() == SMIT_None && ShadowMap)
	{
		Interaction = ShadowMap->GetInteraction();
	}

	return Interaction;
}

ELightInteractionType FLightCacheInterface::GetStaticInteraction(const FLightSceneProxy* LightSceneProxy, const TArray<FGuid>& IrrelevantLights) const
{
	if (bGlobalVolumeLightmap)
	{
		if (LightSceneProxy->HasStaticLighting())
		{
			return LIT_CachedLightMap;
		}
		else if (LightSceneProxy->HasStaticShadowing())
		{
			return LIT_CachedSignedDistanceFieldShadowMap2D;
		}
		else
		{
			return LIT_MAX;
		}
	}

	ELightInteractionType Ret = LIT_MAX;

	// Check if the light has static lighting or shadowing.
	if(LightSceneProxy->HasStaticShadowing())
	{
		const FGuid LightGuid = LightSceneProxy->GetLightGuid();

		if(IrrelevantLights.Contains(LightGuid))
		{
			Ret = LIT_CachedIrrelevant;
		}
		else if(LightMap && LightMap->ContainsLight(LightGuid))
		{
			Ret = LIT_CachedLightMap;
		}
		else if(ShadowMap && ShadowMap->ContainsLight(LightGuid))
		{
			Ret = LIT_CachedSignedDistanceFieldShadowMap2D;
		}
	}

	return Ret;
}

FReadOnlyCVARCache GReadOnlyCVARCache;

const FReadOnlyCVARCache& FReadOnlyCVARCache::Get()
{
	checkSlow(GReadOnlyCVARCache.bInitialized);
	return GReadOnlyCVARCache;
}

void FReadOnlyCVARCache::Init()
{
	UE_LOG(LogInit, Log, TEXT("Initializing FReadOnlyCVARCache"));
	
	static const auto CVarSupportAtmosphericFog = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportAtmosphericFog"));
	static const auto CVarSupportStationarySkylight = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportStationarySkylight"));
	static const auto CVarSupportLowQualityLightmaps = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportLowQualityLightmaps"));
	static const auto CVarSupportPointLightWholeSceneShadows = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportPointLightWholeSceneShadows"));
	static const auto CVarSupportAllShaderPermutations = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportAllShaderPermutations"));	
	static const auto CVarVertexFoggingForOpaque = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VertexFoggingForOpaque"));	
	static const auto CVarAllowStaticLighting = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	static const auto CVarSupportSkyAtmosphere = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportSkyAtmosphere"));

	static const auto CVarMobileAllowMovableDirectionalLights = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowMovableDirectionalLights"));
	static const auto CVarMobileEnableStaticAndCSMShadowReceivers = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.EnableStaticAndCSMShadowReceivers"));
	static const auto CVarMobileAllowDistanceFieldShadows = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowDistanceFieldShadows"));
	static const auto CVarMobileNumDynamicPointLights = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileNumDynamicPointLights"));
	static const auto CVarMobileDynamicPointLightsUseStaticBranch = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileDynamicPointLightsUseStaticBranch"));
	static const auto CVarMobileSkyLightPermutation = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.SkyLightPermutation"));

	const bool bForceAllPermutations = CVarSupportAllShaderPermutations && CVarSupportAllShaderPermutations->GetValueOnAnyThread() != 0;

	bEnableAtmosphericFog = !CVarSupportAtmosphericFog || CVarSupportAtmosphericFog->GetValueOnAnyThread() != 0 || bForceAllPermutations;
	bEnableStationarySkylight = !CVarSupportStationarySkylight || CVarSupportStationarySkylight->GetValueOnAnyThread() != 0 || bForceAllPermutations;
	bEnablePointLightShadows = !CVarSupportPointLightWholeSceneShadows || CVarSupportPointLightWholeSceneShadows->GetValueOnAnyThread() != 0 || bForceAllPermutations;
	bEnableLowQualityLightmaps = !CVarSupportLowQualityLightmaps || CVarSupportLowQualityLightmaps->GetValueOnAnyThread() != 0 || bForceAllPermutations;
	bAllowStaticLighting = CVarAllowStaticLighting->GetValueOnAnyThread() != 0;
	bSupportSkyAtmosphere = !CVarSupportSkyAtmosphere || CVarSupportSkyAtmosphere->GetValueOnAnyThread() != 0;

	// mobile
	bMobileAllowMovableDirectionalLights = CVarMobileAllowMovableDirectionalLights->GetValueOnAnyThread() != 0;
	bMobileAllowDistanceFieldShadows = CVarMobileAllowDistanceFieldShadows->GetValueOnAnyThread() != 0;
	bMobileEnableStaticAndCSMShadowReceivers = CVarMobileEnableStaticAndCSMShadowReceivers->GetValueOnAnyThread() != 0;
	NumMobileMovablePointLights = CVarMobileNumDynamicPointLights->GetValueOnAnyThread();
	bMobileMovablePointLightsUseStaticBranch = CVarMobileDynamicPointLightsUseStaticBranch->GetValueOnAnyThread() != 0;
	MobileSkyLightPermutation = CVarMobileSkyLightPermutation->GetValueOnAnyThread();

	const bool bShowMissmatchedLowQualityLightmapsWarning = (!bEnableLowQualityLightmaps) && (GEngine->bShouldGenerateLowQualityLightmaps_DEPRECATED);
	if ( bShowMissmatchedLowQualityLightmapsWarning )
	{
		UE_LOG(LogInit, Warning, TEXT("Mismatch between bShouldGenerateLowQualityLightmaps(%d) and r.SupportLowQualityLightmaps(%d), UEngine::bShouldGenerateLowQualityLightmaps has been deprecated please use r.SupportLowQualityLightmaps instead"), GEngine->bShouldGenerateLowQualityLightmaps_DEPRECATED, bEnableLowQualityLightmaps);
	}

	bInitialized = true;
}

void FMeshBatch::PreparePrimitiveUniformBuffer(const FPrimitiveSceneProxy* PrimitiveSceneProxy, ERHIFeatureLevel::Type FeatureLevel)
{
	const bool bVFSupportsPrimitiveIdStream = VertexFactory->GetType()->SupportsPrimitiveIdStream();
	checkf((PrimitiveSceneProxy->DoesVFRequirePrimitiveUniformBuffer() || bVFSupportsPrimitiveIdStream), TEXT("PrimitiveSceneProxy has bVFRequiresPrimitiveUniformBuffer disabled yet tried to draw with a vertex factory (%s) that did not support PrimitiveIdStream."), VertexFactory->GetType()->GetName());

	const bool bPrimitiveShaderDataComesFromSceneBuffer = VertexFactory->GetPrimitiveIdStreamIndex(EVertexInputStreamType::Default) >= 0;

	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
	{
		FMeshBatchElement& MeshElement = Elements[ElementIndex];

		if (bPrimitiveShaderDataComesFromSceneBuffer)
		{
			checkf(!Elements[ElementIndex].PrimitiveUniformBuffer, 
				TEXT("FMeshBatch was assigned a PrimitiveUniformBuffer even though Vertex Factory %s fetches primitive shader data through a Scene buffer.  The assigned PrimitiveUniformBuffer cannot be respected.  Use PrimitiveUniformBufferResource instead for dynamic primitive data, or leave both null to get FPrimitiveSceneProxy->UniformBuffer."), VertexFactory->GetType()->GetName());
		}

		// If we are not using GPU Scene, draws using vertex factories that do not support an explicit PrimitiveUniformBuffer on the FMeshBatch need to be setup with the FPrimitiveSceneProxy's uniform buffer
		if (!MeshElement.PrimitiveUniformBufferResource && !UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel) && bVFSupportsPrimitiveIdStream)
		{
			MeshElement.PrimitiveUniformBuffer = PrimitiveSceneProxy->GetUniformBuffer();
		}

		const bool bValidPrimitiveData = bPrimitiveShaderDataComesFromSceneBuffer || Elements[ElementIndex].PrimitiveUniformBuffer || Elements[ElementIndex].PrimitiveUniformBufferResource;

		UE_CLOG(!bValidPrimitiveData, LogEngine, Fatal,
			TEXT("FMeshBatch was not properly setup. No primitive uniform buffer was specified and the vertex factory does not have a valid primitive id stream.\n")
			TEXT("\tVertexFactory[Name: %s, Initialized: %u]\n")
			TEXT("\tPrimitiveSceneProxy[Level: %s, Owner: %s, Resource: %s]"),
			*VertexFactory->GetType()->GetFName().ToString(),
			VertexFactory->IsInitialized() ? 1 : 0,
			*PrimitiveSceneProxy->GetLevelName().ToString(),
			*PrimitiveSceneProxy->GetOwnerName().ToString(),
			*PrimitiveSceneProxy->GetResourceName().ToString());
	}
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileReflectionCaptureShaderParameters, "MobileReflectionCapture");

void FDefaultMobileReflectionCaptureUniformBuffer::InitDynamicRHI()
{
	FMobileReflectionCaptureShaderParameters Parameters;
	Parameters.Params = FVector4(1.f, 0.f, 0.f, 0.f);
	Parameters.Texture = GBlackTextureCube->TextureRHI;
	Parameters.TextureSampler = GBlackTextureCube->SamplerStateRHI;
	SetContents(Parameters);
	Super::InitDynamicRHI();
}

/** Global uniform buffer containing the default reflection data used in mobile renderer. */
TGlobalResource<FDefaultMobileReflectionCaptureUniformBuffer> GDefaultMobileReflectionCaptureUniformBuffer;