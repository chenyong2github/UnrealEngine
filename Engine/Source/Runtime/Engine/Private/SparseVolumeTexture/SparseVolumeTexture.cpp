// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SparseVolumeTexture.cpp: SparseVolumeTexture implementation.
=============================================================================*/

#include "SparseVolumeTexture/SparseVolumeTexture.h"

#include "Misc/SecureHash.h"
#include "EngineUtils.h"
#include "Shader/ShaderTypes.h"

#if WITH_EDITORONLY_DATA
#include "DerivedDataCacheInterface.h"
#endif

#include "Serialization/LargeMemoryReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/BulkDataReader.h"
#include "Serialization/BulkDataWriter.h"
#include "Serialization/EditorBulkDataReader.h"

#define LOCTEXT_NAMESPACE "USparseVolumeTexture"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTexture, Log, All);

FConvertOpenVDBToSparseVolumeTextureDelegate ConvertOpenVDBToSparseVolumeTextureDelegate;

FConvertOpenVDBToSparseVolumeTextureDelegate& OnConvertOpenVDBToSparseVolumeTexture()
{
	return ConvertOpenVDBToSparseVolumeTextureDelegate;
}

void FSparseVolumeAssetHeader::Serialize(FArchive& Ar)
{

	Ar << Version;

	if (Version == 0)
	{
		Ar << PageTableVolumeResolution;
		Ar << TileDataVolumeResolution;
		Ar << SourceVolumeResolution;

		uint32 FormatAsUint = (uint32)PackedDataAFormat;
		Ar << FormatAsUint;
		if (Ar.IsLoading())
		{
			PackedDataAFormat = static_cast<EPixelFormat>(FormatAsUint);
		}
	}
	else
	{
		// FSparseVolumeAssetHeader needs to account for new version
		check(false);
	}
}

void FSparseVolumeRawSource::Serialize(FArchive& Ar)
{
	Ar << Version;

	if (Version == 0)
	{
		uint32 FormatAsUint = (uint32)PackedDataA.Format;
		Ar << FormatAsUint;
		if (Ar.IsLoading())
		{
			PackedDataA.Format = static_cast<ESparseVolumePackedDataFormat>(FormatAsUint);
		}
		Ar << PackedDataA.SourceGridIndex;
		Ar << PackedDataA.SourceComponentIndex;
		Ar << PackedDataA.bRemapInputForUnorm;

		Ar << SourceAssetFile;
	}
	else
	{
		// FSparseVolumeRawSource needs to account for new version
		check(false);
	}
}

void FSparseVolumeTextureRuntime::Serialize(FArchive& Ar)
{
	Header.Serialize(Ar);

	Ar << Version;

	if (Version == 0)
	{
		Ar << DensityPage;
		Ar << DensityData;
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
	DensityPage.SetNumZeroed(VolumeSize * VolumeSize * VolumeSize);
	DensityData.SetNumZeroed(VolumeSize * VolumeSize * VolumeSize);
}

////////////////////////////////////////////////////////////////////////////////////////////////

FSparseVolumeTextureFrame::FSparseVolumeTextureFrame()
	: RuntimeStreamedInData()
	, SparseVolumeTextureRuntime()
	, SparseVolumeTextureSceneProxy()
#if WITH_EDITORONLY_DATA
	, RawData()
#endif
{
}

FSparseVolumeTextureFrame::~FSparseVolumeTextureFrame()
{
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
	case ESparseVolumeTexture_PhysicalUVToPageUV:	return UE::Shader::EValueType::Float3;
	case ESparseVolumeTexture_TileSize:				return UE::Shader::EValueType::Float1;
	case ESparseVolumeTexture_PageTableSize:		return UE::Shader::EValueType::Float3;
	default:
		break;
	}
	check(0);
	return UE::Shader::EValueType::Float4;
}

////////////////////////////////////////////////////////////////////////////////////////////////

UStaticSparseVolumeTexture::UStaticSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, StaticFrame()
{
}

void UStaticSparseVolumeTexture::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	// CumulativeResourceSize.AddDedicatedSystemMemoryBytes but not the RawData size
}

