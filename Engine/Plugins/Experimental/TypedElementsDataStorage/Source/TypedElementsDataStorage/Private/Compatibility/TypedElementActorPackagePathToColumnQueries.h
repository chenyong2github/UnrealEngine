// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementActorPackagePathToColumnQueries.generated.h"

UCLASS()
class UTypedElementActorPackagePathFactory : public UTypedElementDataStorageFactory
{
	GENERATED_BODY()

public:
	~UTypedElementActorPackagePathFactory() override = default;

	void RegisterQueries(ITypedElementDataStorageInterface& DataStorage) const override;
};
