// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsInterface.h: Hair manager implementation.
=============================================================================*/

#include "HairStrandsInterface.h"
#include "HairStrandsRendering.h"
#include "HairStrandsMeshProjection.h"

#include "GPUSkinCache.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "CommonRenderResources.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "SceneRendering.h"
#include "SystemTextures.h"

DEFINE_LOG_CATEGORY_STATIC(LogHairRendering, Log, All);

static int32 GHairStrandsRaytracingEnable = 0;
static FAutoConsoleVariableRef CVarHairStrandsRaytracingEnable(TEXT("r.HairStrands.Raytracing"), GHairStrandsRaytracingEnable, TEXT("Enable/Disable hair strands raytracing (fallback onto raster techniques"));

static int32 GHairStrandsEnable = 1;
static FAutoConsoleVariableRef CVarHairStrandsRenderingEnable(TEXT("r.HairStrands.Enable"), GHairStrandsEnable, TEXT("Enable/Disable hair strands rendering"));

static int32 GHairCardsEnable = 0;
static FAutoConsoleVariableRef CVarHairCardsEnable(TEXT("r.HairStrands.Cards"), GHairCardsEnable, TEXT("Enable/Disable hair cards rendering. This is a temporary CVAR while the feature is developed. Need to be turned on when the engine starts."));

static int32 GHairMeshesEnable = 0;
static FAutoConsoleVariableRef CVarHairMeshesEnable(TEXT("r.HairStrands.Meshes"), GHairMeshesEnable, TEXT("Enable/Disable hair meshes rendering. This is a temporary CVAR while the feature is developed. Need to be turned on when the engine starts."));

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool IsHairCardsEnable() 
{ 
	return GHairCardsEnable > 0;  
}

bool IsHairMeshesEnable()
{
	return GHairMeshesEnable > 0;
}

bool IsHairStrandsEnable(EShaderPlatform Platform)
{
	return
		IsHairStrandsSupported(Platform) &&
		GHairStrandsEnable == 1;
}

bool IsHairRayTracingEnabled()
{
	FString Commandline = FCommandLine::Get();
	bool bIsCookCommandlet = IsRunningCommandlet() && Commandline.Contains(TEXT("run=cook"));

	if (GIsRHIInitialized && !bIsCookCommandlet)
	{
		return IsRayTracingEnabled() && GHairStrandsRaytracingEnable;
	}
	else
	{
		return false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FHairGroupPublicData::FHairGroupPublicData(uint32 InGroupIndex, uint32 InClusterCount, uint32 InVertexCount)
{
	GroupIndex = InGroupIndex;
	GroupControlTriangleStripVertexCount = InVertexCount * 6; // 6 vertex per point for a quad
	ClusterCount = InClusterCount;
	VertexCount = InVertexCount; // Control points
}

void FHairGroupPublicData::InitRHI()
{
	{
		DrawIndirectBuffer.Initialize(sizeof(uint32), 4, PF_R32_UINT, BUF_DrawIndirect);
		uint32* BufferData = (uint32*)RHILockVertexBuffer(DrawIndirectBuffer.Buffer, 0, sizeof(uint32) * 4, RLM_WriteOnly);
		BufferData[0] = GroupControlTriangleStripVertexCount;
		BufferData[1] = 1;
		BufferData[2] = 0;
		BufferData[3] = 0;
		RHIUnlockVertexBuffer(DrawIndirectBuffer.Buffer);
	}

	{
		DrawIndirectRasterComputeBuffer.Initialize(sizeof(uint32), 4, PF_R32_UINT, BUF_DrawIndirect);
		uint32* BufferData = (uint32*)RHILockVertexBuffer(DrawIndirectRasterComputeBuffer.Buffer, 0, sizeof(uint32) * 4, RLM_WriteOnly);
		BufferData[0] = 0;
		BufferData[1] = 1;
		BufferData[2] = 0;
		BufferData[3] = 0;
		RHIUnlockVertexBuffer(DrawIndirectRasterComputeBuffer.Buffer);
	}

	ClusterAABBBuffer.Initialize(sizeof(int32), ClusterCount * 6, EPixelFormat::PF_R32_SINT, BUF_Static);
	GroupAABBBuffer.Initialize(sizeof(int32), 6, EPixelFormat::PF_R32_SINT, BUF_Static);

	CulledVertexIdBuffer.Initialize(sizeof(int32), VertexCount, EPixelFormat::PF_R32_UINT, BUF_Static);
	CulledVertexRadiusScaleBuffer.Initialize(sizeof(float), VertexCount, EPixelFormat::PF_R32_FLOAT, BUF_Static);
}

void FHairGroupPublicData::ReleaseRHI()
{
	DrawIndirectBuffer.Release();
	DrawIndirectRasterComputeBuffer.Release();
	ClusterAABBBuffer.Release();
	GroupAABBBuffer.Release();
	CulledVertexIdBuffer.Release();
	CulledVertexRadiusScaleBuffer.Release();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void TransitBufferToReadable(FRHICommandListImmediate& RHICmdList, FBufferTransitionQueue& BuffersToTransit)
{
	if (BuffersToTransit.Num())
	{
		FMemMark Mark(FMemStack::Get());
		TArray<FRHITransitionInfo, TMemStackAllocator<>> Transitions;
		Transitions.Reserve(BuffersToTransit.Num());
		for (FRHIUnorderedAccessView* UAV : BuffersToTransit)
		{
			// TODO: do we know the source state here?
			Transitions.Add(FRHITransitionInfo(UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
		}
		RHICmdList.Transition(Transitions);
		BuffersToTransit.SetNum(0, false);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Bookmark API
THairStrandsBookmarkFunction  GHairStrandsBookmarkFunction = nullptr;
THairStrandsParameterFunction GHairStrandsParameterFunction = nullptr;
void RegisterBookmarkFunction(THairStrandsBookmarkFunction Bookmark, THairStrandsParameterFunction Parameter)
{
	if (Bookmark)
	{
		GHairStrandsBookmarkFunction = Bookmark;
	}

	if (Parameter)
	{
		GHairStrandsParameterFunction = Parameter;
	}
}

void RunHairStrandsBookmark(FRHICommandListImmediate& RHICmdList, EHairStrandsBookmark Bookmark, FHairStrandsBookmarkParameters& Parameters)
{
	if (GHairStrandsBookmarkFunction)
	{
		GHairStrandsBookmarkFunction(RHICmdList, Bookmark, Parameters);
	}
}

FHairStrandsBookmarkParameters CreateHairStrandsBookmarkParameters(FViewInfo& View)
{
	FHairStrandsBookmarkParameters Out;
	Out.DebugShaderData		= &View.ShaderDrawData;
	Out.SkinCache			= View.Family->Scene->GetGPUSkinCache();
	Out.WorldType			= View.Family->Scene->GetWorld()->WorldType;
	Out.ShaderMap			= View.ShaderMap;
	Out.View				= &View;
	Out.ViewRect			= View.ViewRect;
	Out.SceneColorTexture	= nullptr;

	if (GHairStrandsParameterFunction)
	{
		GHairStrandsParameterFunction(Out);
	}

	return Out;
}