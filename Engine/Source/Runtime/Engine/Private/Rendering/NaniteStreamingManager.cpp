// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/NaniteStreamingManager.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "RenderingThread.h"
#include "UnifiedBuffer.h"
#include "CommonRenderResources.h"
#include "FileCache/FileCache.h"
#include "DistanceFieldAtlas.h"
#include "ClearQuad.h"
#include "RenderGraphUtils.h"
#include "Logging/LogMacros.h"
#include "Async/ParallelFor.h"

#define MAX_STREAMING_PAGES_BITS		11u
#define MAX_STREAMING_PAGES				(1u << MAX_STREAMING_PAGES_BITS)

#define MIN_ROOT_PAGES_CAPACITY			2048u

#define MAX_PENDING_PAGES				32u
#define MAX_INSTALLS_PER_UPDATE			16u

#define MAX_REQUESTS_HASH_TABLE_SIZE	(MAX_STREAMING_REQUESTS << 1)
#define MAX_REQUESTS_HASH_TABLE_MASK	(MAX_REQUESTS_HASH_TABLE_SIZE - 1)
#define INVALID_HASH_ENTRY				0xFFFFFFFFu

#define INVALID_RUNTIME_RESOURCE_ID		0xFFFFFFFFu
#define INVALID_PAGE_INDEX				0xFFFFFFFFu

#define USE_GPU_TRANSCODE				1

float GNaniteStreamingBandwidthLimit = -1.0f;
static FAutoConsoleVariableRef CVarNaniteStreamingBandwidthLimit(
	TEXT( "r.Nanite.StreamingBandwidthLimit" ),
	GNaniteStreamingBandwidthLimit,
	TEXT( "Streaming bandwidth limit in megabytes per second. Negatives values are interpreted as unlimited. " )
);

DECLARE_CYCLE_STAT( TEXT("StreamingManager_Update"),STAT_NaniteStreamingManagerUpdate,	STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("ProcessReadback"),		STAT_NaniteProcessReadback,			STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("UpdatePriorities"),		STAT_NaniteUpdatePriorities,		STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("DeduplicateRequests"),	STAT_NaniteDeduplicateRequests,		STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("SelectStreamingPages"),	STAT_NaniteSelectStreamingPages,	STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("VerifyLRU"),				STAT_NaniteVerifyLRU,				STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("PrioritySort"),			STAT_NanitePrioritySort,			STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("UpdateLRU"),				STAT_NaniteUpdateLRU,				STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("Upload" ),				STAT_NaniteUpload,					STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("CheckReadyPages" ),		STAT_NaniteCheckReadyPages,			STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("InstallStreamingPages" ),	STAT_NaniteInstallStreamingPages,	STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("InstallNewResources" ),	STAT_NaniteInstallNewResources,		STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("TranscodePage"),			STAT_NaniteTranscodePage,			STATGROUP_Nanite);
DECLARE_CYCLE_STAT( TEXT("TranscodePageTask"),		STAT_NaniteTranscodePageTask,		STATGROUP_Nanite);


DECLARE_DWORD_COUNTER_STAT(		TEXT("PageInstalls"),				STAT_NanitePageInstalls,					STATGROUP_Nanite );
DECLARE_DWORD_COUNTER_STAT(		TEXT("StreamingRequests"),			STAT_NaniteStreamingRequests,				STATGROUP_Nanite );
DECLARE_DWORD_COUNTER_STAT(		TEXT("UniqueStreamingRequests"),	STAT_NaniteUniqueStreamingRequests,			STATGROUP_Nanite );
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("TotalPages"),					STAT_NaniteTotalPages,						STATGROUP_Nanite );
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("RegisteredStreamingPages"),	STAT_NaniteRegisteredStreamingPages,		STATGROUP_Nanite );
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("InstalledPages"),				STAT_NaniteInstalledPages,					STATGROUP_Nanite );
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("PendingPages"),				STAT_NanitePendingPages,					STATGROUP_Nanite );
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("RootPages"),					STAT_NaniteRootPages,						STATGROUP_Nanite );

DECLARE_LOG_CATEGORY_EXTERN(LogNaniteStreaming, Log, All);
DEFINE_LOG_CATEGORY(LogNaniteStreaming);

