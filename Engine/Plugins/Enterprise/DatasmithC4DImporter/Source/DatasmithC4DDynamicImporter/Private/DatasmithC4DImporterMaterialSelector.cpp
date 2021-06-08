// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef _CINEWARE_SDK_

#include "DatasmithC4DImporterMaterialSelector.h"

#include "IDatasmithSceneElements.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/Casts.h"

FDatasmithC4DImporterMaterialSelector::FDatasmithC4DImporterMaterialSelector()
{
	MasterMaterial.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/C4DMaster.C4DMaster") );
}

bool FDatasmithC4DImporterMaterialSelector::IsValid() const
{
	return MasterMaterial.IsValid();
}

const FDatasmithMasterMaterial& FDatasmithC4DImporterMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& /*InDatasmithMaterial*/ ) const
{
	return MasterMaterial;
}

void FDatasmithC4DImporterMaterialSelector::FinalizeMaterialInstance(const TSharedPtr<IDatasmithMasterMaterialElement>& InDatasmithMaterial, UMaterialInstanceConstant * MaterialInstance) const
{
	if (!InDatasmithMaterial.IsValid() || MaterialInstance == nullptr)
	{
		return;
	}

	// Set blend mode to translucent if material requires transparency.
	if (InDatasmithMaterial->GetMaterialType() == EDatasmithMasterMaterialType::Transparent)
	{
		MaterialInstance->BasePropertyOverrides.bOverride_BlendMode = true;
		MaterialInstance->BasePropertyOverrides.BlendMode = BLEND_Translucent;
	}
	// Set blend mode to masked if material has cutouts.
	else if (InDatasmithMaterial->GetMaterialType() == EDatasmithMasterMaterialType::CutOut)
	{
		MaterialInstance->BasePropertyOverrides.bOverride_BlendMode = true;
		MaterialInstance->BasePropertyOverrides.BlendMode = BLEND_Masked;
	}
}

#endif //_CINEWARE_SDK_
