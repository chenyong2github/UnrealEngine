// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SparseVolumeTexture.cpp: SparseVolumeTexture implementation.
=============================================================================*/

#include "SparseVolumeTexture/SparseVolumeTexture.h"

#include "Misc/SecureHash.h"
#include "EngineUtils.h"
#include "Shader/ShaderTypes.h"
#include "RenderingThread.h"

#if WITH_EDITORONLY_DATA
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

#include "ContentStreaming.h"

#define LOCTEXT_NAMESPACE "USparseVolumeTexture"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTexture, Log, All);

namespace UE
{
namespace SVT
{
namespace Private
{
	// SVT_TODO: This really should be a shared function.
	template <typename Y, typename T>
	void SerializeEnumAs(FArchive& Ar, T& Target)
	{
		Y Buffer = static_cast<Y>(Target);
		Ar << Buffer;
		if (Ar.IsLoading())
		{
			Target = static_cast<T>(Buffer);
		}
	}
} // Private
} // SVT
} // UE

////////////////////////////////////////////////////////////////////////////////////////////////

void FSparseVolumeRawSource::Serialize(FArchive& Ar)
{
	Header.Serialize(Ar);

	Ar << Version;

	if (Version == 0)
	{
		Ar << PageTable;
		Ar << PhysicalTileDataA;
		Ar << PhysicalTileDataB;
	}
	else
	{
		// FSparseVolumeRawSource needs to account for new version
		check(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

void FSparseVolumeAssetHeader::Serialize(FArchive& Ar)
{
	Ar << Version;

	if (Version == 0)
	{
		Ar << PageTableVolumeResolution;
		Ar << TileDataVolumeResolution;
		Ar << SourceVolumeResolution;
		Ar << SourceVolumeAABBMin;
		UE::SVT::Private::SerializeEnumAs<uint8>(Ar, AttributesAFormat);
		UE::SVT::Private::SerializeEnumAs<uint8>(Ar, AttributesBFormat);
	}
	else
	{
		// FSparseVolumeAssetHeader needs to account for new version
		check(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

void FSparseVolumeTextureRuntime::Serialize(FArchive& Ar)
{
	Header.Serialize(Ar);

	Ar << Version;

	if (Version == 0)
	{
		Ar << PageTable;
		Ar << PhysicalTileDataA;
		Ar << PhysicalTileDataB;
	}
	else
	{
		// FSparseVolumeRawSource needs to account for new version
		check(false);
	}
}

void FSparseVolumeTextureRuntime::SetAsDefaultTexture()
{
	const uint32 VolumeSize = 1;
	PageTable.SetNumZeroed(VolumeSize * VolumeSize * VolumeSize);
	PhysicalTileDataA.SetNumZeroed(VolumeSize * VolumeSize * VolumeSize);
	PhysicalTileDataB.SetNumZeroed(VolumeSize * VolumeSize * VolumeSize);
}

////////////////////////////////////////////////////////////////////////////////////////////////

FSparseVolumeTextureSceneProxy::FSparseVolumeTextureSceneProxy()
	: FRenderResource()
	, SparseVolumeTextureRuntime()
	, PageTableTextureRHI(nullptr)
	, PhysicalTileDataATextureRHI(nullptr)
	, PhysicalTileDataBTextureRHI(nullptr)
{
}

FSparseVolumeTextureSceneProxy::~FSparseVolumeTextureSceneProxy()
{
}

void FSparseVolumeTextureSceneProxy::GetMemorySize(SIZE_T* SizeCPU, SIZE_T* SizeGPU) const
{
	*SizeCPU += sizeof(FSparseVolumeTextureSceneProxy);
	*SizeCPU += SparseVolumeTextureRuntime.PageTable.GetAllocatedSize();
	*SizeCPU += SparseVolumeTextureRuntime.PhysicalTileDataA.GetAllocatedSize();
	*SizeCPU += SparseVolumeTextureRuntime.PhysicalTileDataB.GetAllocatedSize();

#if RHI_ENABLE_RESOURCE_INFO
	FRHIResourceInfo ResourceInfo;
	if (PageTableTextureRHI && PageTableTextureRHI->GetResourceInfo(ResourceInfo))
	{
		*SizeGPU += ResourceInfo.VRamAllocation.AllocationSize;
	}
	if (PhysicalTileDataATextureRHI && PhysicalTileDataATextureRHI->GetResourceInfo(ResourceInfo))
	{
		*SizeGPU += ResourceInfo.VRamAllocation.AllocationSize;
	}
	if (PhysicalTileDataBTextureRHI && PhysicalTileDataBTextureRHI->GetResourceInfo(ResourceInfo))
	{
		*SizeGPU += ResourceInfo.VRamAllocation.AllocationSize;
	}
#endif
}

void FSparseVolumeTextureSceneProxy::InitRHI()
{
	// Page table
	{
		EPixelFormat PageEntryFormat = PF_R32_UINT;
		FIntVector3 PageTableVolumeResolution = SparseVolumeTextureRuntime.Header.PageTableVolumeResolution;
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PageTable.RHITexture"),
				PageTableVolumeResolution.X, PageTableVolumeResolution.Y, PageTableVolumeResolution.Z, PageEntryFormat)
			.SetFlags(ETextureCreateFlags::ShaderResource);

		PageTableTextureRHI = RHICreateTexture(Desc);

		const int32 FormatSize = GPixelFormats[PageEntryFormat].BlockBytes;
		const FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, PageTableVolumeResolution.X, PageTableVolumeResolution.Y, PageTableVolumeResolution.Z);
		RHIUpdateTexture3D(PageTableTextureRHI, 0, UpdateRegion, UpdateRegion.Width * FormatSize, UpdateRegion.Width * UpdateRegion.Height * FormatSize, (const uint8*)SparseVolumeTextureRuntime.PageTable.GetData());
	}

	// Tile data
	{
		FIntVector3 TileDataVolumeResolution = SparseVolumeTextureRuntime.Header.TileDataVolumeResolution;
		EPixelFormat VoxelFormatA = SparseVolumeTextureRuntime.Header.AttributesAFormat;
		EPixelFormat VoxelFormatB = SparseVolumeTextureRuntime.Header.AttributesBFormat;
		const FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, TileDataVolumeResolution.X, TileDataVolumeResolution.Y, TileDataVolumeResolution.Z);

		// A
		if (VoxelFormatA != PF_Unknown)
		{
			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PhysicalTileDataA.RHITexture"),
					TileDataVolumeResolution.X, TileDataVolumeResolution.Y, TileDataVolumeResolution.Z, VoxelFormatA)
				.SetFlags(ETextureCreateFlags::ShaderResource);

			PhysicalTileDataATextureRHI = RHICreateTexture(Desc);

			const int32 FormatSize = GPixelFormats[VoxelFormatA].BlockBytes;
			RHIUpdateTexture3D(PhysicalTileDataATextureRHI, 0, UpdateRegion, UpdateRegion.Width * FormatSize, UpdateRegion.Width * UpdateRegion.Height * FormatSize, (const uint8*)SparseVolumeTextureRuntime.PhysicalTileDataA.GetData());
		}

		// B
		if (VoxelFormatB != PF_Unknown)
		{
			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PhysicalTileDataB.RHITexture"),
					TileDataVolumeResolution.X, TileDataVolumeResolution.Y, TileDataVolumeResolution.Z, VoxelFormatB)
				.SetFlags(ETextureCreateFlags::ShaderResource);

			PhysicalTileDataBTextureRHI = RHICreateTexture(Desc);

			const int32 FormatSize = GPixelFormats[VoxelFormatB].BlockBytes;
			RHIUpdateTexture3D(PhysicalTileDataBTextureRHI, 0, UpdateRegion, UpdateRegion.Width * FormatSize, UpdateRegion.Width * UpdateRegion.Height * FormatSize, (const uint8*)SparseVolumeTextureRuntime.PhysicalTileDataB.GetData());
		}
	}
}

void FSparseVolumeTextureSceneProxy::ReleaseRHI()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////

FSparseVolumeTextureFrame::FSparseVolumeTextureFrame()
	: RuntimeStreamedInData()
	, SparseVolumeTextureSceneProxy()
#if WITH_EDITORONLY_DATA
	, RawData()
#endif
{
}

FSparseVolumeTextureFrame::~FSparseVolumeTextureFrame()
{
}

bool FSparseVolumeTextureFrame::BuildRuntimeData(FSparseVolumeTextureRuntime* OutRuntimeData)
{
#if WITH_EDITORONLY_DATA
	// Check if the virtualized bulk data payload is available now
	if (RawData.HasPayloadData())
	{
		// First, read the source data in from the raw data stored as bulk data
		UE::Serialization::FEditorBulkDataReader RawDataArchiveReader(RawData);
		FSparseVolumeRawSource SparseVolumeRawSource;
		SparseVolumeRawSource.Serialize(RawDataArchiveReader);

		// Then, convert the raw source data to SVT
		OutRuntimeData->Header = SparseVolumeRawSource.Header;
		OutRuntimeData->PageTable = MoveTemp(SparseVolumeRawSource.PageTable);
		OutRuntimeData->PhysicalTileDataA = MoveTemp(SparseVolumeRawSource.PhysicalTileDataA);
		OutRuntimeData->PhysicalTileDataB = MoveTemp(SparseVolumeRawSource.PhysicalTileDataB);

		// Now unload the raw data
		RawData.UnloadData();
		
		return true;
	}
#endif
	return false;
}

void FSparseVolumeTextureFrame::Serialize(FArchive& Ar, UStreamableSparseVolumeTexture* Owner, int32 FrameIndex)
{
	FStripDataFlags StripFlags(Ar);

	const bool bInlinePayload = (FrameIndex == 0);
	RuntimeStreamedInData.SetBulkDataFlags(bInlinePayload ? BULKDATA_ForceInlinePayload : BULKDATA_Force_NOT_InlinePayload);

	if (StripFlags.IsEditorDataStripped() && Ar.IsLoadingFromCookedPackage())
	{
		// In this case we are loading in game with a cooked build so we only need to load the runtime data.

		// Read cooked bulk data from archive
		RuntimeStreamedInData.Serialize(Ar, Owner);

		if (bInlinePayload)
		{
			SparseVolumeTextureSceneProxy = new FSparseVolumeTextureSceneProxy();

			// Create runtime data from cooked bulk data
			{
				FBulkDataReader BulkDataReader(RuntimeStreamedInData);
				SparseVolumeTextureSceneProxy->GetRuntimeData().Serialize(BulkDataReader);
			}

			// The bulk data is no longer needed
			RuntimeStreamedInData.RemoveBulkData();

			// Runtime data is now valid, initialize the render thread proxy
			BeginInitResource(SparseVolumeTextureSceneProxy);
		}
	}
	else if (Ar.IsCooking())
	{
		// We are cooking the game, serialize the asset out.

		FSparseVolumeTextureRuntime RuntimeData;
		const bool bBuiltRuntimeData = BuildRuntimeData(&RuntimeData);
		check(bBuiltRuntimeData); // SVT_TODO: actual error handling

		// Write runtime data into RuntimeStreamedInData
		{
			FBulkDataWriter BulkDataWriter(RuntimeStreamedInData);
			RuntimeData.Serialize(BulkDataWriter);
		}

		// And now write the cooked bulk data to the archive
		RuntimeStreamedInData.Serialize(Ar, Owner);
	}
	else if (!Ar.IsObjectReferenceCollector())
	{
#if WITH_EDITORONLY_DATA
		// When in EDITOR:
		//  - We only serialize raw data 
		//  - The runtime data is fetched/put from/to DDC
		//  - This EditorBulk data do not load the full and huge OpenVDB data. That is only done explicitly later.
		RawData.Serialize(Ar, Owner);
#endif
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

USparseVolumeTexture::USparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FVector4 USparseVolumeTexture::GetUniformParameter(int32 Index) const
{
	const FSparseVolumeTextureSceneProxy* Proxy = GetSparseVolumeTextureSceneProxy();
	if (Proxy)
	{
		const FSparseVolumeAssetHeader& Header = Proxy->GetHeader();
		switch (Index)
		{
		case ESparseVolumeTexture_TileSize:
		{
			return FVector4(float(SPARSE_VOLUME_TILE_RES), 0.0f, 0.0f, 0.0f);
		}
		case ESparseVolumeTexture_PageTableSize:
		{
			return FVector4(Header.PageTableVolumeResolution.X, Header.PageTableVolumeResolution.Y, Header.PageTableVolumeResolution.Z, 0.0f);
		}
		case ESparseVolumeTexture_UVScale: // fallthrough
		case ESparseVolumeTexture_UVBias:
		{
			FVector Scale;
			FVector Bias;
			GetFrameUVScaleBias(&Scale, &Bias);
			return (Index == ESparseVolumeTexture_UVScale) ? FVector4(Scale) : FVector4(Bias);
		}
		default:
		{
			break;
		}
		}
		checkNoEntry();
		return FVector4(ForceInitToZero);
	}

	// 0 while waiting for the proxy
	return FVector4(ForceInitToZero);
}

void USparseVolumeTexture::GetPackedUniforms(FUintVector4& OutPacked0, FUintVector4& OutPacked1) const
{
	const FVector4 TileSize = GetUniformParameter(ESparseVolumeTexture_TileSize);
	const FVector4 PageTableSize = GetUniformParameter(ESparseVolumeTexture_PageTableSize);
	const FVector4 UVScale = GetUniformParameter(ESparseVolumeTexture_UVScale);
	const FVector4 UVBias = GetUniformParameter(ESparseVolumeTexture_UVBias);
	const FUintVector4 PageTableSizeUIMinusOne = FUintVector4(PageTableSize.X - 1, PageTableSize.Y - 1, PageTableSize.Z - 1, 0);

	auto AsUint = [](float X)
	{
		union { float F; uint32 U; } FU = { X };
		return FU.U;
	};

	OutPacked0.X = AsUint((float)UVScale.X);
	OutPacked0.Y = AsUint((float)UVScale.Y);
	OutPacked0.Z = AsUint((float)UVScale.Z);
	OutPacked0.W = (PageTableSizeUIMinusOne.X & 0x7FF) | ((PageTableSizeUIMinusOne.Y & 0x7FF) << 11) | ((PageTableSizeUIMinusOne.Z & 0x3FF) << 22);
	OutPacked1.X = AsUint((float)UVBias.X);
	OutPacked1.Y = AsUint((float)UVBias.Y);
	OutPacked1.Z = AsUint((float)UVBias.Z);
	OutPacked1.W = AsUint((float)TileSize.X);
}

void USparseVolumeTexture::GetFrameUVScaleBias(FVector* OutScale, FVector* OutBias) const
{
	*OutScale = FVector::One();
	*OutBias = FVector::Zero();
	const FSparseVolumeTextureSceneProxy* Proxy = GetSparseVolumeTextureSceneProxy();
	if (Proxy)
	{
		const FSparseVolumeAssetHeader& Header = Proxy->GetHeader();
		const FBox VolumeBounds = GetVolumeBounds();
		check(VolumeBounds.GetVolume() > 0.0);
		const FBox FrameBounds = FBox(FVector(Header.SourceVolumeAABBMin), FVector(Header.SourceVolumeAABBMin + Header.SourceVolumeResolution)); // AABB of current frame
		const FBox FrameBoundsPadded = FBox(FVector(Header.SourceVolumeAABBMin), FVector(Header.SourceVolumeAABBMin + Header.PageTableVolumeResolution * SPARSE_VOLUME_TILE_RES)); // padded to multiple of page size
		const FVector FrameExtent = FrameBounds.GetExtent();

		// 3D UV coordinates are specified in [0, 1]. Before addressing the page tables which might have padding,
		// since source volume resolution might not align to tile resolution, we have to rescale the uv so that [0,1] maps to the source texture boundaries.
		const FVector UVToPhysical = FrameExtent / FrameBoundsPadded.GetExtent();

		*OutScale = VolumeBounds.GetExtent() / FrameExtent;
		*OutScale *= UVToPhysical;
		*OutBias = -((FrameBounds.Min - VolumeBounds.Min) / (VolumeBounds.Max - VolumeBounds.Min) * *OutScale);
	}
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

////////////////////////////////////////////////////////////////////////////////////////////////

UStreamableSparseVolumeTexture::UStreamableSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UStreamableSparseVolumeTexture::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy();
#else
	IStreamingManager::Get().GetSparseVolumeTextureStreamingManager().AddSparseVolumeTexture(this); // GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy() handles this in editor builds
#endif
}

void UStreamableSparseVolumeTexture::FinishDestroy()
{
	Super::FinishDestroy();

	IStreamingManager::Get().GetSparseVolumeTextureStreamingManager().RemoveSparseVolumeTexture(this);
}

void UStreamableSparseVolumeTexture::BeginDestroy()
{
	Super::BeginDestroy();
	for (FSparseVolumeTextureFrame& Frame : Frames)
	{
		if (Frame.SparseVolumeTextureSceneProxy)
		{
			ENQUEUE_RENDER_COMMAND(UStreamableSparseVolumeTexture_DeleteSVTProxy)(
				[Proxy = Frame.SparseVolumeTextureSceneProxy](FRHICommandListImmediate& RHICmdList)
				{
					Proxy->ReleaseResource();
					delete Proxy;
				});
			Frame.SparseVolumeTextureSceneProxy = nullptr;
		}
	}
}

void UStreamableSparseVolumeTexture::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	int32 NumFrames = Frames.Num();
	Ar << NumFrames;

	if (Ar.IsLoading())
	{
		Frames.Reset(NumFrames);
		Frames.AddDefaulted(NumFrames);
	}
	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		Frames[FrameIndex].Serialize(Ar, this, FrameIndex);
	}
}

#if WITH_EDITOR
void UStreamableSparseVolumeTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy();
}
#endif // WITH_EDITOR