namespace Nanite
{
class FTranscodePageToGPU_CS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTranscodePageToGPU_CS);
	SHADER_USE_PARAMETER_STRUCT(FTranscodePageToGPU_CS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(StructuredBuffer<FPageInstallInfo>, InstallInfoBuffer)
		SHADER_PARAMETER_SRV(ByteAddressBuffer,					SrcPageBuffer)
		SHADER_PARAMETER_UAV(RWByteAddressBuffer,				DstPageBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTranscodePageToGPU_CS, "/Engine/Private/Nanite/Transcode.usf", "TranscodePageToGPU", SF_Compute);

// Lean hash table for deduplicating requests.
// Linear probing hash table that only supports add and never grows.
// This is intended to be kept alive over the duration of the program, so allocation and clearing only has to happen once.
// TODO: Unify with VT?
class FRequestsHashTable
{
	FStreamingRequest*		HashTable;
	uint32*					ElementIndices;	// List of indices to unique elements of HashTable
	uint32					NumElements;	// Number of unique elements in HashTable
public:
	FRequestsHashTable()
	{
		check(FMath::IsPowerOfTwo(MAX_REQUESTS_HASH_TABLE_SIZE));
		HashTable = new FStreamingRequest[MAX_REQUESTS_HASH_TABLE_SIZE];
		ElementIndices = new uint32[MAX_REQUESTS_HASH_TABLE_SIZE];
		for(uint32 i = 0; i < MAX_REQUESTS_HASH_TABLE_SIZE; i++)
		{
			HashTable[i].Key.RuntimeResourceID = INVALID_RUNTIME_RESOURCE_ID;
		}
		NumElements = 0;
	}
	~FRequestsHashTable()
	{
		delete[] HashTable;
		delete[] ElementIndices;
		HashTable = nullptr;
		ElementIndices = nullptr;
	}

	FORCEINLINE void AddRequest(const FStreamingRequest& Request)
	{
		uint32 TableIndex = GetTypeHash(Request.Key) & MAX_REQUESTS_HASH_TABLE_MASK;

		while(true)
		{
			FStreamingRequest& TableEntry = HashTable[TableIndex];
			if(TableEntry.Key == Request.Key)
			{
				// Found it. Just update the key.
				TableEntry.Priority = FMath::Max( TableEntry.Priority, Request.Priority );
				return;
			}

			if(TableEntry.Key.RuntimeResourceID == INVALID_RUNTIME_RESOURCE_ID)
			{
				// Empty slot. Take it and add this to cell to the elements list.
				TableEntry = Request;
				ElementIndices[NumElements++] = TableIndex;
				return;
			}

			// Slot was taken by someone else. Move on to next slot.
			TableIndex = (TableIndex + 1) & MAX_REQUESTS_HASH_TABLE_MASK;
		}
	}

	uint32 GetNumElements() const
	{
		return NumElements;
	}

	const FStreamingRequest& GetElement(uint32 Index) const
	{
		check( Index < NumElements );
		return HashTable[ElementIndices[Index]];
	}

	// Clear by looping through unique elements. Cost is proportional to number of unique elements, not the whole table.
	void Clear()
	{
		for( uint32 i = 0; i < NumElements; i++ )
		{
			FStreamingRequest& Request = HashTable[ ElementIndices[ i ] ];
			Request.Key.RuntimeResourceID = INVALID_RUNTIME_RESOURCE_ID;
		}
		NumElements = 0;
	}
};


FORCEINLINE bool IsRootPage(uint32 PageIndex)	// Keep in sync with ClusterCulling.usf
{
	return PageIndex == 0;
}

FORCEINLINE static const void* GetPackedTriClusterMemberSOA(const void* Src, uint32 ClusterIndex, uint32 NumClusters, uint32 MemberOffset)
{
	uint32 Offset = (((NumClusters << 4) * (MemberOffset >> 4)) + (MemberOffset & 15)) + (ClusterIndex << 4);	// Split offset from pointer arithmetic to make static analysis happy
	return (const uint8*)Src + Offset;
}

class FBitReader
{
public:
	FORCEINLINE FBitReader(const uint8* Src):
		SrcPtr(Src)
	{
		BufferBits = *(const uint64*)SrcPtr;
		NumBufferBits = 64;
	}

	FORCEINLINE uint32 GetBits(int32 NumBits)
	{
		while (NumBufferBits < NumBits)
		{
			BufferBits |= (uint64)(*(uint32*)SrcPtr++) << NumBufferBits;
			NumBufferBits += 32;
		}

		uint32 Mask = (1u << NumBits) - 1u;
		uint32 Result = BufferBits & Mask;
		NumBufferBits -= NumBits;
		BufferBits >>= NumBits;
		return Result;
	}
private:
	const uint8* SrcPtr;
	uint64 		BufferBits;
	int32 		NumBufferBits;
};

class FBitWriter
{
public:
	FBitWriter(uint8* Dst, uint32 Size):
		DstStartPtr(Dst),
		DstEndPtr(Dst + Size),
		DstPtr(Dst),
		PendingBits(0),
		NumPendingBits(0)
	{
	}

	FORCEINLINE void PutBits(uint32 Bits, uint32 NumBits)
	{
		checkSlow((uint64)Bits < (1ull << NumBits));
		PendingBits |= (uint64)Bits << NumPendingBits;
		NumPendingBits += NumBits;

		if(NumPendingBits >= 32)
		{
			uint8* NextDstPtr = DstPtr + sizeof(uint32);
			checkSlow(NextDstPtr <= DstEndPtr);
			*(uint32*)DstPtr = PendingBits;
			DstPtr = NextDstPtr;
			PendingBits >>= 32;
			NumPendingBits -= 32;
		}
	}

	uint8* Flush(uint32 Alignment = 1)
	{
		while(NumPendingBits > 0)
		{
			checkSlow(DstPtr < DstEndPtr);
			*DstPtr++ = (uint8)PendingBits;
			PendingBits >>= 8;
			NumPendingBits -= 8;
		}
		while ((DstPtr - DstStartPtr) % Alignment != 0)
			*DstPtr++ = 0;
		NumPendingBits = 0;
		PendingBits = 0;
		return DstPtr;
	}

private:
	uint8*	DstStartPtr;
	uint8*	DstEndPtr;
	uint8*	DstPtr;
	uint64 	PendingBits;
	int32 	NumPendingBits;
};

static void TranscodePageToGPU(uint8* Dst, const uint8* Src, uint32 DiskSize)
{
	SCOPE_CYCLE_COUNTER(STAT_NaniteTranscodePage);

	const uint8* SrcPtr = Src;
	const FPageDiskHeader& PageDiskHeader = *(const FPageDiskHeader*)SrcPtr;			SrcPtr += sizeof(FPageDiskHeader);
	const FClusterDiskHeader* ClusterDiskHeaders = (const FClusterDiskHeader*)SrcPtr;	SrcPtr += PageDiskHeader.NumClusters * sizeof(FClusterDiskHeader);

	const uint8* PackedClusterPtr = SrcPtr;

	uint8* DstPtr = Dst;

	const uint32 NumClusters = PageDiskHeader.NumClusters;	
	uint32 MiscSize = NumClusters * sizeof(FPackedTriCluster) + PageDiskHeader.NumMaterialDwords * sizeof(uint32);
	
	// Copy misc
	FMemory::Memcpy(DstPtr, SrcPtr, MiscSize);
	DstPtr += MiscSize;
	SrcPtr += MiscSize;
	FBitWriter BitWriter(DstPtr, PageDiskHeader.GpuSize - MiscSize);

	// Index Data
	const uint8* IndexDataPtr = SrcPtr;
	for (uint32 ClusterIndex = 0; ClusterIndex < NumClusters; ClusterIndex++)
	{
		// NumVerts:9, NumTris:8, BitsPerIndex:4, QuantizedPosShift:6
		const uint32 NumVerts_NumTris_BitsPerIndex_QuantizedPosShift = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, ClusterIndex, NumClusters, offsetof(FPackedTriCluster, NumVerts_NumTris_BitsPerIndex_QuantizedPosShift));
		const uint32 IndexOffset = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, ClusterIndex, NumClusters, offsetof(FPackedTriCluster, IndexOffset));
		const uint32 NumTris = (NumVerts_NumTris_BitsPerIndex_QuantizedPosShift >> 9) & 0xFF;
		const uint32 BitsPerIndex = (NumVerts_NumTris_BitsPerIndex_QuantizedPosShift >> 17) & 15;
		
		uint32 NextVertexIndex = 0;
		for (uint32 i = 0; i < NumTris; i++)
		{
#if 0
			uint32 Index0 = (NextVertexIndex - SrcPtr[0]) & 0xFF;
			NextVertexIndex += (SrcPtr[0] == 0);
			uint32 Index1 = (NextVertexIndex - SrcPtr[1]) & 0xFF;
			NextVertexIndex += (SrcPtr[1] == 0);
			uint32 Index2 = (NextVertexIndex - SrcPtr[2]) & 0xFF;
			NextVertexIndex += (SrcPtr[2] == 0);
			SrcPtr += 3;
			BitWriter.PutBits(Index0 | (Index1 << BitsPerIndex) | (Index2 << (2 * BitsPerIndex)), BitsPerIndex * 3);
#else
			BitWriter.PutBits(SrcPtr[0] | (SrcPtr[1] << BitsPerIndex) | (SrcPtr[2] << (2 * BitsPerIndex)), BitsPerIndex * 3);
			SrcPtr += 3;
#endif
		}
		BitWriter.Flush();
	}

	// Position Data
	for (uint32 DstClusterIndex = 0; DstClusterIndex < NumClusters; DstClusterIndex++)
	{
		const uint32 NumVerts_NumTris_BitsPerIndex_QuantizedPosShift = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, DstClusterIndex, NumClusters, offsetof(FPackedTriCluster, NumVerts_NumTris_BitsPerIndex_QuantizedPosShift));
		const uint32 NumVerts = NumVerts_NumTris_BitsPerIndex_QuantizedPosShift & 0x1FF;
		const uint32 PositionOffset = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, DstClusterIndex, NumClusters, offsetof(FPackedTriCluster, PositionOffset));
		const FUIntVector& Dst_QuantizedPosStart = *(const FUIntVector*)GetPackedTriClusterMemberSOA(PackedClusterPtr, DstClusterIndex, NumClusters, offsetof(FPackedTriCluster, QuantizedPosStart));
		const uint32 Dst_QuantizedPosShift = NumVerts_NumTris_BitsPerIndex_QuantizedPosShift >> (9 + 8 + 4);		// TODO: get rid of all these magic numbers

		for (uint32 VertexIndex = 0; VertexIndex < NumVerts; VertexIndex++)
		{
			const uint32 VertexRefValue = *(const uint32*)(Src + ClusterDiskHeaders[DstClusterIndex].VertexRefDataOffset + VertexIndex * sizeof(uint32));

			const uint32 Src_ClusterIndex = (VertexRefValue >> 8) - 1;
			const uint32 Src_CodedVertexIndex = VertexRefValue & 0xFF;
			const uint32* Src_QuantiazedPosStart_PositionOffset = (const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, Src_ClusterIndex, NumClusters, offsetof(FPackedTriCluster, QuantizedPosStart));
			const FUIntVector& Src_QuantizedPosStart = *(const FUIntVector*)Src_QuantiazedPosStart_PositionOffset;

			const uint32 Src_NumVerts_NumTris_BitsPerIndex_QuantizedPosShift = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, Src_ClusterIndex, NumClusters, offsetof(FPackedTriCluster, NumVerts_NumTris_BitsPerIndex_QuantizedPosShift));
			const uint32 Src_QuantizedPosShift = Src_NumVerts_NumTris_BitsPerIndex_QuantizedPosShift >> (9 + 8 + 4);		// TODO: get rid of all these magic numbers
			const uint32 Src_UnpackedPosData = *(const uint32*)(Src + ClusterDiskHeaders[Src_ClusterIndex].PositionDataOffset + Src_CodedVertexIndex * sizeof(uint32));
			const uint32 SrcX = Src_UnpackedPosData & POSITION_QUANTIZATION_MASK;
			const uint32 SrcY = (Src_UnpackedPosData >> POSITION_QUANTIZATION_BITS) & POSITION_QUANTIZATION_MASK;
			const uint32 SrcZ = (Src_UnpackedPosData >> (2 * POSITION_QUANTIZATION_BITS)) & POSITION_QUANTIZATION_MASK;
				
			uint32 DstX = ((SrcX + Src_QuantizedPosStart.X) << Src_QuantizedPosShift);
			uint32 DstY = ((SrcY + Src_QuantizedPosStart.Y) << Src_QuantizedPosShift);
			uint32 DstZ = ((SrcZ + Src_QuantizedPosStart.Z) << Src_QuantizedPosShift);
			uint32 DstMask = (1u << Dst_QuantizedPosShift) - 1u;
			checkSlow((DstX & DstMask) == 0);
			checkSlow((DstY & DstMask) == 0);
			checkSlow((DstZ & DstMask) == 0);
			DstX = (DstX >> Dst_QuantizedPosShift);
			DstY = (DstY >> Dst_QuantizedPosShift);
			DstZ = (DstZ >> Dst_QuantizedPosShift);
			checkSlow(DstX >= Dst_QuantizedPosStart.X);
			checkSlow(DstY >= Dst_QuantizedPosStart.Y);
			checkSlow(DstZ >= Dst_QuantizedPosStart.Z);
			DstX -= Dst_QuantizedPosStart.X;
			DstY -= Dst_QuantizedPosStart.Y;
			DstZ -= Dst_QuantizedPosStart.Z;

			checkSlow(DstX <= POSITION_QUANTIZATION_MASK);
			checkSlow(DstY <= POSITION_QUANTIZATION_MASK);
			checkSlow(DstZ <= POSITION_QUANTIZATION_MASK);
			uint32 PosData = DstX | (DstY << POSITION_QUANTIZATION_BITS) | (DstZ << (2 * POSITION_QUANTIZATION_BITS));

			BitWriter.PutBits(PosData, 3 * POSITION_QUANTIZATION_BITS);

		}
		BitWriter.Flush();
	}
	
	// Attribute Data
	for (uint32 DstClusterIndex = 0; DstClusterIndex < NumClusters; DstClusterIndex++)
	{
		const uint32 BitsPerAttrib = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, DstClusterIndex, NumClusters, offsetof(FPackedTriCluster, BitsPerAttrib));
		const uint32 NumVerts_NumTris_BitsPerIndex_QuantizedPosShift = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, DstClusterIndex, NumClusters, offsetof(FPackedTriCluster, NumVerts_NumTris_BitsPerIndex_QuantizedPosShift));
		const uint32 UVPrec = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, DstClusterIndex, NumClusters, offsetof(FPackedTriCluster, UV_Prec));
		const uint32 NumVerts = NumVerts_NumTris_BitsPerIndex_QuantizedPosShift & 0x1FF;
		const uint32 AttributeOffset = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, DstClusterIndex, NumClusters, offsetof(FPackedTriCluster, AttributeOffset));

		for (uint32 VertexIndex = 0; VertexIndex < NumVerts; VertexIndex++)
		{
			const uint32 VertexRefValue = *(const uint32*)(Src + ClusterDiskHeaders[DstClusterIndex].VertexRefDataOffset + VertexIndex * sizeof(uint32));

			const uint32 Src_ClusterIndex = (VertexRefValue >> 8) - 1;
			const uint32 Src_CodedVertexIndex = VertexRefValue & 0xFF;

			const uint32 Src_BitsPerAttrib = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, Src_ClusterIndex, NumClusters, offsetof(FPackedTriCluster, BitsPerAttrib));
			const uint32 Src_UVPrec = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, Src_ClusterIndex, NumClusters, offsetof(FPackedTriCluster, UV_Prec));
			const uint32 Src_AttributeOffset = *(const uint32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, Src_ClusterIndex, NumClusters, offsetof(FPackedTriCluster, AttributeOffset));
			const uint32 Src_BytesPerAttrib = (Src_BitsPerAttrib + 31) / 32 * 4;
				
			FBitReader BitReader(Src + ClusterDiskHeaders[Src_ClusterIndex].AttributeDataOffset + Src_CodedVertexIndex * Src_BytesPerAttrib);

			uint32 NormalXY = BitReader.GetBits(2*NORMAL_QUANTIZATION_BITS);
			BitWriter.PutBits(NormalXY, 2*NORMAL_QUANTIZATION_BITS);
			
			int32 RemainingBits = BitsPerAttrib - 2 * NORMAL_QUANTIZATION_BITS;
				
			uint32 Src_Rolling_UVPrec = Src_UVPrec;
			uint32 Dst_Rolling_UVPrec = UVPrec;
			for(uint32 TexCoordIndex = 0; TexCoordIndex < PageDiskHeader.NumTexCoords; TexCoordIndex++)
			{
				uint32 SrcU_Bits = Src_Rolling_UVPrec & 15;
				uint32 SrcV_Bits = (Src_Rolling_UVPrec >> 4) & 15;
				Src_Rolling_UVPrec >>= 8;

				uint32 DstU_Bits = Dst_Rolling_UVPrec & 15;
				uint32 DstV_Bits = (Dst_Rolling_UVPrec >> 4) & 15;
				Dst_Rolling_UVPrec >>= 8;

				int32 SrcU = BitReader.GetBits(SrcU_Bits);
				int32 SrcV = BitReader.GetBits(SrcV_Bits);

				const FVector2D* Dst_Min_Scale = (const FVector2D*)GetPackedTriClusterMemberSOA(PackedClusterPtr, DstClusterIndex, NumClusters, offsetof(FPackedTriCluster, UVRanges[TexCoordIndex].Min));
				const int32* Dst_GapStart_GapLength = (const int32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, DstClusterIndex, NumClusters, offsetof(FPackedTriCluster, UVRanges[TexCoordIndex].GapStart));

				const FVector2D* Src_Min_Scale = (const FVector2D*)GetPackedTriClusterMemberSOA(PackedClusterPtr, Src_ClusterIndex, NumClusters, offsetof(FPackedTriCluster, UVRanges[TexCoordIndex].Min));
				const int32* Src_GapStart_GapLength = (const int32*)GetPackedTriClusterMemberSOA(PackedClusterPtr, Src_ClusterIndex, NumClusters, offsetof(FPackedTriCluster, UVRanges[TexCoordIndex].GapStart));
					
				int32 U = SrcU + (SrcU >= Src_GapStart_GapLength[0] ? Src_GapStart_GapLength[2] : 0u);
				int32 V = SrcV + (SrcV >= Src_GapStart_GapLength[1] ? Src_GapStart_GapLength[3] : 0u);

				float fU = U * Src_Min_Scale[1].X + Src_Min_Scale[0].X;
				float fV = V * Src_Min_Scale[1].Y + Src_Min_Scale[0].Y;
				
				int32 DstU = FMath::RoundToInt((fU - Dst_Min_Scale[0].X) / Dst_Min_Scale[1].X);
				int32 DstV = FMath::RoundToInt((fV - Dst_Min_Scale[0].Y) / Dst_Min_Scale[1].Y);
					
				if (DstU < Dst_GapStart_GapLength[0])
					;
				else if (DstU >= Dst_GapStart_GapLength[0] + Dst_GapStart_GapLength[2])
					DstU -= Dst_GapStart_GapLength[2];
				else
					DstU = Dst_GapStart_GapLength[0] - 1 + (DstU >= Dst_GapStart_GapLength[0] + (Dst_GapStart_GapLength[2]>>1));

				if (DstV < Dst_GapStart_GapLength[1])
					;
				else if (DstV >= Dst_GapStart_GapLength[1] + Dst_GapStart_GapLength[3])
					DstV -= Dst_GapStart_GapLength[3];
				else
					DstV = Dst_GapStart_GapLength[1] - 1 + (DstV >= Dst_GapStart_GapLength[1] + (Dst_GapStart_GapLength[3]>>1));
				
				DstU = FMath::Clamp(DstU, 0, (1 << DstU_Bits) - 1);
				DstV = FMath::Clamp(DstV, 0, (1 << DstV_Bits) - 1);

				BitWriter.PutBits(DstU | (DstV << DstU_Bits), DstU_Bits + DstV_Bits);
				RemainingBits -= DstU_Bits + DstV_Bits;
			}
			checkSlow(RemainingBits == 0);
		}
		DstPtr = BitWriter.Flush();	//TODO: don't align here. Use bit offsets instead?
	}

	//uint32 ActualDiskSize = uint32(SrcPtr - Src);
	//check(ActualDiskSize == DiskSize);
	uint32 GpuSize = uint32(DstPtr - Dst);
	check(GpuSize == PageDiskHeader.GpuSize);
}

