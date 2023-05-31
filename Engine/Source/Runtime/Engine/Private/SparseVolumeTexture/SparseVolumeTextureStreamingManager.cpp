// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTexture/SparseVolumeTextureStreamingManager.h"
#include "SparseVolumeTexture/SparseVolumeTextureUtility.h"
#include "HAL/IConsoleManager.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"
#include "RenderCore.h"
#include "RenderGraph.h"
#include "GlobalShader.h"
#include "ShaderCompilerCore.h" // AllowGlobalShaderLoad()
#include "Async/ParallelFor.h"

#if WITH_EDITORONLY_DATA
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTextureStreamingManager, Log, All);

#ifndef SVT_STREAMING_LOG_VERBOSE
#define SVT_STREAMING_LOG_VERBOSE 0
#endif

static int32 GSVTStreamingNumPrefetchFrames = 3;
static FAutoConsoleVariableRef CVarSVTStreamingNumPrefetchFrames(
	TEXT("r.SparseVolumeTexture.Streaming.NumPrefetchFrames"),
	GSVTStreamingNumPrefetchFrames,
	TEXT("Number of frames to prefetch when a frame is requested."),
	ECVF_RenderThreadSafe
);

static int32 GSVTStreamingPrefetchMipLevelBias = -1;
static FAutoConsoleVariableRef CVarSVTStreamingPrefetchMipLevelBias(
	TEXT("r.SparseVolumeTexture.Streaming.PrefetchMipLevelBias"),
	GSVTStreamingPrefetchMipLevelBias,
	TEXT("Bias to apply to the mip level of prefetched frames. Prefetching is done at increasingly higher mip levels (lower resolution), so setting a negative value here will increase the requested mip level resolution."),
	ECVF_RenderThreadSafe
);

static int32 GSVTStreamingAsyncThread = 1;
static FAutoConsoleVariableRef CVarSVTStreamingAsync(
	TEXT("r.SparseVolumeTexture.Streaming.AsyncThread"),
	GSVTStreamingAsyncThread,
	TEXT("Perform most of the SVT streaming on an asynchronous worker thread instead of the rendering thread."),
	ECVF_RenderThreadSafe
);

static int32 GSVTStreamingAsyncCompute = 1;
static FAutoConsoleVariableRef CVarSVTStreamingAsyncCompute(
	TEXT("r.SparseVolumeTexture.Streaming.AsyncCompute"),
	GSVTStreamingAsyncCompute,
	TEXT("Schedule GPU work in async compute queue."),
	ECVF_RenderThreadSafe
);

static int32 GSVTStreamingEmptyPhysicalTileTextures = 0;
static FAutoConsoleVariableRef CVarSVTStreamingEmptyPhysicalTileTextures(
	TEXT("r.SparseVolumeTexture.Streaming.EmptyPhysicalTileTextures"),
	GSVTStreamingEmptyPhysicalTileTextures,
	TEXT("Streams out all streamable tiles of all physical tile textures."),
	ECVF_RenderThreadSafe
);

static int32 GSVTStreamingMaxPendingMipLevels = 128;
static FAutoConsoleVariableRef CVarSVTStreamingMaxPendingMipLevels(
	TEXT("r.SparseVolumeTexture.Streaming.MaxPendingMipLevels"),
	GSVTStreamingMaxPendingMipLevels,
	TEXT("Maximum number of mip levels that can be pending for installation."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

namespace UE
{
namespace SVT
{

static bool DoesPlatformSupportSparseVolumeTexture(EShaderPlatform Platform)
{
	// SVT_TODO: This is a bit of a hack: FStreamingManager::Add_GameThread() issues a rendering thread lambda for creating the RHI resources and uploading root tile data.
	// Uploading root tile data involves access to the global shader map, which is empty under certain circumstances. By checking AllowGlobalShaderLoad(), we disallow streaming completely.
	return AllowGlobalShaderLoad();
}

static FIntVector3 ComputeTileDataVolumeResolution2(int32 NumAllocatedPages)
{
	int32 TileVolumeResolutionCube = 1;
	while (TileVolumeResolutionCube * TileVolumeResolutionCube * TileVolumeResolutionCube < NumAllocatedPages)
	{
		TileVolumeResolutionCube++;				// We use a simple loop to compute the minimum resolution of a cube to store all the tile data
	}
	FIntVector3 TileDataVolumeResolution = FIntVector3(TileVolumeResolutionCube, TileVolumeResolutionCube, TileVolumeResolutionCube);
	
	// Trim volume to reclaim some space
	while ((TileDataVolumeResolution.X * TileDataVolumeResolution.Y * (TileDataVolumeResolution.Z - 1)) > NumAllocatedPages)
	{
		TileDataVolumeResolution.Z--;
	}
	while ((TileDataVolumeResolution.X * (TileDataVolumeResolution.Y - 1) * TileDataVolumeResolution.Z) > NumAllocatedPages)
	{
		TileDataVolumeResolution.Y--;
	}
	while (((TileDataVolumeResolution.X - 1) * TileDataVolumeResolution.Y * TileDataVolumeResolution.Z) > NumAllocatedPages)
	{
		TileDataVolumeResolution.X--;
	}

	return TileDataVolumeResolution * SPARSE_VOLUME_TILE_RES_PADDED;
}

static FIntVector3 ComputeLargestPossibleTileDataVolumeResolution(int32 VoxelMemSize)
{
	const int64 TileMemSize = SVTNumVoxelsPerPaddedTile * VoxelMemSize;
	const int64 NumMaxTiles = int64(INT32_MAX) / TileMemSize;
	int64 ResourceSize = NumMaxTiles * TileMemSize;

	// Find a cube with a volume as close to NumMaxTiles as possible
	int32 TileVolumeResolutionCube = 1;
	while (((TileVolumeResolutionCube + 1) * (TileVolumeResolutionCube + 1) * (TileVolumeResolutionCube + 1)) <= NumMaxTiles)
	{
		++TileVolumeResolutionCube;
	}

	// Try to add to the sides to get closer to NumMaxTiles
	FIntVector3 ResolutionInTiles = FIntVector3(TileVolumeResolutionCube, TileVolumeResolutionCube, TileVolumeResolutionCube);
	if (((ResolutionInTiles.X + 1) * ResolutionInTiles.Y * ResolutionInTiles.Z) <= NumMaxTiles)
	{
		++ResolutionInTiles.X;
	}
	if ((ResolutionInTiles.X * (ResolutionInTiles.Y + 1) * ResolutionInTiles.Z) <= NumMaxTiles)
	{
		++ResolutionInTiles.Y;
	}
	if ((ResolutionInTiles.X * ResolutionInTiles.Y * (ResolutionInTiles.Z + 1)) <= NumMaxTiles)
	{
		++ResolutionInTiles.Z;
	}

	const FIntVector3 Resolution = ResolutionInTiles * SPARSE_VOLUME_TILE_RES_PADDED;
	check(Resolution.X <= SVTMaxVolumeTextureDim && Resolution.Y <= SVTMaxVolumeTextureDim && Resolution.Z <= SVTMaxVolumeTextureDim);
	check(((int64)Resolution.X * (int64)Resolution.Y * (int64)Resolution.Z) < int64(INT32_MAX));

	return Resolution;
}

class SparseVolumeTextureUpdateFromBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(SparseVolumeTextureUpdateFromBufferCS);
	SHADER_USE_PARAMETER_STRUCT(SparseVolumeTextureUpdateFromBufferCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D, DstPhysicalTileTextureA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D, DstPhysicalTileTextureB)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, DstTileCoords)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SrcPhysicalTileBufferA)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SrcPhysicalTileBufferB)
		SHADER_PARAMETER(uint32, TileCoordsBufferOffset)
		SHADER_PARAMETER(uint32, TileDataBufferOffsetInTiles)
		SHADER_PARAMETER(uint32, NumTilesToCopy)
		SHADER_PARAMETER(uint32, NumDispatchedGroups)
		SHADER_PARAMETER(uint32, PaddedTileSize)
		SHADER_PARAMETER(uint32, bCopyTexureAOnlyUI)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UPDATE_TILE_TEXTURE_FROM_BUFFER"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(SparseVolumeTextureUpdateFromBufferCS, "/Engine/Private/SparseVolumeTexture/UpdateSparseVolumeTexture.usf", "SparseVolumeTextureUpdateFromBufferCS", SF_Compute);

class SparseVolumeTextureUpdatePageTableCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(SparseVolumeTextureUpdatePageTableCS);
	SHADER_USE_PARAMETER_STRUCT(SparseVolumeTextureUpdatePageTableCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D, PageTable)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, PageTableUpdates)
		SHADER_PARAMETER(uint32, UpdateCoordOffset)
		SHADER_PARAMETER(uint32, UpdatePayloadOffset)
		SHADER_PARAMETER(uint32, NumUpdates)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UPDATE_PAGE_TABLE"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(SparseVolumeTextureUpdatePageTableCS, "/Engine/Private/SparseVolumeTexture/UpdateSparseVolumeTexture.usf", "SparseVolumeTextureUpdatePageTableCS", SF_Compute);

class SparseVolumeTextureUpdateStreamingInfoBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(SparseVolumeTextureUpdateStreamingInfoBufferCS);
	SHADER_USE_PARAMETER_STRUCT(SparseVolumeTextureUpdateStreamingInfoBufferCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, StreamingInfoBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, StreamingInfoBufferUpdates)
		SHADER_PARAMETER(uint32, UpdateOffset)
		SHADER_PARAMETER(uint32, NumUpdates)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UPDATE_STREAMING_INFO_BUFFER"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(SparseVolumeTextureUpdateStreamingInfoBufferCS, "/Engine/Private/SparseVolumeTexture/UpdateSparseVolumeTexture.usf", "SparseVolumeTextureUpdateStreamingInfoBufferCS", SF_Compute);

// Utility class for uploading tiles to a physical tile data texture
class FTileUploader
{
public:
	FTileUploader()
	{
		ResetState();
	}

	void Init(FRDGBuilder& GraphBuilder, int32 InMaxNumTiles, EPixelFormat InFormatA, EPixelFormat InFormatB)
	{
		check(InFormatA != PF_Unknown || InFormatB != PF_Unknown);
		ResetState();
		MaxNumTiles = InMaxNumTiles;
		FormatA = InFormatA;
		FormatB = InFormatB;
		FormatSizeA = GPixelFormats[FormatA].BlockBytes;
		FormatSizeB = GPixelFormats[FormatB].BlockBytes;

		// Create a new set of buffers if the old set is already queued into RDG.
		if (IsRegistered(GraphBuilder, DstTileCoordsUploadBuffer))
		{
			DstTileCoordsUploadBuffer = nullptr;
			TileDataAUploadBuffer = nullptr;
			TileDataBUploadBuffer = nullptr;
		}

		if (MaxNumTiles > 0)
		{
			// TileCoords
			{
				// Add EBufferUsageFlags::Dynamic to skip the unneeded copy from upload to VRAM resource on d3d12 RHI
				FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateByteAddressUploadDesc(MaxNumTiles * sizeof(uint32));
				BufferDesc.Usage |= EBufferUsageFlags::Dynamic;
				AllocatePooledBuffer(BufferDesc, DstTileCoordsUploadBuffer, TEXT("SparseVolumeTexture.TileCoordsUploadBuffer"));

				TileCoordsPtr = (uint8*)RHILockBuffer(DstTileCoordsUploadBuffer->GetRHI(), 0, MaxNumTiles * sizeof(uint32), RLM_WriteOnly);
			}

			const int32 NumVoxels = MaxNumTiles * UE::SVT::SVTNumVoxelsPerPaddedTile;

			// TileData
			if (FormatSizeA > 0)
			{
				// Add EBufferUsageFlags::Dynamic to skip the unneeded copy from upload to VRAM resource on d3d12 RHI
				FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateUploadDesc(FormatSizeA, NumVoxels);
				BufferDesc.Usage |= EBufferUsageFlags::Dynamic;
				AllocatePooledBuffer(BufferDesc, TileDataAUploadBuffer, TEXT("SparseVolumeTexture.TileDataAUploadBuffer"));

				TileDataAPtr = (uint8*)RHILockBuffer(TileDataAUploadBuffer->GetRHI(), 0, NumVoxels * FormatSizeA, RLM_WriteOnly);
			}
			if (FormatSizeB > 0)
			{
				// Add EBufferUsageFlags::Dynamic to skip the unneeded copy from upload to VRAM resource on d3d12 RHI
				FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateUploadDesc(FormatSizeB, NumVoxels);
				BufferDesc.Usage |= EBufferUsageFlags::Dynamic;
				AllocatePooledBuffer(BufferDesc, TileDataBUploadBuffer, TEXT("SparseVolumeTexture.TileDataBUploadBuffer"));

				TileDataBPtr = (uint8*)RHILockBuffer(TileDataBUploadBuffer->GetRHI(), 0, NumVoxels * FormatSizeB, RLM_WriteOnly);
			}
		}
	}

