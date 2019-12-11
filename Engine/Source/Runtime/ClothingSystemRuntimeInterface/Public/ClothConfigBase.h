// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "ClothConfigBase.generated.h"

class UClothingAssetBase;

/**
 * Base class for simulator specific simulation controls.
 * Each cloth instance on a skeletal mesh can have a unique cloth config
 */
UCLASS(Abstract, DefaultToInstanced)
class CLOTHINGSYSTEMRUNTIMEINTERFACE_API UClothConfigBase : public UObject
{
	GENERATED_BODY()
public:
	UClothConfigBase();
	virtual ~UClothConfigBase();

	virtual bool HasSelfCollision() const
	{
		unimplemented(); return false;
	}
};


/*
These settings are shared between all instances on a skeletal mesh
*/
UCLASS(Abstract)
class CLOTHINGSYSTEMRUNTIMEINTERFACE_API UClothSharedSimConfigBase : public UObject
{
	GENERATED_BODY()
public:
	UClothSharedSimConfigBase() {};
	virtual ~UClothSharedSimConfigBase() {};
	
};
