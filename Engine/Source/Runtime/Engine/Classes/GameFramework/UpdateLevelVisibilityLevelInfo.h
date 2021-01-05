// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UpdateLevelVisibilityLevelInfo.generated.h"

/** This structure is used to pass arguments to ServerUpdateLevelVisibilty() and ServerUpdateMultipleLevelsVisibility() server RPC functions */
USTRUCT()
struct ENGINE_API FUpdateLevelVisibilityLevelInfo
{
	GENERATED_BODY();

	FUpdateLevelVisibilityLevelInfo()
		: PackageName(NAME_None)
		, FileName(NAME_None)
		, bIsVisible(false)
		, bSkipCloseOnError(false)
	{
	}

	/**
	 * @param Level			Level to pull PackageName and FileName from.
	 * @param bInIsVisible	Default value for bIsVisible.
	 */
	FUpdateLevelVisibilityLevelInfo(const class ULevel* const Level, const bool bInIsVisible);

	/** The name of the package for the level whose status changed. */
	UPROPERTY()
	FName PackageName;

	/** The name / path of the asset file for the level whose status changed. */
	UPROPERTY()
	FName FileName;

	/** The new visibility state for this level. */
	UPROPERTY()
	uint32 bIsVisible : 1;

	/** Skip connection close if level can't be found (not net serialized) */
	uint32 bSkipCloseOnError : 1;

	bool NetSerialize(FArchive& Ar, UPackageMap* PackageMap, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FUpdateLevelVisibilityLevelInfo> : public TStructOpsTypeTraitsBase2<FUpdateLevelVisibilityLevelInfo>
{
	enum
	{
		WithNetSerializer = true
	};
};