void UStaticSparseVolumeTexture::PostLoad()
{
	GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy();

	Super::PostLoad();
}

void UStaticSparseVolumeTexture::BeginDestroy()
{
	Super::BeginDestroy();

	BeginReleaseResource(&StaticFrame.SparseVolumeTextureSceneProxy);
}

void UStaticSparseVolumeTexture::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	FStripDataFlags StripFlags(Ar);

	// The runtime bulk data for static sparse volume texture is always loaded, not streamed in.
	StaticFrame.RuntimeStreamedInData.SetBulkDataFlags(BULKDATA_ForceInlinePayload);

	if (StripFlags.IsEditorDataStripped() && Ar.IsLoadingFromCookedPackage())
	{
		// In this case we are loading in game with a cooked build so we only need to load the runtime data.
		{
			FBulkDataReader RuntimeStreamedInData(StaticFrame.RuntimeStreamedInData);
			StaticFrame.SparseVolumeTextureRuntime.Serialize(RuntimeStreamedInData);
		}

		// The bulk data is no longer needed
		StaticFrame.RuntimeStreamedInData.RemoveBulkData();

		// Runtime data is now valid, create the render thread proxy
		StaticFrame.SparseVolumeTextureSceneProxy.InitialiseRuntimeData(StaticFrame.SparseVolumeTextureRuntime);
		BeginInitResource(&StaticFrame.SparseVolumeTextureSceneProxy);
	}
	else if (Ar.IsCooking())
	{
		// We are cooking the game, serialize the asset out.
		ConvertRawSourceDataToSparseVolumeTextureRuntime();

		{
			FBulkDataWriter RuntimeStreamedInData(StaticFrame.RuntimeStreamedInData);
			StaticFrame.SparseVolumeTextureRuntime.Serialize(RuntimeStreamedInData);
		}
	}
	else if (!Ar.IsObjectReferenceCollector())
	{
#if WITH_EDITORONLY_DATA
		// When in EDITOR:
		//  - We only serialize raw data 
		//  - The runtime data is fetched/put from/to DDC
		//  - This EditorBulk data do not load the full and huge OpenVDB data. That is only done explicitly later.
		StaticFrame.RawData.Serialize(Ar, this);
#endif
	}
}

void UStaticSparseVolumeTexture::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);
}

#if WITH_EDITOR
void UStaticSparseVolumeTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy();
}
#endif // WITH_EDITOR

void UStaticSparseVolumeTexture::ConvertRawSourceDataToSparseVolumeTextureRuntime()
{
#if WITH_EDITOR
	// Check if the virtualized bulk data payload is available now
	if (StaticFrame.RawData.HasPayloadData())
	{
		// When cooking, the runtime data must be serialized out.

		// First, read the source data in from the raw data stored as bulk data
		UE::Serialization::FEditorBulkDataReader RawDataArchiveReader(StaticFrame.RawData);
		FSparseVolumeRawSource SparseVolumeRawSource;
		SparseVolumeRawSource.Serialize(RawDataArchiveReader);

		// Then, cook the runtime data and serialize it out.
		const bool bSuccess = ConvertOpenVDBToSparseVolumeTextureDelegate.IsBound() && ConvertOpenVDBToSparseVolumeTextureDelegate.Execute(
			SparseVolumeRawSource.SourceAssetFile,
			SparseVolumeRawSource.PackedDataA,
			&StaticFrame.SparseVolumeTextureRuntime.Header,
			&StaticFrame.SparseVolumeTextureRuntime.DensityPage,
			&StaticFrame.SparseVolumeTextureRuntime.DensityData,
			false, FVector::Zero(), FVector::Zero());
		ensure(bSuccess);

		// Now unload the raw data
		StaticFrame.RawData.UnloadData();
	}
	else
	{
		UE_LOG(LogSparseVolumeTexture, Error, TEXT("SparseVolumeTexture - Raw source data is not available for %s. Using default data."), *GetName());
		StaticFrame.SparseVolumeTextureRuntime.SetAsDefaultTexture();
	}
#endif // WITH_EDITOR
}