struct FPageInstallInfo
{
	uint32 SrcPageOffset;
	uint32 DstPageOffset;
};

class FStreamingPageUploader
{
public:
	FStreamingPageUploader()
	{
		ResetState();
	}

	void Init(uint32 NumPages, uint32 NumPageBytes)
	{
		ResetState();
		MaxPages = NumPages;
		MaxPageBytes = NumPageBytes;


		uint32 InstallInfoAllocationSize	= FMath::RoundUpToPowerOfTwo(NumPages * sizeof(FPageInstallInfo));
		uint32 PageAllocationSize			= FMath::RoundUpToPowerOfTwo(NumPageBytes);

		if (InstallInfoAllocationSize > InstallInfoUploadBuffer.NumBytes)
		{
			InstallInfoUploadBuffer.Release();
			InstallInfoUploadBuffer.NumBytes = InstallInfoAllocationSize;

			FRHIResourceCreateInfo CreateInfo(TEXT("InstallInfoUploadBuffer"));
			InstallInfoUploadBuffer.Buffer = RHICreateStructuredBuffer(sizeof(FPageInstallInfo), InstallInfoUploadBuffer.NumBytes, BUF_ShaderResource | BUF_Volatile, CreateInfo);
			InstallInfoUploadBuffer.SRV = RHICreateShaderResourceView(InstallInfoUploadBuffer.Buffer);
		}

		if (PageAllocationSize > PageUploadBuffer.NumBytes)
		{
			PageUploadBuffer.Release();
			PageUploadBuffer.NumBytes = PageAllocationSize;

			FRHIResourceCreateInfo CreateInfo(TEXT("PageUploadBuffer"));
			PageUploadBuffer.Buffer = RHICreateStructuredBuffer(sizeof(uint32), PageUploadBuffer.NumBytes, BUF_ShaderResource | BUF_Volatile | BUF_ByteAddressBuffer, CreateInfo);
			PageUploadBuffer.SRV = RHICreateShaderResourceView(PageUploadBuffer.Buffer);
		}
		
		InstallInfoPtr = (FPageInstallInfo*)RHILockStructuredBuffer(InstallInfoUploadBuffer.Buffer, 0, InstallInfoAllocationSize, RLM_WriteOnly);
		PageDataPtr = (uint8*)RHILockStructuredBuffer(PageUploadBuffer.Buffer, 0, PageAllocationSize, RLM_WriteOnly);
	}

	uint8* Add_GetRef(uint32 PageSize, uint32 DstPageOffset)
	{
		check(NextPageIndex < MaxPages);
		check(NextPageOffset + PageSize <= MaxPageBytes);

		FPageInstallInfo& Info = InstallInfoPtr[NextPageIndex];
		Info.SrcPageOffset = NextPageOffset;
		Info.DstPageOffset = DstPageOffset;

		uint8* ResultPtr = PageDataPtr + NextPageOffset;
		NextPageOffset += PageSize;
		NextPageIndex++;

		return ResultPtr;
	}

	void Release()
	{
		InstallInfoUploadBuffer.Release();
		PageUploadBuffer.Release();
		ResetState();
	}

	void ResourceUploadTo(FRHICommandList& RHICmdList, FRWByteAddressBuffer& DstBuffer)
	{
		RHIUnlockStructuredBuffer(InstallInfoUploadBuffer.Buffer);
		RHIUnlockStructuredBuffer(PageUploadBuffer.Buffer);

		if (NextPageIndex > 0)
		{
			FTranscodePageToGPU_CS::FParameters Parameters;
			Parameters.InstallInfoBuffer	= InstallInfoUploadBuffer.SRV;
			Parameters.SrcPageBuffer		= PageUploadBuffer.SRV;
			Parameters.DstPageBuffer		= DstBuffer.UAV;

			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FTranscodePageToGPU_CS>();
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, FIntVector(MAX_TRANSCODE_GROUPS_PER_PAGE, NextPageIndex, 1));
		}

		ResetState();
	}
private:
	FByteAddressBuffer	InstallInfoUploadBuffer;
	FByteAddressBuffer	PageUploadBuffer;
	FPageInstallInfo*	InstallInfoPtr;
	uint8*				PageDataPtr;

	uint32				MaxPages;
	uint32				MaxPageBytes;
	uint32				NextPageIndex;
	uint32				NextPageOffset;
	
	void ResetState()
	{
		InstallInfoPtr = nullptr;
		PageDataPtr = nullptr;
		MaxPages = 0;
		MaxPageBytes = 0;
		NextPageIndex = 0;
		NextPageOffset = 0;
	}
};

FStreamingManager::FStreamingManager() :
	MaxStreamingPages(MAX_STREAMING_PAGES),
	MaxPendingPages(MAX_PENDING_PAGES),
	MaxStreamingReadbackBuffers(4u),
	ReadbackBuffersWriteIndex(0),
	ReadbackBuffersNumPending(0),
	NextRuntimeResourceID(0),
	NextUpdateIndex(0),
	NumRegisteredStreamingPages(0),
	NumPendingPages(0),
	NextPendingPageIndex(0)
#if !UE_BUILD_SHIPPING
	,PrevUpdateTick(0)
#endif
{
	LLM_SCOPE(ELLMTag::Nanite);

	check( MaxStreamingPages <= MAX_GPU_PAGES );
	StreamingRequestReadbackBuffers.AddZeroed( MaxStreamingReadbackBuffers );

	// Initialize pages
	StreamingPageInfos.AddUninitialized( MaxStreamingPages );
	for( uint32 i = 0; i < MaxStreamingPages; i++ )
	{
		FStreamingPageInfo& Page = StreamingPageInfos[ i ];
		Page.RegisteredKey = { INVALID_RUNTIME_RESOURCE_ID, INVALID_PAGE_INDEX };
		Page.ResidentKey = { INVALID_RUNTIME_RESOURCE_ID, INVALID_PAGE_INDEX };
		Page.GPUPageIndex = i;
	}

	// Add pages to free list
	StreamingPageInfoFreeList = &StreamingPageInfos[0];
	for( uint32 i = 1; i < MaxStreamingPages; i++ )
	{
		StreamingPageInfos[ i - 1 ].Next = &StreamingPageInfos[ i ];
	}
	StreamingPageInfos[ MaxStreamingPages - 1 ].Next = nullptr;

	// Initialize LRU sentinels
	StreamingPageLRU.RegisteredKey		= { INVALID_RUNTIME_RESOURCE_ID, INVALID_PAGE_INDEX };
	StreamingPageLRU.ResidentKey		= { INVALID_RUNTIME_RESOURCE_ID, INVALID_PAGE_INDEX };
	StreamingPageLRU.GPUPageIndex		= INVALID_PAGE_INDEX;
	StreamingPageLRU.LatestUpdateIndex	= 0xFFFFFFFFu;
	StreamingPageLRU.RefCount			= 0xFFFFFFFFu;
	StreamingPageLRU.Next				= &StreamingPageLRU;
	StreamingPageLRU.Prev				= &StreamingPageLRU;

	StreamingPageFixupChunks.SetNumUninitialized( MaxStreamingPages );

	PendingPages.SetNum( MaxPendingPages );

	RequestsHashTable	= new FRequestsHashTable();
	PageUploader		= new FStreamingPageUploader();
}

FStreamingManager::~FStreamingManager()
{
	delete RequestsHashTable;
	delete PageUploader;
}

void FStreamingManager::InitRHI()
{
	LLM_SCOPE(ELLMTag::Nanite);
	ClusterPageData.DataBuffer.Initialize( sizeof(uint32), 0, TEXT("FStreamingManagerClusterPageDataInitial") );
	ClusterPageHeaders.DataBuffer.Initialize( sizeof(uint32), 0, TEXT("FStreamingManagerClusterPageHeadersInitial") );
	Hierarchy.DataBuffer.Initialize( sizeof(uint32), 0, TEXT("FStreamingManagerHierarchyInitial") );	// Dummy allocation to make sure it is a valid resource
}

void FStreamingManager::ReleaseRHI()
{
	LLM_SCOPE(ELLMTag::Nanite);
	for (uint32 BufferIndex = 0; BufferIndex < MaxStreamingReadbackBuffers; ++BufferIndex)
	{
		if (StreamingRequestReadbackBuffers[BufferIndex])
		{
			delete StreamingRequestReadbackBuffers[BufferIndex];
			StreamingRequestReadbackBuffers[BufferIndex] = nullptr;
		}
	}

	ClusterPageData.Release();
	ClusterPageHeaders.Release();
	Hierarchy.Release();
	ClusterFixupUploadBuffer.Release();
	StreamingRequestsBuffer.SafeRelease();
}

