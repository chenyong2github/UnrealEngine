// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomResources.h"
#include "EngineUtils.h"
#include "GroomAssetImportData.h"
#include "GroomBuilder.h"
#include "GroomImportOptions.h"
#include "GroomSettings.h"
#include "RenderingThread.h"
#include "Engine/AssetUserData.h"
#include "HairStrandsVertexFactory.h"
#include "Misc/Paths.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/PhysicsObjectVersion.h"
#include "UObject/ReleaseObjectVersion.h"
#include "NiagaraSystem.h"
#include "Async/ParallelFor.h"
#include "RenderGraph.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "GroomBindingBuilder.h"

enum class EHairResourceUsageType : uint8
{
	Static,
	Dynamic
};

#define HAIRSTRANDS_RESOUCE_NAME(Type, Name) (Type == EHairStrandsResourcesType::Guides  ? TEXT(#Name "(Guides)") : (Type == EHairStrandsResourcesType::Strands ? TEXT(#Name "(Strands)") : TEXT(#Name "(Cards)")))

/////////////////////////////////////////////////////////////////////////////////////////
// FRWBuffer utils 
void UploadDataToBuffer(FReadBuffer& OutBuffer, uint32 DataSizeInBytes, const void* InCpuData)
{
	void* BufferData = RHILockBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memcpy(BufferData, InCpuData, DataSizeInBytes);
	RHIUnlockBuffer(OutBuffer.Buffer);
}

void UploadDataToBuffer(FRWBufferStructured& OutBuffer, uint32 DataSizeInBytes, const void* InCpuData)
{
	void* BufferData = RHILockBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memcpy(BufferData, InCpuData, DataSizeInBytes);
	RHIUnlockBuffer(OutBuffer.Buffer);
}

template<typename FormatType>
void CreateBuffer(const TArray<typename FormatType::Type>& InData, FRWBuffer& OutBuffer, const TCHAR* DebugName, ERHIAccess InitialAccess = ERHIAccess::SRVMask)
{
	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = FormatType::SizeInByte*DataCount;

	if (DataSizeInBytes == 0) return;

	OutBuffer.Initialize(FormatType::SizeInByte, DataCount, FormatType::Format, InitialAccess, BUF_Static, DebugName);
	void* BufferData = RHILockBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);

	FMemory::Memcpy(BufferData, InData.GetData(), DataSizeInBytes);
	RHIUnlockBuffer(OutBuffer.Buffer);
}

template<typename FormatType>
void CreateBuffer(uint32 InVertexCount, FRWBuffer& OutBuffer, const TCHAR* DebugName)
{
	const uint32 DataCount = InVertexCount;
	const uint32 DataSizeInBytes = FormatType::SizeInByte*DataCount;

	if (DataSizeInBytes == 0) return;

	OutBuffer.Initialize(FormatType::SizeInByte, DataCount, FormatType::Format, ERHIAccess::UAVCompute, BUF_Static, DebugName);
	void* BufferData = RHILockBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memset(BufferData, 0, DataSizeInBytes);
	RHIUnlockBuffer(OutBuffer.Buffer);
}

template<typename FormatType>
void CreateBuffer(const TArray<typename FormatType::Type>& InData, FHairCardsVertexBuffer& OutBuffer, const TCHAR* DebugName, ERHIAccess InitialAccess = ERHIAccess::SRVMask)
{
	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = FormatType::SizeInByte * DataCount;

	if (DataSizeInBytes == 0) return;

	FRHIResourceCreateInfo CreateInfo(DebugName);
	CreateInfo.ResourceArray = nullptr;

	OutBuffer.VertexBufferRHI = RHICreateVertexBuffer(DataSizeInBytes, BUF_Static | BUF_ShaderResource, InitialAccess, CreateInfo);

	void* BufferData = RHILockBuffer(OutBuffer.VertexBufferRHI, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memcpy(BufferData, InData.GetData(), DataSizeInBytes);
	RHIUnlockBuffer(OutBuffer.VertexBufferRHI);
	OutBuffer.ShaderResourceViewRHI = RHICreateShaderResourceView(OutBuffer.VertexBufferRHI, FormatType::SizeInByte, FormatType::Format);
}

/////////////////////////////////////////////////////////////////////////////////////////
// RDG buffers utils 

static FRDGBufferRef InternalCreateVertexBuffer(
	FRDGBuilder& GraphBuilder,
	const TCHAR* Name,
	const FRDGBufferDesc& Desc,
	const void* InitialData,
	uint64 InitialDataSize,
	ERDGInitialDataFlags InitialDataFlags)
{
	checkf(Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer, TEXT("CreateVertexBuffer called with an FRDGBufferDesc underlying type that is not 'VertexBuffer'. Buffer: %s"), Name);
	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(Desc, Name, ERDGBufferFlags::MultiFrame);
	if (InitialData && InitialDataSize)
	{
		GraphBuilder.QueueBufferUpload(Buffer, InitialData, InitialDataSize, InitialDataFlags);
	}
	return Buffer;
}

template<typename FormatType>
void InternalCreateVertexBufferRDG_FromBulkData(FRDGBuilder& GraphBuilder, FByteBulkData& InBulkData, uint32 InDataCount, FRDGExternalBuffer& Out, const TCHAR* DebugName, EHairResourceUsageType UsageType)
{
	const uint32 InDataCount_Check = InBulkData.GetBulkDataSize() / sizeof(typename FormatType::BulkType);
	check(InDataCount_Check == InDataCount);

	const uint32 DataSizeInBytes = FormatType::SizeInByte * InDataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}

	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(FormatType::SizeInByte, InDataCount);
	if (UsageType != EHairResourceUsageType::Dynamic)
	{
		Desc.Usage = Desc.Usage & (~BUF_UnorderedAccess);
	}

	const typename FormatType::BulkType* BulkData = (const typename FormatType::BulkType*)InBulkData.Lock(LOCK_READ_ONLY);
	FRDGBufferRef Buffer = InternalCreateVertexBuffer(
		GraphBuilder,
		DebugName,
		Desc,
		BulkData,
		DataSizeInBytes,
		ERDGInitialDataFlags::None); // Copy data internally
	InBulkData.Unlock();

	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out, FormatType::Format);
}

template<typename FormatType>
void InternalCreateVertexBufferRDG(FRDGBuilder& GraphBuilder, const typename FormatType::Type* InData, uint32 InDataCount, FRDGExternalBuffer& Out, const TCHAR* DebugName, EHairResourceUsageType UsageType, ERDGInitialDataFlags InitialDataFlags)
{
	FRDGBufferRef Buffer = nullptr;

	const uint32 DataSizeInBytes = FormatType::SizeInByte * InDataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}

	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(FormatType::SizeInByte, InDataCount);
	if (UsageType != EHairResourceUsageType::Dynamic)
	{
		Desc.Usage = Desc.Usage & (~BUF_UnorderedAccess);
	}
	Buffer = InternalCreateVertexBuffer(
		GraphBuilder,
		DebugName,
		Desc,
		InData,
		DataSizeInBytes,
		InitialDataFlags);

	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out, FormatType::Format);
}

template<typename FormatType>
void InternalCreateVertexBufferRDG(FRDGBuilder& GraphBuilder, const TArray<typename FormatType::Type>& InData, FRDGExternalBuffer& Out, const TCHAR* DebugName, EHairResourceUsageType UsageType, ERDGInitialDataFlags InitialDataFlags= ERDGInitialDataFlags::NoCopy)
{
	FRDGBufferRef Buffer = nullptr;

	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = FormatType::SizeInByte * DataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}

	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(FormatType::SizeInByte, InData.Num());
	if (UsageType != EHairResourceUsageType::Dynamic)
	{
		Desc.Usage = Desc.Usage & (~BUF_UnorderedAccess);
	}
	Buffer = InternalCreateVertexBuffer(
		GraphBuilder,
		DebugName,
		Desc,
		InData.GetData(),
		DataSizeInBytes,
		InitialDataFlags);

	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out, FormatType::Format);
}

