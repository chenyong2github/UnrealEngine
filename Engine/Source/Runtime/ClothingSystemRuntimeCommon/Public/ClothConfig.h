// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothConfigBase.h"
#include "ClothConfig.generated.h"

struct FClothConfig_Legacy;

/** Different mass modes deciding the setup process. */
UENUM()
enum class EClothMassMode : uint8
{
	/** The mass value is used to set the same mass for each particle. */
	UniformMass,
	/** The mass value is used to set the mass of the entire cloth, distributing it to each particle depending on the amount of connected surface area. */
	TotalMass,
	/** The mass value is used to set the density of the cloth, calculating the mass for each particle depending on its connected surface area. */
	Density,
	MaxClothMassMode UMETA(Hidden)
};

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

	/** Migrate from shared configs. */
	virtual void MigrateFrom(const class UClothSharedConfigCommon*) {}

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
