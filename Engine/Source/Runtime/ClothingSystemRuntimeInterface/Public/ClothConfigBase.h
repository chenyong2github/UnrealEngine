// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "ClothConfigBase.generated.h"

/**
 * Base class for simulator specific simulation controls.
 * Each cloth instance on a skeletal mesh can have a unique cloth config
 */
UCLASS(Abstract)
class CLOTHINGSYSTEMRUNTIMEINTERFACE_API UClothConfigBase : public UObject
{
	GENERATED_BODY()
public:
	UClothConfigBase();
	virtual ~UClothConfigBase();

	/**
	 * Return whether self collision is enabled for this config.
	 */
	UE_DEPRECATED(4.25, "This function is deprecated. Please use NeedsSelfCollisionIndices instead.")
	virtual bool HasSelfCollision() const
	{ unimplemented(); return false; }

	/**
	 * Return the self collision radius if building self collision indices is required for this config.
	 * Otherwise return 0.f.
	 */
	virtual float NeedsSelfCollisionIndices() const
	{ return 0.f; }
};

/**
 * These settings are shared between all instances on a skeletal mesh
 * Deprecated, use UClothConfigBase instead.
 */
UCLASS(Abstract, Deprecated)
class CLOTHINGSYSTEMRUNTIMEINTERFACE_API UDEPRECATED_ClothSharedSimConfigBase : public UObject
{
	GENERATED_BODY()
public:
	UDEPRECATED_ClothSharedSimConfigBase() {}
	virtual ~UDEPRECATED_ClothSharedSimConfigBase() {}

	/**
	 * Return a new updated cloth shared config migrated from this current object.
	 */
	virtual UClothConfigBase* Migrate() { return nullptr; }
};
