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
#include "SparseVolumeTexture/SparseVolumeTextureSceneProxy.h"
#include "SparseVolumeTexture/SparseVolumeTextureData.h"
#include "SparseVolumeTexture/SparseVolumeTextureUtility.h"

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

////////////////////////////////////////////////////////////////////////////////////////////////

void FSparseVolumeTextureHeader::Serialize(FArchive& Ar)
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
		Ar << NullTileValues[0];
		Ar << NullTileValues[1];
	}
	else
	{
		// FSparseVolumeTextureHeader needs to account for new version
		check(false);
	}
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

bool FSparseVolumeTextureFrame::BuildDerivedData(const FIntVector3& VolumeResolution, TextureAddress AddressX, TextureAddress AddressY, TextureAddress AddressZ, FSparseVolumeTextureData* OutMippedTextureData)
{
#if WITH_EDITORONLY_DATA
	// Check if the virtualized bulk data payload is available now
	if (RawData.HasPayloadData())
	{
		// First, read the source data in from the raw data stored as bulk data
		UE::Serialization::FEditorBulkDataReader RawDataArchiveReader(RawData);
		FSparseVolumeTextureData TextureData;
		TextureData.Serialize(RawDataArchiveReader);

		FSparseVolumeTextureDataAddressingInfo AddressingInfo{};
		AddressingInfo.VolumeResolution = VolumeResolution;
		AddressingInfo.AddressX = AddressX;
		AddressingInfo.AddressY = AddressY;
		AddressingInfo.AddressZ = AddressZ;

		const int32 NumMipLevels = 1; // generate entire mip chain
		const bool bMoveMip0FromSource = true; // we have no need to keep TextureData around
		if (!TextureData.BuildDerivedData(AddressingInfo, NumMipLevels, bMoveMip0FromSource, *OutMippedTextureData))
		{
			return false;
		}

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
				FSparseVolumeTextureData TextureData;
				TextureData.Serialize(BulkDataReader);
				bool bSuccess = SparseVolumeTextureSceneProxy->GetRuntimeData().Create(TextureData);
				check(bSuccess); // SVT_TODO
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

		FSparseVolumeTextureData DerivedData;
		const bool bBuiltDerivedData = BuildDerivedData(Owner->VolumeResolution, Owner->AddressX, Owner->AddressY, Owner->AddressZ, &DerivedData);
		check(bBuiltDerivedData); // SVT_TODO: actual error handling

		// Write derived data into RuntimeStreamedInData
		{
			FBulkDataWriter BulkDataWriter(RuntimeStreamedInData);
			DerivedData.Serialize(BulkDataWriter);
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
		const FSparseVolumeTextureRuntimeHeader& Header = Proxy->GetHeader();
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
	FIntVector3 PageTableOffset = FIntVector3::ZeroValue;
	FVector3f TileDataTexelSize = FVector3f(0.0f, 0.0f, 0.0f);
	int32 MinMipLevel = 0;
	int32 MaxMipLevel = 0;
	const FSparseVolumeTextureSceneProxy* Proxy = GetSparseVolumeTextureSceneProxy();
	if (Proxy)
	{
		const FSparseVolumeTextureRuntimeHeader& Header = Proxy->GetHeader();
		PageTableOffset = Header.PageTableVolumeAABBMin;
		TileDataTexelSize.X = 1.0f / Header.TileDataVolumeResolution.X;
		TileDataTexelSize.Y = 1.0f / Header.TileDataVolumeResolution.Y;
		TileDataTexelSize.Z = 1.0f / Header.TileDataVolumeResolution.Z;
		MinMipLevel = Header.LowestResidentLevel;
		MaxMipLevel = Header.HighestResidentLevel;
	}
	const FIntVector3 VolumeResolution = GetVolumeResolution();
	const FVector3f VolumePageResolution = FVector3f(VolumeResolution) / SPARSE_VOLUME_TILE_RES;

	auto AsUint = [](float X)
	{
		union { float F; uint32 U; } FU = { X };
		return FU.U;
	};

	OutPacked0.X = AsUint(VolumePageResolution.X);
	OutPacked0.Y = AsUint(VolumePageResolution.Y);
	OutPacked0.Z = AsUint(VolumePageResolution.Z);
	OutPacked0.W = UE::SVT::PackPageTableEntry(PageTableOffset);
	OutPacked1.X = AsUint(TileDataTexelSize.X);
	OutPacked1.Y = AsUint(TileDataTexelSize.Y);
	OutPacked1.Z = AsUint(TileDataTexelSize.Z);
	OutPacked1.W = 0;
	OutPacked1.W |= (uint32)((MinMipLevel & 0xFF) << 0);
	OutPacked1.W |= (uint32)((MaxMipLevel & 0xFF) << 8);
	OutPacked1.W |= (uint32)((int32(SPARSE_VOLUME_TILE_RES) & 0xFF) << 16);
	OutPacked1.W |= (uint32)((int32(SPARSE_VOLUME_TILE_BORDER) & 0xFF) << 24);
}

void USparseVolumeTexture::GetFrameUVScaleBias(FVector* OutScale, FVector* OutBias) const
{
	*OutScale = FVector::One();
	*OutBias = FVector::Zero();
	const FSparseVolumeTextureSceneProxy* Proxy = GetSparseVolumeTextureSceneProxy();
	if (Proxy)
	{
		const FSparseVolumeTextureRuntimeHeader& Header = Proxy->GetHeader();
		const FVector GlobalVolumeRes = FVector(GetVolumeResolution());
		check(GlobalVolumeRes.X > 0.0 && GlobalVolumeRes.Y > 0.0 && GlobalVolumeRes.Z > 0.0);
		const FVector FrameBoundsPaddedMin = FVector(Header.PageTableVolumeAABBMin * SPARSE_VOLUME_TILE_RES); // padded to multiple of page size
		const FVector FrameBoundsPaddedMax = FVector(Header.PageTableVolumeAABBMax * SPARSE_VOLUME_TILE_RES);
		const FVector FramePaddedSize = FrameBoundsPaddedMax - FrameBoundsPaddedMin;

		*OutScale = GlobalVolumeRes / FramePaddedSize; // scale from SVT UV space to frame (padded) local UV space
		*OutBias = -(FrameBoundsPaddedMin / GlobalVolumeRes * *OutScale);
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
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UStreamableSparseVolumeTexture, AddressX)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UStreamableSparseVolumeTexture, AddressY)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UStreamableSparseVolumeTexture, AddressZ))
	{
		// SVT need to recompile shaders when address mode changes
		NotifyMaterials();
	}

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

