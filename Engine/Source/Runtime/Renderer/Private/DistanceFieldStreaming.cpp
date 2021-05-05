// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldStreaming.cpp
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "Async/ParallelFor.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "SceneUtils.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "DistanceFieldLightingShared.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "GlobalDistanceField.h"

extern int32 GDFReverseAtlasAllocationOrder;

static TAutoConsoleVariable<int32> CVarBrickAtlasSizeXYInBricks(
	TEXT("r.DistanceFields.BrickAtlasSizeXYInBricks"),
	128,	
	TEXT("Controls the allocation granularity of the atlas, which grows in Z."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarMaxAtlasDepthInBricks(
	TEXT("r.DistanceFields.BrickAtlasMaxSizeZ"),
	32,	
	TEXT("Target for maximum depth of the Mesh Distance Field atlas, in 8^3 bricks.  32 => 128 * 128 * 32 * 8^3 = 256Mb.  Actual atlas size can go over since mip2 is always loaded."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarTextureUploadLimitKBytes(
	TEXT("r.DistanceFields.TextureUploadLimitKBytes"),
	8192,	
	TEXT("Max KB of distance field texture data to upload per frame from streaming requests."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarResizeAtlasEveryFrame(
	TEXT("r.DistanceFields.Debug.ResizeAtlasEveryFrame"),
	0,	
	TEXT("Whether to resize the Distance Field atlas every frame, which is useful for debugging."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDebugForceNumMips(
	TEXT("r.DistanceFields.Debug.ForceNumMips"),
	0,	
	TEXT("When set to > 0, overrides the requested number of mips for streaming.  1 = only lowest resolution mip loaded, 3 = all mips loaded.  Mips will still be clamped by available space in the atlas."),
	ECVF_RenderThreadSafe);

int32 GDistanceFieldAtlasLogStats = 0;
FAutoConsoleVariableRef CVarDistanceFieldAtlasLogStats(
	TEXT("r.DistanceFields.LogAtlasStats"),
	GDistanceFieldAtlasLogStats,
	TEXT("Set to 1 to dump atlas stats, set to 2 to dump atlas and SDF asset stats."),
	ECVF_RenderThreadSafe
	);

const int32 MaxStreamingRequests = 4095;
const int32 DistanceFieldBlockAllocatorSizeInBricks = 16;

class FCopyDistanceFieldAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCopyDistanceFieldAtlasCS)
	SHADER_USE_PARAMETER_STRUCT(FCopyDistanceFieldAtlasCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<UNORM float>, RWDistanceFieldBrickAtlas)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldAtlasParameters, DistanceFieldAtlas)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportDistanceFields(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 4;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FCopyDistanceFieldAtlasCS, "/Engine/Private/DistanceFieldStreaming.usf", "CopyDistanceFieldAtlasCS", SF_Compute);


class FScatterUploadDistanceFieldAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScatterUploadDistanceFieldAtlasCS)
	SHADER_USE_PARAMETER_STRUCT(FScatterUploadDistanceFieldAtlasCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<UNORM float>, RWDistanceFieldBrickAtlas)
		SHADER_PARAMETER_SRV(Buffer<uint3>, BrickUploadCoordinates)
		SHADER_PARAMETER_SRV(Buffer<float>, BrickUploadData)
		SHADER_PARAMETER(uint32, StartBrickIndex)
		SHADER_PARAMETER(uint32, NumBrickUploads)
		SHADER_PARAMETER(uint32, BrickSize)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportDistanceFields(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 4;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FScatterUploadDistanceFieldAtlasCS, "/Engine/Private/DistanceFieldStreaming.usf", "ScatterUploadDistanceFieldAtlasCS", SF_Compute);


class FComputeDistanceFieldAssetWantedMipsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeDistanceFieldAssetWantedMipsCS)
	SHADER_USE_PARAMETER_STRUCT(FComputeDistanceFieldAssetWantedMipsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWDistanceFieldAssetWantedNumMips)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWDistanceFieldAssetStreamingRequests)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, DistanceFieldObjectBuffers)
		SHADER_PARAMETER(int32, DebugForceNumMips)
		SHADER_PARAMETER(FVector3f, Mip1WorldCenter)
		SHADER_PARAMETER(FVector3f, Mip1WorldExtent)
		SHADER_PARAMETER(FVector3f, Mip2WorldCenter)
		SHADER_PARAMETER(FVector3f, Mip2WorldExtent)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportDistanceFields(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FComputeDistanceFieldAssetWantedMipsCS, "/Engine/Private/DistanceFieldStreaming.usf", "ComputeDistanceFieldAssetWantedMipsCS", SF_Compute);


class FGenerateDistanceFieldAssetStreamingRequestsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateDistanceFieldAssetStreamingRequestsCS)
	SHADER_USE_PARAMETER_STRUCT(FGenerateDistanceFieldAssetStreamingRequestsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWDistanceFieldAssetStreamingRequests)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DistanceFieldAssetWantedNumMips)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, DistanceFieldObjectBuffers)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldAtlasParameters, DistanceFieldAtlasParameters)
		SHADER_PARAMETER(uint32, NumDistanceFieldAssets)
		SHADER_PARAMETER(uint32, MaxNumStreamingRequests)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportDistanceFields(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FGenerateDistanceFieldAssetStreamingRequestsCS, "/Engine/Private/DistanceFieldStreaming.usf", "GenerateDistanceFieldAssetStreamingRequestsCS", SF_Compute);

const int32 AssetDataMipStrideFloat4s = 3;

FIntVector GetBrickCoordinate(int32 BrickIndex, FIntVector BrickAtlasSize)
{
	return FIntVector(
		BrickIndex % BrickAtlasSize.X,
		(BrickIndex / BrickAtlasSize.X) % BrickAtlasSize.Y,
		BrickIndex / (BrickAtlasSize.X * BrickAtlasSize.Y));
}

class FDistanceFieldAtlasUpload
{
public:
	FReadBuffer& BrickUploadCoordinatesBuffer;
	FReadBuffer& BrickUploadDataBuffer;

	FIntVector4* BrickUploadCoordinatesPtr;
	uint8* BrickUploadDataPtr;

	FDistanceFieldAtlasUpload(
		FReadBuffer& InBrickUploadCoordinatesBuffer, 
		FReadBuffer& InBrickUploadDataBuffer) : 
		BrickUploadCoordinatesBuffer(InBrickUploadCoordinatesBuffer),
		BrickUploadDataBuffer(InBrickUploadDataBuffer)
	{}

