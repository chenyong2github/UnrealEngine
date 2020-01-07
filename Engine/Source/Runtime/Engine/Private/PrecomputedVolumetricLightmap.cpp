// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PrecomputedVolumetricLightmap.cpp
=============================================================================*/

#include "PrecomputedVolumetricLightmap.h"
#include "Stats/Stats.h"
#include "EngineDefines.h"
#include "UObject/RenderingObjectVersion.h"
#include "SceneManagement.h"
#include "UnrealEngine.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Interfaces/ITargetPlatform.h"
#include "UObject/MobileObjectVersion.h"
#include "RenderGraphUtils.h"
// FIXME: temp fix for ordering issue between WorldContext.World()->InitWorld(); and GShaderCompilingManager->ProcessAsyncResults(false, true); in UnrealEngine.cpp
#include "ShaderCompiler.h"

DECLARE_MEMORY_STAT(TEXT("Volumetric Lightmap"),STAT_VolumetricLightmapBuildData,STATGROUP_MapBuildData);

void FVolumetricLightmapDataLayer::CreateTexture(FIntVector Dimensions)
{
	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.BulkData = this;
	CreateInfo.DebugName = TEXT("VolumetricLightmap");

	Texture = RHICreateTexture3D(
		Dimensions.X, 
		Dimensions.Y, 
		Dimensions.Z, 
		Format,
		1,
		TexCreate_ShaderResource | TexCreate_DisableAutoDefrag | TexCreate_UAV,
		CreateInfo);
}

void FVolumetricLightmapDataLayer::CreateTargetTexture(FIntVector Dimensions)
{
	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.DebugName = TEXT("VolumetricLightmap");

	Texture = RHICreateTexture3D(
		Dimensions.X,
		Dimensions.Y,
		Dimensions.Z,
		Format,
		1,
		TexCreate_ShaderResource | TexCreate_DisableAutoDefrag | TexCreate_UAV,
		CreateInfo);
}

void FVolumetricLightmapDataLayer::CreateUAV()
{
	check(Texture);

	UAV = RHICreateUnorderedAccessView(Texture);
}

struct FVolumetricLightmapBrickTextureSet
{
	FIntVector BrickDataDimensions;

	FVolumetricLightmapDataLayer AmbientVector;
	FVolumetricLightmapDataLayer SHCoefficients[6];
	FVolumetricLightmapDataLayer SkyBentNormal;
	FVolumetricLightmapDataLayer DirectionalLightShadowing;

	template<class VolumetricLightmapBrickDataType> // Can be either FVolumetricLightmapBrickData or FVolumetricLightmapBrickTextureSet
	void Initialize(FIntVector InBrickDataDimensions, VolumetricLightmapBrickDataType& BrickData)
	{
		BrickDataDimensions = InBrickDataDimensions;

		AmbientVector.Format = BrickData.AmbientVector.Format;
		SkyBentNormal.Format = BrickData.SkyBentNormal.Format;
		DirectionalLightShadowing.Format = BrickData.DirectionalLightShadowing.Format;

		for (int32 i = 0; i < UE_ARRAY_COUNT(SHCoefficients); i++)
		{
			SHCoefficients[i].Format = BrickData.SHCoefficients[i].Format;
		}

		AmbientVector.CreateTargetTexture(BrickDataDimensions);
		AmbientVector.CreateUAV();

		for (int32 i = 0; i < UE_ARRAY_COUNT(SHCoefficients); i++)
		{
			SHCoefficients[i].CreateTargetTexture(BrickDataDimensions);
			SHCoefficients[i].CreateUAV();
		}

		if (BrickData.SkyBentNormal.Texture.IsValid())
		{
			SkyBentNormal.CreateTargetTexture(BrickDataDimensions);
			SkyBentNormal.CreateUAV();
		}

		DirectionalLightShadowing.CreateTargetTexture(BrickDataDimensions);
		DirectionalLightShadowing.CreateUAV();
	}

	void Release()
	{
		AmbientVector.Texture.SafeRelease();
		for (int32 i = 0; i < UE_ARRAY_COUNT(SHCoefficients); i++)
		{
			SHCoefficients[i].Texture.SafeRelease();
		}
		SkyBentNormal.Texture.SafeRelease();
		DirectionalLightShadowing.Texture.SafeRelease();

		AmbientVector.UAV.SafeRelease();
		for (int32 i = 0; i < UE_ARRAY_COUNT(SHCoefficients); i++)
		{
			SHCoefficients[i].UAV.SafeRelease();
		}
		SkyBentNormal.UAV.SafeRelease();
		DirectionalLightShadowing.UAV.SafeRelease();
	}
};

class ENGINE_API FVolumetricLightmapBrickAtlas : public FRenderResource
{
public:
	FVolumetricLightmapBrickAtlas();

	FVolumetricLightmapBrickTextureSet TextureSet;

	virtual void ReleaseRHI() override;

	struct Allocation
	{
		// The data being allocated, as an identifier for the entry
		class FPrecomputedVolumetricLightmapData* Data = nullptr;

		int32 Size = 0;
		int32 StartOffset = 0;
	};

	TArray<Allocation> Allocations;

	void Insert(int32 Index, FPrecomputedVolumetricLightmapData* Data);
	void Remove(FPrecomputedVolumetricLightmapData* Data);

private:
	bool bInitialized;
	int32 PaddedBrickSize;
};

TGlobalResource<FVolumetricLightmapBrickAtlas> GVolumetricLightmapBrickAtlas = TGlobalResource<FVolumetricLightmapBrickAtlas>();

inline void ConvertBGRA8ToRGBA8ForLayer(FVolumetricLightmapDataLayer& Layer)
{
	if (Layer.Format == PF_B8G8R8A8)
	{
		for (int32 PixelIndex = 0; PixelIndex < Layer.Data.Num() / GPixelFormats[PF_B8G8R8A8].BlockBytes; PixelIndex++)
		{
			FColor Color;

			Color.B = Layer.Data[PixelIndex * 4 + 0];
			Color.G = Layer.Data[PixelIndex * 4 + 1];
			Color.R = Layer.Data[PixelIndex * 4 + 2];
			Color.A = Layer.Data[PixelIndex * 4 + 3];

			Layer.Data[PixelIndex * 4 + 0] = Color.R;
			Layer.Data[PixelIndex * 4 + 1] = Color.G;
			Layer.Data[PixelIndex * 4 + 2] = Color.B;
			Layer.Data[PixelIndex * 4 + 3] = Color.A;
		}

		Layer.Format = PF_R8G8B8A8;
	}
}