void UStaticSparseVolumeTexture::GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy()
{
	// Release any previously allocated render thread proxy
	BeginReleaseResource(&StaticFrame.SparseVolumeTextureSceneProxy);

#if WITH_EDITORONLY_DATA
	// We only fetch/put DDC when in editor. Otherwise, StaticFrame.SparseVolumeTextureRuntime is serialize in.
	GenerateOrLoadDDCRuntimeData();
#endif

	// Runtime data is now valid, create the render thread proxy
	StaticFrame.SparseVolumeTextureSceneProxy.InitialiseRuntimeData(StaticFrame.SparseVolumeTextureRuntime);
	BeginInitResource(&StaticFrame.SparseVolumeTextureSceneProxy);
}

void UStaticSparseVolumeTexture::GenerateOrLoadDDCRuntimeData()
{
#if WITH_EDITORONLY_DATA
	static const FString SparseVolumeTextureDDCVersion = TEXT("381AE2A9-A903-4C8F-8486-891E24D6EC70");	// Bump this if you want to ignore all cached data so far.
	const FString DerivedDataKey = StaticFrame.RawData.GetIdentifier().ToString() + SparseVolumeTextureDDCVersion;

	bool bSuccess = true;
	TArray<uint8> DerivedData;
	if (GetDerivedDataCacheRef().GetSynchronous(*DerivedDataKey, DerivedData, GetPathName()))
	{
		UE_LOG(LogSparseVolumeTexture, Display, TEXT("SparseVolumeTexture - Caching %s"), *GetName());

		FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);

		int64 UncompressedSize = 0;
		Ar << UncompressedSize;

		uint8* DecompressionBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(UncompressedSize));
		Ar.SerializeCompressed(DecompressionBuffer, UncompressedSize, NAME_Zlib);

		FLargeMemoryReader LargeMemReader(DecompressionBuffer, UncompressedSize, ELargeMemoryReaderFlags::Persistent | ELargeMemoryReaderFlags::TakeOwnership);

		StaticFrame.SparseVolumeTextureRuntime.Serialize(LargeMemReader);
	}
	else
	{
		// Using a LargeMemoryWriter for serialization since the data can be bigger than 2 GB
		FLargeMemoryWriter LargeMemWriter(0, /*bIsPersistent=*/ true);

		ConvertRawSourceDataToSparseVolumeTextureRuntime();
		StaticFrame.SparseVolumeTextureRuntime.Serialize(LargeMemWriter);

		int64 UncompressedSize = LargeMemWriter.TotalSize();

		// Since the DDC doesn't support data bigger than 2 GB, we only cache for such uncompressed size.
		constexpr int64 SizeThreshold = 2147483648;	// 2GB
		const bool bIsCacheable = UncompressedSize < SizeThreshold;
		if (bIsCacheable)
		{
			FMemoryWriter CompressedArchive(DerivedData, true);

			CompressedArchive << UncompressedSize; // needed for allocating decompression buffer
			CompressedArchive.SerializeCompressed(LargeMemWriter.GetData(), UncompressedSize, NAME_Zlib);

			GetDerivedDataCacheRef().Put(*DerivedDataKey, DerivedData, GetPathName());
		}
		else
		{
			UE_LOG(LogSparseVolumeTexture, Error, TEXT("SparseVolumeTexture - the asset is too large to fit in Derived Data Cache %s"), *GetName());
		}
	}
#endif // WITH_EDITORONLY_DATA
}

const FSparseVolumeAssetHeader* UStaticSparseVolumeTexture::GetSparseVolumeTextureHeader() const
{
	return &StaticFrame.SparseVolumeTextureRuntime.Header;
}
FSparseVolumeTextureSceneProxy* UStaticSparseVolumeTexture::GetSparseVolumeTextureSceneProxy()
{ 
	return &StaticFrame.SparseVolumeTextureSceneProxy; 
}
const FSparseVolumeTextureSceneProxy* UStaticSparseVolumeTexture::GetSparseVolumeTextureSceneProxy() const
{ 
	return &StaticFrame.SparseVolumeTextureSceneProxy; 
}

