// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothConfigBase.h"
#include "ClothConfig.generated.h"

struct FClothConfig_Legacy;

/** Common configuration base class. */
UCLASS(Abstract)
class CLOTHINGSYSTEMRUNTIMECOMMON_API UClothConfigCommon : public UClothConfigBase
{
	GENERATED_BODY()
public:
	UClothConfigCommon();
	virtual ~UClothConfigCommon() override;

	/** Migrate from the legacy FClothConfig structure. */
	virtual void MigrateFrom(const FClothConfig_Legacy&) {}

	/**
	 * Migrate to the legacy FClothConfig structure.
	 * Useful for converting configs that are compatible with this legacy structure.
	 * @return true when the migration is possible, false otherwise.
	 */
	virtual bool MigrateTo(FClothConfig_Legacy&) const { return false; }
};

/** Common shared configuration base class. */
UCLASS(Abstract)
class CLOTHINGSYSTEMRUNTIMECOMMON_API UClothSharedConfigCommon : public UClothConfigCommon
{
	GENERATED_BODY()
public:
	UClothSharedConfigCommon();
	virtual ~UClothSharedConfigCommon() override;
};
