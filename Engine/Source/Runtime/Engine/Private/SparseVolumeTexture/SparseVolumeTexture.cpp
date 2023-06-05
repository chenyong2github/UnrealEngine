// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SparseVolumeTexture.cpp: SparseVolumeTexture implementation.
=============================================================================*/

#include "SparseVolumeTexture/SparseVolumeTexture.h"

#include "Materials/Material.h"
#include "MaterialShared.h"
#include "UObject/UObjectIterator.h"
#include "Misc/SecureHash.h"
#include "EngineUtils.h"
#include "Shader/ShaderTypes.h"
#include "RenderingThread.h"
#include "GlobalRenderResources.h"
#include "UObject/Package.h"
#include "SparseVolumeTexture/SparseVolumeTextureData.h"
#include "SparseVolumeTexture/SparseVolumeTextureUtility.h"
#include "SparseVolumeTexture/ISparseVolumeTextureStreamingManager.h"

#if WITH_EDITORONLY_DATA
#include "Misc/ScopedSlowTask.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#endif

#include "Serialization/LargeMemoryReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/BulkDataReader.h"
#include "Serialization/BulkDataWriter.h"
#include "Serialization/EditorBulkDataReader.h"
#include "Serialization/EditorBulkDataWriter.h"

#include "ContentStreaming.h"

#define LOCTEXT_NAMESPACE "USparseVolumeTexture"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTexture, Log, All);

////////////////////////////////////////////////////////////////////////////////////////////////

FArchive& operator<<(FArchive& Ar, UE::SVT::FMipLevelStreamingInfo& MipLevelStreamingInfo)
{
	Ar << MipLevelStreamingInfo.BulkOffset;
	Ar << MipLevelStreamingInfo.BulkSize;
	Ar << MipLevelStreamingInfo.PageTableOffset;
	Ar << MipLevelStreamingInfo.PageTableSize;
	Ar << MipLevelStreamingInfo.TileDataAOffset;
	Ar << MipLevelStreamingInfo.TileDataASize;
	Ar << MipLevelStreamingInfo.TileDataBOffset;
	Ar << MipLevelStreamingInfo.TileDataBSize;
	Ar << MipLevelStreamingInfo.NumPhysicalTiles;
	return Ar;
}