FVector4 UStaticSparseVolumeTexture::GetUniformParameter(int32 Index) const
{
	const FSparseVolumeTextureSceneProxy* Proxy = GetSparseVolumeTextureSceneProxy();
	if (Proxy)
	{
		const FSparseVolumeAssetHeader& Header = Proxy->GetHeader();
		switch (Index)
		{
		case ESparseVolumeTexture_PhysicalUVToPageUV:
		{
			// 3d uv coordinates are specified in [0, 1]. Before addressing the page tables which might have padding,
			// since source volume resolution might not align to tile resolution, we have to rescale the uv so that [0,1] maps to the source texture boundaries.
			FVector3f PhysicalUVToPageUV = FVector3f(Header.SourceVolumeResolution)/ FVector3f(Header.PageTableVolumeResolution * SPARSE_VOLUME_TILE_RES);
			return FVector4(PhysicalUVToPageUV.X, PhysicalUVToPageUV.Y, PhysicalUVToPageUV.Z, 0.0f);
		}
		case ESparseVolumeTexture_PageTableSize:
		{
			return FVector4(Header.PageTableVolumeResolution.X, Header.PageTableVolumeResolution.Y, Header.PageTableVolumeResolution.Z, 0.0f);
		}
		case ESparseVolumeTexture_TileSize:				return FVector4(float(SPARSE_VOLUME_TILE_RES), 0.0f, 0.0f, 0.0f);
		default:
			break;
		}
		check(0);
		return FVector4(ForceInitToZero);
	}
	// 0 while waiting for the proxy
	return FVector4(ForceInitToZero);
}

////////////////////////////////////////////////////////////////////////////////////////////////

FSparseVolumeTextureSceneProxy::FSparseVolumeTextureSceneProxy()
	: FRenderResource()
	, SparseVolumeTextureRuntime(nullptr)
	, PageTableTextureRHI(nullptr)
	, TileDataTextureRHI(nullptr)
{
}

FSparseVolumeTextureSceneProxy::~FSparseVolumeTextureSceneProxy()
{
}

void FSparseVolumeTextureSceneProxy::InitialiseRuntimeData(FSparseVolumeTextureRuntime& SparseVolumeTextureRuntimeIn)
{
	SparseVolumeTextureRuntime = &SparseVolumeTextureRuntimeIn;
}

void FSparseVolumeTextureSceneProxy::InitRHI()
{
	// Page table
	{
		EPixelFormat PageEntryFormat = PF_R32_UINT;
		FIntVector3 PageTableVolumeResolution = SparseVolumeTextureRuntime->Header.PageTableVolumeResolution;
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PageTable.RHITexture"),
				PageTableVolumeResolution.X, PageTableVolumeResolution.Y, PageTableVolumeResolution.Z, PageEntryFormat)
			.SetFlags(ETextureCreateFlags::ShaderResource);

		PageTableTextureRHI = RHICreateTexture(Desc);

		const int32 FormatSize = GPixelFormats[PageEntryFormat].BlockBytes;
		const FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, PageTableVolumeResolution.X, PageTableVolumeResolution.Y, PageTableVolumeResolution.Z);
		RHIUpdateTexture3D(PageTableTextureRHI, 0, UpdateRegion, UpdateRegion.Width * FormatSize, UpdateRegion.Width * UpdateRegion.Height * FormatSize, (const uint8*)SparseVolumeTextureRuntime->DensityPage.GetData());
	}

	// Tile data
	{
		EPixelFormat VoxelFormat = SparseVolumeTextureRuntime->Header.PackedDataAFormat;
		FIntVector3 TileDataVolumeResolution = SparseVolumeTextureRuntime->Header.TileDataVolumeResolution;
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.TileData.RHITexture"),
				TileDataVolumeResolution.X, TileDataVolumeResolution.Y, TileDataVolumeResolution.Z, VoxelFormat)
			.SetFlags(ETextureCreateFlags::ShaderResource);

		TileDataTextureRHI = RHICreateTexture(Desc);

		const int32 FormatSize = GPixelFormats[VoxelFormat].BlockBytes;
		const FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, TileDataVolumeResolution.X, TileDataVolumeResolution.Y, TileDataVolumeResolution.Z);
		RHIUpdateTexture3D(TileDataTextureRHI, 0, UpdateRegion, UpdateRegion.Width * FormatSize, UpdateRegion.Width * UpdateRegion.Height * FormatSize, (const uint8*)SparseVolumeTextureRuntime->DensityData.GetData());
	}
}

