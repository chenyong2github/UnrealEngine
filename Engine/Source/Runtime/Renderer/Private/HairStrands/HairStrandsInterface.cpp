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

static int32 GHairStrandsGlobalEnable = 1;
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
// Import/export utils function for hair resources
void FRDGExternalBuffer::Release()
{
	Buffer = nullptr;
	SRV = nullptr;
	UAV = nullptr;
}

FRDGImportedBuffer Register(FRDGBuilder& GraphBuilder, const FRDGExternalBuffer& In, ERDGImportedBufferFlags Flags, ERDGUnorderedAccessViewFlags UAVFlags)
{
	FRDGImportedBuffer Out;
	if (!In.Buffer)
	{
		return Out;
	}
	const uint32 uFlags = uint32(Flags);
	Out.Buffer = GraphBuilder.RegisterExternalBuffer(In.Buffer);
	if (In.Format != PF_Unknown)
	{
		if (uFlags & uint32(ERDGImportedBufferFlags::CreateSRV)) { Out.SRV = GraphBuilder.CreateSRV(Out.Buffer, In.Format); }
		if (uFlags & uint32(ERDGImportedBufferFlags::CreateUAV)) { Out.UAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Out.Buffer, In.Format), UAVFlags); }
	}
	else
	{
		if (uFlags & uint32(ERDGImportedBufferFlags::CreateSRV)) { Out.SRV = GraphBuilder.CreateSRV(Out.Buffer); }
		if (uFlags & uint32(ERDGImportedBufferFlags::CreateUAV)) { Out.UAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Out.Buffer),  UAVFlags); }
	}
	return Out;
}

FRDGBufferSRVRef RegisterAsSRV(FRDGBuilder& GraphBuilder, const FRDGExternalBuffer& In)
{
	if (!In.Buffer)
	{
		return nullptr;
	}

	FRDGBufferSRVRef Out = nullptr;
	FRDGBufferRef Buffer = GraphBuilder.RegisterExternalBuffer(In.Buffer);
	if (In.Format != PF_Unknown)
	{
		Out = GraphBuilder.CreateSRV(Buffer, In.Format);
	}
	else
	{
		Out = GraphBuilder.CreateSRV(Buffer);
	}
	return Out;
}

FRDGBufferUAVRef RegisterAsUAV(FRDGBuilder& GraphBuilder, const FRDGExternalBuffer& In, ERDGUnorderedAccessViewFlags Flags)
{
	if (!In.Buffer)
	{
		return nullptr;
	}

	FRDGBufferUAVRef Out = nullptr;
	FRDGBufferRef Buffer = GraphBuilder.RegisterExternalBuffer(In.Buffer);
	if (In.Format != PF_Unknown)
	{
		Out = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Buffer, In.Format), Flags);
	}
	else
	{
		Out = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Buffer), Flags);
	}
	return Out;
}

bool IsHairRayTracingEnabled()
{
	if (GIsRHIInitialized && !IsRunningCookCommandlet())
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
void ConvertToExternalBufferWithViews(FRDGBuilder& GraphBuilder, FRDGBufferRef& InBuffer, FRDGExternalBuffer& OutBuffer, EPixelFormat Format)
{
	ConvertToExternalBuffer(GraphBuilder, InBuffer, OutBuffer.Buffer);
	if (Format != PF_Unknown)
	{
		OutBuffer.SRV = OutBuffer.Buffer->GetOrCreateSRV(FRDGBufferSRVDesc(InBuffer, Format));
		OutBuffer.UAV = OutBuffer.Buffer->GetOrCreateUAV(FRDGBufferUAVDesc(InBuffer, Format));
	}
	else
	{
		OutBuffer.SRV = OutBuffer.Buffer->GetOrCreateSRV(FRDGBufferSRVDesc(InBuffer));
		OutBuffer.UAV = OutBuffer.Buffer->GetOrCreateUAV(FRDGBufferUAVDesc(InBuffer));
	}
	OutBuffer.Format = Format;
}

void InternalCreateIndirectBufferRDG(FRDGBuilder& GraphBuilder, FRDGExternalBuffer& Out, const TCHAR* DebugName, const FUintVector4& InitValues)
{
	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(4, 4);
	Desc.Usage |= BUF_DrawIndirect;
	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(Desc, DebugName);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Buffer, PF_R32_UINT), 0u);
	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out, PF_R32_UINT);
}

void InternalCreateVertexBufferRDG(FRDGBuilder& GraphBuilder, uint32 ElementSizeInBytes, uint32 ElementCount, EPixelFormat Format, FRDGExternalBuffer& Out, const TCHAR* DebugName, bool bClearFloat=false)
{
	FRDGBufferRef Buffer = nullptr;

	const uint32 DataCount = ElementCount;
	const uint32 DataSizeInBytes = ElementSizeInBytes * DataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}

	// #hair_todo: Create this with a create+clear pass instead?
	const FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(ElementSizeInBytes, ElementCount);
	TArray<uint8> InitializeData;
	InitializeData.Init(0u, DataSizeInBytes);
	Buffer = CreateVertexBuffer(
		GraphBuilder,
		DebugName,
		Desc,
		InitializeData.GetData(),
		DataSizeInBytes,
		ERDGInitialDataFlags::None);

	if (bClearFloat)
	{
		AddClearUAVFloatPass(GraphBuilder, GraphBuilder.CreateUAV(Buffer, Format), 0.f);
	}
	else
	{
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Buffer, Format), 0);
	}
	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out, Format);
}

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

	if (GUsingNullRHI) { return; }

	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);
	InternalCreateIndirectBufferRDG(GraphBuilder, DrawIndirectBuffer, TEXT("HairStrandsCluster_DrawIndirectBuffer"), FUintVector4(GroupControlTriangleStripVertexCount, 1, 0, 0));
	InternalCreateIndirectBufferRDG(GraphBuilder, DrawIndirectRasterComputeBuffer, TEXT("HairStrandsCluster_DrawIndirectRasterComputeBuffer"), FUintVector4(0, 1, 0, 0));

	InternalCreateVertexBufferRDG(GraphBuilder, sizeof(int32), ClusterCount * 6, EPixelFormat::PF_R32_SINT, ClusterAABBBuffer, TEXT("HairStrandsCluster_ClusterAABBBuffer"));
	InternalCreateVertexBufferRDG(GraphBuilder, sizeof(int32), 6, EPixelFormat::PF_R32_SINT, GroupAABBBuffer, TEXT("HairStrandsCluster_GroupAABBBuffer"));

	InternalCreateVertexBufferRDG(GraphBuilder, sizeof(int32), VertexCount, EPixelFormat::PF_R32_UINT, CulledVertexIdBuffer, TEXT("HairStrandsCluster_CulledVertexIdBuffer"));
	InternalCreateVertexBufferRDG(GraphBuilder, sizeof(float), VertexCount, EPixelFormat::PF_R32_FLOAT, CulledVertexRadiusScaleBuffer, TEXT("HairStrandsCluster_CulledVertexRadiusScaleBuffer"), true);

	GraphBuilder.Execute();
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
				Transitions.Add(FRHITransitionInfo(UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
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