template<typename DataType>
void InternalCreateVertexBufferRDG(FRDGBuilder& GraphBuilder, const TArray<DataType>& InData, EPixelFormat Format, FRDGExternalBuffer& Out, const TCHAR* DebugName, EHairResourceUsageType UsageType)
{
	FRDGBufferRef Buffer = nullptr;

	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = sizeof(DataType) * DataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}

	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(DataType), InData.Num());
	if (UsageType != EHairResourceUsageType::Dynamic)
	{
		Desc.Usage = Desc.Usage & (~BUF_UnorderedAccess);
	}
	Buffer = InternalCreateVertexBuffer(
		GraphBuilder,
		DebugName,
		Desc,
		InData.GetData(),
		DataSizeInBytes,
		ERDGInitialDataFlags::NoCopy);

	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out, Format);
}

template<typename FormatType>
void InternalCreateVertexBufferRDG(FRDGBuilder& GraphBuilder, uint32 InVertexCount, FRDGExternalBuffer& Out, const TCHAR* DebugName, EHairResourceUsageType Usage)
{
	// Sanity check
	check(Usage == EHairResourceUsageType::Dynamic);

	const uint32 DataCount = InVertexCount;
	const uint32 DataSizeInBytes = FormatType::SizeInByte * DataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}

	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(FormatType::SizeInByte, InVertexCount);
	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(Desc, DebugName, ERDGBufferFlags::MultiFrame);

	auto IsFloatFormat = []()
	{
		switch (FormatType::Format)
		{
		case PF_A32B32G32R32F:
		case PF_FloatR11G11B10:
		case PF_FloatRGB:
		case PF_FloatRGBA:
		case PF_G16R16F_FILTER:
		case PF_G16R16F:
		case PF_G32R32F:
		case PF_R16F_FILTER:
		case PF_R16F:
		case PF_R32_FLOAT:
			return true;
		default:
			return false;
		}
	};

	if (IsFloatFormat())
	{
		AddClearUAVFloatPass(GraphBuilder, GraphBuilder.CreateUAV(Buffer, FormatType::Format), 0.0f);
	}
	else
	{
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Buffer, FormatType::Format), 0u);
	}
	
	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out, FormatType::Format);
}

template<typename FormatType>
void InternalCreateStructuredBufferRDG(FRDGBuilder& GraphBuilder, const TArray<FormatType>& InData, uint32 InSizeInByte, FRDGExternalBuffer& Out, const TCHAR* DebugName, EHairResourceUsageType UsageType)
{
	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = InSizeInByte * DataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}
	
	FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(InSizeInByte, DataCount);
	if (UsageType != EHairResourceUsageType::Dynamic)
	{
		Desc.Usage = Desc.Usage & (~BUF_UnorderedAccess);
	}

	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(Desc, DebugName, ERDGBufferFlags::MultiFrame);
	if (InData.GetData() && DataSizeInBytes)
	{
		GraphBuilder.QueueBufferUpload(Buffer, InData.GetData(), DataSizeInBytes, ERDGInitialDataFlags::NoCopy);
	}
	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out);
}

template<typename FormatType>
void InternalCreateStructuredBufferRDG_FromBulkData(FRDGBuilder& GraphBuilder, FByteBulkData& InBulkData, uint32 InDataCount, FRDGExternalBuffer& Out, const TCHAR* DebugName, EHairResourceUsageType UsageType)
{
	const uint32 InSizeInByte = sizeof(typename FormatType::Type);
	const uint32 DataSizeInBytes = InSizeInByte * InDataCount;
	check(InBulkData.GetBulkDataSize() == DataSizeInBytes);

	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}

	FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(InSizeInByte, InDataCount);
	if (UsageType != EHairResourceUsageType::Dynamic)
	{
		Desc.Usage = Desc.Usage & (~BUF_UnorderedAccess);
	}

	const typename FormatType::Type* Data = (const typename FormatType::Type*)InBulkData.Lock(LOCK_READ_ONLY);
	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(Desc, DebugName, ERDGBufferFlags::MultiFrame);
	if (Data && DataSizeInBytes)
	{
		GraphBuilder.QueueBufferUpload(Buffer, Data, DataSizeInBytes, ERDGInitialDataFlags::None);  // Copy data internally
	}
	InBulkData.Unlock();

	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out);
}

/////////////////////////////////////////////////////////////////////////////////////////

static UTexture2D* CreateCardTexture(FIntPoint Resolution)
{
	UTexture2D* Out = nullptr;

	// Pass NAME_None as name to ensure an unique name is picked, so GC dont delete the new texture when it wants to delete the old one 
	Out = NewObject<UTexture2D>(GetTransientPackage(), NAME_None /*TEXT("ProceduralFollicleMaskTexture")*/, RF_Transient);
	Out->AddToRoot();
	Out->PlatformData = new FTexturePlatformData();
	Out->PlatformData->SizeX = Resolution.X;
	Out->PlatformData->SizeY = Resolution.Y;
	Out->PlatformData->PixelFormat = PF_R32_FLOAT;
	Out->SRGB = false;

	const uint32 MipCount = 1; // FMath::Min(FMath::FloorLog2(Resolution), 5u);// Don't need the full chain
	for (uint32 MipIt = 0; MipIt < MipCount; ++MipIt)
	{
		const uint32 MipResolutionX = Resolution.X >> MipIt;
		const uint32 MipResolutionY = Resolution.Y >> MipIt;
		const uint32 SizeInBytes = sizeof(float) * MipResolutionX * MipResolutionY;

		FTexture2DMipMap* MipMap = new FTexture2DMipMap();
		Out->PlatformData->Mips.Add(MipMap);
		MipMap->SizeX = MipResolutionX;
		MipMap->SizeY = MipResolutionY;
		MipMap->BulkData.Lock(LOCK_READ_WRITE);
		float* MipMemory = (float*)MipMap->BulkData.Realloc(SizeInBytes);
		for (uint32 Y = 0; Y < MipResolutionY; Y++)
			for (uint32 X = 0; X < MipResolutionX; X++)
			{
				MipMemory[X + Y * MipResolutionY] = X / float(MipResolutionX);
			}
		//FMemory::Memzero(MipMemory, SizeInBytes);
		MipMap->BulkData.Unlock();
	}
	Out->UpdateResource();

	return Out;
}

