// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementDataStorageFactory.generated.h"

class ITypedElementDataStorageInterface;
class ITypedElementDataStorageUiInterface;

/**
 * Base class that can be used to register various elements, such as queries and widgets, with
 * the Typed Elements Data Storage.
 */
UCLASS()
class TYPEDELEMENTFRAMEWORK_API UTypedElementDataStorageFactory : public UObject
{
	GENERATED_BODY()

public:
	~UTypedElementDataStorageFactory() override = default;

	virtual void RegisterQueries(ITypedElementDataStorageInterface& DataStorage) const {}
	virtual void RegisterWidgetConstructor(ITypedElementDataStorageInterface& DataStorage,
		ITypedElementDataStorageUiInterface& DataStorageUi) const {}
};