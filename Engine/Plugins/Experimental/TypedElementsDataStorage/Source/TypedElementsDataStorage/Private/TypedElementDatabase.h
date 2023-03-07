// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "MassArchetypeTypes.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "TypedElementHandleStore.h"
#include "UObject/ObjectMacros.h"
#include "UObject/StrongObjectPtr.h"

#include "TypedElementDatabase.generated.h"

struct FMassEntityManager;
class UWorld;

struct FTypedElementDatabaseExtendedQuery
{
	FMassEntityQuery NativeQuery;
	ITypedElementDataStorageInterface::FQueryDescription::EActionType Action{ ITypedElementDataStorageInterface::FQueryDescription::EActionType::None };
	ITypedElementDataStorageInterface::FQueryDescription::FCallbackData Callback;
	TStrongObjectPtr<UMassProcessor> Processor;
	bool bSimpleQuery{ false };
};

UCLASS()
class TYPEDELEMENTSDATASTORAGE_API UTypedElementDatabase 
	: public UObject
	, public ITypedElementDataStorageInterface
{
	GENERATED_BODY()

public:
	~UTypedElementDatabase() override = default;
	
	void Initialize();
	void Deinitialize();

	/** Triggered just before underlying Mass processing gets ticked */
	void OnPreMassTick(float DeltaTime);

	TSharedPtr<FMassEntityManager> GetActiveMutableEditorEntityManager();
	TSharedPtr<const FMassEntityManager> GetActiveEditorEntityManager() const;

	TypedElementTableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList) override;
	TypedElementTableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList, const FName Name) override;
	TypedElementTableHandle RegisterTable(TypedElementTableHandle SourceTable, TConstArrayView<const UScriptStruct*> ColumnList) override;
	TypedElementTableHandle RegisterTable(TypedElementTableHandle SourceTable, TConstArrayView<const UScriptStruct*> ColumnList, 
		const FName Name) override;
	TypedElementTableHandle FindTable(const FName Name) override;

	TypedElementRowHandle AddRow(TypedElementTableHandle Table) override;
	TypedElementRowHandle AddRow(FName TableName) override;
	bool BatchAddRow(TypedElementTableHandle Table, int32 Count, TypedElementDataStorageCreationCallbackRef OnCreated) override;
	bool BatchAddRow(FName TableName, int32 Count, TypedElementDataStorageCreationCallbackRef OnCreated) override;
	void RemoveRow(TypedElementRowHandle Row) override;

	void AddTag(TypedElementRowHandle Row, const UScriptStruct* TagType) override;
	void AddTag(TypedElementRowHandle Row, FTopLevelAssetPath TagName) override;
	void* AddOrGetColumnData(TypedElementRowHandle Row, const UScriptStruct* ColumnType) override;
	ColumnDataResult AddOrGetColumnData(TypedElementRowHandle Row, FTopLevelAssetPath ColumnName) override;
	void* GetColumnData(TypedElementRowHandle Row, const UScriptStruct* ColumnType) override;
	ColumnDataResult AddOrGetColumnData(TypedElementRowHandle Row, FTopLevelAssetPath ColumnName,
		TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments) override;
	ColumnDataResult GetColumnData(TypedElementRowHandle Row, FTopLevelAssetPath ColumnName) override;

	TypedElementQueryHandle RegisterQuery(FQueryDescription&& Query) override;
	void UnregisterQuery(TypedElementQueryHandle Query) override;
	FName GetQueryTickGroupName(EQueryTickGroups Group) const override;
	FQueryResult RunQuery(TypedElementQueryHandle Query) override;

	FTypedElementOnDataStorageUpdate& OnUpdate() override;
	bool IsAvailable() const override;
	void* GetExternalSystemAddress(UClass* Target) override;

private:
	using QueryStore = TTypedElementHandleStore<FTypedElementDatabaseExtendedQuery>;

	void Reset();
	
	TArray<FMassArchetypeHandle> Tables;
	TMap<FName, TypedElementTableHandle> TableNameLookup;

	QueryStore Queries;

	FTypedElementOnDataStorageUpdate OnUpdateDelegate;

	TSharedPtr<FMassEntityManager> ActiveEditorEntityManager;
};