void UStreamableSparseVolumeTexture::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	SIZE_T SizeCPU = sizeof(Frames);
	SIZE_T SizeGPU = 0;
	SizeCPU += Frames.GetAllocatedSize();
	for (const FSparseVolumeTextureFrame& Frame : Frames)
	{
		if (Frame.SparseVolumeTextureSceneProxy)
		{
			Frame.SparseVolumeTextureSceneProxy->GetMemorySize(&SizeCPU, &SizeGPU);
		}
	}
	ISparseVolumeTextureStreamingManager& StreamingManager = IStreamingManager::Get().GetSparseVolumeTextureStreamingManager();
	StreamingManager.GetMemorySizeForSparseVolumeTexture(this, &SizeCPU, &SizeGPU);
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(SizeCPU);
	CumulativeResourceSize.AddDedicatedVideoMemoryBytes(SizeGPU);
}

const FSparseVolumeTextureSceneProxy* UStreamableSparseVolumeTexture::GetStreamedFrameProxyOrFallback(int32 FrameIndex) const
{
	ISparseVolumeTextureStreamingManager& StreamingManager = IStreamingManager::Get().GetSparseVolumeTextureStreamingManager();
	const FSparseVolumeTextureSceneProxy* Proxy = StreamingManager.GetSparseVolumeTextureSceneProxy(this, FrameIndex, true);

	int32 FallbackFrameIndex = FrameIndex;
	while (!Proxy)
	{
		FallbackFrameIndex = FallbackFrameIndex > 0 ? (FallbackFrameIndex - 1) : (Frames.Num() - 1);
		if (FallbackFrameIndex == FrameIndex)
		{
			UE_LOG(LogSparseVolumeTexture, Warning, TEXT("Failed to get ANY streamed SparseVolumeTexture frame  SVT: %s, FrameIndex: %i"), *GetName(), FrameIndex);
			return nullptr;
		}
		Proxy = StreamingManager.GetSparseVolumeTextureSceneProxy(this, FallbackFrameIndex, false);
	}

	return Proxy;
}

