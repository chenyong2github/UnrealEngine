// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "MassArchetypeTypes.h"
#include "MassEntityQuery.h"
#include "TypedElementHandleStore.h"

#include "TypedElementDatabase.generated.h"

struct FMassEntityManager;
class UWorld;

UCLASS()
class TYPEDELEMENTSDATASTORAGE_API UTypedElementDatabase 
	: public UObject
	, public ITypedElementDataStorageInterface
{
	GENERATED_BODY()
public:
	struct FExtendedQuery
	{
		FMassEntityQuery NativeQuery;
		FQueryDescription::EActionType Action{ FQueryDescription::EActionType::None };
		bool bSimpleQuery{ false };
	};

	~UTypedElementDatabase() override = default;

	void Initialize();
	void Deinitialize();

	TSharedPtr<FMassEntityManager> GetActiveMutableEditorEntityManager();
	TSharedPtr<const FMassEntityManager> GetActiveEditorEntityManager() const;

	TypedElementTableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList) override;
	TypedElementTableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList, const FName Name) override;

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

	TypedElementQueryHandle RegisterQuery(const FQueryDescription& Query) override;
	void UnregisterQuery(TypedElementQueryHandle Query) override;
	FQueryResult RunQuery(TypedElementQueryHandle Query) override;

	FTypedElementOnDataStorageCreation& OnCreation() override;
	FTypedElementOnDataStorageDestruction& OnDestruction() override;
	FTypedElementOnDataStorageUpdate& OnUpdate() override;
	bool IsAvailable() const override;
	void* GetExternalSystemAddress(UClass* Target) override;

private:
	using QueryStore = TTypedElementHandleStore<FExtendedQuery>;

	void Reset();
	
	TArray<FMassArchetypeHandle> Tables;
	TMap<FName, TypedElementTableHandle> TableNameLookup;

	QueryStore Queries;

	FTypedElementOnDataStorageCreation OnCreationDelegate;
	FTypedElementOnDataStorageDestruction OnDestructionDelegate;
	FTypedElementOnDataStorageUpdate OnUpdateDelegate;

	FDelegateHandle TickCallHandle;

	TSharedPtr<FMassEntityManager> ActiveEditorEntityManager;
	UWorld* ActiveEditorWorld{ nullptr };
};