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

/////////////////////////////////////////////////////////////////////////////////////////
// FRWBuffer utils 
void UploadDataToBuffer(FReadBuffer& OutBuffer, uint32 DataSizeInBytes, const void* InCpuData)
{
	void* BufferData = RHILockVertexBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memcpy(BufferData, InCpuData, DataSizeInBytes);
	RHIUnlockVertexBuffer(OutBuffer.Buffer);
}

void UploadDataToBuffer(FRWBufferStructured& OutBuffer, uint32 DataSizeInBytes, const void* InCpuData)
{
	void* BufferData = RHILockStructuredBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memcpy(BufferData, InCpuData, DataSizeInBytes);
	RHIUnlockStructuredBuffer(OutBuffer.Buffer);
}

template<typename FormatType>
void CreateBuffer(const TArray<typename FormatType::Type>& InData, FRWBuffer& OutBuffer, const TCHAR* DebugName, ERHIAccess InitialAccess = ERHIAccess::SRVMask)
{
	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = FormatType::SizeInByte*DataCount;

	if (DataSizeInBytes == 0) return;

	OutBuffer.Initialize(FormatType::SizeInByte, DataCount, FormatType::Format, InitialAccess, BUF_Static, DebugName);
	void* BufferData = RHILockVertexBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);

	FMemory::Memcpy(BufferData, InData.GetData(), DataSizeInBytes);
	RHIUnlockVertexBuffer(OutBuffer.Buffer);
}

template<typename FormatType>
void CreateBuffer(uint32 InVertexCount, FRWBuffer& OutBuffer, const TCHAR* DebugName)
{
	const uint32 DataCount = InVertexCount;
	const uint32 DataSizeInBytes = FormatType::SizeInByte*DataCount;

	if (DataSizeInBytes == 0) return;

	OutBuffer.Initialize(FormatType::SizeInByte, DataCount, FormatType::Format, ERHIAccess::UAVCompute, BUF_Static, DebugName);
	void* BufferData = RHILockVertexBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memset(BufferData, 0, DataSizeInBytes);
	RHIUnlockVertexBuffer(OutBuffer.Buffer);
}

template<typename FormatType>
void CreateBuffer(const TArray<typename FormatType::Type>& InData, FHairCardsVertexBuffer& OutBuffer, const TCHAR* DebugName, ERHIAccess InitialAccess = ERHIAccess::SRVMask)
{
	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = FormatType::SizeInByte * DataCount;

	if (DataSizeInBytes == 0) return;

	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.DebugName = DebugName;
	CreateInfo.ResourceArray = nullptr;

	OutBuffer.VertexBufferRHI = RHICreateVertexBuffer(DataSizeInBytes, BUF_Static | BUF_ShaderResource, InitialAccess, CreateInfo);

	void* BufferData = RHILockVertexBuffer(OutBuffer.VertexBufferRHI, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memcpy(BufferData, InData.GetData(), DataSizeInBytes);
	RHIUnlockVertexBuffer(OutBuffer.VertexBufferRHI);
	OutBuffer.ShaderResourceViewRHI = RHICreateShaderResourceView(OutBuffer.VertexBufferRHI, FormatType::SizeInByte, FormatType::Format);
}

/////////////////////////////////////////////////////////////////////////////////////////
// RDG buffers utils 
template<typename FormatType>
void InternalCreateVertexBufferRDG(FRDGBuilder& GraphBuilder, const TArray<typename FormatType::Type>& InData, FRDGExternalBuffer& Out, const TCHAR* DebugName, ERDGInitialDataFlags InitialDataFlags= ERDGInitialDataFlags::NoCopy)
{
	FRDGBufferRef Buffer = nullptr;

	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = FormatType::SizeInByte * DataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}

	const FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(FormatType::SizeInByte, InData.Num());
	Buffer = CreateVertexBuffer(
		GraphBuilder,
		DebugName,
		Desc,
		InData.GetData(),
		DataSizeInBytes,
		InitialDataFlags);

	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out, FormatType::Format);
}

template<typename DataType>
void InternalCreateVertexBufferRDG(FRDGBuilder& GraphBuilder, const TArray<DataType>& InData, EPixelFormat Format, FRDGExternalBuffer& Out, const TCHAR* DebugName)
{
	FRDGBufferRef Buffer = nullptr;

	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = sizeof(DataType) * DataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}

	const FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(DataType), InData.Num());
	Buffer = CreateVertexBuffer(
		GraphBuilder,
		DebugName,
		Desc,
		InData.GetData(),
		DataSizeInBytes,
		ERDGInitialDataFlags::NoCopy);

	ConvertToExternalBufferWithViews(GraphBuilder, Buffer, Out, Format);
}