	void Add_GetRef(int32 NumTiles, uint8*& OutPackedPhysicalTileCoordsPtr, uint8*& OutPtrA, uint8*& OutPtrB)
	{
		check((NumWrittenTiles + NumTiles) <= MaxNumTiles);
		check(TileCoordsPtr);
		check(FormatSizeA <= 0 || TileDataAPtr);
		check(FormatSizeB <= 0 || TileDataBPtr);

		OutPackedPhysicalTileCoordsPtr = TileCoordsPtr + NumWrittenTiles * sizeof(uint32);
		OutPtrA = TileDataAPtr ? TileDataAPtr + (NumWrittenTiles * UE::SVT::SVTNumVoxelsPerPaddedTile * FormatSizeA) : nullptr;
		OutPtrB = TileDataBPtr ? TileDataBPtr + (NumWrittenTiles * UE::SVT::SVTNumVoxelsPerPaddedTile * FormatSizeB) : nullptr;

		NumWrittenTiles += NumTiles;
	}

	void Release()
	{
		DstTileCoordsUploadBuffer.SafeRelease();
		TileDataAUploadBuffer.SafeRelease();
		TileDataBUploadBuffer.SafeRelease();
		ResetState();
	}

	void ResourceUploadTo(FRDGBuilder& GraphBuilder, FRHITexture* DstTextureA, FRHITexture* DstTextureB)
	{
		check(DstTextureA || FormatSizeA <= 0);
		check(DstTextureB || FormatSizeB <= 0);
		if (MaxNumTiles > 0)
		{
			RHIUnlockBuffer(DstTileCoordsUploadBuffer->GetRHI());
			if (TileDataAPtr)
			{
				RHIUnlockBuffer(TileDataAUploadBuffer->GetRHI());
			}
			if (TileDataBPtr)
			{
				RHIUnlockBuffer(TileDataBUploadBuffer->GetRHI());
			}

			if (NumWrittenTiles > 0)
			{
				FRDGTexture* DstTextureARDG = DstTextureA ? GraphBuilder.RegisterExternalTexture(CreateRenderTarget(DstTextureA, TEXT("SparseVolumeTexture.TileDataTextureA"))) : nullptr;
				FRDGTexture* DstTextureBRDG = DstTextureB ? GraphBuilder.RegisterExternalTexture(CreateRenderTarget(DstTextureB, TEXT("SparseVolumeTexture.TileDataTextureB"))) : nullptr;

				FRDGBufferSRV* DstTileCoordsBufferSRV = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(DstTileCoordsUploadBuffer));
				FRDGBufferSRV* TileDataABufferSRV = FormatSizeA ? GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(TileDataAUploadBuffer), FormatA) : nullptr;
				FRDGBufferSRV* TileDataBBufferSRV = FormatSizeB ? GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(TileDataBUploadBuffer), FormatB) : nullptr;
				FRDGTextureUAV* DstTextureAUAV = DstTextureARDG ? GraphBuilder.CreateUAV(DstTextureARDG) : nullptr;
				FRDGTextureUAV* DstTextureBUAV = DstTextureBRDG ? GraphBuilder.CreateUAV(DstTextureBRDG) : nullptr;

				auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<SparseVolumeTextureUpdateFromBufferCS>();

				SparseVolumeTextureUpdateFromBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<SparseVolumeTextureUpdateFromBufferCS::FParameters>();
				if (FormatSizeA > 0) // TextureA exists
				{
					PassParameters->DstPhysicalTileTextureA = DstTextureAUAV;
					PassParameters->SrcPhysicalTileBufferA = TileDataABufferSRV;
					if (FormatSizeB == 0) // B doesn't exist: fill B params with A
					{
						PassParameters->DstPhysicalTileTextureB = DstTextureAUAV;
						PassParameters->SrcPhysicalTileBufferB = TileDataABufferSRV;
					}
				}
				if (FormatSizeB > 0) // TextureB exists
				{
					PassParameters->DstPhysicalTileTextureB = DstTextureBUAV;
					PassParameters->SrcPhysicalTileBufferB = TileDataBBufferSRV;
					if (FormatSizeA == 0) // A doesnt't exist: fill A params with B
					{
						PassParameters->DstPhysicalTileTextureA = DstTextureBUAV;
						PassParameters->SrcPhysicalTileBufferA = TileDataBBufferSRV;
					}
				}
				PassParameters->DstTileCoords = DstTileCoordsBufferSRV;
				PassParameters->TileCoordsBufferOffset = 0;
				PassParameters->TileDataBufferOffsetInTiles = 0;
				PassParameters->NumTilesToCopy = NumWrittenTiles;
				PassParameters->NumDispatchedGroups = FMath::Min(NumWrittenTiles, 1024);
				PassParameters->PaddedTileSize = SPARSE_VOLUME_TILE_RES_PADDED;
				PassParameters->bCopyTexureAOnlyUI = (FormatSizeA == 0 || FormatSizeB == 0);

				const bool bAsyncCompute = GSupportsEfficientAsyncCompute && (GSVTStreamingAsyncCompute != 0);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Upload SVT Tiles (TileCount: %u)", NumWrittenTiles),
					bAsyncCompute ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
					ComputeShader,
					PassParameters,
					FIntVector3(PassParameters->NumDispatchedGroups, 1, 1)
				);
			}
		}
		Release();
	}

private:
	TRefCountPtr<FRDGPooledBuffer> DstTileCoordsUploadBuffer;
	TRefCountPtr<FRDGPooledBuffer> TileDataAUploadBuffer;
	TRefCountPtr<FRDGPooledBuffer> TileDataBUploadBuffer;
	uint8* TileCoordsPtr = nullptr;
	uint8* TileDataAPtr = nullptr;
	uint8* TileDataBPtr = nullptr;
	int32 MaxNumTiles = 0;
	EPixelFormat FormatA = PF_Unknown;
	EPixelFormat FormatB = PF_Unknown;
	int32 FormatSizeA = 0;
	int32 FormatSizeB = 0;
	int32 NumWrittenTiles = 0;

	void ResetState()
	{
		TileCoordsPtr = nullptr;
		TileDataAPtr = nullptr;
		TileDataBPtr = nullptr;
		MaxNumTiles = 0;
		FormatA = PF_Unknown;
		FormatB = PF_Unknown;
		FormatSizeA = 0;
		FormatSizeB = 0;
		NumWrittenTiles = 0;
	}
};

// Utility class for writing page table entries
class FPageTableUpdater
{
public:
	FPageTableUpdater()
	{
		ResetState();
	}

	void Init(FRDGBuilder& GraphBuilder, int32 InMaxNumUpdates, int32 InEstimatedNumBatches)
	{
		ResetState();
		MaxNumUpdates = InMaxNumUpdates;
		Batches.Reserve(InEstimatedNumBatches);

		// Create a new buffer if the old one is already queued into RDG.
		if (IsRegistered(GraphBuilder, UpdatesUploadBuffer))
		{
			UpdatesUploadBuffer = nullptr;
		}

		if (MaxNumUpdates > 0)
		{
			// Add EBufferUsageFlags::Dynamic to skip the unneeded copy from upload to VRAM resource on d3d12 RHI
			FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateByteAddressUploadDesc(MaxNumUpdates * 2 * sizeof(uint32));
			BufferDesc.Usage |= EBufferUsageFlags::Dynamic;
			AllocatePooledBuffer(BufferDesc, UpdatesUploadBuffer, TEXT("SparseVolumeTexture.PageTableUpdatesUploadBuffer"));

			DataPtr = (uint8*)RHILockBuffer(UpdatesUploadBuffer->GetRHI(), 0, MaxNumUpdates * 2 * sizeof(uint32), RLM_WriteOnly);
		}
	}

	void Add_GetRef(FRHITexture* PageTable, int32 MipLevel, int32 NumUpdates, uint8*& OutCoordsPtr, uint8*& OutPayloadPtr)
	{
		check((NumWrittenUpdates + NumUpdates) <= MaxNumUpdates);
		check(DataPtr);
		FBatch* Batch = Batches.IsEmpty() ? nullptr : &Batches.Last();
		if (!Batch || Batch->PageTable != PageTable || Batch->MipLevel != MipLevel)
		{
			Batch = &Batches.Add_GetRef(FBatch(PageTable, MipLevel));
		}

		OutCoordsPtr = DataPtr + NumWrittenUpdates * sizeof(uint32);
		OutPayloadPtr = DataPtr + (MaxNumUpdates + NumWrittenUpdates) * sizeof(uint32);

		Batch->NumUpdates += NumUpdates;
		NumWrittenUpdates += NumUpdates;
	}

	void Release()
	{
		UpdatesUploadBuffer.SafeRelease();
		ResetState();
	}

	void Apply(FRDGBuilder& GraphBuilder)
	{
		if (MaxNumUpdates > 0)
		{
			RHIUnlockBuffer(UpdatesUploadBuffer->GetRHI());

			if (NumWrittenUpdates > 0)
			{
				auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<SparseVolumeTextureUpdatePageTableCS>();
				const bool bAsyncCompute = GSupportsEfficientAsyncCompute && (GSVTStreamingAsyncCompute != 0);

				uint32 UpdatesOffset = 0;
				for (const FBatch& Batch : Batches)
				{
					FRDGTexture* PageTableRDG = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Batch.PageTable, TEXT("SparseVolumeTexture.PageTableTexture")));
					FRDGTextureUAV* PageTableUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PageTableRDG, Batch.MipLevel, PF_R32_UINT));
					FRDGBufferSRV* UpdatesBufferSRV = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(UpdatesUploadBuffer));

					SparseVolumeTextureUpdatePageTableCS::FParameters* PassParameters = GraphBuilder.AllocParameters<SparseVolumeTextureUpdatePageTableCS::FParameters>();
					PassParameters->PageTable = PageTableUAV;
					PassParameters->PageTableUpdates = UpdatesBufferSRV;
					PassParameters->UpdateCoordOffset = UpdatesOffset;
					PassParameters->UpdatePayloadOffset = MaxNumUpdates + UpdatesOffset;
					PassParameters->NumUpdates = Batch.NumUpdates;

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("Update SVT PageTable (UpdateCount: %u)", Batch.NumUpdates),
						bAsyncCompute ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
						ComputeShader,
						PassParameters,
						FComputeShaderUtils::GetGroupCount(Batch.NumUpdates, 64)
					);

					UpdatesOffset += Batch.NumUpdates;
				}
			}
		}
		
		Release();
	}

private:
	struct FBatch
	{
		FRHITexture* PageTable;
		int32 MipLevel;
		int32 NumUpdates;

		FBatch() = default;
		FBatch(FRHITexture* InPageTable, int32 InMipLevel) : PageTable(InPageTable), MipLevel(InMipLevel), NumUpdates(0) {}
	};

	TRefCountPtr<FRDGPooledBuffer> UpdatesUploadBuffer;
	TArray<FBatch> Batches;
	uint8* DataPtr = nullptr;
	int32 NumWrittenUpdates = 0;
	int32 MaxNumUpdates = 0;


	void ResetState()
	{
		Batches.Reset();
		DataPtr = nullptr;
		NumWrittenUpdates = 0;
		MaxNumUpdates = 0;
	}
};

// Updates entries in the streaming info buffers of multiple SVTs
class FStreamingInfoBufferUpdater
{
public:
	FStreamingInfoBufferUpdater()
	{
		ResetState();
	}

	void Add(TRefCountPtr<FRDGPooledBuffer> StreamingInfoBuffer, int32 FrameIndex, int32 LowestResidentMipLevel)
	{
		FBatch* Batch = Batches.IsEmpty() ? nullptr : &Batches.Last();
		if (!Batch || Batch->StreamingInfoBuffer != StreamingInfoBuffer)
		{
			Batch = &Batches.Add_GetRef(FBatch(StreamingInfoBuffer, Updates.Num()));
		}

		Updates.Add(FrameIndex);
		Updates.Add(LowestResidentMipLevel);

		++Batch->NumUpdates;
	}