FArchive& operator<<(FArchive& Ar,FVolumetricLightmapDataLayer& Layer)
{
	Ar << Layer.Data;
	
	if (Ar.IsLoading())
	{
		Layer.DataSize = Layer.Data.Num() * Layer.Data.GetTypeSize();
	}

	UEnum* PixelFormatEnum = UTexture::GetPixelFormatEnum();

	if (Ar.IsLoading())
	{
		FString PixelFormatString;
		Ar << PixelFormatString;
		Layer.Format = (EPixelFormat)PixelFormatEnum->GetValueByName(*PixelFormatString);

		ConvertBGRA8ToRGBA8ForLayer(Layer);
	}
	else if (Ar.IsSaving())
	{
		FString PixelFormatString = PixelFormatEnum->GetNameByValue(Layer.Format).GetPlainNameString();
		Ar << PixelFormatString;
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar,FPrecomputedVolumetricLightmapData& Volume)
{
	Ar.UsingCustomVersion(FMobileObjectVersion::GUID);
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);

	Ar << Volume.Bounds;
	Ar << Volume.IndirectionTextureDimensions;
	Ar << Volume.IndirectionTexture;

	Ar << Volume.BrickSize;
	Ar << Volume.BrickDataDimensions;

	Ar << Volume.BrickData.AmbientVector;

	for (int32 i = 0; i < UE_ARRAY_COUNT(Volume.BrickData.SHCoefficients); i++)
	{
		Ar << Volume.BrickData.SHCoefficients[i];
	}

	Ar << Volume.BrickData.SkyBentNormal;
	Ar << Volume.BrickData.DirectionalLightShadowing;
	
	if (Ar.CustomVer(FMobileObjectVersion::GUID) >= FMobileObjectVersion::LQVolumetricLightmapLayers)
	{
		if (Ar.IsCooking() && !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::LowQualityLightmaps))
		{
			// Don't serialize cooked LQ data if the cook target does not want it.
			FVolumetricLightmapDataLayer Dummy;
			Ar << Dummy;
			Ar << Dummy;
		}
		else
		{
			Ar << Volume.BrickData.LQLightColor;
			Ar << Volume.BrickData.LQLightDirection;
		}
	}

	if (Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::VolumetricLightmapStreaming)
	{
		Ar << Volume.SubLevelBrickPositions;
		Ar << Volume.IndirectionTextureOriginalValues;
	}

	if (Ar.IsLoading())
	{
		if (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5 && !GIsEditor)
		{
			// drop LQ data for SM4+
			Volume.BrickData.DiscardLowQualityLayers();
		}

		Volume.bTransient = false;

		const SIZE_T VolumeBytes = Volume.GetAllocatedBytes();
		INC_DWORD_STAT_BY(STAT_VolumetricLightmapBuildData, VolumeBytes);
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FPrecomputedVolumetricLightmapData*& Volume)
{
	bool bValid = Volume != NULL;

	Ar << bValid;

	if (bValid)
	{
		if (Ar.IsLoading())
		{
			Volume = new FPrecomputedVolumetricLightmapData();
		}

		Ar << (*Volume);
	}

	return Ar;
}

int32 FVolumetricLightmapBrickData::GetMinimumVoxelSize() const
{
	check(AmbientVector.Format != PF_Unknown);
	int32 VoxelSize = GPixelFormats[AmbientVector.Format].BlockBytes;

	for (int32 i = 0; i < UE_ARRAY_COUNT(SHCoefficients); i++)
	{
		VoxelSize += GPixelFormats[SHCoefficients[i].Format].BlockBytes;
	}
		
	// excluding SkyBentNormal because it is conditional

	VoxelSize += GPixelFormats[DirectionalLightShadowing.Format].BlockBytes;

	return VoxelSize;
}

FPrecomputedVolumetricLightmapData::FPrecomputedVolumetricLightmapData()
	: Bounds(EForceInit::ForceInitToZero)
	, bTransient(true)
	, IndirectionTextureDimensions(EForceInit::ForceInitToZero)
	, BrickSize(0)
	, BrickDataDimensions(EForceInit::ForceInitToZero)
	, BrickDataBaseOffsetInAtlas(0)
{}

FPrecomputedVolumetricLightmapData::~FPrecomputedVolumetricLightmapData()
{
	if (!bTransient)
	{
		const SIZE_T VolumeBytes = GetAllocatedBytes();
		DEC_DWORD_STAT_BY(STAT_VolumetricLightmapBuildData, VolumeBytes);
	}
}

/** */
void FPrecomputedVolumetricLightmapData::InitializeOnImport(const FBox& NewBounds, int32 InBrickSize)
{
	Bounds = NewBounds;
	BrickSize = InBrickSize;
}

void FPrecomputedVolumetricLightmapData::FinalizeImport()
{
	bTransient = false;
	const SIZE_T VolumeBytes = GetAllocatedBytes();
	INC_DWORD_STAT_BY(STAT_VolumetricLightmapBuildData, VolumeBytes);
}

ENGINE_API void FPrecomputedVolumetricLightmapData::InitRHI()
{
	if (FeatureLevel >= ERHIFeatureLevel::SM5)
	{
		if (IndirectionTextureDimensions.GetMax() > 0)
		{
			IndirectionTexture.CreateTexture(IndirectionTextureDimensions);
		}

		if (BrickDataDimensions.GetMax() > 0)
		{
			BrickData.AmbientVector.CreateTexture(BrickDataDimensions);

			for (int32 i = 0; i < UE_ARRAY_COUNT(BrickData.SHCoefficients); i++)
			{
				BrickData.SHCoefficients[i].CreateTexture(BrickDataDimensions);
			}

			if (BrickData.SkyBentNormal.Data.Num() > 0)
			{
				BrickData.SkyBentNormal.CreateTexture(BrickDataDimensions);
			}

			BrickData.DirectionalLightShadowing.CreateTexture(BrickDataDimensions);
		}

		GVolumetricLightmapBrickAtlas.Insert(INT_MAX, this);

		// It is now safe to release the brick data used for upload. They will stay in GPU memory until UMapBuildDataRegistry::BeginDestroy().
		BrickData.ReleaseRHI();
	}
}

