// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementSubsystems.generated.h"

class ITypedElementDataStorageInterface;
class ITypedElementDataStorageUiInterface;
class ITypedElementDataStorageCompatibilityInterface;

/**
 * A subsystem to provide alternative access to the Typed Elements Data Storage. This can be used in situations where directly accessing
 * the Data Storage from the Typed Elements Registry is not recommended, such as for MASS.
 */
UCLASS()
class TYPEDELEMENTSDATASTORAGE_API UTypedElementDataStorageSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	~UTypedElementDataStorageSubsystem() override;

	ITypedElementDataStorageInterface* Get();
	const ITypedElementDataStorageInterface* Get() const;

protected:
	bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	mutable ITypedElementDataStorageInterface* DataStorage{ nullptr };
};

/**
 * A subsystem to provide alternative access to the Typed Elements Data Storage UI. This can be used in situations where directly 
 * accessing the UI from the Typed Elements Registry is not recommended, such as for MASS.
 */
UCLASS()
class TYPEDELEMENTSDATASTORAGE_API UTypedElementDataStorageUiSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	~UTypedElementDataStorageUiSubsystem() override;

	ITypedElementDataStorageUiInterface* Get();
	const ITypedElementDataStorageUiInterface* Get() const;

protected:
	bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	mutable ITypedElementDataStorageUiInterface* DataStorageUi{ nullptr };
};

/**
 * A subsystem to provide alternative access to the Typed Elements Data Storage Compatiblity. This can be used in situations where directly 
 * accessing the Compatiblity extension from the Typed Elements Registry is not recommended, such as for MASS.
 */
UCLASS()
class TYPEDELEMENTSDATASTORAGE_API UTypedElementDataStorageCompatibilitySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	~UTypedElementDataStorageCompatibilitySubsystem() override;

	ITypedElementDataStorageCompatibilityInterface* Get();
	const ITypedElementDataStorageCompatibilityInterface* Get() const;

protected:
	bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	mutable ITypedElementDataStorageCompatibilityInterface* DataStorageCompatibility{ nullptr };
};