/////////////////////////////////////////////////////////////////////////////////////////
void CreateHairStrandsDebugAttributeBuffer(FRDGBuilder& GraphBuilder, FRDGExternalBuffer* DebugAttributeBuffer, uint32 VertexCount)
{
	if (VertexCount == 0 || !DebugAttributeBuffer)
		return;
	InternalCreateVertexBufferRDG<FHairStrandsAttribute0Format>(GraphBuilder, VertexCount, *DebugAttributeBuffer, TEXT("Hair.Strands_DebugAttributeBuffer"), EHairResourceUsageType::Dynamic);
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairCommonResource::FHairCommonResource(EHairStrandsAllocationType InAllocationType, bool bInUseRenderGraph):
bUseRenderGraph(bInUseRenderGraph),
bIsInitialized(false),
AllocationType(InAllocationType)
{
}

void FHairCommonResource::InitRHI()
{
	if (bIsInitialized || AllocationType == EHairStrandsAllocationType::Deferred || GUsingNullRHI) { return; }

	if (bUseRenderGraph)
	{
		FMemMark Mark(FMemStack::Get());
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		FRDGBuilder GraphBuilder(RHICmdList);
		InternalAllocate(GraphBuilder);
		GraphBuilder.Execute();
	}
	else
	{
		InternalAllocate();
	}
	bIsInitialized = true;
}

void FHairCommonResource::ReleaseRHI()
{
	InternalRelease();
	bIsInitialized = false;
}

void FHairCommonResource::Allocate(FRDGBuilder& GraphBuilder)
{
	if (bIsInitialized) { return; }

	check(AllocationType == EHairStrandsAllocationType::Deferred);
	FRenderResource::InitResource(); // Call RenderResource InitResource() so that the resources is marked as initialized
	InternalAllocate(GraphBuilder);
	bIsInitialized = true;
}

void FHairCommonResource::AllocateLOD(FRDGBuilder& GraphBuilder, int32 LODIndex)
{
	// Sanity check. For allocation sub-resources (like LOD resources) the common/main resource needs to be already initialized.
	check(bIsInitialized);
	check(AllocationType == EHairStrandsAllocationType::Deferred);

	InternalAllocateLOD(GraphBuilder, LODIndex);
}

/////////////////////////////////////////////////////////////////////////////////////////

void FHairCardIndexBuffer::InitRHI()
{
	const uint32 DataSizeInBytes = FHairCardsIndexFormat::SizeInByte * Indices.Num();

	FRHIResourceCreateInfo CreateInfo(TEXT("FHairCardIndexBuffer"));
	IndexBufferRHI = RHICreateBuffer(DataSizeInBytes, BUF_Static | BUF_IndexBuffer, FHairCardsIndexFormat::SizeInByte, ERHIAccess::VertexOrIndexBuffer, CreateInfo);
	void* Buffer = RHILockBuffer(IndexBufferRHI, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memcpy(Buffer, Indices.GetData(), DataSizeInBytes);
	RHIUnlockBuffer(IndexBufferRHI);
}

FHairCardsRestResource::FHairCardsRestResource(const FHairCardsBulkData& InBulkData) :
	FHairCommonResource(EHairStrandsAllocationType::Immediate, false),
	RestPositionBuffer(),
	RestIndexBuffer(InBulkData.Indices),
	NormalsBuffer(),
	UVsBuffer(),
	BulkData(InBulkData)
{

}

void FHairCardsRestResource::InternalAllocate()
{
	// These resources are kept as regular (i.e., non-RDG resources) as they need to be bound at the input assembly stage by the Vertex declaraction which requires FVertexBuffer type
	CreateBuffer<FHairCardsPositionFormat>(BulkData.Positions, RestPositionBuffer, TEXT("Hair.CardsRest_PositionBuffer"));
	CreateBuffer<FHairCardsNormalFormat>(BulkData.Normals, NormalsBuffer, TEXT("Hair.CardsRest_NormalBuffer"));
	CreateBuffer<FHairCardsUVFormat>(BulkData.UVs, UVsBuffer, TEXT("Hair.CardsRest_UVBuffer"));

	FSamplerStateRHIRef DefaultSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	DepthSampler = DefaultSampler;
	TangentSampler = DefaultSampler;
	CoverageSampler = DefaultSampler;
	AttributeSampler = DefaultSampler;
}

void FHairCardsRestResource::InternalRelease()
{
}

void FHairCardsRestResource::InitResource()
{
	FRenderResource::InitResource();
	RestIndexBuffer.InitResource();
	RestPositionBuffer.InitResource();
	NormalsBuffer.InitResource();
	UVsBuffer.InitResource();
}

void FHairCardsRestResource::ReleaseResource()
{
	FRenderResource::ReleaseResource();
	RestIndexBuffer.ReleaseResource();
	RestPositionBuffer.ReleaseResource();
	NormalsBuffer.ReleaseResource();
	UVsBuffer.ReleaseResource();
}

/////////////////////////////////////////////////////////////////////////////////////////
FHairCardsProceduralResource::FHairCardsProceduralResource(const FHairCardsProceduralDatas::FRenderData& InRenderData, const FIntPoint& InAtlasResolution, const FHairCardsVoxel& InVoxel):
	FHairCommonResource(EHairStrandsAllocationType::Immediate),
	CardBoundCount(InRenderData.ClusterBounds.Num()),
	AtlasResolution(InAtlasResolution),
	AtlasRectBuffer(),
	LengthBuffer(),
	CardItToClusterBuffer(),
	ClusterIdToVerticesBuffer(),
	ClusterBoundBuffer(),
	CardsStrandsPositions(),
	CardsStrandsAttributes(),
	RenderData(InRenderData)
{
	CardVoxel = InVoxel;
}

void FHairCardsProceduralResource::InternalAllocate(FRDGBuilder& GraphBuilder)
{
	InternalCreateVertexBufferRDG<FHairCardsAtlasRectFormat>(GraphBuilder, RenderData.CardsRect, AtlasRectBuffer, TEXT("Hair.CardsProcedural_AtlasRectBuffer"), EHairResourceUsageType::Static);
	InternalCreateVertexBufferRDG<FHairCardsDimensionFormat>(GraphBuilder, RenderData.CardsLengths, LengthBuffer, TEXT("Hair.CardsProcedural_LengthBuffer"), EHairResourceUsageType::Static);

	InternalCreateVertexBufferRDG<FHairCardsOffsetAndCount>(GraphBuilder, RenderData.CardItToCluster, CardItToClusterBuffer, TEXT("Hair.CardsProcedural_CardItToClusterBuffer"), EHairResourceUsageType::Static);
	InternalCreateVertexBufferRDG<FHairCardsOffsetAndCount>(GraphBuilder, RenderData.ClusterIdToVertices, ClusterIdToVerticesBuffer, TEXT("Hair.CardsProcedural_ClusterIdToVerticesBuffer"), EHairResourceUsageType::Static);
	InternalCreateVertexBufferRDG<FHairCardsBoundsFormat>(GraphBuilder, RenderData.ClusterBounds, ClusterBoundBuffer, TEXT("Hair.CardsProcedural_ClusterBoundBuffer"), EHairResourceUsageType::Static);

	InternalCreateVertexBufferRDG<FHairCardsVoxelDensityFormat>(GraphBuilder, RenderData.VoxelDensity, CardVoxel.DensityBuffer, TEXT("Hair.CardsProcedural_VoxelDensityBuffer"), EHairResourceUsageType::Static);
	InternalCreateVertexBufferRDG<FHairCardsVoxelTangentFormat>(GraphBuilder, RenderData.VoxelTangent, CardVoxel.TangentBuffer, TEXT("Hair.CardsProcedural_VoxelTangentBuffer"), EHairResourceUsageType::Static);
	InternalCreateVertexBufferRDG<FHairCardsVoxelTangentFormat>(GraphBuilder, RenderData.VoxelNormal, CardVoxel.NormalBuffer, TEXT("Hair.CardsProcedural_VoxelNormalBuffer"), EHairResourceUsageType::Static);

	InternalCreateVertexBufferRDG<FHairCardsStrandsPositionFormat>(GraphBuilder, RenderData.CardsStrandsPositions, CardsStrandsPositions, TEXT("Hair.CardsProcedural_CardsStrandsPositions"), EHairResourceUsageType::Static);
	InternalCreateVertexBufferRDG<FHairCardsStrandsAttributeFormat>(GraphBuilder, RenderData.CardsStrandsAttributes, CardsStrandsAttributes, TEXT("Hair.CardsProcedural_CardsStrandsAttributes"), EHairResourceUsageType::Static);
}

void FHairCardsProceduralResource::InternalRelease()
{
	AtlasRectBuffer.Release();
	LengthBuffer.Release();

	CardItToClusterBuffer.Release();
	ClusterIdToVerticesBuffer.Release();
	ClusterBoundBuffer.Release();
	CardsStrandsPositions.Release();
	CardsStrandsAttributes.Release();

	CardVoxel.DensityBuffer.Release();
	CardVoxel.TangentBuffer.Release();
	CardVoxel.NormalBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairCardsDeformedResource::FHairCardsDeformedResource(const FHairCardsBulkData& InBulkData, bool bInInitializedData) :
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	BulkData(InBulkData), bInitializedData(bInInitializedData)
{}

void FHairCardsDeformedResource::InternalAllocate(FRDGBuilder& GraphBuilder)
{
	if (bInitializedData)
	{
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, BulkData.Positions, DeformedPositionBuffer[0], TEXT("Hair.CardsDeformed_Position0"), EHairResourceUsageType::Dynamic);
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder,BulkData.Positions, DeformedPositionBuffer[1], TEXT("Hair.CardsDeformed_Position1"), EHairResourceUsageType::Dynamic);
	}
	else
	{
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, BulkData.GetNumVertices(), DeformedPositionBuffer[0], TEXT("Hair.CardsDeformed_Position0"), EHairResourceUsageType::Dynamic);
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, BulkData.GetNumVertices(), DeformedPositionBuffer[1], TEXT("Hair.CardsDeformed_Position1"), EHairResourceUsageType::Dynamic);
	}
}

