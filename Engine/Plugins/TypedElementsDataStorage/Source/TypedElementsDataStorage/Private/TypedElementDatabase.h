// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "MassArchetypeTypes.h"

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
	~UTypedElementDatabase() override = default;

	void Initialize();
	void Deinitialize();

	TSharedPtr<FMassEntityManager> GetActiveMutableEditorEntityManager();
	TSharedPtr<const FMassEntityManager> GetActiveEditorEntityManager() const;

	FTypedElementOnDataStorageCreation& OnCreation() override;
	FTypedElementOnDataStorageDestruction& OnDestruction() override;
	FTypedElementOnDataStorageUpdate& OnUpdate() override;
	bool IsAvailable() const override;

	TypedElementTableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList) override;
	TypedElementTableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList, const FName Name) override;

	TypedElementRowHandle AddRow(TypedElementTableHandle Table) override;
	TypedElementRowHandle AddRow(FName TableName) override;
	bool BatchAddRow(TypedElementTableHandle Table, int32 Count, TypedElementDataStorageCreationCallbackRef OnCreated) override;
	bool BatchAddRow(FName TableName, int32 Count, TypedElementDataStorageCreationCallbackRef OnCreated) override;
	void RemoveRow(TypedElementRowHandle Row) override;

	void* GetColumnData(TypedElementRowHandle Row, const UScriptStruct* ColumnType) override;

	void* GetExternalSystemAddress(UClass* Target) override;

private:
	void Reset();
	
	TArray<FMassArchetypeHandle> Tables;
	TMap<FName, TypedElementTableHandle> TableNameLookup;

	FTypedElementOnDataStorageCreation OnCreationDelegate;
	FTypedElementOnDataStorageDestruction OnDestructionDelegate;
	FTypedElementOnDataStorageUpdate OnUpdateDelegate;

	FDelegateHandle TickCallHandle;

	TSharedPtr<FMassEntityManager> ActiveEditorEntityManager;
	UWorld* ActiveEditorWorld{ nullptr };
};