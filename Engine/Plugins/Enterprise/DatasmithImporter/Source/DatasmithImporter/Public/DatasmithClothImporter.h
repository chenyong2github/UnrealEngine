// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once


#include "CoreTypes.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"

class FDatasmithCloth;
class FDatasmithClothPresetPropertySet;
class IDatasmithClothElement;


// #ue_ds_cloth_arch: Temp API
class IDatasmithImporterExt
{
public:
	virtual UObject* MakeClothAsset(UObject* Outer, const TCHAR* Name, EObjectFlags ObjectFlags) = 0;
	virtual void FillCloth(UObject* ClothAsset, TSharedRef<IDatasmithClothElement> ClothElement, FDatasmithCloth& Cloth) = 0;

	virtual UObject* MakeClothPropertyAsset(UObject* Outer, const TCHAR* Name, EObjectFlags ObjectFlags) = 0;
	virtual void FillPropertySet(UObject* PropertySetAsset, TSharedRef<IDatasmithClothElement> ClothElement, const FDatasmithClothPresetPropertySet& PropertySet) = 0;
};

