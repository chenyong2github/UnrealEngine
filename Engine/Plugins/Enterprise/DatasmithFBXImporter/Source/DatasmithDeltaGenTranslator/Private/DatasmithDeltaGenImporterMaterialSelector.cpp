// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithDeltaGenImporterMaterialSelector.h"
#include "IDatasmithSceneElements.h"

#include "Materials/Material.h"
#include "Templates/Casts.h"
#include "UObject/SoftObjectPath.h"

FDatasmithDeltaGenImporterMaterialSelector::FDatasmithDeltaGenImporterMaterialSelector()
{
	// Master
	MasterMaterial.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/FBXImporter/DeltaGenMaster.DeltaGenMaster") );

	// Transparent
	MasterMaterialTransparent.FromSoftObjectPath( FSoftObjectPath("/DatasmithContent/Materials/FBXImporter/DeltaGenMasterTransparent.DeltaGenMasterTransparent") );
}

bool FDatasmithDeltaGenImporterMaterialSelector::IsValid() const
{
	return MasterMaterial.IsValid() && MasterMaterialTransparent.IsValid();
}

const FDatasmithMasterMaterial& FDatasmithDeltaGenImporterMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial ) const
{
	// Transparent material
	const TSharedPtr< IDatasmithKeyValueProperty > OpacityProperty = InDatasmithMaterial->GetPropertyByName(TEXT("Opacity"));

	float OpacityValue;

	if ( OpacityProperty.IsValid() &&
		GetFloat( OpacityProperty, OpacityValue ) &&
		OpacityValue < 1.f )
	{
		return MasterMaterialTransparent;
	}

	return MasterMaterial;
}

bool FDatasmithDeltaGenImporterMaterialSelector::IsValidMaterialType( EDatasmithMasterMaterialType InType ) const
{
	return InType == EDatasmithMasterMaterialType::Auto || InType == EDatasmithMasterMaterialType::Opaque || InType == EDatasmithMasterMaterialType::Transparent;
}