void FHairCardsDeformedResource::InternalRelease()
{
	DeformedPositionBuffer[0].Release();
	DeformedPositionBuffer[1].Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairMeshesRestResource::FHairMeshesRestResource(const FHairMeshesBulkData& InBulkData) :
	FHairCommonResource(EHairStrandsAllocationType::Immediate, false),
	RestPositionBuffer(),
	IndexBuffer(InBulkData.Indices),
	NormalsBuffer(),
	UVsBuffer(),
	BulkData(InBulkData)
{
	check(BulkData.GetNumVertices() > 0);
	check(IndexBuffer.Indices.Num() > 0);
}

void FHairMeshesRestResource::InternalAllocate()
{
	// These resources are kept as regular (i.e., non-RDG resources) as they need to be bound at the input assembly stage by the Vertex declaraction which requires FVertexBuffer type
	CreateBuffer<FHairCardsPositionFormat>(BulkData.Positions, RestPositionBuffer, TEXT("Hair.MeshesRest_Positions"));
	CreateBuffer<FHairCardsNormalFormat>(BulkData.Normals, NormalsBuffer, TEXT("Hair.MeshesRest_Normals"));
	CreateBuffer<FHairCardsUVFormat>(BulkData.UVs, UVsBuffer, TEXT("Hair.MeshesRest_UVs"));
}

void FHairMeshesRestResource::InternalRelease()
{

}

void FHairMeshesRestResource::InitResource()
{
	FRenderResource::InitResource();
	IndexBuffer.InitResource();
	RestPositionBuffer.InitResource();
	NormalsBuffer.InitResource();
	UVsBuffer.InitResource();
}

void FHairMeshesRestResource::ReleaseResource()
{
	FRenderResource::ReleaseResource();
	IndexBuffer.ReleaseResource();
	RestPositionBuffer.ReleaseResource();
	NormalsBuffer.ReleaseResource();
	UVsBuffer.ReleaseResource();

}

/////////////////////////////////////////////////////////////////////////////////////////

FHairMeshesDeformedResource::FHairMeshesDeformedResource(const FHairMeshesBulkData& InBulkData, bool bInInitializedData) :
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	BulkData(InBulkData), bInitializedData(bInInitializedData)
{}

void FHairMeshesDeformedResource::InternalAllocate(FRDGBuilder& GraphBuilder)
{
	if (bInitializedData)
	{
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, BulkData.Positions, DeformedPositionBuffer[0], TEXT("Hair.MeshesDeformed_Positions0"), EHairResourceUsageType::Dynamic);
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, BulkData.Positions, DeformedPositionBuffer[1], TEXT("Hair.MeshesDeformed_Positions1"), EHairResourceUsageType::Dynamic);
	}
	else
	{
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, BulkData.GetNumVertices(), DeformedPositionBuffer[0], TEXT("Hair.MeshesDeformed_Positions0"), EHairResourceUsageType::Dynamic);
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, BulkData.GetNumVertices(), DeformedPositionBuffer[1], TEXT("Hair.MeshesDeformed_Positions1"), EHairResourceUsageType::Dynamic);
	}
}

void FHairMeshesDeformedResource::InternalRelease()
{
	DeformedPositionBuffer[0].Release();
	DeformedPositionBuffer[1].Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsRestResource::FHairStrandsRestResource(FHairStrandsBulkData& InBulkData, EHairStrandsResourcesType InCurveType) :
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	PositionBuffer(), Attribute0Buffer(), Attribute1Buffer(), MaterialBuffer(), BulkData(InBulkData), CurveType(InCurveType)
{}

void FHairStrandsRestResource::InternalAllocate(FRDGBuilder& GraphBuilder)
{
	const uint32 PointCount = BulkData.PointCount;

	// 1. Lock data, which force the loading data from files (on non-editor build/cooked data). These data are then uploaded to the GPU
	// 2. A local copy is done by the buffer uploader. This copy is discarded once the uploading is done.
	InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsPositionFormat>(GraphBuilder, BulkData.Positions, PointCount, PositionBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRest_PositionBuffer), EHairResourceUsageType::Static);
	InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsAttribute0Format>(GraphBuilder, BulkData.Attributes0, PointCount, Attribute0Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRest_Attribute0Buffer), EHairResourceUsageType::Static);

	if (!!(BulkData.Flags & FHairStrandsBulkData::DataFlags_HasUDIMData))
	{
		InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsAttribute1Format>(GraphBuilder, BulkData.Attributes1, PointCount, Attribute1Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRest_Attribute1Buffer), EHairResourceUsageType::Static);
	}

	if (!!(BulkData.Flags & FHairStrandsBulkData::DataFlags_HasMaterialData))
	{
		InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsMaterialFormat>(GraphBuilder, BulkData.Materials, PointCount, MaterialBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRest_MaterialBuffer), EHairResourceUsageType::Static);
	}

	TArray<FVector4> RestOffset;
	RestOffset.Add(BulkData.GetPositionOffset());
	InternalCreateVertexBufferRDG<FHairStrandsPositionOffsetFormat>(GraphBuilder, RestOffset, PositionOffsetBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRest_PositionOffsetBuffer), EHairResourceUsageType::Static, ERDGInitialDataFlags::None);
}

void AddHairTangentPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	uint32 VertexCount,
	FHairGroupPublicData* HairGroupPublicData,
	FRDGBufferSRVRef PositionBuffer,
	FRDGImportedBuffer OutTangentBuffer);

FRDGExternalBuffer FHairStrandsRestResource::GetTangentBuffer(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap)
{
	// Lazy allocation and update
	if (TangentBuffer.Buffer == nullptr)
	{
		InternalCreateVertexBufferRDG<FHairStrandsTangentFormat>(GraphBuilder, BulkData.PointCount * FHairStrandsTangentFormat::ComponentCount, TangentBuffer, TEXT("Hair.StrandsRest_TangentBuffer"), EHairResourceUsageType::Dynamic);

		AddHairTangentPass(
			GraphBuilder,
			ShaderMap,
			BulkData.PointCount,
			nullptr,
			RegisterAsSRV(GraphBuilder, PositionBuffer),
			Register(GraphBuilder, TangentBuffer, ERDGImportedBufferFlags::CreateUAV));
	}

	return TangentBuffer;
}

void FHairStrandsRestResource::InternalRelease()
{
	PositionBuffer.Release();
	PositionOffsetBuffer.Release();
	Attribute0Buffer.Release();
	Attribute1Buffer.Release();
	MaterialBuffer.Release();
	TangentBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsDeformedResource::FHairStrandsDeformedResource(FHairStrandsBulkData& InBulkData, bool bInInitializedData, EHairStrandsResourcesType InCurveType) :
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	BulkData(InBulkData), bInitializedData(bInInitializedData), CurveType(InCurveType)
{
	GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Current)  = BulkData.GetPositionOffset();
	GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Previous) = BulkData.GetPositionOffset();
}

void FHairStrandsDeformedResource::InternalAllocate(FRDGBuilder& GraphBuilder)
{
	const uint32 PointCount = BulkData.PointCount;

	if (bInitializedData)
	{
		// 1. Lock data, which force the loading data from files (on non-editor build/cooked data). These data are then uploaded to the GPU
		// 2. A local copy is done by the buffer uploader. This copy is discarded once the uploading is done.
		InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsPositionFormat>(GraphBuilder, BulkData.Positions, PointCount, DeformedPositionBuffer[0], HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsDeformed_DeformedPositionBuffer0), EHairResourceUsageType::Dynamic);
		InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsPositionFormat>(GraphBuilder, BulkData.Positions, PointCount, DeformedPositionBuffer[1], HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsDeformed_DeformedPositionBuffer1), EHairResourceUsageType::Dynamic);
	}
	else
	{
		InternalCreateVertexBufferRDG<FHairStrandsPositionFormat>(GraphBuilder, BulkData.PointCount, DeformedPositionBuffer[0], HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsDeformed_DeformedPositionBuffer0), EHairResourceUsageType::Dynamic);
		InternalCreateVertexBufferRDG<FHairStrandsPositionFormat>(GraphBuilder, BulkData.PointCount, DeformedPositionBuffer[1], HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsDeformed_DeformedPositionBuffer1), EHairResourceUsageType::Dynamic);
	}
	InternalCreateVertexBufferRDG<FHairStrandsTangentFormat>(GraphBuilder, BulkData.PointCount * FHairStrandsTangentFormat::ComponentCount, TangentBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsDeformed_TangentBuffer), EHairResourceUsageType::Dynamic);

	TArray<FVector4> DefaultOffsets;
	DefaultOffsets.Add(BulkData.GetPositionOffset());
	InternalCreateVertexBufferRDG<FHairStrandsPositionOffsetFormat>(GraphBuilder, DefaultOffsets, DeformedOffsetBuffer[0], HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsDeformed_DeformedOffsetBuffer0), EHairResourceUsageType::Dynamic, ERDGInitialDataFlags::None);
	InternalCreateVertexBufferRDG<FHairStrandsPositionOffsetFormat>(GraphBuilder, DefaultOffsets, DeformedOffsetBuffer[1], HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsDeformed_DeformedOffsetBuffer1), EHairResourceUsageType::Dynamic, ERDGInitialDataFlags::None);

}