const FSparseVolumeTextureSceneProxy* UStreamableSparseVolumeTexture::GetStreamedFrameProxyOrFallback(int32 FrameIndex, int32 MipLevel) const
{
	ISparseVolumeTextureStreamingManager& StreamingManager = IStreamingManager::Get().GetSparseVolumeTextureStreamingManager();
	const FSparseVolumeTextureSceneProxy* Proxy = StreamingManager.GetSparseVolumeTextureSceneProxy(this, FrameIndex, MipLevel, true);

	int32 FallbackFrameIndex = FrameIndex;
	while (!Proxy)
	{
		FallbackFrameIndex = FallbackFrameIndex > 0 ? (FallbackFrameIndex - 1) : (Frames.Num() - 1);
		if (FallbackFrameIndex == FrameIndex)
		{
			UE_LOG(LogSparseVolumeTexture, Warning, TEXT("Failed to get ANY streamed SparseVolumeTexture frame  SVT: %s, FrameIndex: %i"), *GetName(), FrameIndex);
			return nullptr;
		}
		Proxy = StreamingManager.GetSparseVolumeTextureSceneProxy(this, FallbackFrameIndex, MipLevel, false);
	}

	return Proxy;
}

TArrayView<const FSparseVolumeTextureFrame> UStreamableSparseVolumeTexture::GetFrames() const
{
	return Frames;
}