	void AllocateAndLock(uint32 NumBrickUploads, uint32 BrickSize)
	{
		const uint32 NumCoordElements = FMath::RoundUpToPowerOfTwo(NumBrickUploads);
		const uint32 CoordNumBytesPerElement = GPixelFormats[PF_R32G32B32A32_UINT].BlockBytes;

		if (BrickUploadCoordinatesBuffer.NumBytes < NumCoordElements * CoordNumBytesPerElement)
		{
			BrickUploadCoordinatesBuffer.Initialize(TEXT("DistanceFields.BrickUploadCoordinatesBuffer"), CoordNumBytesPerElement, NumCoordElements, PF_R32G32B32A32_UINT, BUF_Volatile);
		}

		const uint32 NumBrickDataElements = FMath::RoundUpToPowerOfTwo(NumBrickUploads) * BrickSize * BrickSize * BrickSize;
		const uint32 BrickDataNumBytesPerElement = GPixelFormats[DistanceField::DistanceFieldFormat].BlockBytes;

		if (BrickUploadDataBuffer.NumBytes < NumBrickDataElements * BrickDataNumBytesPerElement 
			|| (BrickUploadDataBuffer.NumBytes > NumBrickDataElements * BrickDataNumBytesPerElement && BrickUploadDataBuffer.NumBytes > 32 * 1024 * 1024))
		{
			BrickUploadDataBuffer.Initialize(TEXT("DistanceFields.BrickUploadDataBuffer"), BrickDataNumBytesPerElement, NumBrickDataElements, DistanceField::DistanceFieldFormat, BUF_Volatile);
		}

		BrickUploadCoordinatesPtr = (FIntVector4*)RHILockBuffer(BrickUploadCoordinatesBuffer.Buffer, 0, NumCoordElements * CoordNumBytesPerElement, RLM_WriteOnly);
		BrickUploadDataPtr = (uint8*)RHILockBuffer(BrickUploadDataBuffer.Buffer, 0, NumBrickDataElements * BrickDataNumBytesPerElement, RLM_WriteOnly);
	}

	void Unlock() const
	{
		RHIUnlockBuffer(BrickUploadCoordinatesBuffer.Buffer);
		RHIUnlockBuffer(BrickUploadDataBuffer.Buffer);
	}
};

void FDistanceFieldBlockAllocator::Allocate(int32 NumBlocks, TArray<int32, TInlineAllocator<4>>& OutBlocks)
{
	OutBlocks.Empty(NumBlocks);
	OutBlocks.AddUninitialized(NumBlocks);

	const int32 NumFree = FMath::Min(NumBlocks, FreeBlocks.Num());

	if (NumFree > 0)
	{
		for (int32 i = 0; i < NumFree; i++)
		{
			OutBlocks[i] = FreeBlocks[FreeBlocks.Num() - i - 1];
		}

		FreeBlocks.RemoveAt(FreeBlocks.Num() - NumFree, NumFree, false);
	}
		
	const int32 NumRemaining = NumBlocks - NumFree;

	for (int32 i = 0; i < NumRemaining; i++)
	{
		OutBlocks[i + NumFree] = MaxNumBlocks + i;
	}
	MaxNumBlocks += NumRemaining;
}

void FDistanceFieldBlockAllocator::Free(const TArray<int32, TInlineAllocator<4>>& ElementRange)
{
	FreeBlocks.Append(ElementRange);
}

class FDistanceFieldStreamingUpdateTask
{
public:
	explicit FDistanceFieldStreamingUpdateTask(const FDistanceFieldAsyncUpdateParameters& InParams) : Parameters(InParams) {}

	FDistanceFieldAsyncUpdateParameters Parameters;

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		Parameters.DistanceFieldSceneData->AsyncUpdate(Parameters);
	}

	static ESubsequentsMode::Type	GetSubsequentsMode()	{ return ESubsequentsMode::TrackSubsequents; }
	ENamedThreads::Type				GetDesiredThread()		{ return ENamedThreads::AnyNormalThreadNormalTask; }
	FORCEINLINE TStatId				GetStatId() const		{ return TStatId(); }
};

void FDistanceFieldSceneData::AsyncUpdate(FDistanceFieldAsyncUpdateParameters UpdateParameters)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FDistanceFieldSceneData_AsyncUpdate);
	TRACE_CPUPROFILER_EVENT_SCOPE(FDistanceFieldSceneData::AsyncUpdate);

	const uint32 BrickSizeBytes = GPixelFormats[DistanceField::DistanceFieldFormat].BlockBytes * DistanceField::BrickSize * DistanceField::BrickSize * DistanceField::BrickSize;

	int32 BrickUploadIndex = 0;

	for (FDistanceFieldReadRequest ReadRequest : UpdateParameters.ReadRequestsToUpload)
	{
		const FDistanceFieldAssetState& AssetState = AssetStateArray[ReadRequest.AssetSetId];
		const int32 ReversedMipIndex = ReadRequest.ReversedMipIndex;
		const FDistanceFieldAssetMipState& MipState = AssetState.ReversedMips[ReversedMipIndex];
		const int32 MipIndex = AssetState.BuiltData->Mips.Num() - ReversedMipIndex - 1;
		const FSparseDistanceFieldMip& MipBuiltData = AssetState.BuiltData->Mips[MipIndex];

		const uint8* BulkDataReadPtr = ReadRequest.BulkData ? ReadRequest.ReadOutputDataPtr : ReadRequest.AlwaysLoadedDataPtr;

#if WITH_EDITOR
		if (ReadRequest.BulkData)
		{
			check(ReadRequest.BulkData->IsBulkDataLoaded() && ReadRequest.BulkData->GetBulkDataSize() > 0);
			BulkDataReadPtr = (const uint8*)ReadRequest.BulkData->LockReadOnly() + ReadRequest.BulkOffset;
		}
#endif
		const int32 NumIndirectionEntries = MipBuiltData.IndirectionDimensions.X * MipBuiltData.IndirectionDimensions.Y * MipBuiltData.IndirectionDimensions.Z;
		const uint32 ExpectedBulkSize = NumIndirectionEntries * sizeof(uint32) + ReadRequest.NumDistanceFieldBricks * BrickSizeBytes;

		check(ReadRequest.BuiltDataId == AssetState.BuiltData->GetId());
		checkf(ReadRequest.BulkSize == ExpectedBulkSize, 
			TEXT("Bulk size mismatch: BulkSize %u, ExpectedSize %u, NumIndirectionEntries %u, NumBricks %u, ReversedMip %u"),
			ReadRequest.BulkSize,
			ExpectedBulkSize,
			NumIndirectionEntries,
			ReadRequest.NumDistanceFieldBricks,
			ReversedMipIndex);

		const uint32* SourceIndirectionTable = (const uint32*)BulkDataReadPtr;
		const int32* RESTRICT GlobalBlockOffsets = MipState.AllocatedBlocks.GetData();
		uint32* DestIndirectionTable = (uint32*)IndirectionTableUploadBuffer.Add_GetRef(MipState.IndirectionTableOffset, NumIndirectionEntries);

		// Add global allocated brick offset to indirection table entries as we upload them
		for (int32 i = 0; i < NumIndirectionEntries; i++)
		{
			const uint32 BrickIndex = SourceIndirectionTable[i];
			uint32 GlobalBrickIndex = DistanceField::InvalidBrickIndex;

			if (BrickIndex != DistanceField::InvalidBrickIndex)
			{
				const int32 BlockIndex = BrickIndex / DistanceFieldBlockAllocatorSizeInBricks;

				if (BlockIndex < MipState.AllocatedBlocks.Num())
				{
					GlobalBrickIndex = BrickIndex % DistanceFieldBlockAllocatorSizeInBricks + GlobalBlockOffsets[BlockIndex] * DistanceFieldBlockAllocatorSizeInBricks;
				}
			}
			DestIndirectionTable[i] = GlobalBrickIndex;
		}

		check(MipState.NumBricks == ReadRequest.NumDistanceFieldBricks);
		const uint8* DistanceFieldBrickDataPtr = BulkDataReadPtr + NumIndirectionEntries * sizeof(uint32);
		const SIZE_T DistanceFieldBrickDataSizeBytes = ReadRequest.NumDistanceFieldBricks * BrickSizeBytes;
		FMemory::Memcpy(UpdateParameters.BrickUploadDataPtr + BrickUploadIndex * BrickSizeBytes, DistanceFieldBrickDataPtr, DistanceFieldBrickDataSizeBytes);

		for (int32 BrickIndex = 0; BrickIndex < MipState.NumBricks; BrickIndex++)
		{
			const int32 GlobalBrickIndex = BrickIndex % DistanceFieldBlockAllocatorSizeInBricks + GlobalBlockOffsets[BrickIndex / DistanceFieldBlockAllocatorSizeInBricks] * DistanceFieldBlockAllocatorSizeInBricks;
			const FIntVector BrickTextureCoordinate = GetBrickCoordinate(GlobalBrickIndex, BrickTextureDimensionsInBricks);
			UpdateParameters.BrickUploadCoordinatesPtr[BrickUploadIndex + BrickIndex] = FIntVector4(BrickTextureCoordinate.X, BrickTextureCoordinate.Y, BrickTextureCoordinate.Z, 0);
		}

#if WITH_EDITOR
		if (ReadRequest.BulkData)
		{
			ReadRequest.BulkData->Unlock();
		}
#endif

		BrickUploadIndex += MipState.NumBricks;
	}

