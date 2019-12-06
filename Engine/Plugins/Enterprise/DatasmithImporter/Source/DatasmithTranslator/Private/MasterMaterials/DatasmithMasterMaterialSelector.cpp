// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MasterMaterials/DatasmithMasterMaterialSelector.h"

#include "MasterMaterials/DatasmithMasterMaterial.h"
#include "IDatasmithSceneElements.h"

FDatasmithMasterMaterial FDatasmithMasterMaterialSelector::InvalidMasterMaterial;

const FDatasmithMasterMaterial& FDatasmithMasterMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial ) const
{
	return InvalidMasterMaterial;
}

bool FDatasmithMasterMaterialSelector::GetColor( const TSharedPtr< IDatasmithKeyValueProperty >& InMaterialProperty, FLinearColor& OutValue ) const
{
	if (!InMaterialProperty.IsValid() || InMaterialProperty->GetPropertyType() != EDatasmithKeyValuePropertyType::Color )
	{
		return false;
	}

	return OutValue.InitFromString( InMaterialProperty->GetValue() ); // TODO: Handle sRGB vs linear RGB properly?
}

bool FDatasmithMasterMaterialSelector::GetFloat( const TSharedPtr< IDatasmithKeyValueProperty >& InMaterialProperty, float& OutValue ) const
{
	if (!InMaterialProperty.IsValid() || InMaterialProperty->GetPropertyType() != EDatasmithKeyValuePropertyType::Float )
	{
		return false;
	}

	OutValue = FCString::Atof( InMaterialProperty->GetValue() );

	return true;
}

bool FDatasmithMasterMaterialSelector::GetBool( const TSharedPtr< IDatasmithKeyValueProperty >& InMaterialProperty, bool& OutValue ) const
{
	if (!InMaterialProperty.IsValid() || InMaterialProperty->GetPropertyType() != EDatasmithKeyValuePropertyType::Bool )
	{
		return false;
	}

	OutValue = FString( InMaterialProperty->GetValue() ).ToBool();

	return true;
}

bool FDatasmithMasterMaterialSelector::GetTexture( const TSharedPtr< IDatasmithKeyValueProperty >& InMaterialProperty, FString& OutValue ) const
{
	if (!InMaterialProperty.IsValid() || InMaterialProperty->GetPropertyType() != EDatasmithKeyValuePropertyType::Texture )
	{
		return false;
	}

	OutValue = InMaterialProperty->GetValue();

	return true;
}

bool FDatasmithMasterMaterialSelector::GetString(const TSharedPtr< IDatasmithKeyValueProperty >& InMaterialProperty, FString& OutValue) const
{
	if (!InMaterialProperty.IsValid() || InMaterialProperty->GetPropertyType() != EDatasmithKeyValuePropertyType::String )
	{
		return false;
	}

	OutValue = InMaterialProperty->GetValue();

	return true;
}