	void Apply(FRDGBuilder& GraphBuilder)
	{
		if (!Updates.IsEmpty())
		{
			TRefCountPtr<FRDGPooledBuffer> UpdatesUploadBuffer;
			{
				// Add EBufferUsageFlags::Dynamic to skip the unneeded copy from upload to VRAM resource on d3d12 RHI
				FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateByteAddressUploadDesc(Updates.Num() * sizeof(uint32));
				BufferDesc.Usage |= EBufferUsageFlags::Dynamic;
				AllocatePooledBuffer(BufferDesc, UpdatesUploadBuffer, TEXT("SparseVolumeTexture.StreamingInfoUploadBuffer"));

				void* DataPtr = RHILockBuffer(UpdatesUploadBuffer->GetRHI(), 0, Updates.Num() * sizeof(uint32), RLM_WriteOnly);
				FMemory::Memcpy(DataPtr, Updates.GetData(), Updates.Num() * sizeof(uint32));
				RHIUnlockBuffer(UpdatesUploadBuffer->GetRHI());
			}

			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<SparseVolumeTextureUpdateStreamingInfoBufferCS>();
			const bool bAsyncCompute = GSupportsEfficientAsyncCompute && (GSVTStreamingAsyncCompute != 0);

			uint32 UpdatesOffset = 0;
			for (const FBatch& Batch : Batches)
			{
				FRDGBufferUAV* StreamingInfoBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(Batch.StreamingInfoBuffer), PF_R32_UINT);
				FRDGBufferSRV* UpdatesBufferSRV = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(UpdatesUploadBuffer));

				SparseVolumeTextureUpdateStreamingInfoBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<SparseVolumeTextureUpdateStreamingInfoBufferCS::FParameters>();
				PassParameters->StreamingInfoBuffer = StreamingInfoBufferUAV;
				PassParameters->StreamingInfoBufferUpdates = UpdatesBufferSRV;
				PassParameters->UpdateOffset = UpdatesOffset;
				PassParameters->NumUpdates = Batch.NumUpdates;

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Update SVT StreamingInfoBuffer (UpdateCount: %u)", Batch.NumUpdates),
					bAsyncCompute ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(Batch.NumUpdates, 64)
				);

				UpdatesOffset += Batch.NumUpdates;
			}
		}
		
		ResetState();
	}

private:
	struct FBatch
	{
		TRefCountPtr<FRDGPooledBuffer> StreamingInfoBuffer;
		int32 UpdatesOffset;
		int32 NumUpdates;

		FBatch() = default;
		FBatch(TRefCountPtr<FRDGPooledBuffer> InStreamingInfoBuffer, int32 InUpdatesOffset) : StreamingInfoBuffer(InStreamingInfoBuffer), UpdatesOffset(InUpdatesOffset), NumUpdates(0) {}
	};

	TArray<FBatch> Batches;
	TArray<uint32> Updates;

	void ResetState()
	{
		Batches.Reset();
		Updates.Reset();
	}
};

struct FStreamingUpdateParameters
{
	FStreamingManager* StreamingManager = nullptr;
};

class FStreamingUpdateTask
{
public:
	explicit FStreamingUpdateTask(const FStreamingUpdateParameters& InParams) : Parameters(InParams) {}

	FStreamingUpdateParameters Parameters;

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		Parameters.StreamingManager->InstallReadyMipLevels();
	}

	static ESubsequentsMode::Type	GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }
	ENamedThreads::Type				GetDesiredThread() { return ENamedThreads::AnyNormalThreadNormalTask; }
	FORCEINLINE TStatId				GetStatId() const { return TStatId(); }
};

FStreamingManager::FStreamingManager()
{

}

void FStreamingManager::InitRHI()
{
	using namespace UE::DerivedData;

	if (!DoesPlatformSupportSparseVolumeTexture(GMaxRHIShaderPlatform))
	{
		return;
	}

	MaxPendingMipLevels = GSVTStreamingMaxPendingMipLevels;
	PendingMipLevels.SetNum(MaxPendingMipLevels);
	PageTableUpdater = new FPageTableUpdater();
	StreamingInfoBufferUpdater = new FStreamingInfoBufferUpdater();

#if WITH_EDITORONLY_DATA
	RequestOwner = new FRequestOwner(EPriority::Normal);
#endif
}

void FStreamingManager::ReleaseRHI()
{
	if (!DoesPlatformSupportSparseVolumeTexture(GMaxRHIShaderPlatform))
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	delete RequestOwner;
	RequestOwner = nullptr;
#endif

	delete PageTableUpdater;
	PageTableUpdater = nullptr;
	delete StreamingInfoBufferUpdater;
	StreamingInfoBufferUpdater = nullptr;
}

void FStreamingManager::Add_GameThread(UStreamableSparseVolumeTexture* SparseVolumeTexture)
{
	if (!DoesPlatformSupportSparseVolumeTexture(GMaxRHIShaderPlatform) || !SparseVolumeTexture)
	{
		return;
	}

	FNewSparseVolumeTextureInfo NewSVTInfo{};
	const int32 NumFrames = SparseVolumeTexture->GetNumFrames();
	NewSVTInfo.SVT = SparseVolumeTexture;
	NewSVTInfo.FormatA = SparseVolumeTexture->GetFormat(0);
	NewSVTInfo.FormatB = SparseVolumeTexture->GetFormat(1);
	NewSVTInfo.FallbackValueA = SparseVolumeTexture->GetFallbackValue(0);
	NewSVTInfo.FallbackValueB = SparseVolumeTexture->GetFallbackValue(1);
	NewSVTInfo.NumMipLevelsGlobal = SparseVolumeTexture->GetNumMipLevels();
	NewSVTInfo.FrameInfo.SetNum(NumFrames);

	for (int32 FrameIdx = 0; FrameIdx < NumFrames; ++FrameIdx)
	{
		USparseVolumeTextureFrame* SVTFrame = SparseVolumeTexture->GetFrame(FrameIdx);
		FFrameInfo& FrameInfo = NewSVTInfo.FrameInfo[FrameIdx];
		FrameInfo.Resources = SVTFrame->GetResources();
		FrameInfo.TextureRenderResources = SVTFrame->TextureRenderResources;
		check(FrameInfo.TextureRenderResources);
	}


	ENQUEUE_RENDER_COMMAND(SVTAdd)(
		[this, NewSVTInfoCaptured = MoveTemp(NewSVTInfo), SVTName = SparseVolumeTexture->GetName()](FRHICommandListImmediate& RHICmdList) mutable /* Required to be able to move from NewSVTInfoCaptured inside the lambda */
		{
			// We need to fully initialize the SVT streaming state (including resource creation) to ensure that valid resources exist before FillUniformBuffers() is called.
			// This is why we can't defer resource creation until BeginAsyncUpdate() is called.
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SVT::FStreamingManager::Add(%s)", *SVTName));
			AddInternal(GraphBuilder, MoveTemp(NewSVTInfoCaptured));
			GraphBuilder.Execute();
		});
}

void FStreamingManager::Remove_GameThread(UStreamableSparseVolumeTexture* SparseVolumeTexture)
{
	if (!DoesPlatformSupportSparseVolumeTexture(GMaxRHIShaderPlatform) || !SparseVolumeTexture)
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(SVTRemove)(
		[this, SparseVolumeTexture](FRHICommandListImmediate& RHICmdList)
		{
			RemoveInternal(SparseVolumeTexture);
		});
}

void FStreamingManager::BeginAsyncUpdate(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());
	check(!AsyncState.bUpdateActive);
	if (!DoesPlatformSupportSparseVolumeTexture(GMaxRHIShaderPlatform) || StreamingInfo.IsEmpty())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(SVT::FStreamingManager::BeginAsyncUpdate);

#if SVT_STREAMING_LOG_VERBOSE
	UE_LOG(LogSparseVolumeTextureStreamingManager, Display, TEXT("SVT Streaming Update %i"), NextUpdateIndex);
#endif

	AsyncState = {};
	AsyncState.bUpdateActive = true;

	// For debugging, we can stream out ALL tiles
	if (GSVTStreamingEmptyPhysicalTileTextures != 0)
	{
		TArray<FLRUNode*> MipLevelsToFree;
		for (auto& Pair : StreamingInfo)
		{
			MipLevelsToFree.Reset();
			FStreamingInfo& SVTInfo = Pair.Value;
			const int32 NumFrames = SVTInfo.PerFrameInfo.Num();
			const int32 NumMipLevelsGlobal = SVTInfo.NumMipLevelsGlobal;
			
			for (int32 MipLevel = 0; MipLevel < NumMipLevelsGlobal; ++MipLevel)
			{
				for (auto& Node : SVTInfo.PerMipLRULists[MipLevel])
				{
					MipLevelsToFree.Add(&Node);
				}
			}
			for (FLRUNode* Node : MipLevelsToFree)
			{
				StreamOutMipLevel(SVTInfo, Node);
			}
		}

		GSVTStreamingEmptyPhysicalTileTextures = 0;
	}

	AddParentRequests();
	const int32 MaxSelectedMipLevels = MaxPendingMipLevels - NumPendingMipLevels;
	SelectHighestPriorityRequestsAndUpdateLRU(MaxSelectedMipLevels);
	IssueRequests(MaxSelectedMipLevels);
	AsyncState.NumReadyMipLevels = DetermineReadyMipLevels();

	// Do a first pass over all the mips to be uploaded to compute the upload buffer size requirements.
	int32 NumPageTableUpdatesTotal = 0;
	TileDataTexturesToUpdate.Reset();
	{
		const int32 StartPendingMipLevelIndex = (NextPendingMipLevelIndex + MaxPendingMipLevels - NumPendingMipLevels) % MaxPendingMipLevels;
		for (int32 i = 0; i < AsyncState.NumReadyMipLevels; ++i)
		{
			const int32 PendingMipLevelIndex = (StartPendingMipLevelIndex + i) % MaxPendingMipLevels;
			FPendingMipLevel& PendingMipLevel = PendingMipLevels[PendingMipLevelIndex];

			FStreamingInfo* SVTInfo = StreamingInfo.Find(PendingMipLevel.SparseVolumeTexture);
			if (!SVTInfo || (SVTInfo->PerFrameInfo[PendingMipLevel.FrameIndex].LowestRequestedMipLevel > PendingMipLevel.MipLevelIndex))
			{
				continue; // Skip mip level install. SVT no longer exists or mip level was "streamed out" before it was even installed in the first place.
			}

			const FResources* Resources = SVTInfo->PerFrameInfo[PendingMipLevel.FrameIndex].Resources;
			SVTInfo->TileDataTexture->NumTilesToUpload += Resources->MipLevelStreamingInfo[PendingMipLevel.MipLevelIndex].NumPhysicalTiles;
			TileDataTexturesToUpdate.Add(SVTInfo->TileDataTexture);
			NumPageTableUpdatesTotal += Resources->MipLevelStreamingInfo[PendingMipLevel.MipLevelIndex].PageTableSize / (2 * sizeof(uint32));
		}

		PageTableUpdater->Init(GraphBuilder, NumPageTableUpdatesTotal, 1);

		for (FTileDataTexture* TileDataTexture : TileDataTexturesToUpdate)
		{
			TileDataTexture->TileUploader->Init(GraphBuilder, TileDataTexture->NumTilesToUpload, TileDataTexture->FormatA, TileDataTexture->FormatB);
		}
	}

	// Start async processing
	FStreamingUpdateParameters Parameters;
	Parameters.StreamingManager = this;
	
	check(AsyncTaskEvents.IsEmpty());
	if (GSVTStreamingAsyncThread)
	{
		AsyncTaskEvents.Add(TGraphTask<FStreamingUpdateTask>::CreateTask().ConstructAndDispatchWhenReady(Parameters));
	}
	else
	{
		InstallReadyMipLevels();
	}
}