template<typename FormatType>
void InternalCreateVertexBufferRDG(FRDGBuilder& GraphBuilder, uint32 InVertexCount, FRDGExternalBuffer& Out, const TCHAR* DebugName)
{
	const uint32 DataCount = InVertexCount;
	const uint32 DataSizeInBytes = FormatType::SizeInByte * DataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}

	const FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(FormatType::SizeInByte, InVertexCount);
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
		case PF_R16G16B16A16_SNORM:
		case PF_R16G16B16A16_UNORM:
		case PF_R32_FLOAT:
		case PF_R5G6B5_UNORM:
		case PF_R8G8B8A8_SNORM:
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
void InternalCreateStructuredBufferRDG(FRDGBuilder& GraphBuilder, const TArray<FormatType>& InData, uint32 InSizeInByte, FRDGExternalBuffer& Out, const TCHAR* DebugName)
{
	FRDGBufferRef Buffer = nullptr;

	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = InSizeInByte * DataCount;
	if (DataSizeInBytes == 0)
	{
		Out.Buffer = nullptr;
		return;
	}
	// #hair_todo: Create this with a create+clear pass instead?
	Buffer = CreateStructuredBuffer(
		GraphBuilder,
		DebugName,
		InSizeInByte,
		DataCount,
		InData.GetData(),
		DataSizeInBytes,
		ERDGInitialDataFlags::NoCopy);

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
void CreateHairStrandsDebugAttributeBuffer(FRDGExternalBuffer* DebugAttributeBuffer, uint32 VertexCount)
{
	if (VertexCount == 0 || !DebugAttributeBuffer)
		return;

	FRDGExternalBuffer* LocalAttributeBuffer = DebugAttributeBuffer;
	ENQUEUE_RENDER_COMMAND(FHairStrandsCreateDebugAttributeBuffer)(
	[LocalAttributeBuffer, VertexCount](FRHICommandListImmediate& RHICmdList)
	{
		if (GUsingNullRHI) { return; }
		FMemMark Mark(FMemStack::Get());
		FRDGBuilder GraphBuilder(RHICmdList);
		InternalCreateVertexBufferRDG<FHairStrandsAttributeFormat>(GraphBuilder, VertexCount, *LocalAttributeBuffer, TEXT("HairStrands_DebugAttributeBuffer"));
		GraphBuilder.Execute();
	});
}

/////////////////////////////////////////////////////////////////////////////////////////

void FHairCardIndexBuffer::InitRHI()
{
	const uint32 DataSizeInBytes = FHairCardsIndexFormat::SizeInByte * Indices.Num();

	FRHIResourceCreateInfo CreateInfo;
	void* Buffer = nullptr;
	IndexBufferRHI = RHICreateAndLockIndexBuffer(FHairCardsIndexFormat::SizeInByte, DataSizeInBytes, BUF_Static, CreateInfo, Buffer);
	FMemory::Memcpy(Buffer, Indices.GetData(), DataSizeInBytes);
	RHIUnlockIndexBuffer(IndexBufferRHI);
}

FHairCardsRestResource::FHairCardsRestResource(const FHairCardsDatas::FRenderData& InRenderData, uint32 InVertexCount, uint32 InPrimitiveCount) :
	RestPositionBuffer(),
	RestIndexBuffer(InRenderData.Indices),
	VertexCount(InVertexCount),
	PrimitiveCount(InPrimitiveCount),
	NormalsBuffer(),
	UVsBuffer(),
	RenderData(InRenderData)
{

}

void FHairCardsRestResource::InitRHI()
{
	// These resources are kept as regular (i.e., non-RDG resources) as they need to be bound at the input assembly stage by the Vertex declaraction which requires FVertexBuffer type
	CreateBuffer<FHairCardsPositionFormat>(RenderData.Positions, RestPositionBuffer, TEXT("HairCardsRest_PositionBuffer"));
	CreateBuffer<FHairCardsNormalFormat>(RenderData.Normals, NormalsBuffer, TEXT("HairCardsRest_NormalBuffer"));
	CreateBuffer<FHairCardsUVFormat>(RenderData.UVs, UVsBuffer, TEXT("HairCardsRest_UVBuffer"));

	FSamplerStateRHIRef DefaultSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	DepthSampler = DefaultSampler;
	TangentSampler = DefaultSampler;
	CoverageSampler = DefaultSampler;
	AttributeSampler = DefaultSampler;
}

void FHairCardsRestResource::ReleaseRHI()
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

void FHairCardsProceduralResource::InitRHI()
{
	if (GUsingNullRHI) { return; }
	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);

	InternalCreateVertexBufferRDG<FHairCardsAtlasRectFormat>(GraphBuilder, RenderData.CardsRect, AtlasRectBuffer, TEXT("HairCardsProcedural_AtlasRectBuffer"));
	InternalCreateVertexBufferRDG<FHairCardsDimensionFormat>(GraphBuilder, RenderData.CardsLengths, LengthBuffer, TEXT("HairCardsProcedural_LengthBuffer"));

	InternalCreateVertexBufferRDG<FHairCardsOffsetAndCount>(GraphBuilder, RenderData.CardItToCluster, CardItToClusterBuffer, TEXT("HairCardsProcedural_CardItToClusterBuffer"));
	InternalCreateVertexBufferRDG<FHairCardsOffsetAndCount>(GraphBuilder, RenderData.ClusterIdToVertices, ClusterIdToVerticesBuffer, TEXT("HairCardsProcedural_ClusterIdToVerticesBuffer"));
	InternalCreateVertexBufferRDG<FHairCardsBoundsFormat>(GraphBuilder, RenderData.ClusterBounds, ClusterBoundBuffer, TEXT("HairCardsProcedural_ClusterBoundBuffer"));

	InternalCreateVertexBufferRDG<FHairCardsVoxelDensityFormat>(GraphBuilder, RenderData.VoxelDensity, CardVoxel.DensityBuffer, TEXT("HairCardsProcedural_VoxelDensityBuffer"));
	InternalCreateVertexBufferRDG<FHairCardsVoxelTangentFormat>(GraphBuilder, RenderData.VoxelTangent, CardVoxel.TangentBuffer, TEXT("HairCardsProcedural_VoxelTangentBuffer"));
	InternalCreateVertexBufferRDG<FHairCardsVoxelTangentFormat>(GraphBuilder, RenderData.VoxelNormal, CardVoxel.NormalBuffer, TEXT("HairCardsProcedural_VoxelNormalBuffer"));

	InternalCreateVertexBufferRDG<FHairCardsStrandsPositionFormat>(GraphBuilder, RenderData.CardsStrandsPositions, CardsStrandsPositions, TEXT("HairCardsProcedural_CardsStrandsPositions"));
	InternalCreateVertexBufferRDG<FHairCardsStrandsAttributeFormat>(GraphBuilder, RenderData.CardsStrandsAttributes, CardsStrandsAttributes, TEXT("HairCardsProcedural_CardsStrandsAttributes"));

	GraphBuilder.Execute();
}