ENGINE_API void FPrecomputedVolumetricLightmapData::InitRHIForSubLevelResources()
{
	if (SubLevelBrickPositions.Num() > 0)
	{
		SubLevelBrickPositions.SetAllowCPUAccess(true);
		IndirectionTextureOriginalValues.SetAllowCPUAccess(true);

		{
			FRHIResourceCreateInfo CreateInfo(&SubLevelBrickPositions);
			SubLevelBrickPositionsBuffer = RHICreateVertexBuffer(SubLevelBrickPositions.Num() * SubLevelBrickPositions.GetTypeSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			SubLevelBrickPositionsSRV = RHICreateShaderResourceView(SubLevelBrickPositionsBuffer, sizeof(uint32), PF_R32_UINT);
		}

		{
			FRHIResourceCreateInfo CreateInfo(&IndirectionTextureOriginalValues);
			IndirectionTextureOriginalValuesBuffer = RHICreateVertexBuffer(IndirectionTextureOriginalValues.Num() * IndirectionTextureOriginalValues.GetTypeSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			IndirectionTextureOriginalValuesSRV = RHICreateShaderResourceView(IndirectionTextureOriginalValuesBuffer, sizeof(FColor), PF_R8G8B8A8_UINT);
		}
	}
}

ENGINE_API void FPrecomputedVolumetricLightmapData::ReleaseRHI()
{
	if (FeatureLevel >= ERHIFeatureLevel::SM5)
	{
		{
			IndirectionTexture.Texture.SafeRelease();
			IndirectionTexture.UAV.SafeRelease();
			BrickData.ReleaseRHI();
		}

		GVolumetricLightmapBrickAtlas.Remove(this);
	}
}

ENGINE_API void FPrecomputedVolumetricLightmapData::ReleaseRHIForSubLevelResources()
{
	{
		SubLevelBrickPositionsBuffer.SafeRelease();
		SubLevelBrickPositionsSRV.SafeRelease();

		IndirectionTextureOriginalValuesBuffer.SafeRelease();
		IndirectionTextureOriginalValuesSRV.SafeRelease();
	}
}

ENGINE_API void FPrecomputedVolumetricLightmapData::HandleDataMovementInAtlas(int32 OldOffset, int32 NewOffset)
{
	BrickDataBaseOffsetInAtlas = NewOffset;

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (IndirectionTextureDimensions.GetMax() > 0)
	{
		const int32 PaddedBrickSize = BrickSize + 1;
		int32 NumBricks = BrickDataDimensions.X * BrickDataDimensions.Y * BrickDataDimensions.Z / (PaddedBrickSize * PaddedBrickSize * PaddedBrickSize);

		for (FPrecomputedVolumetricLightmapData* SceneData : SceneDataAdded)
		{
			TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

			TShaderMapRef<FMoveWholeIndirectionTextureCS> ComputeShader(GlobalShaderMap);

			FVolumetricLightmapDataLayer NewIndirectionTexture = SceneData->IndirectionTexture;
			NewIndirectionTexture.CreateTargetTexture(IndirectionTextureDimensions);
			NewIndirectionTexture.CreateUAV();

			FMoveWholeIndirectionTextureCS::FParameters Parameters;
			Parameters.NumBricks = NumBricks;
			Parameters.StartPosInOldVolume = OldOffset;
			Parameters.StartPosInNewVolume = BrickDataBaseOffsetInAtlas;
			Parameters.OldIndirectionTexture = SceneData->IndirectionTexture.Texture;
			Parameters.IndirectionTexture = NewIndirectionTexture.UAV;

			FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, Parameters,
				FIntVector(
					FMath::DivideAndRoundUp(IndirectionTextureDimensions.X, 4),
					FMath::DivideAndRoundUp(IndirectionTextureDimensions.Y, 4),
					FMath::DivideAndRoundUp(IndirectionTextureDimensions.Z, 4))
			);

			SceneData->IndirectionTexture = NewIndirectionTexture;

 			FRHIUnorderedAccessView* UAV = NewIndirectionTexture.UAV;
 			RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, &UAV, 1);
		}
	}
	else
	{
		InitRHIForSubLevelResources();

		for (FPrecomputedVolumetricLightmapData* SceneData : SceneDataAdded)
		{
			if (SceneData->IndirectionTexture.Texture.IsValid())
			{
				TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

				TShaderMapRef<FPatchIndirectionTextureCS> ComputeShader(GlobalShaderMap);

				int32 NumBricks = SubLevelBrickPositions.Num();

				FPatchIndirectionTextureCS::FParameters Parameters;
				Parameters.NumBricks = NumBricks;
				Parameters.StartPosInNewVolume = BrickDataBaseOffsetInAtlas;
				Parameters.IndirectionTexture = SceneData->IndirectionTexture.UAV;
				Parameters.SubLevelBrickPositions = SubLevelBrickPositionsSRV;

				FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, Parameters, FIntVector(FMath::DivideAndRoundUp(NumBricks, 64), 1, 1));

				FRHIUnorderedAccessView* UAV = SceneData->IndirectionTexture.UAV;
				RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, &UAV, 1);
			}
		}

		ReleaseRHIForSubLevelResources();
	}
}

inline FIntVector ComputeBrickLayoutPosition(int32 BrickLayoutAllocation, FIntVector BrickLayoutDimensions)
{
	const FIntVector BrickPosition(
		BrickLayoutAllocation % BrickLayoutDimensions.X,
		(BrickLayoutAllocation / BrickLayoutDimensions.X) % BrickLayoutDimensions.Y,
		BrickLayoutAllocation / (BrickLayoutDimensions.X * BrickLayoutDimensions.Y));

	return BrickPosition;
}

