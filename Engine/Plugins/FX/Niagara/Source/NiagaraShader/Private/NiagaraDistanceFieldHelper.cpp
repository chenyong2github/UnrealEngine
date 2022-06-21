// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDistanceFieldHelper.h"
#include "Runtime/Renderer/Private/SystemTextures.h"

// todo - currently duplicated from SetupGlobalDistanceFieldParameters (GlobalDistanceField.cpp) because of problems getting it properly exported from the dll
void FNiagaraDistanceFieldHelper::SetGlobalDistanceFieldParameters(const FGlobalDistanceFieldParameterData* OptionalParameterData, FGlobalDistanceFieldParameters2& ShaderParameters)
{
	if (OptionalParameterData != nullptr)
	{
		ShaderParameters.GlobalDistanceFieldPageAtlasTexture = OrBlack3DIfNull(OptionalParameterData->PageAtlasTexture);
		ShaderParameters.GlobalDistanceFieldCoverageAtlasTexture = OrBlack3DIfNull(OptionalParameterData->CoverageAtlasTexture);
		ShaderParameters.GlobalDistanceFieldPageTableTexture = OrBlack3DUintIfNull(OptionalParameterData->PageTableTexture);
		ShaderParameters.GlobalDistanceFieldMipTexture = OrBlack3DIfNull(OptionalParameterData->MipTexture);

		for (int32 Index = 0; Index < GlobalDistanceField::MaxClipmaps; Index++)
		{
			ShaderParameters.GlobalVolumeCenterAndExtent[Index] = OptionalParameterData->CenterAndExtent[Index];
			ShaderParameters.GlobalVolumeWorldToUVAddAndMul[Index] = OptionalParameterData->WorldToUVAddAndMul[Index];
			ShaderParameters.GlobalDistanceFieldMipWorldToUVScale[Index] = OptionalParameterData->MipWorldToUVScale[Index];
			ShaderParameters.GlobalDistanceFieldMipWorldToUVBias[Index] = OptionalParameterData->MipWorldToUVBias[Index];
		}

		ShaderParameters.GlobalDistanceFieldMipFactor = OptionalParameterData->MipFactor;
		ShaderParameters.GlobalDistanceFieldMipTransition = OptionalParameterData->MipTransition;
		ShaderParameters.GlobalDistanceFieldClipmapSizeInPages = OptionalParameterData->ClipmapSizeInPages;
		ShaderParameters.GlobalDistanceFieldInvPageAtlasSize = (FVector3f)OptionalParameterData->InvPageAtlasSize;
		ShaderParameters.GlobalDistanceFieldInvCoverageAtlasSize = (FVector3f)OptionalParameterData->InvCoverageAtlasSize;
		ShaderParameters.GlobalVolumeDimension = OptionalParameterData->GlobalDFResolution;
		ShaderParameters.GlobalVolumeTexelSize = 1.0f / OptionalParameterData->GlobalDFResolution;
		ShaderParameters.MaxGlobalDFAOConeDistance = OptionalParameterData->MaxDFAOConeDistance;
		ShaderParameters.NumGlobalSDFClipmaps = OptionalParameterData->NumGlobalSDFClipmaps;
		ShaderParameters.FullyCoveredExpandSurfaceScale = 0.0f;//GLumenSceneGlobalSDFFullyCoveredExpandSurfaceScale;
		ShaderParameters.UncoveredExpandSurfaceScale = 0.0f;//GLumenSceneGlobalSDFUncoveredExpandSurfaceScale;
		ShaderParameters.UncoveredMinStepScale = 0.0f;//GLumenSceneGlobalSDFUncoveredMinStepScale;
	}
	else
	{
		ShaderParameters.GlobalDistanceFieldPageAtlasTexture = GBlackVolumeTexture->TextureRHI;
		ShaderParameters.GlobalDistanceFieldCoverageAtlasTexture = GBlackVolumeTexture->TextureRHI;
		ShaderParameters.GlobalDistanceFieldPageTableTexture = GBlackUintVolumeTexture->TextureRHI;
		ShaderParameters.GlobalDistanceFieldMipTexture = GBlackVolumeTexture->TextureRHI;

		for (int32 Index = 0; Index < GlobalDistanceField::MaxClipmaps; Index++)
		{
			ShaderParameters.GlobalVolumeCenterAndExtent[Index] = FVector4f::Zero();
			ShaderParameters.GlobalVolumeWorldToUVAddAndMul[Index] = FVector4f::Zero();
			ShaderParameters.GlobalDistanceFieldMipWorldToUVScale[Index] = FVector4f::Zero();
			ShaderParameters.GlobalDistanceFieldMipWorldToUVBias[Index] = FVector4f::Zero();
		}

		ShaderParameters.GlobalDistanceFieldMipFactor = 0.0f;
		ShaderParameters.GlobalDistanceFieldMipTransition = 0.0f;
		ShaderParameters.GlobalDistanceFieldClipmapSizeInPages = 0;
		ShaderParameters.GlobalDistanceFieldInvPageAtlasSize = FVector3f::ZeroVector;
		ShaderParameters.GlobalDistanceFieldInvCoverageAtlasSize = FVector3f::ZeroVector;
		ShaderParameters.GlobalVolumeDimension = 0.0f;
		ShaderParameters.GlobalVolumeTexelSize = 0.0f;
		ShaderParameters.MaxGlobalDFAOConeDistance = 0.0f;
		ShaderParameters.NumGlobalSDFClipmaps = 0;
		ShaderParameters.FullyCoveredExpandSurfaceScale = 0.0f;
		ShaderParameters.UncoveredExpandSurfaceScale = 0.0f;
		ShaderParameters.UncoveredMinStepScale = 0.0f;
		ShaderParameters.FullyCoveredExpandSurfaceScale = 0.0f;//GLumenSceneGlobalSDFFullyCoveredExpandSurfaceScale;
		ShaderParameters.UncoveredExpandSurfaceScale = 0.0f;//GLumenSceneGlobalSDFUncoveredExpandSurfaceScale;
		ShaderParameters.UncoveredMinStepScale = 0.0f;//GLumenSceneGlobalSDFUncoveredMinStepScale;
	}
}