#if !WITH_EDITOR

	for (FDistanceFieldReadRequest ReadRequest : UpdateParameters.ReadRequestsToCleanUp)
	{
		if (ReadRequest.AsyncRequest)
		{
			check(ReadRequest.AsyncRequest->PollCompletion());	
			delete ReadRequest.AsyncRequest;
			delete ReadRequest.AsyncHandle;
		}
		else
		{
			check(ReadRequest.Request.Status().IsCompleted());
		}

		delete ReadRequest.ReadOutputDataPtr;
	}

	FIoBatch Batch;

	for (FDistanceFieldReadRequest& ReadRequest : UpdateParameters.NewReadRequests)
	{
		check(ReadRequest.BulkSize > 0);
		ReadRequest.ReadOutputDataPtr = (uint8*)FMemory::Malloc(ReadRequest.BulkSize);
		const bool bIODispatcher = ReadRequest.BulkData->IsUsingIODispatcher();

		if (bIODispatcher)
		{
			// Use IODispatcher when available
			FIoChunkId ChunkID = ReadRequest.BulkData->CreateChunkId();
			FIoReadOptions ReadOptions;
			ReadOptions.SetRange(ReadRequest.BulkData->GetBulkDataOffsetInFile() + ReadRequest.BulkOffset, ReadRequest.BulkSize);
			ReadOptions.SetTargetVa(ReadRequest.ReadOutputDataPtr);
			ReadRequest.Request = Batch.Read(ChunkID, ReadOptions, IoDispatcherPriority_Low);
		}
		else
		{
			// Compatibility path without IODispatcher
			ReadRequest.AsyncHandle = ReadRequest.BulkData->OpenAsyncReadHandle();
			ReadRequest.AsyncRequest = ReadRequest.AsyncHandle->ReadRequest(ReadRequest.BulkData->GetBulkDataOffsetInFile() + ReadRequest.BulkOffset, ReadRequest.BulkSize, AIOP_Low, nullptr, ReadRequest.ReadOutputDataPtr);
		}
	}

	Batch.Issue();

#endif

	ReadRequests.Append(UpdateParameters.NewReadRequests);
}

bool AssetHasOutstandingRequest(FSetElementId AssetSetId, const TArray<FDistanceFieldReadRequest>& ReadRequests)
{
	for (const FDistanceFieldReadRequest& ReadRequest : ReadRequests)
	{
		if (ReadRequest.AssetSetId == AssetSetId)
		{
			return true;
		}
	}

	return false;
}

void FDistanceFieldSceneData::ProcessStreamingRequestsFromGPU(
	TArray<FDistanceFieldReadRequest>& NewReadRequests,
	TArray<FDistanceFieldAssetMipId>& AssetDataUploads)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_DistanceFieldProcessStreamingRequests);
	TRACE_CPUPROFILER_EVENT_SCOPE(DistanceFieldProcessStreamingRequests);

	FRHIGPUBufferReadback* LatestReadbackBuffer = nullptr;

	{
		// Find latest buffer that is ready
		uint32 Index = (ReadbackBuffersWriteIndex + MaxStreamingReadbackBuffers - ReadbackBuffersNumPending) % MaxStreamingReadbackBuffers;
		while (ReadbackBuffersNumPending > 0)
		{
			if (StreamingRequestReadbackBuffers[Index]->IsReady())	
			{
				ReadbackBuffersNumPending--;
				LatestReadbackBuffer = StreamingRequestReadbackBuffers[Index];
			}
			else
			{
				break;
			}
		}
	}

	const int32 BrickAtlasSizeXYInBricks = CVarBrickAtlasSizeXYInBricks.GetValueOnRenderThread();
	const int32 NumBricksBeforeDroppingMips = FMath::Max((CVarMaxAtlasDepthInBricks.GetValueOnRenderThread() - 1) * BrickAtlasSizeXYInBricks * BrickAtlasSizeXYInBricks, 0);
	int32 NumAllocatedDistanceFieldBricks = DistanceFieldAtlasBlockAllocator.GetAllocatedSize() * DistanceFieldBlockAllocatorSizeInBricks;

	for (const FDistanceFieldReadRequest& ReadRequest : ReadRequests)
	{
		// Account for size that will be added when all async read requests complete
		NumAllocatedDistanceFieldBricks += ReadRequest.NumDistanceFieldBricks;
	}

	if (LatestReadbackBuffer)
	{
		const uint32* LatestReadbackBufferPtr = (const uint32*)LatestReadbackBuffer->Lock((MaxStreamingRequests * 2 + 1) * sizeof(uint32));

		const uint32 NumStreamingRequests = FMath::Min<uint32>(LatestReadbackBufferPtr[0], MaxStreamingRequests);

		// Process streaming requests in two passes so that mip1 requests will be allocated before mip2
		for (int32 PassIndex = 0; PassIndex < 2; PassIndex++)
		{
			const bool bFirstPass = PassIndex == 0;

			for (uint32 StreamingRequestIndex = 0; StreamingRequestIndex < NumStreamingRequests; StreamingRequestIndex++)
			{
				const int32 AssetIndex = LatestReadbackBufferPtr[1 + StreamingRequestIndex * 2 + 0];
				const FSetElementId AssetSetId = FSetElementId::FromInteger(AssetIndex);

				if (AssetStateArray.IsValidId(AssetSetId))
				{
					FDistanceFieldAssetState& AssetState = AssetStateArray[AssetSetId];

					const int32 WantedNumMips = LatestReadbackBufferPtr[1 + StreamingRequestIndex * 2 + 1];
					check(WantedNumMips <= DistanceField::NumMips && WantedNumMips <= AssetState.BuiltData->Mips.Num());
					AssetState.WantedNumMips = WantedNumMips;

					if (WantedNumMips < AssetState.ReversedMips.Num() && bFirstPass)
					{
						check(AssetState.ReversedMips.Num() > 1);
						const FDistanceFieldAssetMipState MipState = AssetState.ReversedMips.Pop();
						IndirectionTableAllocator.Free(MipState.IndirectionTableOffset, MipState.IndirectionDimensions.X * MipState.IndirectionDimensions.Y * MipState.IndirectionDimensions.Z);
						
						if (MipState.NumBricks > 0)
						{
							check(MipState.AllocatedBlocks.Num() > 0);
							DistanceFieldAtlasBlockAllocator.Free(MipState.AllocatedBlocks);
						}

						// Re-upload mip0 to push the new NumMips to the shader
						AssetDataUploads.Add(FDistanceFieldAssetMipId(AssetSetId, 0));
					}
					else if (WantedNumMips > AssetState.ReversedMips.Num())
					{
						const int32 ReversedMipIndexToAdd = AssetState.ReversedMips.Num();
						// Don't allocate mip if we are close to the max size
						const bool bAllowedToAllocateMipBricks = NumAllocatedDistanceFieldBricks <= NumBricksBeforeDroppingMips;
						// Only allocate mip2 requests in the second pass after all mip1 requests have succeeded
						const bool bShouldProcessThisPass = ((bFirstPass && ReversedMipIndexToAdd < DistanceField::NumMips - 1) || (!bFirstPass && ReversedMipIndexToAdd == DistanceField::NumMips - 1));

						if (bAllowedToAllocateMipBricks 
							&& bShouldProcessThisPass 
							// Only allow one IO request in flight for a given asset
							&& !AssetHasOutstandingRequest(AssetSetId, ReadRequests))
						{
							const int32 MipIndexToAdd = AssetState.BuiltData->Mips.Num() - ReversedMipIndexToAdd - 1;
							const FSparseDistanceFieldMip& MipBuiltData = AssetState.BuiltData->Mips[MipIndexToAdd];

							//@todo - this condition shouldn't be possible as the built data always has non-zero size, needs more investigation
							if (MipBuiltData.BulkSize > 0)
							{
								FDistanceFieldReadRequest ReadRequest;
								ReadRequest.AssetSetId = AssetSetId;
								ReadRequest.BuiltDataId = AssetState.BuiltData->GetId();
								ReadRequest.ReversedMipIndex = ReversedMipIndexToAdd;
								ReadRequest.NumDistanceFieldBricks = MipBuiltData.NumDistanceFieldBricks;
								ReadRequest.BulkData = &AssetState.BuiltData->StreamableMips;
								ReadRequest.BulkOffset = MipBuiltData.BulkOffset;
								ReadRequest.BulkSize = MipBuiltData.BulkSize;
								check(ReadRequest.BulkSize > 0);
								NewReadRequests.Add(MoveTemp(ReadRequest));

								NumAllocatedDistanceFieldBricks += MipBuiltData.NumDistanceFieldBricks;
							}
						}
					}
				}
			}
		}
		
		LatestReadbackBuffer->Unlock();
	}
}