ENGINE_API void FPrecomputedVolumetricLightmapData::AddToSceneData(FPrecomputedVolumetricLightmapData* SceneData)
{
	if (SceneDataAdded.Find(SceneData) != INDEX_NONE)
	{
		return;
	}

	SceneDataAdded.Add(SceneData);

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (IndirectionTextureDimensions.GetMax() > 0)
	{
		// Copy parameters from the persistent level VLM
		SceneData->Bounds = Bounds;
		SceneData->BrickSize = BrickSize;
		SceneData->BrickDataDimensions = BrickDataDimensions;

		SceneData->IndirectionTexture.Format = IndirectionTexture.Format;
		SceneData->IndirectionTextureDimensions = IndirectionTextureDimensions;

		if (FeatureLevel >= ERHIFeatureLevel::SM5)
		{
			// GPU Path

			const int32 PaddedBrickSize = BrickSize + 1;
			int32 NumBricks = BrickDataDimensions.X * BrickDataDimensions.Y * BrickDataDimensions.Z / (PaddedBrickSize * PaddedBrickSize * PaddedBrickSize);

			TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

			TShaderMapRef<FMoveWholeIndirectionTextureCS> ComputeShader(GlobalShaderMap);

			FVolumetricLightmapDataLayer NewIndirectionTexture = SceneData->IndirectionTexture;
			NewIndirectionTexture.CreateTargetTexture(IndirectionTextureDimensions);
			NewIndirectionTexture.CreateUAV();

			FMoveWholeIndirectionTextureCS::FParameters Parameters;
			Parameters.NumBricks = NumBricks;
			Parameters.StartPosInOldVolume = 0;
			Parameters.StartPosInNewVolume = BrickDataBaseOffsetInAtlas;
			Parameters.OldIndirectionTexture = IndirectionTexture.Texture;
			Parameters.IndirectionTexture = NewIndirectionTexture.UAV;

			FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, Parameters,
				FIntVector(
					FMath::DivideAndRoundUp(IndirectionTextureDimensions.X, 4),
					FMath::DivideAndRoundUp(IndirectionTextureDimensions.Y, 4),
					FMath::DivideAndRoundUp(IndirectionTextureDimensions.Z, 4))
			);

			SceneData->IndirectionTexture = NewIndirectionTexture;

			if (!GIsEditor)
			{
				// Steal the indirection texture. When the sublevels are unloaded the values will be restored.
				IndirectionTexture = SceneData->IndirectionTexture;
			}

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, IndirectionTexture.Texture);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, NewIndirectionTexture.UAV);
		}
		else
		{
			// CPU Path
			SceneData->IndirectionTexture.Data = IndirectionTexture.Data;
			SceneData->CPUSubLevelIndirectionTable.Empty();
			SceneData->CPUSubLevelIndirectionTable.AddZeroed(IndirectionTextureDimensions.X * IndirectionTextureDimensions.Y * IndirectionTextureDimensions.Z);
			SceneData->CPUSubLevelBrickDataList.Empty();
			SceneData->CPUSubLevelBrickDataList.Add(this);
		}
	}
	else
	{
		if (FeatureLevel >= ERHIFeatureLevel::SM5)
		{
			// GPU Path
			if (SceneData->IndirectionTexture.Texture.IsValid())
			{
				InitRHIForSubLevelResources();

				TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

				TShaderMapRef<FPatchIndirectionTextureCS> ComputeShader(GlobalShaderMap);

				int32 NumBricks = SubLevelBrickPositions.Num();

				FPatchIndirectionTextureCS::FParameters Parameters;
				Parameters.NumBricks = NumBricks;
				Parameters.StartPosInNewVolume = BrickDataBaseOffsetInAtlas;
				Parameters.IndirectionTexture = SceneData->IndirectionTexture.UAV;
				Parameters.SubLevelBrickPositions = SubLevelBrickPositionsSRV;

				FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, Parameters, FIntVector(FMath::DivideAndRoundUp(NumBricks, 64), 1, 1));

				FRHIUnorderedAccessView* UAV = SceneData->IndirectionTexture.UAV;
				RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, &UAV, 1);

				ReleaseRHIForSubLevelResources();
			}
		}
		else
		{
			// CPU Path
			if (SceneData->IndirectionTexture.Data.Num() > 0)
			{
				int32 Index = SceneData->CPUSubLevelBrickDataList.Add(this);
				check(Index < UINT8_MAX);
				uint8 Value = (uint8)Index;

				for (int32 BrickIndex = 0; BrickIndex < SubLevelBrickPositions.Num(); BrickIndex++)
				{
					const FIntVector BrickLayoutPosition = ComputeBrickLayoutPosition(BrickIndex, BrickDataDimensions);

					const FIntVector IndirectionDestDataCoordinate = SubLevelBrickPositions[BrickIndex];
					const int32 IndirectionDestDataIndex =
						((IndirectionDestDataCoordinate.Z * SceneData->IndirectionTextureDimensions.Y) + IndirectionDestDataCoordinate.Y) *
						SceneData->IndirectionTextureDimensions.X + IndirectionDestDataCoordinate.X;

					{
						const int32 IndirectionTextureDataStride = GPixelFormats[SceneData->IndirectionTexture.Format].BlockBytes;
						uint8* IndirectionVoxelPtr = (uint8*)&SceneData->IndirectionTexture.Data[IndirectionDestDataIndex * IndirectionTextureDataStride];
						*(IndirectionVoxelPtr + 0) = BrickLayoutPosition.X;
						*(IndirectionVoxelPtr + 1) = BrickLayoutPosition.Y;
						*(IndirectionVoxelPtr + 2) = BrickLayoutPosition.Z;
						*(IndirectionVoxelPtr + 3) = 1;
					}

					{
						SceneData->CPUSubLevelIndirectionTable[IndirectionDestDataIndex] = Value;
					}
				}
			}
		}
	}

	SceneData->BrickDataDimensions = GVolumetricLightmapBrickAtlas.TextureSet.BrickDataDimensions;
	SceneData->BrickData.AmbientVector = GVolumetricLightmapBrickAtlas.TextureSet.AmbientVector;
	for (int32 i = 0; i < UE_ARRAY_COUNT(SceneData->BrickData.SHCoefficients); i++)
	{
		SceneData->BrickData.SHCoefficients[i] = GVolumetricLightmapBrickAtlas.TextureSet.SHCoefficients[i];
	}
	SceneData->BrickData.SkyBentNormal = GVolumetricLightmapBrickAtlas.TextureSet.SkyBentNormal;
	SceneData->BrickData.DirectionalLightShadowing = GVolumetricLightmapBrickAtlas.TextureSet.DirectionalLightShadowing;
}

ENGINE_API void FPrecomputedVolumetricLightmapData::RemoveFromSceneData(FPrecomputedVolumetricLightmapData* SceneData, int32 PersistentLevelBrickDataBaseOffset)
{
	if (SceneDataAdded.Find(SceneData) == INDEX_NONE)
	{
		return;
	}

	SceneDataAdded.Remove(SceneData);

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (IndirectionTextureDimensions.GetMax() > 0)
	{
		// Do nothing, as when a VLM data with indirection texture is being destroyed, the persistent level is going away
	}
	else
	{
		if (FeatureLevel >= ERHIFeatureLevel::SM5)
		{
			if (SceneData->IndirectionTexture.Texture.IsValid())
			{
				SCOPED_DRAW_EVENT(RHICmdList, RemoveSubLevelBricksCS);

				InitRHIForSubLevelResources();

				TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

				TShaderMapRef<FRemoveSubLevelBricksCS> ComputeShader(GlobalShaderMap);

				FRemoveSubLevelBricksCS::FParameters Parameters;
				Parameters.NumBricks = SubLevelBrickPositions.Num();
				Parameters.IndirectionTexture = SceneData->IndirectionTexture.UAV;
				Parameters.SubLevelBrickPositions = SubLevelBrickPositionsSRV;
				Parameters.IndirectionTextureOriginalValues = IndirectionTextureOriginalValuesSRV;
				Parameters.PersistentLevelBrickDataBaseOffset = PersistentLevelBrickDataBaseOffset;

				FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, Parameters, FIntVector(FMath::DivideAndRoundUp(SubLevelBrickPositions.Num(), 64), 1, 1));

				FRHIUnorderedAccessView* UAV = SceneData->IndirectionTexture.UAV;
				RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, &UAV, 1);

				ReleaseRHIForSubLevelResources();
			}
		}
		else
		{
			// CPU Path
			if (SceneData->IndirectionTexture.Data.Num() > 0)
			{
				SceneData->CPUSubLevelBrickDataList.Remove(this);

				for (int32 BrickIndex = 0; BrickIndex < SubLevelBrickPositions.Num(); BrickIndex++)
				{
					const FColor OriginalValue = IndirectionTextureOriginalValues[BrickIndex];

					const FIntVector IndirectionDestDataCoordinate = SubLevelBrickPositions[BrickIndex];
					const int32 IndirectionDestDataIndex =
						((IndirectionDestDataCoordinate.Z * SceneData->IndirectionTextureDimensions.Y) + IndirectionDestDataCoordinate.Y) *
						SceneData->IndirectionTextureDimensions.X + IndirectionDestDataCoordinate.X;

					{
						const int32 IndirectionTextureDataStride = GPixelFormats[SceneData->IndirectionTexture.Format].BlockBytes;
						uint8* IndirectionVoxelPtr = (uint8*)&SceneData->IndirectionTexture.Data[IndirectionDestDataIndex * IndirectionTextureDataStride];
						*(IndirectionVoxelPtr + 0) = OriginalValue.R;
						*(IndirectionVoxelPtr + 1) = OriginalValue.G;
						*(IndirectionVoxelPtr + 2) = OriginalValue.B;
						*(IndirectionVoxelPtr + 3) = 1;
					}

					{
						SceneData->CPUSubLevelIndirectionTable[IndirectionDestDataIndex] = 0;
					}
				}
			}
		}
	}

	SceneData->BrickDataDimensions = GVolumetricLightmapBrickAtlas.TextureSet.BrickDataDimensions;
	SceneData->BrickData.AmbientVector = GVolumetricLightmapBrickAtlas.TextureSet.AmbientVector;
	for (int32 i = 0; i < UE_ARRAY_COUNT(SceneData->BrickData.SHCoefficients); i++)
	{
		SceneData->BrickData.SHCoefficients[i] = GVolumetricLightmapBrickAtlas.TextureSet.SHCoefficients[i];
	}
	SceneData->BrickData.SkyBentNormal = GVolumetricLightmapBrickAtlas.TextureSet.SkyBentNormal;
	SceneData->BrickData.DirectionalLightShadowing = GVolumetricLightmapBrickAtlas.TextureSet.DirectionalLightShadowing;
}