void FNiagaraDistanceFieldHelper::SetMeshDistanceFieldParameters(FRDGBuilder& GraphBuilder, const FDistanceFieldSceneData* OptionalDistanceFieldData, FDistanceFieldObjectBufferParameters& ObjectShaderParameters, FDistanceFieldAtlasParameters& AtlasShaderParameters, FRHIShaderResourceView* DummyFloat4Buffer)
{
	if (OptionalDistanceFieldData != nullptr && OptionalDistanceFieldData->NumObjectsInBuffer > 0)
	{
		ObjectShaderParameters = DistanceField::SetupObjectBufferParameters(GraphBuilder, *OptionalDistanceFieldData);
		AtlasShaderParameters = DistanceField::SetupAtlasParameters(GraphBuilder, *OptionalDistanceFieldData);
	}
	else
	{
		FRDGBufferSRVRef DefaultVector4 = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FVector4f))));
		FRDGBufferSRVRef DefaultUInt32 = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(uint32))));

		ObjectShaderParameters.SceneObjectBounds = DummyFloat4Buffer;
		ObjectShaderParameters.SceneObjectData = DummyFloat4Buffer;
		ObjectShaderParameters.NumSceneObjects = 0;
		ObjectShaderParameters.SceneHeightfieldObjectBounds = DefaultVector4;
		ObjectShaderParameters.SceneHeightfieldObjectData = DefaultVector4;
		ObjectShaderParameters.NumSceneHeightfieldObjects = 0;

		AtlasShaderParameters.SceneDistanceFieldAssetData = DefaultVector4;
		AtlasShaderParameters.DistanceFieldIndirectionTable = DefaultUInt32;
		AtlasShaderParameters.DistanceFieldIndirection2Table = DefaultVector4;
		AtlasShaderParameters.DistanceFieldIndirectionAtlas = FRDGSystemTextures::Get(GraphBuilder).VolumetricBlack;
		AtlasShaderParameters.DistanceFieldBrickTexture = GBlackVolumeTexture->GetTextureRHI();
		AtlasShaderParameters.DistanceFieldSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		AtlasShaderParameters.DistanceFieldBrickSize = FVector3f::ZeroVector;
		AtlasShaderParameters.DistanceFieldUniqueDataBrickSize = FVector3f::ZeroVector;
		AtlasShaderParameters.DistanceFieldBrickAtlasSizeInBricks = FIntVector::ZeroValue;
		AtlasShaderParameters.DistanceFieldBrickAtlasMask = FIntVector::ZeroValue;
		AtlasShaderParameters.DistanceFieldBrickAtlasSizeLog2 = FIntVector::ZeroValue;
		AtlasShaderParameters.DistanceFieldBrickAtlasTexelSize = FVector3f::ZeroVector;
		AtlasShaderParameters.DistanceFieldBrickAtlasHalfTexelSize = FVector3f::ZeroVector;
		AtlasShaderParameters.DistanceFieldBrickOffsetToAtlasUVScale = FVector3f::ZeroVector;
		AtlasShaderParameters.DistanceFieldUniqueDataBrickSizeInAtlasTexels = FVector3f::ZeroVector;
	}
}
