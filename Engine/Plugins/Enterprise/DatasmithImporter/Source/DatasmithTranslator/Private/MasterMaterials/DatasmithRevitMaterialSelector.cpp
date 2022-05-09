// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRevitMaterialSelector.h"

#include "IDatasmithSceneElements.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/Casts.h"

FDatasmithRevitMaterialSelector::FDatasmithRevitMaterialSelector()
{
	// Master
	MasterMaterial.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/RevitMaster.RevitMaster") );
	MasterMaterialDecal.FromSoftObjectPath(FSoftObjectPath("/DatasmithContent/Materials/StdDecal/M_StdDecal.M_StdDecal"));
}

bool FDatasmithRevitMaterialSelector::IsValid() const
{
	return MasterMaterial.IsValid();
}

const FDatasmithMasterMaterial& FDatasmithRevitMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial ) const
{
	if (InDatasmithMaterial->GetMaterialType() == EDatasmithMasterMaterialType::Decal)
	{
		return MasterMaterialDecal;
	}

	return MasterMaterial;
}

void FDatasmithRevitMaterialSelector::FinalizeMaterialInstance(const TSharedPtr<IDatasmithMasterMaterialElement>& InDatasmithMaterial, UMaterialInstanceConstant * MaterialInstance) const
{
	if (!InDatasmithMaterial.IsValid() || MaterialInstance == nullptr)
	{
		return;
	}

	// Set blend mode to translucent if material requires transparency
	if (InDatasmithMaterial->GetMaterialType() == EDatasmithMasterMaterialType::Transparent)
	{
		MaterialInstance->BasePropertyOverrides.bOverride_BlendMode = true;
		MaterialInstance->BasePropertyOverrides.BlendMode = BLEND_Translucent;
	}
	// Set blend mode to masked if material has cutouts
	else if (InDatasmithMaterial->GetMaterialType() == EDatasmithMasterMaterialType::CutOut)
	{
		MaterialInstance->BasePropertyOverrides.bOverride_BlendMode = true;
		MaterialInstance->BasePropertyOverrides.BlendMode = BLEND_Masked;
	}
}
