// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DatasmithDefinitions.h"
#include "MasterMaterials/DatasmithMasterMaterial.h"
#include "MasterMaterials/DatasmithMasterMaterialSelector.h"

#include "Templates/SharedPointer.h"

class IDatasmithMasterMaterialElement;

class FDatasmithDeltaGenImporterMaterialSelector : public FDatasmithMasterMaterialSelector
{
public:
	FDatasmithDeltaGenImporterMaterialSelector();

	virtual bool IsValid() const override;
	virtual const FDatasmithMasterMaterial& GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial ) const override;

protected:
	bool IsValidMaterialType( EDatasmithMasterMaterialType InType ) const;

private:
	FDatasmithMasterMaterial MasterMaterial;
	FDatasmithMasterMaterial MasterMaterialTransparent;
};
