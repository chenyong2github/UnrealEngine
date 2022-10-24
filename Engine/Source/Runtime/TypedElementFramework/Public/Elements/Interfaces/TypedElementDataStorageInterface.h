// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Delegates/Delegate.h"
#include "Math/NumericLimits.h"
#include "UObject/Interface.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementDataStorageInterface.generated.h"

class UClass;
class UScriptStruct;

using TypedElementTableHandle = uint64;
static constexpr auto TypedElementInvalidTableHandle = TNumericLimits<TypedElementTableHandle>::Max();
using TypedElementRowHandle = uint64;
static constexpr auto TypedElementInvalidRowHandle = TNumericLimits<TypedElementRowHandle>::Max();

using FTypedElementOnDataStorageCreation = FSimpleMulticastDelegate;
using FTypedElementOnDataStorageDestruction = FSimpleMulticastDelegate;
using FTypedElementOnDataStorageUpdate = FSimpleMulticastDelegate;

using TypedElementDataStorageCreationCallbackRef = TFunctionRef<void(TypedElementRowHandle Row)>;

UINTERFACE(MinimalAPI)
class UTypedElementDataStorageInterface : public UInterface
{
	GENERATED_BODY()
};

class TYPEDELEMENTFRAMEWORK_API ITypedElementDataStorageInterface
{
	GENERATED_BODY()

public:
	/**
	 * Delegate that will be called when the data storage has been created. For various reasons the data storage
	 * can be destroyed and recreated during the lifetime of the editor. This delegate allows for the opportunity
	 * to register for this event so information such as table definitions can be registered again.
	 * Note that by the time this delegate is available there will already be a data storage created. It's therefore
	 * important that any information is registered with the data storage before registering a callback with the
	 * delegate.
	 */
	virtual FTypedElementOnDataStorageCreation& OnCreation() = 0;
	/**
	 * Delegate that will be called when the data storage is about to be destroyed. This can be use as an opportunity
	 * to store any data in the data storage before it becomes unavailable.
	 */
	virtual FTypedElementOnDataStorageDestruction& OnDestruction() = 0;
	/**
	 * Called periodically when the storage is available. This provides an opportunity to do any repeated processing
	 * for the data storage.
	 */
	virtual FTypedElementOnDataStorageUpdate& OnUpdate() = 0;

	/**
	 * Whether or not the data storage is available. The data storage is available most of the time, but can be
	 * unavailable for a brief time between being destroyed and a new one created.
	 */
	virtual bool IsAvailable() const = 0;

	/**
	 * Tables are automatically created by taking an existing table and adding/removing columns. For
	 * performance its however better to create a tables before hand and add objects to the table. This
	 * doesn't prevent those objects from having columns added/removed at a later time.
	 * To make debugging and profiling easier it's also recommended to add a name.
	 */
	virtual TypedElementTableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList) = 0;
	virtual TypedElementTableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList, const FName Name) = 0;

	/**
	 * Adds a row to a table.
	 */
	virtual TypedElementRowHandle AddRow(TypedElementTableHandle Table) = 0;
	virtual TypedElementRowHandle AddRow(FName TableName) = 0;
	/**
	 * Add multiple rows at once. For each new row the OnCreated callback is called. Callers are expected to use the callback to
	 * initialize the row if needed.
	 */
	virtual bool BatchAddRow(TypedElementTableHandle Table, int32 Count, TypedElementDataStorageCreationCallbackRef OnCreated) = 0;
	virtual bool BatchAddRow(FName TableName, int32 Count, TypedElementDataStorageCreationCallbackRef OnCreated) = 0;
	
	/**
	 * Removes a previously added row.
	 */
	virtual void RemoveRow(TypedElementRowHandle Row) = 0;

	/**
	 * Retrieves a pointer to the column of the given row or a nullptr if not found.
	 */
	virtual void* GetColumnData(TypedElementRowHandle Row, const UScriptStruct* ColumnType) = 0;

	/**
	 * Returns a pointer to the registered external system if found, otherwise null.
	 */
	virtual void* GetExternalSystemAddress(UClass* Target) = 0;

	
	/**
	 *
	 * The following are utility functions that are not part of the interface but are provided in order to make using the
	 * interface easier.
	 *
	 */
	
	
	/**
	 * Returns a pointer to the column of the given row or a nullptr if the type couldn't be found or the row doesn't exist.
	 */
	template<typename ColumnType>
	ColumnType* GetColumn(TypedElementRowHandle Row);

	/**
	 * Returns a pointer to the registered external system if found, otherwise null.
	 */
	template<typename SystemType>
	SystemType* GetExternalSystem();
};

// Implementations
template<typename ColumnType>
ColumnType* ITypedElementDataStorageInterface::GetColumn(TypedElementRowHandle Row)
{
	return reinterpret_cast<ColumnType*>(GetColumnData(Row, ColumnType::StaticStruct()));
}

template<typename SystemType>
SystemType* ITypedElementDataStorageInterface::GetExternalSystem()
{
	return reinterpret_cast<SystemType*>(GetExternalSystemAddress(SystemType::StaticClass()));
}