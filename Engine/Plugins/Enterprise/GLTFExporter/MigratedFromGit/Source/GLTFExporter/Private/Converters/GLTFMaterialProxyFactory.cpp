// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "Converters/GLTFMaterialProxyFactory.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Converters/GLTFImageUtility.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialUtilities.h"
#include "ImageUtils.h"

FGLTFMaterialProxyFactory::FGLTFMaterialProxyFactory(const UGLTFProxyOptions* Options)
	: Builder(TEXT(""), CreateExportOptions(Options))
{
	Builder.ImageConverter = CreateCustomImageConverter();
}

UMaterialInterface* FGLTFMaterialProxyFactory::Create(UMaterialInterface* OriginalMaterial)
{
	if (FGLTFMaterialUtility::IsProxyMaterial(OriginalMaterial))
	{
		return OriginalMaterial;
	}

	if (FGLTFMaterialUtility::NeedsMeshData(OriginalMaterial))
	{
		Builder.LogWarning(FString::Printf(
			TEXT("Material %s uses mesh data but prebaking will only use a simple quad as mesh data currently"),
			*OriginalMaterial->GetName()));
	}

	const FGLTFJsonMaterialIndex MaterialIndex = Builder.GetOrAddMaterial(OriginalMaterial);
	if (MaterialIndex == INDEX_NONE)
	{
		// TODO: report error
		return nullptr;
	}

	MakeDirectory(RootPath);

	Builder.CompleteAllTasks();
	const FGLTFJsonMaterial& JsonMaterial = Builder.GetMaterial(MaterialIndex);

	UMaterialInstanceConstant* ProxyMaterial = CreateInstancedMaterial(OriginalMaterial, JsonMaterial.ShadingModel);
	if (ProxyMaterial != nullptr)
	{
		SetProxyProperties(ProxyMaterial, JsonMaterial);
	}

	return ProxyMaterial;
}

void FGLTFMaterialProxyFactory::SetProxyProperties(UMaterialInstanceConstant* ProxyMaterial, const FGLTFJsonMaterial& JsonMaterial)
{
	SetProxyProperty(ProxyMaterial, TEXT("Base Color Factor"), JsonMaterial.PBRMetallicRoughness.BaseColorFactor);
	SetProxyProperty(ProxyMaterial, TEXT("Base Color"), JsonMaterial.PBRMetallicRoughness.BaseColorTexture, EGLTFMaterialPropertyGroup::BaseColorOpacity);

	if (JsonMaterial.ShadingModel == EGLTFJsonShadingModel::Default || JsonMaterial.ShadingModel == EGLTFJsonShadingModel::ClearCoat)
	{
		SetProxyProperty(ProxyMaterial, TEXT("Emissive Factor"), JsonMaterial.EmissiveFactor);
		SetProxyProperty(ProxyMaterial, TEXT("Emissive"), JsonMaterial.EmissiveTexture, EGLTFMaterialPropertyGroup::EmissiveColor);

		SetProxyProperty(ProxyMaterial, TEXT("Metallic Factor"), JsonMaterial.PBRMetallicRoughness.MetallicFactor);
		SetProxyProperty(ProxyMaterial, TEXT("Roughness Factor"), JsonMaterial.PBRMetallicRoughness.RoughnessFactor);
		SetProxyProperty(ProxyMaterial, TEXT("Metallic Roughness"), JsonMaterial.PBRMetallicRoughness.MetallicRoughnessTexture, EGLTFMaterialPropertyGroup::MetallicRoughness);

		SetProxyProperty(ProxyMaterial, TEXT("Normal Scale"), JsonMaterial.NormalTexture.Scale);
		SetProxyProperty(ProxyMaterial, TEXT("Normal"), JsonMaterial.NormalTexture, EGLTFMaterialPropertyGroup::Normal);

		SetProxyProperty(ProxyMaterial, TEXT("Occlusion Strength"), JsonMaterial.OcclusionTexture.Strength);
		SetProxyProperty(ProxyMaterial, TEXT("Occlusion"), JsonMaterial.OcclusionTexture, EGLTFMaterialPropertyGroup::AmbientOcclusion);

		if (JsonMaterial.ShadingModel == EGLTFJsonShadingModel::ClearCoat)
		{
			SetProxyProperty(ProxyMaterial, TEXT("Clear Coat Factor"), JsonMaterial.ClearCoat.ClearCoatFactor);
			SetProxyProperty(ProxyMaterial, TEXT("Clear Coat"), JsonMaterial.ClearCoat.ClearCoatTexture, EGLTFMaterialPropertyGroup::ClearCoatRoughness); // TODO: add property group for clear coat intensity only

			SetProxyProperty(ProxyMaterial, TEXT("Clear Coat Roughness Factor"), JsonMaterial.ClearCoat.ClearCoatRoughnessFactor);
			SetProxyProperty(ProxyMaterial, TEXT("Clear Coat Roughness"), JsonMaterial.ClearCoat.ClearCoatRoughnessTexture, EGLTFMaterialPropertyGroup::ClearCoatRoughness);

			SetProxyProperty(ProxyMaterial, TEXT("Clear Coat Normal Scale"), JsonMaterial.ClearCoat.ClearCoatNormalTexture.Scale);
			SetProxyProperty(ProxyMaterial, TEXT("Clear Coat Normal"), JsonMaterial.ClearCoat.ClearCoatNormalTexture, EGLTFMaterialPropertyGroup::ClearCoatBottomNormal);
		}
	}

	ProxyMaterial->PostEditChange();
}

