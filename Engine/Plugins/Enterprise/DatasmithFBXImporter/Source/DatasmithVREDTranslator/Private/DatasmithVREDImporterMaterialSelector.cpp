// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithVREDImporterMaterialSelector.h"

#include "IDatasmithSceneElements.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Templates/Casts.h"
#include "UObject/SoftObjectPath.h"

#define DEFAULT_MATERIAL_NAME TEXT("UPlasticMaterial")

FDatasmithVREDImporterMaterialSelector::FDatasmithVREDImporterMaterialSelector()
{
	FDatasmithMasterMaterial& Phong = MasterMaterials.Add(TEXT("UPhongMaterial"));
	Phong.FromSoftObjectPath(FSoftObjectPath(TEXT("/DatasmithContent/Materials/FBXImporter/VRED/Phong.Phong")));

	FDatasmithMasterMaterial& Plastic = MasterMaterials.Add(TEXT("UPlasticMaterial"));
	Plastic.FromSoftObjectPath(FSoftObjectPath(TEXT("/DatasmithContent/Materials/FBXImporter/VRED/Plastic.Plastic")));

	FDatasmithMasterMaterial& Glass = MasterMaterials.Add(TEXT("UGlassMaterial"));
	Glass.FromSoftObjectPath(FSoftObjectPath(TEXT("/DatasmithContent/Materials/FBXImporter/VRED/Glass.Glass")));

	FDatasmithMasterMaterial& Chrome = MasterMaterials.Add(TEXT("UChromeMaterial"));
	Chrome.FromSoftObjectPath(FSoftObjectPath(TEXT("/DatasmithContent/Materials/FBXImporter/VRED/Chrome.Chrome")));

	FDatasmithMasterMaterial& BrushedMetal = MasterMaterials.Add(TEXT("UBrushedMetalMaterial"));
	BrushedMetal.FromSoftObjectPath(FSoftObjectPath(TEXT("/DatasmithContent/Materials/FBXImporter/VRED/BrushedMetal.BrushedMetal")));

	FDatasmithMasterMaterial& UnicolorCarpaint = MasterMaterials.Add(TEXT("UUnicolorPaintMaterial"));
	UnicolorCarpaint.FromSoftObjectPath(FSoftObjectPath(TEXT("/DatasmithContent/Materials/FBXImporter/VRED/UnicolorCarpaint.UnicolorCarpaint")));
}

bool FDatasmithVREDImporterMaterialSelector::IsValid() const
{
	for (const auto& Pair : MasterMaterials)
	{
		const FDatasmithMasterMaterial& Mat = Pair.Value;
		if (!Mat.IsValid())
		{
			return false;
		}
	}

	if (!MasterMaterials.Contains(DEFAULT_MATERIAL_NAME))
	{
		return false;
	}

	return true;
}

const FDatasmithMasterMaterial& FDatasmithVREDImporterMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial ) const
{
	const TSharedPtr< IDatasmithKeyValueProperty > TypeProperty = InDatasmithMaterial->GetPropertyByName(TEXT("Type"));
	FString TypeValue;
	if (TypeProperty.IsValid() && GetString(TypeProperty, TypeValue))
	{
		if (const FDatasmithMasterMaterial* FoundMat = MasterMaterials.Find(TypeValue))
		{
			return *FoundMat;
		}
	}

	return MasterMaterials[DEFAULT_MATERIAL_NAME];
}

void FDatasmithVREDImporterMaterialSelector::FinalizeMaterialInstance(const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial, UMaterialInstanceConstant* MaterialInstance) const
{
	const TSharedPtr<IDatasmithKeyValueProperty> OpacityProperty = InDatasmithMaterial->GetPropertyByName(TEXT("Opacity"));
	const TSharedPtr<IDatasmithKeyValueProperty> TransparencyTextureProperty = InDatasmithMaterial->GetPropertyByName(TEXT("TexTransparencyIsActive"));
	const TSharedPtr< IDatasmithKeyValueProperty > TypeProperty = InDatasmithMaterial->GetPropertyByName(TEXT("Type"));

	FString TypeValue;
	if (TypeProperty.IsValid())
	{
		GetString(TypeProperty, TypeValue);
	}
	bool bIsGlassMaterial = TypeValue == TEXT("UGlassMaterial");

	float Opacity = 1.0f;
	bool bTransparencyActive = false;

	// If we have a non UGlassMaterial that has translucency or a translucency texture, we enable blend mode override to
	// make it render in translucent mode
	if (!bIsGlassMaterial &&
		((OpacityProperty.IsValid() && GetFloat(OpacityProperty, Opacity) && Opacity < 1.0f) ||
		(TransparencyTextureProperty.IsValid() && GetBool(TransparencyTextureProperty, bTransparencyActive) && bTransparencyActive)))
	{
		// Commented out due to this strange bug where if we enable these overrides, having either a transparency texture or a
		// bump texture assigned to the material (even if disabled) will cause it to crash. It was either disabling this functionality
		// or completely discarding the imported information on those textures. At least like this the user can manually switch the blend mode
		// override later with no consequence
		//MaterialInstance->BasePropertyOverrides.bOverride_BlendMode = true;
		//MaterialInstance->BasePropertyOverrides.BlendMode = EBlendMode::BLEND_Translucent;
	}
}

bool FDatasmithVREDImporterMaterialSelector::IsValidMaterialType( EDatasmithMasterMaterialType InType ) const
{
	return InType == EDatasmithMasterMaterialType::Auto || InType == EDatasmithMasterMaterialType::Opaque || InType == EDatasmithMasterMaterialType::Transparent;
}