#if WITH_EDITOR
void UStreamableSparseVolumeTexture::NotifyMaterials(const ENotifyMaterialsEffectOnShaders EffectOnShaders)
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
#endif //WITH_EDITOR

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
	const FString DerivedDataKey = Frame.RawData.GetIdentifier().ToString() 
		+ FString::Format(TEXT("{0},{1},{2}"), {AddressX.GetIntValue(), AddressY.GetIntValue(), AddressZ.GetIntValue()}) 
		+ SparseVolumeTextureDDCVersion;

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
						FSparseVolumeTextureData TextureData;
						TextureData.Serialize(Ar);
						bool bSuccess = Frame.SparseVolumeTextureSceneProxy->GetRuntimeData().Create(TextureData);
						check(bSuccess); // SVT_TODO
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
							FSparseVolumeTextureData TextureData;
							bool bSuccess = Frame.BuildDerivedData(VolumeResolution, AddressX, AddressY, AddressZ, &TextureData);
							ensure(bSuccess);

							bSuccess = RuntimeData.Create(TextureData);
							ensure(bSuccess);

							// Using a LargeMemoryWriter for serialization since the data can be bigger than 2 GB
							FLargeMemoryWriter LargeMemWriter(0, /*bIsPersistent=*/ true);
							TextureData.Serialize(LargeMemWriter);

							const int64 UncompressedSize = LargeMemWriter.TotalSize();

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
						}
						else
						{
							UE_LOG(LogSparseVolumeTexture, Error, TEXT("SparseVolumeTexture - Raw source data is not available for %s. Using default data."), *GetName());
							RuntimeData.SetAsDefaultTexture();
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
	const int32 MipLevel = FMath::Clamp(PreviewMipLevel, 0, GetNumMipLevels() - 1);
	return GetStreamedFrameProxyOrFallback(FrameIndex, MipLevel);
}

////////////////////////////////////////////////////////////////////////////////////////////////

USparseVolumeTextureFrame::USparseVolumeTextureFrame(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USparseVolumeTextureFrame* USparseVolumeTextureFrame::CreateFrame(USparseVolumeTexture* Texture, int32 FrameIndex, int32 MipLevel)
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
		Proxy = StreamableSVT->GetStreamedFrameProxyOrFallback(FrameIndex, MipLevel);
	}
	else
	{
		Proxy = Texture->GetSparseVolumeTextureSceneProxy();
	}

	if (Proxy)
	{
		USparseVolumeTextureFrame* Frame = NewObject<USparseVolumeTextureFrame>();
		Frame->Initialize(Proxy, Texture->GetVolumeResolution(), Texture->GetTextureAddressX(), Texture->GetTextureAddressY(), Texture->GetTextureAddressZ());
		return Frame;
	}
	
	return nullptr;
}

void USparseVolumeTextureFrame::Initialize(const FSparseVolumeTextureSceneProxy* InSceneProxy, const FIntVector& InVolumeResolution, TextureAddress InAddressX, TextureAddress InAddressY, TextureAddress InAddressZ)
{
	SceneProxy = InSceneProxy;
	VolumeResolution = InVolumeResolution;
	AddressX = InAddressX;
	AddressY = InAddressY;
	AddressZ = InAddressZ;
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

	const int32 FrameCount = SparseVolumeTexture->GetNumFrames();
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

	const int32 FrameCount = SparseVolumeTexture->GetNumFrames();
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
	USparseVolumeTextureFrame* Frame = USparseVolumeTextureFrame::CreateFrame(SparseVolumeTexture, FrameIndex, MipLevel);

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
	Frame0 = USparseVolumeTextureFrame::CreateFrame(SparseVolumeTexture, FrameIndex, MipLevel);
	Frame1 = USparseVolumeTextureFrame::CreateFrame(SparseVolumeTexture, FrameIndex + 1, MipLevel);
}

float UAnimatedSparseVolumeTextureController::GetDuration()
{
	if (!SparseVolumeTexture)
	{
		return 0.0f;
	}

	const int32 FrameCount = SparseVolumeTexture->GetNumFrames();
	const float AnimationDuration = FrameCount / (FrameRate + UE_SMALL_NUMBER);
	return AnimationDuration;
}

////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