void FHairCardsProceduralResource::ReleaseRHI()
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

FHairCardsDeformedResource::FHairCardsDeformedResource(const FHairCardsDatas::FRenderData& HairCardsRenderData, bool bInInitializedData, bool bInDynamic) :
	RenderData(HairCardsRenderData), bInitializedData(!bInDynamic || bInInitializedData), bDynamic(bInDynamic)
{}

void FHairCardsDeformedResource::InitRHI()
{
	if (GUsingNullRHI) { return; }
	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);

	const uint32 VertexCount = RenderData.Positions.Num();
	if (bInitializedData)
	{
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, RenderData.Positions, DeformedPositionBuffer[0], TEXT("HairCardsDeformed_Position0"));
		if (bDynamic)
		{
			InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, RenderData.Positions, DeformedPositionBuffer[1], TEXT("HairCardsDeformed_Position1"));
		}
	}
	else
	{
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, VertexCount, DeformedPositionBuffer[0], TEXT("HairCardsDeformed_Position0"));
		if (bDynamic)
		{
			InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, VertexCount, DeformedPositionBuffer[1], TEXT("HairCardsDeformed_Position1"));
		}
	}
	GraphBuilder.Execute();
}

void FHairCardsDeformedResource::ReleaseRHI()
{
	DeformedPositionBuffer[0].Release();
	if (bDynamic)
	{
		DeformedPositionBuffer[1].Release();
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairMeshesRestResource::FHairMeshesRestResource(const FHairMeshesDatas::FRenderData& InRenderData, uint32 InVertexCount, uint32 InPrimitiveCount) :
	RestPositionBuffer(),
	IndexBuffer(InRenderData.Indices),
	VertexCount(InVertexCount),
	PrimitiveCount(InPrimitiveCount),
	NormalsBuffer(),
	UVsBuffer(),
	RenderData(InRenderData)
{
	check(VertexCount > 0);
	check(IndexBuffer.Indices.Num() > 0);
}

void FHairMeshesRestResource::InitRHI()
{
	// These resources are kept as regular (i.e., non-RDG resources) as they need to be bound at the input assembly stage by the Vertex declaraction which requires FVertexBuffer type
	CreateBuffer<FHairCardsPositionFormat>(RenderData.Positions, RestPositionBuffer, TEXT("HairMeshesRest_Positions"));
	CreateBuffer<FHairCardsNormalFormat>(RenderData.Normals, NormalsBuffer, TEXT("HairMeshesRest_Normals"));
	CreateBuffer<FHairCardsUVFormat>(RenderData.UVs, UVsBuffer, TEXT("HairMeshesRest_UVs"));
}

void FHairMeshesRestResource::ReleaseRHI()
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

FHairMeshesDeformedResource::FHairMeshesDeformedResource(const FHairMeshesDatas::FRenderData& HairMeshesData, bool bInInitializedData, bool bInDynamic) :
	RenderData(HairMeshesData), bInitializedData(!bInDynamic || bInInitializedData), bDynamic(bInDynamic)
{}

void FHairMeshesDeformedResource::InitRHI()
{
	if (GUsingNullRHI) { return; }
	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);

	const uint32 VertexCount = RenderData.Positions.Num();
	if (bInitializedData)
	{
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, RenderData.Positions, DeformedPositionBuffer[0], TEXT("HairMeshesDeformed_Positions0"));
		if (bDynamic)
		{
			InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, RenderData.Positions, DeformedPositionBuffer[1], TEXT("HairMeshesDeformed_Positions1"));
		}
	}
	else
	{
		InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, VertexCount, DeformedPositionBuffer[0], TEXT("HairMeshesDeformed_Positions0"));
		if (bDynamic)
		{
			InternalCreateVertexBufferRDG<FHairCardsPositionFormat>(GraphBuilder, VertexCount, DeformedPositionBuffer[1], TEXT("HairMeshesDeformed_Positions1"));
		}
	}

	GraphBuilder.Execute();
}

