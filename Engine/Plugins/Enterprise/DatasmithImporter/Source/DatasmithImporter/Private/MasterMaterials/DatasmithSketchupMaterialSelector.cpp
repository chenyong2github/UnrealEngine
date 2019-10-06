// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchupMaterialSelector.h"

#include "IDatasmithSceneElements.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/Casts.h"

FDatasmithSketchUpMaterialSelector::FDatasmithSketchUpMaterialSelector()
{
	// Master
	MasterMaterial.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/SketchupMaster.SketchupMaster") );
}

bool FDatasmithSketchUpMaterialSelector::IsValid() const
{
	return MasterMaterial.IsValid();
}

const FDatasmithMasterMaterial& FDatasmithSketchUpMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& /*InDatasmithMaterial*/ ) const
{
	return MasterMaterial;
}

void FDatasmithSketchUpMaterialSelector::FinalizeMaterialInstance(const TSharedPtr<IDatasmithMasterMaterialElement>& InDatasmithMaterial, UMaterialInstanceConstant * MaterialInstance) const
{
	if (!InDatasmithMaterial.IsValid() || MaterialInstance == nullptr)
	{
		return;
	}

	if (InDatasmithMaterial->GetMaterialType() == EDatasmithMasterMaterialType::Transparent)
	{
		MaterialInstance->BasePropertyOverrides.bOverride_BlendMode = true;
		MaterialInstance->BasePropertyOverrides.BlendMode = BLEND_Translucent;
	}
}