SIZE_T FPrecomputedVolumetricLightmapData::GetAllocatedBytes() const
{
	return
		IndirectionTexture.DataSize + 
		BrickData.GetAllocatedBytes() + 
		SubLevelBrickPositions.Num() * SubLevelBrickPositions.GetTypeSize() +
		IndirectionTextureOriginalValues.Num() * IndirectionTextureOriginalValues.GetTypeSize();
}


FPrecomputedVolumetricLightmap::FPrecomputedVolumetricLightmap() :
	Data(NULL),
	bAddedToScene(false),
	WorldOriginOffset(ForceInitToZero)
{}

FPrecomputedVolumetricLightmap::~FPrecomputedVolumetricLightmap()
{
}

void FPrecomputedVolumetricLightmap::AddToScene(FSceneInterface* Scene, UMapBuildDataRegistry* Registry, FGuid LevelBuildDataId, bool bIsPersistentLevel)
{
	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));

	if (AllowStaticLightingVar->GetValueOnAnyThread() == 0)
	{
		return;
	}

	// FIXME: temp fix for ordering issue between WorldContext.World()->InitWorld(); and GShaderCompilingManager->ProcessAsyncResults(false, true); in UnrealEngine.cpp
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->ProcessAsyncResults(false, true);
	}

	check(!bAddedToScene);

	FPrecomputedVolumetricLightmapData* NewData = NULL;

	if (Registry)
	{
		NewData = Registry->GetLevelPrecomputedVolumetricLightmapBuildData(LevelBuildDataId);
	}

	if (NewData && Scene)
	{
		bAddedToScene = true;

		FPrecomputedVolumetricLightmap* Volume = this;

		ENQUEUE_RENDER_COMMAND(SetVolumeDataCommand)
			([Volume, NewData, Scene](FRHICommandListImmediate& RHICmdList) 
			{
				Volume->SetData(NewData, Scene);
			});
		Scene->AddPrecomputedVolumetricLightmap(this, bIsPersistentLevel);
	}
}

void FPrecomputedVolumetricLightmap::RemoveFromScene(FSceneInterface* Scene)
{
	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));

	if (AllowStaticLightingVar->GetValueOnAnyThread() == 0)
	{
		return;
	}

	if (bAddedToScene)
	{
		bAddedToScene = false;

		if (Scene)
		{
			Scene->RemovePrecomputedVolumetricLightmap(this);
		}
	}

	WorldOriginOffset = FVector::ZeroVector;
}

void FPrecomputedVolumetricLightmap::SetData(FPrecomputedVolumetricLightmapData* NewData, FSceneInterface* Scene)
{
	Data = NewData;

	if (Data)
	{
		Data->FeatureLevel = Scene->GetFeatureLevel();
		Data->IndirectionTexture.bNeedsCPUAccess = GIsEditor;
		Data->BrickData.SetNeedsCPUAccess(GIsEditor);

		if (Scene->GetFeatureLevel() >= ERHIFeatureLevel::SM5)
		{
			Data->InitResource();
		}
	}
}

void FPrecomputedVolumetricLightmap::ApplyWorldOffset(const FVector& InOffset)
{
	WorldOriginOffset += InOffset;
}

FVector ComputeIndirectionCoordinate(FVector LookupPosition, const FBox& VolumeBounds, FIntVector IndirectionTextureDimensions)
{
	const FVector InvVolumeSize = FVector(1.0f) / VolumeBounds.GetSize();
	const FVector VolumeWorldToUVScale = InvVolumeSize;
	const FVector VolumeWorldToUVAdd = -VolumeBounds.Min * InvVolumeSize;

	FVector IndirectionDataSourceCoordinate = (LookupPosition * VolumeWorldToUVScale + VolumeWorldToUVAdd) * FVector(IndirectionTextureDimensions);
	IndirectionDataSourceCoordinate.X = FMath::Clamp<float>(IndirectionDataSourceCoordinate.X, 0.0f, IndirectionTextureDimensions.X - .01f);
	IndirectionDataSourceCoordinate.Y = FMath::Clamp<float>(IndirectionDataSourceCoordinate.Y, 0.0f, IndirectionTextureDimensions.Y - .01f);
	IndirectionDataSourceCoordinate.Z = FMath::Clamp<float>(IndirectionDataSourceCoordinate.Z, 0.0f, IndirectionTextureDimensions.Z - .01f);

	return IndirectionDataSourceCoordinate;
}