void FHairStrandsDeformedResource::InternalRelease()
{
	DeformedPositionBuffer[0].Release();
	DeformedPositionBuffer[1].Release();
	TangentBuffer.Release();

	DeformedOffsetBuffer[0].Release();
	DeformedOffsetBuffer[1].Release();
}

/////////////////////////////////////////////////////////////////////////////////////////
// Cluster culling data

FHairStrandsClusterCullingData::FHairStrandsClusterCullingData()
{

}

void FHairStrandsClusterCullingData::Reset()
{
	*this = FHairStrandsClusterCullingData();
}

/////////////////////////////////////////////////////////////////////////////////////////
// Cluster culling bulk data
FHairStrandsClusterCullingBulkData::FHairStrandsClusterCullingBulkData()
{

}

void FHairStrandsClusterCullingBulkData::Reset()
{
	ClusterCount = 0;
	ClusterLODCount = 0;
	VertexCount = 0;
	VertexLODCount = 0;

	LODVisibility.Empty();;
	CPULODScreenSize.Empty();

	ClusterLODInfos.RemoveBulkData();
	VertexToClusterIds.RemoveBulkData();
	ClusterVertexIds.RemoveBulkData();
	PackedClusterInfos.RemoveBulkData();
}

void FHairStrandsClusterCullingBulkData::Serialize(FArchive& Ar, UObject* Owner)
{
	Ar << ClusterCount;
	Ar << ClusterLODCount;
	Ar << VertexCount;
	Ar << VertexLODCount;
	Ar << LODVisibility;
	Ar << CPULODScreenSize;

	const int32 ChunkIndex = 0;
	bool bAttemptFileMapping = false;

	const uint32 BulkFlags = BULKDATA_Force_NOT_InlinePayload | BULKDATA_SerializeCompressed;
	ClusterLODInfos.SetBulkDataFlags(BulkFlags);
	VertexToClusterIds.SetBulkDataFlags(BulkFlags);
	ClusterVertexIds.SetBulkDataFlags(BulkFlags);
	PackedClusterInfos.SetBulkDataFlags(BulkFlags);

	ClusterLODInfos.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	VertexToClusterIds.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	ClusterVertexIds.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	PackedClusterInfos.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
}

/////////////////////////////////////////////////////////////////////////////////////////
// Cluster culling resources
FHairStrandsClusterCullingResource::FHairStrandsClusterCullingResource(FHairStrandsClusterCullingBulkData& InBulkData): 
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	BulkData(InBulkData)
{

}

void FHairStrandsClusterCullingResource::InternalAllocate(FRDGBuilder& GraphBuilder)
{
	InternalCreateStructuredBufferRDG_FromBulkData<FHairClusterInfoFormat>(GraphBuilder, BulkData.PackedClusterInfos, BulkData.ClusterCount, ClusterInfoBuffer, TEXT("Hair.StrandsClusterCulling_ClusterInfoBuffer"), EHairResourceUsageType::Static);
	InternalCreateStructuredBufferRDG_FromBulkData<FHairClusterLODInfoFormat>(GraphBuilder, BulkData.ClusterLODInfos, BulkData.ClusterLODCount, ClusterLODInfoBuffer, TEXT("Hair.StrandsClusterCulling_ClusterLODInfoBuffer"), EHairResourceUsageType::Static);

	InternalCreateVertexBufferRDG_FromBulkData<FHairClusterIndexFormat>(GraphBuilder, BulkData.VertexToClusterIds, BulkData.VertexCount, VertexToClusterIdBuffer, TEXT("Hair.StrandsClusterCulling_VertexToClusterIds"), EHairResourceUsageType::Static);
	InternalCreateVertexBufferRDG_FromBulkData<FHairClusterIndexFormat>(GraphBuilder, BulkData.ClusterVertexIds, BulkData.VertexLODCount, ClusterVertexIdBuffer, TEXT("Hair.StrandsClusterCulling_ClusterVertexIds"), EHairResourceUsageType::Static);
}

void FHairStrandsClusterCullingResource::InternalRelease()
{
	ClusterInfoBuffer.Release();
	ClusterLODInfoBuffer.Release();
	ClusterVertexIdBuffer.Release();
	VertexToClusterIdBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsRestRootResource::FHairStrandsRestRootResource(FHairStrandsRootBulkData& InBulkData, EHairStrandsResourcesType InCurveType) :
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	BulkData(InBulkData), CurveType(InCurveType)
{
	PopulateFromRootData();
}

void FHairStrandsRestRootResource::PopulateFromRootData()
{
	uint32 LODIndex = 0;
	LODs.Reserve(BulkData.MeshProjectionLODs.Num());
	for (const FHairStrandsRootBulkData::FMeshProjectionLOD& MeshProjectionLOD : BulkData.MeshProjectionLODs)
	{
		FLOD& LOD = LODs.AddDefaulted_GetRef();

		LOD.LODIndex = MeshProjectionLOD.LODIndex;
		LOD.Status = FLOD::EStatus::Invalid;
		LOD.SampleCount = MeshProjectionLOD.SampleCount;
	}
}

void FHairStrandsRestRootResource::InternalAllocate(FRDGBuilder& GraphBuilder)
{
	// Once empty, the MeshProjectionLODsneeds to be repopulate as it might be re-initialized. 
	// E.g., when a resource is updated, it is first released, then re-init. 
	if (LODs.IsEmpty())
	{
		PopulateFromRootData();
	}

	if (BulkData.PointCount > 0)
	{
		InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsIndexFormat>(GraphBuilder, BulkData.VertexToCurveIndexBuffer, BulkData.PointCount, VertexToCurveIndexBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_VertexToCurveIndexBuffer), EHairResourceUsageType::Static);
	}
}

