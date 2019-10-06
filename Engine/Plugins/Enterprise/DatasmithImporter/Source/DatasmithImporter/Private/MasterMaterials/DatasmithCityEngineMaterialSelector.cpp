// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithCityEngineMaterialSelector.h"

#include "IDatasmithSceneElements.h"

#include "Materials/Material.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/Casts.h"

FDatasmithCityEngineMaterialSelector::FDatasmithCityEngineMaterialSelector()
{
	// Master
	MasterMaterial.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/CE_OpaqueMaster.CE_OpaqueMaster") );
	MasterMaterialTransparent.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/CE_OpacityMaster.CE_OpacityMaster") );
	MasterMaterialTransparentSimple.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/CE_OpacitySimpleMaster.CE_OpacitySimpleMaster") );
}

bool FDatasmithCityEngineMaterialSelector::IsValid() const
{
	return MasterMaterial.IsValid() && MasterMaterialTransparent.IsValid() && MasterMaterialTransparentSimple.IsValid();
}

const FDatasmithMasterMaterial& FDatasmithCityEngineMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial ) const
{
	EDatasmithMasterMaterialType MaterialType = InDatasmithMaterial->GetMaterialType();

	if ( !IsValidMaterialType( MaterialType ) )
	{
		MaterialType = EDatasmithMasterMaterialType::Auto;
	}

	bool bIsTransparent = MaterialType == EDatasmithMasterMaterialType::Transparent;

	if ( MaterialType == EDatasmithMasterMaterialType::Auto )
	{
		const TSharedPtr< IDatasmithKeyValueProperty > OpacityProperty = InDatasmithMaterial->GetPropertyByName(TEXT("Opacity"));
		const TSharedPtr< IDatasmithKeyValueProperty > OpacityMapProperty = InDatasmithMaterial->GetPropertyByName(TEXT("OpacityMap"));

		float OpacityValue;
		bIsTransparent = ( OpacityProperty.IsValid() && GetFloat( OpacityProperty, OpacityValue ) && OpacityValue < 1.f );

		FString OpacityMap;
		bIsTransparent = bIsTransparent || ( OpacityMapProperty.IsValid() && GetTexture( OpacityMapProperty, OpacityMap ) && !OpacityMap.IsEmpty() );
	}

	if ( bIsTransparent )
	{
		if ( InDatasmithMaterial->GetQuality() == EDatasmithMasterMaterialQuality::Low )
		{
			return MasterMaterialTransparentSimple;
		}
		else
		{
			return MasterMaterialTransparent;
		}
	}

	return MasterMaterial;
}

bool FDatasmithCityEngineMaterialSelector::IsValidMaterialType( EDatasmithMasterMaterialType InType ) const
{
	return InType == EDatasmithMasterMaterialType::Auto || InType == EDatasmithMasterMaterialType::Opaque || InType == EDatasmithMasterMaterialType::Transparent;
}
