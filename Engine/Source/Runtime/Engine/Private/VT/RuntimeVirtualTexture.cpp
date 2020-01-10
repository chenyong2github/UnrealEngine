// Copyright Epic Games, Inc. All Rights Reserved.

#include "VT/RuntimeVirtualTexture.h"

#include "EngineModule.h"
#include "RendererInterface.h"
#include "VT/VirtualTextureBuildSettings.h"
#include "VT/RuntimeVirtualTextureNotify.h"
#include "VT/RuntimeVirtualTextureStreamingProxy.h"
#include "VT/VirtualTextureLevelRedirector.h"
#include "VT/VirtualTextureScalability.h"
#include "VT/UploadingVirtualTexture.h"

namespace
{
	/** Null producer to use as placeholder when no producer has been set on a URuntimeVirtualTexture */
	class FNullVirtualTextureProducer : public IVirtualTexture
	{
	public:
		/** Get a producer description to use with the null producer. */
		static void GetNullProducerDescription(FVTProducerDescription& OutDesc)
		{
			OutDesc.Dimensions = 2;
			OutDesc.TileSize = 4;
			OutDesc.TileBorderSize = 0;
			OutDesc.BlockWidthInTiles = 1;
			OutDesc.BlockHeightInTiles = 1;
			OutDesc.MaxLevel = 1;
			OutDesc.DepthInTiles = 1;
			OutDesc.WidthInBlocks = 1;
			OutDesc.HeightInBlocks = 1;
			OutDesc.NumTextureLayers = 0;
			OutDesc.NumPhysicalGroups = 0;
		}

		//~ Begin IVirtualTexture Interface.
		virtual FVTRequestPageResult RequestPageData(
			const FVirtualTextureProducerHandle& ProducerHandle,
			uint8 LayerMask,
			uint8 vLevel,
			uint32 vAddress,
			EVTRequestPagePriority Priority
		) override
		{
			return FVTRequestPageResult();
		}

		virtual IVirtualTextureFinalizer* ProducePageData(
			FRHICommandListImmediate& RHICmdList,
			ERHIFeatureLevel::Type FeatureLevel,
			EVTProducePageFlags Flags,
			const FVirtualTextureProducerHandle& ProducerHandle,
			uint8 LayerMask,
			uint8 vLevel,
			uint32 vAddress,
			uint64 RequestHandle,
			const FVTProduceTargetLayer* TargetLayers
		) override 
		{
			return nullptr; 
		}
		//~ End IVirtualTexture Interface.
	};
}


/** 
 * Container for render thread resources created for a URuntimeVirtualTexture object. 
 * Any access to the resources should be on the render thread only so that access is serialized with the Init()/Release() render thread tasks.
 */
class FRuntimeVirtualTextureRenderResource
{
public:
	FRuntimeVirtualTextureRenderResource()
		: AllocatedVirtualTexture(nullptr)
	{
	}

	/** Getter for the virtual texture producer. */
	FVirtualTextureProducerHandle GetProducerHandle() const
	{
		checkSlow(IsInRenderingThread());
		return ProducerHandle;
	}

	/** Getter for the virtual texture allocation. */
	IAllocatedVirtualTexture* GetAllocatedVirtualTexture() const 
	{
		checkSlow(IsInRenderingThread());
		return AllocatedVirtualTexture;
	}

	/** Queues up render thread work to create resources and also releases any old resources. */
	void Init(FVTProducerDescription const& InDesc, IVirtualTexture* InVirtualTextureProducer, bool InPrivateSpace)
	{
		FRuntimeVirtualTextureRenderResource* Resource = this;
		
		ENQUEUE_RENDER_COMMAND(FRuntimeVirtualTextureRenderResource_Init)(
			[Resource, InDesc, InVirtualTextureProducer, InPrivateSpace](FRHICommandList& RHICmdList)
		{
			FVirtualTextureProducerHandle OldProducerHandle = Resource->ProducerHandle;
			ReleaseVirtualTexture(Resource->AllocatedVirtualTexture);
			Resource->ProducerHandle = GetRendererModule().RegisterVirtualTextureProducer(InDesc, InVirtualTextureProducer);
			Resource->AllocatedVirtualTexture = AllocateVirtualTexture(InDesc, Resource->ProducerHandle, InPrivateSpace);
			// Release old producer after new one is created so that any destroy callbacks can access the new producer
			GetRendererModule().ReleaseVirtualTextureProducer(OldProducerHandle);
		});
	}