void FStreamingManager::Add( FResources* Resources )
{
	LLM_SCOPE(ELLMTag::Nanite);
	if (Resources->RuntimeResourceID == INVALID_RUNTIME_RESOURCE_ID)
	{
		Resources->HierarchyOffset = Hierarchy.Allocator.Allocate(Resources->HierarchyNodes.Num());
		Hierarchy.TotalUpload += Resources->HierarchyNodes.Num();
		INC_DWORD_STAT_BY( STAT_NaniteTotalPages, Resources->PageStreamingStates.Num() );
		INC_DWORD_STAT_BY( STAT_NaniteRootPages, 1 );

		Resources->RootPageIndex = RootPagesAllocator.Allocate( 1 );

		Resources->RuntimeResourceID = NextRuntimeResourceID++;
		RuntimeResourceMap.Add( Resources->RuntimeResourceID, Resources );
		
		PendingAdds.Add( Resources );
	}
}

void FStreamingManager::Remove( FResources* Resources )
{
	LLM_SCOPE(ELLMTag::Nanite);
	if (Resources->RuntimeResourceID != INVALID_RUNTIME_RESOURCE_ID)
	{
		Hierarchy.Allocator.Free( Resources->HierarchyOffset, Resources->HierarchyNodes.Num() );
		Resources->HierarchyOffset = -1;

		RootPagesAllocator.Free( Resources->RootPageIndex, 1 );
		Resources->RootPageIndex = -1;

		const uint32 NumResourcePages = Resources->PageStreamingStates.Num();
		INC_DWORD_STAT_BY( STAT_NaniteTotalPages, NumResourcePages );
		DEC_DWORD_STAT_BY( STAT_NaniteRootPages, 1 );

		// Move all registered pages to the free list. No need to properly uninstall them as they are no longer referenced from the hierarchy.
		for( uint32 PageIndex = 0; PageIndex < NumResourcePages; PageIndex++ )
		{
			FPageKey Key = { Resources->RuntimeResourceID, PageIndex };
			FStreamingPageInfo* Page;
			if( RegisteredStreamingPagesMap.RemoveAndCopyValue(Key, Page) )
			{
				Page->RegisteredKey.RuntimeResourceID = INVALID_RUNTIME_RESOURCE_ID;	// Mark as free, so we won't try to uninstall it later
				MovePageToFreeList( Page );
			}
		}

		RuntimeResourceMap.Remove( Resources->RuntimeResourceID );
		Resources->RuntimeResourceID = INVALID_RUNTIME_RESOURCE_ID;
		PendingAdds.Remove( Resources );
	}
}

void FStreamingManager::CollectDependencyPages( FResources* Resources, TSet< FPageKey >& DependencyPages, const FPageKey& Key )
{
	LLM_SCOPE(ELLMTag::Nanite);
	if( DependencyPages.Find( Key ) )
		return;

	DependencyPages.Add( Key );

	FPageStreamingState& PageStreamingState = Resources->PageStreamingStates[ Key.PageIndex ];
	for( uint32 i = 0; i < PageStreamingState.DependenciesNum; i++ )
	{
		uint32 DependencyPageIndex = Resources->PageDependencies[ PageStreamingState.DependenciesStart + i ];

		if( IsRootPage( DependencyPageIndex ) )
			continue;

		FPageKey ChildKey = { Key.RuntimeResourceID, DependencyPageIndex };
		if( DependencyPages.Find( ChildKey ) == nullptr )
		{
			CollectDependencyPages( Resources, DependencyPages, ChildKey );
		}
	}
}

void FStreamingManager::SelectStreamingPages( FResources* Resources, TArray< FPageKey >& SelectedPages, TSet<FPageKey>& SelectedPagesSet, uint32 RuntimeResourceID, uint32 PageIndex, uint32 Priority, uint32 MaxSelectedPages )
{
	LLM_SCOPE(ELLMTag::Nanite);
	FPageKey Key = { RuntimeResourceID, PageIndex };
	if( SelectedPagesSet.Find( Key ) || (uint32)SelectedPages.Num() >= MaxSelectedPages )
		return;

	SelectedPagesSet.Add( Key );

	const FPageStreamingState& PageStreamingState = Resources->PageStreamingStates[ PageIndex ];
	
	for( uint32 i = 0; i < PageStreamingState.DependenciesNum; i++ )
	{
		uint32 DependencyPageIndex = Resources->PageDependencies[ PageStreamingState.DependenciesStart + i ];
		if( IsRootPage( DependencyPageIndex ) )
			continue;

		FPageKey DependencyKey = { RuntimeResourceID, DependencyPageIndex };
		if( RegisteredStreamingPagesMap.Find( DependencyKey ) == nullptr )
		{
			SelectStreamingPages( Resources, SelectedPages, SelectedPagesSet, RuntimeResourceID, DependencyPageIndex, Priority + 100u, MaxSelectedPages );
		}
	}

	if( (uint32)SelectedPages.Num() < MaxSelectedPages )
	{
		SelectedPages.Push( { RuntimeResourceID, PageIndex } );	// We need to write ourselves after our dependencies
	}
}

void FStreamingManager::RegisterStreamingPage( FStreamingPageInfo* Page, const FPageKey& Key )
{
	LLM_SCOPE(ELLMTag::Nanite);
	check( !IsRootPage( Key.PageIndex ) );

	FResources** Resources = RuntimeResourceMap.Find( Key.RuntimeResourceID );
	check( Resources != nullptr );
	
	TArray< FPageStreamingState >& PageStreamingStates = (*Resources)->PageStreamingStates;
	FPageStreamingState& PageStreamingState = PageStreamingStates[ Key.PageIndex ];
	
	for( uint32 i = 0; i < PageStreamingState.DependenciesNum; i++ )
	{
		uint32 DependencyPageIndex = ( *Resources )->PageDependencies[ PageStreamingState.DependenciesStart + i ];
		if( IsRootPage( DependencyPageIndex ) )
			continue;

		FPageKey DependencyKey = { Key.RuntimeResourceID, DependencyPageIndex };
		FStreamingPageInfo** DependencyPage = RegisteredStreamingPagesMap.Find( DependencyKey );
		check( DependencyPage != nullptr );
		(*DependencyPage)->RefCount++;
	}

	// Insert at the front of the LRU
	FStreamingPageInfo& LRUSentinel = StreamingPageLRU;

	Page->Prev = &LRUSentinel;
	Page->Next = LRUSentinel.Next;
	LRUSentinel.Next->Prev = Page;
	LRUSentinel.Next = Page;

	Page->RegisteredKey = Key;
	Page->LatestUpdateIndex = NextUpdateIndex;
	Page->RefCount = 0;

	// Register Page
	RegisteredStreamingPagesMap.Add(Key, Page);

	NumRegisteredStreamingPages++;
	INC_DWORD_STAT( STAT_NaniteRegisteredStreamingPages );
}

void FStreamingManager::UnregisterPage( const FPageKey& Key )
{
	LLM_SCOPE(ELLMTag::Nanite);
	check( !IsRootPage( Key.PageIndex ) );

	FResources** Resources = RuntimeResourceMap.Find( Key.RuntimeResourceID );
	check( Resources != nullptr );

	FStreamingPageInfo** PagePtr = RegisteredStreamingPagesMap.Find( Key );
	check( PagePtr != nullptr );
	FStreamingPageInfo* Page = *PagePtr;
	
	// Decrement reference counts of dependencies.
	TArray< FPageStreamingState >& PageStreamingStates = ( *Resources )->PageStreamingStates;
	FPageStreamingState& PageStreamingState = PageStreamingStates[ Key.PageIndex ];
	for( uint32 i = 0; i < PageStreamingState.DependenciesNum; i++ )
	{
		uint32 DependencyPageIndex = ( *Resources )->PageDependencies[ PageStreamingState.DependenciesStart + i ];
		if( IsRootPage( DependencyPageIndex ) )
			continue;

		FPageKey DependencyKey = { Key.RuntimeResourceID, DependencyPageIndex };
		FStreamingPageInfo** DependencyPage = RegisteredStreamingPagesMap.Find( DependencyKey );
		check( DependencyPage != nullptr );
		( *DependencyPage )->RefCount--;
	}

	RegisteredStreamingPagesMap.Remove( Key );
	MovePageToFreeList( Page );
}

void FStreamingManager::MovePageToFreeList( FStreamingPageInfo* Page )
{
	// Unlink
	FStreamingPageInfo* OldNext = Page->Next;
	FStreamingPageInfo* OldPrev = Page->Prev;
	OldNext->Prev = OldPrev;
	OldPrev->Next = OldNext;

	// Add to free list
	Page->Next = StreamingPageInfoFreeList;
	StreamingPageInfoFreeList = Page;

	NumRegisteredStreamingPages--;
	DEC_DWORD_STAT( STAT_NaniteRegisteredStreamingPages );
}

bool FStreamingManager::ArePageDependenciesCommitted(uint32 RuntimeResourceID, uint32 PageIndex, uint32 DependencyPageStart, uint32 DependencyPageNum)
{
	bool bResult = true;

	if (DependencyPageNum == 1)
	{
		// If there is only one dependency, we don't have to check as it is the page we are about to install.
		check(DependencyPageStart == PageIndex);
	}
	else if (DependencyPageNum > 1)	
	{
		for (uint32 i = 0; i < DependencyPageNum; i++)
		{
			uint32 DependencyPage = DependencyPageStart + i;
			FPageKey DependencyKey = { RuntimeResourceID, DependencyPage };
			FStreamingPageInfo** DependencyPagePtr = CommittedStreamingPageMap.Find(DependencyKey);
			if (DependencyPagePtr == nullptr || (*DependencyPagePtr)->ResidentKey != DependencyKey)	// Is the page going to be committed after this batch and does it already have its fixupchunk loaded?
			{
				bResult = false;
				break;
			}
		}
	}

	return bResult;
}