void FStreamingManager::EndAsyncUpdate(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());
	if (!DoesPlatformSupportSparseVolumeTexture(GMaxRHIShaderPlatform) || StreamingInfo.IsEmpty())
	{
		return;
	}
	check(AsyncState.bUpdateActive);

	TRACE_CPUPROFILER_EVENT_SCOPE(SVT::FStreamingManager::EndAsyncUpdate);

	// Wait for async processing to finish
	if (GSVTStreamingAsyncThread)
	{
		check(!AsyncTaskEvents.IsEmpty());
		FTaskGraphInterface::Get().WaitUntilTasksComplete(AsyncTaskEvents, ENamedThreads::GetRenderThread_Local());
	}
	AsyncTaskEvents.Empty();

	// Clear unused mip levels to 0. SVT_TODO: We can probably skip this because the page table lookup in the shader is clamped
	if (!PageTableClears.IsEmpty())
	{
		for (auto& Clear : PageTableClears)
		{
			FRDGTexture* PageTableTextureRDG = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Clear.PageTableTexture, TEXT("SparseVolumeTexture.PageTableTexture")));
			FRDGTextureUAVDesc UAVDesc(PageTableTextureRDG, static_cast<uint8>(Clear.MipLevel), PF_R32_UINT);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(UAVDesc), FUintVector4(ForceInitToZero));
		}
		PageTableClears.Reset();
	}

	// Issue the actual data uploads
	for (FTileDataTexture* TileDataTexture : TileDataTexturesToUpdate)
	{
		TileDataTexture->TileUploader->ResourceUploadTo(GraphBuilder, TileDataTexture->TileDataTextureARHIRef, TileDataTexture->TileDataTextureBRHIRef);
		TileDataTexture->NumTilesToUpload = 0;
	}

	// Update streaming info buffers
	for (FStreamingInfo* SVTInfo : InvalidatedStreamingInfos)
	{
#if DO_CHECK
		bool bSVTInfoExists = false;
		for (const auto& Pair : StreamingInfo)
		{
			if (&Pair.Value == SVTInfo)
			{
				bSVTInfoExists = true;
				break;
			}
		}
		check(bSVTInfoExists);
#endif
		
		for (TConstSetBitIterator It(SVTInfo->DirtyStreamingInfoData); It; ++It)
		{
			const int32 FrameIndex = It.GetIndex();
			StreamingInfoBufferUpdater->Add(SVTInfo->StreamingInfoBuffer, FrameIndex, SVTInfo->PerFrameInfo[FrameIndex].LowestResidentMipLevel);
		}
	}
	InvalidatedStreamingInfos.Reset();
	StreamingInfoBufferUpdater->Apply(GraphBuilder);

	PageTableUpdater->Apply(GraphBuilder);

	check(AsyncState.NumReadyMipLevels <= NumPendingMipLevels);
	NumPendingMipLevels -= AsyncState.NumReadyMipLevels;
	++NextUpdateIndex;
	AsyncState.bUpdateActive = false;

#if DO_CHECK
	for (const auto& Pair : StreamingInfo)
	{
#if SVT_STREAMING_LOG_VERBOSE
		FString ResidentMipLevelsStr = TEXT("");
#endif
		const int32 NumFrames = Pair.Value.PerFrameInfo.Num();
		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			const auto& FrameInfo = Pair.Value.PerFrameInfo[FrameIndex];
			check(FrameInfo.LowestResidentMipLevel <= (FrameInfo.NumMipLevels - 1));
			check(FrameInfo.LowestRequestedMipLevel <= FrameInfo.LowestResidentMipLevel);
			check(FrameInfo.TextureRenderResources->GetNumLogicalMipLevels() == FrameInfo.NumMipLevels);

#if SVT_STREAMING_LOG_VERBOSE
			ResidentMipLevelsStr += FString::Printf(TEXT("%i"), FrameInfo.LowestResidentMipLevel);
#endif
		}
#if SVT_STREAMING_LOG_VERBOSE
		UE_LOG(LogSparseVolumeTextureStreamingManager, Display, TEXT("%s"), *ResidentMipLevelsStr);
#endif
	}
#endif // DO_CHECK
}

void FStreamingManager::Request_GameThread(UStreamableSparseVolumeTexture* SparseVolumeTexture, float FrameIndex, int32 MipLevel)
{
	if (!DoesPlatformSupportSparseVolumeTexture(GMaxRHIShaderPlatform) || !SparseVolumeTexture)
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(SVTRequest)(
		[this, SparseVolumeTexture, FrameIndex, MipLevel](FRHICommandListImmediate& RHICmdList)
		{
			Request(SparseVolumeTexture, FrameIndex, MipLevel);
		});
}

void FStreamingManager::Request(UStreamableSparseVolumeTexture* SparseVolumeTexture, float FrameIndex, int32 MipLevel)
{
	check(IsInRenderingThread());
	FStreamingInfo* SVTInfo = StreamingInfo.Find(SparseVolumeTexture);
	if (SVTInfo)
	{
		const int32 NumFrames = SVTInfo->PerFrameInfo.Num();
		const int32 FrameIndexI32 = static_cast<int32>(FrameIndex);
		if (FrameIndexI32 < 0 || FrameIndexI32 >= NumFrames)
		{
			return;
		}

		// Try to find a FStreamingWindow around the requested frame index. This will inform us about which direction we need to prefetch into.
		FStreamingWindow* StreamingWindow = nullptr;
		for (FStreamingWindow& Window : SVTInfo->StreamingWindows)
		{
			if (FMath::Abs(FrameIndex - Window.CenterFrame) <= FStreamingWindow::WindowSize)
			{
				StreamingWindow = &Window;
				break;
			}
		}
		// Found an existing window!
		if (StreamingWindow)
		{
			const bool bForward = StreamingWindow->LastCenterFrame <= FrameIndex;
			if (StreamingWindow->LastRequested < NextUpdateIndex)
			{
				StreamingWindow->LastCenterFrame = StreamingWindow->CenterFrame;
				StreamingWindow->CenterFrame = FrameIndex;
				StreamingWindow->NumRequestsThisUpdate = 1;
				StreamingWindow->LastRequested = NextUpdateIndex;
				StreamingWindow->bPlayForward = bForward;
				StreamingWindow->bPlayBackward = !bForward;
			}
			else
			{
				// Update the average center frame
				StreamingWindow->CenterFrame = (StreamingWindow->CenterFrame * StreamingWindow->NumRequestsThisUpdate + FrameIndex) / (StreamingWindow->NumRequestsThisUpdate + 1.0f);
				++StreamingWindow->NumRequestsThisUpdate;
				StreamingWindow->bPlayForward |= bForward;
				StreamingWindow->bPlayBackward |= !bForward;
			}
		}
		// No existing window. Create a new one.
		else
		{
			StreamingWindow = &SVTInfo->StreamingWindows.AddDefaulted_GetRef();
			StreamingWindow->CenterFrame = FrameIndex;
			StreamingWindow->LastCenterFrame = FrameIndex;
			StreamingWindow->NumRequestsThisUpdate = 1;
			StreamingWindow->LastRequested = NextUpdateIndex;
			StreamingWindow->bPlayForward = true; // No prior data, so just take a guess that playback is forwards
			StreamingWindow->bPlayBackward = false;
		}

		check(StreamingWindow);
		const int32 OffsetMagnitude = GSVTStreamingNumPrefetchFrames;
		const int32 LowerFrameOffset = StreamingWindow->bPlayBackward ? -OffsetMagnitude : 0;
		const int32 UpperFrameOffset = StreamingWindow->bPlayForward ? OffsetMagnitude : 0;

		for (int32 i = LowerFrameOffset; i <= UpperFrameOffset; ++i)
		{
			const int32 RequestFrameIndex = (static_cast<int32>(FrameIndex) + i + NumFrames) % NumFrames;
			const int32 RequestMipLevelOffset = FMath::Abs(i) + GSVTStreamingPrefetchMipLevelBias;
			FStreamingRequest Request;
			Request.Key.SVT = SparseVolumeTexture;
			Request.Key.FrameIndex = RequestFrameIndex;
			Request.Key.MipLevelIndex = FMath::Clamp(MipLevel + RequestMipLevelOffset, 0, SVTInfo->PerFrameInfo[RequestFrameIndex].NumMipLevels);
			Request.Priority = FMath::Max(0, OffsetMagnitude - FMath::Abs(i));
			AddRequest(Request);
		}

		// Clean up unused streaming windows
		SVTInfo->StreamingWindows.RemoveAll([&](const FStreamingWindow& Window) { return (NextUpdateIndex - Window.LastRequested) > 5; });
	}
}

