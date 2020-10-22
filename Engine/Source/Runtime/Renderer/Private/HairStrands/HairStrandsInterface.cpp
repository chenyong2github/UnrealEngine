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

static int32 GHairStrandsRaytracingEnable = 1;
static FAutoConsoleVariableRef CVarHairStrandsRaytracingEnable(TEXT("r.HairStrands.Raytracing"), GHairStrandsRaytracingEnable, TEXT("Enable/Disable hair strands raytracing geometry. This is anopt-in option per groom asset/groom instance."));

static int32 GHairStrandsGlobalEnable = 0;
static FAutoConsoleVariableRef CVarHairStrandsGlobalEnable(TEXT("r.HairStrands.Enable"), GHairStrandsGlobalEnable, TEXT("Enable/Disable the entire hair strands system. This affects all geometric representations (i.e., strands, cards, and meshes)."));

static int32 GHairStrandsEnable = 1;
static FAutoConsoleVariableRef CVarHairStrandsEnable(TEXT("r.HairStrands.Strands"), GHairStrandsEnable, TEXT("Enable/Disable hair strands rendering"));

static int32 GHairCardsEnable = 1;
static FAutoConsoleVariableRef CVarHairCardsEnable(TEXT("r.HairStrands.Cards"), GHairCardsEnable, TEXT("Enable/Disable hair cards rendering. This variable needs to be turned on when the engine starts."));

static int32 GHairMeshesEnable = 1;
static FAutoConsoleVariableRef CVarHairMeshesEnable(TEXT("r.HairStrands.Meshes"), GHairMeshesEnable, TEXT("Enable/Disable hair meshes rendering. This variable needs to be turned on when the engine starts."));

static int32 GHairStrandsBinding = 1;
static FAutoConsoleVariableRef CVarHairStrandsBinding(TEXT("r.HairStrands.Binding"), GHairStrandsBinding, TEXT("Enable/Disable hair binding, i.e., hair attached to skeletal meshes."));

static int32 GHairStrandsSimulation = 1;
static FAutoConsoleVariableRef CVarHairStrandsSimulation(TEXT("r.HairStrands.Simulation"), GHairStrandsSimulation, TEXT("Enable/disable hair simulation"));

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

bool IsHairStrandsSupported(EHairStrandsShaderType Type, EShaderPlatform Platform)
{
	// Important:
	// EHairStrandsShaderType::All: Mobile is excluded as we don't need any interpolation/simulation code for this. It only do rigid transformation. 
	//                              The runtime setting in these case are r.HairStrands.Binding=0 & r.HairStrands.Simulation=0
	const bool Cards_Meshes_All = true;
	const bool bIsMobile = IsMobilePlatform(Platform) || Platform == SP_PCD3D_ES3_1;

	switch (Type)
	{
	case EHairStrandsShaderType::Strands: return IsHairStrandsGeometrySupported(Platform);
	case EHairStrandsShaderType::Cards:	  return Cards_Meshes_All;
	case EHairStrandsShaderType::Meshes:  return Cards_Meshes_All;
	case EHairStrandsShaderType::Tool:	  return (IsD3DPlatform(Platform, false) || IsVulkanSM5Platform(Platform)) && IsPCPlatform(Platform) && GetMaxSupportedFeatureLevel(Platform) == ERHIFeatureLevel::SM5;
	case EHairStrandsShaderType::All:	  return Cards_Meshes_All && !bIsMobile;
	}
	return false;
}

bool IsHairStrandsEnabled(EHairStrandsShaderType Type, EShaderPlatform Platform)
{
	if (GHairStrandsGlobalEnable <= 0) return false;

	// Important:
	// EHairStrandsShaderType::All: Mobile is excluded as we don't need any interpolation/simulation code for this. It only do rigid transformation. 
	//                              The runtime setting in these case are r.HairStrands.Binding=0 & r.HairStrands.Simulation=0
	const bool bIsMobile = Platform != EShaderPlatform::SP_NumPlatforms ? IsMobilePlatform(Platform) || Platform == SP_PCD3D_ES3_1 : false;
	switch (Type)
	{
	case EHairStrandsShaderType::Strands:	return GHairStrandsEnable > 0 && (Platform != EShaderPlatform::SP_NumPlatforms ? IsHairStrandsGeometrySupported(Platform) : true);
	case EHairStrandsShaderType::Cards:		return GHairCardsEnable > 0;
	case EHairStrandsShaderType::Meshes:	return GHairMeshesEnable > 0;
#if PLATFORM_DESKTOP && PLATFORM_WINDOWS
	case EHairStrandsShaderType::Tool:		return (GHairCardsEnable > 0 || GHairMeshesEnable > 0 || GHairStrandsEnable > 0);
#else
	case EHairStrandsShaderType::Tool:		return false;
#endif
	case EHairStrandsShaderType::All :		return GHairStrandsGlobalEnable > 0 && (GHairCardsEnable > 0 || GHairMeshesEnable > 0 || GHairStrandsEnable > 0) && !bIsMobile;
	}
	return false;
}

bool IsHairStrandsBindingEnable()
{
	return GHairStrandsBinding > 0;
}

