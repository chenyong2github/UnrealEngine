// Copyright Epic Games, Inc. All Rights Reserved.

#include "VT/RuntimeVirtualTexture.h"

#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "EngineModule.h"
#include "Engine/TextureLODSettings.h"
#include "Interfaces/ITargetPlatform.h"
#include "RendererInterface.h"
#include "VT/RuntimeVirtualTextureNotify.h"
#include "VT/VirtualTexture.h"
#include "VT/VirtualTextureLevelRedirector.h"
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
	void Init(FVTProducerDescription const& InDesc, IVirtualTexture* InVirtualTextureProducer, bool InSinglePhysicalSpace, bool InPrivateSpace)
	{
		FRuntimeVirtualTextureRenderResource* Resource = this;
		
		ENQUEUE_RENDER_COMMAND(FRuntimeVirtualTextureRenderResource_Init)(
			[Resource, InDesc, InVirtualTextureProducer, InSinglePhysicalSpace, InPrivateSpace](FRHICommandList& RHICmdList)
		{
			FVirtualTextureProducerHandle OldProducerHandle = Resource->ProducerHandle;
			ReleaseVirtualTexture(Resource->AllocatedVirtualTexture);
			Resource->ProducerHandle = GetRendererModule().RegisterVirtualTextureProducer(InDesc, InVirtualTextureProducer);
			Resource->AllocatedVirtualTexture = AllocateVirtualTexture(InDesc, Resource->ProducerHandle, InSinglePhysicalSpace, InPrivateSpace);
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
	static IAllocatedVirtualTexture* AllocateVirtualTexture(FVTProducerDescription const& InDesc, FVirtualTextureProducerHandle const& InProducerHandle, bool InSinglePhysicalSpace, bool InPrivateSpace)
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
			VTDesc.bShareDuplicateLayers = InSinglePhysicalSpace;

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

void URuntimeVirtualTexture::GetProducerDescription(FVTProducerDescription& OutDesc, FInitSettings const& InitSettings, FTransform const& VolumeToWorld) const
{
	OutDesc.Name = GetFName();
	OutDesc.Dimensions = 2;
	OutDesc.DepthInTiles = 1;
	OutDesc.WidthInBlocks = 1;
	OutDesc.HeightInBlocks = 1;

	// Apply LODGroup TileSize bias here.
	const int32 TileSizeBias = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetTextureLODGroup(LODGroup).VirtualTextureTileSizeBias;
	OutDesc.TileSize = GetTileSize(TileSize + TileSizeBias);

	OutDesc.TileBorderSize = GetTileBorderSize();

	// Apply TileCount bias here.
	const int32 TileCountBiasFromLodGroup = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetTextureLODGroup(LODGroup).VirtualTextureTileCountBias;
	const int32 MaxSizeInTiles = GetTileCount(TileCount + TileCountBiasFromLodGroup + InitSettings.TileCountBias);

	// Set width and height to best match the runtime virtual texture volume's aspect ratio.
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

	OutDesc.bContinuousUpdate = bContinuousUpdate;

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

static EPixelFormat PlatformCompressedRVTFormat(EPixelFormat Format)
{
	if (GPixelFormats[Format].Supported)
	{
		return Format;
	}
	else if (GPixelFormats[PF_ETC2_RGB].Supported)
	{
		switch(Format)
		{
		case PF_DXT1:
			Format = PF_ETC2_RGB;
			break;
		case PF_DXT5:
			Format = PF_ETC2_RGBA;
			break;
		case PF_BC5:
			Format = PF_ETC2_RG11_EAC;
			break;
		default:
			check(false);
		};
	}

	return Format;
}

EPixelFormat URuntimeVirtualTexture::GetLayerFormat(int32 LayerIndex) const
{
	if (LayerIndex == 0)
	{
		switch (MaterialType)
		{
		case ERuntimeVirtualTextureMaterialType::BaseColor:
			return bCompressTextures ? PlatformCompressedRVTFormat(PF_DXT1) : PF_B8G8R8A8;
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg:
			return bCompressTextures ? PlatformCompressedRVTFormat(PF_DXT5) : PF_B8G8R8A8;
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
			return bCompressTextures ? PlatformCompressedRVTFormat(PF_DXT5) : PF_B8G8R8A8;
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg:
			return bCompressTextures ? PlatformCompressedRVTFormat(PF_BC5) : PF_B8G8R8A8;
		default:
			break;
		}
	}
	else if (LayerIndex == 2)
	{
		switch (MaterialType)
		{
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
			return bCompressTextures ? PlatformCompressedRVTFormat(PF_DXT1) : PF_B8G8R8A8;
		case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg:
			return bCompressTextures ? PlatformCompressedRVTFormat(PF_DXT5) : PF_B8G8R8A8;
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

#if WITH_EDITOR

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

#endif

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
	switch (Index)
	{
	case ERuntimeVirtualTextureShaderUniform_WorldToUVTransform0: return WorldToUVTransformParameters[0];
	case ERuntimeVirtualTextureShaderUniform_WorldToUVTransform1: return WorldToUVTransformParameters[1];
	case ERuntimeVirtualTextureShaderUniform_WorldToUVTransform2: return WorldToUVTransformParameters[2];
	case ERuntimeVirtualTextureShaderUniform_WorldHeightUnpack: return WorldHeightUnpackParameter;
	default:
		break;
	}
	
	check(0);
	return FVector4(ForceInitToZero);
}

void URuntimeVirtualTexture::Initialize(IVirtualTexture* InProducer, FInitSettings const& InitSettings, FTransform const& VolumeToWorld, FBox const& WorldBounds)
{
	//todo[vt]: possible issues with precision in large worlds here it might be better to calculate/upload camera space relative transform per frame?
	WorldToUVTransformParameters[0] = VolumeToWorld.GetTranslation();
	WorldToUVTransformParameters[1] = VolumeToWorld.GetUnitAxis(EAxis::X) * 1.f / VolumeToWorld.GetScale3D().X;
	WorldToUVTransformParameters[2] = VolumeToWorld.GetUnitAxis(EAxis::Y) * 1.f / VolumeToWorld.GetScale3D().Y;

	const float HeightRange = FMath::Max(WorldBounds.Max.Z - WorldBounds.Min.Z, 1.f);
	WorldHeightUnpackParameter = FVector4(HeightRange, WorldBounds.Min.Z, 0.f, 0.f);

	InitResource(InProducer, InitSettings, VolumeToWorld);
}

void URuntimeVirtualTexture::Release()
{
	InitNullResource();
}

void URuntimeVirtualTexture::InitResource(IVirtualTexture* InProducer, FInitSettings const& InitSettings, FTransform const& VolumeToWorld)
{
	FVTProducerDescription Desc;
	GetProducerDescription(Desc, InitSettings, VolumeToWorld);
	Resource->Init(Desc, InProducer, bSinglePhysicalSpace, bPrivateSpace);
}

void URuntimeVirtualTexture::InitNullResource()
{
	FVTProducerDescription Desc;
	FNullVirtualTextureProducer::GetNullProducerDescription(Desc);
	FNullVirtualTextureProducer* Producer = new FNullVirtualTextureProducer;
	Resource->Init(Desc, Producer, false, false);
}

void URuntimeVirtualTexture::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);

	OutTags.Add(FAssetRegistryTag("Size", FString::FromInt(GetSize()), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("TileCount", FString::FromInt(GetTileCount()), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("TileSize", FString::FromInt(GetTileSize()), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("TileBorderSize", FString::FromInt(GetTileBorderSize()), FAssetRegistryTag::TT_Numerical));
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

	// Remove StreamingTexture_DEPRECATED
	StreamingTexture_DEPRECATED = nullptr;

	Super::PostLoad();
}

#if WITH_EDITOR

void URuntimeVirtualTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	RuntimeVirtualTexture::NotifyComponents(this);
	RuntimeVirtualTexture::NotifyPrimitives(this);
}

#endif

namespace RuntimeVirtualTexture
{
	IVirtualTexture* CreateStreamingTextureProducer(
		IVirtualTexture* InProducer,
		FVTProducerDescription const& InProducerDesc,
		UVirtualTexture2D* InStreamingTexture,
		int32 InMaxLevel,
		int32& OutTransitionLevel)
	{
		if (InProducer != nullptr && InStreamingTexture != nullptr)
		{
			FTexturePlatformData** StreamingTextureData = InStreamingTexture->GetRunningPlatformData();
			if (StreamingTextureData != nullptr && *StreamingTextureData != nullptr)
			{
				FVirtualTextureBuiltData* VTData = (*StreamingTextureData)->VTData;

				ensure(InProducerDesc.TileSize == VTData->TileSize);
				ensure(InProducerDesc.TileBorderSize == VTData->TileBorderSize);
				if (InProducerDesc.TileSize == VTData->TileSize && InProducerDesc.TileBorderSize == VTData->TileBorderSize)
				{
					// Note that streaming data may have mips added/removed during cook.
					const uint32 Size = FMath::Max(VTData->Width, VTData->Height);
					const uint32 NumTiles = FMath::DivideAndRoundUp(Size, VTData->TileSize);
					const uint32 NumMips = FMath::CeilLogTwo(NumTiles) + 1;

					// If the streaming texture is bigger then the runtime virtual texture then offset the first mip.
					const int32 TransitionLevel = InMaxLevel - (int32)NumMips + 1;
					const int32 FirstStreamingMip = TransitionLevel < 0 ? -TransitionLevel : 0;
					const int32 AdjustedTransitionLevel = TransitionLevel + FirstStreamingMip;
					OutTransitionLevel = TransitionLevel;

					IVirtualTexture* StreamingProducer = new FUploadingVirtualTexture(VTData, FirstStreamingMip);
					return new FVirtualTextureLevelRedirector(InProducer, StreamingProducer, AdjustedTransitionLevel);
				}
			}
		}

		// Can't create a streaming producer so return original producer.
		OutTransitionLevel = InMaxLevel;
		return InProducer;
	}
}
