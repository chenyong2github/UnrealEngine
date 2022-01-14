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
	MasterMaterialEmissive.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/StdEmissive/M_StdEmissive.M_StdEmissive") );
}

bool FDatasmithStdMaterialSelector::IsValid() const
{
	return MasterMaterialOpaque.IsValid() && MasterMaterialTranslucent.IsValid() && MasterMaterialEmissive.IsValid();
}

const FDatasmithMasterMaterial& FDatasmithStdMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial ) const
{
	switch (InDatasmithMaterial->GetMaterialType())
	{
		case EDatasmithMasterMaterialType::Transparent:
			return MasterMaterialTranslucent;
			break;
		case EDatasmithMasterMaterialType::Emissive:
			return MasterMaterialEmissive;
			break;
		default:
			return MasterMaterialOpaque;
			break;
	}
}