// Applies the fixups required to install/uninstall a page.
// Hierarchy references are patched up and leaf flags of parent clusters are set accordingly.
// GPUPageIndex == INVALID_PAGE_INDEX signals that the page should be uninstalled.
void FStreamingManager::ApplyFixups( const FFixupChunk& FixupChunk, const FResources& Resources, uint32 PageIndex, uint32 GPUPageIndex )
{
	LLM_SCOPE(ELLMTag::Nanite);

	const uint32 RuntimeResourceID = Resources.RuntimeResourceID;
	const uint32 HierarchyOffset = Resources.HierarchyOffset;
	bool bIsUninstall = ( GPUPageIndex == INVALID_PAGE_INDEX );
	uint32 Flags = bIsUninstall ? NANITE_CLUSTER_FLAG_LEAF : 0;

	// Fixup clusters
	for( uint32 i = 0; i < FixupChunk.Header.NumClusterFixups; i++ )
	{
		const FClusterFixup& Fixup = FixupChunk.GetClusterFixup( i );

		bool bPageDependenciesCommitted = bIsUninstall || ArePageDependenciesCommitted(RuntimeResourceID, PageIndex, Fixup.GetPageDependencyStart(), Fixup.GetPageDependencyNum());
		if (!bPageDependenciesCommitted)
			continue;
		
		uint32 TargetPageIndex = Fixup.GetPageIndex();
		uint32 TargetGPUPageIndex = INVALID_PAGE_INDEX;
		uint32 NumTargetPageClusters = 0;

		if( IsRootPage( TargetPageIndex ) )
		{
			TargetGPUPageIndex = MaxStreamingPages + Resources.RootPageIndex;
			NumTargetPageClusters = RootPageInfos[ Resources.RootPageIndex ].NumClusters;
		}
		else
		{
			FPageKey TargetKey = { RuntimeResourceID, TargetPageIndex };
			FStreamingPageInfo** TargetPagePtr = CommittedStreamingPageMap.Find( TargetKey );

			check( bIsUninstall || TargetPagePtr );
			if (TargetPagePtr)
			{
				FStreamingPageInfo* TargetPage = *TargetPagePtr;
				FFixupChunk& TargetFixupChunk = StreamingPageFixupChunks[TargetPage->GPUPageIndex];
				check(StreamingPageInfos[TargetPage->GPUPageIndex].ResidentKey == TargetKey);

				NumTargetPageClusters = TargetFixupChunk.Header.NumClusters;
				check(Fixup.GetClusterIndex() < NumTargetPageClusters);

				TargetGPUPageIndex = TargetPage->GPUPageIndex;
			}
		}
		
		if(TargetGPUPageIndex != INVALID_PAGE_INDEX)
		{
			uint32 ClusterIndex = Fixup.GetClusterIndex();
			uint32 FlagsOffset = offsetof( FPackedTriCluster, Flags );
			uint32 Offset = ( TargetGPUPageIndex << CLUSTER_PAGE_GPU_SIZE_BITS ) + ( ( FlagsOffset >> 4 ) * NumTargetPageClusters + ClusterIndex ) * 16 + ( FlagsOffset & 15 );
			ClusterFixupUploadBuffer.Add( Offset / sizeof( uint32 ), &Flags, 1 );
		}
	}

	// Fixup hierarchy
	for( uint32 i = 0; i < FixupChunk.Header.NumHierachyFixups; i++ )
	{
		const FHierarchyFixup& Fixup = FixupChunk.GetHierarchyFixup( i );

		bool bPageDependenciesCommitted = bIsUninstall || ArePageDependenciesCommitted(RuntimeResourceID, PageIndex, Fixup.GetPageDependencyStart(), Fixup.GetPageDependencyNum());
		if (!bPageDependenciesCommitted)
			continue;

		FPageKey TargetKey = { RuntimeResourceID, Fixup.GetPageIndex() };
		uint32 TargetGPUPageIndex = INVALID_PAGE_INDEX;
		if (!bIsUninstall)
		{
			if (IsRootPage(TargetKey.PageIndex))
			{
				TargetGPUPageIndex = MaxStreamingPages + Resources.RootPageIndex;
			}
			else
			{
				FStreamingPageInfo** TargetPagePtr = CommittedStreamingPageMap.Find(TargetKey);
				check(TargetPagePtr);
				check((*TargetPagePtr)->ResidentKey == TargetKey);
				TargetGPUPageIndex = (*TargetPagePtr)->GPUPageIndex;
			}
		}
		
		// Uninstalls are unconditional. The same uninstall might happen more than once.
		// If this page is getting uninstalled it also means it wont be reinstalled and any split groups can't be satisfied, so we can safely uninstall them.	
		
		uint32 HierarchyNodeIndex = Fixup.GetNodeIndex();
		check( HierarchyNodeIndex < (uint32)Resources.HierarchyNodes.Num() );
		uint32 ChildIndex = Fixup.GetChildIndex();
		uint32 ChildStartReference = bIsUninstall ? 0xFFFFFFFFu : ( ( TargetGPUPageIndex << MAX_CLUSTERS_PER_PAGE_BITS ) | Fixup.GetClusterGroupPartStartIndex() );
		uint32 Offset = ( size_t )&( ( (FPackedHierarchyNode*)0 )[ HierarchyOffset + HierarchyNodeIndex ].Misc[ ChildIndex ].ChildStartReference );
		Hierarchy.UploadBuffer.Add( Offset / sizeof( uint32 ), &ChildStartReference );
	}
}