void SampleIndirectionTexture(
	FVector IndirectionDataSourceCoordinate,
	FIntVector IndirectionTextureDimensions,
	const uint8* IndirectionTextureData,
	FIntVector& OutIndirectionBrickOffset,
	int32& OutIndirectionBrickSize)
{
	FIntVector IndirectionDataCoordinateInt(IndirectionDataSourceCoordinate);
	
	IndirectionDataCoordinateInt.X = FMath::Clamp<int32>(IndirectionDataCoordinateInt.X, 0, IndirectionTextureDimensions.X - 1);
	IndirectionDataCoordinateInt.Y = FMath::Clamp<int32>(IndirectionDataCoordinateInt.Y, 0, IndirectionTextureDimensions.Y - 1);
	IndirectionDataCoordinateInt.Z = FMath::Clamp<int32>(IndirectionDataCoordinateInt.Z, 0, IndirectionTextureDimensions.Z - 1);

	const int32 IndirectionDataIndex = ((IndirectionDataCoordinateInt.Z * IndirectionTextureDimensions.Y) + IndirectionDataCoordinateInt.Y) * IndirectionTextureDimensions.X + IndirectionDataCoordinateInt.X;
	const uint8* IndirectionVoxelPtr = (const uint8*)&IndirectionTextureData[IndirectionDataIndex * sizeof(uint8) * 4];
	OutIndirectionBrickOffset = FIntVector(*(IndirectionVoxelPtr + 0), *(IndirectionVoxelPtr + 1), *(IndirectionVoxelPtr + 2));
	OutIndirectionBrickSize = *(IndirectionVoxelPtr + 3);
}

void SampleIndirectionTextureWithSubLevel(
	FVector IndirectionDataSourceCoordinate,
	FIntVector IndirectionTextureDimensions,
	const uint8* IndirectionTextureData,
	const TArray<uint8>& CPUSubLevelIndirectionTable,
	FIntVector& OutIndirectionBrickOffset,
	int32& OutIndirectionBrickSize,
	int32& OutSubLevelIndex)
{
	SampleIndirectionTexture(IndirectionDataSourceCoordinate, IndirectionTextureDimensions, IndirectionTextureData, OutIndirectionBrickOffset, OutIndirectionBrickSize);

	FIntVector IndirectionDataCoordinateInt(IndirectionDataSourceCoordinate);

	IndirectionDataCoordinateInt.X = FMath::Clamp<int32>(IndirectionDataCoordinateInt.X, 0, IndirectionTextureDimensions.X - 1);
	IndirectionDataCoordinateInt.Y = FMath::Clamp<int32>(IndirectionDataCoordinateInt.Y, 0, IndirectionTextureDimensions.Y - 1);
	IndirectionDataCoordinateInt.Z = FMath::Clamp<int32>(IndirectionDataCoordinateInt.Z, 0, IndirectionTextureDimensions.Z - 1);

	const int32 IndirectionDataIndex = ((IndirectionDataCoordinateInt.Z * IndirectionTextureDimensions.Y) + IndirectionDataCoordinateInt.Y) * IndirectionTextureDimensions.X + IndirectionDataCoordinateInt.X;

	OutSubLevelIndex = CPUSubLevelIndirectionTable[IndirectionDataIndex];
}

FVector ComputeBrickTextureCoordinate(
	FVector IndirectionDataSourceCoordinate,
	FIntVector IndirectionBrickOffset, 
	int32 IndirectionBrickSize,
	int32 BrickSize)
{
	FVector IndirectionDataSourceCoordinateInBricks = IndirectionDataSourceCoordinate / IndirectionBrickSize;
	FVector FractionalIndirectionDataCoordinate(FMath::Frac(IndirectionDataSourceCoordinateInBricks.X), FMath::Frac(IndirectionDataSourceCoordinateInBricks.Y), FMath::Frac(IndirectionDataSourceCoordinateInBricks.Z));
	int32 PaddedBrickSize = BrickSize + 1;
	FVector BrickTextureCoordinate = FVector(IndirectionBrickOffset * PaddedBrickSize) + FractionalIndirectionDataCoordinate * BrickSize;
	return BrickTextureCoordinate;
}

IMPLEMENT_GLOBAL_SHADER(FRemoveSubLevelBricksCS, "/Engine/Private/VolumetricLightmapStreaming.usf", "RemoveSubLevelBricksCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FCopyResidentBricksCS, "/Engine/Private/VolumetricLightmapStreaming.usf", "CopyResidentBricksCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FCopyResidentBrickSHCoefficientsCS, "/Engine/Private/VolumetricLightmapStreaming.usf", "CopyResidentBrickSHCoefficientsCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FPatchIndirectionTextureCS, "/Engine/Private/VolumetricLightmapStreaming.usf", "PatchIndirectionTextureCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FMoveWholeIndirectionTextureCS, "/Engine/Private/VolumetricLightmapStreaming.usf", "MoveWholeIndirectionTextureCS", SF_Compute);

FVolumetricLightmapBrickAtlas::FVolumetricLightmapBrickAtlas()
	: bInitialized(false)
	, PaddedBrickSize(5)
{
}

template<class VolumetricLightmapBrickDataType>
void CopyDataIntoAtlas(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 SrcOffset, int32 DestOffset, int32 NumBricks, const VolumetricLightmapBrickDataType& SrcData, FVolumetricLightmapBrickTextureSet DestTextureSet)
{
	{
		TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

		FCopyResidentBricksCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FCopyResidentBricksCS::FHasSkyBentNormal>(SrcData.SkyBentNormal.Texture.IsValid());

		TShaderMapRef<FCopyResidentBricksCS> ComputeShader(GlobalShaderMap, PermutationVector);

		FCopyResidentBricksCS::FParameters Parameters;

		Parameters.StartPosInOldVolume = SrcOffset;
		Parameters.StartPosInNewVolume = DestOffset;

		Parameters.AmbientVector = SrcData.AmbientVector.Texture;
		Parameters.SkyBentNormal = SrcData.SkyBentNormal.Texture;
		Parameters.DirectionalLightShadowing = SrcData.DirectionalLightShadowing.Texture;

		Parameters.OutAmbientVector = DestTextureSet.AmbientVector.UAV;
		Parameters.OutSkyBentNormal = DestTextureSet.SkyBentNormal.UAV;
		Parameters.OutDirectionalLightShadowing = DestTextureSet.DirectionalLightShadowing.UAV;

		FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, Parameters, FIntVector(NumBricks, 1, 1));

		FRHIUnorderedAccessView* UAVs[3];
		UAVs[0] = Parameters.OutAmbientVector;
		UAVs[1] = Parameters.OutSkyBentNormal;
		UAVs[2] = Parameters.OutDirectionalLightShadowing;
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, UAVs, 3);
	}

	for (int32 i = 0; i < UE_ARRAY_COUNT(SrcData.SHCoefficients); i++)
	{
		TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

		TShaderMapRef<FCopyResidentBrickSHCoefficientsCS> ComputeShader(GlobalShaderMap);

		FCopyResidentBrickSHCoefficientsCS::FParameters Parameters;

		Parameters.StartPosInOldVolume = SrcOffset;
		Parameters.StartPosInNewVolume = DestOffset;

		Parameters.SHCoefficients = SrcData.SHCoefficients[i].Texture;
		Parameters.OutSHCoefficients = DestTextureSet.SHCoefficients[i].UAV;

		FComputeShaderUtils::Dispatch(RHICmdList, *ComputeShader, Parameters, FIntVector(NumBricks, 1, 1));

		FRHIUnorderedAccessView* UAV = Parameters.OutSHCoefficients;
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, &UAV, 1);
	}
}