////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE
{
namespace SVT
{

static int32 ComputeNumMipLevels(const FIntVector3& InResolution)
{
	int32 Levels = 1;
	FIntVector3 Resolution = InResolution;
	while (Resolution.X > SPARSE_VOLUME_TILE_RES || Resolution.Y > SPARSE_VOLUME_TILE_RES || Resolution.Z > SPARSE_VOLUME_TILE_RES)
	{
		Resolution /= 2;
		++Levels;
	}
	return Levels;
};

static const FString& GetDerivedDataVersion()
{
	static FString CachedVersionString = TEXT("381AE2A9-A903-4C8F-8486-891E24D6FC71");	// Bump this if you want to ignore all cached data so far.
	return CachedVersionString;
}

FHeader::FHeader(const FIntVector3& AABBMin, const FIntVector3& AABBMax, EPixelFormat FormatA, EPixelFormat FormatB, const FVector4f& FallbackValueA, const FVector4f& FallbackValueB)
{
	VirtualVolumeAABBMin = AABBMin;
	VirtualVolumeAABBMax = AABBMax;
	VirtualVolumeResolution = VirtualVolumeAABBMax - VirtualVolumeAABBMin;

	PageTableVolumeAABBMin = VirtualVolumeAABBMin / SPARSE_VOLUME_TILE_RES;
	PageTableVolumeAABBMax = (VirtualVolumeAABBMax + FIntVector3(SPARSE_VOLUME_TILE_RES - 1)) / SPARSE_VOLUME_TILE_RES;
	PageTableVolumeResolution = PageTableVolumeAABBMax - PageTableVolumeAABBMin;

	// We need to ensure a power of two resolution for the page table in order to fit all mips of the page table into the physical mips of the texture resource.
	PageTableVolumeResolution.X = FMath::RoundUpToPowerOfTwo(PageTableVolumeResolution.X);
	PageTableVolumeResolution.Y = FMath::RoundUpToPowerOfTwo(PageTableVolumeResolution.Y);
	PageTableVolumeResolution.Z = FMath::RoundUpToPowerOfTwo(PageTableVolumeResolution.Z);
	PageTableVolumeAABBMax = PageTableVolumeAABBMin + PageTableVolumeResolution;

	AttributesFormats[0] = FormatA;
	AttributesFormats[1] = FormatB;

	FallbackValues[0] = FallbackValueA;
	FallbackValues[1] = FallbackValueB;
}

void FHeader::Serialize(FArchive& Ar)
{
	Ar << Version;

	if (Version == 0)
	{
		Ar << VirtualVolumeResolution;
		Ar << VirtualVolumeAABBMin;
		Ar << VirtualVolumeAABBMax;
		Ar << PageTableVolumeResolution;
		Ar << PageTableVolumeAABBMin;
		Ar << PageTableVolumeAABBMax;
		UE::SVT::Private::SerializeEnumAs<uint8>(Ar, AttributesFormats[0]);
		UE::SVT::Private::SerializeEnumAs<uint8>(Ar, AttributesFormats[1]);
		Ar << FallbackValues[0];
		Ar << FallbackValues[1];
	}
	else
	{
		// FHeader needs to account for new version
		check(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

void FResources::Serialize(FArchive& Ar, UObject* Owner, bool bCooked)
{
	// Note: this is all derived data, native versioning is not needed, but be sure to bump GetDerivedDataVersion() when modifying!
	FStripDataFlags StripFlags(Ar, 0);
	if (!StripFlags.IsDataStrippedForServer())
	{
		Header.Serialize(Ar);

		uint32 StoredResourceFlags;
		if (Ar.IsSaving() && bCooked)
		{
			// Disable DDC store when saving out a cooked build
			StoredResourceFlags = ResourceFlags & ~EResourceFlag_StreamingDataInDDC;
			Ar << StoredResourceFlags;
		}
		else
		{
			Ar << ResourceFlags;
			StoredResourceFlags = ResourceFlags;
		}

		Ar << MipLevelStreamingInfo;
		Ar << RootData;

		// StreamableMipLevels is only serialized in cooked builds and when caching to DDC failed in editor builds.
		// If the data was successfully cached to DDC, we just query it from DDC on the next run or recreate it if that failed.
		if (StoredResourceFlags & EResourceFlag_StreamingDataInDDC)
		{
#if !WITH_EDITORONLY_DATA
			checkf(false, TEXT("UE::SVT::FResources was serialized with EResourceFlag_StreamingDataInDDC in a cooked build!"));
#endif
		}
		else
		{
			StreamableMipLevels.Serialize(Ar, Owner, 0);
		}

#if WITH_EDITORONLY_DATA
		if (!bCooked)
		{
			SourceData.Serialize(Ar, Owner);
		}
#endif

#if !WITH_EDITORONLY_DATA
		check(!HasStreamingData() || StreamableMipLevels.GetBulkDataSize() > 0);
#endif
	}
}

bool FResources::HasStreamingData() const
{
	if (MipLevelStreamingInfo.Num() == 1)
	{
		// Root mip level does not stream
		return false;
	}
	else
	{
		// It is possible for multiple mip levels to exist but all these levels are empty, so we can't just check the number of mip levels
		bool bHasStreamingData = false;
		for (int32 MipLevelIndex = 0; MipLevelIndex < MipLevelStreamingInfo.Num() - 1; ++MipLevelIndex)
		{
			bHasStreamingData = bHasStreamingData || (MipLevelStreamingInfo[MipLevelIndex].BulkSize > 0);
		}
		return bHasStreamingData;
	}
}

#if WITH_EDITORONLY_DATA

void FResources::DropBulkData()
{
	if (HasStreamingData() && (ResourceFlags & EResourceFlag_StreamingDataInDDC))
	{
		StreamableMipLevels.RemoveBulkData();
	}
}

bool FResources::RebuildBulkDataFromCacheAsync(const UObject* Owner, bool& bFailed)
{
	bFailed = false;
	if (!HasStreamingData() || (ResourceFlags & EResourceFlag_StreamingDataInDDC) == 0u)
	{
		return true;
	}
	if (DDCRebuildState.load() == EDDCRebuildState::Initial)
	{
		if (StreamableMipLevels.IsBulkDataLoaded())
		{
			return true;
		}
		// Handle Initial state first so we can transition directly to Succeeded/Failed if the data was immediately available from the cache.
		check(!(*DDCRequestOwner).IsValid());
		BeginRebuildBulkDataFromCache(Owner);
	}
	switch (DDCRebuildState.load())
	{
	case EDDCRebuildState::Pending:
		return false;
	case EDDCRebuildState::Succeeded:
		check(StreamableMipLevels.GetBulkDataSize() > 0);
		EndRebuildBulkDataFromCache();
		return true;
	case EDDCRebuildState::Failed:
		bFailed = true;
		EndRebuildBulkDataFromCache();
		return true;
	default:
		check(false);
		return true;
	}
}

bool FResources::Build(USparseVolumeTextureFrame* Owner)
{
	// Check if the virtualized bulk data payload is available
	if (SourceData.HasPayloadData())
	{
		UE::Serialization::FEditorBulkDataReader SourceDataReader(SourceData);
		FTextureData SourceTextureData;
		SourceTextureData.Serialize(SourceDataReader);

		FTextureDataAddressingInfo AddressingInfo{};
		AddressingInfo.VolumeResolution = Owner->GetVolumeResolution();
		AddressingInfo.AddressX = Owner->GetTextureAddressX();
		AddressingInfo.AddressY = Owner->GetTextureAddressY();
		AddressingInfo.AddressZ = Owner->GetTextureAddressZ();

		const int32 NumMipLevelsRequested = -1; // generate entire mip chain
		const bool bMoveMip0FromSource = true; // we have no need to keep SourceTextureData around
		FTextureData DerivedTextureData;
		if (!SourceTextureData.BuildDerivedData(AddressingInfo, NumMipLevelsRequested, bMoveMip0FromSource, DerivedTextureData))
		{
			return false;
		}

		// Now unload the source data
		SourceData.UnloadData();

		const int32 NumMipLevels = DerivedTextureData.MipMaps.Num();

		Header = DerivedTextureData.Header;
		RootData.Reset();
		MipLevelStreamingInfo.SetNumZeroed(NumMipLevels);
		ResourceFlags = 0;
		ResourceName.Reset();
		DDCKeyHash.Reset();
		DDCRawHash.Reset();
		DDCRebuildState.store(EDDCRebuildState::Initial);

		// Stores page table into BulkData as two consecutive arrays of packed page coordinates and linear indices into the physical tiles array.
		// Returns number of written/non-zero page table entries
		auto CompressPageTable = [](const TArray<uint32>& PageTable, const FIntVector3& Resolution, TArray<uint8>& BulkData) -> int32
		{
			int32 NumNonZeroEntries = 0;
			for (uint32 Entry : PageTable)
			{
				if (Entry)
				{
					++NumNonZeroEntries;
				}
			}

			const int32 BaseOffset = BulkData.Num();
			BulkData.SetNum(BulkData.Num() + NumNonZeroEntries * 2 * sizeof(uint32));
			uint32* PackedCoords = reinterpret_cast<uint32*>(BulkData.GetData() + BaseOffset);
			uint32* PageEntries = PackedCoords + NumNonZeroEntries;
			
			int32 NumWrittenEntries = 0;
			for (int32 Z = 0; Z < Resolution.Z; ++Z)
			{
				for (int32 Y = 0; Y < Resolution.Y; ++Y)
				{
					for (int32 X = 0; X < Resolution.X; ++X)
					{
						const int32 LinearCoord = (Z * Resolution.Y * Resolution.X) + (Y * Resolution.X) + X;
						const uint32 Entry = PageTable[LinearCoord];
						if (Entry)
						{
							const uint32 Packed = (X & 0x7FFu) | ((Y & 0x7FFu) << 11u) | ((Z & 0x3FFu) << 22u);
							PackedCoords[NumWrittenEntries] = Packed;
							PageEntries[NumWrittenEntries] = Entry - 1;
							++NumWrittenEntries;
						}
					}
				}
			}

			return NumNonZeroEntries;
		};

		TArray<uint8> StreamableBulkData;
		for (int32 MipLevelIdx = 0; MipLevelIdx < DerivedTextureData.MipMaps.Num(); ++MipLevelIdx)
		{
			const FTextureData::FMipMap& Mip = DerivedTextureData.MipMaps[MipLevelIdx];
			const bool bIsRootMipLevel = (MipLevelIdx == (DerivedTextureData.MipMaps.Num() - 1));
			TArray<uint8>& BulkData = bIsRootMipLevel ? RootData : StreamableBulkData;

			FIntVector3 MipPageTableResolution = DerivedTextureData.Header.PageTableVolumeResolution >> MipLevelIdx;
			MipPageTableResolution = FIntVector3(FMath::Max(1, MipPageTableResolution.X), FMath::Max(1, MipPageTableResolution.Y), FMath::Max(1, MipPageTableResolution.Z));

			FMipLevelStreamingInfo& MipStreamingInfo = MipLevelStreamingInfo[MipLevelIdx];
			MipStreamingInfo.BulkOffset = BulkData.Num();

			MipStreamingInfo.PageTableOffset = BulkData.Num() - MipStreamingInfo.BulkOffset;
			const int32 NumNonZeroPageTableEntries = CompressPageTable(Mip.PageTable, MipPageTableResolution, BulkData);
			MipStreamingInfo.PageTableSize = NumNonZeroPageTableEntries * 2 * sizeof(uint32);
			
			MipStreamingInfo.TileDataAOffset = BulkData.Num() - MipStreamingInfo.BulkOffset;
			MipStreamingInfo.TileDataASize = Mip.PhysicalTileDataA.Num();
			BulkData.Append(Mip.PhysicalTileDataA.GetData(), Mip.PhysicalTileDataA.Num());

			MipStreamingInfo.TileDataBOffset = BulkData.Num() - MipStreamingInfo.BulkOffset;
			MipStreamingInfo.TileDataBSize = Mip.PhysicalTileDataB.Num();
			BulkData.Append(Mip.PhysicalTileDataB.GetData(), Mip.PhysicalTileDataB.Num());

			MipStreamingInfo.BulkSize = BulkData.Num() - MipStreamingInfo.BulkOffset;
			MipStreamingInfo.NumPhysicalTiles = Mip.NumPhysicalTiles;
		}

		// Store StreamableMipLevels
		{
			StreamableMipLevels.Lock(LOCK_READ_WRITE);
			uint8* Ptr = (uint8*)StreamableMipLevels.Realloc(StreamableBulkData.Num());
			FMemory::Memcpy(Ptr, StreamableBulkData.GetData(), StreamableBulkData.Num());
			StreamableMipLevels.Unlock();
			StreamableMipLevels.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
		}

		return true;
	}
	return false;
}

void FResources::Cache(USparseVolumeTextureFrame* Owner)
{
	if (Owner->GetPackage()->bIsCookedForEditor)
	{
		// Don't cache for cooked packages
		return;
	}

	using namespace UE::DerivedData;

	static const FValueId SVTDataId = FValueId::FromName("SparseVolumeTextureData");
	static const FValueId SVTStreamingDataId = FValueId::FromName("SparseVolumeTextureStreamingData");
	const FString KeySuffix = SourceData.GetIdentifier().ToString() + FString::Format(TEXT("{0}_{1}_{2}_{3}"), { Owner->GetNumMipLevels(), Owner->GetTextureAddressX(), Owner->GetTextureAddressY(), Owner->GetTextureAddressZ() });
	FString DerivedDataKey = FDerivedDataCacheInterface::BuildCacheKey(TEXT("SPARSEVOLUMETEXTURE"), *GetDerivedDataVersion(), *KeySuffix);

	FCacheKey CacheKey;
	CacheKey.Bucket = FCacheBucket(TEXT("SparseVolumeTexture"));
	CacheKey.Hash = FIoHash::HashBuffer(MakeMemoryView(FTCHARToUTF8(DerivedDataKey)));

	// Check if the data already exists in DDC
	FSharedBuffer ResourcesDataBuffer;
	FIoHash SVTStreamingDataHash;
	{
		FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::Default | ECachePolicy::KeepAlive);
		PolicyBuilder.AddValuePolicy(SVTStreamingDataId, ECachePolicy::Default | ECachePolicy::SkipData);

		FCacheGetRequest Request;
		Request.Name = Owner->GetPathName();
		Request.Key = CacheKey;
		Request.Policy = PolicyBuilder.Build();

		FRequestOwner RequestOwner(EPriority::Blocking);
		GetCache().Get(MakeArrayView(&Request, 1), RequestOwner,
			[&ResourcesDataBuffer, &SVTStreamingDataHash](FCacheGetResponse&& Response)
			{
				if (Response.Status == EStatus::Ok)
				{
					const FCompressedBuffer& CompressedBuffer = Response.Record.GetValue(SVTDataId).GetData();
					ResourcesDataBuffer = CompressedBuffer.Decompress();

					SVTStreamingDataHash = Response.Record.GetValue(SVTStreamingDataId).GetRawHash();
				}
			});
		RequestOwner.Wait();
	}

	if (!ResourcesDataBuffer.IsNull())
	{
		// Found it!
		// We can serialize the data from the DDC buffer and are done.
		FMemoryReaderView Ar(ResourcesDataBuffer.GetView(), /*bIsPersistent=*/ true);
		Serialize(Ar, Owner, /*bCooked=*/ false);

		check(StreamableMipLevels.GetBulkDataSize() == 0);
		if (ResourceFlags & EResourceFlag_StreamingDataInDDC)
		{
			DDCKeyHash = CacheKey.Hash;
			DDCRawHash = SVTStreamingDataHash;
		}
	}
	else
	{
		// DDC lookup failed! Build the data again.
		const bool bBuiltSuccessfully = Build(Owner);
		check(bBuiltSuccessfully);

		FCacheRecordBuilder RecordBuilder(CacheKey);
		if (!MipLevelStreamingInfo.IsEmpty())
		{
			if (HasStreamingData())
			{
				FByteBulkData& BulkData = StreamableMipLevels;

				FValue Value = FValue::Compress(FSharedBuffer::MakeView(BulkData.LockReadOnly(), BulkData.GetBulkDataSize()));
				RecordBuilder.AddValue(SVTStreamingDataId, Value);
				BulkData.Unlock();
				ResourceFlags |= EResourceFlag_StreamingDataInDDC;
				DDCKeyHash = CacheKey.Hash;
				DDCRawHash = Value.GetRawHash();
			}
		}

		// Serialize to a buffer and store into DDC.
		FLargeMemoryWriter Ar(0, /*bIsPersistent=*/ true);
		Serialize(Ar, Owner, /*bCooked=*/ false);

		bool bSavedToDDC = false;
		{
			FValue Value = FValue::Compress(FSharedBuffer::MakeView(Ar.GetData(), Ar.TotalSize()));
			RecordBuilder.AddValue(SVTDataId, Value);

			FRequestOwner RequestOwner(UE::DerivedData::EPriority::Blocking);
			const FCachePutRequest PutRequest = { FSharedString(Owner->GetPathName()), RecordBuilder.Build(), ECachePolicy::Default | ECachePolicy::KeepAlive };
			GetCache().Put(MakeArrayView(&PutRequest, 1), RequestOwner,
				[&bSavedToDDC](FCachePutResponse&& Response)
				{
					if (Response.Status == EStatus::Ok)
					{
						bSavedToDDC = true;
					}
				});

			RequestOwner.Wait();

			if (bSavedToDDC && HasStreamingData())
			{
				// Drop streaming data from memory when it has been successfully committed to DDC
				DropBulkData();
			}
		}

		if (HasStreamingData() && !bSavedToDDC)
		{
			// Streaming data was not pushed to DDC. Disable DDC streaming flag.
			check(StreamableMipLevels.GetBulkDataSize() > 0);
			ResourceFlags &= ~EResourceFlag_StreamingDataInDDC;
		}
	}
}

#endif // WITH_EDITORONLY_DATA

#if WITH_EDITORONLY_DATA

void FResources::BeginRebuildBulkDataFromCache(const UObject* Owner)
{
	check(DDCRebuildState.load() == EDDCRebuildState::Initial);
	if (!HasStreamingData() || (ResourceFlags & EResourceFlag_StreamingDataInDDC) == 0u)
	{
		return;
	}
	using namespace UE::DerivedData;
	FCacheKey Key;
	Key.Bucket = FCacheBucket(TEXT("SparseVolumeTexture"));
	Key.Hash = DDCKeyHash;
	check(!DDCKeyHash.IsZero());
	FCacheGetChunkRequest Request;
	Request.Name = Owner->GetPathName();
	Request.Id = FValueId::FromName("SparseVolumeTextureStreamingData");
	Request.Key = Key;
	Request.RawHash = DDCRawHash;
	check(!DDCRawHash.IsZero());
	FSharedBuffer SharedBuffer;
	*DDCRequestOwner = MakePimpl<FRequestOwner>(EPriority::Normal);
	DDCRebuildState.store(EDDCRebuildState::Pending);
	GetCache().GetChunks(MakeArrayView(&Request, 1), **DDCRequestOwner,
		[this](FCacheGetChunkResponse&& Response)
		{
			if (Response.Status == EStatus::Ok)
			{
				StreamableMipLevels.Lock(LOCK_READ_WRITE);
				uint8* Ptr = (uint8*)StreamableMipLevels.Realloc(Response.RawData.GetSize());
				FMemory::Memcpy(Ptr, Response.RawData.GetData(), Response.RawData.GetSize());
				StreamableMipLevels.Unlock();
				StreamableMipLevels.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
				DDCRebuildState.store(EDDCRebuildState::Succeeded);
			}
			else
			{
				DDCRebuildState.store(EDDCRebuildState::Failed);
			}
		});
}

void FResources::EndRebuildBulkDataFromCache()
{
	if (*DDCRequestOwner)
	{
		(*DDCRequestOwner)->Wait();
		(*DDCRequestOwner).Reset();
	}
	DDCRebuildState.store(EDDCRebuildState::Initial);
}

#endif // WITH_EDITORONLY_DATA

////////////////////////////////////////////////////////////////////////////////////////////////

void FTextureRenderResources::GetPackedUniforms(FUintVector4& OutPacked0, FUintVector4& OutPacked1) const
{
	check(IsInParallelRenderingThread());

	auto AsUint = [](float X)
	{
		union { float F; uint32 U; } FU = { X };
		return FU.U;
	};

	const FIntVector3 PageTableOffset = Header.PageTableVolumeAABBMin;
	const FVector3f TileDataTexelSize = FVector3f(
		1.0f / TileDataTextureResolution.X,
		1.0f / TileDataTextureResolution.Y,
		1.0f / TileDataTextureResolution.Z);
	const FVector3f VolumePageResolution = FVector3f(GlobalVolumeResolution) / SPARSE_VOLUME_TILE_RES;

	OutPacked0.X = AsUint(VolumePageResolution.X);
	OutPacked0.Y = AsUint(VolumePageResolution.Y);
	OutPacked0.Z = AsUint(VolumePageResolution.Z);
	OutPacked0.W = (PageTableOffset.X & 0x7FFu) | ((PageTableOffset.Y & 0x7FFu) << 11u) | ((PageTableOffset.Z & 0x3FFu) << 22u);
	OutPacked1.X = AsUint(TileDataTexelSize.X);
	OutPacked1.Y = AsUint(TileDataTexelSize.Y);
	OutPacked1.Z = AsUint(TileDataTexelSize.Z);
	OutPacked1.W = 0;
	OutPacked1.W |= (uint32)((FrameIndex & 0xFFFF) << 0);
	OutPacked1.W |= (uint32)(((NumLogicalMipLevels - 1) & 0xFFFF) << 16);
}

void FTextureRenderResources::SetGlobalVolumeResolution_GameThread(const FIntVector3& InGlobalVolumeResolution)
{
	ENQUEUE_RENDER_COMMAND(FTextureRenderResources_UpdateGlobalVolumeResolution)(
		[this, InGlobalVolumeResolution](FRHICommandListImmediate& RHICmdList)
		{
			GlobalVolumeResolution = InGlobalVolumeResolution;
		});
}

void FTextureRenderResources::InitRHI()
{
	PageTableTextureReferenceRHI = RHICreateTextureReference(GBlackUintVolumeTexture->TextureRHI);
	PhysicalTileDataATextureReferenceRHI = RHICreateTextureReference(GBlackVolumeTexture->TextureRHI);
	PhysicalTileDataBTextureReferenceRHI = RHICreateTextureReference(GBlackVolumeTexture->TextureRHI);
}

void FTextureRenderResources::ReleaseRHI()
{
	PageTableTextureReferenceRHI.SafeRelease();
	PhysicalTileDataATextureReferenceRHI.SafeRelease();
	PhysicalTileDataBTextureReferenceRHI.SafeRelease();
	StreamingInfoBufferSRVRHI.SafeRelease();
}

}
}

////////////////////////////////////////////////////////////////////////////////////////////////

USparseVolumeTexture::USparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UE::Shader::EValueType USparseVolumeTexture::GetUniformParameterType(int32 Index)
{
	switch (Index)
	{
	case ESparseVolumeTexture_TileSize:				return UE::Shader::EValueType::Float1;
	case ESparseVolumeTexture_PageTableSize:		return UE::Shader::EValueType::Float3;
	case ESparseVolumeTexture_UVScale:				return UE::Shader::EValueType::Float3;
	case ESparseVolumeTexture_UVBias:				return UE::Shader::EValueType::Float3;
	default:
		break;
	}
	checkNoEntry();
	return UE::Shader::EValueType::Float4;
}

#if WITH_EDITOR
void USparseVolumeTexture::NotifyMaterials(const ENotifyMaterialsEffectOnShaders EffectOnShaders)
{
	// Create a material update context to safely update materials.
	{
		FMaterialUpdateContext UpdateContext;

		// Notify any material that uses this texture
		TSet<UMaterial*> BaseMaterialsThatUseThisTexture;
		for (TObjectIterator<UMaterialInterface> It; It; ++It)
		{
			UMaterialInterface* MaterialInterface = *It;
			if (!FPlatformProperties::IsServerOnly() && MaterialInterface->GetReferencedTextures().Contains(this))
			{
				UpdateContext.AddMaterialInterface(MaterialInterface);
				// This is a bit tricky. We want to make sure all materials using this texture are
				// updated. Materials are always updated. Material instances may also have to be
				// updated and if they have static permutations their children must be updated
				// whether they use the texture or not! The safe thing to do is to add the instance's
				// base material to the update context causing all materials in the tree to update.
				BaseMaterialsThatUseThisTexture.Add(MaterialInterface->GetMaterial());
			}
		}

		// Go ahead and update any base materials that need to be.
		if (EffectOnShaders == ENotifyMaterialsEffectOnShaders::Default)
		{
			for (TSet<UMaterial*>::TConstIterator It(BaseMaterialsThatUseThisTexture); It; ++It)
			{
				(*It)->PostEditChange();
			}
		}
		else
		{
			FPropertyChangedEvent EmptyPropertyUpdateStruct(nullptr);
			for (TSet<UMaterial*>::TConstIterator It(BaseMaterialsThatUseThisTexture); It; ++It)
			{
				(*It)->PostEditChangePropertyInternal(EmptyPropertyUpdateStruct, UMaterial::EPostEditChangeEffectOnShaders::DoesNotInvalidate);
			}
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////

USparseVolumeTextureFrame::USparseVolumeTextureFrame(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USparseVolumeTextureFrame* USparseVolumeTextureFrame::GetFrameAndIssueStreamingRequest(USparseVolumeTexture* SparseVolumeTexture, float FrameIndex, int32 MipLevel, bool bBlocking)
{
	if (UStreamableSparseVolumeTexture* StreamableSVT = Cast<UStreamableSparseVolumeTexture>(SparseVolumeTexture))
	{
		UE::SVT::GetStreamingManager().Request_GameThread(StreamableSVT, FrameIndex, MipLevel, bBlocking);
		return StreamableSVT->GetFrame(static_cast<int32>(FrameIndex));
	}
	return nullptr;
}

bool USparseVolumeTextureFrame::Initialize(USparseVolumeTexture* InOwner, int32 InFrameIndex, UE::SVT::FTextureData& UncookedFrame)
{
#if WITH_EDITORONLY_DATA
	Owner = InOwner;
	FrameIndex = InFrameIndex;
	{
		UE::Serialization::FEditorBulkDataWriter SourceDataArchiveWriter(Resources.SourceData);
		UncookedFrame.Serialize(SourceDataArchiveWriter);
	}

	return true;
#else
	return false;
#endif
}

bool USparseVolumeTextureFrame::CreateTextureRenderResources()
{
	if (!TextureRenderResources)
	{
		TextureRenderResources = new UE::SVT::FTextureRenderResources();
		TextureRenderResources->SetGlobalVolumeResolution_GameThread(Owner->GetVolumeResolution());
		BeginInitResource(TextureRenderResources);

		return true;
	}
	return false;
}

void USparseVolumeTextureFrame::PostLoad()
{
	Super::PostLoad();

	CreateTextureRenderResources();
}

void USparseVolumeTextureFrame::FinishDestroy()
{
	Super::FinishDestroy();
}

void USparseVolumeTextureFrame::BeginDestroy()
{
	// Ensure that the streamable SVT has been removed from the streaming manager
	if (IsValid(Owner))
	{
		UE::SVT::GetStreamingManager().Remove_GameThread(CastChecked<UStreamableSparseVolumeTexture>(Owner));
	}
	
	if (TextureRenderResources)
	{
		ENQUEUE_RENDER_COMMAND(USparseVolumeTextureFrame_DeleteTextureRenderResources)(
			[Resources = TextureRenderResources](FRHICommandListImmediate& RHICmdList)
			{
				Resources->ReleaseResource();
				delete Resources;
			});
		TextureRenderResources = nullptr;
	}

	Super::BeginDestroy();
}

void USparseVolumeTextureFrame::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	FStripDataFlags StripFlags(Ar);
	
	bool bCooked = Ar.IsCooking();
	Ar << bCooked;
	Ar << FrameIndex;
	Resources.Serialize(Ar, this, bCooked);
}

void USparseVolumeTextureFrame::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
}

#if WITH_EDITOR

void USparseVolumeTextureFrame::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
}

bool USparseVolumeTextureFrame::IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform)
{
	bool bFailed = false;
	if (!Resources.RebuildBulkDataFromCacheAsync(this, bFailed))
	{
		return false;
	}

	if (bFailed)
	{
		UE_LOG(LogSparseVolumeTexture, Log, TEXT("Failed to recover SparseVolumeTexture streaming from DDC for '%s' Frame %i. Rebuilding and retrying."), *Owner->GetPathName(), FrameIndex);

		Resources.Cache(this);
		return false;
	}

	return true;
}

void USparseVolumeTextureFrame::WillNeverCacheCookedPlatformDataAgain()
{
}

void USparseVolumeTextureFrame::ClearCachedCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	Resources.DropBulkData();
}

void USparseVolumeTextureFrame::ClearAllCachedCookedPlatformData()
{
	Resources.DropBulkData();
}

#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
void USparseVolumeTextureFrame::Cache()
{
	Resources.Cache(this);
	CreateTextureRenderResources();
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////

UStreamableSparseVolumeTexture::UStreamableSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UStreamableSparseVolumeTexture::BeginInitialize(int32 NumExpectedFrames)
{
#if WITH_EDITORONLY_DATA
	if (InitState != EInitState::Uninitialized)
	{
		UE_LOG(LogSparseVolumeTexture, Error, TEXT("Tried to call UStreamableSparseVolumeTexture::BeginInitialize() while not in the Uninitialized init state."));
		return false;
	}

	check(Frames.IsEmpty());
	Frames.Empty(NumExpectedFrames);
	VolumeBoundsMin = FIntVector(INT32_MAX, INT32_MAX, INT32_MAX);
	VolumeBoundsMax = FIntVector(INT32_MIN, INT32_MIN, INT32_MIN);
	check(FormatA == PF_Unknown);
	check(FormatB == PF_Unknown);

	InitState = EInitState::Pending;

	return true;
#else
	return false;
#endif
}

bool UStreamableSparseVolumeTexture::AppendFrame(UE::SVT::FTextureData& UncookedFrame)
{
#if WITH_EDITORONLY_DATA
	if (InitState != EInitState::Pending)
	{
		UE_LOG(LogSparseVolumeTexture, Error, TEXT("Tried to call UStreamableSparseVolumeTexture::AppendFrame() while not in the Pending init state."));
		return false;
	}

	// The the minimum of the union of all frame AABBs should ideally be at (0, 0, 0), but it should also be fine if it is greater than that.
	// A mimimum of less than (0, 0, 0) is not permitted.
	if (UncookedFrame.Header.VirtualVolumeAABBMin.X < 0 || UncookedFrame.Header.VirtualVolumeAABBMin.Y < 0 || UncookedFrame.Header.VirtualVolumeAABBMin.Z < 0)
	{
		UE_LOG(LogSparseVolumeTexture, Error, TEXT("Tried to add a frame to a SparseVolumeTexture with a VirtualVolumeAABBMin < 0 (%i, %i, %i)"),
			VolumeBoundsMin.X, VolumeBoundsMin.Y, VolumeBoundsMin.Z);
		return false;
	}

	// SVT_TODO: Valide formats against list of supported formats
	if (Frames.IsEmpty())
	{
		FormatA = UncookedFrame.Header.AttributesFormats[0];
		FormatB = UncookedFrame.Header.AttributesFormats[1];
		FallbackValueA = UncookedFrame.Header.FallbackValues[0];
		FallbackValueB = UncookedFrame.Header.FallbackValues[1];
	}
	else
	{
		if (UncookedFrame.Header.AttributesFormats[0] != FormatA || UncookedFrame.Header.AttributesFormats[1] != FormatB)
		{
			UE_LOG(LogSparseVolumeTexture, Error, TEXT("Tried to add a frame to a SparseVolumeTexture without matching formats! Expected: (%i, %i), Actual: (%i %i)"),
				(int)FormatA.GetIntValue(), (int)FormatB.GetIntValue(), (int)UncookedFrame.Header.AttributesFormats[0], (int)UncookedFrame.Header.AttributesFormats[1]);
			return false;
		}
		if (UncookedFrame.Header.FallbackValues[0] != FallbackValueA || UncookedFrame.Header.FallbackValues[1] != FallbackValueB)
		{
			UE_LOG(LogSparseVolumeTexture, Error, TEXT("Tried to add a frame to a SparseVolumeTexture without matching fallback/null tile values!"));
			return false;
		}
	}

	// Compute union of all frame AABBs
	VolumeBoundsMin.X = FMath::Min(VolumeBoundsMin.X, UncookedFrame.Header.VirtualVolumeAABBMin.X);
	VolumeBoundsMin.Y = FMath::Min(VolumeBoundsMin.Y, UncookedFrame.Header.VirtualVolumeAABBMin.Y);
	VolumeBoundsMin.Z = FMath::Min(VolumeBoundsMin.Z, UncookedFrame.Header.VirtualVolumeAABBMin.Z);
	VolumeBoundsMax.X = FMath::Max(VolumeBoundsMax.X, UncookedFrame.Header.VirtualVolumeAABBMax.X);
	VolumeBoundsMax.Y = FMath::Max(VolumeBoundsMax.Y, UncookedFrame.Header.VirtualVolumeAABBMax.Y);
	VolumeBoundsMax.Z = FMath::Max(VolumeBoundsMax.Z, UncookedFrame.Header.VirtualVolumeAABBMax.Z);

	VolumeResolution = VolumeBoundsMax;

	USparseVolumeTextureFrame* Frame = NewObject<USparseVolumeTextureFrame>(this);
	if (Frame->Initialize(this, Frames.Num(), UncookedFrame))
	{
		Frames.Add(Frame);
		return true;
	}
	return false;
	
#else
	return false;
#endif
}

bool UStreamableSparseVolumeTexture::EndInitialize(int32 InNumMipLevels)
{
#if WITH_EDITORONLY_DATA
	if (InitState != EInitState::Pending)
	{
		UE_LOG(LogSparseVolumeTexture, Error, TEXT("Tried to call UStreamableSparseVolumeTexture::EndInitialize() while not in the Pending init state."));
		return false;
	}

	// Ensure that at least one frame of data exists
	if (Frames.IsEmpty())
	{
		UE_LOG(LogSparseVolumeTexture, Warning, TEXT("SVT has zero frames! Adding a dummy frame. SVT: %s"), *GetName());
		UE::SVT::FTextureData DummyFrame;
		DummyFrame.CreateDefault();
		AppendFrame(DummyFrame);
	}

	check(VolumeResolution.X > 0 && VolumeResolution.Y > 0 && VolumeResolution.Z > 0);
	check(VolumeBoundsMin.X >= 0 && VolumeBoundsMin.Y >= 0 && VolumeBoundsMin.Z >= 0);
	check(FormatA != PF_Unknown || FormatB != PF_Unknown);

	if (VolumeBoundsMin.X > 0 || VolumeBoundsMin.Y > 0 || VolumeBoundsMin.Z > 0)
	{
		UE_LOG(LogSparseVolumeTexture, Warning, TEXT("Initialized a SparseVolumeTexture with a VirtualVolumeAABBMin > 0 (%i, %i, %i). This wastes memory"),
			VolumeBoundsMin.X, VolumeBoundsMin.Y, VolumeBoundsMin.Z);
	}

	const int32 NumMipLevelsFullMipChain = UE::SVT::ComputeNumMipLevels(VolumeResolution);
	check(NumMipLevelsFullMipChain > 0);

	NumMipLevels = (InNumMipLevels <= INDEX_NONE) ? NumMipLevelsFullMipChain : FMath::Clamp(InNumMipLevels, 1, NumMipLevelsFullMipChain);

	for (USparseVolumeTextureFrame* Frame : Frames)
	{
		Frame->PostLoad();
	}

	InitState = EInitState::Done;

	return true;
#else
	return false;
#endif
}

bool UStreamableSparseVolumeTexture::Initialize(const TArrayView<UE::SVT::FTextureData>& InUncookedData, int32 InNumMipLevels)
{
	if (InUncookedData.IsEmpty())
	{
		UE_LOG(LogSparseVolumeTexture, Error, TEXT("Tried to initialize a SparseVolumeTexture with no frames"));
		return false;
	}

	if (!BeginInitialize(InUncookedData.Num()))
	{
		return false;
	}
	for (UE::SVT::FTextureData& UncookedFrame : InUncookedData)
	{
		if (!AppendFrame(UncookedFrame))
		{
			return false;
		}
	}
	if (!EndInitialize(InNumMipLevels))
	{
		return false;
	}

	return true;
}

void UStreamableSparseVolumeTexture::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	RecacheFrames();
#else
	for (USparseVolumeTextureFrame* Frame : Frames)
	{
		Frame->CreateTextureRenderResources();
	}
	UE::SVT::GetStreamingManager().Add_GameThread(this); // RecacheFrames() handles this in editor builds
#endif
}

void UStreamableSparseVolumeTexture::FinishDestroy()
{
	Super::FinishDestroy();
}

void UStreamableSparseVolumeTexture::BeginDestroy()
{
	Super::BeginDestroy();
	UE::SVT::GetStreamingManager().Remove_GameThread(this);
}

void UStreamableSparseVolumeTexture::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// To ensure that we keep the same binary format between builds with and without editor-only data, we serialize dummy data if the editor-only data is stripped.
	// SVT_TODO: Is there a cleaner way of doing this?
	FIntVector* VolumeBoundsMinPtr = nullptr;
	FIntVector* VolumeBoundsMaxPtr = nullptr;
	EInitState* InitStatePtr = nullptr;

#if WITH_EDITORONLY_DATA
	// Check that we are not trying to cook unitialized data!
	check(!Ar.IsCooking() || InitState == EInitState::Done);

	VolumeBoundsMinPtr = &VolumeBoundsMin;
	VolumeBoundsMaxPtr = &VolumeBoundsMax;
	InitStatePtr = &InitState;
#else
	FIntVector VolumeBoundsMinDummy{};
	FIntVector VolumeBoundsMaxDummy{};
	EInitState InitStateDummy{};
	VolumeBoundsMinPtr = &VolumeBoundsMinDummy;
	VolumeBoundsMaxPtr = &VolumeBoundsMaxDummy;
	InitStatePtr = &InitStateDummy;
#endif

	UE::SVT::Private::SerializeEnumAs<uint8>(Ar, *InitStatePtr);
	Ar << *VolumeBoundsMinPtr;
	Ar << *VolumeBoundsMaxPtr;
}