void FGLTFMaterialProxyFactory::SetProxyProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, float Scalar)
{
	float DefaultValue;
	if (!ProxyMaterial->GetScalarParameterDefaultValue(*PropertyName, DefaultValue))
	{
		// TODO: report error
		return;
	}

	if (DefaultValue != Scalar)
	{
		ProxyMaterial->SetScalarParameterValueEditorOnly(*PropertyName, Scalar);
	}
}

void FGLTFMaterialProxyFactory::SetProxyProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, const FGLTFJsonColor3& Color)
{
	FLinearColor DefaultValue;
	if (!ProxyMaterial->GetVectorParameterDefaultValue(*PropertyName, DefaultValue))
	{
		// TODO: report error
		return;
	}

	const FLinearColor Value(Color.R, Color.G, Color.B);
	if (DefaultValue != Value)
	{
		ProxyMaterial->SetVectorParameterValueEditorOnly(*PropertyName, Value);
	}
}

void FGLTFMaterialProxyFactory::SetProxyProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, const FGLTFJsonColor4& Color)
{
	FLinearColor DefaultValue;
	if (!ProxyMaterial->GetVectorParameterDefaultValue(*PropertyName, DefaultValue))
	{
		// TODO: report error
		return;
	}

	const FLinearColor Value(Color.R, Color.G, Color.B, Color.A);
	if (DefaultValue != Value)
	{
		ProxyMaterial->SetVectorParameterValueEditorOnly(*PropertyName, Value);
	}
}

void FGLTFMaterialProxyFactory::SetProxyProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, const FGLTFJsonTextureInfo& TextureInfo, EGLTFMaterialPropertyGroup PropertyGroup)
{
	if (TextureInfo.Index != INDEX_NONE)
	{
		const UTexture* Texture = FindOrCreateTexture(TextureInfo.Index, PropertyGroup);
		if (Texture == nullptr)
		{
			// TODO: report error
		}

		ProxyMaterial->SetTextureParameterValueEditorOnly(*(PropertyName + TEXT(" Texture")), const_cast<UTexture*>(Texture));
	}

	if (TextureInfo.TexCoord != 0)
	{
		ProxyMaterial->SetScalarParameterValueEditorOnly(*(PropertyName + TEXT(" UV Index")), TextureInfo.TexCoord);
	}

	if (TextureInfo.Transform.Offset != FGLTFJsonVector2::Zero)
	{
		const FLinearColor Offset(TextureInfo.Transform.Offset.X, TextureInfo.Transform.Offset.Y, 0, 0);
		ProxyMaterial->SetVectorParameterValueEditorOnly(*(PropertyName + TEXT(" UV Offset")), Offset);
	}

	if (TextureInfo.Transform.Scale != FGLTFJsonVector2::One)
	{
		const FLinearColor Scale(TextureInfo.Transform.Scale.X, TextureInfo.Transform.Scale.Y, 0, 0);
		ProxyMaterial->SetVectorParameterValueEditorOnly(*(PropertyName + TEXT(" UV Scale")), Scale);
	}

	if (TextureInfo.Transform.Rotation != 0)
	{
		ProxyMaterial->SetScalarParameterValueEditorOnly(*(PropertyName + TEXT(" UV Rotation")), TextureInfo.Transform.Rotation);
	}
}