TArrayView<const FSparseVolumeTextureFrame> UStreamableSparseVolumeTexture::GetFrames() const
{
	return Frames;
}

void UStreamableSparseVolumeTexture::GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy()
{
#if WITH_EDITORONLY_DATA
	UE::DerivedData::FRequestOwner DDCRequestOwner(UE::DerivedData::EPriority::Normal);
	{
		UE::DerivedData::FRequestBarrier DDCRequestBarrier(DDCRequestOwner);
		for (FSparseVolumeTextureFrame& Frame : Frames)
		{
			// Release any previously allocated render thread proxy
			if (Frame.SparseVolumeTextureSceneProxy)
			{
				BeginReleaseResource(Frame.SparseVolumeTextureSceneProxy);
			}
			else
			{
				Frame.SparseVolumeTextureSceneProxy = new FSparseVolumeTextureSceneProxy();
			}

			GenerateOrLoadDDCRuntimeDataForFrame(Frame, DDCRequestOwner);
		}
	}

	// Wait for all DDC requests to complete before creating the proxies
	DDCRequestOwner.Wait();

	for (FSparseVolumeTextureFrame& Frame : Frames)
	{
		// Runtime data is now valid, initialize the render thread proxy
		BeginInitResource(Frame.SparseVolumeTextureSceneProxy);
	}

	IStreamingManager::Get().GetSparseVolumeTextureStreamingManager().AddSparseVolumeTexture(this);
#endif
}