void FHairStrandsRestRootResource::InternalAllocateLOD(FRDGBuilder& GraphBuilder, int32 LODIndex)
{
	// Sanity check ti insure that the 'common' part of FHairStrandsRestRootResource is already initialized
	check(bIsInitialized);
	check(BulkData.PointCount > 0);
	check(LODs.Num() == BulkData.MeshProjectionLODs.Num());
	if (LODIndex >= 0 && LODIndex < LODs.Num())
	{
		FLOD& GPUData = LODs[LODIndex];
		const bool bIsLODInitialized = GPUData.Status == FLOD::EStatus::Completed || GPUData.Status == FLOD::EStatus::Initialized;
		if (bIsLODInitialized)
		{
			return;
		}

		FHairStrandsRootBulkData::FMeshProjectionLOD& CPUData = BulkData.MeshProjectionLODs[LODIndex];
		const bool bHasValidCPUData = CPUData.RootTriangleBarycentricBuffer.GetBulkDataSize() > 0;
		if (bHasValidCPUData)
		{
			GPUData.Status = FLOD::EStatus::Completed;

			InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsCurveTriangleBarycentricFormat>(GraphBuilder, CPUData.RootTriangleBarycentricBuffer, BulkData.RootCount, GPUData.RootTriangleBarycentricBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RootTriangleBarycentricBuffer), EHairResourceUsageType::Static);
			InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsCurveTriangleIndexFormat>(GraphBuilder, CPUData.RootTriangleIndexBuffer, BulkData.RootCount, GPUData.RootTriangleIndexBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RootTriangleIndexBuffer), EHairResourceUsageType::Static);

			InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, CPUData.RestRootTrianglePosition0Buffer, BulkData.RootCount, GPUData.RestRootTrianglePosition0Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RestRootTrianglePosition0Buffer), EHairResourceUsageType::Static);
			InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, CPUData.RestRootTrianglePosition1Buffer, BulkData.RootCount, GPUData.RestRootTrianglePosition1Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RestRootTrianglePosition1Buffer), EHairResourceUsageType::Static);
			InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, CPUData.RestRootTrianglePosition2Buffer, BulkData.RootCount, GPUData.RestRootTrianglePosition2Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RestRootTrianglePosition2Buffer), EHairResourceUsageType::Static);
		}
		else
		{
			GPUData.Status = FLOD::EStatus::Initialized;
			
			InternalCreateVertexBufferRDG<FHairStrandsCurveTriangleBarycentricFormat>(GraphBuilder, BulkData.RootCount, GPUData.RootTriangleBarycentricBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RootTriangleBarycentricBuffer), EHairResourceUsageType::Dynamic);
			InternalCreateVertexBufferRDG<FHairStrandsCurveTriangleIndexFormat>(GraphBuilder, BulkData.RootCount, GPUData.RootTriangleIndexBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RootTriangleIndexBuffer), EHairResourceUsageType::Dynamic);
			
			// Create buffers. Initialization will be done by render passes
			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, BulkData.RootCount, GPUData.RestRootTrianglePosition0Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RestRootTrianglePosition0Buffer), EHairResourceUsageType::Dynamic);
			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, BulkData.RootCount, GPUData.RestRootTrianglePosition1Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RestRootTrianglePosition1Buffer), EHairResourceUsageType::Dynamic);
			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, BulkData.RootCount, GPUData.RestRootTrianglePosition2Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RestRootTrianglePosition2Buffer), EHairResourceUsageType::Dynamic);
		}

		GPUData.SampleCount = CPUData.SampleCount;
		const bool bHasValidCPUWeights = CPUData.MeshSampleIndicesBuffer.GetBulkDataSize() > 0;
		if (bHasValidCPUWeights)
		{
			const uint32 InteroplationWeightCount = CPUData.MeshInterpolationWeightsBuffer.GetBulkDataSize() / sizeof(FHairStrandsWeightFormat::Type);
			InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsWeightFormat>(GraphBuilder, CPUData.MeshInterpolationWeightsBuffer, InteroplationWeightCount, GPUData.MeshInterpolationWeightsBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_MeshInterpolationWeightsBuffer), EHairResourceUsageType::Static);
			InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsIndexFormat>(GraphBuilder, CPUData.MeshSampleIndicesBuffer, CPUData.SampleCount, GPUData.MeshSampleIndicesBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_MeshSampleIndicesBuffer), EHairResourceUsageType::Static);
			InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, CPUData.RestSamplePositionsBuffer, CPUData.SampleCount, GPUData.RestSamplePositionsBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RestSamplePositionsBuffer), EHairResourceUsageType::Static);
		}
		else
		{
			// TODO: do not allocate these resources, since they won't be used
			InternalCreateVertexBufferRDG<FHairStrandsWeightFormat>(GraphBuilder, (CPUData.SampleCount+4) * (CPUData.SampleCount+4), GPUData.MeshInterpolationWeightsBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_MeshInterpolationWeightsBuffer), EHairResourceUsageType::Dynamic);
			InternalCreateVertexBufferRDG<FHairStrandsIndexFormat>(GraphBuilder, CPUData.SampleCount, GPUData.MeshSampleIndicesBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_MeshSampleIndicesBuffer), EHairResourceUsageType::Dynamic);
			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, CPUData.SampleCount, GPUData.RestSamplePositionsBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRestRoot_RestSamplePositionsBuffer), EHairResourceUsageType::Dynamic);
		}
	}
}

void FHairStrandsRestRootResource::InternalRelease()
{
	VertexToCurveIndexBuffer.Release();
	
	for (FLOD& GPUData : LODs)
	{
		GPUData.Status = FLOD::EStatus::Invalid;
		GPUData.RootTriangleIndexBuffer.Release();
		GPUData.RootTriangleBarycentricBuffer.Release();
		GPUData.RestRootTrianglePosition0Buffer.Release();
		GPUData.RestRootTrianglePosition1Buffer.Release();
		GPUData.RestRootTrianglePosition2Buffer.Release();
		GPUData.SampleCount = 0;
		GPUData.MeshInterpolationWeightsBuffer.Release();
		GPUData.MeshSampleIndicesBuffer.Release();
		GPUData.RestSamplePositionsBuffer.Release();
	}
	LODs.Empty();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsDeformedRootResource::FHairStrandsDeformedRootResource(EHairStrandsResourcesType InCurveType):
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	CurveType(InCurveType)
{

}

FHairStrandsDeformedRootResource::FHairStrandsDeformedRootResource(const FHairStrandsRestRootResource* InRestResources, EHairStrandsResourcesType InCurveType):
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	CurveType(InCurveType)
{
	check(InRestResources);
	uint32 LODIndex = 0;
	RootCount = InRestResources->BulkData.RootCount;
	LODs.Reserve(InRestResources->LODs.Num());
	for (const FHairStrandsRestRootResource::FLOD& InLOD : InRestResources->LODs)
	{
		FLOD& LOD = LODs.AddDefaulted_GetRef();

		LOD.Status = FLOD::EStatus::Invalid;
		LOD.LODIndex = InLOD.LODIndex;
		LOD.SampleCount = InLOD.SampleCount;
	}
}

void FHairStrandsDeformedRootResource::InternalAllocateLOD(FRDGBuilder& GraphBuilder, int32 LODIndex)
{
	if (RootCount > 0 && LODIndex >= 0 && LODIndex < LODs.Num())
	{
		FLOD& LOD = LODs[LODIndex];
		if (LOD.Status == FLOD::EStatus::Invalid)
		{		
			LOD.Status = FLOD::EStatus::Initialized;
			if (LOD.SampleCount > 0)
			{
				InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, LOD.SampleCount, LOD.DeformedSamplePositionsBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRootDeformed_DeformedSamplePositionsBuffer), EHairResourceUsageType::Dynamic);
				InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, LOD.SampleCount + 4, LOD.MeshSampleWeightsBuffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRootDeformed_MeshSampleWeightsBuffer), EHairResourceUsageType::Dynamic);
			}

			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, RootCount, LOD.DeformedRootTrianglePosition0Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRootDeformed_DeformedRootTrianglePosition0Buffer), EHairResourceUsageType::Dynamic);
			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, RootCount, LOD.DeformedRootTrianglePosition1Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRootDeformed_DeformedRootTrianglePosition1Buffer), EHairResourceUsageType::Dynamic);
			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, RootCount, LOD.DeformedRootTrianglePosition2Buffer, HAIRSTRANDS_RESOUCE_NAME(CurveType, Hair.StrandsRootDeformed_DeformedRootTrianglePosition2Buffer), EHairResourceUsageType::Dynamic);
		}
	}
}

void FHairStrandsDeformedRootResource::InternalRelease()
{
	for (FLOD& GPUData : LODs)
	{
		GPUData.Status = FLOD::EStatus::Invalid;
		GPUData.DeformedRootTrianglePosition0Buffer.Release();
		GPUData.DeformedRootTrianglePosition1Buffer.Release();
		GPUData.DeformedRootTrianglePosition2Buffer.Release();
		GPUData.DeformedSamplePositionsBuffer.Release();
		GPUData.MeshSampleWeightsBuffer.Release();
	}
	LODs.Empty();
}

/////////////////////////////////////////////////////////////////////////////////////////
// Root bulk data
FHairStrandsRootBulkData::FHairStrandsRootBulkData()
{

}

bool FHairStrandsRootBulkData::HasProjectionData() const
{
	bool bIsValid = MeshProjectionLODs.Num() > 0;
	bIsValid = RootCount > 0;
	for (const FMeshProjectionLOD& LOD : MeshProjectionLODs)
	{
		bIsValid = bIsValid && LOD.LODIndex > 0;
		if (!bIsValid) break;
	}
	return bIsValid;
}

const TArray<uint32>& FHairStrandsRootBulkData::GetValidSectionIndices(int32 LODIndex) const
{
	check(LODIndex >= 0 && LODIndex < MeshProjectionLODs.Num());
	return MeshProjectionLODs[LODIndex].ValidSectionIndices;
}

