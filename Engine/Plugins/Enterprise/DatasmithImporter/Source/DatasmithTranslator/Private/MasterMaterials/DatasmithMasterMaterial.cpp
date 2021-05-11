// Copyright Epic Games, Inc. All Rights Reserved.

#include "MasterMaterials/DatasmithMasterMaterial.h"

#include "Materials/Material.h"

#include "UObject/SoftObjectPath.h"

FDatasmithMasterMaterial::FDatasmithMasterMaterial()
	: Material( nullptr )
{
}

void FDatasmithMasterMaterial::FromMaterial( UMaterialInterface* InMaterial )
{
	if ( InMaterial )
	{
		TArray< FGuid > ParameterIds;

		TArray< FMaterialParameterInfo > VectorParameterInfo;
		InMaterial->GetAllVectorParameterInfo(VectorParameterInfo, ParameterIds);
		for ( const FMaterialParameterInfo& Info : VectorParameterInfo )
		{
			VectorParams.Add( Info.Name.ToString() );
		}

		TArray< FMaterialParameterInfo > ScalarParameterInfo;
		InMaterial->GetAllScalarParameterInfo(ScalarParameterInfo, ParameterIds);
		for (const FMaterialParameterInfo& Info : ScalarParameterInfo)
		{
			ScalarParams.Add(Info.Name.ToString());
		}

		TArray< FMaterialParameterInfo > TextureParameterInfo;
		InMaterial->GetAllTextureParameterInfo(TextureParameterInfo, ParameterIds);
		for (const FMaterialParameterInfo& Info : TextureParameterInfo)
		{
			TextureParams.Add(Info.Name.ToString());
		}

#if WITH_EDITORONLY_DATA
		TArray< FMaterialParameterInfo > BoolParameterInfo;
		InMaterial->GetAllStaticSwitchParameterInfo(BoolParameterInfo, ParameterIds);
		for (const FMaterialParameterInfo& Info : BoolParameterInfo)
		{
			BoolParams.Add(Info.Name.ToString());
		}
#endif
	}

	Material = InMaterial;
}

void FDatasmithMasterMaterial::FromSoftObjectPath( const FSoftObjectPath& InObjectPath)
{
	FromMaterial( Cast< UMaterialInterface >(InObjectPath.TryLoad() ) );
}