void FVolumetricLightmapBrickAtlas::Insert(int32 Index, FPrecomputedVolumetricLightmapData* Data)
{
	check(!Allocations.FindByPredicate([Data](const Allocation& Other) { return Other.Data == Data; }));

	if (!bInitialized)
	{
		FeatureLevel = ERHIFeatureLevel::SM5;
		check(Data->BrickSize > 0);
		PaddedBrickSize = Data->BrickSize + 1;
		TextureSet.Initialize(Data->BrickDataDimensions, Data->BrickData);
		bInitialized = true;
	}
	else
	{
		check(TextureSet.AmbientVector.Format == Data->BrickData.AmbientVector.Format);
		for (int32 i = 0; i < UE_ARRAY_COUNT(TextureSet.SHCoefficients); i++)
		{
			check(TextureSet.SHCoefficients[i].Format == Data->BrickData.SHCoefficients[i].Format);
		}
		check(TextureSet.SkyBentNormal.Format == Data->BrickData.SkyBentNormal.Format);
		check(TextureSet.DirectionalLightShadowing.Format == Data->BrickData.DirectionalLightShadowing.Format);

		// If the incoming BrickData has sky bent normal, also create one in atlas
		// TODO: release SkyBentNormal if no brick data in the atlas uses it
		if (!TextureSet.SkyBentNormal.Texture.IsValid() && Data->BrickData.SkyBentNormal.Texture.IsValid())
		{
			TextureSet.SkyBentNormal.CreateTargetTexture(TextureSet.BrickDataDimensions);
			TextureSet.SkyBentNormal.CreateUAV();
		}
	}

	int32 NumTotalBricks = 0;

	for (const auto& Allocation : Allocations)
	{
		NumTotalBricks += Allocation.Size;
	}

	NumTotalBricks += Data->BrickDataDimensions.X * Data->BrickDataDimensions.Y * Data->BrickDataDimensions.Z / (PaddedBrickSize * PaddedBrickSize * PaddedBrickSize);

	TArray<Allocation> NewAllocations;
	FVolumetricLightmapBrickTextureSet NewTextureSet;

	{
		const int32 MaxBricksInLayoutOneDim = 1 << 8;
		int32 BrickTextureLinearAllocator = NumTotalBricks;
		FIntVector BrickLayoutDimensions;
		BrickLayoutDimensions.X = FMath::Min(BrickTextureLinearAllocator, MaxBricksInLayoutOneDim);
		BrickTextureLinearAllocator = FMath::DivideAndRoundUp(BrickTextureLinearAllocator, BrickLayoutDimensions.X);
		BrickLayoutDimensions.Y = FMath::Min(BrickTextureLinearAllocator, MaxBricksInLayoutOneDim);
		BrickTextureLinearAllocator = FMath::DivideAndRoundUp(BrickTextureLinearAllocator, BrickLayoutDimensions.Y);
		BrickLayoutDimensions.Z = FMath::Min(BrickTextureLinearAllocator, MaxBricksInLayoutOneDim);
		FIntVector BrickDataDimensions = BrickLayoutDimensions * PaddedBrickSize;

		NewTextureSet.Initialize(BrickDataDimensions, TextureSet);
	}

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	// Dry run to handle persistent level data movement properly
	{
		int32 BrickStartAllocation = 0;

		// Copy old allocations
		for (int32 AllocationIndex = 0; AllocationIndex < Index && AllocationIndex < Allocations.Num(); AllocationIndex++)
		{
			BrickStartAllocation += Allocations[AllocationIndex].Size;
		}

		// Insert new allocation
		{
			int32 NumBricks = Data->BrickDataDimensions.X * Data->BrickDataDimensions.Y * Data->BrickDataDimensions.Z / (PaddedBrickSize * PaddedBrickSize * PaddedBrickSize);

			BrickStartAllocation += NumBricks;
		}

		// Copy the rest of allocations
		for (int32 AllocationIndex = Index; AllocationIndex < Allocations.Num(); AllocationIndex++)
		{
			if (Allocations[AllocationIndex].Data->IndirectionTextureDimensions.GetMax() > 0)
			{
				Allocations[AllocationIndex].Data->HandleDataMovementInAtlas(Allocations[AllocationIndex].StartOffset, BrickStartAllocation);
			}
			BrickStartAllocation += Allocations[AllocationIndex].Size;
		}
	}

	{
		{
			// Transition all the UAVs to writable
			FRHIUnorderedAccessView* UAVs[UE_ARRAY_COUNT(NewTextureSet.SHCoefficients) + 3] =
			{
				NewTextureSet.AmbientVector.UAV,
				NewTextureSet.SkyBentNormal.UAV,
				NewTextureSet.DirectionalLightShadowing.UAV
			};
			for (int32 i = 0; i < UE_ARRAY_COUNT(NewTextureSet.SHCoefficients); i++)
			{
				UAVs[i + 3] = NewTextureSet.SHCoefficients[i].UAV;
			}
			RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, UAVs, UE_ARRAY_COUNT(UAVs), nullptr);
		}

		int32 BrickStartAllocation = 0;

		// Copy old allocations
		for (int32 AllocationIndex = 0; AllocationIndex < Index && AllocationIndex < Allocations.Num(); AllocationIndex++)
		{
			CopyDataIntoAtlas(RHICmdList, FeatureLevel, Allocations[AllocationIndex].StartOffset, BrickStartAllocation, Allocations[AllocationIndex].Size, TextureSet, NewTextureSet);

			NewAllocations.Add(Allocation{ Allocations[AllocationIndex].Data, Allocations[AllocationIndex].Size, BrickStartAllocation });
			BrickStartAllocation += Allocations[AllocationIndex].Size;
		}

		// Insert new allocation
		{
			int32 NumBricks = Data->BrickDataDimensions.X * Data->BrickDataDimensions.Y * Data->BrickDataDimensions.Z / (PaddedBrickSize * PaddedBrickSize * PaddedBrickSize);

			CopyDataIntoAtlas(RHICmdList, FeatureLevel, 0, BrickStartAllocation, NumBricks, Data->BrickData, NewTextureSet);

			NewAllocations.Add(Allocation{ Data, NumBricks, BrickStartAllocation });
			Data->BrickDataBaseOffsetInAtlas = BrickStartAllocation;
			BrickStartAllocation += NumBricks;
		}

		// Copy the rest of allocations
		for (int32 AllocationIndex = Index; AllocationIndex < Allocations.Num(); AllocationIndex++)
		{
			CopyDataIntoAtlas(RHICmdList, FeatureLevel, Allocations[AllocationIndex].StartOffset, BrickStartAllocation, Allocations[AllocationIndex].Size, TextureSet, NewTextureSet);

			NewAllocations.Add(Allocation{ Allocations[AllocationIndex].Data, Allocations[AllocationIndex].Size, BrickStartAllocation });
			// Handle the sub level data movements
			if (Allocations[AllocationIndex].Data->IndirectionTextureDimensions.GetMax() == 0)
			{
				Allocations[AllocationIndex].Data->HandleDataMovementInAtlas(Allocations[AllocationIndex].StartOffset, BrickStartAllocation);
			}
			BrickStartAllocation += Allocations[AllocationIndex].Size;
		}

		{
			// Transition all UAVs in the new set to readable
			FRHIUnorderedAccessView* UAVs[UE_ARRAY_COUNT(NewTextureSet.SHCoefficients) + 3] =
			{
				NewTextureSet.AmbientVector.UAV,
				NewTextureSet.SkyBentNormal.UAV,
				NewTextureSet.DirectionalLightShadowing.UAV
			};
			for (int32 i = 0; i < UE_ARRAY_COUNT(NewTextureSet.SHCoefficients); i++)
			{
				UAVs[i + 3] = NewTextureSet.SHCoefficients[i].UAV;
			}
			RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, UAVs, UE_ARRAY_COUNT(UAVs), nullptr);
		}
	}

	// Replace with new allcations
	Allocations = NewAllocations;
	TextureSet = NewTextureSet; // <-- Old texture references are released here

	FRHITexture* Textures[3 + UE_ARRAY_COUNT(TextureSet.SHCoefficients)] = 
	{
		TextureSet.AmbientVector.Texture,
		TextureSet.SkyBentNormal.Texture,
		TextureSet.DirectionalLightShadowing.Texture
	};

	for (int32 TextureIndex = 0; TextureIndex < UE_ARRAY_COUNT(TextureSet.SHCoefficients); ++TextureIndex)
	{
		Textures[TextureIndex + 3] = TextureSet.SHCoefficients[TextureIndex].Texture;
	}

	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, Textures, UE_ARRAY_COUNT(Textures));
}