static void InternalSerialize(FArchive& Ar, UObject* Owner, FHairStrandsRootBulkData::FMeshProjectionLOD& LOD)
{
	const int32 ChunkIndex = 0;
	bool bAttemptFileMapping = false;

	const uint32 BulkFlags = BULKDATA_Force_NOT_InlinePayload | BULKDATA_SerializeCompressed;
	LOD.RootTriangleIndexBuffer.SetBulkDataFlags(BulkFlags);
	LOD.RootTriangleBarycentricBuffer.SetBulkDataFlags(BulkFlags);
	LOD.RestRootTrianglePosition0Buffer.SetBulkDataFlags(BulkFlags);
	LOD.RestRootTrianglePosition1Buffer.SetBulkDataFlags(BulkFlags);
	LOD.RestRootTrianglePosition2Buffer.SetBulkDataFlags(BulkFlags);

	LOD.MeshInterpolationWeightsBuffer.SetBulkDataFlags(BulkFlags);
	LOD.MeshSampleIndicesBuffer.SetBulkDataFlags(BulkFlags);
	LOD.RestSamplePositionsBuffer.SetBulkDataFlags(BulkFlags);

	Ar << LOD.LODIndex;
	LOD.RootTriangleIndexBuffer.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	LOD.RootTriangleBarycentricBuffer.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	LOD.RestRootTrianglePosition0Buffer.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	LOD.RestRootTrianglePosition1Buffer.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	LOD.RestRootTrianglePosition2Buffer.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);

	Ar << LOD.SampleCount;
	LOD.MeshInterpolationWeightsBuffer.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	LOD.MeshSampleIndicesBuffer.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	LOD.RestSamplePositionsBuffer.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
	LOD.ValidSectionIndices.BulkSerialize(Ar);
}

static void InternalSerialize(FArchive& Ar, UObject* Owner, TArray<FHairStrandsRootBulkData::FMeshProjectionLOD>& LODs)
{
	uint32 LODCount = LODs.Num();
	Ar << LODCount;
	if (Ar.IsLoading())
	{
		LODs.SetNum(LODCount);
	}
	for (uint32 LODIt = 0; LODIt < LODCount; ++LODIt)
	{
		InternalSerialize(Ar, Owner, LODs[LODIt]);
	}
}

void FHairStrandsRootBulkData::Serialize(FArchive& Ar, UObject* Owner)
{
	const int32 ChunkIndex = 0;
	bool bAttemptFileMapping = false;

	const uint32 BulkFlags = BULKDATA_Force_NOT_InlinePayload | BULKDATA_SerializeCompressed;
	VertexToCurveIndexBuffer.SetBulkDataFlags(BulkFlags);

	if (!Ar.IsObjectReferenceCollector())
	{
		Ar << RootCount;
		Ar << PointCount;
		VertexToCurveIndexBuffer.Serialize(Ar, Owner, ChunkIndex, bAttemptFileMapping);
		InternalSerialize(Ar, Owner, MeshProjectionLODs);
	}
}

void FHairStrandsRootBulkData::Reset()
{
	RootCount = 0;
	VertexToCurveIndexBuffer.RemoveBulkData();
	for (FMeshProjectionLOD& LOD : MeshProjectionLODs)
	{
		LOD.RootTriangleIndexBuffer.RemoveBulkData();
		LOD.RootTriangleBarycentricBuffer.RemoveBulkData();
		LOD.RestRootTrianglePosition0Buffer.RemoveBulkData();
		LOD.RestRootTrianglePosition1Buffer.RemoveBulkData();
		LOD.RestRootTrianglePosition2Buffer.RemoveBulkData();
		LOD.MeshInterpolationWeightsBuffer.RemoveBulkData();
		LOD.MeshSampleIndicesBuffer.RemoveBulkData();
		LOD.RestSamplePositionsBuffer.RemoveBulkData();
		LOD.ValidSectionIndices.Empty();
	}
	MeshProjectionLODs.Empty();
}

/////////////////////////////////////////////////////////////////////////////////////////
// Root data
FHairStrandsRootData::FHairStrandsRootData()
{

}

bool FHairStrandsRootData::HasProjectionData() const
{
	bool bIsValid = MeshProjectionLODs.Num() > 0;
	for (const FMeshProjectionLOD& LOD : MeshProjectionLODs)
	{
		const bool bHasValidCPUData = LOD.RootTriangleBarycentricBuffer.Num() > 0;
		if (bHasValidCPUData)
		{
			bIsValid = bIsValid && LOD.RootTriangleBarycentricBuffer.Num() > 0;
			bIsValid = bIsValid && LOD.RootTriangleIndexBuffer.Num() > 0;
			bIsValid = bIsValid && LOD.RestRootTrianglePosition0Buffer.Num() > 0;
			bIsValid = bIsValid && LOD.RestRootTrianglePosition1Buffer.Num() > 0;
			bIsValid = bIsValid && LOD.RestRootTrianglePosition2Buffer.Num() > 0;

			if (!bIsValid) break;
		}
	}

	return bIsValid;
}

