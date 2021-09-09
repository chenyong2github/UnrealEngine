// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithStdMaterialSelector.h"

#include "IDatasmithSceneElements.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/Casts.h"

FDatasmithStdMaterialSelector::FDatasmithStdMaterialSelector()
{
	// Master
	MasterMaterialOpaque.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/StdOpaque/M_StdOpaque.M_StdOpaque") );
	MasterMaterialTranslucent.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/StdTranslucent/M_StdTranslucent.M_StdTranslucent") );
}

bool FDatasmithStdMaterialSelector::IsValid() const
{
	return MasterMaterialOpaque.IsValid() && MasterMaterialTranslucent.IsValid();
}

const FDatasmithMasterMaterial& FDatasmithStdMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial ) const
{
	if (InDatasmithMaterial->GetMaterialType() == EDatasmithMasterMaterialType::Transparent)
	{
		return MasterMaterialTranslucent;
	}
	return MasterMaterialOpaque;
}