bool FStreamingManager::ProcessPendingPages( FRHICommandListImmediate& RHICmdList )
{
	LLM_SCOPE(ELLMTag::Nanite);
	SCOPED_GPU_STAT(RHICmdList, NaniteStreaming);

	uint32 NumReadyPages = 0;
	uint32 StartPendingPageIndex = ( NextPendingPageIndex + MaxPendingPages - NumPendingPages ) % MaxPendingPages;

#if !UE_BUILD_SHIPPING
	uint64 UpdateTick = FPlatformTime::Cycles64();
	uint64 DeltaTick = PrevUpdateTick ? UpdateTick - PrevUpdateTick : 0;
	uint32 SimulatedBytesRemaining = FPlatformTime::ToSeconds64( DeltaTick ) * GNaniteStreamingBandwidthLimit * 1048576.0;
	PrevUpdateTick = UpdateTick;
#endif

	// Check how many pages are ready
	{
		SCOPE_CYCLE_COUNTER( STAT_NaniteCheckReadyPages );

		for( uint32 i = 0; i < NumPendingPages; i++ )
		{
			uint32 PendingPageIndex = ( StartPendingPageIndex + i ) % MaxPendingPages;
			FPendingPage& PendingPage = PendingPages[ PendingPageIndex ];
			bool bIsReady = true;

#if WITH_EDITOR == 0
			for( FGraphEventRef& EventRef : PendingPage.CompletionEvents )
			{
				if( !EventRef->IsComplete() )
				{
					bIsReady = false;
					break;
				}
			}

			if( !bIsReady )
				break;

			PendingPage.CompletionEvents.Empty();
#endif

#if !UE_BUILD_SHIPPING
			if( GNaniteStreamingBandwidthLimit >= 0.0 )
			{
				uint32 SimulatedBytesRead = FMath::Min( PendingPage.BytesLeftToStream, SimulatedBytesRemaining );
				PendingPage.BytesLeftToStream -= SimulatedBytesRead;
				SimulatedBytesRemaining -= SimulatedBytesRead;
				if( PendingPage.BytesLeftToStream > 0 )
					break;
			}
#endif

			NumReadyPages++;

			if( NumReadyPages >= MAX_INSTALLS_PER_UPDATE )
				break;
		}
	}
	
	if( NumReadyPages == 0 )
		return false;

	struct FTranscodeTask
	{
		FPendingPage* PendingPage = nullptr;
		uint8* Dst = nullptr;
#if WITH_EDITOR
		const uint8* ReadPtr = nullptr;
#else
		uint32 ReadOffset = 0;
#endif
		uint32 ReadSize = 0;
		FFixupChunk::FHeader Header;
	};

#if WITH_EDITOR
	TMap<FResources*, const uint8*> ResourceToBulkPointer;
#endif

	TArray<FTranscodeTask> TranscodeTasks;
	TranscodeTasks.AddDefaulted(NumReadyPages);

	// Install ready pages
	{
#if USE_GPU_TRANSCODE
		PageUploader->Init(MAX_INSTALLS_PER_UPDATE, MAX_INSTALLS_PER_UPDATE * CLUSTER_PAGE_DISK_SIZE);	//TODO: proper max size
#else
		ClusterPageData.UploadBuffer.Init(MAX_INSTALLS_PER_UPDATE, CLUSTER_PAGE_GPU_SIZE, false, TEXT("ClusterPageDataUploadBuffer"));
#endif
		ClusterFixupUploadBuffer.Init(MAX_INSTALLS_PER_UPDATE * MAX_CLUSTERS_PER_PAGE, sizeof(uint32), false, TEXT("ClusterFixupUploadBuffer"));	// No more parents than children, so no more than MAX_CLUSTER_PER_PAGE parents need to be fixed

		ClusterPageHeaders.UploadBuffer.Init(MAX_INSTALLS_PER_UPDATE, sizeof(uint32), false, TEXT("ClusterPageHeadersUploadBuffer"));
		Hierarchy.UploadBuffer.Init(2 * MAX_INSTALLS_PER_UPDATE  * MAX_CLUSTERS_PER_PAGE, sizeof(uint32), false, TEXT("HierarchyUploadBuffer"));	// Allocate enough to load all selected pages and evict old pages

		SCOPE_CYCLE_COUNTER( STAT_NaniteInstallStreamingPages );

		// Batched page install:
		// GPU uploads are unordered, so we need to make sure we have no overlapping writes.
		// For actual page uploads, we only upload the last page that ends up on a given GPU page.

		// Fixups are handled with set of UploadBuffers that are executed AFTER page upload.
		// To ensure we don't end up fixing up the same addresses more than once, we only perform the fixup associated with the first uninstall and the last install on a given GPU page.
		// If a page ends up being both installed and uninstalled in the same frame, we only install it to prevent a race.
		// Uninstall fixup depends on StreamingPageFixupChunks that is also updated by installs. To prevent races we perform all uninstalls before installs.
		
		// Calculate first and last Pending Page Index update for each GPU page.
		TMap<uint32, uint32> GPUPageToLastPendingPageIndex;
		for (uint32 i = 0; i < NumReadyPages; i++)
		{
			uint32 PendingPageIndex = (StartPendingPageIndex + i) % MaxPendingPages;
			FPendingPage& PendingPage = PendingPages[PendingPageIndex];
			
			// Update when the GPU page was touched for the last time.
			FResources** Resources = RuntimeResourceMap.Find(PendingPage.InstallKey.RuntimeResourceID);
			if(Resources)
			{
				GPUPageToLastPendingPageIndex.Add(PendingPage.GPUPageIndex, PendingPageIndex);
			}
		}

		TSet<FPageKey> BatchNewPageKeys;
		for (auto& Elem : GPUPageToLastPendingPageIndex)
		{
			uint32 GPUPageIndex = Elem.Key;

			// Remove uninstalled pages from streaming map, so we won't try to do uninstall fixup on them.
			FStreamingPageInfo& StreamingPageInfo = StreamingPageInfos[GPUPageIndex];
			if (StreamingPageInfo.ResidentKey.RuntimeResourceID != INVALID_RUNTIME_RESOURCE_ID)
			{
				CommittedStreamingPageMap.Remove(StreamingPageInfo.ResidentKey);
			}

			// Mark newly installed page
			FPendingPage& PendingPage = PendingPages[Elem.Value];
			BatchNewPageKeys.Add(PendingPage.InstallKey);
		}

		// Uninstall pages
		// We are uninstalling pages in a separate pass as installs will also overwrite the GPU page fixup information we need for uninstalls.
		for (auto& Elem : GPUPageToLastPendingPageIndex)
		{
			uint32 GPUPageIndex = Elem.Key;
			FStreamingPageInfo& StreamingPageInfo = StreamingPageInfos[GPUPageIndex];

			// Uninstall GPU page
			if (StreamingPageInfo.ResidentKey.RuntimeResourceID != INVALID_RUNTIME_RESOURCE_ID)
			{
				// Apply fixups to uninstall page. No need to fix up anything if resource is gone.
				FResources** Resources = RuntimeResourceMap.Find(StreamingPageInfo.ResidentKey.RuntimeResourceID);
				if (Resources)
				{
					// Prevent race between installs and uninstalls of the same page. Only uninstall if the page is not going to be installed again.
					if (!BatchNewPageKeys.Contains(StreamingPageInfo.ResidentKey))
					{
						ApplyFixups(StreamingPageFixupChunks[GPUPageIndex], **Resources, INVALID_PAGE_INDEX, INVALID_PAGE_INDEX);
					}
				}
			}

			StreamingPageInfo.ResidentKey.RuntimeResourceID = INVALID_RUNTIME_RESOURCE_ID;	// Only uninstall it the first time.
			DEC_DWORD_STAT(STAT_NaniteInstalledPages);
		}

		// Commit to streaming map, so install fixups will happen on all pages
		for (auto& Elem : GPUPageToLastPendingPageIndex)
		{
			uint32 GPUPageIndex = Elem.Key;
			uint32 LastPendingPageIndex = Elem.Value;
			FPendingPage& PendingPage = PendingPages[LastPendingPageIndex];

			FResources** Resources = RuntimeResourceMap.Find(PendingPage.InstallKey.RuntimeResourceID);
			if (Resources)
			{
				CommittedStreamingPageMap.Add(PendingPage.InstallKey, &StreamingPageInfos[GPUPageIndex]);
			}
		}

		// Install pages
		// Must be processed in PendingPages order so FFixupChunks are loaded when we need them.
		for (uint32 i = 0; i < NumReadyPages; i++)
		{
			uint32 LastPendingPageIndex = (StartPendingPageIndex + i) % MaxPendingPages;
			FPendingPage& PendingPage = PendingPages[LastPendingPageIndex];

			FTranscodeTask& TranscodeTask = TranscodeTasks[i];
			TranscodeTask.PendingPage = &PendingPage;

			uint32* PagePtr = GPUPageToLastPendingPageIndex.Find(PendingPages[LastPendingPageIndex].GPUPageIndex);
			if (PagePtr == nullptr || *PagePtr != LastPendingPageIndex)
				continue;

			FStreamingPageInfo& StreamingPageInfo = StreamingPageInfos[PendingPage.GPUPageIndex];
			
			FResources** Resources = RuntimeResourceMap.Find( PendingPage.InstallKey.RuntimeResourceID );
			check(Resources);

			TArray< FPageStreamingState >& PageStreamingStates = ( *Resources )->PageStreamingStates;
			const FPageStreamingState& PageStreamingState = PageStreamingStates[ PendingPage.InstallKey.PageIndex ];
			FStreamingPageInfo* StreamingPage = &StreamingPageInfos[ PendingPage.GPUPageIndex ];

			CommittedStreamingPageMap.Add(PendingPage.InstallKey, StreamingPage);

#if WITH_EDITOR
			// Make sure we only lock each resource BulkData once.
			const uint8** BulkDataPtrPtr = ResourceToBulkPointer.Find(*Resources);
			const uint8* BulkDataPtr;
			if (!BulkDataPtrPtr)
			{
				FByteBulkData& BulkData = (*Resources)->StreamableClusterPages;
				check(BulkData.IsBulkDataLoaded() && BulkData.GetBulkDataSize() > 0);
				BulkDataPtr = (const uint8*)BulkData.LockReadOnly();
				ResourceToBulkPointer.Add(*Resources, BulkDataPtr);
			}
			else
			{
				BulkDataPtr = *BulkDataPtrPtr;
			}
			
			const uint8* Ptr = BulkDataPtr + PageStreamingState.BulkOffset;
			uint32 FixupChunkSize = ((FFixupChunk*)Ptr)->GetSize();

			FFixupChunk* FixupChunk = &StreamingPageFixupChunks[PendingPage.GPUPageIndex];
			FMemory::Memcpy(FixupChunk, Ptr, FixupChunkSize);
			TranscodeTask.ReadPtr = Ptr + FixupChunkSize;
#else
			// Read header of FixupChunk so the length can be calculated
			FFixupChunk* FixupChunk = &StreamingPageFixupChunks[ PendingPage.GPUPageIndex ];

			PendingPage.ReadStream->CopyTo( FixupChunk, 0, sizeof( FFixupChunk::Header ) );
			uint32 FixupChunkSize = FixupChunk->GetSize();

			// Read the rest of FixupChunk
			PendingPage.ReadStream->CopyTo( FixupChunk->Data, sizeof( FFixupChunk::Header ), FixupChunkSize - sizeof( FFixupChunk::Header ) );
			TranscodeTask.ReadOffset = FixupChunkSize;
#endif

			
			TranscodeTask.PendingPage = &PendingPage;
			TranscodeTask.ReadSize = PageStreamingState.BulkSize - FixupChunkSize;
			TranscodeTask.Header = FixupChunk->Header;
#if USE_GPU_TRANSCODE
			TranscodeTask.Dst = PageUploader->Add_GetRef(TranscodeTask.ReadSize, PendingPage.GPUPageIndex << CLUSTER_PAGE_GPU_SIZE_BITS);
#else
			TranscodeTask.Dst = (uint8*)ClusterPageData.UploadBuffer.Add_GetRef(PendingPage.GPUPageIndex);
#endif

			// Update page headers
			uint32 NumPageClusters = FixupChunk->Header.NumClusters;
			ClusterPageHeaders.UploadBuffer.Add( PendingPage.GPUPageIndex, &NumPageClusters );

			// Apply fixups to install page
			StreamingPage->ResidentKey = PendingPage.InstallKey;
			ApplyFixups( *FixupChunk, **Resources, PendingPage.InstallKey.PageIndex, PendingPage.GPUPageIndex );

			INC_DWORD_STAT( STAT_NaniteInstalledPages );
			INC_DWORD_STAT(STAT_NanitePageInstalls);
		}
	}

	// Transcode pages
	ParallelFor(NumReadyPages, [&TranscodeTasks](int32 i)
	{
		SCOPE_CYCLE_COUNTER(STAT_NaniteTranscodePageTask);
		const FTranscodeTask& Task = TranscodeTasks[i];
		
		// Read GPU data
#if WITH_EDITOR
		if (Task.Dst)
		{
#if USE_GPU_TRANSCODE
			FMemory::Memcpy(Task.Dst, Task.ReadPtr, Task.ReadSize);
#else
			TranscodePageToGPU(Task.Dst, Task.ReadPtr, Task.ReadSize);
#endif
		}
#else
		if (Task.Dst)
		{
			TArray<uint8> Tmp;
			Tmp.SetNumUninitialized(Task.ReadSize);
			Task.PendingPage->ReadStream->CopyTo(Tmp.GetData(), Task.ReadOffset, Task.ReadSize);
#if USE_GPU_TRANSCODE
			FMemory::Memcpy(Task.Dst, Tmp.GetData(), Tmp.Num());
#else
			TranscodePageToGPU(Task.Dst, Tmp.GetData(), Tmp.Num());
#endif
		}

		// Clean up IO handles
		Task.PendingPage->ReadStream.SafeRelease();
		delete Task.PendingPage->Handle;
		Task.PendingPage->Handle = nullptr;
#endif
	});

#if WITH_EDITOR
	// Unlock BulkData
	for (auto it : ResourceToBulkPointer)
	{
		FResources* Resources = it.Key;
		FByteBulkData& BulkData = Resources->StreamableClusterPages;
		BulkData.Unlock();
	}
#endif
	
	{
		SCOPE_CYCLE_COUNTER(STAT_NaniteUpload);

		{
			FRHIUnorderedAccessView* UAVs[] = { ClusterPageData.DataBuffer.UAV, ClusterPageHeaders.DataBuffer.UAV, Hierarchy.DataBuffer.UAV };
			RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, UAVs, UE_ARRAY_COUNT(UAVs));
		}
		
#if USE_GPU_TRANSCODE
		PageUploader->ResourceUploadTo(RHICmdList, ClusterPageData.DataBuffer);
#else
		ClusterPageData.UploadBuffer.ResourceUploadTo(RHICmdList, ClusterPageData.DataBuffer, false);
#endif

		ClusterPageHeaders.UploadBuffer.ResourceUploadTo(RHICmdList, ClusterPageHeaders.DataBuffer, false);
		Hierarchy.UploadBuffer.ResourceUploadTo(RHICmdList, Hierarchy.DataBuffer, false);

		// NOTE: We need an additional barrier here to make sure pages are finished uploading before fixups can be applied.
		{
			FRHIUnorderedAccessView* UAVs[] = { ClusterPageData.DataBuffer.UAV };
			RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, UAVs, UE_ARRAY_COUNT(UAVs));
		}
		ClusterFixupUploadBuffer.ResourceUploadTo(RHICmdList, ClusterPageData.DataBuffer, false);
	}


	NumPendingPages -= NumReadyPages;
	DEC_DWORD_STAT_BY( STAT_NanitePendingPages, NumReadyPages );

	return true;
}

#if DO_CHECK
void FStreamingManager::VerifyPageLRU( FStreamingPageInfo& List, uint32 TargetListLength, bool bCheckUpdateIndex )
{
	SCOPE_CYCLE_COUNTER( STAT_NaniteVerifyLRU );

	uint32 ListLength = 0u;
	uint32 PrevUpdateIndex = 0u;
	FStreamingPageInfo* Ptr = List.Prev;
	while( Ptr != &List )
	{
		if( bCheckUpdateIndex )
		{
			check( Ptr->LatestUpdateIndex >= PrevUpdateIndex );
			PrevUpdateIndex = Ptr->LatestUpdateIndex;
		}

		ListLength++;
		Ptr = Ptr->Prev;
	}

	check( ListLength == TargetListLength );
}
#endif