#if WITH_EDITOR
void UStreamableSparseVolumeTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UStreamableSparseVolumeTexture, AddressX)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UStreamableSparseVolumeTexture, AddressY)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UStreamableSparseVolumeTexture, AddressZ))
	{
		// SVT need to recompile shaders when address mode changes
		NotifyMaterials();
		for (USparseVolumeTextureFrame* Frame : Frames)
		{
			Frame->NotifyMaterials();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	RecacheFrames();
}
#endif // WITH_EDITOR

void UStreamableSparseVolumeTexture::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	SIZE_T SizeCPU = sizeof(*this) - sizeof(Super);
	SIZE_T SizeGPU = 0;
	SizeCPU += Frames.GetAllocatedSize();
	for (USparseVolumeTextureFrame* Frame : Frames)
	{
		Frame->GetResourceSizeEx(CumulativeResourceSize);
	}
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(SizeCPU);
	CumulativeResourceSize.AddDedicatedVideoMemoryBytes(SizeGPU);
}

#if WITH_EDITORONLY_DATA
void UStreamableSparseVolumeTexture::RecacheFrames()
{
	if (InitState != EInitState::Done)
	{
		UE_LOG(LogSparseVolumeTexture, Warning, TEXT("Tried to cache derived data of an uninitialized SVT: %s"), *GetName());
		return;
	}

	// SVT_TODO: This shows the user that something is actually happening and the editor did not freeze. The cancel button is currently ignored.
	FScopedSlowTask RecacheTask(static_cast<float>(Frames.Num() + 2), LOCTEXT("SparseVolumeTextureCacheFrames", "Caching SparseVolumeTexture frames in Derived Data Cache"));
	RecacheTask.MakeDialog(true);

	UE::SVT::GetStreamingManager().Remove_GameThread(this);
	RecacheTask.EnterProgressFrame(1.0f);

	for (USparseVolumeTextureFrame* Frame : Frames)
	{
		RecacheTask.EnterProgressFrame(1.0f);
		Frame->Cache();
	}

	RecacheTask.EnterProgressFrame(1.0f);
	UE::SVT::GetStreamingManager().Add_GameThread(this);
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////

UStaticSparseVolumeTexture::UStaticSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UStaticSparseVolumeTexture::AppendFrame(UE::SVT::FTextureData& UncookedFrame)
{
	if (!Frames.IsEmpty())
	{
		UE_LOG(LogSparseVolumeTexture, Error, TEXT("Tried to initialize a UStaticSparseVolumeTexture with more than 1 frame"));
		return false;
	}
	return Super::AppendFrame(UncookedFrame);
}

////////////////////////////////////////////////////////////////////////////////////////////////

UAnimatedSparseVolumeTexture::UAnimatedSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////

UAnimatedSparseVolumeTextureController::UAnimatedSparseVolumeTextureController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAnimatedSparseVolumeTextureController::Play()
{
	bIsPlaying = true;
}

void UAnimatedSparseVolumeTextureController::Pause()
{
	bIsPlaying = false;
}

void UAnimatedSparseVolumeTextureController::Stop()
{
	if (bIsPlaying)
	{
		bIsPlaying = false;
		Time = 0.0f;
	}
}

void UAnimatedSparseVolumeTextureController::Update(float DeltaTime)
{
	if (!IsValid(SparseVolumeTexture) || !bIsPlaying)
	{
		return;
	}

	// Update animation time
	const float AnimationDuration = GetDuration();
	Time = FMath::Fmod(Time + DeltaTime, AnimationDuration + UE_SMALL_NUMBER);
}

float UAnimatedSparseVolumeTextureController::GetFractionalFrameIndex()
{
	if (!IsValid(SparseVolumeTexture))
	{
		return 0.0f;
	}

	const int32 FrameCount = SparseVolumeTexture->GetNumFrames();
	const float FrameIndexF = FMath::Fmod(Time * FrameRate, (float)FrameCount);
	return FrameIndexF;
}

USparseVolumeTextureFrame* UAnimatedSparseVolumeTextureController::GetFrameByIndex(int32 FrameIndex)
{
	if (!IsValid(SparseVolumeTexture))
	{
		return nullptr;
	}

	return USparseVolumeTextureFrame::GetFrameAndIssueStreamingRequest(SparseVolumeTexture, FrameIndex, MipLevel);
}

USparseVolumeTextureFrame* UAnimatedSparseVolumeTextureController::GetCurrentFrame()
{
	if (!IsValid(SparseVolumeTexture))
	{
		return nullptr;
	}

	// Compute (fractional) index of frame to sample
	const float FrameIndexF = GetFractionalFrameIndex();

	return USparseVolumeTextureFrame::GetFrameAndIssueStreamingRequest(SparseVolumeTexture, FrameIndexF, MipLevel);
}

void UAnimatedSparseVolumeTextureController::GetCurrentFramesForInterpolation(USparseVolumeTextureFrame*& Frame0, USparseVolumeTextureFrame*& Frame1, float& LerpAlpha)
{
	if (!IsValid(SparseVolumeTexture))
	{
		return;
	}

	// Compute (fractional) index of frame to sample
	const float FrameIndexF = GetFractionalFrameIndex();
	const int32 FrameIndex = (int32)FrameIndexF;
	LerpAlpha = FMath::Frac(FrameIndexF);

	Frame0 = USparseVolumeTextureFrame::GetFrameAndIssueStreamingRequest(SparseVolumeTexture, FrameIndexF, MipLevel);
	Frame1 = USparseVolumeTextureFrame::GetFrameAndIssueStreamingRequest(SparseVolumeTexture, (FrameIndex + 1) % SparseVolumeTexture->GetNumFrames(), MipLevel);
}

float UAnimatedSparseVolumeTextureController::GetDuration()
{
	if (!IsValid(SparseVolumeTexture))
	{
		return 0.0f;
	}

	const int32 FrameCount = SparseVolumeTexture->GetNumFrames();
	const float AnimationDuration = FrameCount / (FrameRate + UE_SMALL_NUMBER);
	return AnimationDuration;
}

////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