	/** Queues up render thread work to release resources. */
	void Release()
	{
		FVirtualTextureProducerHandle ProducerHandleToRelease = ProducerHandle;
		ProducerHandle = FVirtualTextureProducerHandle();
		IAllocatedVirtualTexture* AllocatedVirtualTextureToRelease = AllocatedVirtualTexture;
		AllocatedVirtualTexture = nullptr;

		ENQUEUE_RENDER_COMMAND(FRuntimeVirtualTextureRenderResource_Release)(
			[ProducerHandleToRelease, AllocatedVirtualTextureToRelease](FRHICommandList& RHICmdList)
		{
			ReleaseVirtualTexture(AllocatedVirtualTextureToRelease);
			GetRendererModule().ReleaseVirtualTextureProducer(ProducerHandleToRelease);
		});
	}

protected:
	/** Allocate in the global virtual texture system. */
	static IAllocatedVirtualTexture* AllocateVirtualTexture(FVTProducerDescription const& InDesc, FVirtualTextureProducerHandle const& InProducerHandle, bool InPrivateSpace)
	{
		IAllocatedVirtualTexture* OutAllocatedVirtualTexture = nullptr;

		// Check for NumLayers avoids allocating for the null producer
		if (InDesc.NumTextureLayers > 0)
		{
			FAllocatedVTDescription VTDesc;
			VTDesc.Dimensions = InDesc.Dimensions;
			VTDesc.TileSize = InDesc.TileSize;
			VTDesc.TileBorderSize = InDesc.TileBorderSize;
			VTDesc.NumTextureLayers = InDesc.NumTextureLayers;
			VTDesc.bPrivateSpace = InPrivateSpace;
			VTDesc.bShareDuplicateLayers = true;

			for (uint32 LayerIndex = 0u; LayerIndex < VTDesc.NumTextureLayers; ++LayerIndex)
			{
				VTDesc.ProducerHandle[LayerIndex] = InProducerHandle;
				VTDesc.ProducerLayerIndex[LayerIndex] = LayerIndex;
			}

			OutAllocatedVirtualTexture = GetRendererModule().AllocateVirtualTexture(VTDesc);
		}

		return OutAllocatedVirtualTexture;
	}

	/** Release our virtual texture allocations  */
	static void ReleaseVirtualTexture(IAllocatedVirtualTexture* InAllocatedVirtualTexture)
	{
		if (InAllocatedVirtualTexture != nullptr)
		{
			GetRendererModule().DestroyVirtualTexture(InAllocatedVirtualTexture);
		}
	}

private:
	FVirtualTextureProducerHandle ProducerHandle;
	IAllocatedVirtualTexture* AllocatedVirtualTexture;
};


URuntimeVirtualTextureStreamingProxy::URuntimeVirtualTextureStreamingProxy(const FObjectInitializer& ObjectInitializer)
	: UTexture2D(ObjectInitializer)
	, BuildHash(0)
{
}

void URuntimeVirtualTextureStreamingProxy::GetVirtualTextureBuildSettings(FVirtualTextureBuildSettings& OutSettings) const
{
	OutSettings = Settings;
}

#if WITH_EDITOR

void URuntimeVirtualTextureStreamingProxy::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	// Even though we skip the cook of this object for non VT platforms in URuntimeVirtualTexture::Serialize()
	// we still load the object at cook time and kick off the DDC build. This will trigger an error in the texture DDC code.
	// Either we need to make the DDC code more robust for non VT platforms or we can skip the process here...
	if (!UseVirtualTexturing(GMaxRHIFeatureLevel, TargetPlatform))
	{
		return;
	}

	Super::BeginCacheForCookedPlatformData(TargetPlatform);
}

#endif


URuntimeVirtualTexture::URuntimeVirtualTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Initialize the RHI resources with a null producer
	Resource = new FRuntimeVirtualTextureRenderResource;
	InitNullResource();
}

URuntimeVirtualTexture::~URuntimeVirtualTexture()
{
	Resource->Release();
	delete Resource;
}