bool FStreamingManager::ProcessNewResources( FRHICommandListImmediate& RHICmdList )
{
	LLM_SCOPE(ELLMTag::Nanite);

	if( PendingAdds.Num() == 0 )
		return false;

	SCOPE_CYCLE_COUNTER( STAT_NaniteInstallNewResources );
	SCOPED_GPU_STAT(RHICmdList, NaniteStreaming);

	// Upload hierarchy for pending resources
	ResizeResourceIfNeeded( RHICmdList, Hierarchy.DataBuffer, FMath::RoundUpToPowerOfTwo( Hierarchy.Allocator.GetMaxSize() ) * sizeof( FPackedHierarchyNode ), TEXT("FStreamingManagerHierarchy") );

	check( MaxStreamingPages <= MAX_GPU_PAGES );
	uint32 MaxRootPages = MAX_GPU_PAGES - MaxStreamingPages;
	uint32 NumAllocatedRootPages = FMath::Clamp( FMath::RoundUpToPowerOfTwo( RootPagesAllocator.GetMaxSize() ), MIN_ROOT_PAGES_CAPACITY, MaxRootPages );
	check( NumAllocatedRootPages >= (uint32)RootPagesAllocator.GetMaxSize() );	// Root pages just don't fit!

	uint32 NumAllocatedPages = MaxStreamingPages + NumAllocatedRootPages;
	check( NumAllocatedPages <= MAX_GPU_PAGES );
	ResizeResourceIfNeeded( RHICmdList, ClusterPageHeaders.DataBuffer, NumAllocatedPages * sizeof( uint32 ), TEXT("FStreamingManagerClusterPageHeaders") );
	ResizeResourceIfNeeded( RHICmdList, ClusterPageData.DataBuffer, NumAllocatedPages << CLUSTER_PAGE_GPU_SIZE_BITS, TEXT("FStreamingManagerClusterPageData") );

	check( NumAllocatedPages <= ( 1u << ( 31 - CLUSTER_PAGE_GPU_SIZE_BITS ) ) );	// 2GB seems to be some sort of limit.
																				// TODO: Is it a GPU/API limit or is it a signed integer bug on our end?

	RootPageInfos.SetNum( NumAllocatedRootPages );

	uint32 NumPendingAdds = PendingAdds.Num();

	// TODO: These uploads can end up being quite large.
	// We should try to change the high level logic so the proxy is not considered loaded until the root page has been loaded, so we can split this over multiple frames.
	
	ClusterPageHeaders.UploadBuffer.Init( NumPendingAdds, sizeof( uint32 ), false, TEXT("FStreamingManagerClusterPageHeadersUpload") );
	Hierarchy.UploadBuffer.Init( Hierarchy.TotalUpload, sizeof( FPackedHierarchyNode ), false, TEXT("FStreamingManagerHierarchyUpload"));
	
	// Calculate total requires size
#if USE_GPU_TRANSCODE
	uint32 TotalDiskSize = 0;
	for(uint32 i = 0; i < NumPendingAdds; i++)
	{
		FResources* Resources = PendingAdds[i];
		
		uint8* Ptr = Resources->RootClusterPage.GetData();
		FFixupChunk& FixupChunk = *(FFixupChunk*)Ptr;
		uint32 FixupChunkSize = FixupChunk.GetSize();
		uint32 DiskSize = Resources->PageStreamingStates[0].BulkSize - FixupChunkSize;
		TotalDiskSize += DiskSize;
	}
	PageUploader->Init(NumPendingAdds, TotalDiskSize);
#else
	ClusterPageData.UploadBuffer.Init(NumPendingAdds, CLUSTER_PAGE_GPU_SIZE, false, TEXT("FStreamingManagerClusterPageDataUpload"));
#endif

	for( FResources* Resources : PendingAdds )
	{
		uint32 GPUPageIndex = MaxStreamingPages + Resources->RootPageIndex;
		uint8* Ptr = Resources->RootClusterPage.GetData();
		FFixupChunk& FixupChunk = *(FFixupChunk*)Ptr;
		uint32 FixupChunkSize = FixupChunk.GetSize();
		uint32 NumClusters = FixupChunk.Header.NumClusters;

		uint32 PageDiskSize = Resources->PageStreamingStates[0].BulkSize - FixupChunkSize;
#if USE_GPU_TRANSCODE
		uint8* Dst = PageUploader->Add_GetRef(PageDiskSize, GPUPageIndex << CLUSTER_PAGE_GPU_SIZE_BITS);
		FMemory::Memcpy(Dst, Ptr + FixupChunkSize, PageDiskSize);
#else
		uint8* Dst = ClusterPageData.UploadBuffer.Add_GetRef( GPUPageIndex );
		TranscodePageToGPU(Dst, Ptr + FixupChunkSize, PageDiskSize);
#endif

		ClusterPageHeaders.UploadBuffer.Add(GPUPageIndex, &NumClusters);

		// Root node should only have fixups that depend on other pages and cannot be satisfied yet.

		// Fixup hierarchy
		for(uint32 i = 0; i < FixupChunk.Header.NumHierachyFixups; i++)
		{
			const FHierarchyFixup& Fixup = FixupChunk.GetHierarchyFixup( i );
			uint32 HierarchyNodeIndex = Fixup.GetNodeIndex();
			check( HierarchyNodeIndex < (uint32)Resources->HierarchyNodes.Num() );
			uint32 ChildIndex = Fixup.GetChildIndex();
			uint32 GroupStartIndex = Fixup.GetClusterGroupPartStartIndex();
			uint32 ChildStartReference = ( GPUPageIndex << MAX_CLUSTERS_PER_PAGE_BITS ) | Fixup.GetClusterGroupPartStartIndex();

			if(Fixup.GetPageDependencyNum() == 0)	// Only install part if it has no other dependencies
			{
				Resources->HierarchyNodes[HierarchyNodeIndex].Misc[ChildIndex].ChildStartReference = ChildStartReference;
			}
		}
		
		Hierarchy.UploadBuffer.Add( Resources->HierarchyOffset, &Resources->HierarchyNodes[ 0 ], Resources->HierarchyNodes.Num() );

		FRootPageInfo& RootPageInfo = RootPageInfos[ Resources->RootPageIndex ];
		RootPageInfo.RuntimeResourceID = Resources->RuntimeResourceID;
		RootPageInfo.NumClusters = NumClusters;
		Resources->RootClusterPage.Empty();
	}

	{
		SCOPE_CYCLE_COUNTER( STAT_NaniteUpload );

		FRHIUnorderedAccessView* UAVs[] = { ClusterPageData.DataBuffer.UAV, ClusterPageHeaders.DataBuffer.UAV, Hierarchy.DataBuffer.UAV };
		RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, UAVs, UE_ARRAY_COUNT(UAVs));

		Hierarchy.TotalUpload = 0;
		Hierarchy.UploadBuffer.ResourceUploadTo( RHICmdList, Hierarchy.DataBuffer, false );
		ClusterPageHeaders.UploadBuffer.ResourceUploadTo( RHICmdList, ClusterPageHeaders.DataBuffer, false );
#if USE_GPU_TRANSCODE
		PageUploader->ResourceUploadTo(RHICmdList, ClusterPageData.DataBuffer);
#else
		ClusterPageData.UploadBuffer.ResourceUploadTo(RHICmdList, ClusterPageData.DataBuffer, false);
#endif
	}

	PendingAdds.Reset();
	if( NumPendingAdds > 1 )
	{
#if USE_GPU_TRANSCODE
		PageUploader->Release();
#else
		ClusterPageData.UploadBuffer.Release();	// Release large buffers. On uploads RHI ends up using the full size of the buffer, NOT just the size of the update, so we need to keep the size down.
#endif
	}
	
	return true;
}

void FStreamingManager::Update( FRHICommandListImmediate& RHICmdList )
{	
	LLM_SCOPE(ELLMTag::Nanite);
	SCOPED_NAMED_EVENT( STAT_NaniteStreamingManagerUpdate, FColor::Red );
	SCOPE_CYCLE_COUNTER( STAT_NaniteStreamingManagerUpdate );
	SCOPED_GPU_STAT(RHICmdList, NaniteStreaming);

	if( !StreamingRequestsBuffer.IsValid() )
	{
		// Init and clear StreamingRequestsBuffer.
		// Can't do this in InitRHI as RHICmdList doesn't have a valid context yet.
		FRDGBuilder GraphBuilder( RHICmdList );
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(4, 3 * MAX_STREAMING_REQUESTS);
		Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_SourceCopy);
		FRDGBufferRef StreamingRequestsBufferRef = GraphBuilder.CreateBuffer( Desc, TEXT( "StreamingRequests" ) );	// TODO: Can't be a structured buffer as EnqueueCopy is only defined for vertex buffers
		AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV( StreamingRequestsBufferRef, PF_R32_UINT ), 0 );
		GraphBuilder.QueueBufferExtraction( StreamingRequestsBufferRef, &StreamingRequestsBuffer);
		GraphBuilder.Execute();
	}

	bool bBuffersTransitionedToWrite = false;

	bBuffersTransitionedToWrite |= ProcessNewResources( RHICmdList  );

#if WITH_EDITOR == 0
	bBuffersTransitionedToWrite |= ProcessPendingPages( RHICmdList );