void FHairMeshesDeformedResource::ReleaseRHI()
{
	DeformedPositionBuffer[0].Release();
	if (bDynamic)
	{
		DeformedPositionBuffer[1].Release();
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsRestResource::FHairStrandsRestResource(const FHairStrandsDatas::FRenderData& HairStrandRenderData, const FVector& InPositionOffset) :
	RestPositionBuffer(), AttributeBuffer(), MaterialBuffer(), PositionOffset(InPositionOffset), RenderData(HairStrandRenderData)
{}

void FHairStrandsRestResource::InitRHI()
{
	if (GUsingNullRHI) { return; }
	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);

	InternalCreateVertexBufferRDG<FHairStrandsPositionFormat>(GraphBuilder, RenderData.Positions, RestPositionBuffer, TEXT("HairStrandsRest_RestPositionBuffer"));
	InternalCreateVertexBufferRDG<FHairStrandsAttributeFormat>(GraphBuilder, RenderData.Attributes, AttributeBuffer, TEXT("HairStrandsRest_AttributeBuffer"));
	InternalCreateVertexBufferRDG<FHairStrandsMaterialFormat>(GraphBuilder, RenderData.Materials, MaterialBuffer, TEXT("HairStrandsRest_MaterialBuffer"));

	GraphBuilder.Execute();
}

void FHairStrandsRestResource::ReleaseRHI()
{
	RestPositionBuffer.Release();
	AttributeBuffer.Release();
	MaterialBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsDeformedResource::FHairStrandsDeformedResource(const FHairStrandsDatas::FRenderData& HairStrandRenderData, bool bInInitializedData, bool bInDynamic, const FVector& InDefaultOffset) :
	RenderData(HairStrandRenderData), bInitializedData(bInInitializedData), bDynamic(bInDynamic), DefaultOffset(InDefaultOffset)
{}

void FHairStrandsDeformedResource::InitRHI()
{
	if (GUsingNullRHI) { return; }
	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);

	const uint32 VertexCount = RenderData.Positions.Num();
	if (bInitializedData)
	{
		InternalCreateVertexBufferRDG<FHairStrandsPositionFormat>(GraphBuilder, RenderData.Positions, DeformedPositionBuffer[0], TEXT("HairStrandsDeformed_DeformedPositionBuffer0")); // , bDynamic ? ERHIAccess::UAVCompute : ERHIAccess::SRVMask
		if (bDynamic)
		{
			InternalCreateVertexBufferRDG<FHairStrandsPositionFormat>(GraphBuilder, RenderData.Positions, DeformedPositionBuffer[1], TEXT("HairStrandsDeformed_DeformedPositionBuffer1")); // , ERHIAccess::UAVCompute
		}
	}
	else
	{
		InternalCreateVertexBufferRDG<FHairStrandsPositionFormat>(GraphBuilder, VertexCount, DeformedPositionBuffer[0], TEXT("HairStrandsDeformed_DeformedPositionBuffer0"));
		if (bDynamic)
		{
			InternalCreateVertexBufferRDG<FHairStrandsPositionFormat>(GraphBuilder, VertexCount, DeformedPositionBuffer[1], TEXT("HairStrandsDeformed_DeformedPositionBuffer1"));
		}
	}
	InternalCreateVertexBufferRDG<FHairStrandsTangentFormat>(GraphBuilder, VertexCount * FHairStrandsTangentFormat::ComponentCount, TangentBuffer, TEXT("HairStrandsDeformed_TangentBuffer"));

	TArray<FVector4> DefaultOffsets;
	DefaultOffsets.Add(DefaultOffset);
	InternalCreateVertexBufferRDG<FHairStrandsPositionOffsetFormat>(GraphBuilder, DefaultOffsets, DeformedOffsetBuffer[0], TEXT("HairStrandsDeformed_DeformedOffsetBuffer0"), ERDGInitialDataFlags::None);
	InternalCreateVertexBufferRDG<FHairStrandsPositionOffsetFormat>(GraphBuilder, DefaultOffsets, DeformedOffsetBuffer[1], TEXT("HairStrandsDeformed_DeformedOffsetBuffer1"), ERDGInitialDataFlags::None);

	GraphBuilder.Execute();
}

void FHairStrandsDeformedResource::ReleaseRHI()
{
	DeformedPositionBuffer[0].Release();
	if (bDynamic)
	{
		DeformedPositionBuffer[1].Release();
	}
	TangentBuffer.Release();

	DeformedOffsetBuffer[0].Release();
	DeformedOffsetBuffer[1].Release();
}

bool FHairStrandsDeformedResource::NeedsToUpdateTangent()
{ 
	if (bDynamic)
	{
		return true;
	}
	else
	{ 
		const bool bInit = bInitializedTangent;
		bInitializedTangent = false;
		return bInit;
	} 
}

/////////////////////////////////////////////////////////////////////////////////////////

FArchive& operator<<(FArchive& Ar, FHairStrandsClusterCullingData::FHairClusterInfo& Info);
FArchive& operator<<(FArchive& Ar, FHairStrandsClusterCullingData::FHairClusterLODInfo& Info);
FArchive& operator<<(FArchive& Ar, FHairStrandsClusterCullingData::FHairClusterInfo::Packed& Info);

FHairStrandsClusterCullingData::FHairStrandsClusterCullingData()
{

}

void FHairStrandsClusterCullingData::Reset()
{
	*this = FHairStrandsClusterCullingData();
}

void FHairStrandsClusterCullingData::Serialize(FArchive& Ar)
{
	Ar << ClusterCount;
	Ar << VertexCount;
	Ar << LODVisibility;
	Ar << CPULODScreenSize;
	Ar << ClusterInfos;
	Ar << ClusterLODInfos;
	Ar << VertexToClusterIds;
	Ar << ClusterVertexIds;
	Ar << PackedClusterInfos;
}

FHairStrandsClusterCullingResource::FHairStrandsClusterCullingResource(const FHairStrandsClusterCullingData& InData)
: Data(InData) 
{

}

void FHairStrandsClusterCullingResource::InitRHI()
{
	if (GUsingNullRHI) { return; }
	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);

	InternalCreateStructuredBufferRDG(GraphBuilder, Data.PackedClusterInfos, sizeof(FHairStrandsClusterCullingData::FHairClusterInfo::Packed), ClusterInfoBuffer, TEXT("HairStrandsClusterCulling_ClusterInfoBuffer"));
	InternalCreateStructuredBufferRDG(GraphBuilder, Data.ClusterLODInfos, sizeof(FHairStrandsClusterCullingData::FHairClusterLODInfo), ClusterLODInfoBuffer, TEXT("HairStrandsClusterCulling_ClusterLODInfoBuffer"));

	InternalCreateVertexBufferRDG(GraphBuilder, Data.ClusterVertexIds, PF_R32_UINT, ClusterVertexIdBuffer, TEXT("HairStrandsClusterCulling_ClusterVertexIds"));
	InternalCreateVertexBufferRDG(GraphBuilder, Data.VertexToClusterIds, PF_R32_UINT, VertexToClusterIdBuffer, TEXT("HairStrandsClusterCulling_VertexToClusterIds"));

	GraphBuilder.Execute();
}

