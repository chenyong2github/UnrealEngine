// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/MaterialMerging.h"
#include "StaticParameterSet.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "MaterialUtilities.h"
#include "Materials/MaterialInstanceConstant.h"
#include "IMeshMergeUtilities.h"
#include "MeshMergeModule.h"
#include "Modules/ModuleManager.h"

namespace ProxyMaterialUtilities
{
	static const bool CalculatePackedTextureData(const FFlattenMaterial& InMaterial, bool& bOutPackMetallic, bool& bOutPackSpecular, bool& bOutPackRoughness, int32& OutNumSamples, FIntPoint& OutSize)
	{
		// Whether or not a material property is baked down
		const bool bHasMetallic = InMaterial.DoesPropertyContainData(EFlattenMaterialProperties::Metallic) && !InMaterial.IsPropertyConstant(EFlattenMaterialProperties::Metallic);
		const bool bHasRoughness = InMaterial.DoesPropertyContainData(EFlattenMaterialProperties::Roughness) && !InMaterial.IsPropertyConstant(EFlattenMaterialProperties::Roughness);
		const bool bHasSpecular = InMaterial.DoesPropertyContainData(EFlattenMaterialProperties::Specular) && !InMaterial.IsPropertyConstant(EFlattenMaterialProperties::Specular);

		// Check for same texture sizes
		bool bSameTextureSize = false;

		// Determine whether or not the properties sizes match


		const FIntPoint MetallicSize = InMaterial.GetPropertySize(EFlattenMaterialProperties::Metallic);
		const FIntPoint SpecularSize = InMaterial.GetPropertySize(EFlattenMaterialProperties::Specular);
		const FIntPoint RoughnessSize = InMaterial.GetPropertySize(EFlattenMaterialProperties::Roughness);

		bSameTextureSize = (MetallicSize == RoughnessSize) || (MetallicSize == SpecularSize);
		if (bSameTextureSize)
		{
			OutSize = MetallicSize;
			OutNumSamples = InMaterial.GetPropertySamples(EFlattenMaterialProperties::Metallic).Num();
		}
		else
		{
			bSameTextureSize = (RoughnessSize == SpecularSize);
			if (bSameTextureSize)
			{
				OutSize = RoughnessSize;
				OutNumSamples = InMaterial.GetPropertySamples(EFlattenMaterialProperties::Roughness).Num();
			}
		}

		// Now that we know if the data matches determine whether or not we should pack the properties
		int32 NumPacked = 0;
		if (OutNumSamples != 0)
		{
			bOutPackMetallic = bHasMetallic ? (OutNumSamples == InMaterial.GetPropertySamples(EFlattenMaterialProperties::Metallic).Num()) : false;
			NumPacked += (bOutPackMetallic) ? 1 : 0;
			bOutPackRoughness = bHasRoughness ? (OutNumSamples == InMaterial.GetPropertySamples(EFlattenMaterialProperties::Roughness).Num()) : false;
			NumPacked += (bOutPackRoughness) ? 1 : 0;
			bOutPackSpecular = bHasSpecular ? (OutNumSamples == InMaterial.GetPropertySamples(EFlattenMaterialProperties::Specular).Num()) : false;
			NumPacked += (bOutPackSpecular) ? 1 : 0;
		}
		else
		{
			bOutPackMetallic = bOutPackRoughness = bOutPackSpecular = false;
		}

		// Need atleast two properties to pack
		return NumPacked >= 2;
	}