void FSparseVolumeTextureSceneProxy::ReleaseRHI()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////

UAnimatedSparseVolumeTexture::UAnimatedSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FrameCount(0)
	, AnimationFrames()
{
}

void UAnimatedSparseVolumeTexture::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	// CumulativeResourceSize.AddDedicatedSystemMemoryBytes but not the RawData size
}

void UAnimatedSparseVolumeTexture::PostLoad()
{
	const int32 FrameCountToLoad = GetFrameCountToLoad();
	for (int32 FrameIndex = 0; FrameIndex < FrameCountToLoad; FrameIndex++)
	{
		GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy(FrameIndex);
	}

	Super::PostLoad();
}

int32 UAnimatedSparseVolumeTexture::GetFrameCountToLoad() const 
{
	if (FrameCount > 0)
	{
		return bLoadAllFramesToProxies ? FrameCount : 1;
	}
	return 0;
}

void UAnimatedSparseVolumeTexture::BeginDestroy()
{
	Super::BeginDestroy();

	const int32 FrameCountToLoad = GetFrameCountToLoad();
	for(int32 FrameIndex = 0; FrameIndex < FrameCountToLoad; FrameIndex++)
	{
		FSparseVolumeTextureFrame& Frame = AnimationFrames[FrameIndex];
		BeginReleaseResource(&Frame.SparseVolumeTextureSceneProxy);
	}
}

void UAnimatedSparseVolumeTexture::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	FStripDataFlags StripFlags(Ar);

	if (StripFlags.IsEditorDataStripped() && Ar.IsLoadingFromCookedPackage())
	{
		check(false); // SVT_TODO Implement
	}
	else if (Ar.IsCooking())
	{
		check(false); // SVT_TODO Implement
	}
	else if (!Ar.IsObjectReferenceCollector())
	{
#if WITH_EDITORONLY_DATA
		// When in EDITOR:
		//  - We only serialize raw data 
		//  - The runtime data is fetched/put from/to DDC
		//  - This EditorBulk data do not load the full and huge OpenVDB data. That is only done explicitly later.
		if (Ar.IsSaving())
		{
			check(AnimationFrames.Num() == FrameCount);
		}
		else if (Ar.IsLoading())
		{
			AnimationFrames.SetNum(FrameCount);
		}

		for (int32 i = 0; i < FrameCount; ++i)
		{
			AnimationFrames[i].RawData.Serialize(Ar, this);
		}
#endif
	}
}

void UAnimatedSparseVolumeTexture::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);
}

#if WITH_EDITOR
void UAnimatedSparseVolumeTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

void UAnimatedSparseVolumeTexture::ConvertRawSourceDataToSparseVolumeTextureRuntime(int32 FrameIndex)
{
#if WITH_EDITOR
	FSparseVolumeTextureFrame& Frame = AnimationFrames[FrameIndex];

	// Check if the virtualized bulk data payload is available now
	if (Frame.RawData.HasPayloadData())
	{
		// When cooking, the runtime data must be serialized out.

		// First, read the source data in from the raw data stored as bulk data
		UE::Serialization::FEditorBulkDataReader RawDataArchiveReader(Frame.RawData);
		FSparseVolumeRawSource SparseVolumeRawSource;
		SparseVolumeRawSource.Serialize(RawDataArchiveReader);

		// Then, cook the runtime data and serialize it out.
		const bool bSuccess = ConvertOpenVDBToSparseVolumeTextureDelegate.IsBound() && ConvertOpenVDBToSparseVolumeTextureDelegate.Execute(
			SparseVolumeRawSource.SourceAssetFile,
			SparseVolumeRawSource.PackedDataA,
			&Frame.SparseVolumeTextureRuntime.Header,
			&Frame.SparseVolumeTextureRuntime.DensityPage,
			&Frame.SparseVolumeTextureRuntime.DensityData,
			false, FVector::Zero(), FVector::Zero());
		ensure(bSuccess);

		// Now unload the raw data
		Frame.RawData.UnloadData();
	}
	else
	{
		UE_LOG(LogSparseVolumeTexture, Error, TEXT("AnimatedSparseVolumeTexture - Raw source data is not available for %s - Frame %i. Using default data."), *GetName(), FrameIndex);
		Frame.SparseVolumeTextureRuntime.SetAsDefaultTexture();
	}
#endif // WITH_EDITOR
}

