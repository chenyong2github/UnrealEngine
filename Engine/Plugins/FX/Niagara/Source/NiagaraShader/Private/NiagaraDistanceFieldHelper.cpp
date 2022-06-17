// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDistanceFieldHelper.h"

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