void URuntimeVirtualTexture::GetProducerDescription(FVTProducerDescription& OutDesc, FTransform const& VolumeToWorld) const
{
	OutDesc.Name = GetFName();
	OutDesc.Dimensions = 2;
	OutDesc.TileSize = GetTileSize();
	OutDesc.TileBorderSize = GetTileBorderSize();
	OutDesc.DepthInTiles = 1;
	OutDesc.WidthInBlocks = 1;
	OutDesc.HeightInBlocks = 1;

	// Apply TileCount modifier here to allow size scalability option
	const int32 TileCountBias = VirtualTextureScalability::GetRuntimeVirtualTextureSizeBias();
	const int32 MaxSizeInTiles = GetTileCount(TileCount + TileCountBias);

	// Set width and height to best match the runtime virtual texture volume's aspect ratio
	const FVector VolumeSize = VolumeToWorld.GetScale3D();
	const float VolumeSizeX = FMath::Max(FMath::Abs(VolumeSize.X), 0.0001f);
	const float VolumeSizeY = FMath::Max(FMath::Abs(VolumeSize.Y), 0.0001f);
	const float AspectRatioLog2 = FMath::Log2(VolumeSizeX / VolumeSizeY);

	uint32 WidthInTiles, HeightInTiles;
	if (AspectRatioLog2 >= 0.f)
	{
		WidthInTiles = MaxSizeInTiles;
		HeightInTiles = FMath::Max(WidthInTiles >> FMath::RoundToInt(AspectRatioLog2), 1u);
	}
	else
	{
		HeightInTiles = MaxSizeInTiles;
		WidthInTiles = FMath::Max(HeightInTiles >> FMath::RoundToInt(-AspectRatioLog2), 1u);
	}

	OutDesc.BlockWidthInTiles = WidthInTiles;
	OutDesc.BlockHeightInTiles = HeightInTiles;
	OutDesc.MaxLevel = FMath::Max((int32)FMath::CeilLogTwo(FMath::Max(OutDesc.BlockWidthInTiles, OutDesc.BlockHeightInTiles)) - GetRemoveLowMips(), 0);

	OutDesc.NumTextureLayers = GetLayerCount();
	OutDesc.NumPhysicalGroups = bSinglePhysicalSpace ? 1 : GetLayerCount();

	for (int32 Layer = 0; Layer < OutDesc.NumTextureLayers; Layer++)
	{
		OutDesc.LayerFormat[Layer] = GetLayerFormat(Layer);
		OutDesc.PhysicalGroupIndex[Layer] = bSinglePhysicalSpace ? 0 : Layer;
	}
}

int32 URuntimeVirtualTexture::GetLayerCount(ERuntimeVirtualTextureMaterialType InMaterialType)
{
	switch (InMaterialType)
	{
	case ERuntimeVirtualTextureMaterialType::BaseColor:
	case ERuntimeVirtualTextureMaterialType::WorldHeight:
		return 1;
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:
		return 2;
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg:
		return 3;
	default:
		break;
	}

	// Implement logic for any missing material types
	check(false);
	return 1;
}

int32 URuntimeVirtualTexture::GetLayerCount() const
{
	return GetLayerCount(MaterialType);
}

EPixelFormat URuntimeVirtualTexture::GetLayerFormat(int32 LayerIndex) const
{
	if (LayerIndex == 0)
	{
		switch (MaterialType)
		{
		case ERuntimeVirtualTextureMaterialType::BaseColor:
			return bCompressTextures ? PF_DXT1 : PF_B8G8R8A8;
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg:
			return bCompressTextures ? PF_DXT5 : PF_B8G8R8A8;
		case ERuntimeVirtualTextureMaterialType::WorldHeight:
			return PF_G16;
		default:
			break;
		}
	}
	else if (LayerIndex == 1)
	{
		switch (MaterialType)
		{
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:
			return bCompressTextures ? PF_DXT5 : PF_B8G8R8A8;
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg:
			return bCompressTextures ? PF_BC5 : PF_B8G8R8A8;
		default:
			break;
		}
	}
	else if (LayerIndex == 2)
	{
		switch (MaterialType)
		{
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
			return bCompressTextures ? PF_DXT1 : PF_B8G8R8A8;
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg:
			return bCompressTextures ? PF_DXT5 : PF_B8G8R8A8;
		default:
			break;
		}
	}

	// Implement logic for any missing material types
	check(false);
	return PF_B8G8R8A8;
}

bool URuntimeVirtualTexture::IsLayerSRGB(int32 LayerIndex) const
{
	switch (MaterialType)
	{
	case ERuntimeVirtualTextureMaterialType::BaseColor:
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:
		// Only BaseColor layer is sRGB
		return LayerIndex == 0;
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg:
	case ERuntimeVirtualTextureMaterialType::WorldHeight:
		return false;
	default:
		break;
	}

	// Implement logic for any missing material types
	check(false);
	return false;
}

bool URuntimeVirtualTexture::IsLayerYCoCg(int32 LayerIndex) const
{
	switch (MaterialType)
	{
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg:
		return LayerIndex == 0;
	default:
		break;
	}
	return false;
}