void UAnimatedSparseVolumeTexture::GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy(int32 FrameIndex)
{
	FSparseVolumeTextureFrame& Frame = AnimationFrames[FrameIndex];

	// Release any previously allocated render thread proxy
	BeginReleaseResource(&Frame.SparseVolumeTextureSceneProxy);

#if WITH_EDITORONLY_DATA
	// We only fetch/put DDC when in editor. Otherwise, StaticFrame.SparseVolumeTextureRuntime is serialize in.
	GenerateOrLoadDDCRuntimeData(FrameIndex);
#endif

	// Runtime data is now valid, create the render thread proxy
	Frame.SparseVolumeTextureSceneProxy.InitialiseRuntimeData(Frame.SparseVolumeTextureRuntime);
	BeginInitResource(&Frame.SparseVolumeTextureSceneProxy);
}

void UAnimatedSparseVolumeTexture::GenerateOrLoadDDCRuntimeData(int32 FrameIndex)
{
#if WITH_EDITORONLY_DATA
	static const FString SparseVolumeTextureDDCVersion = TEXT("381AE2A9-A903-4C8F-8486-891E24D6EC71");	// Bump this if you want to ignore all cached data so far.

	FSparseVolumeTextureFrame& Frame = AnimationFrames[FrameIndex];

	const FString DerivedDataKey = Frame.RawData.GetIdentifier().ToString() + SparseVolumeTextureDDCVersion;

	bool bSuccess = true;
	TArray<uint8> DerivedData;
	if (GetDerivedDataCacheRef().GetSynchronous(*DerivedDataKey, DerivedData, GetPathName()))
	{
		UE_LOG(LogSparseVolumeTexture, Display, TEXT("SparseVolumeTexture - Caching %s"), *GetName());

		FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);

		int64 UncompressedSize = 0;
		Ar << UncompressedSize;

		uint8* DecompressionBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(UncompressedSize));
		Ar.SerializeCompressed(DecompressionBuffer, UncompressedSize, NAME_Zlib);

		FLargeMemoryReader LargeMemReader(DecompressionBuffer, UncompressedSize, ELargeMemoryReaderFlags::Persistent | ELargeMemoryReaderFlags::TakeOwnership);

		Frame.SparseVolumeTextureRuntime.Serialize(LargeMemReader);
	}
	else
	{
		// Using a LargeMemoryWriter for serialization since the data can be bigger than 2 GB
		FLargeMemoryWriter LargeMemWriter(0, /*bIsPersistent=*/ true);

		ConvertRawSourceDataToSparseVolumeTextureRuntime(FrameIndex);
		Frame.SparseVolumeTextureRuntime.Serialize(LargeMemWriter);

		int64 UncompressedSize = LargeMemWriter.TotalSize();

		// Since the DDC doesn't support data bigger than 2 GB, we only cache for such uncompressed size.
		constexpr int64 SizeThreshold = 2147483648;	// 2GB
		const bool bIsCacheable = UncompressedSize < SizeThreshold;
		if (bIsCacheable)
		{
			FMemoryWriter CompressedArchive(DerivedData, true);

			CompressedArchive << UncompressedSize; // needed for allocating decompression buffer
			CompressedArchive.SerializeCompressed(LargeMemWriter.GetData(), UncompressedSize, NAME_Zlib);

			GetDerivedDataCacheRef().Put(*DerivedDataKey, DerivedData, GetPathName());
		}
		else
		{
			UE_LOG(LogSparseVolumeTexture, Error, TEXT("SparseVolumeTexture - the asset is too large to fit in Derived Data Cache %s - Frame %i"), *GetName(), FrameIndex);
		}
	}