void UStreamableSparseVolumeTexture::GenerateOrLoadDDCRuntimeDataForFrame(FSparseVolumeTextureFrame& Frame, UE::DerivedData::FRequestOwner& DDCRequestOwner)
{
#if WITH_EDITORONLY_DATA
	using namespace UE::DerivedData;

	static const FString SparseVolumeTextureDDCVersion = TEXT("381AE2A9-A903-4C8F-8486-891E24D6EC70");	// Bump this if you want to ignore all cached data so far.
	const FString DerivedDataKey = Frame.RawData.GetIdentifier().ToString() + SparseVolumeTextureDDCVersion;

	const FCacheKey Key = ConvertLegacyCacheKey(DerivedDataKey);
	const FSharedString Name = MakeStringView(GetPathName());

	UE::DerivedData::GetCache().GetValue({ {Name, Key} }, DDCRequestOwner,
		[this, &Frame, &DDCRequestOwner](FCacheGetValueResponse&& Response)
		{
			if (Response.Status == EStatus::Ok)
			{
				DDCRequestOwner.LaunchTask(TEXT("UStreamableSparseVolumeTexture_DerivedDataLoad"),
					[this, &Frame, Value = MoveTemp(Response.Value)]()
					{
						FSharedBuffer Data = Value.GetData().Decompress();
						FMemoryReaderView Ar(Data, true /*bIsPersistent*/);
						Frame.SparseVolumeTextureSceneProxy->GetRuntimeData().Serialize(Ar);
					});
			}
			else if (Response.Status == EStatus::Error)
			{
				DDCRequestOwner.LaunchTask(TEXT("UStreamableSparseVolumeTexture_DerivedDataBuild"),
					[this, &Frame, &DDCRequestOwner, Name = Response.Name, Key = Response.Key]()
					{
						FSparseVolumeTextureRuntime& RuntimeData = Frame.SparseVolumeTextureSceneProxy->GetRuntimeData();

						// Check if the virtualized bulk data payload is available now
						if (Frame.RawData.HasPayloadData())
						{
							const bool bSuccess = Frame.BuildRuntimeData(&RuntimeData);
							ensure(bSuccess);
						}
						else
						{
							UE_LOG(LogSparseVolumeTexture, Error, TEXT("SparseVolumeTexture - Raw source data is not available for %s. Using default data."), *GetName());
							RuntimeData.SetAsDefaultTexture();
						}

						// Using a LargeMemoryWriter for serialization since the data can be bigger than 2 GB
						FLargeMemoryWriter LargeMemWriter(0, /*bIsPersistent=*/ true);
						RuntimeData.Serialize(LargeMemWriter);

						int64 UncompressedSize = LargeMemWriter.TotalSize();

						// Since the DDC doesn't support data bigger than 2 GB, we only cache for such uncompressed size.
						constexpr int64 SizeThreshold = 2147483648LL;	// 2GB
						const bool bIsCacheable = UncompressedSize < SizeThreshold;
						if (bIsCacheable)
						{
							FValue Value = FValue::Compress(FSharedBuffer::MakeView(LargeMemWriter.GetView()));
							UE::DerivedData::GetCache().PutValue({ {Name, Key, Value} }, DDCRequestOwner);
						}
						else
						{
							UE_LOG(LogSparseVolumeTexture, Error, TEXT("SparseVolumeTexture - the asset is too large to fit in Derived Data Cache %s"), *GetName());
						}
					});
			}
		});
#endif // WITH_EDITORONLY_DATA
}