void FHairStrandsClusterCullingResource::ReleaseRHI()
{
	ClusterInfoBuffer.Release();
	ClusterLODInfoBuffer.Release();
	ClusterVertexIdBuffer.Release();
	VertexToClusterIdBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsRestRootResource::FHairStrandsRestRootResource(const FHairStrandsRootData& InRootData):
RootData(InRootData)
{
	PopulateFromRootData();
}

FHairStrandsRestRootResource::FHairStrandsRestRootResource(const FHairStrandsDatas* HairStrandsDatas, uint32 LODCount, const TArray<uint32>& NumSamples):
	RootData(HairStrandsDatas, LODCount, NumSamples)
{
	PopulateFromRootData();
}

void FHairStrandsRestRootResource::PopulateFromRootData()
{
	uint32 LODIndex = 0;
	for (FHairStrandsRootData::FMeshProjectionLOD& MeshProjectionLOD : RootData.MeshProjectionLODs)
	{
		FLOD& LOD = LODs.AddDefaulted_GetRef();

		LOD.LODIndex = MeshProjectionLOD.LODIndex;
		LOD.Status = FLOD::EStatus::Invalid;
		LOD.SampleCount = MeshProjectionLOD.SampleCount;
	}
}

void FHairStrandsRestRootResource::InitRHI()
{
	if (RootData.VertexToCurveIndexBuffer.Num() > 0)
	{
		if (GUsingNullRHI) { return; }
		FMemMark Mark(FMemStack::Get());
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		FRDGBuilder GraphBuilder(RHICmdList);

		InternalCreateVertexBufferRDG<FHairStrandsIndexFormat>(GraphBuilder, RootData.VertexToCurveIndexBuffer, VertexToCurveIndexBuffer, TEXT("HairStrandsRestRoot_VertexToCurveIndexBuffer"));
		InternalCreateVertexBufferRDG<FHairStrandsRootPositionFormat>(GraphBuilder, RootData.RootPositionBuffer, RootPositionBuffer, TEXT("HairStrandsRestRoot_RootPositionBuffer"));
		InternalCreateVertexBufferRDG<FHairStrandsRootNormalFormat>(GraphBuilder, RootData.RootNormalBuffer, RootNormalBuffer, TEXT("HairStrandsRestRoot_RootNormalBuffer"));
		
		check(LODs.Num() == RootData.MeshProjectionLODs.Num());
		for (uint32 LODIt=0, LODCount = LODs.Num(); LODIt<LODCount; ++LODIt)
		{
			FLOD& GPUData = LODs[LODIt];
			const FHairStrandsRootData::FMeshProjectionLOD& CPUData = RootData.MeshProjectionLODs[LODIt];

			const bool bHasValidCPUData = CPUData.RootTriangleBarycentricBuffer.Num() > 0;
			if (bHasValidCPUData)
			{
				GPUData.Status = FLOD::EStatus::Completed;

				check(CPUData.RootTriangleBarycentricBuffer.Num() > 0);
				InternalCreateVertexBufferRDG<FHairStrandsCurveTriangleBarycentricFormat>(GraphBuilder, CPUData.RootTriangleBarycentricBuffer, GPUData.RootTriangleBarycentricBuffer, TEXT("HairStrandsRestRoot_RootTriangleBarycentricBuffer"));

				check(CPUData.RootTriangleIndexBuffer.Num() > 0);
				InternalCreateVertexBufferRDG<FHairStrandsCurveTriangleIndexFormat>(GraphBuilder, CPUData.RootTriangleIndexBuffer, GPUData.RootTriangleIndexBuffer, TEXT("HairStrandsRestRoot_RootTriangleIndexBuffer"));

				check(CPUData.RestRootTrianglePosition0Buffer.Num() > 0);
				check(CPUData.RestRootTrianglePosition1Buffer.Num() > 0);
				check(CPUData.RestRootTrianglePosition2Buffer.Num() > 0);
				InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, CPUData.RestRootTrianglePosition0Buffer, GPUData.RestRootTrianglePosition0Buffer, TEXT("HairStrandsRestRoot_RestRootTrianglePosition0Buffer"));
				InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, CPUData.RestRootTrianglePosition1Buffer, GPUData.RestRootTrianglePosition1Buffer, TEXT("HairStrandsRestRoot_RestRootTrianglePosition1Buffer"));
				InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, CPUData.RestRootTrianglePosition2Buffer, GPUData.RestRootTrianglePosition2Buffer, TEXT("HairStrandsRestRoot_RestRootTrianglePosition2Buffer"));
			}
			else
			{
				GPUData.Status = FLOD::EStatus::Initialized;

				InternalCreateVertexBufferRDG<FHairStrandsCurveTriangleBarycentricFormat>(GraphBuilder, RootData.RootCount, GPUData.RootTriangleBarycentricBuffer, TEXT("HairStrandsRestRoot_RootTriangleBarycentricBuffer"));
				InternalCreateVertexBufferRDG<FHairStrandsCurveTriangleIndexFormat>(GraphBuilder, RootData.RootCount, GPUData.RootTriangleIndexBuffer, TEXT("HairStrandsRestRoot_RootTriangleIndexBuffer"));

				// Create buffers. Initialization will be done by render passes
				InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, RootData.RootCount, GPUData.RestRootTrianglePosition0Buffer, TEXT("HairStrandsRestRoot_RestRootTrianglePosition0Buffer"));
				InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, RootData.RootCount, GPUData.RestRootTrianglePosition1Buffer, TEXT("HairStrandsRestRoot_RestRootTrianglePosition1Buffer"));
				InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, RootData.RootCount, GPUData.RestRootTrianglePosition2Buffer, TEXT("HairStrandsRestRoot_RestRootTrianglePosition2Buffer"));
			}

			GPUData.SampleCount = CPUData.SampleCount;
			const bool bHasValidCPUWeights = CPUData.MeshSampleIndicesBuffer.Num() > 0;
			if(bHasValidCPUWeights)
			{
				//check(CPUData.MeshInterpolationWeightsBuffer.Num() == (CPUData.SampleCount+4) * (CPUData.SampleCount+4));
				check(CPUData.MeshSampleIndicesBuffer.Num() == CPUData.SampleCount);
				check(CPUData.RestSamplePositionsBuffer.Num() == CPUData.SampleCount);

				InternalCreateVertexBufferRDG<FHairStrandsWeightFormat>(GraphBuilder, CPUData.MeshInterpolationWeightsBuffer, GPUData.MeshInterpolationWeightsBuffer, TEXT("HairStrandsRestRoot_MeshInterpolationWeightsBuffer"));
				InternalCreateVertexBufferRDG<FHairStrandsIndexFormat>(GraphBuilder, CPUData.MeshSampleIndicesBuffer, GPUData.MeshSampleIndicesBuffer, TEXT("HairStrandsRestRoot_MeshSampleIndicesBuffer"));
				InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, CPUData.RestSamplePositionsBuffer, GPUData.RestSamplePositionsBuffer, TEXT("HairStrandsRestRoot_RestSamplePositionsBuffer"));
			}
			else
			{
				InternalCreateVertexBufferRDG<FHairStrandsWeightFormat>(GraphBuilder, (CPUData.SampleCount+4) * (CPUData.SampleCount+4), GPUData.MeshInterpolationWeightsBuffer, TEXT("HairStrandsRestRoot_MeshInterpolationWeightsBuffer"));
				InternalCreateVertexBufferRDG<FHairStrandsIndexFormat>(GraphBuilder, CPUData.SampleCount, GPUData.MeshSampleIndicesBuffer, TEXT("HairStrandsRestRoot_MeshSampleIndicesBuffer"));
				InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, CPUData.SampleCount, GPUData.RestSamplePositionsBuffer, TEXT("HairStrandsRestRoot_RestSamplePositionsBuffer"));
			}
		}
		GraphBuilder.Execute();
	}
}