void FDistanceFieldSceneData::ProcessReadRequests(
	TArray<FDistanceFieldAssetMipId>& AssetDataUploads,
	TArray<FDistanceFieldAssetMipId>& DistanceFieldAssetMipAdds,
	TArray<FDistanceFieldReadRequest>& ReadRequestsToUpload,
	TArray<FDistanceFieldReadRequest>& ReadRequestsToCleanUp)
{
	const uint32 BrickSizeBytes = GPixelFormats[DistanceField::DistanceFieldFormat].BlockBytes * DistanceField::BrickSize * DistanceField::BrickSize * DistanceField::BrickSize;
	const SIZE_T TextureUploadLimitBytes = (SIZE_T)CVarTextureUploadLimitKBytes.GetValueOnRenderThread() * 1024;

	SIZE_T TextureUploadBytes = 0;

	// At this point DistanceFieldAssetMipAdds contains only lowest resolution mip adds which are always loaded
	// Forward these to the Requests to Upload list, with a null BulkData
	for (FDistanceFieldAssetMipId AssetMipAdd : DistanceFieldAssetMipAdds)
	{
		const FDistanceFieldAssetState& AssetState = AssetStateArray[AssetMipAdd.AssetId];
		const int32 ReversedMipIndex = AssetMipAdd.ReversedMipIndex;
		check(ReversedMipIndex == 0);
		const int32 MipIndex = AssetState.BuiltData->Mips.Num() - ReversedMipIndex - 1;
		const FSparseDistanceFieldMip& MipBuiltData = AssetState.BuiltData->Mips[MipIndex];
		TextureUploadBytes += MipBuiltData.NumDistanceFieldBricks * BrickSizeBytes;

		FDistanceFieldReadRequest NewReadRequest;
		NewReadRequest.AssetSetId = AssetMipAdd.AssetId;
		NewReadRequest.BuiltDataId = AssetState.BuiltData->GetId();
		NewReadRequest.ReversedMipIndex = AssetMipAdd.ReversedMipIndex;
		NewReadRequest.NumDistanceFieldBricks = MipBuiltData.NumDistanceFieldBricks;
		NewReadRequest.AlwaysLoadedDataPtr = AssetState.BuiltData->AlwaysLoadedMip.GetData();
		NewReadRequest.BulkSize = AssetState.BuiltData->AlwaysLoadedMip.Num();
		ReadRequestsToUpload.Add(MoveTemp(NewReadRequest));
	}

	for (int32 RequestIndex = 0; RequestIndex < ReadRequests.Num(); RequestIndex++)
	{
		const FDistanceFieldReadRequest ReadRequest = ReadRequests[RequestIndex];

		bool bReady = true;

#if !WITH_EDITOR
		if (ReadRequest.AsyncRequest)
		{
			bReady = bReady && ReadRequest.AsyncRequest->PollCompletion();
		}
		else
		{
			bReady = bReady && ReadRequest.Request.Status().IsCompleted();
		}
#endif

		if (bReady)
		{
			ReadRequests.RemoveAt(RequestIndex);
			RequestIndex--;

			if (AssetStateArray.IsValidId(ReadRequest.AssetSetId) 
				// Prevent attempting to upload after a different asset has been allocated at the same index
				&& ReadRequest.BuiltDataId == AssetStateArray[ReadRequest.AssetSetId].BuiltData->GetId()
				// Shader requires sequential reversed mips starting from 0, skip upload if the IO request got out of sync with the streaming feedback requests
				&& ReadRequest.ReversedMipIndex == AssetStateArray[ReadRequest.AssetSetId].ReversedMips.Num())
			{
				TextureUploadBytes += ReadRequest.NumDistanceFieldBricks * BrickSizeBytes;

				DistanceFieldAssetMipAdds.Add(FDistanceFieldAssetMipId(ReadRequest.AssetSetId, ReadRequest.ReversedMipIndex));
				// Re-upload mip0 to push the new NumMips to the shader
				AssetDataUploads.Add(FDistanceFieldAssetMipId(ReadRequest.AssetSetId, 0));
				ReadRequestsToUpload.Add(ReadRequest);
			}

			ReadRequestsToCleanUp.Add(ReadRequest);
		}

		// Stop uploading when we reach the limit
		// In practice we can still exceed the limit with a single large upload request
		if (TextureUploadBytes >= TextureUploadLimitBytes)
		{
			break;
		}
	}

	// Re-upload asset data for all mips we are uploading this frame
	AssetDataUploads.Append(DistanceFieldAssetMipAdds);
}