const UTexture2D* FGLTFMaterialProxyFactory::FindOrCreateTexture(FGLTFJsonTextureIndex Index, EGLTFMaterialPropertyGroup PropertyGroup)
{
	// TODO: fix potential conflict when same texture used for different material properties that have different encoding (sRGB vs Linear, Normalmap compression etc)

	const UTexture2D** FoundPtr = Textures.Find(Index);
	if (FoundPtr != nullptr)
	{
		return *FoundPtr;
	}

	const FGLTFJsonTexture& JsonTexture = Builder.GetTexture(Index);
	const FGLTFImageData* ImageData = Images.Find(JsonTexture.Source);
	if (ImageData == nullptr)
	{
		return nullptr;
	}

	const FGLTFJsonSampler& JsonSampler = Builder.GetSampler(JsonTexture.Sampler);
	const UTexture2D* Texture = CreateTexture(ImageData, JsonSampler, PropertyGroup);
	Textures.Add(Index, Texture);
	return Texture;
}

UTexture2D* FGLTFMaterialProxyFactory::CreateTexture(const FGLTFImageData* ImageData, const FGLTFJsonSampler& JsonSampler, EGLTFMaterialPropertyGroup PropertyGroup)
{
	const bool bSRGB = PropertyGroup == EGLTFMaterialPropertyGroup::BaseColorOpacity || PropertyGroup == EGLTFMaterialPropertyGroup::EmissiveColor;
	const bool bNormalMap = PropertyGroup == EGLTFMaterialPropertyGroup::Normal || PropertyGroup == EGLTFMaterialPropertyGroup::ClearCoatBottomNormal;

	const FString PackageName = RootPath / TEXT("T_GLTF_") + ImageData->Filename;
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	Package->Modify();

	FCreateTexture2DParameters TexParams;
	TexParams.bUseAlpha = !ImageData->bIgnoreAlpha;
	TexParams.CompressionSettings = bNormalMap ? TC_Normalmap : TC_Default;
	TexParams.bDeferCompression = true;
	TexParams.bSRGB = bSRGB;
	TexParams.TextureGroup = bNormalMap ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_World;
	TexParams.SourceGuidHash = FGuid();

	UTexture2D* Texture = FImageUtils::CreateTexture2D(ImageData->Size.X, ImageData->Size.Y, *ImageData->Pixels, Package, FPaths::GetCleanFilename(Package->GetName()), RF_Public | RF_Standalone, TexParams);
	Texture->Filter = ConvertFilter(JsonSampler.MagFilter);
	Texture->AddressX = ConvertWrap(JsonSampler.WrapS);
	Texture->AddressY = ConvertWrap(JsonSampler.WrapT);

	Texture->PostEditChange();
	return Texture;
}

UMaterialInstanceConstant* FGLTFMaterialProxyFactory::CreateInstancedMaterial(UMaterialInterface* OriginalMaterial, EGLTFJsonShadingModel ShadingModel)
{
	const FString PackageName = RootPath / TEXT("M_GLTF_") + OriginalMaterial->GetName();
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	Package->Modify();

	UMaterialInterface* BaseMaterial = FGLTFMaterialUtility::GetProxyBaseMaterial(ShadingModel);
	if (BaseMaterial == nullptr)
	{
		Builder.LogError(FString::Printf(
			TEXT("Can't create proxy for material %s, because shading model %s has no base material"),
			*OriginalMaterial->GetName(),
			FGLTFJsonUtility::GetValue(ShadingModel)));
		return nullptr;
	}

	const FString BaseName = TEXT("GLTF_") + OriginalMaterial->GetName(); // prefix "M_" is added automatically by CreateInstancedMaterial
	UMaterialInstanceConstant* ProxyMaterial = FMaterialUtilities::CreateInstancedMaterial(BaseMaterial, Package, BaseName, RF_Public | RF_Standalone);

	const bool bTwoSided = OriginalMaterial->IsTwoSided();
	if (bTwoSided != BaseMaterial->IsTwoSided())
	{
		ProxyMaterial->BasePropertyOverrides.bOverride_TwoSided = true;
		ProxyMaterial->BasePropertyOverrides.TwoSided = bTwoSided;
	}

	const EBlendMode BlendMode = OriginalMaterial->GetBlendMode();
	if (BlendMode != BaseMaterial->GetBlendMode())
	{
		ProxyMaterial->BasePropertyOverrides.bOverride_BlendMode = true;
		ProxyMaterial->BasePropertyOverrides.BlendMode = BlendMode;
	}

	const float OpacityMaskClipValue = OriginalMaterial->GetOpacityMaskClipValue();
	if (OpacityMaskClipValue != BaseMaterial->GetOpacityMaskClipValue())
	{
		ProxyMaterial->BasePropertyOverrides.bOverride_OpacityMaskClipValue = true;
		ProxyMaterial->BasePropertyOverrides.OpacityMaskClipValue = OpacityMaskClipValue;
	}

	UGLTFMaterialExportOptions* UserData = OriginalMaterial->GetAssetUserData<UGLTFMaterialExportOptions>();
	if (UserData == nullptr)
	{
		UserData = NewObject<UGLTFMaterialExportOptions>();
		OriginalMaterial->AddAssetUserData(UserData);
	}

	UserData->Proxy = ProxyMaterial;
	OriginalMaterial->Modify();
	return ProxyMaterial;
}