void FVolumetricLightmapBrickAtlas::Remove(FPrecomputedVolumetricLightmapData* Data)
{
	Allocation* AllocationEntry = Allocations.FindByPredicate([Data](const Allocation& Other) { return Other.Data == Data; });
	if (!AllocationEntry)
	{
		return;
	}

	int32 Index = (int32)(AllocationEntry - Allocations.GetData());

	int32 NumTotalBricks = 0;

	for (const auto& Allocation : Allocations)
	{
		if (Allocation.Data != Data)
		{
			NumTotalBricks += Allocation.Size;
		}
	}

	TArray<Allocation> NewAllocations;
	FVolumetricLightmapBrickTextureSet NewTextureSet;

	if (NumTotalBricks > 0)
	{
		{
			const int32 MaxBricksInLayoutOneDim = 1 << 8;
			int32 BrickTextureLinearAllocator = NumTotalBricks;
			FIntVector BrickLayoutDimensions;
			BrickLayoutDimensions.X = FMath::Min(BrickTextureLinearAllocator, MaxBricksInLayoutOneDim);
			BrickTextureLinearAllocator = FMath::DivideAndRoundUp(BrickTextureLinearAllocator, BrickLayoutDimensions.X);
			BrickLayoutDimensions.Y = FMath::Min(BrickTextureLinearAllocator, MaxBricksInLayoutOneDim);
			BrickTextureLinearAllocator = FMath::DivideAndRoundUp(BrickTextureLinearAllocator, BrickLayoutDimensions.Y);
			BrickLayoutDimensions.Z = FMath::Min(BrickTextureLinearAllocator, MaxBricksInLayoutOneDim);
			FIntVector BrickDataDimensions = BrickLayoutDimensions * PaddedBrickSize;

			NewTextureSet.Initialize(BrickDataDimensions, TextureSet);
		}

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		{
			{
				// Transition all the UAVs to writable
				FRHIUnorderedAccessView* UAVs[UE_ARRAY_COUNT(NewTextureSet.SHCoefficients) + 3] =
				{
					NewTextureSet.AmbientVector.UAV,
					NewTextureSet.SkyBentNormal.UAV,
					NewTextureSet.DirectionalLightShadowing.UAV
				};
				for (int32 i = 0; i < UE_ARRAY_COUNT(NewTextureSet.SHCoefficients); i++)
				{
					UAVs[i + 3] = NewTextureSet.SHCoefficients[i].UAV;
				}
				RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, UAVs, UE_ARRAY_COUNT(UAVs), nullptr);
			}

			int32 BrickStartAllocation = 0;

			// Copy old allocations
			for (int32 AllocationIndex = 0; AllocationIndex < Index && AllocationIndex < Allocations.Num(); AllocationIndex++)
			{
				CopyDataIntoAtlas(RHICmdList, FeatureLevel, Allocations[AllocationIndex].StartOffset, BrickStartAllocation, Allocations[AllocationIndex].Size, TextureSet, NewTextureSet);

				NewAllocations.Add(Allocation{ Allocations[AllocationIndex].Data, Allocations[AllocationIndex].Size, BrickStartAllocation });
				BrickStartAllocation += Allocations[AllocationIndex].Size;
			}

			// Skip the allocation being deleted

			// Copy the rest of allocations
			for (int32 AllocationIndex = Index + 1; AllocationIndex < Allocations.Num(); AllocationIndex++)
			{
				CopyDataIntoAtlas(RHICmdList, FeatureLevel, Allocations[AllocationIndex].StartOffset, BrickStartAllocation, Allocations[AllocationIndex].Size, TextureSet, NewTextureSet);

				NewAllocations.Add(Allocation{ Allocations[AllocationIndex].Data, Allocations[AllocationIndex].Size, BrickStartAllocation });
				Allocations[AllocationIndex].Data->HandleDataMovementInAtlas(Allocations[AllocationIndex].StartOffset, BrickStartAllocation);
				BrickStartAllocation += Allocations[AllocationIndex].Size;
			}

			{
				// Transition all UAVs in the new set to readable
				FRHIUnorderedAccessView* UAVs[UE_ARRAY_COUNT(NewTextureSet.SHCoefficients) + 3] =
				{
					NewTextureSet.AmbientVector.UAV,
					NewTextureSet.SkyBentNormal.UAV,
					NewTextureSet.DirectionalLightShadowing.UAV
				};
				for (int32 i = 0; i < UE_ARRAY_COUNT(NewTextureSet.SHCoefficients); i++)
				{
					UAVs[i + 3] = NewTextureSet.SHCoefficients[i].UAV;
				}
				RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, UAVs, UE_ARRAY_COUNT(UAVs), nullptr);
			}
		}
	}
	else
	{
		bInitialized = false;
	}

	// Replace with new allcations
	Allocations = NewAllocations;
	TextureSet = NewTextureSet; // <-- Old texture references are released here
}

void FVolumetricLightmapBrickAtlas::ReleaseRHI()
{
	TextureSet.Release();
}
