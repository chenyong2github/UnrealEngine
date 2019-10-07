// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"

class IDatasmithBaseMaterialElement;
class IDatasmithMaterialElement;
class IDatasmithMasterMaterialElement;
struct FDatasmithImportContext;
class UMaterialInterface;
class UMaterialFunction;
class UTexture;

namespace EMaterialRequirements
{
	enum Type
	{
		RequiresNothing = 0x00,
		RequiresNormals = 0x01,
		RequiresTangents = 0x02,
		RequiresAdjacency = 0x04,
	};
};

class FDatasmithMaterialImporter
{
public:
	static UMaterialFunction* CreateMaterialFunction( FDatasmithImportContext& ImportContext, const TSharedRef< IDatasmithBaseMaterialElement >& MaterialElement );

	static UMaterialInterface* CreateMaterial( FDatasmithImportContext& ImportContext, const TSharedRef< IDatasmithBaseMaterialElement >& BaseMaterialElement, UMaterialInterface* ExistingMaterial );

	static int32 GetMaterialRequirements(UMaterialInterface* MaterialInterface);

private:
	static UMaterialInterface* ImportMasterMaterial( FDatasmithImportContext& ImportContext, const TSharedRef< IDatasmithMasterMaterialElement >& MaterialElement, UMaterialInterface* ExistingMaterial );
};