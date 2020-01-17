// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Math/Color.h"
#include "Templates/SharedPointer.h"

class FDatasmithMasterMaterial;
class IDatasmithMasterMaterialElement;
class IDatasmithKeyValueProperty;
class UMaterialInstanceConstant;

class DATASMITHTRANSLATOR_API FDatasmithMasterMaterialSelector
{
public:
	virtual ~FDatasmithMasterMaterialSelector() = default;

	virtual bool IsValid() const { return false; }
	virtual const FDatasmithMasterMaterial& GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial ) const;
	virtual void FinalizeMaterialInstance(const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial, UMaterialInstanceConstant* MaterialInstance) const {}

	virtual bool GetColor( const TSharedPtr< IDatasmithKeyValueProperty >& InMaterialProperty, FLinearColor& OutValue ) const;
	virtual bool GetFloat( const TSharedPtr< IDatasmithKeyValueProperty >& InMaterialProperty, float& OutValue ) const;
	virtual bool GetBool( const TSharedPtr< IDatasmithKeyValueProperty >& InMaterialProperty, bool& OutValue ) const;
	virtual bool GetTexture( const TSharedPtr< IDatasmithKeyValueProperty >& InMaterialProperty, FString& OutValue ) const;
	virtual bool GetString( const TSharedPtr< IDatasmithKeyValueProperty >& InMaterialProperty, FString& OutValue ) const;

protected:
	static FDatasmithMasterMaterial InvalidMasterMaterial;

};