void FDistanceFieldSceneData::ResizeBrickAtlasIfNeeded(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap)
{
	const int32 BrickAtlasSizeXYInBricks = CVarBrickAtlasSizeXYInBricks.GetValueOnRenderThread();
	int32 DesiredZSizeInBricks = FMath::DivideAndRoundUp(DistanceFieldAtlasBlockAllocator.GetMaxSize() * DistanceFieldBlockAllocatorSizeInBricks, BrickAtlasSizeXYInBricks * BrickAtlasSizeXYInBricks);

	if (DesiredZSizeInBricks <= CVarMaxAtlasDepthInBricks.GetValueOnRenderThread())
	{
		DesiredZSizeInBricks = FMath::RoundUpToPowerOfTwo(DesiredZSizeInBricks);
	}
	else
	{
		DesiredZSizeInBricks = FMath::DivideAndRoundUp(DesiredZSizeInBricks, 4) * 4;
	}

	const FIntVector DesiredBrickTextureDimensionsInBricks = FIntVector(BrickAtlasSizeXYInBricks, BrickAtlasSizeXYInBricks, DesiredZSizeInBricks);
	const bool bResizeAtlasEveryFrame = CVarResizeAtlasEveryFrame.GetValueOnRenderThread() != 0;

	if (!DistanceFieldBrickVolumeTexture 
		|| DistanceFieldBrickVolumeTexture->GetDesc().GetSize() != DesiredBrickTextureDimensionsInBricks * DistanceField::BrickSize
		|| bResizeAtlasEveryFrame)
	{
		const FRDGTextureDesc BrickVolumeTextureDesc = FRDGTextureDesc::Create3D(
			DesiredBrickTextureDimensionsInBricks * DistanceField::BrickSize,
			DistanceField::DistanceFieldFormat,
			FClearValueBinding::Black, 
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling);

		FRDGTextureRef DistanceFieldBrickVolumeTextureRDG = GraphBuilder.CreateTexture(BrickVolumeTextureDesc, TEXT("DistanceFields.DistanceFieldBrickTexture"));

		if (DistanceFieldBrickVolumeTexture)
		{
			FCopyDistanceFieldAtlasCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCopyDistanceFieldAtlasCS::FParameters>();

			PassParameters->RWDistanceFieldBrickAtlas = GraphBuilder.CreateUAV(DistanceFieldBrickVolumeTextureRDG);
			PassParameters->DistanceFieldAtlas = DistanceField::SetupAtlasParameters(*this);

			auto ComputeShader = GlobalShaderMap->GetShader<FCopyDistanceFieldAtlasCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CopyDistanceFieldAtlas"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(DistanceFieldBrickVolumeTexture->GetDesc().GetSize(), FCopyDistanceFieldAtlasCS::GetGroupSize()));
		}

		BrickTextureDimensionsInBricks = DesiredBrickTextureDimensionsInBricks;
		DistanceFieldBrickVolumeTexture = GraphBuilder.ConvertToExternalTexture(DistanceFieldBrickVolumeTextureRDG);
	}
}

void FDistanceFieldSceneData::GenerateStreamingRequests(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View,
	FScene* Scene,
	bool bLumenEnabled,
	FGlobalShaderMap* GlobalShaderMap)
{
	// It is not safe to EnqueueCopy on a buffer that already has a pending copy.
	if (ReadbackBuffersNumPending < MaxStreamingReadbackBuffers && NumObjectsInBuffer > 0)
	{
		if (!StreamingRequestReadbackBuffers[ReadbackBuffersWriteIndex])
		{
			FRHIGPUBufferReadback* GPUBufferReadback = new FRHIGPUBufferReadback(TEXT("DistanceFields.StreamingRequestReadBack"));
			StreamingRequestReadbackBuffers[ReadbackBuffersWriteIndex] = GPUBufferReadback;
		}

		const uint32 NumAssets = AssetStateArray.GetMaxIndex();
		FRDGBufferDesc WantedNumMipsDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), FMath::RoundUpToPowerOfTwo(NumAssets));
		FRDGBufferRef WantedNumMips = GraphBuilder.CreateBuffer(WantedNumMipsDesc, TEXT("DistanceFields.DistanceFieldAssetWantedNumMips"));

		// Every asset wants at least 1 mipmap
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FRDGBufferUAVDesc(WantedNumMips)), 1);

		FRDGBufferDesc StreamingRequestsDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxStreamingRequests * 2 + 1);
		StreamingRequestsDesc.Usage = EBufferUsageFlags(StreamingRequestsDesc.Usage | BUF_SourceCopy);
		FRDGBufferRef StreamingRequestsBuffer = GraphBuilder.CreateBuffer(StreamingRequestsDesc, TEXT("DistanceFields.DistanceFieldStreamingRequests"));

		{
			FComputeDistanceFieldAssetWantedMipsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeDistanceFieldAssetWantedMipsCS::FParameters>();

			checkf(DistanceField::NumMips == 3, TEXT("Shader needs to be updated"));
			PassParameters->RWDistanceFieldAssetWantedNumMips = GraphBuilder.CreateUAV(WantedNumMips);
			PassParameters->RWDistanceFieldAssetStreamingRequests = GraphBuilder.CreateUAV(StreamingRequestsBuffer);
			PassParameters->DistanceFieldObjectBuffers = DistanceField::SetupObjectBufferParameters(*this);
			PassParameters->DebugForceNumMips = CVarDebugForceNumMips.GetValueOnRenderThread();
			extern int32 GAOGlobalDistanceFieldNumClipmaps;
			// Request Mesh SDF mips based off of the Global SDF clipmaps
			PassParameters->Mip1WorldCenter = View.ViewMatrices.GetViewOrigin();
			PassParameters->Mip1WorldExtent = FVector(GlobalDistanceField::GetClipmapExtent(GAOGlobalDistanceFieldNumClipmaps - 1, Scene, bLumenEnabled));
			PassParameters->Mip2WorldCenter = View.ViewMatrices.GetViewOrigin();
			PassParameters->Mip2WorldExtent = FVector(GlobalDistanceField::GetClipmapExtent(FMath::Max<int32>(GAOGlobalDistanceFieldNumClipmaps / 2 - 1, 0), Scene, bLumenEnabled));

			auto ComputeShader = GlobalShaderMap->GetShader<FComputeDistanceFieldAssetWantedMipsCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ComputeWantedMips"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(NumObjectsInBuffer, FComputeDistanceFieldAssetWantedMipsCS::GetGroupSize()));
		}

		{
			FGenerateDistanceFieldAssetStreamingRequestsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateDistanceFieldAssetStreamingRequestsCS::FParameters>();
			PassParameters->RWDistanceFieldAssetStreamingRequests = GraphBuilder.CreateUAV(StreamingRequestsBuffer);
			PassParameters->DistanceFieldAssetWantedNumMips = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(WantedNumMips));
			PassParameters->DistanceFieldObjectBuffers = DistanceField::SetupObjectBufferParameters(*this);
			PassParameters->DistanceFieldAtlasParameters = DistanceField::SetupAtlasParameters(*this);
			PassParameters->NumDistanceFieldAssets = NumAssets;
			PassParameters->MaxNumStreamingRequests = MaxStreamingRequests;

			auto ComputeShader = GlobalShaderMap->GetShader<FGenerateDistanceFieldAssetStreamingRequestsCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("GenerateStreamingRequests"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(NumAssets, FGenerateDistanceFieldAssetStreamingRequestsCS::GetGroupSize()));
		}

		FRHIGPUBufferReadback* ReadbackBuffer = StreamingRequestReadbackBuffers[ReadbackBuffersWriteIndex];

		AddReadbackBufferPass(GraphBuilder, RDG_EVENT_NAME("DistanceFieldAssetReadback"), StreamingRequestsBuffer,
			[ReadbackBuffer, StreamingRequestsBuffer](FRHICommandList& RHICmdList)
		{
			ReadbackBuffer->EnqueueCopy(RHICmdList, StreamingRequestsBuffer->GetRHI(), 0u);
		});

		ReadbackBuffersWriteIndex = (ReadbackBuffersWriteIndex + 1u) % MaxStreamingReadbackBuffers;
		ReadbackBuffersNumPending = FMath::Min(ReadbackBuffersNumPending + 1u, MaxStreamingReadbackBuffers);
	}
}