void FStreamingManager::AddInternal(FRDGBuilder& GraphBuilder, FNewSparseVolumeTextureInfo&& NewSVTInfo)
{
	check(IsInRenderingThread());
	check(!AsyncState.bUpdateActive);
	if (!ensure(!StreamingInfo.Contains(NewSVTInfo.SVT)))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(SVT::FStreamingManager::AddInternal);

	const int32 NumFrames = NewSVTInfo.FrameInfo.Num();

	FStreamingInfo& SVTInfo = StreamingInfo.Add(NewSVTInfo.SVT);
	SVTInfo.FormatA = NewSVTInfo.FormatA;
	SVTInfo.FormatB = NewSVTInfo.FormatB;
	SVTInfo.FallbackValueA = NewSVTInfo.FallbackValueA;
	SVTInfo.FallbackValueB = NewSVTInfo.FallbackValueB;
	SVTInfo.NumMipLevelsGlobal = NewSVTInfo.NumMipLevelsGlobal;
	SVTInfo.LastRequested = 0;
	SVTInfo.PerFrameInfo = MoveTemp(NewSVTInfo.FrameInfo);
	SVTInfo.LRUNodes.SetNum(NumFrames * SVTInfo.NumMipLevelsGlobal);
	SVTInfo.PerMipLRULists.SetNum(SVTInfo.NumMipLevelsGlobal);

	int32 NumRootPhysicalTiles = 0;
	int32 MaxNumPhysicalTiles = 0;
	for (int32 FrameIdx = 0; FrameIdx < NumFrames; ++FrameIdx)
	{
		FFrameInfo& FrameInfo = SVTInfo.PerFrameInfo[FrameIdx];
		check(FrameInfo.TextureRenderResources && FrameInfo.TextureRenderResources->IsInitialized());
		const FResources* Resources = FrameInfo.Resources;

		FrameInfo.NumMipLevels = Resources->MipLevelStreamingInfo.Num();
		FrameInfo.LowestRequestedMipLevel = FrameInfo.NumMipLevels - 1;
		FrameInfo.LowestResidentMipLevel = FrameInfo.NumMipLevels - 1;
		FrameInfo.TileAllocations.SetNum(FrameInfo.NumMipLevels);
		for (int32 MipLevel = 0; MipLevel < FrameInfo.NumMipLevels; ++MipLevel)
		{
			FrameInfo.TileAllocations[MipLevel].SetNumZeroed(Resources->MipLevelStreamingInfo[MipLevel].NumPhysicalTiles);
		}
		
		int32 NumPhysicalTiles = 0;
		for (const FMipLevelStreamingInfo& MipLevelStreamingInfo : Resources->MipLevelStreamingInfo)
		{
			NumPhysicalTiles += MipLevelStreamingInfo.NumPhysicalTiles;
		}

		MaxNumPhysicalTiles = FMath::Max(NumPhysicalTiles, MaxNumPhysicalTiles);
		if (NumPhysicalTiles > 0)
		{
			++NumRootPhysicalTiles;
		}

		for (int32 MipIdx = 0; MipIdx < SVTInfo.NumMipLevelsGlobal; ++MipIdx)
		{
			FLRUNode& LRUNode = SVTInfo.LRUNodes[FrameIdx * SVTInfo.NumMipLevelsGlobal + MipIdx];
			LRUNode.Reset();
			LRUNode.FrameIndex = FrameIdx;
			LRUNode.MipLevelIndex = MipIdx < FrameInfo.NumMipLevels ? MipIdx : INDEX_NONE;

			if ((MipIdx + 1) < FrameInfo.NumMipLevels)
			{
				LRUNode.NextHigherMipLevel = &SVTInfo.LRUNodes[FrameIdx * SVTInfo.NumMipLevelsGlobal + (MipIdx + 1)];
			}
		}
	}

	// Create RHI resources and upload root tile data
	{
		const int32 TileFactor = NumFrames <= 1 ? 1 : 3;
		const int32 NumPhysicalTilesCapacity = NumRootPhysicalTiles + (TileFactor * MaxNumPhysicalTiles);
		const FIntVector3 TileDataVolumeResolution = ComputeTileDataVolumeResolution2(NumPhysicalTilesCapacity);
		const FIntVector3 TileDataVolumeResolutionInTiles = TileDataVolumeResolution / SPARSE_VOLUME_TILE_RES_PADDED;

		SVTInfo.TileDataTexture = new FTileDataTexture(TileDataVolumeResolutionInTiles, SVTInfo.FormatA, SVTInfo.FormatB);
		SVTInfo.TileDataTexture->InitResource();

		// Create streaming info buffer
		{
			FRDGBufferRef StreamingInfoBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(sizeof(uint32) * NumFrames), TEXT("SparseVolumeTexture.StreamingInfo.RHIBuffer"));
			SVTInfo.StreamingInfoBuffer = GraphBuilder.ConvertToExternalBuffer(StreamingInfoBufferRDG);
			SVTInfo.StreamingInfoBufferSRVRHIRef = SVTInfo.StreamingInfoBuffer->GetSRV();
			SVTInfo.DirtyStreamingInfoData.Init(true, NumFrames);
		}

		FTileUploader RootTileUploader;
		RootTileUploader.Init(GraphBuilder, NumRootPhysicalTiles + 1 /*null tile*/, SVTInfo.FormatA, SVTInfo.FormatB);

		// Allocate null tile
		{
			const uint32 NullTileCoord = SVTInfo.TileDataTexture->Allocate();
			check(NullTileCoord == 0);
			uint8* TileCoordsPtr = nullptr;
			uint8* DataAPtr = nullptr;
			uint8* DataBPtr = nullptr;
			RootTileUploader.Add_GetRef(1, TileCoordsPtr, DataAPtr, DataBPtr);
			FMemory::Memcpy(TileCoordsPtr, &NullTileCoord, sizeof(NullTileCoord));
			for (int32 VoxelIdx = 0; VoxelIdx < UE::SVT::SVTNumVoxelsPerPaddedTile; ++VoxelIdx)
			{
				if (SVTInfo.FormatA != PF_Unknown)
				{
					WriteVoxel(VoxelIdx, DataAPtr, SVTInfo.FormatA, SVTInfo.FallbackValueA);
				}
				if (SVTInfo.FormatB != PF_Unknown)
				{
					WriteVoxel(VoxelIdx, DataBPtr, SVTInfo.FormatB, SVTInfo.FallbackValueB);
				}
			}
		}

		// Process frames
		for (int32 FrameIdx = 0; FrameIdx < NumFrames; ++FrameIdx)
		{
			FFrameInfo& FrameInfo = SVTInfo.PerFrameInfo[FrameIdx];
			const FResources* Resources = FrameInfo.Resources;
			const int32 NumMipLevels = Resources->MipLevelStreamingInfo.Num();

			FrameInfo.LowestRequestedMipLevel = NumMipLevels - 1;
			FrameInfo.LowestResidentMipLevel = NumMipLevels - 1;

			// Initialize TextureRenderResources
			FrameInfo.TextureRenderResources->PhysicalTileDataATextureRHI = SVTInfo.TileDataTexture->TileDataTextureARHIRef;
			FrameInfo.TextureRenderResources->PhysicalTileDataBTextureRHI = SVTInfo.TileDataTexture->TileDataTextureBRHIRef;
			FrameInfo.TextureRenderResources->StreamingInfoBufferSRVRHI = SVTInfo.StreamingInfoBufferSRVRHIRef;
			FrameInfo.TextureRenderResources->Header = Resources->Header;
			FrameInfo.TextureRenderResources->TileDataTextureResolution = SVTInfo.TileDataTexture->ResolutionInTiles * SPARSE_VOLUME_TILE_RES_PADDED;
			FrameInfo.TextureRenderResources->FrameIndex = FrameIdx;
			FrameInfo.TextureRenderResources->NumLogicalMipLevels = NumMipLevels;

			// Create page table
			{
				// SVT_TODO: Currently we keep all mips of the page table resident. It would be better to stream in/out page table mips.
				const int32 NumResidentMipLevels = NumMipLevels;
				FIntVector3 PageTableResolution = Resources->Header.PageTableVolumeResolution;
				PageTableResolution = FIntVector3(FMath::Max(1, PageTableResolution.X), FMath::Max(1, PageTableResolution.Y), FMath::Max(1, PageTableResolution.Z));

				const EPixelFormat PageEntryFormat = PF_R32_UINT;
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PageTable.RHITexture"), PageTableResolution.X, PageTableResolution.Y, PageTableResolution.Z, PageEntryFormat)
					.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV)
					.SetNumMips((uint8)NumResidentMipLevels);

				FrameInfo.TextureRenderResources->PageTableTextureRHI = RHICreateTexture(Desc);
			}

			// Upload root mip data and update page tables
			const FMipLevelStreamingInfo* RootStreamingInfo = !Resources->MipLevelStreamingInfo.IsEmpty() ? &Resources->MipLevelStreamingInfo.Last() : nullptr;
			if (!Resources->RootData.IsEmpty() && RootStreamingInfo)
			{
				check((RootStreamingInfo->TileDataASize > 0) || (RootStreamingInfo->TileDataBSize > 0));

				const uint32 TileCoord = SVTInfo.TileDataTexture->Allocate();
				check(TileCoord != INDEX_NONE);
				FrameInfo.TileAllocations.Last()[0] = TileCoord;
				uint8* TileCoordsPtr = nullptr;
				uint8* DataAPtr = nullptr;
				uint8* DataBPtr = nullptr;
				RootTileUploader.Add_GetRef(1, TileCoordsPtr, DataAPtr, DataBPtr);

				FMemory::Memcpy(TileCoordsPtr, &TileCoord, sizeof(TileCoord));
				if (RootStreamingInfo->TileDataASize > 0)
				{
					const uint8* Src = Resources->RootData.GetData() + RootStreamingInfo->TileDataAOffset;
					check(DataAPtr);
					FMemory::Memcpy(DataAPtr, Src, RootStreamingInfo->TileDataASize);
				}
				if (RootStreamingInfo->TileDataBSize > 0)
				{
					const uint8* Src = Resources->RootData.GetData() + RootStreamingInfo->TileDataBOffset;
					check(DataBPtr);
					FMemory::Memcpy(DataBPtr, Src, RootStreamingInfo->TileDataBSize);
				}

				// Update highest mip (1x1x1) in page table
				const FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, 1, 1, 1);
				RHIUpdateTexture3D(FrameInfo.TextureRenderResources->PageTableTextureRHI, FrameInfo.TextureRenderResources->PageTableTextureRHI->GetDesc().NumMips - 1, UpdateRegion, sizeof(uint32), sizeof(uint32), (uint8*)&TileCoord);
			}
		}

		RootTileUploader.ResourceUploadTo(GraphBuilder, SVTInfo.TileDataTexture->TileDataTextureARHIRef, SVTInfo.TileDataTexture->TileDataTextureBRHIRef);
	}

	InvalidatedStreamingInfos.Add(&SVTInfo);

	// Add requests for all mips the first frame. This is necessary for cases where UAnimatedSparseVolumeTexture or UStaticSparseVolumeTexture
	// are directly bound to the material without getting a specific frame through USparseVolumeTextureFrame::GetFrameAndIssueStreamingRequest().
	const int32 NumMipLevelsFrame0 = SVTInfo.PerFrameInfo[0].NumMipLevels;
	for (int32 MipLevel = 0; (MipLevel + 1) < NumMipLevelsFrame0; ++MipLevel)
	{
		FStreamingRequest Request;
		Request.Key.SVT = NewSVTInfo.SVT;
		Request.Key.FrameIndex = 0;
		Request.Key.MipLevelIndex = MipLevel;
		Request.Priority = MipLevel;
		AddRequest(Request);
	}
}

void FStreamingManager::RemoveInternal(UStreamableSparseVolumeTexture* SparseVolumeTexture)
{
	check(IsInRenderingThread());
	check(!AsyncState.bUpdateActive);
	FStreamingInfo* SVTInfo = StreamingInfo.Find(SparseVolumeTexture);
	if (SVTInfo)
	{
		// The RHI resources in FTextureRenderResources are managed by FStreamingManager, so release them now.
		for (FFrameInfo& FrameInfo : SVTInfo->PerFrameInfo)
		{
			FrameInfo.TextureRenderResources->PageTableTextureRHI.SafeRelease();
			FrameInfo.TextureRenderResources->PhysicalTileDataATextureRHI.SafeRelease();
			FrameInfo.TextureRenderResources->PhysicalTileDataBTextureRHI.SafeRelease();
		}
		if (SVTInfo->TileDataTexture)
		{
			SVTInfo->TileDataTexture->ReleaseResource();
			delete SVTInfo->TileDataTexture;
		}

		StreamingInfo.Remove(SparseVolumeTexture);
	}
}

bool FStreamingManager::AddRequest(const FStreamingRequest& Request)
{
	uint32* ExistingRequestPriority = RequestsHashTable.Find(Request.Key);
	if (ExistingRequestPriority)
	{
		if (Request.Priority > *ExistingRequestPriority)
		{
			*ExistingRequestPriority = Request.Priority;
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		RequestsHashTable.Add(Request.Key, Request.Priority);
		return true;
	}
}

void FStreamingManager::AddParentRequests()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SVT::AddParentRequests);

	ParentRequestsToAdd.Reset();
	for (const auto& Request : RequestsHashTable)
	{
		FStreamingInfo* SVTInfo = StreamingInfo.Find(Request.Key.SVT);
		check(SVTInfo);
		const int32 NumStreamableMipLevels = SVTInfo->PerFrameInfo[Request.Key.FrameIndex].NumMipLevels - 1;
		uint32 Priority = Request.Value + 1;
		for (int32 MipLevelIndex = Request.Key.MipLevelIndex + 1; MipLevelIndex < NumStreamableMipLevels; ++MipLevelIndex)
		{
			FMipLevelKey ParentKey = Request.Key;
			ParentKey.MipLevelIndex = MipLevelIndex;
			
			uint32* ExistingParentRequestPriority = RequestsHashTable.Find(ParentKey);
			if (ExistingParentRequestPriority && Priority > *ExistingParentRequestPriority)
			{
				*ExistingParentRequestPriority = Priority;
			}
			else
			{
				ParentRequestsToAdd.Add(FStreamingRequest{ ParentKey, Priority });
			}

			++Priority;
		}
	}

	for (const FStreamingRequest& Request : ParentRequestsToAdd)
	{
		AddRequest(Request);
	}
}

void FStreamingManager::SelectHighestPriorityRequestsAndUpdateLRU(int32 MaxSelectedMipLevels)
{
	PrioritizedRequestsHeap.Reset();
	SelectedMipLevels.Reset();

	if (!RequestsHashTable.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SVT::SelectHighestPriorityRequestsAndUpdateLRU);

		for (const auto& Request : RequestsHashTable)
		{
			FStreamingInfo* SVTInfo = StreamingInfo.Find(Request.Key.SVT);
			check(SVTInfo);

			// Discard invalid requests
			if (Request.Key.FrameIndex < 0
				|| Request.Key.FrameIndex >= SVTInfo->PerFrameInfo.Num()
				|| Request.Key.MipLevelIndex >= (SVTInfo->PerFrameInfo[Request.Key.FrameIndex].NumMipLevels - 1))
			{
				continue;
			}

			const int32 LRUNodeIndex = Request.Key.FrameIndex * SVTInfo->NumMipLevelsGlobal + Request.Key.MipLevelIndex;
			FLRUNode* LRUNode = &SVTInfo->LRUNodes[LRUNodeIndex];
#if DO_CHECK
			bool bFoundNodeInList = false;
			for (auto& Node : SVTInfo->PerMipLRULists[Request.Key.MipLevelIndex])
			{
				if (&Node == LRUNode)
				{
					bFoundNodeInList = true;
					break;
				}
			}
#endif

			const bool bIsAlreadyStreaming = Request.Key.MipLevelIndex >= SVTInfo->PerFrameInfo[Request.Key.FrameIndex].LowestRequestedMipLevel;
			if (bIsAlreadyStreaming)
			{
				check(bFoundNodeInList);
				// Update LastRequested and move to front of LRU
				LRUNode->LastRequested = NextUpdateIndex;

				// Unlink
				LRUNode->Remove();

				// Insert at the end of the LRU list
				SVTInfo->PerMipLRULists[Request.Key.MipLevelIndex].AddTail(LRUNode);
			}
			else
			{
				check(!bFoundNodeInList);
				PrioritizedRequestsHeap.Add(FStreamingRequest{ Request.Key, Request.Value });
			}
		}

		auto PriorityPredicate = [](const auto& A, const auto& B) { return A.Priority > B.Priority; };
		PrioritizedRequestsHeap.Heapify(PriorityPredicate);

		while (SelectedMipLevels.Num() < MaxSelectedMipLevels && PrioritizedRequestsHeap.Num() > 0)
		{
			FStreamingRequest SelectedRequest;
			PrioritizedRequestsHeap.HeapPop(SelectedRequest, PriorityPredicate, false /*bAllowShrinking*/);

			FStreamingInfo* SVTInfo = StreamingInfo.Find(SelectedRequest.Key.SVT);
			if (SVTInfo)
			{
				check(SelectedRequest.Key.FrameIndex < SVTInfo->PerFrameInfo.Num());
				check(SelectedRequest.Key.MipLevelIndex < SVTInfo->PerFrameInfo[SelectedRequest.Key.FrameIndex].NumMipLevels);
				SelectedMipLevels.Push(SelectedRequest.Key);
			}
		}

		RequestsHashTable.Reset();
	}
}