#endif

	// Process readback
	FRHIGPUBufferReadback* LatestReadbackBuffer = nullptr;
	
	{
		// Find latest buffer that is ready
		uint32 Index = ( ReadbackBuffersWriteIndex + MaxStreamingReadbackBuffers - ReadbackBuffersNumPending ) % MaxStreamingReadbackBuffers;
		while( ReadbackBuffersNumPending > 0 )
		{
			if( StreamingRequestReadbackBuffers[ Index ]->IsReady() )	//TODO: process all buffers or just the latest?
			{
				ReadbackBuffersNumPending--;
				LatestReadbackBuffer = StreamingRequestReadbackBuffers[ Index ];
			}
			else
			{
				break;
			}
		}
	}
	
	auto StreamingPriorityPredicate = []( const FStreamingRequest& A, const FStreamingRequest& B ) { return A.Priority > B.Priority; };

	PrioritizedRequestsHeap.Empty( MAX_STREAMING_REQUESTS );

	if( LatestReadbackBuffer )
	{
		SCOPE_CYCLE_COUNTER( STAT_NaniteProcessReadback );
		const uint32* BufferPtr = (const uint32*) LatestReadbackBuffer->Lock( MAX_STREAMING_REQUESTS * sizeof( uint32 ) * 3 );
		uint32 NumStreamingRequests = FMath::Min( BufferPtr[ 0 ], MAX_STREAMING_REQUESTS - 1u );	// First request is reserved for counter

		if( NumStreamingRequests > 0 )
		{
			// Update priorities
			FGPUStreamingRequest* StreamingRequestsPtr = ( ( FGPUStreamingRequest* ) BufferPtr + 1 );

			{
				SCOPE_CYCLE_COUNTER( STAT_NaniteDeduplicateRequests );
				RequestsHashTable->Clear();
				for( uint32 Index = 0; Index < NumStreamingRequests; Index++ )
				{
					const FGPUStreamingRequest& GPURequest = StreamingRequestsPtr[ Index ];
					uint32 NumPages = GPURequest.PageIndex_NumPages & MAX_GROUP_PARTS_MASK;
					uint32 PageStartIndex = GPURequest.PageIndex_NumPages >> MAX_GROUP_PARTS_BITS;
					
					FStreamingRequest Request;
					Request.Key.RuntimeResourceID = GPURequest.RuntimeResourceID;
					Request.Priority = GPURequest.Priority;
					for (uint32 i = 0; i < NumPages; i++)
					{
						Request.Key.PageIndex = PageStartIndex + i;
						check(!IsRootPage(Request.Key.PageIndex));
						RequestsHashTable->AddRequest(Request);
					}
				}
			}

			uint32 NumUniqueStreamingRequests = RequestsHashTable->GetNumElements();
#if 0
			// Verify against TMap
			{
				TMap< FPageKey, uint32 > UniqueRequestsMap;	
				{
					for( uint32 Index = 0; Index < NumStreamingRequests; Index++ )
					{
						const FStreamingRequest& Request = StreamingRequestsPtr[ Index ];
						check( Request.Key.PageIndex != 0 );
						uint32* Priority = UniqueRequestsMap.Find( Request.Key );
						if( Priority )
							*Priority = FMath::Max( *Priority, Request.Priority );
						else
							UniqueRequestsMap.Add( Request.Key, Request.Priority );
					}
				}

				check( UniqueRequestsMap.Num() == NumUniqueStreamingRequests );
				for( uint32 i = 0; i < NumUniqueStreamingRequests; i++ )
				{
					const FStreamingRequest& Request = RequestsHashTable->GetElement(i);
					uint32* Priority = UniqueRequestsMap.Find( Request.Key );
					check( Priority );
					check( *Priority == Request.Priority );
				}
			}
#endif

			INC_DWORD_STAT_BY( STAT_NaniteStreamingRequests, NumStreamingRequests );
			INC_DWORD_STAT_BY( STAT_NaniteUniqueStreamingRequests, NumUniqueStreamingRequests );

			{
				SCOPE_CYCLE_COUNTER( STAT_NaniteUpdatePriorities );
				
				struct FPrioritizedStreamingPage
				{
					FStreamingPageInfo* Page;
					uint32 Priority;
				};

				TArray< FPrioritizedStreamingPage > UpdatedPages;
				for(uint32 UniqueRequestIndex = 0; UniqueRequestIndex < NumUniqueStreamingRequests; UniqueRequestIndex++)
				{
					const FStreamingRequest& Request = RequestsHashTable->GetElement(UniqueRequestIndex);
					FStreamingPageInfo** StreamingPage = RegisteredStreamingPagesMap.Find( Request.Key );
					if( StreamingPage )
					{
						// Update index and move to front of LRU.
						(*StreamingPage)->LatestUpdateIndex = NextUpdateIndex;
						UpdatedPages.Push( { *StreamingPage, Request.Priority } );
					}
					else
					{
						// Page isn't there. Is the resource still here?
						FResources** Resources = RuntimeResourceMap.Find( Request.Key.RuntimeResourceID );
						if( Resources )
						{
							// ResourcesID is valid, so add request to the queue
							PrioritizedRequestsHeap.Push( Request );
						}
					}
				}

				PrioritizedRequestsHeap.Heapify( StreamingPriorityPredicate );

				{
					SCOPE_CYCLE_COUNTER( STAT_NanitePrioritySort );
					UpdatedPages.Sort( []( const FPrioritizedStreamingPage& A, const FPrioritizedStreamingPage& B ) { return A.Priority < B.Priority; } );
				}

				{
					SCOPE_CYCLE_COUNTER( STAT_NaniteUpdateLRU );

					for( const FPrioritizedStreamingPage& PrioritizedPage : UpdatedPages )
					{
						FStreamingPageInfo* Page = PrioritizedPage.Page;

						// Unlink
						FStreamingPageInfo* OldNext = Page->Next;
						FStreamingPageInfo* OldPrev = Page->Prev;
						OldNext->Prev = OldPrev;
						OldPrev->Next = OldNext;

						// Insert at the front of the LRU
						Page->Prev = &StreamingPageLRU;
						Page->Next = StreamingPageLRU.Next;
						StreamingPageLRU.Next->Prev = Page;
						StreamingPageLRU.Next = Page;
					}
				}
			}
		}
		LatestReadbackBuffer->Unlock();

#if DO_CHECK
		VerifyPageLRU( StreamingPageLRU, NumRegisteredStreamingPages, true );
#endif
			
		uint32 MaxSelectedPages = MaxPendingPages - NumPendingPages;
		if( PrioritizedRequestsHeap.Num() > 0 )
		{
			TArray< FPageKey > SelectedPages;
			TSet< FPageKey > SelectedPagesSet;
			
			{
				SCOPE_CYCLE_COUNTER( STAT_NaniteSelectStreamingPages );

				// Add low priority pages based on prioritized requests
				while( (uint32)SelectedPages.Num() < MaxSelectedPages && PrioritizedRequestsHeap.Num() > 0 )
				{
					FStreamingRequest SelectedRequest;
					PrioritizedRequestsHeap.HeapPop( SelectedRequest, StreamingPriorityPredicate, false );
					FResources** Resources = RuntimeResourceMap.Find( SelectedRequest.Key.RuntimeResourceID );
					check( Resources != nullptr );

					SelectStreamingPages( *Resources, SelectedPages, SelectedPagesSet, SelectedRequest.Key.RuntimeResourceID, SelectedRequest.Key.PageIndex, SelectedRequest.Priority, MaxSelectedPages );
				}
				check( (uint32)SelectedPages.Num() <= MaxSelectedPages );
			}

			if( SelectedPages.Num() > 0 )
			{
				// Collect all pending registration dependencies so we are not going to remove them.
				TSet< FPageKey > RegistrationDependencyPages;
				for( const FPageKey& SelectedKey : SelectedPages )
				{
					FResources** Resources = RuntimeResourceMap.Find( SelectedKey.RuntimeResourceID );
					check( Resources != nullptr );

					CollectDependencyPages( *Resources, RegistrationDependencyPages, SelectedKey );	// Mark all dependencies as unremovable.
				}

				// Register Pages
				for( const FPageKey& SelectedKey : SelectedPages )
				{

					FPendingPage& PendingPage = PendingPages[ NextPendingPageIndex ];

					if( NumRegisteredStreamingPages >= MaxStreamingPages )
					{
						// No space. Free a page!
						FStreamingPageInfo* StreamingPage = StreamingPageLRU.Prev;
						while( StreamingPage != &StreamingPageLRU )
						{
							FStreamingPageInfo* PrevStreamingPage = StreamingPage->Prev;

							// Only remove leaf nodes. Make sure to never delete a node that was added this frame or is a dependency for a pending page registration.
							FPageKey FreeKey = PrevStreamingPage->RegisteredKey;
							if( PrevStreamingPage->RefCount == 0 && ( PrevStreamingPage->LatestUpdateIndex < NextUpdateIndex ) && RegistrationDependencyPages.Find( FreeKey ) == nullptr )
							{
								FStreamingPageInfo** Page = RegisteredStreamingPagesMap.Find( FreeKey );
								check( Page != nullptr );
								UnregisterPage( FreeKey );
								break;
							}
							StreamingPage = PrevStreamingPage;
						}
					}

					if( NumRegisteredStreamingPages >= MaxStreamingPages )
						break;

					FResources** Resources = RuntimeResourceMap.Find( SelectedKey.RuntimeResourceID );
					check( Resources );
					FByteBulkData& BulkData = ( *Resources )->StreamableClusterPages;
					const FPageStreamingState& PageStreamingState = ( *Resources )->PageStreamingStates[ SelectedKey.PageIndex ];
					check( !IsRootPage( SelectedKey.PageIndex ) );

#if WITH_EDITOR == 0
					// Start async IO
					PendingPage.Handle = IFileCacheHandle::CreateFileCacheHandle(BulkData.OpenAsyncReadHandle());
					PendingPage.ReadStream = PendingPage.Handle->ReadData( PendingPage.CompletionEvents, PageStreamingState.BulkOffset, PageStreamingState.BulkSize, AIOP_Normal );
					if(PendingPage.ReadStream == nullptr)
					{
						// IO can fail. Retry next frame if it does. We can't just proceed to the next request as it might depend on this one.
						UE_LOG(LogNaniteStreaming, Warning, TEXT("IFileCache.ReadData failed for %s"), *BulkData.GetFilename());
						delete PendingPage.Handle;
						PendingPage.Handle = nullptr;
						break;
					}
					check(PendingPage.ReadStream != nullptr);
#endif

					// Grab a free page
					check(StreamingPageInfoFreeList != nullptr);
					FStreamingPageInfo* Page = StreamingPageInfoFreeList;
					StreamingPageInfoFreeList = StreamingPageInfoFreeList->Next;


					PendingPage.InstallKey = SelectedKey;
					PendingPage.GPUPageIndex = Page->GPUPageIndex;

					NextPendingPageIndex = ( NextPendingPageIndex + 1 ) % MaxPendingPages;
					NumPendingPages++;
					INC_DWORD_STAT( STAT_NanitePendingPages );

#if !UE_BUILD_SHIPPING
					PendingPage.BytesLeftToStream = PageStreamingState.BulkSize;
#endif

					RegisterStreamingPage( Page, SelectedKey );
				}
			}
		}
	}

#if WITH_EDITOR
	bBuffersTransitionedToWrite |= ProcessPendingPages( RHICmdList );	// Process streaming requests immediately in editor
#endif

	// Transition resource back to read
	if( bBuffersTransitionedToWrite )
	{
		FRHIUnorderedAccessView* UAVs[] = { Hierarchy.DataBuffer.UAV , ClusterPageData.DataBuffer.UAV, ClusterPageHeaders.DataBuffer.UAV };
		RHICmdList.TransitionResources( EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, UAVs, UE_ARRAY_COUNT(UAVs) );
	}

	NextUpdateIndex++;
}

BEGIN_SHADER_PARAMETER_STRUCT(FReadbackPassParameters, )
	SHADER_PARAMETER_RDG_BUFFER(, Input)
END_SHADER_PARAMETER_STRUCT()

void FStreamingManager::SubmitFrameStreamingRequests(FRDGBuilder& GraphBuilder)
{
	LLM_SCOPE(ELLMTag::Nanite);
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteStreaming);
	RDG_EVENT_SCOPE(GraphBuilder, "NaniteStreaming");

	if( ReadbackBuffersNumPending == MaxStreamingReadbackBuffers )
	{
		// Return when queue is full. It is NOT safe to EnqueueCopy on a buffer that already has a pending copy.
		return;
	}

	if( StreamingRequestReadbackBuffers[ ReadbackBuffersWriteIndex ] == nullptr )
	{
		FRHIGPUBufferReadback* GPUBufferReadback = new FRHIGPUBufferReadback( TEXT("Nanite streaming requests readback") );
		StreamingRequestReadbackBuffers[ ReadbackBuffersWriteIndex ] = GPUBufferReadback;
	}

	FRDGBufferRef Buffer = GraphBuilder.RegisterExternalBuffer(
		StreamingRequestsBuffer,
		TEXT("StreamingRequests"),
		ERDGParentResourceFlags::None,
		EResourceTransitionAccess::EReadable,
		EResourceTransitionAccess::EWritable);

	{
		FRHIGPUBufferReadback* ReadbackBuffer = StreamingRequestReadbackBuffers[ReadbackBuffersWriteIndex];

		FReadbackPassParameters* PassParameters = GraphBuilder.AllocParameters<FReadbackPassParameters>();
		PassParameters->Input = Buffer;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Readback"),
			PassParameters,
			ERDGPassFlags::Readback,
			[ReadbackBuffer, Buffer](FRHICommandList& RHICmdList)
		{
			Buffer->MarkResourceAsUsed();
			ReadbackBuffer->EnqueueCopy(RHICmdList, Buffer->GetRHIVertexBuffer(), 0u);
		});
	}

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Buffer, PF_R32_UINT), 0);

	ReadbackBuffersWriteIndex = ( ReadbackBuffersWriteIndex + 1u ) % MaxStreamingReadbackBuffers;
	ReadbackBuffersNumPending = FMath::Min( ReadbackBuffersNumPending + 1u, MaxStreamingReadbackBuffers );
}

TGlobalResource< FStreamingManager > GStreamingManager;

} // namespace Nanite