void FDistanceFieldSceneData::UpdateDistanceFieldAtlas(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View,
	FScene* Scene,
	bool bLumenEnabled,
	FGlobalShaderMap* GlobalShaderMap,
	TArray<FDistanceFieldAssetMipId>& DistanceFieldAssetMipAdds,
	TArray<FSetElementId>& DistanceFieldAssetRemoves)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateDistanceFieldAtlas);
	TRACE_CPUPROFILER_EVENT_SCOPE(FDistanceFieldSceneData::UpdateDistanceFieldAtlas);
	RDG_EVENT_SCOPE(GraphBuilder, "UpdateDistanceFieldAtlas");

	TArray<FDistanceFieldAssetMipId> AssetDataUploads;

	for (FSetElementId AssetSetId : DistanceFieldAssetRemoves)
	{
		const FDistanceFieldAssetState& AssetState = AssetStateArray[AssetSetId];
		check(AssetState.RefCount == 0);

		for (const FDistanceFieldAssetMipState& MipState : AssetState.ReversedMips)
		{
			IndirectionTableAllocator.Free(MipState.IndirectionTableOffset, MipState.IndirectionDimensions.X * MipState.IndirectionDimensions.Y * MipState.IndirectionDimensions.Z);

			if (MipState.NumBricks > 0)
			{
				check(MipState.AllocatedBlocks.Num() > 0);
				DistanceFieldAtlasBlockAllocator.Free(MipState.AllocatedBlocks);
			}
		}
		
		// Clear GPU data for removed asset
		AssetDataUploads.Add(FDistanceFieldAssetMipId(AssetSetId, 0));

		AssetStateArray.Remove(AssetSetId);
	}

	TArray<FDistanceFieldReadRequest> NewReadRequests;
	// Lock the most recent streaming request buffer from the GPU, create new read requests for mips we want to load in the Async Task
	ProcessStreamingRequestsFromGPU(NewReadRequests, AssetDataUploads);

	TArray<FDistanceFieldReadRequest> ReadRequestsToUpload;
	TArray<FDistanceFieldReadRequest> ReadRequestsToCleanUp;
	// Build a list of completed read requests that should be uploaded to the GPU this frame
	ProcessReadRequests(AssetDataUploads, DistanceFieldAssetMipAdds, ReadRequestsToUpload, ReadRequestsToCleanUp);

	int32 NumIndirectionTableAdds = 0;
	int32 NumBrickUploads = 0;

	// Allocate the mips we are adding this frame from the IndirectionTable and BrickAtlas
	for (int32 MipAddIndex = 0; MipAddIndex < DistanceFieldAssetMipAdds.Num(); MipAddIndex++)
	{
		const int32 Index = GDFReverseAtlasAllocationOrder ? DistanceFieldAssetMipAdds.Num() - MipAddIndex - 1 : MipAddIndex;
		FSetElementId AssetSetId = DistanceFieldAssetMipAdds[Index].AssetId;
		FDistanceFieldAssetState& AssetState = AssetStateArray[AssetSetId];

		const int32 ReversedMipIndex = DistanceFieldAssetMipAdds[Index].ReversedMipIndex;

		// Shader requires sequential reversed mips starting from 0
		check(ReversedMipIndex == AssetState.ReversedMips.Num());
			
		const int32 MipIndex = AssetState.BuiltData->Mips.Num() - ReversedMipIndex - 1;
		const FSparseDistanceFieldMip& MipBuiltData = AssetState.BuiltData->Mips[MipIndex];
		FDistanceFieldAssetMipState NewMipState;
		NewMipState.NumBricks = MipBuiltData.NumDistanceFieldBricks;
		DistanceFieldAtlasBlockAllocator.Allocate(FMath::DivideAndRoundUp(MipBuiltData.NumDistanceFieldBricks, DistanceFieldBlockAllocatorSizeInBricks), NewMipState.AllocatedBlocks);
		NewMipState.IndirectionDimensions = MipBuiltData.IndirectionDimensions;
		const int32 NumIndirectionEntries = NewMipState.IndirectionDimensions.X * NewMipState.IndirectionDimensions.Y * NewMipState.IndirectionDimensions.Z;
		NewMipState.IndirectionTableOffset = IndirectionTableAllocator.Allocate(NumIndirectionEntries);
		AssetState.ReversedMips.Add(MoveTemp(NewMipState));

		NumIndirectionTableAdds += NumIndirectionEntries;
		NumBrickUploads += MipBuiltData.NumDistanceFieldBricks;
	}

	// Now that DistanceFieldAtlasBlockAllocator has been modified, potentially resize the atlas
	ResizeBrickAtlasIfNeeded(GraphBuilder, GlobalShaderMap);

	const uint32 NumAssets = AssetStateArray.GetMaxIndex();
	const int32 AssetDataStrideFloat4s = DistanceField::NumMips * AssetDataMipStrideFloat4s;

	const uint32 AssetDataSizeBytes = FMath::RoundUpToPowerOfTwo(NumAssets) * AssetDataStrideFloat4s * sizeof(FVector4);
	ResizeResourceIfNeeded(GraphBuilder.RHICmdList, AssetDataBuffer, AssetDataSizeBytes, TEXT("DistanceFields.DFAssetData"));
	const uint32 IndirectionTableSizeBytes = FMath::Max<uint32>(FMath::RoundUpToPowerOfTwo(IndirectionTableAllocator.GetMaxSize()) * sizeof(uint32), 16);
	ResizeResourceIfNeeded(GraphBuilder.RHICmdList, IndirectionTable, IndirectionTableSizeBytes, TEXT("DistanceFields.DFIndirectionTable"));

	{
		FDistanceFieldAsyncUpdateParameters UpdateParameters;
		UpdateParameters.DistanceFieldSceneData = this;

		check(ReadRequestsToUpload.Num() == 0 && NumIndirectionTableAdds == 0 || ReadRequestsToUpload.Num() > 0 && NumIndirectionTableAdds > 0);

		if (NumIndirectionTableAdds > 0)
		{
			// Allocate staging buffer space for the indirection table compute scatter
			IndirectionTableUploadBuffer.Init(NumIndirectionTableAdds, sizeof(uint32), false, TEXT("DistanceFields.DFIndirectionTableUploadBuffer"));
		}

		FDistanceFieldAtlasUpload AtlasUpload(BrickUploadCoordinatesBuffer, BrickUploadDataBuffer);

		if (NumBrickUploads > 0)
		{
			// Allocate staging buffer space for the brick atlas compute scatter
			AtlasUpload.AllocateAndLock(NumBrickUploads, DistanceField::BrickSize);
			UpdateParameters.BrickUploadDataPtr = AtlasUpload.BrickUploadDataPtr;
			UpdateParameters.BrickUploadCoordinatesPtr = AtlasUpload.BrickUploadCoordinatesPtr;
		}

		UpdateParameters.NewReadRequests = MoveTemp(NewReadRequests);
		UpdateParameters.ReadRequestsToUpload = MoveTemp(ReadRequestsToUpload);
		UpdateParameters.ReadRequestsToCleanUp = MoveTemp(ReadRequestsToCleanUp);

		check(AsyncTaskEvents.IsEmpty());
		// Kick off an async task to copy completed read requests into upload staging buffers, and issue new read requests
		AsyncTaskEvents.Add(TGraphTask<FDistanceFieldStreamingUpdateTask>::CreateTask().ConstructAndDispatchWhenReady(UpdateParameters));

		AddPass(GraphBuilder, [this, AtlasUpload, NumBrickUploads, NumIndirectionTableAdds](FRHICommandListImmediate& RHICmdList)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_WaitOnDistanceFieldStreamingUpdate);
			TRACE_CPUPROFILER_EVENT_SCOPE(WaitOnDistanceFieldStreamingUpdate);

			check(!AsyncTaskEvents.IsEmpty());
			// Block on the async task before RDG execution of compute scatter uploads
			FTaskGraphInterface::Get().WaitUntilTasksComplete(AsyncTaskEvents, ENamedThreads::GetRenderThread_Local());
			AsyncTaskEvents.Empty();

			if (NumBrickUploads > 0)
			{
				AtlasUpload.Unlock();
			}

			if (NumIndirectionTableAdds > 0)
			{
				RHICmdList.Transition({
					FRHITransitionInfo(IndirectionTable.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
					});

				IndirectionTableUploadBuffer.ResourceUploadTo(RHICmdList, IndirectionTable, false);

				RHICmdList.Transition({
					FRHITransitionInfo(IndirectionTable.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask)
					});
			}
		});

		FRDGTextureRef DistanceFieldBrickVolumeTextureRDG = GraphBuilder.RegisterExternalTexture(DistanceFieldBrickVolumeTexture, TEXT("DistanceFields.DistanceFieldBrickVolumeTexture"));

		if (NumBrickUploads > 0)
		{
			// GRHIMaxDispatchThreadGroupsPerDimension can be MAX_int32 so we need to do this math in 64-bit.
			const int32 MaxBrickUploadsPerPass = (int32)FMath::Min<int64>((int64)GRHIMaxDispatchThreadGroupsPerDimension.Z * FScatterUploadDistanceFieldAtlasCS::GetGroupSize() / DistanceField::BrickSize, MAX_int32);

			for (int32 StartBrickIndex = 0; StartBrickIndex < NumBrickUploads; StartBrickIndex += MaxBrickUploadsPerPass)
			{
				const int32 NumBrickUploadsThisPass = FMath::Min(MaxBrickUploadsPerPass, NumBrickUploads - StartBrickIndex);
				FScatterUploadDistanceFieldAtlasCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScatterUploadDistanceFieldAtlasCS::FParameters>();

				PassParameters->RWDistanceFieldBrickAtlas = GraphBuilder.CreateUAV(DistanceFieldBrickVolumeTextureRDG);
				PassParameters->BrickUploadCoordinates = AtlasUpload.BrickUploadCoordinatesBuffer.SRV;
				PassParameters->BrickUploadData = AtlasUpload.BrickUploadDataBuffer.SRV;
				PassParameters->StartBrickIndex = StartBrickIndex;
				PassParameters->NumBrickUploads = NumBrickUploadsThisPass;
				PassParameters->BrickSize = DistanceField::BrickSize;

				auto ComputeShader = GlobalShaderMap->GetShader<FScatterUploadDistanceFieldAtlasCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ScatterUploadDistanceFieldAtlas"),
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(FIntVector(DistanceField::BrickSize, DistanceField::BrickSize, NumBrickUploadsThisPass * DistanceField::BrickSize), FScatterUploadDistanceFieldAtlasCS::GetGroupSize()));
			}

			DistanceFieldBrickVolumeTexture = GraphBuilder.ConvertToExternalTexture(DistanceFieldBrickVolumeTextureRDG);
		}

		GraphBuilder.FinalizeTextureAccess(DistanceFieldBrickVolumeTextureRDG, ERHIAccess::SRVMask);
	}

	if (AssetDataUploads.Num() > 0)
	{
		AssetDataUploadBuffer.Init(AssetDataUploads.Num(), AssetDataMipStrideFloat4s * sizeof(FVector4), true, TEXT("DistanceFields.DFAssetDataUploadBuffer"));

		for (FDistanceFieldAssetMipId AssetMipUpload : AssetDataUploads)
		{
			const int32 ReversedMipIndex = AssetMipUpload.ReversedMipIndex;
			FVector4* UploadAssetData = (FVector4*)AssetDataUploadBuffer.Add_GetRef(AssetMipUpload.AssetId.AsInteger() * DistanceField::NumMips + ReversedMipIndex);

			if (AssetStateArray.IsValidId(AssetMipUpload.AssetId))
			{
				const FDistanceFieldAssetState& AssetState = AssetStateArray[AssetMipUpload.AssetId];
				const FDistanceFieldAssetMipState& MipState = AssetState.ReversedMips[ReversedMipIndex];
				const int32 MipIndex = AssetState.BuiltData->Mips.Num() - ReversedMipIndex - 1;
				const FSparseDistanceFieldMip& MipBuiltData = AssetState.BuiltData->Mips[MipIndex];
				const FVector2D DistanceFieldToVolumeScaleBias = MipBuiltData.DistanceFieldToVolumeScaleBias;
				const int32 NumMips = AssetState.ReversedMips.Num();

				check(NumMips <= DistanceField::NumMips);
				check(DistanceField::NumMips < 4);
				check(MipBuiltData.IndirectionDimensions.X < DistanceField::MaxIndirectionDimension
					&& MipBuiltData.IndirectionDimensions.Y < DistanceField::MaxIndirectionDimension
					&& MipBuiltData.IndirectionDimensions.Z < DistanceField::MaxIndirectionDimension);

				uint32 IntVector0[4] =
				{
					(uint32)MipBuiltData.IndirectionDimensions.X | (uint32)(MipBuiltData.IndirectionDimensions.Y << 10) | (uint32)(MipBuiltData.IndirectionDimensions.Z << 20) | (uint32)(NumMips << 30),
					(uint32)FFloat16(DistanceFieldToVolumeScaleBias.X).Encoded | ((uint32)FFloat16(DistanceFieldToVolumeScaleBias.Y).Encoded << 16),
					(uint32)MipState.IndirectionTableOffset,
					0
				};

				// Bypass NaN checks in FVector4 ctors
				FVector4 FloatVector0;
				FloatVector0.X = *(const float*)&IntVector0[0];
				FloatVector0.Y = *(const float*)&IntVector0[1];
				FloatVector0.Z = *(const float*)&IntVector0[2];
				FloatVector0.W = *(const float*)&IntVector0[3];

				UploadAssetData[0] = FloatVector0;
				UploadAssetData[1] = FVector4(MipBuiltData.VolumeToVirtualUVScale, 0.0f);
				UploadAssetData[2] = FVector4(MipBuiltData.VolumeToVirtualUVAdd, 0.0f);
			}
			else
			{
				// Clear invalid entries to zero
				UploadAssetData[0] = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
				UploadAssetData[1] = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
				UploadAssetData[2] = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			}
		}

		AddPass(GraphBuilder, [this](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.Transition({
				FRHITransitionInfo(AssetDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
			});

			AssetDataUploadBuffer.ResourceUploadTo(RHICmdList, AssetDataBuffer, false);

			RHICmdList.Transition({
				FRHITransitionInfo(AssetDataBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask)
			});
		});
	}

	GenerateStreamingRequests(GraphBuilder, View, Scene, bLumenEnabled, GlobalShaderMap);

	if (GDistanceFieldAtlasLogStats)
	{
		const bool bDumpAssetStats = GDistanceFieldAtlasLogStats > 1;
		ListMeshDistanceFields(bDumpAssetStats);
		GDistanceFieldAtlasLogStats = 0;
	}
}