void FStreamingManager::IssueRequests(int32 MaxSelectedMipLevels)
{
	using namespace UE::DerivedData;

	if (SelectedMipLevels.IsEmpty())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(SVT::IssueRequests);

#if WITH_EDITORONLY_DATA
	TArray<FCacheGetChunkRequest> DDCRequests;
	DDCRequests.Reserve(MaxSelectedMipLevels);
#endif

	FBulkDataBatchRequest::FBatchBuilder Batch = FBulkDataBatchRequest::NewBatch(SelectedMipLevels.Num());
	bool bIssueIOBatch = false;

	for (const FMipLevelKey& SelectedKey : SelectedMipLevels)
	{
		FStreamingInfo* SVTInfo = StreamingInfo.Find(SelectedKey.SVT);
		check(SVTInfo);
		check(SVTInfo->PerFrameInfo.Num() > SelectedKey.FrameIndex && SelectedKey.FrameIndex >= 0);
		check(SVTInfo->PerFrameInfo[SelectedKey.FrameIndex].LowestRequestedMipLevel > SelectedKey.MipLevelIndex);
		const FResources* Resources = SVTInfo->PerFrameInfo[SelectedKey.FrameIndex].Resources;
		check((SelectedKey.MipLevelIndex + 1) < Resources->MipLevelStreamingInfo.Num()); // The lowest/last mip level is always resident and does not stream.
		const FMipLevelStreamingInfo& MipLevelStreamingInfo = Resources->MipLevelStreamingInfo[SelectedKey.MipLevelIndex];

		FTileDataTexture* TileDataTexture = SVTInfo->TileDataTexture;
		check(TileDataTexture);

		// Ensure that enough tiles are available in the tile texture
		const int32 TileDataTextureCapacity = TileDataTexture->PhysicalTilesCapacity;
		const int32 NumAvailableTiles = TileDataTexture->GetNumAvailableTiles();
		const int32 NumRequiredTiles = MipLevelStreamingInfo.NumPhysicalTiles;
		if (NumAvailableTiles < NumRequiredTiles)
		{
#if SVT_STREAMING_LOG_VERBOSE
			UE_LOG(LogSparseVolumeTextureStreamingManager, Display, TEXT("(%i)%i IssueRequests() Frame %i Mip %i: Not enough tiles available (%i) to fit mip level (%i)"), 
				NextUpdateIndex, NextPendingMipLevelIndex, SelectedKey.FrameIndex, SelectedKey.MipLevelIndex, NumAvailableTiles, NumRequiredTiles);
#endif

			// Try to free old mip levels, starting at higher resolution mips and going up the mip chain
			TArray<FLRUNode*, TInlineAllocator<16>> MipLevelsToFree;
			int32 NumNewlyAvailableTiles = 0;
			const int32 NumMipLevelsGlobal = SVTInfo->NumMipLevelsGlobal;
			for (int32 MipLevel = 0; MipLevel < NumMipLevelsGlobal && (NumAvailableTiles + NumNewlyAvailableTiles) < NumRequiredTiles; ++MipLevel)
			{
				for (auto& Node : SVTInfo->PerMipLRULists[MipLevel])
				{
					// Only free "leaf" mip levels with no higher resolution mip levels resident. Don't free mip levels requested this frame.
					if (Node.RefCount == 0 && Node.LastRequested < NextUpdateIndex)
					{
						MipLevelsToFree.Add(&Node);
						NumNewlyAvailableTiles += SVTInfo->PerFrameInfo[Node.FrameIndex].Resources->MipLevelStreamingInfo[Node.MipLevelIndex].NumPhysicalTiles;

						// Decrement ref count of mip levels higher up the chain
						FLRUNode* Dependency = Node.NextHigherMipLevel;
						while (Dependency)
						{
							check(Dependency->RefCount > 0);
							--Dependency->RefCount;
							Dependency = Dependency->NextHigherMipLevel;
						}
					}

					// Exit once we freed enough tiles
					if ((NumAvailableTiles + NumNewlyAvailableTiles) >= NumRequiredTiles)
					{
						break;
					}
				}
			}

			// Free mip levels
			for (FLRUNode* MipLevelToFree : MipLevelsToFree)
			{
				StreamOutMipLevel(*SVTInfo, MipLevelToFree);
			}

			// Couldn't free enough tiles, so skip this mip level
			if ((NumAvailableTiles + NumNewlyAvailableTiles) < NumRequiredTiles)
			{
				UE_LOG(LogSparseVolumeTextureStreamingManager, Warning, TEXT("IssueRequests() Frame %i Mip %i: Not enough tiles available (%i) to fit mip level (%i) even after freeing"),
					SelectedKey.FrameIndex, SelectedKey.MipLevelIndex, (NumAvailableTiles + NumNewlyAvailableTiles), NumRequiredTiles);
				continue;
			}
		}

#if DO_CHECK
		for (auto& Pending : PendingMipLevels)
		{
			check(Pending.FrameIndex != SelectedKey.FrameIndex || Pending.MipLevelIndex != SelectedKey.MipLevelIndex);
		}
#endif

		const int32 PendingMipLevelIndex = NextPendingMipLevelIndex;
		FPendingMipLevel& PendingMipLevel = PendingMipLevels[PendingMipLevelIndex];
		PendingMipLevel.Reset();
		PendingMipLevel.SparseVolumeTexture = SelectedKey.SVT;
		PendingMipLevel.FrameIndex = SelectedKey.FrameIndex;
		PendingMipLevel.MipLevelIndex = SelectedKey.MipLevelIndex;
		PendingMipLevel.IssuedInFrame = NextUpdateIndex;

		const FByteBulkData& BulkData = Resources->StreamableMipLevels;
#if WITH_EDITORONLY_DATA
		const bool bDiskRequest = (!(Resources->ResourceFlags & EResourceFlag_StreamingDataInDDC) && !BulkData.IsBulkDataLoaded());
#else
		const bool bDiskRequest = true;
#endif

#if WITH_EDITORONLY_DATA
		if (!bDiskRequest)
		{
			if (Resources->ResourceFlags & EResourceFlag_StreamingDataInDDC)
			{
				DDCRequests.Add(BuildDDCRequest(*Resources, MipLevelStreamingInfo, NextPendingMipLevelIndex));
				PendingMipLevel.State = FPendingMipLevel::EState::DDC_Pending;
			}
			else
			{
				PendingMipLevel.State = FPendingMipLevel::EState::Memory;
			}
		}
		else
#endif
		{
			PendingMipLevel.RequestBuffer = FIoBuffer(MipLevelStreamingInfo.BulkSize); // SVT_TODO: Use FIoBuffer::Wrap with preallocated memory
			Batch.Read(BulkData, MipLevelStreamingInfo.BulkOffset, MipLevelStreamingInfo.BulkSize, AIOP_Low, PendingMipLevel.RequestBuffer, PendingMipLevel.Request);
			bIssueIOBatch = true;

#if WITH_EDITORONLY_DATA
			PendingMipLevel.State = FPendingMipLevel::EState::Disk;
#endif
		}

		NextPendingMipLevelIndex = (NextPendingMipLevelIndex + 1) % MaxPendingMipLevels;
		check(NumPendingMipLevels < MaxPendingMipLevels);
		++NumPendingMipLevels;

		FFrameInfo& FrameInfo = SVTInfo->PerFrameInfo[SelectedKey.FrameIndex];

		// Allocate tiles in the tile data texture
		{
			TArray<uint32>& TileAllocations = FrameInfo.TileAllocations[SelectedKey.MipLevelIndex];
			check(TileAllocations.Num() == NumRequiredTiles);
			for (int32 TileIdx = 0; TileIdx < NumRequiredTiles; ++TileIdx)
			{
				const int32 TileCoord = TileDataTexture->Allocate();
				check(TileCoord != INDEX_NONE);
				TileAllocations[TileIdx] = TileCoord;
			}
		}

		// Add to tail of LRU list
		{
			const int32 LRUNodeIndex = SelectedKey.FrameIndex * SVTInfo->NumMipLevelsGlobal + SelectedKey.MipLevelIndex;
			FLRUNode* LRUNode = &SVTInfo->LRUNodes[LRUNodeIndex];
			check(!LRUNode->IsInList());
			LRUNode->LastRequested = NextUpdateIndex;
			LRUNode->PendingMipLevelIndex = PendingMipLevelIndex;

			FLRUNode* Dependency = LRUNode->NextHigherMipLevel;
			while (Dependency)
			{
				++Dependency->RefCount;
				Dependency = Dependency->NextHigherMipLevel;
			}

			SVTInfo->PerMipLRULists[SelectedKey.MipLevelIndex].AddTail(LRUNode);
		}

#if SVT_STREAMING_LOG_VERBOSE
		UE_LOG(LogSparseVolumeTextureStreamingManager, Display, TEXT("(%i)%i StreamIn Frame %i OldReqMip %i, NewReqMip %i, ResMip %i"),
			PendingMipLevel.IssuedInFrame, PendingMipLevelIndex,
			SelectedKey.FrameIndex, 
			FrameInfo.LowestRequestedMipLevel, SelectedKey.MipLevelIndex, 
			FrameInfo.LowestResidentMipLevel);
#endif

		check(FrameInfo.LowestRequestedMipLevel == (SelectedKey.MipLevelIndex + 1));
		FrameInfo.LowestRequestedMipLevel = SelectedKey.MipLevelIndex;
	}

#if WITH_EDITORONLY_DATA
	if (!DDCRequests.IsEmpty())
	{
		RequestDDCData(DDCRequests);
		DDCRequests.Empty();
	}
#endif

	if (bIssueIOBatch)
	{
		(void)Batch.Issue();
	}
}

void FStreamingManager::StreamOutMipLevel(FStreamingInfo& SVTInfo, FLRUNode* LRUNode)
{
	const int32 FrameIndex = LRUNode->FrameIndex;
	const int32 MipLevelIndex = LRUNode->MipLevelIndex;

	FFrameInfo& FrameInfo = SVTInfo.PerFrameInfo[FrameIndex];

	check(FrameInfo.LowestResidentMipLevel >= MipLevelIndex); // mip might not have streamed in yet, so use >= instead of ==
	check(FrameInfo.LowestRequestedMipLevel == MipLevelIndex);

	// Cancel potential IO request
	check((MipLevelIndex < FrameInfo.LowestResidentMipLevel) == (LRUNode->PendingMipLevelIndex != INDEX_NONE));
	if (LRUNode->PendingMipLevelIndex != INDEX_NONE)
	{
		PendingMipLevels[LRUNode->PendingMipLevelIndex].Reset();
		LRUNode->PendingMipLevelIndex = INDEX_NONE;
	}

	const int32 NewLowestRequestedMipLevel = MipLevelIndex + 1;
	const int32 NewLowestResidentMipLevel = FMath::Max(MipLevelIndex + 1, FrameInfo.LowestResidentMipLevel);
#if SVT_STREAMING_LOG_VERBOSE
	UE_LOG(LogSparseVolumeTextureStreamingManager, Display, TEXT("(%i)%i StreamOut Frame %i OldReqMip %i, NewReqMip %i, OldResMip %i, NewResMip %i"),
		NextUpdateIndex, NextPendingMipLevelIndex,
		FrameIndex,
		FrameInfo.LowestRequestedMipLevel, NewLowestRequestedMipLevel,
		FrameInfo.LowestResidentMipLevel, NewLowestResidentMipLevel);
#endif

	// Only clear page table mip if the mip to be freed is actually resident.
	const bool bNeedsPageTableClear = MipLevelIndex >= FrameInfo.LowestResidentMipLevel;
	FrameInfo.LowestRequestedMipLevel = NewLowestRequestedMipLevel;
	FrameInfo.LowestResidentMipLevel = NewLowestResidentMipLevel;
	
	// Update the streaming info buffer data
	SVTInfo.DirtyStreamingInfoData[FrameIndex] = true;
	InvalidatedStreamingInfos.Add(&SVTInfo);

	// Unlink
	LRUNode->Remove();
	LRUNode->LastRequested = INDEX_NONE;

	if (bNeedsPageTableClear)
	{
		PageTableClears.Push({ FrameInfo.TextureRenderResources->PageTableTextureRHI, MipLevelIndex });
	}

	// Free allocated tiles
	for (uint32& TileCoord : FrameInfo.TileAllocations[MipLevelIndex])
	{
		SVTInfo.TileDataTexture->Free(TileCoord);
		TileCoord = 0;
	}
}