int32 URuntimeVirtualTexture::GetEstimatedPageTableTextureMemoryKb() const
{
	//todo[vt]: Estimate memory usage
	return 0;
}

int32 URuntimeVirtualTexture::GetEstimatedPhysicalTextureMemoryKb() const
{
	//todo[vt]: Estimate memory usage
	return 0;
}

FVirtualTextureProducerHandle URuntimeVirtualTexture::GetProducerHandle() const
{
	return Resource->GetProducerHandle();
}

IAllocatedVirtualTexture* URuntimeVirtualTexture::GetAllocatedVirtualTexture() const
{
	return Resource->GetAllocatedVirtualTexture();
}

FVector4 URuntimeVirtualTexture::GetUniformParameter(int32 Index) const
{
	check(Index >= 0 && Index < sizeof(WorldToUVTransformParameters)/sizeof(WorldToUVTransformParameters[0]));
	
	return WorldToUVTransformParameters[Index];
}

void URuntimeVirtualTexture::Initialize(IVirtualTexture* InProducer, FTransform const& VolumeToWorld)
{
	//todo[vt]: possible issues with precision in large worlds here it might be better to calculate/upload camera space relative transform per frame?
	WorldToUVTransformParameters[0] = VolumeToWorld.GetTranslation();
	WorldToUVTransformParameters[1] = VolumeToWorld.GetUnitAxis(EAxis::X) * 1.f / VolumeToWorld.GetScale3D().X;
	WorldToUVTransformParameters[2] = VolumeToWorld.GetUnitAxis(EAxis::Y) * 1.f / VolumeToWorld.GetScale3D().Y;

	InitResource(InProducer, VolumeToWorld);
}

void URuntimeVirtualTexture::Release()
{
	InitNullResource();
}

void URuntimeVirtualTexture::InitResource(IVirtualTexture* InProducer, FTransform const& VolumeToWorld)
{
	FVTProducerDescription Desc;
	GetProducerDescription(Desc, VolumeToWorld);
	Resource->Init(Desc, InProducer, bPrivateSpace);
}

void URuntimeVirtualTexture::InitNullResource()
{
	FVTProducerDescription Desc;
	FNullVirtualTextureProducer::GetNullProducerDescription(Desc);
	FNullVirtualTextureProducer* Producer = new FNullVirtualTextureProducer;
	Resource->Init(Desc, Producer, false);
}