TUniquePtr<IGLTFImageConverter> FGLTFMaterialProxyFactory::CreateCustomImageConverter()
{
	class FGLTFCustomImageConverter final: public IGLTFImageConverter
	{
	public:

		FGLTFMaterialProxyFactory& Proxyr;
		TSet<FString> UniqueFilenames;

		FGLTFCustomImageConverter(FGLTFMaterialProxyFactory& Proxyr)
			: Proxyr(Proxyr)
		{
		}

	protected:

		virtual FGLTFJsonImageIndex Convert(TGLTFSuperfluous<FString> Name, EGLTFTextureType Type, bool bIgnoreAlpha, FIntPoint Size, TGLTFSharedArray<FColor> Pixels) override
		{
			const FString Filename = FGLTFImageUtility::GetUniqueFilename(Name, TEXT(""), UniqueFilenames);
			UniqueFilenames.Add(Filename);

			const FGLTFJsonImageIndex ImageIndex = Proxyr.Builder.AddImage();
			Proxyr.Images.Add(ImageIndex, { Filename, Type, bIgnoreAlpha, Size, Pixels });
			return ImageIndex;
		}
	};

	return MakeUnique<FGLTFCustomImageConverter>(*this);
}

bool FGLTFMaterialProxyFactory::MakeDirectory(const FString& PackagePath)
{
	const FString DirPath = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(PackagePath + TEXT("/")));
	if (DirPath.IsEmpty())
	{
		return false;
	}

	bool bResult = true;
	if (!IFileManager::Get().DirectoryExists(*DirPath))
	{
		bResult = IFileManager::Get().MakeDirectory(*DirPath, true);
	}

	if (bResult)
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().AddPath(PackagePath);
	}

	return bResult;
}

UGLTFExportOptions* FGLTFMaterialProxyFactory::CreateExportOptions(const UGLTFProxyOptions* ProxyOptions)
{
	UGLTFExportOptions* ExportOptions = NewObject<UGLTFExportOptions>();
	ExportOptions->ResetToDefault();
	ExportOptions->bExportProxyMaterials = false;
	ExportOptions->BakeMaterialInputs = EGLTFMaterialBakeMode::Simple;
	ExportOptions->DefaultMaterialBakeSize = ProxyOptions->DefaultMaterialBakeSize;
	ExportOptions->DefaultMaterialBakeFilter = ProxyOptions->DefaultMaterialBakeFilter;
	ExportOptions->DefaultMaterialBakeTiling = ProxyOptions->DefaultMaterialBakeTiling;
	ExportOptions->DefaultInputBakeSettings = ProxyOptions->DefaultInputBakeSettings;
	ExportOptions->bAdjustNormalmaps = false;
	return ExportOptions;
}

TextureAddress FGLTFMaterialProxyFactory::ConvertWrap(EGLTFJsonTextureWrap Wrap)
{
	switch (Wrap)
	{
		case EGLTFJsonTextureWrap::Repeat:         return TA_Wrap;
		case EGLTFJsonTextureWrap::MirroredRepeat: return TA_Mirror;
		case EGLTFJsonTextureWrap::ClampToEdge:    return TA_Clamp;
		default:
			checkNoEntry();
			return TA_MAX;
	}
}

TextureFilter FGLTFMaterialProxyFactory::ConvertFilter(EGLTFJsonTextureFilter Filter)
{
	switch (Filter)
	{
		case EGLTFJsonTextureFilter::Nearest:              return TF_Nearest;
		case EGLTFJsonTextureFilter::NearestMipmapNearest: return TF_Nearest;
		case EGLTFJsonTextureFilter::LinearMipmapNearest:  return TF_Bilinear;
		case EGLTFJsonTextureFilter::NearestMipmapLinear:  return TF_Bilinear;
		case EGLTFJsonTextureFilter::Linear:               return TF_Trilinear;
		case EGLTFJsonTextureFilter::LinearMipmapLinear:   return TF_Trilinear;
		default:
			checkNoEntry();
			return TF_MAX;
	}
}

#endif