bool IsHairStrandsSimulationEnable()
{
	return GHairStrandsSimulation > 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FHairGroupPublicData::FHairGroupPublicData(uint32 InGroupIndex)
{
	GroupIndex = InGroupIndex;
	GroupControlTriangleStripVertexCount = 0;
	ClusterCount = 0;
	VertexCount = 0;
}

void FHairGroupPublicData::SetClusters(uint32 InClusterCount, uint32 InVertexCount)
{
	GroupControlTriangleStripVertexCount = InVertexCount * 6; // 6 vertex per point for a quad
	ClusterCount = InClusterCount;
	VertexCount = InVertexCount; // Control points
}

void FHairGroupPublicData::InitRHI()
{
	if (ClusterCount == 0)
		return;

	{
		DrawIndirectBuffer.Initialize(sizeof(uint32), 4, PF_R32_UINT, BUF_DrawIndirect, TEXT("HairStrandsCluster_DrawIndirectBuffer"));
		uint32* BufferData = (uint32*)RHILockVertexBuffer(DrawIndirectBuffer.Buffer, 0, sizeof(uint32) * 4, RLM_WriteOnly);
		BufferData[0] = GroupControlTriangleStripVertexCount;
		BufferData[1] = 1;
		BufferData[2] = 0;
		BufferData[3] = 0;
		RHIUnlockVertexBuffer(DrawIndirectBuffer.Buffer);
	}

	{
		DrawIndirectRasterComputeBuffer.Initialize(sizeof(uint32), 4, PF_R32_UINT, BUF_DrawIndirect, TEXT("HairStrandsCluster_DrawIndirectRasterComputeBuffer"));
		uint32* BufferData = (uint32*)RHILockVertexBuffer(DrawIndirectRasterComputeBuffer.Buffer, 0, sizeof(uint32) * 4, RLM_WriteOnly);
		BufferData[0] = 0;
		BufferData[1] = 1;
		BufferData[2] = 0;
		BufferData[3] = 0;
		RHIUnlockVertexBuffer(DrawIndirectRasterComputeBuffer.Buffer);
	}

	ClusterAABBBuffer.Initialize(sizeof(int32), ClusterCount * 6, EPixelFormat::PF_R32_SINT, BUF_Static, TEXT("HairStrandsCluster_ClusterAABBBuffer"));
	GroupAABBBuffer.Initialize(sizeof(int32), 6, EPixelFormat::PF_R32_SINT, BUF_Static, TEXT("HairStrandsCluster_GroupAABBBuffer"));

	CulledVertexIdBuffer.Initialize(sizeof(int32), VertexCount, EPixelFormat::PF_R32_UINT, BUF_Static, TEXT("HairStrandsCluster_CulledVertexIdBuffer"));
	CulledVertexRadiusScaleBuffer.Initialize(sizeof(float), VertexCount, EPixelFormat::PF_R32_FLOAT, BUF_Static, TEXT("HairStrandsCluster_CulledVertexRadiusScaleBuffer"));
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
void TransitBufferToReadable(FRDGBuilder& GraphBuilder, FBufferTransitionQueue& BuffersToTransit)
{
	if (BuffersToTransit.Num())
	{
		AddPass(GraphBuilder, [LocalBuffersToTransit = MoveTemp(BuffersToTransit)](FRHICommandList& RHICmdList)
		{
			FMemMark Mark(FMemStack::Get());
			TArray<FRHITransitionInfo, TMemStackAllocator<>> Transitions;
			Transitions.Reserve(LocalBuffersToTransit.Num());
			for (FRHIUnorderedAccessView* UAV : LocalBuffersToTransit)
			{
				Transitions.Add(FRHITransitionInfo(UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
			}
			RHICmdList.Transition(Transitions);
		});
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

void RunHairStrandsBookmark(FRDGBuilder& GraphBuilder, EHairStrandsBookmark Bookmark, FHairStrandsBookmarkParameters& Parameters)
{
	if (GHairStrandsBookmarkFunction)
	{
		GHairStrandsBookmarkFunction(GraphBuilder, Bookmark, Parameters);
	}
}

bool IsHairStrandsClusterCullingUseHzb();
FHairStrandsBookmarkParameters CreateHairStrandsBookmarkParameters(FViewInfo& View)
{
	FHairStrandsBookmarkParameters Out;
	Out.DebugShaderData			= &View.ShaderDrawData;
	Out.SkinCache				= View.Family->Scene->GetGPUSkinCache();
	Out.WorldType				= View.Family->Scene->GetWorld()->WorldType;
	Out.ShaderMap				= View.ShaderMap;
	Out.View					= &View;
	Out.ViewRect				= View.ViewRect;
	Out.SceneColorTexture		= nullptr;
	Out.bStrandsGeometryEnabled = IsHairStrandsEnabled(EHairStrandsShaderType::Strands, View.GetShaderPlatform());
	if (GHairStrandsParameterFunction)
	{
		GHairStrandsParameterFunction(Out);
	}
	Out.bHzbRequest = Out.bHasElements && Out.bStrandsGeometryEnabled && IsHairStrandsClusterCullingUseHzb();

	return Out;
}

FHairStrandsBookmarkParameters CreateHairStrandsBookmarkParameters(TArray<FViewInfo>& Views)
{
	FHairStrandsBookmarkParameters Out;
	Out = CreateHairStrandsBookmarkParameters(Views[0]);
	Out.AllViews.Reserve(Views.Num());
	for (const FViewInfo& View : Views)
	{
		Out.AllViews.Add(&View);
	}

	return Out;
}