void FHairStrandsRestRootResource::ReleaseRHI()
{
	RootPositionBuffer.Release();
	RootNormalBuffer.Release();
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

	// Once empty, the MeshProjectionLODsneeds to be repopulate as it might be re-initialized. 
	// E.g., when a resource is updated, it is first released, then re-init. 
	PopulateFromRootData();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsDeformedRootResource::FHairStrandsDeformedRootResource()
{

}

FHairStrandsDeformedRootResource::FHairStrandsDeformedRootResource(const FHairStrandsRestRootResource* InRestResources)
{
	check(InRestResources);
	uint32 LODIndex = 0;
	RootCount = InRestResources->RootData.RootCount;
	for (const FHairStrandsRestRootResource::FLOD& InLOD : InRestResources->LODs)
	{
		FLOD& LOD = LODs.AddDefaulted_GetRef();

		LOD.Status = FLOD::EStatus::Invalid;
		LOD.LODIndex = InLOD.LODIndex;
		LOD.SampleCount = InLOD.SampleCount;
	}
}

void FHairStrandsDeformedRootResource::InitRHI()
{
	if (GUsingNullRHI) { return; }
	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);

	if (RootCount > 0)
	{
		for (FLOD& LOD : LODs)
		{		
			LOD.Status = FLOD::EStatus::Initialized;
			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, LOD.SampleCount, LOD.DeformedSamplePositionsBuffer, TEXT("HairStrandsRootDeformed_DeformedSamplePositionsBuffer"));
			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, LOD.SampleCount + 5, LOD.MeshSampleWeightsBuffer, TEXT("HairStrandsRootDeformed_MeshSampleWeightsBuffer"));

			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, RootCount, LOD.DeformedRootTrianglePosition0Buffer, TEXT("HairStrandsRootDeformed_DeformedRootTrianglePosition0Buffer"));
			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, RootCount, LOD.DeformedRootTrianglePosition1Buffer, TEXT("HairStrandsRootDeformed_DeformedRootTrianglePosition1Buffer"));
			InternalCreateVertexBufferRDG<FHairStrandsMeshTrianglePositionFormat>(GraphBuilder, RootCount, LOD.DeformedRootTrianglePosition2Buffer, TEXT("HairStrandsRootDeformed_DeformedRootTrianglePosition2Buffer"));
		}
	}
	GraphBuilder.Execute();
}