int32 FStreamingManager::DetermineReadyMipLevels()
{
	using namespace UE::DerivedData;

	TRACE_CPUPROFILER_EVENT_SCOPE(SVT::DetermineReadyMipLevels);

	const int32 StartPendingMipLevelIndex = (NextPendingMipLevelIndex + MaxPendingMipLevels - NumPendingMipLevels) % MaxPendingMipLevels;
	int32 NumReadyMipLevels = 0;

	for (int32 i = 0; i < NumPendingMipLevels; ++i)
	{
		const int32 PendingMipLevelIndex = (StartPendingMipLevelIndex + i) % MaxPendingMipLevels;
		FPendingMipLevel& PendingMipLevel = PendingMipLevels[PendingMipLevelIndex];

		FStreamingInfo* SVTInfo = StreamingInfo.Find(PendingMipLevel.SparseVolumeTexture);
		if (!SVTInfo)
		{
#if WITH_EDITORONLY_DATA
			// Resource is no longer there. Just mark as ready so it will be skipped later
			PendingMipLevel.State = FPendingMipLevel::EState::DDC_Ready;
#endif
			continue; 
		}

		const FResources* Resources = SVTInfo->PerFrameInfo[PendingMipLevel.FrameIndex].Resources;

#if WITH_EDITORONLY_DATA
		if (PendingMipLevel.State == FPendingMipLevel::EState::DDC_Ready)
		{
			if (PendingMipLevel.RetryCount > 0)
			{
				check(SVTInfo);
				UE_LOG(LogSparseVolumeTextureStreamingManager, Display, TEXT("SVT DDC retry succeeded for '%s' (frame %i, mip %i) on %i attempt."), 
					*Resources->ResourceName, PendingMipLevel.FrameIndex, PendingMipLevel.MipLevelIndex, PendingMipLevel.RetryCount);
			}
		}
		else if (PendingMipLevel.State == FPendingMipLevel::EState::DDC_Pending)
		{
			break;
		}
		else if (PendingMipLevel.State == FPendingMipLevel::EState::DDC_Failed)
		{
			PendingMipLevel.State = FPendingMipLevel::EState::DDC_Pending;

			if (PendingMipLevel.RetryCount == 0) // Only warn on first retry to prevent spam
			{
				UE_LOG(LogSparseVolumeTextureStreamingManager, Warning, TEXT("SVT DDC request failed for '%s' (frame %i, mip %i). Retrying..."),
					*Resources->ResourceName, PendingMipLevel.FrameIndex, PendingMipLevel.MipLevelIndex);
			}

			const FMipLevelStreamingInfo& MipLevelStreamingInfo = Resources->MipLevelStreamingInfo[PendingMipLevel.MipLevelIndex];
			FCacheGetChunkRequest Request = BuildDDCRequest(*Resources, MipLevelStreamingInfo, PendingMipLevelIndex);
			RequestDDCData(MakeArrayView(&Request, 1));

			++PendingMipLevel.RetryCount;
			break;
		}
		else if (PendingMipLevel.State == FPendingMipLevel::EState::Memory)
		{
			// Memory is always ready
		}
		else
#endif // WITH_EDITORONLY_DATA
		{
#if WITH_EDITORONLY_DATA
			check(PendingMipLevel.State == FPendingMipLevel::EState::Disk);
#endif
			if (PendingMipLevel.Request.IsCompleted())
			{
				if (!PendingMipLevel.Request.IsOk())
				{
					// Retry if IO request failed for some reason
					const FMipLevelStreamingInfo& MipLevelStreamingInfo = Resources->MipLevelStreamingInfo[PendingMipLevel.MipLevelIndex];
					UE_LOG(LogSparseVolumeTextureStreamingManager, Warning, TEXT("SVT IO request failed for '%p' (frame %i, mip %i, offset %i, size %i). Retrying..."),
						PendingMipLevel.SparseVolumeTexture, PendingMipLevel.FrameIndex, PendingMipLevel.MipLevelIndex, MipLevelStreamingInfo.BulkOffset, MipLevelStreamingInfo.BulkSize);
					
					FBulkDataBatchRequest::FBatchBuilder Batch = FBulkDataBatchRequest::NewBatch(1);
					Batch.Read(Resources->StreamableMipLevels, MipLevelStreamingInfo.BulkOffset, MipLevelStreamingInfo.BulkSize, AIOP_Low, PendingMipLevel.RequestBuffer, PendingMipLevel.Request);
					(void)Batch.Issue();
					break;
				}
			}
			else
			{
				break;
			}
		}

		++NumReadyMipLevels;
	}

	return NumReadyMipLevels;
}

void FStreamingManager::InstallReadyMipLevels()
{
	check(AsyncState.bUpdateActive);
	check(AsyncState.NumReadyMipLevels <= PendingMipLevels.Num())
	if (AsyncState.NumReadyMipLevels <= 0)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(SVT::InstallReadyMipLevels);

	const int32 StartPendingMipLevelIndex = (NextPendingMipLevelIndex + MaxPendingMipLevels - NumPendingMipLevels) % MaxPendingMipLevels;

	// Do a first pass over all the mips to be uploaded to compute the upload buffer size requirements.
	int32 NumPageTableUpdatesTotal = 0;
	for (int32 i = 0; i < AsyncState.NumReadyMipLevels; ++i)
	{
		const int32 PendingMipLevelIndex = (StartPendingMipLevelIndex + i) % MaxPendingMipLevels;
		FPendingMipLevel& PendingMipLevel = PendingMipLevels[PendingMipLevelIndex];

		FStreamingInfo* SVTInfo = StreamingInfo.Find(PendingMipLevel.SparseVolumeTexture);
		if (!SVTInfo || (SVTInfo->PerFrameInfo[PendingMipLevel.FrameIndex].LowestRequestedMipLevel > PendingMipLevel.MipLevelIndex))
		{
			continue; // Skip mip level install. SVT no longer exists or mip level was "streamed out" before it was even installed in the first place.
		}

		const FResources* Resources = SVTInfo->PerFrameInfo[PendingMipLevel.FrameIndex].Resources;
		SVTInfo->TileDataTexture->NumTilesToUpload += Resources->MipLevelStreamingInfo[PendingMipLevel.MipLevelIndex].NumPhysicalTiles;
		NumPageTableUpdatesTotal += Resources->MipLevelStreamingInfo[PendingMipLevel.MipLevelIndex].PageTableSize / (2 * sizeof(uint32));
	}

	UploadTasks.Reset();
	UploadTasks.Reserve(AsyncState.NumReadyMipLevels * 2 /*slack for splitting large uploads*/);
	UploadCleanupTasks.Reset();
	

#if WITH_EDITORONLY_DATA
	TMap<const FResources*, const uint8*> ResourceToBulkPointer;
#endif

	// Do a second pass over all ready mip levels, claiming memory in the upload buffers and creating FUploadTasks
	for (int32 i = 0; i < AsyncState.NumReadyMipLevels; ++i)
	{
		const int32 PendingMipLevelIndex = (StartPendingMipLevelIndex + i) % MaxPendingMipLevels;
		FPendingMipLevel& PendingMipLevel = PendingMipLevels[PendingMipLevelIndex];

		FStreamingInfo* SVTInfo = StreamingInfo.Find(PendingMipLevel.SparseVolumeTexture);
		if (!SVTInfo || (SVTInfo->PerFrameInfo[PendingMipLevel.FrameIndex].LowestRequestedMipLevel > PendingMipLevel.MipLevelIndex))
		{
			PendingMipLevel.Reset();
			continue; // Skip mip level install. SVT no longer exists or mip level was "streamed out" before it was even installed in the first place.
		}

		FFrameInfo& FrameInfo = SVTInfo->PerFrameInfo[PendingMipLevel.FrameIndex];
		const FResources* Resources = FrameInfo.Resources;
		const FMipLevelStreamingInfo& MipLevelStreamingInfo = Resources->MipLevelStreamingInfo[PendingMipLevel.MipLevelIndex];

		const uint8* SrcPtr = nullptr;

#if WITH_EDITORONLY_DATA
		if (PendingMipLevel.State == FPendingMipLevel::EState::DDC_Ready)
		{
			check(Resources->ResourceFlags & EResourceFlag_StreamingDataInDDC);
			SrcPtr = (const uint8*)PendingMipLevel.SharedBuffer.GetData();
		}
		else if (PendingMipLevel.State == FPendingMipLevel::EState::Memory)
		{
			const uint8** BulkDataPtrPtr = ResourceToBulkPointer.Find(Resources);
			if (BulkDataPtrPtr)
			{
				SrcPtr = *BulkDataPtrPtr + MipLevelStreamingInfo.BulkOffset;
			}
			else
			{
				const FByteBulkData& BulkData = Resources->StreamableMipLevels;
				check(BulkData.IsBulkDataLoaded() && BulkData.GetBulkDataSize() > 0);
				const uint8* BulkDataPtr = (const uint8*)BulkData.LockReadOnly();
				ResourceToBulkPointer.Add(Resources, BulkDataPtr);
				SrcPtr = BulkDataPtr + MipLevelStreamingInfo.BulkOffset;
			}
		}
		else
#endif
		{
#if WITH_EDITORONLY_DATA
			check(PendingMipLevel.State == FPendingMipLevel::EState::Disk);
#endif
			SrcPtr = PendingMipLevel.RequestBuffer.GetData();
		}

		check(SrcPtr);

		const int32 NumPhysicalTiles = MipLevelStreamingInfo.NumPhysicalTiles;
		TArray<uint32>& TileAllocations = FrameInfo.TileAllocations[PendingMipLevel.MipLevelIndex];
		check(TileAllocations.Num() == NumPhysicalTiles);
		check((MipLevelStreamingInfo.PageTableSize % (sizeof(uint32) * 2)) == 0);
		const int32 NumPageTableUpdates = MipLevelStreamingInfo.PageTableSize / (sizeof(uint32) * 2);

		uint8* DstPhysicalTileCoords = nullptr;
		uint8* DstPhysicalTileDataA = nullptr;
		uint8* DstPhysicalTileDataB = nullptr;
		SVTInfo->TileDataTexture->TileUploader->Add_GetRef(NumPhysicalTiles, DstPhysicalTileCoords, DstPhysicalTileDataA, DstPhysicalTileDataB);

		uint8* DstPageCoords = nullptr;
		uint8* DstPageEntries = nullptr;
		PageTableUpdater->Add_GetRef(FrameInfo.TextureRenderResources->PageTableTextureRHI, PendingMipLevel.MipLevelIndex, NumPageTableUpdates, DstPageCoords, DstPageEntries);

		// Tile data
		{
			check(MipLevelStreamingInfo.TileDataASize == (SVTNumVoxelsPerPaddedTile * GPixelFormats[SVTInfo->TileDataTexture->FormatA].BlockBytes * NumPhysicalTiles));
			check(MipLevelStreamingInfo.TileDataBSize == (SVTNumVoxelsPerPaddedTile * GPixelFormats[SVTInfo->TileDataTexture->FormatB].BlockBytes * NumPhysicalTiles));

			FUploadTask& Task = UploadTasks.AddDefaulted_GetRef();
			Task.TaskType = FUploadTask::ETaskType::TileData;
			Task.TileDataTask.DstA = DstPhysicalTileDataA;
			Task.TileDataTask.DstB = DstPhysicalTileDataB;
			Task.TileDataTask.DstPhysicalTileCoords = DstPhysicalTileCoords;
			Task.TileDataTask.SrcA = SrcPtr + MipLevelStreamingInfo.TileDataAOffset;
			Task.TileDataTask.SrcB = SrcPtr + MipLevelStreamingInfo.TileDataBOffset;
			Task.TileDataTask.SrcPhysicalTileCoords = reinterpret_cast<const uint8*>(TileAllocations.GetData());
			Task.TileDataTask.SizeA = MipLevelStreamingInfo.TileDataASize;
			Task.TileDataTask.SizeB = MipLevelStreamingInfo.TileDataBSize;
			Task.TileDataTask.NumPhysicalTiles = NumPhysicalTiles;
		}

		// Page table
		{
			FUploadTask& Task = UploadTasks.AddDefaulted_GetRef();
			Task.TaskType = FUploadTask::ETaskType::PageTable;
			Task.PageTableTask.PendingMipLevel = &PendingMipLevel;
			Task.PageTableTask.DstPageCoords = DstPageCoords;
			Task.PageTableTask.DstPageEntries = DstPageEntries;
			Task.PageTableTask.SrcPageCoords = SrcPtr + MipLevelStreamingInfo.PageTableOffset;
			Task.PageTableTask.SrcPageEntries = SrcPtr + MipLevelStreamingInfo.PageTableOffset + NumPageTableUpdates * sizeof(uint32);
			Task.PageTableTask.NumPageTableUpdates = NumPageTableUpdates;
		}

		// Cleanup
		{
			UploadCleanupTasks.Add(&PendingMipLevel);
		}
	
#if SVT_STREAMING_LOG_VERBOSE
		UE_LOG(LogSparseVolumeTextureStreamingManager, Display, TEXT("(%i)%i Install Frame %i OldResMip %i, NewResMip %i, ReqMip %i"),
			PendingMipLevel.IssuedInFrame, PendingMipLevelIndex,
			PendingMipLevel.FrameIndex, 
			FrameInfo.LowestResidentMipLevel, PendingMipLevel.MipLevelIndex,
			FrameInfo.LowestRequestedMipLevel);
#endif

		check(FrameInfo.LowestResidentMipLevel == (PendingMipLevel.MipLevelIndex + 1));
		FrameInfo.LowestResidentMipLevel = PendingMipLevel.MipLevelIndex;

		// Update the streaming info buffer data
		SVTInfo->DirtyStreamingInfoData[PendingMipLevel.FrameIndex] = true;
		InvalidatedStreamingInfos.Add(SVTInfo);

		const int32 LRUNodeIndex = PendingMipLevel.FrameIndex * SVTInfo->NumMipLevelsGlobal + PendingMipLevel.MipLevelIndex;
		SVTInfo->LRUNodes[LRUNodeIndex].PendingMipLevelIndex = INDEX_NONE;
	}

	// Do all the memcpy's in parallel
	ParallelFor(UploadTasks.Num(), [&](int32 TaskIndex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SVT::FUploadTask);

			FUploadTask& Task = UploadTasks[TaskIndex];

			switch (Task.TaskType)
			{
			case FUploadTask::ETaskType::PageTable:
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SVT::PageTableUpload);
				if (Task.PageTableTask.NumPageTableUpdates > 0)
				{
					FMemory::Memcpy(Task.PageTableTask.DstPageCoords, Task.PageTableTask.SrcPageCoords, Task.PageTableTask.NumPageTableUpdates * sizeof(uint32));

					FStreamingInfo* SVTInfo = StreamingInfo.Find(Task.PageTableTask.PendingMipLevel->SparseVolumeTexture);
					TArray<uint32>& TileAllocations = SVTInfo->PerFrameInfo[Task.PageTableTask.PendingMipLevel->FrameIndex].TileAllocations[Task.PageTableTask.PendingMipLevel->MipLevelIndex];
					const uint32* SrcEntries = reinterpret_cast<const uint32*>(Task.PageTableTask.SrcPageEntries);
					uint32* DstEntries = reinterpret_cast<uint32*>(Task.PageTableTask.DstPageEntries);
					for (int32 i = 0; i < Task.PageTableTask.NumPageTableUpdates; ++i)
					{
						DstEntries[i] = TileAllocations[SrcEntries[i]];
					}
				}
				break;
			}
			case FUploadTask::ETaskType::TileData:
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SVT::TileDataUpload);
				FMemory::Memcpy(Task.TileDataTask.DstPhysicalTileCoords, Task.TileDataTask.SrcPhysicalTileCoords, Task.TileDataTask.NumPhysicalTiles * sizeof(uint32));
				if (Task.TileDataTask.SizeA > 0)
				{
					FMemory::Memcpy(Task.TileDataTask.DstA, Task.TileDataTask.SrcA, Task.TileDataTask.SizeA);
				}
				if (Task.TileDataTask.SizeB > 0)
				{
					FMemory::Memcpy(Task.TileDataTask.DstB, Task.TileDataTask.SrcB, Task.TileDataTask.SizeB);
				}
				break;
			}
			default:
				checkNoEntry();
			}
		});

	ParallelFor(UploadCleanupTasks.Num(), [&](int32 TaskIndex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SVT::FUploadCleanupTask);

			FPendingMipLevel* PendingMipLevel = UploadCleanupTasks[TaskIndex];
#if WITH_EDITORONLY_DATA
			PendingMipLevel->SharedBuffer.Reset();
#endif
			if (!PendingMipLevel->Request.IsNone())
			{
				check(PendingMipLevel->Request.IsCompleted());
				PendingMipLevel->Request.Reset();
			}
		});