	static UMaterialInstanceConstant* CreateProxyMaterialInstance(UPackage* InOuter, const FMaterialProxySettings& InMaterialProxySettings, UMaterialInterface* InBaseMaterial, const FFlattenMaterial& FlattenMaterial, const FString& AssetBasePath, const FString& AssetBaseName, TArray<UObject*>& OutAssetsToSync, FMaterialUpdateContext* MaterialUpdateContext = nullptr)
	{
		UMaterialInterface* BaseMaterial = InBaseMaterial;
		
		const IMeshMergeUtilities& Module = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
		if (!Module.IsValidBaseMaterial(InBaseMaterial, false))
		{
			BaseMaterial = GEngine->DefaultFlattenMaterial;
		} 

		UMaterialInstanceConstant* OutMaterial = FMaterialUtilities::CreateInstancedMaterial(BaseMaterial, InOuter, AssetBasePath + AssetBaseName, RF_Public | RF_Standalone);
		OutAssetsToSync.Add(OutMaterial);

		OutMaterial->BasePropertyOverrides.TwoSided = FlattenMaterial.bTwoSided && InMaterialProxySettings.bAllowTwoSidedMaterial;
		OutMaterial->BasePropertyOverrides.bOverride_TwoSided = (FlattenMaterial.bTwoSided != false) && InMaterialProxySettings.bAllowTwoSidedMaterial;
		OutMaterial->BasePropertyOverrides.DitheredLODTransition = FlattenMaterial.bDitheredLODTransition;
		OutMaterial->BasePropertyOverrides.bOverride_DitheredLODTransition = FlattenMaterial.bDitheredLODTransition != false;

		if (InMaterialProxySettings.BlendMode != BLEND_Opaque)
		{
			OutMaterial->BasePropertyOverrides.bOverride_BlendMode = true;
			OutMaterial->BasePropertyOverrides.BlendMode = InMaterialProxySettings.BlendMode;
		}

		bool bPackMetallic, bPackSpecular, bPackRoughness;
		int32 NumSamples = 0;
		FIntPoint PackedSize;
		const bool bPackTextures = CalculatePackedTextureData(FlattenMaterial, bPackMetallic, bPackSpecular, bPackRoughness, NumSamples, PackedSize);

		FStaticParameterSet NewStaticParameterSet;

		if(FlattenMaterial.UVChannel != 0)
		{
			// If the used texture coordinate was not the default UV0 set the appropriate one on the instance material
			FStaticSwitchParameter SwitchParameter;
			SwitchParameter.ParameterInfo.Name = TEXT("UseCustomUV");
			SwitchParameter.Value = true;
			SwitchParameter.bOverride = true;
			NewStaticParameterSet.StaticSwitchParameters.Add(SwitchParameter);

			SwitchParameter.ParameterInfo.Name = *(TEXT("UseUV") + FString::FromInt(FlattenMaterial.UVChannel));
			NewStaticParameterSet.StaticSwitchParameters.Add(SwitchParameter);
		}

		auto CreateTextureFromDefault = [&](const FName TextureName, const FString& AssetLongName, FIntPoint Size, const TArray<FColor>& Samples)
		{
			bool bSRGB = false;
			bool bVirtualTextureStreaming = false;
			TextureCompressionSettings CompressionSettings = TextureCompressionSettings::TC_Default;
			TextureGroup LODGroup = TextureGroup::TEXTUREGROUP_World;

			UTexture* DefaultTexture = nullptr;
			OutMaterial->GetTextureParameterValue(TextureName, DefaultTexture);
			if (ensure(DefaultTexture))
			{
				bSRGB = DefaultTexture->SRGB;
				bVirtualTextureStreaming = DefaultTexture->VirtualTextureStreaming;
				CompressionSettings = DefaultTexture->CompressionSettings;
				LODGroup = DefaultTexture->LODGroup;
			}

			UTexture2D* Texture = FMaterialUtilities::CreateTexture(InOuter, AssetLongName, Size, Samples, CompressionSettings, LODGroup, RF_Public | RF_Standalone, bSRGB);
			Texture->VirtualTextureStreaming = bVirtualTextureStreaming;
			Texture->PostEditChange();
			return Texture;
		};

		auto SetTextureParam = [&](const FString& PropertyName, EFlattenMaterialProperties FlattenProperty)
		{
			if (FlattenMaterial.DoesPropertyContainData(FlattenProperty) && !FlattenMaterial.IsPropertyConstant(FlattenProperty))
			{
				const FName TextureName(PropertyName + TEXT("Texture"));
				const FName UseTexture(TEXT("Use") + PropertyName);

				UTexture2D* Texture = CreateTextureFromDefault(TextureName, AssetBasePath + TEXT("T_") + AssetBaseName + TEXT("_") + PropertyName, FlattenMaterial.GetPropertySize(FlattenProperty), FlattenMaterial.GetPropertySamples(FlattenProperty));

				FStaticSwitchParameter SwitchParameter;
				SwitchParameter.ParameterInfo.Name = UseTexture;
				SwitchParameter.Value = true;
				SwitchParameter.bOverride = true;
				NewStaticParameterSet.StaticSwitchParameters.Add(SwitchParameter);

				OutMaterial->SetTextureParameterValueEditorOnly(TextureName, Texture);
			
				OutAssetsToSync.Add(Texture);
			}
		};

		auto SetTextureParamConstVector = [&](const FString& PropertyName, EFlattenMaterialProperties FlattenProperty)
		{
			if (FlattenMaterial.DoesPropertyContainData(FlattenProperty) && !FlattenMaterial.IsPropertyConstant(FlattenProperty))
			{
				SetTextureParam(PropertyName, FlattenProperty);
			} 
			else
			{
				const FName ConstName(PropertyName + TEXT("Const"));
				OutMaterial->SetVectorParameterValueEditorOnly(ConstName, FlattenMaterial.GetPropertySamples(FlattenProperty)[0]);
			} 
		};

		auto SetTextureParamConstScalar = [&](const FString& PropertyName, EFlattenMaterialProperties FlattenProperty, float ConstantValue)
		{
			if (FlattenMaterial.DoesPropertyContainData(FlattenProperty) && !FlattenMaterial.IsPropertyConstant(FlattenProperty))
			{
				SetTextureParam(PropertyName, FlattenProperty);
			}
			else
			{
				const FName ConstName(PropertyName + TEXT("Const"));
				FLinearColor Colour = FlattenMaterial.IsPropertyConstant(FlattenProperty) ? FLinearColor::FromSRGBColor(FlattenMaterial.GetPropertySamples(FlattenProperty)[0]) : FLinearColor(ConstantValue, 0, 0, 0);
				OutMaterial->SetScalarParameterValueEditorOnly(ConstName, Colour.R);
			}
		};

		// Load textures and set switches accordingly
		if (FlattenMaterial.GetPropertySize(EFlattenMaterialProperties::Diffuse).Num() > 0 && !(FlattenMaterial.IsPropertyConstant(EFlattenMaterialProperties::Diffuse) && FlattenMaterial.GetPropertySamples(EFlattenMaterialProperties::Diffuse)[0] == FColor::Black))
		{
			SetTextureParamConstVector("BaseColor", EFlattenMaterialProperties::Diffuse);
		}

		if (FlattenMaterial.GetPropertySize(EFlattenMaterialProperties::Normal).Num() > 1)
		{
			SetTextureParam("Normal", EFlattenMaterialProperties::Normal);
		}

		// Determine whether or not specific material properties are packed together into one texture (requires at least two to match (number of samples and texture size) in order to be packed
		if (!bPackMetallic && (FlattenMaterial.GetPropertySize(EFlattenMaterialProperties::Metallic).Num() > 0 || !InMaterialProxySettings.bMetallicMap))
		{
			SetTextureParamConstScalar("Metallic", EFlattenMaterialProperties::Metallic, InMaterialProxySettings.MetallicConstant);
		}

		if (!bPackRoughness && (FlattenMaterial.GetPropertySize(EFlattenMaterialProperties::Roughness).Num() > 0 || !InMaterialProxySettings.bRoughnessMap))
		{
			SetTextureParamConstScalar("Roughness", EFlattenMaterialProperties::Roughness, InMaterialProxySettings.RoughnessConstant);
		}

		if (!bPackSpecular && (FlattenMaterial.GetPropertySize(EFlattenMaterialProperties::Specular).Num() > 0 || !InMaterialProxySettings.bSpecularMap))
		{
			SetTextureParamConstScalar("Specular", EFlattenMaterialProperties::Specular, InMaterialProxySettings.SpecularConstant);
		}

		if (FlattenMaterial.GetPropertySize(EFlattenMaterialProperties::Opacity).Num() > 0 || !InMaterialProxySettings.bOpacityMap)
		{
			SetTextureParamConstScalar("Opacity", EFlattenMaterialProperties::Opacity, InMaterialProxySettings.OpacityConstant);
		}

		if (FlattenMaterial.GetPropertySize(EFlattenMaterialProperties::OpacityMask).Num() > 0 || !InMaterialProxySettings.bOpacityMaskMap)
		{
			SetTextureParamConstScalar("OpacityMask", EFlattenMaterialProperties::OpacityMask, InMaterialProxySettings.OpacityMaskConstant);
		}

		if (FlattenMaterial.GetPropertySize(EFlattenMaterialProperties::AmbientOcclusion).Num() > 0 || !InMaterialProxySettings.bAmbientOcclusionMap)
		{
			SetTextureParamConstScalar("AmbientOcclusion", EFlattenMaterialProperties::AmbientOcclusion, InMaterialProxySettings.AmbientOcclusionConstant);
		}

		// Handle the packed texture if applicable
		if (bPackTextures)
		{
			TArray<FColor> MergedTexture;
			MergedTexture.AddZeroed(NumSamples);

			// Merge properties into one texture using the separate colour channels
			const EFlattenMaterialProperties Properties[3] = { EFlattenMaterialProperties::Metallic , EFlattenMaterialProperties::Roughness, EFlattenMaterialProperties::Specular };

			//Property that is not part of the pack (because of a different size), will see is reserve pack space fill with Black Color.
			const bool PropertyShouldBePack[3] = { bPackMetallic , bPackRoughness , bPackSpecular };

			// Red mask (all properties are rendered into the red channel)
			FColor NonAlphaRed = FColor::Red;
			NonAlphaRed.A = 0;
			const uint32 ColorMask = NonAlphaRed.DWColor();
			const uint32 Shift[3] = { 0, 8, 16 };
			for (int32 PropertyIndex = 0; PropertyIndex < 3; ++PropertyIndex)
			{
				const EFlattenMaterialProperties Property = Properties[PropertyIndex];
				const bool HasProperty = PropertyShouldBePack[PropertyIndex] && FlattenMaterial.DoesPropertyContainData(Property) && !FlattenMaterial.IsPropertyConstant(Property);

				if (HasProperty)
				{
					const TArray<FColor>& PropertySamples = FlattenMaterial.GetPropertySamples(Property);
					// OR masked values (samples initialized to zero, so no random data)
					for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
					{
						// Black adds the alpha + red channel value shifted into the correct output channel
						MergedTexture[SampleIndex].DWColor() |= (FColor::Black.DWColor() + ((PropertySamples[SampleIndex].DWColor() & ColorMask) >> Shift[PropertyIndex]));
					}
				}
			}

			// Create texture using the merged property data
			const FName PackedTextureName(TEXT("PackedTexture"));
			UTexture2D* PackedTexture = CreateTextureFromDefault(PackedTextureName, AssetBasePath + TEXT("T_") + AssetBaseName + TEXT("_MRS"), PackedSize, MergedTexture);
			checkf(PackedTexture, TEXT("Failed to create texture"));
			OutAssetsToSync.Add(PackedTexture);

			// Setup switches for whether or not properties will be packed into one texture
			FStaticSwitchParameter SwitchParameter;
			SwitchParameter.ParameterInfo.Name = TEXT("PackMetallic");
			SwitchParameter.Value = bPackMetallic;
			SwitchParameter.bOverride = true;
			NewStaticParameterSet.StaticSwitchParameters.Add(SwitchParameter);

			SwitchParameter.ParameterInfo.Name = TEXT("PackSpecular");
			SwitchParameter.Value = bPackSpecular;
			SwitchParameter.bOverride = true;
			NewStaticParameterSet.StaticSwitchParameters.Add(SwitchParameter);

			SwitchParameter.ParameterInfo.Name = TEXT("PackRoughness");
			SwitchParameter.Value = bPackRoughness;
			SwitchParameter.bOverride = true;
			NewStaticParameterSet.StaticSwitchParameters.Add(SwitchParameter);

			// Set up switch and texture values
			OutMaterial->SetTextureParameterValueEditorOnly(PackedTextureName, PackedTexture);
		}

		// Emissive is a special case due to the scaling variable
		if (FlattenMaterial.GetPropertySamples(EFlattenMaterialProperties::Emissive).Num() > 0 && !(FlattenMaterial.GetPropertySize(EFlattenMaterialProperties::Emissive).Num() == 1 && FlattenMaterial.GetPropertySamples(EFlattenMaterialProperties::Emissive)[0] == FColor::Black))
		{
			SetTextureParamConstVector("EmissiveColor", EFlattenMaterialProperties::Emissive);

			if (FlattenMaterial.EmissiveScale != 1.0f)
			{
				FMaterialParameterInfo ParameterInfo(TEXT("EmissiveScale"));
				OutMaterial->SetScalarParameterValueEditorOnly(ParameterInfo, FlattenMaterial.EmissiveScale);
			}
		}

		// Force initializing the static permutations according to the switches we have set
		OutMaterial->UpdateStaticPermutation(NewStaticParameterSet, MaterialUpdateContext);
		OutMaterial->InitStaticPermutation();

		OutMaterial->PostEditChange();

		return OutMaterial;
	}	

	static UMaterialInstanceConstant* CreateProxyMaterialInstance(UPackage* InOuter, const FMaterialProxySettings& InMaterialProxySettings, FFlattenMaterial& FlattenMaterial, const FString& AssetBasePath, const FString& AssetBaseName, TArray<UObject*>& OutAssetsToSync, FMaterialUpdateContext* MaterialUpdateContext = nullptr)
	{
		return CreateProxyMaterialInstance(InOuter, InMaterialProxySettings, GEngine->DefaultFlattenMaterial, FlattenMaterial, AssetBasePath, AssetBasePath, OutAssetsToSync, MaterialUpdateContext);
	}

};
