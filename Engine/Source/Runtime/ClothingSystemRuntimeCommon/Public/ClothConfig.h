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
	virtual ~UClothConfigCommon();

	/** Migrate from the legacy FClothConfig structure. */
	virtual void MigrateFrom(const FClothConfig_Legacy&) {}
};