#endif // WITH_EDITORONLY_DATA
}

const FSparseVolumeAssetHeader* UAnimatedSparseVolumeTexture::GetSparseVolumeTextureHeader() const
{
	// When an AnimatedSparseVolumeTexture is used as SparseVolumeTexture, it can only be previewed using its first frame.
	check(AnimationFrames.Num() >= 1);
	const FSparseVolumeTextureFrame& Frame = AnimationFrames[PreviewFrameIndex];
	return &Frame.SparseVolumeTextureRuntime.Header;
}

FSparseVolumeTextureSceneProxy* UAnimatedSparseVolumeTexture::GetSparseVolumeTextureSceneProxy()
{
	// When an AnimatedSparseVolumeTexture is used as SparseVolumeTexture, it can only be previewed using its first frame.
	check(AnimationFrames.Num() >= 1);
	FSparseVolumeTextureFrame& Frame = AnimationFrames[PreviewFrameIndex];
	return &Frame.SparseVolumeTextureSceneProxy;
}

const FSparseVolumeTextureSceneProxy* UAnimatedSparseVolumeTexture::GetSparseVolumeTextureSceneProxy() const
{
	// When an AnimatedSparseVolumeTexture is used as SparseVolumeTexture, it can only be previewed using its first frame.
	check(AnimationFrames.Num() >= 1);
	const FSparseVolumeTextureFrame& Frame = AnimationFrames[PreviewFrameIndex];
	return &Frame.SparseVolumeTextureSceneProxy;
}

FVector4 UAnimatedSparseVolumeTexture::GetUniformParameter(int32 Index) const
{
	// When an AnimatedSparseVolumeTexture is used as SparseVolumeTexture, it can only be previewed using its first frame.
	const FSparseVolumeTextureSceneProxy* Proxy = GetSparseVolumeTextureSceneProxy();
	if (Proxy)
	{
		const FSparseVolumeAssetHeader& Header = Proxy->GetHeader();
		switch (Index)
		{
		case ESparseVolumeTexture_PhysicalUVToPageUV:
		{
			// 3d uv coordinates are specified in [0, 1]. Before addressing the page tables which might have padding,
			// since source volume resolution might not align to tile resolution, we have to rescale the uv so that [0,1] maps to the source texture boundaries.
			FVector3f PhysicalUVToPageUV = FVector3f(Header.SourceVolumeResolution) / FVector3f(Header.PageTableVolumeResolution * SPARSE_VOLUME_TILE_RES);
			return FVector4(PhysicalUVToPageUV.X, PhysicalUVToPageUV.Y, PhysicalUVToPageUV.Z, 0.0f);
		}
		case ESparseVolumeTexture_PageTableSize:
		{
			return FVector4(Header.PageTableVolumeResolution.X, Header.PageTableVolumeResolution.Y, Header.PageTableVolumeResolution.Z, 0.0f);
		}
		case ESparseVolumeTexture_TileSize:				return FVector4(float(SPARSE_VOLUME_TILE_RES), 0.0f, 0.0f, 0.0f);
		default:
			break;
		}
		check(0);
		return FVector4(ForceInitToZero);
	}

	// 0 while waiting for the proxy
	return FVector4(ForceInitToZero);
}


const FSparseVolumeTextureSceneProxy* UAnimatedSparseVolumeTexture::GetSparseVolumeTextureFrameSceneProxy(int32 FrameIndex) const
{
	check(AnimationFrames.Num() >= 1);
	FrameIndex = FrameIndex % GetFrameCountToLoad();
	// SVT_TODO when streaming is enabled, this will likely change.
	return &AnimationFrames[FrameIndex].SparseVolumeTextureSceneProxy;
}

////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