#if DO_CHECK // Clear processed pending mip levels for better debugging
	for (int32 i = 0; i < AsyncState.NumReadyMipLevels; ++i)
	{
		const int32 PendingMipLevelIndex = (StartPendingMipLevelIndex + i) % MaxPendingMipLevels;
		PendingMipLevels[PendingMipLevelIndex].Reset();
	}
#endif

#if WITH_EDITORONLY_DATA
	// Unlock BulkData
	for (auto& Pair : ResourceToBulkPointer)
	{
		Pair.Key->StreamableMipLevels.Unlock();
	}
#endif
}

#if WITH_EDITORONLY_DATA

UE::DerivedData::FCacheGetChunkRequest FStreamingManager::BuildDDCRequest(const FResources& Resources, const FMipLevelStreamingInfo& MipLevelStreamingInfo, const uint32 PendingMipLevelIndex)
{
	using namespace UE::DerivedData;

	FCacheKey Key;
	Key.Bucket = FCacheBucket(TEXT("SparseVolumeTexture"));
	Key.Hash = Resources.DDCKeyHash;
	check(!Resources.DDCRawHash.IsZero());

	FCacheGetChunkRequest Request;
	Request.Id = FValueId::FromName("SparseVolumeTextureStreamingData");
	Request.Key = Key;
	Request.RawOffset = MipLevelStreamingInfo.BulkOffset;
	Request.RawSize = MipLevelStreamingInfo.BulkSize;
	Request.RawHash = Resources.DDCRawHash;
	Request.UserData = (((uint64)PendingMipLevelIndex) << uint64(32)) | (uint64)PendingMipLevels[PendingMipLevelIndex].RequestVersion;
	return Request;
}

void FStreamingManager::RequestDDCData(TConstArrayView<UE::DerivedData::FCacheGetChunkRequest> DDCRequests)
{
	using namespace UE::DerivedData;

	FRequestBarrier Barrier(*RequestOwner);	// This is a critical section on the owner. It does not constrain ordering
	GetCache().GetChunks(DDCRequests, *RequestOwner,
		[this](FCacheGetChunkResponse&& Response)
		{
			const uint32 PendingMipLevelIndex = (uint32)(Response.UserData >> uint64(32));
			const uint32 RequestVersion = (uint32)Response.UserData;
			
			// In case the request returned after the mip level was already streamed out again we need to abort so that we do not overwrite data in the FPendingMipLevel slot.
			if (RequestVersion < PendingMipLevels[PendingMipLevelIndex].RequestVersion)
			{
				return;
			}
			
			FPendingMipLevel& PendingMipLevel = PendingMipLevels[PendingMipLevelIndex];
			check(PendingMipLevel.SparseVolumeTexture); // A valid PendingMipLevel should have a non-nullptr here

			if (Response.Status == EStatus::Ok)
			{
				PendingMipLevel.SharedBuffer = MoveTemp(Response.RawData);
				PendingMipLevel.State = FPendingMipLevel::EState::DDC_Ready;
			}
			else
			{
				PendingMipLevel.State = FPendingMipLevel::EState::DDC_Failed;
			}
		});
}

#endif // WITH_EDITORONLY_DATA

FStreamingManager::FTileDataTexture::FTileDataTexture(const FIntVector3& InResolutionInTiles, EPixelFormat InFormatA, EPixelFormat InFormatB)
	: ResolutionInTiles(InResolutionInTiles), 
	PhysicalTilesCapacity(InResolutionInTiles.X * InResolutionInTiles.Y * InResolutionInTiles.Z), 
	FormatA(InFormatA),
	FormatB(InFormatB),
	TileUploader(MakeUnique<FTileUploader>()),
	NumTilesToUpload(0)
{
	const int64 MaxFormatSize = FMath::Max(GPixelFormats[FormatA].BlockBytes, GPixelFormats[FormatB].BlockBytes);
	const FIntVector3 LargestPossibleResolution = ComputeLargestPossibleTileDataVolumeResolution(MaxFormatSize);
	const int32 LargestPossiblePhysicalTilesCapacity = LargestPossibleResolution.X * LargestPossibleResolution.Y * LargestPossibleResolution.Z;
	
	// Ensure that the tile data texture(s) do not exceed the memory size and resolution limits.
	if (PhysicalTilesCapacity > LargestPossiblePhysicalTilesCapacity
		|| (ResolutionInTiles.X * SPARSE_VOLUME_TILE_RES_PADDED) > SVTMaxVolumeTextureDim
		|| (ResolutionInTiles.Y * SPARSE_VOLUME_TILE_RES_PADDED) > SVTMaxVolumeTextureDim
		|| (ResolutionInTiles.Z * SPARSE_VOLUME_TILE_RES_PADDED) > SVTMaxVolumeTextureDim)
	{
		ResolutionInTiles = LargestPossibleResolution;
		PhysicalTilesCapacity = LargestPossiblePhysicalTilesCapacity;

		UE_LOG(LogSparseVolumeTextureStreamingManager, Warning, TEXT("Requested SparseVolumeTexture tile data texture resolution (in tiles) (%i, %i, %i) exceeds the resource size limit. Using the maximum value of (%i, %i. %i) instead."),
			InResolutionInTiles.X, InResolutionInTiles.Y, InResolutionInTiles.Z,
			LargestPossibleResolution.X, LargestPossibleResolution.Y, LargestPossibleResolution.Z);
	}

	const FIntVector3 Resolution = ResolutionInTiles * SPARSE_VOLUME_TILE_RES_PADDED;
	check(Resolution.X <= SVTMaxVolumeTextureDim && Resolution.Y <= SVTMaxVolumeTextureDim && Resolution.Z <= SVTMaxVolumeTextureDim);
	check(((int64)Resolution.X * (int64)Resolution.Y * (int64)Resolution.Z * (int64)GPixelFormats[FormatA].BlockBytes) <= int64(INT32_MAX));
	check(((int64)Resolution.X * (int64)Resolution.Y * (int64)Resolution.Z * (int64)GPixelFormats[FormatB].BlockBytes) <= int64(INT32_MAX));
	
	TileCoords.SetNum(PhysicalTilesCapacity);

	int32 TileCoordsIndex = 0;
	for (int32 Z = 0; Z < ResolutionInTiles.Z; ++Z)
	{
		for (int32 Y = 0; Y < ResolutionInTiles.Y; ++Y)
		{
			for (int32 X = 0; X < ResolutionInTiles.X; ++X)
			{
				uint32 PackedCoord = 0;
				PackedCoord |= (X & 0xFFu);
				PackedCoord |= (Y & 0xFFu) << 8u;
				PackedCoord |= (Z & 0xFFu) << 16u;
				TileCoords[TileCoordsIndex++] = PackedCoord;
			}
		}
	}
	check(TileCoordsIndex == PhysicalTilesCapacity);
}

void FStreamingManager::FTileDataTexture::InitRHI()
{
	const FIntVector3 Resolution = ResolutionInTiles * SPARSE_VOLUME_TILE_RES_PADDED;
	if (FormatA != PF_Unknown)
	{
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PhysicalTileDataA.RHITexture"), Resolution.X, Resolution.Y, Resolution.Z, FormatA)
			.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
		TileDataTextureARHIRef = RHICreateTexture(Desc);
	}
	if (FormatB != PF_Unknown)
	{
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PhysicalTileDataB.RHITexture"), Resolution.X, Resolution.Y, Resolution.Z, FormatB)
			.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
		TileDataTextureBRHIRef = RHICreateTexture(Desc);
	}
}

void FStreamingManager::FTileDataTexture::ReleaseRHI()
{
}

TGlobalResource<FStreamingManager> GStreamingManager;

}
}