void FDistanceFieldSceneData::ListMeshDistanceFields(bool bDumpAssetStats) const
{
	SIZE_T BlockAllocatorWasteBytes = 0;

	struct FMeshDistanceFieldStats
	{
		int32 LoadedMips;
		int32 WantedMips;
		SIZE_T BrickMemoryBytes;
		SIZE_T IndirectionMemoryBytes;
		FIntVector Resolution;
		FName AssetName;
	};

	struct FMipStats
	{
		SIZE_T BrickMemoryBytes;
		SIZE_T IndirectionMemoryBytes;
	};

	TArray<FMeshDistanceFieldStats> AssetStats;
	TArray<FMipStats> MipStats;
	MipStats.AddZeroed(DistanceField::NumMips);

	const uint32 BrickSizeBytes = GPixelFormats[DistanceField::DistanceFieldFormat].BlockBytes * DistanceField::BrickSize * DistanceField::BrickSize * DistanceField::BrickSize;

	for (TSet<FDistanceFieldAssetState, TFDistanceFieldAssetStateFuncs>::TConstIterator It(AssetStateArray); It; ++It)
	{
		const FDistanceFieldAssetState& AssetState = *It;

		FMeshDistanceFieldStats Stats;
		Stats.Resolution = AssetState.BuiltData->Mips[0].IndirectionDimensions * DistanceField::UniqueDataBrickSize;
		Stats.BrickMemoryBytes = 0;
		Stats.IndirectionMemoryBytes = 0;
		Stats.AssetName = AssetState.BuiltData->AssetName;
		Stats.LoadedMips = AssetState.ReversedMips.Num();
		Stats.WantedMips = AssetState.WantedNumMips;

		for (int32 ReversedMipIndex = 0; ReversedMipIndex < AssetState.ReversedMips.Num(); ReversedMipIndex++)
		{
			const FDistanceFieldAssetMipState& MipState = AssetState.ReversedMips[ReversedMipIndex];
			const SIZE_T MipBrickBytes = MipState.NumBricks * BrickSizeBytes;

			BlockAllocatorWasteBytes += MipState.AllocatedBlocks.Num() * DistanceFieldBlockAllocatorSizeInBricks * BrickSizeBytes - MipBrickBytes;
			MipStats[ReversedMipIndex].BrickMemoryBytes += MipBrickBytes;
			Stats.BrickMemoryBytes += MipBrickBytes;

			const SIZE_T MipIndirectionBytes = MipState.IndirectionDimensions.X * MipState.IndirectionDimensions.Y * MipState.IndirectionDimensions.Z * sizeof(uint32);
			MipStats[ReversedMipIndex].IndirectionMemoryBytes += MipIndirectionBytes;
			Stats.IndirectionMemoryBytes += MipIndirectionBytes;
		}

		AssetStats.Add(Stats);
	}

	struct FMeshDistanceFieldStatsSorter
	{
		bool operator()( const FMeshDistanceFieldStats& A, const FMeshDistanceFieldStats& B ) const
		{
			return A.BrickMemoryBytes > B.BrickMemoryBytes;
		}
	};

	AssetStats.Sort(FMeshDistanceFieldStatsSorter());

	const FIntVector AtlasDimensions = BrickTextureDimensionsInBricks * DistanceField::BrickSize;
	const SIZE_T AtlasSizeBytes = AtlasDimensions.X * AtlasDimensions.Y * AtlasDimensions.Z * GPixelFormats[DistanceField::DistanceFieldFormat].BlockBytes;
	const SIZE_T AtlasUsedBytes = DistanceFieldAtlasBlockAllocator.GetAllocatedSize() * DistanceFieldBlockAllocatorSizeInBricks * BrickSizeBytes;
	const float BlockAllocatorWasteMb = BlockAllocatorWasteBytes / 1024.0f / 1024.0f;
	const SIZE_T IndirectionTableBytes = IndirectionTable.NumBytes;
	const int32 BrickAtlasSizeXYInBricks = CVarBrickAtlasSizeXYInBricks.GetValueOnRenderThread();
	const float MaxAtlasSizeMb = CVarMaxAtlasDepthInBricks.GetValueOnRenderThread() * BrickAtlasSizeXYInBricks * BrickAtlasSizeXYInBricks * BrickSizeBytes / 1024.0f / 1024.0f;

	UE_LOG(LogDistanceField, Log, TEXT("Mesh Distance Field Atlas %ux%ux%u = %.1fMb (%.1fMb target max), with %.1fMb free, %.1fMb block allocator waste, Indirection Table %.1fMb"), 
		AtlasDimensions.X,
		AtlasDimensions.Y,
		AtlasDimensions.Z,
		AtlasSizeBytes / 1024.0f / 1024.0f,
		MaxAtlasSizeMb,
		(AtlasSizeBytes - AtlasUsedBytes) / 1024.0f / 1024.0f,
		BlockAllocatorWasteMb,
		IndirectionTableBytes / 1024.0f / 1024.0f);

	for (int32 ReversedMipIndex = 0; ReversedMipIndex < DistanceField::NumMips; ReversedMipIndex++)
	{
		UE_LOG(LogDistanceField, Log, TEXT("   Bricks at Mip%u: %.1fMb, %.1f%%"), 
			ReversedMipIndex,
			MipStats[ReversedMipIndex].BrickMemoryBytes / 1024.0f / 1024.0f,
			100.0f * MipStats[ReversedMipIndex].BrickMemoryBytes / (float)AtlasUsedBytes);
	}

	if (bDumpAssetStats)
	{
		UE_LOG(LogDistanceField, Log, TEXT(""));
		UE_LOG(LogDistanceField, Log, TEXT("Dumping mesh distance fields for %u mesh assets"), AssetStats.Num());
		UE_LOG(LogDistanceField, Log, TEXT("   Memory Mb, Loaded Mips / Wanted Mips, Mip0 Resolution, Asset Name"));

		for (int32 EntryIndex = 0; EntryIndex < AssetStats.Num(); EntryIndex++)
		{
			const FMeshDistanceFieldStats& MeshStats = AssetStats[EntryIndex];

			UE_LOG(LogDistanceField, Log, TEXT("   %.2fMb, %u%s, %dx%dx%d, %s"), 
				(MeshStats.BrickMemoryBytes + MeshStats.IndirectionMemoryBytes) / 1024.0f / 1024.0f, 
				MeshStats.LoadedMips,
				MeshStats.LoadedMips == MeshStats.WantedMips ? TEXT("") : *FString::Printf(TEXT(" / %u"), MeshStats.WantedMips),
				MeshStats.Resolution.X, 
				MeshStats.Resolution.Y,
				MeshStats.Resolution.Z, 
				*MeshStats.AssetName.ToString());
		}
	}
}