////////////////////////////////////////////////////////////////////////////////////////////////

UStaticSparseVolumeTexture::UStaticSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////

UAnimatedSparseVolumeTexture::UAnimatedSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

const FSparseVolumeTextureSceneProxy* UAnimatedSparseVolumeTexture::GetSparseVolumeTextureSceneProxy() const
{
	// When an AnimatedSparseVolumeTexture is used as SparseVolumeTexture, it can only be previewed using a single preview frame.
	check(!Frames.IsEmpty());
	const int32 FrameIndex = PreviewFrameIndex % Frames.Num();
	return GetStreamedFrameProxyOrFallback(FrameIndex);
}

const FSparseVolumeTextureSceneProxy* UAnimatedSparseVolumeTexture::GetSparseVolumeTextureFrameSceneProxy(int32 FrameIndex) const
{
	check(!Frames.IsEmpty());
	FrameIndex = FrameIndex % Frames.Num();
	return GetStreamedFrameProxyOrFallback(FrameIndex);
}

const FSparseVolumeAssetHeader* UAnimatedSparseVolumeTexture::GetSparseVolumeTextureFrameHeader(int32 FrameIndex) const
{
	check(!Frames.IsEmpty());
	FrameIndex = FrameIndex % Frames.Num();
	const FSparseVolumeTextureSceneProxy* Proxy = GetStreamedFrameProxyOrFallback(FrameIndex);
	return Proxy ? &Proxy->GetHeader() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////

USparseVolumeTextureFrame::USparseVolumeTextureFrame(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USparseVolumeTextureFrame* USparseVolumeTextureFrame::CreateFrame(USparseVolumeTexture* Texture, int32 FrameIndex)
{
	if (!Texture)
	{
		return nullptr;
	}
	
	const FSparseVolumeTextureSceneProxy* Proxy = nullptr;
	if (Texture->IsA<UStreamableSparseVolumeTexture>())
	{
		UStreamableSparseVolumeTexture* StreamableSVT = CastChecked<UStreamableSparseVolumeTexture>(Texture);
		check(StreamableSVT);
		Proxy = StreamableSVT->GetStreamedFrameProxyOrFallback(FrameIndex);
	}
	else
	{
		Proxy = Texture->GetSparseVolumeTextureSceneProxy();
	}

	if (Proxy)
	{
		USparseVolumeTextureFrame* Frame = NewObject<USparseVolumeTextureFrame>();
		Frame->Initialize(Proxy, Texture->GetVolumeBounds());
		return Frame;
	}
	
	return nullptr;
}

void USparseVolumeTextureFrame::Initialize(const FSparseVolumeTextureSceneProxy* InSceneProxy, const FBox& InVolumeBounds)
{
	SceneProxy = InSceneProxy;
	VolumeBounds = InVolumeBounds;
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

bool UAnimatedSparseVolumeTextureController::IsPlaying()
{
	return bIsPlaying;
}

void UAnimatedSparseVolumeTextureController::Update(float DeltaTime)
{
	if (!SparseVolumeTexture || !bIsPlaying)
	{
		return;
	}

	// Update animation time
	const float AnimationDuration = GetDuration();
	Time = FMath::Fmod(Time + DeltaTime, AnimationDuration + UE_SMALL_NUMBER);
}

void UAnimatedSparseVolumeTextureController::SetSparseVolumeTexture(USparseVolumeTexture* Texture)
{
	if (Texture == SparseVolumeTexture)
	{
		return;
	}

	SparseVolumeTexture = Texture;
	bIsPlaying = bIsPlaying && (SparseVolumeTexture != nullptr);
	Time = 0.0f;
}

void UAnimatedSparseVolumeTextureController::SetTime(float InTime)
{
	const float AnimationDuration = GetDuration();
	Time = FMath::Fmod(InTime, AnimationDuration + UE_SMALL_NUMBER);
}

void UAnimatedSparseVolumeTextureController::SetFractionalFrameIndex(float Frame)
{
	if (!SparseVolumeTexture)
	{
		return;
	}

	const int32 FrameCount = SparseVolumeTexture->GetFrameCount();
	Frame = FMath::Fmod(Frame, (float)FrameCount);
	Time = Frame / (FrameRate + UE_SMALL_NUMBER);
}

USparseVolumeTexture* UAnimatedSparseVolumeTextureController::GetSparseVolumeTexture()
{
	return SparseVolumeTexture;
}

float UAnimatedSparseVolumeTextureController::GetTime()
{
	return Time;
}

float UAnimatedSparseVolumeTextureController::GetFractionalFrameIndex()
{
	if (!SparseVolumeTexture)
	{
		return 0.0f;
	}

	const int32 FrameCount = SparseVolumeTexture->GetFrameCount();
	const float FrameIndexF = FMath::Fmod(Time * FrameRate, (float)FrameCount);
	return FrameIndexF;
}

USparseVolumeTextureFrame* UAnimatedSparseVolumeTextureController::GetCurrentFrame()
{
	if (!SparseVolumeTexture)
	{
		return nullptr;
	}

	// Compute (fractional) index of frame to sample
	const float FrameIndexF = GetFractionalFrameIndex();
	const int32 FrameIndex = (int32)FrameIndexF;

	// Create and initialize a USparseVolumeTextureFrame which holds the frame to sample and can be bound to shaders
	USparseVolumeTextureFrame* Frame = USparseVolumeTextureFrame::CreateFrame(SparseVolumeTexture, FrameIndex);

	return Frame;
}

void UAnimatedSparseVolumeTextureController::GetLerpFrames(USparseVolumeTextureFrame*& Frame0, USparseVolumeTextureFrame*& Frame1, float& LerpAlpha)
{
	if (!SparseVolumeTexture)
	{
		return;
	}

	// Compute (fractional) index of frame to sample
	const float FrameIndexF = GetFractionalFrameIndex();
	const int32 FrameIndex = (int32)FrameIndexF;
	LerpAlpha = FMath::Frac(FrameIndexF);

	// Create and initialize a USparseVolumeTextureFrame which holds the frame to sample and can be bound to shaders
	Frame0 = USparseVolumeTextureFrame::CreateFrame(SparseVolumeTexture, FrameIndex);
	Frame1 = USparseVolumeTextureFrame::CreateFrame(SparseVolumeTexture, FrameIndex + 1);
}

float UAnimatedSparseVolumeTextureController::GetDuration()
{
	if (!SparseVolumeTexture)
	{
		return 0.0f;
	}

	const int32 FrameCount = SparseVolumeTexture->GetFrameCount();
	const float AnimationDuration = FrameCount / (FrameRate + UE_SMALL_NUMBER);
	return AnimationDuration;
}

////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