void FHairStrandsDeformedRootResource::ReleaseRHI()
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

FHairStrandsRootData::FHairStrandsRootData()
{

}

FHairStrandsRootData::FHairStrandsRootData(const FHairStrandsDatas* HairStrandsDatas, uint32 LODCount, const TArray<uint32>& NumSamples):
	RootCount(HairStrandsDatas ? HairStrandsDatas->GetNumCurves() : 0)
{
	if (!HairStrandsDatas)
		return;

	const uint32 CurveCount = HairStrandsDatas->GetNumCurves();
	VertexToCurveIndexBuffer.SetNum(HairStrandsDatas->GetNumPoints());
	RootPositionBuffer.SetNum(RootCount);
	RootNormalBuffer.SetNum(RootCount);

	for (uint32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
	{
		const uint32 RootIndex = HairStrandsDatas->StrandsCurves.CurvesOffset[CurveIndex];
		const uint32 PointCount = HairStrandsDatas->StrandsCurves.CurvesCount[CurveIndex];
		for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			VertexToCurveIndexBuffer[RootIndex + PointIndex] = CurveIndex; // RootIndex;
		}

		check(PointCount > 1);

		const FVector P0 = HairStrandsDatas->StrandsPoints.PointsPosition[RootIndex];
		const FVector P1 = HairStrandsDatas->StrandsPoints.PointsPosition[RootIndex + 1];
		FVector N0 = (P1 - P0).GetSafeNormal();

		// Fallback in case the initial points are too close (this happens on certain assets)
		if (FVector::DotProduct(N0, N0) == 0)
		{
			N0 = FVector(0, 0, 1);
		}

		FHairStrandsRootPositionFormat::Type P;
		P.X = P0.X;
		P.Y = P0.Y;
		P.Z = P0.Z;
		P.W = 1;

		FHairStrandsRootNormalFormat::Type N;
		N.X = N0.X;
		N.Y = N0.Y;
		N.Z = N0.Z;
		N.W = 0;

		RootPositionBuffer[CurveIndex] = P;
		RootNormalBuffer[CurveIndex] = N;
	}
	check(NumSamples.Num() == LODCount);

	MeshProjectionLODs.SetNum(LODCount);
	uint32 LODIndex = 0;
	for (FMeshProjectionLOD& MeshProjectionLOD : MeshProjectionLODs)
	{
		MeshProjectionLOD.SampleCount = NumSamples[LODIndex];
		MeshProjectionLOD.LODIndex = LODIndex++;
		MeshProjectionLOD.MeshInterpolationWeightsBuffer.Empty();
		MeshProjectionLOD.MeshSampleIndicesBuffer.Empty();
		MeshProjectionLOD.RestSamplePositionsBuffer.Empty();
	}
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

FArchive& operator<<(FArchive& Ar, FHairStrandsRootData::FMeshProjectionLOD& LOD)
{
	Ar << LOD.LODIndex;
	Ar << LOD.RootTriangleIndexBuffer;
	Ar << LOD.RootTriangleBarycentricBuffer;
	Ar << LOD.RestRootTrianglePosition0Buffer;
	Ar << LOD.RestRootTrianglePosition1Buffer;
	Ar << LOD.RestRootTrianglePosition2Buffer;

	Ar << LOD.SampleCount;
	Ar << LOD.MeshInterpolationWeightsBuffer;
	Ar << LOD.MeshSampleIndicesBuffer;
	Ar << LOD.RestSamplePositionsBuffer;

	if (Ar.IsLoading())
	{
		FGroomBindingBuilder::BuildUniqueSections(LOD);
	}
	return Ar;
}

void FHairStrandsRootData::Serialize(FArchive& Ar)
{
	if (!Ar.IsObjectReferenceCollector())
	{
		Ar << RootCount;
		Ar << VertexToCurveIndexBuffer;
		Ar << RootPositionBuffer;
		Ar << RootNormalBuffer;
		Ar << MeshProjectionLODs;
	}
}

void FHairStrandsRootData::Reset()
{
	RootCount = 0;
	VertexToCurveIndexBuffer.Empty();
	RootPositionBuffer.Empty();
	RootNormalBuffer.Empty();
	MeshProjectionLODs.Empty();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsInterpolationResource::FHairStrandsInterpolationResource(const FHairStrandsInterpolationDatas::FRenderData& InterpolationRenderData, const FHairStrandsDatas& SimDatas) :
	Interpolation0Buffer(), Interpolation1Buffer(), RenderData(InterpolationRenderData)
{
	const uint32 RootCount = SimDatas.GetNumCurves();
	SimRootPointIndex.SetNum(SimDatas.GetNumPoints());
	for (uint32 CurveIndex = 0; CurveIndex < RootCount; ++CurveIndex)
	{
		const uint16 PointCount = SimDatas.StrandsCurves.CurvesCount[CurveIndex];
		const uint32 PointOffset = SimDatas.StrandsCurves.CurvesOffset[CurveIndex];
		for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			SimRootPointIndex[PointIndex + PointOffset] = PointOffset;
		}
	}
}

void FHairStrandsInterpolationResource::InitRHI()
{
	if (GUsingNullRHI) { return; }
	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);

	InternalCreateVertexBufferRDG<FHairStrandsInterpolation0Format>(GraphBuilder, RenderData.Interpolation0, Interpolation0Buffer, TEXT("HairStrandsInterpolation_Interpolation0Buffer"));
	InternalCreateVertexBufferRDG<FHairStrandsInterpolation1Format>(GraphBuilder, RenderData.Interpolation1, Interpolation1Buffer, TEXT("HairStrandsInterpolation_Interpolation1Buffer"));
	InternalCreateVertexBufferRDG<FHairStrandsRootIndexFormat>(GraphBuilder, SimRootPointIndex, SimRootPointIndexBuffer, TEXT("HairStrandsInterpolation_SimRootPointIndex"));
	GraphBuilder.Execute();
	//SimRootPointIndex.SetNum(0);
}