void URuntimeVirtualTexture::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);

	OutTags.Add(FAssetRegistryTag("Size", FString::FromInt(GetSize()), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("TileCount", FString::FromInt(GetTileCount()), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("TileSize", FString::FromInt(GetTileSize()), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("TileBorderSize", FString::FromInt(GetTileBorderSize()), FAssetRegistryTag::TT_Numerical));
}

void URuntimeVirtualTexture::Serialize(FArchive& Ar)
{
	if (Ar.IsCooking() && Ar.IsSaving() && !UseVirtualTexturing(GMaxRHIFeatureLevel, Ar.CookingTarget()))
	{
		// Clear StreamingTexture during cook for platforms that don't support virtual texturing
		URuntimeVirtualTextureStreamingProxy* StreamingTextureBackup = StreamingTexture;
		StreamingTexture = nullptr;
		Super::Serialize(Ar);
		StreamingTexture = StreamingTextureBackup;
	}
	else
	{
		Super::Serialize(Ar);
	}
}

void URuntimeVirtualTexture::PostLoad()
{
	// Convert Size_DEPRECATED to TileCount
	if (Size_DEPRECATED >= 0)
	{
		int32 OldSize = 1 << FMath::Clamp(Size_DEPRECATED + 10, 10, 18);
		int32 TileCountFromSize = FMath::Max(OldSize / GetTileSize(), 1);
		TileCount = FMath::FloorLog2(TileCountFromSize);
		Size_DEPRECATED = -1;
	}

	// Convert BaseColor_Normal_DEPRECATED
	if (MaterialType == ERuntimeVirtualTextureMaterialType::BaseColor_Normal_DEPRECATED)
	{
		MaterialType = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular;
	}

	Super::PostLoad();
}

#if WITH_EDITOR

void URuntimeVirtualTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Invalidate StreamingTexture if it is no longer compatible with this object
	if (StreamingTexture != nullptr && StreamingTexture->BuildHash != GetStreamingTextureBuildHash())
	{
		StreamingTexture = nullptr;
	}

	RuntimeVirtualTexture::NotifyComponents(this);
	RuntimeVirtualTexture::NotifyPrimitives(this);
}

uint32 URuntimeVirtualTexture::GetStreamingTextureBuildHash() const
{
	union FPackedSettings
	{
		uint32 PackedValue;
		struct
		{
			uint32 MaterialType : 3;
			uint32 CompressTextures : 1;
			uint32 SinglePhysicalSpace : 1;
			uint32 TileSize : 4;
			uint32 TileBorderSize : 4;
			uint32 StreamLowMips : 4;
			uint32 EnableCompressCrunch : 1;
		};
	};

	FPackedSettings Settings;
	Settings.PackedValue = 0;
	Settings.MaterialType = (uint32)MaterialType;
	Settings.CompressTextures = (uint32)bCompressTextures;
	Settings.SinglePhysicalSpace = (uint32)bSinglePhysicalSpace;
	Settings.TileSize = (uint32)TileSize;
	Settings.TileBorderSize = (uint32)TileBorderSize;
	Settings.StreamLowMips = (uint32)GetStreamLowMips();
	Settings.EnableCompressCrunch = (uint32)bEnableCompressCrunch;

	return Settings.PackedValue;
}

void URuntimeVirtualTexture::InitializeStreamingTexture(uint32 InSizeX, uint32 InSizeY, uint8* InData)
{
	// Release current producer. 
	// It may reference data inside StreamingTexture which could be garbage collected any time from now.
	InitNullResource();

	StreamingTexture = NewObject<URuntimeVirtualTextureStreamingProxy>(GetOutermost(), TEXT("StreamingTexture"));
	StreamingTexture->VirtualTextureStreaming = true;
	StreamingTexture->bSinglePhysicalSpace = bSinglePhysicalSpace;

	StreamingTexture->Settings.Init();
	StreamingTexture->Settings.TileSize = GetTileSize();
	StreamingTexture->Settings.TileBorderSize = GetTileBorderSize();
	StreamingTexture->Settings.bEnableCompressCrunch = bEnableCompressCrunch;

	StreamingTexture->BuildHash = GetStreamingTextureBuildHash();

	const int32 LayerCount = GetLayerCount();
	check(LayerCount <= RuntimeVirtualTexture::MaxTextureLayers);
	ETextureSourceFormat LayerFormats[RuntimeVirtualTexture::MaxTextureLayers];

	for (int32 Layer = 0; Layer < LayerCount; Layer++)
	{
		EPixelFormat LayerFormat = GetLayerFormat(Layer);
		LayerFormats[Layer] = LayerFormat == PF_G16 ? TSF_G16 : TSF_BGRA8;

		FTextureFormatSettings FormatSettings;
		FormatSettings.CompressionSettings = LayerFormat == PF_BC5 ? TC_Normalmap : TC_Default;
		FormatSettings.CompressionNone = LayerFormat == PF_B8G8R8A8 || LayerFormat == PF_G16;
		FormatSettings.CompressionNoAlpha = LayerFormat == PF_DXT1 || LayerFormat == PF_BC5;
		FormatSettings.CompressionYCoCg = IsLayerYCoCg(Layer);
		FormatSettings.SRGB = IsLayerSRGB(Layer);
		
		StreamingTexture->SetLayerFormatSettings(Layer, FormatSettings);
	}

	StreamingTexture->Source.InitLayered(InSizeX, InSizeY, 1, LayerCount, 1, LayerFormats, InData);

	StreamingTexture->PostEditChange();
}

#endif

IVirtualTexture* URuntimeVirtualTexture::CreateStreamingTextureProducer(IVirtualTexture* InProducer, int32 InMaxLevel, int32& OutTransitionLevel) const
{
	if (StreamingTexture != nullptr)
	{
		FTexturePlatformData** StreamingTextureData = StreamingTexture->GetRunningPlatformData();
		if (StreamingTextureData != nullptr && *StreamingTextureData != nullptr)
		{
			FVirtualTextureBuiltData* VTData = (*StreamingTextureData)->VTData;
			check(GetTileSize() == VTData->TileSize);
			check(GetTileBorderSize() == VTData->TileBorderSize);

			// Streaming data may have mips removed during cook
			const int32 NumStreamMips = FMath::Min(GetStreamLowMips(), (*StreamingTextureData)->GetNumVTMips());

			OutTransitionLevel = FMath::Max(InMaxLevel - NumStreamMips + 1, 0);
			IVirtualTexture* StreamingProducer = new FUploadingVirtualTexture(VTData, 0);
			return new FVirtualTextureLevelRedirector(InProducer, StreamingProducer, OutTransitionLevel);
		}
	}
	// Can't create a streaming producer so return original producer.
	OutTransitionLevel = InMaxLevel;
	return InProducer;
}
