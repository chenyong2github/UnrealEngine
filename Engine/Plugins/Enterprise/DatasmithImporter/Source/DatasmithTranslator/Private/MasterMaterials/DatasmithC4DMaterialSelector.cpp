// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithC4DMaterialSelector.h"

#include "IDatasmithSceneElements.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/Casts.h"

FDatasmithC4DMaterialSelector::FDatasmithC4DMaterialSelector()
{
	MasterMaterial.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/C4DMaster.C4DMaster") );
}

bool FDatasmithC4DMaterialSelector::IsValid() const
{
	return MasterMaterial.IsValid();
}

const FDatasmithMasterMaterial& FDatasmithC4DMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& /*InDatasmithMaterial*/ ) const
{
	return MasterMaterial;
}

void FDatasmithC4DMaterialSelector::FinalizeMaterialInstance(const TSharedPtr<IDatasmithMasterMaterialElement>& InDatasmithMaterial, UMaterialInstanceConstant * MaterialInstance) const
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