void FHairStrandsInterpolationResource::ReleaseRHI()
{
	Interpolation0Buffer.Release();
	Interpolation1Buffer.Release();
	SimRootPointIndexBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////
FArchive& operator<<(FArchive& Ar, FHairCardsInterpolationDatas& CardInterpData)
{
	Ar << CardInterpData.PointsSimCurvesIndex;
	Ar << CardInterpData.PointsSimCurvesVertexIndex;
	Ar << CardInterpData.PointsSimCurvesVertexLerp;
	Ar << CardInterpData.RenderData.Interpolation;

	return Ar;
}

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

FHairCardsInterpolationResource::FHairCardsInterpolationResource(const FHairCardsInterpolationDatas::FRenderData& InterpolationRenderData) :
	InterpolationBuffer(), RenderData(InterpolationRenderData)
{
}

void FHairCardsInterpolationResource::InitRHI()
{
	if (GUsingNullRHI) { return; }
	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);

	InternalCreateVertexBufferRDG<FHairCardsInterpolationFormat>(GraphBuilder, RenderData.Interpolation, InterpolationBuffer, TEXT("HairCardsInterpolation_InterpolationBuffer"));
	GraphBuilder.Execute();
}

void FHairCardsInterpolationResource::ReleaseRHI()
{
	InterpolationBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

#if RHI_RAYTRACING
// RT geometry is built to for a cross around the fiber.
// 4 triangles per hair vertex => 12 vertices per hair vertex
FHairStrandsRaytracingResource::FHairStrandsRaytracingResource(const FHairStrandsDatas& InData) :
	PositionBuffer(), VertexCount(InData.GetNumPoints()*12)  
{}

FHairStrandsRaytracingResource::FHairStrandsRaytracingResource(const FHairCardsDatas& InData) :
	PositionBuffer(), VertexCount(InData.Cards.GetNumVertices())
{}

FHairStrandsRaytracingResource::FHairStrandsRaytracingResource(const FHairMeshesDatas& InData) :
	PositionBuffer(), VertexCount(InData.Meshes.GetNumVertices())
{}

void FHairStrandsRaytracingResource::InitRHI()
{
	if (GUsingNullRHI) { return; }
	FMemMark Mark(FMemStack::Get());
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FRDGBuilder GraphBuilder(RHICmdList);

	InternalCreateVertexBufferRDG<FHairStrandsRaytracingFormat>(GraphBuilder, VertexCount, PositionBuffer, TEXT("HairStrandsRaytracing_PositionBuffer"));
	GraphBuilder.Execute();
}

void FHairStrandsRaytracingResource::ReleaseRHI()
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

	ConvertToUntrackedExternalBuffer(GraphBuilder, VoxelOffsetAndCount, Out->VoxelOffsetAndCount, ERHIAccess::SRVMask);
	ConvertToUntrackedExternalBuffer(GraphBuilder, VoxelData, Out->VoxelData, ERHIAccess::SRVMask);
}