void FHairStrandsRootData::Reset()
{
	RootCount = 0;
	VertexToCurveIndexBuffer.Empty();
	MeshProjectionLODs.Empty();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsInterpolationResource::FHairStrandsInterpolationResource(FHairStrandsInterpolationBulkData& InBulkData) :
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	InterpolationBuffer(), Interpolation0Buffer(), Interpolation1Buffer(), BulkData(InBulkData)
{
}

void FHairStrandsInterpolationResource::InternalAllocate(FRDGBuilder& GraphBuilder)
{
	const bool bUseSingleGuide = !!(BulkData.Flags & FHairStrandsInterpolationBulkData::DataFlags_HasSingleGuideData);
	if (bUseSingleGuide)
	{
		InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsInterpolationFormat>(GraphBuilder, BulkData.Interpolation, BulkData.PointCount, InterpolationBuffer, TEXT("Hair.StrandsInterpolation_InterpolationBuffer"), EHairResourceUsageType::Static);
	}
	else
	{
		InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsInterpolation0Format>(GraphBuilder, BulkData.Interpolation0, BulkData.PointCount, Interpolation0Buffer, TEXT("Hair.StrandsInterpolation_Interpolation0Buffer"), EHairResourceUsageType::Static);
		InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsInterpolation1Format>(GraphBuilder, BulkData.Interpolation1, BulkData.PointCount, Interpolation1Buffer, TEXT("Hair.StrandsInterpolation_Interpolation1Buffer"), EHairResourceUsageType::Static);
	}
	InternalCreateVertexBufferRDG_FromBulkData<FHairStrandsRootIndexFormat>(GraphBuilder, BulkData.SimRootPointIndex, BulkData.SimPointCount, SimRootPointIndexBuffer, TEXT("Hair.StrandsInterpolation_SimRootPointIndex"), EHairResourceUsageType::Static);
}

void FHairStrandsInterpolationResource::InternalRelease()
{
	InterpolationBuffer.Release();
	Interpolation0Buffer.Release();
	Interpolation1Buffer.Release();
	SimRootPointIndexBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////
void FHairCardsInterpolationDatas::SetNum(const uint32 NumPoints)
{
	PointsSimCurvesIndex.SetNum(NumPoints);
	PointsSimCurvesVertexIndex.SetNum(NumPoints);
	PointsSimCurvesVertexLerp.SetNum(NumPoints);
}

void FHairCardsInterpolationDatas::Reset()
{
	PointsSimCurvesIndex.Empty();
	PointsSimCurvesVertexIndex.Empty();
	PointsSimCurvesVertexLerp.Empty();
}

void FHairCardsInterpolationBulkData::Serialize(FArchive& Ar)
{
	Ar << Interpolation;
}

FHairCardsInterpolationResource::FHairCardsInterpolationResource(FHairCardsInterpolationBulkData& InBulkData) :
	FHairCommonResource(EHairStrandsAllocationType::Immediate),
	InterpolationBuffer(), BulkData(InBulkData)
{
}

void FHairCardsInterpolationResource::InternalAllocate(FRDGBuilder& GraphBuilder)
{
	InternalCreateVertexBufferRDG<FHairCardsInterpolationFormat>(GraphBuilder, BulkData.Interpolation, InterpolationBuffer, TEXT("Hair.CardsInterpolation_InterpolationBuffer"), EHairResourceUsageType::Static);
}

void FHairCardsInterpolationResource::InternalRelease()
{
	InterpolationBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

#if RHI_RAYTRACING
// RT geometry is built to for a cross around the fiber.
// 4 triangles per hair vertex => 12 vertices per hair vertex
FHairStrandsRaytracingResource::FHairStrandsRaytracingResource(const FHairStrandsBulkData& InData) :
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	PositionBuffer(), VertexCount(InData.GetNumPoints()*12)  
{}

FHairStrandsRaytracingResource::FHairStrandsRaytracingResource(const FHairCardsBulkData& InData) :
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	PositionBuffer(), VertexCount(InData.GetNumVertices())
{}

FHairStrandsRaytracingResource::FHairStrandsRaytracingResource(const FHairMeshesBulkData& InData) :
	FHairCommonResource(EHairStrandsAllocationType::Deferred),
	PositionBuffer(), VertexCount(InData.GetNumVertices())
{}

void FHairStrandsRaytracingResource::InternalAllocate(FRDGBuilder& GraphBuilder)
{
	InternalCreateVertexBufferRDG<FHairStrandsRaytracingFormat>(GraphBuilder, VertexCount, PositionBuffer, TEXT("Hair.StrandsRaytracing_PositionBuffer"), EHairResourceUsageType::Dynamic);
}

void FHairStrandsRaytracingResource::InternalRelease()
{
	PositionBuffer.Release();
	RayTracingGeometry.ReleaseResource();
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug data

static uint32 ToLinearCoord(const FIntVector& T, const FIntVector& Resolution)
{
	// Morton instead for better locality?
	return T.X + T.Y * Resolution.X + T.Z * Resolution.X * Resolution.Y;
}

static FIntVector ToCoord(const FVector& T, const FIntVector& Resolution, const FVector& MinBound, const float VoxelSize)
{
	const FVector C = (T - MinBound) / VoxelSize;
	return FIntVector(
		FMath::Clamp(FMath::FloorToInt(C.X), 0, Resolution.X - 1),
		FMath::Clamp(FMath::FloorToInt(C.Y), 0, Resolution.Y - 1),
		FMath::Clamp(FMath::FloorToInt(C.Z), 0, Resolution.Z - 1));
}

void CreateHairStrandsDebugDatas(
	const FHairStrandsDatas& InData,
	float WorldVoxelSize,
	FHairStrandsDebugDatas& Out)
{
	const FVector BoundSize = InData.BoundingBox.Max - InData.BoundingBox.Min;
	Out.VoxelDescription.VoxelSize = WorldVoxelSize;
	Out.VoxelDescription.VoxelResolution = FIntVector(FMath::CeilToFloat(BoundSize.X / Out.VoxelDescription.VoxelSize), FMath::CeilToFloat(BoundSize.Y / Out.VoxelDescription.VoxelSize), FMath::CeilToFloat(BoundSize.Z / Out.VoxelDescription.VoxelSize));
	Out.VoxelDescription.VoxelMinBound = InData.BoundingBox.Min;
	Out.VoxelDescription.VoxelMaxBound = FVector(Out.VoxelDescription.VoxelResolution) * Out.VoxelDescription.VoxelSize + InData.BoundingBox.Min;
	Out.VoxelOffsetAndCount.Init(FHairStrandsDebugDatas::FOffsetAndCount(), Out.VoxelDescription.VoxelResolution.X * Out.VoxelDescription.VoxelResolution.Y * Out.VoxelDescription.VoxelResolution.Z);

	uint32 AllocationCount = 0;
	TArray<TArray<FHairStrandsDebugDatas::FVoxel>> TempVoxelData;

	// Fill in voxel (TODO: make it parallel)
	const uint32 CurveCount = InData.StrandsCurves.Num();
	for (uint32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
	{
		const uint32 PointOffset = InData.StrandsCurves.CurvesOffset[CurveIndex];
		const uint32 PointCount = InData.StrandsCurves.CurvesCount[CurveIndex];

		for (uint32 PointIndex = 0; PointIndex < PointCount - 1; ++PointIndex)
		{
			const uint32 Index0 = PointOffset + PointIndex;
			const uint32 Index1 = PointOffset + PointIndex + 1;
			const FVector& P0 = InData.StrandsPoints.PointsPosition[Index0];
			const FVector& P1 = InData.StrandsPoints.PointsPosition[Index1];
			const FVector Segment = P1 - P0;

			const float Length = Segment.Size();
			const uint32 StepCount = FMath::CeilToInt(Length / Out.VoxelDescription.VoxelSize);
			uint32 PrevLinearCoord = ~0;
			for (uint32 StepIt = 0; StepIt < StepCount + 1; ++StepIt)
			{
				const FVector P = P0 + Segment * StepIt / float(StepCount);
				const FIntVector Coord = ToCoord(P, Out.VoxelDescription.VoxelResolution, Out.VoxelDescription.VoxelMinBound, Out.VoxelDescription.VoxelSize);
				const uint32 LinearCoord = ToLinearCoord(Coord, Out.VoxelDescription.VoxelResolution);
				if (LinearCoord != PrevLinearCoord)
				{
					if (Out.VoxelOffsetAndCount[LinearCoord].Count == 0)
					{
						Out.VoxelOffsetAndCount[LinearCoord].Offset = TempVoxelData.Num();
						TempVoxelData.Add(TArray<FHairStrandsDebugDatas::FVoxel>());
					}

					const uint32 Offset = Out.VoxelOffsetAndCount[LinearCoord].Offset;
					const uint32 LocalOffset = Out.VoxelOffsetAndCount[LinearCoord].Count;
					Out.VoxelOffsetAndCount[LinearCoord].Count += 1;
					TempVoxelData[Offset].Add({Index0, Index1});

					PrevLinearCoord = LinearCoord;

					++AllocationCount;
				}
			}
		}

	}

	Out.VoxelData.Reserve(AllocationCount);

	for (int32 Index = 0, Count = Out.VoxelOffsetAndCount.Num(); Index < Count; ++Index)
	{
		const uint32 ArrayIndex = Out.VoxelOffsetAndCount[Index].Offset;
		Out.VoxelOffsetAndCount[Index].Offset = Out.VoxelData.Num();
		if (Out.VoxelOffsetAndCount[Index].Count > 0)
		{
			Out.VoxelData.Append(TempVoxelData[ArrayIndex]);
		}

		// Sanity check
		//check(Out.VoxelOffsetAndCount[Index].Offset + Out.VoxelOffsetAndCount[Index].Count == Out.VoxelData.Num());
	}

	check(Out.VoxelData.Num()>0);
}

void CreateHairStrandsDebugResources(FRDGBuilder& GraphBuilder, const FHairStrandsDebugDatas* In, FHairStrandsDebugDatas::FResources* Out)
{
	check(In);
	check(Out);

	Out->VoxelDescription = In->VoxelDescription;

	FRDGBufferRef VoxelOffsetAndCount = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("HairStrandsDebug_VoxelOffsetAndCount"),
		sizeof(FHairStrandsDebugDatas::FOffsetAndCount),
		In->VoxelOffsetAndCount.Num(),
		In->VoxelOffsetAndCount.GetData(),
		sizeof(FHairStrandsDebugDatas::FOffsetAndCount) * In->VoxelOffsetAndCount.Num());

	FRDGBufferRef VoxelData = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("HairStrandsDebug_VoxelData"),
		sizeof(FHairStrandsDebugDatas::FVoxel),
		In->VoxelData.Num(),
		In->VoxelData.GetData(),
		sizeof(FHairStrandsDebugDatas::FVoxel) * In->VoxelData.Num());

	FRDGResourceAccessFinalizer ResourceAccessFinalizer;
	Out->VoxelOffsetAndCount = ConvertToFinalizedExternalBuffer(GraphBuilder, ResourceAccessFinalizer, VoxelOffsetAndCount);
	Out->VoxelData = ConvertToFinalizedExternalBuffer(GraphBuilder, ResourceAccessFinalizer, VoxelData);
	ResourceAccessFinalizer.Finalize(GraphBuilder);
}
