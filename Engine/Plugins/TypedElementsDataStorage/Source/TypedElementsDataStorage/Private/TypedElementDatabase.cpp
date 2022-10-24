// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementDatabase.h"

#include "Editor.h"
#include "Engine/World.h"
#include "MassEntitySubsystem.h"

void UTypedElementDatabase::Initialize()
{
	ActiveEditorWorld = GEditor->GetEditorWorldContext().World();
	check(ActiveEditorWorld);
	UMassEntitySubsystem* Mass = ActiveEditorWorld->GetSubsystem<UMassEntitySubsystem>();
	check(Mass);
	ActiveEditorEntityManager = Mass->GetMutableEntityManager().AsShared();
	
	TickCallHandle = FWorldDelegates::OnWorldTickStart.AddLambda([this](UWorld* World, ELevelTick, float)
		{
			if (World == ActiveEditorWorld)
			{
				OnUpdateDelegate.Broadcast();
			}
		});

	GEditor->OnWorldAdded().AddLambda([this](UWorld* NewWorld)
		{
			if (GEditor->GetEditorWorldContext().World() == NewWorld)
			{
				ActiveEditorWorld = NewWorld;
				UMassEntitySubsystem* Mass = NewWorld->GetSubsystem<UMassEntitySubsystem>();
				check(Mass);
				ActiveEditorEntityManager = Mass->GetMutableEntityManager().AsShared();
				
				OnCreationDelegate.Broadcast();
			}
		});
	GEditor->OnWorldDestroyed().AddLambda([this](UWorld* DestroyedWorld)
		{
			if (DestroyedWorld == ActiveEditorWorld)
			{
				Reset();
			}
		});
}

void UTypedElementDatabase::Deinitialize()
{
	FWorldDelegates::OnWorldTickStart.Remove(TickCallHandle);

	Reset();
}

TSharedPtr<FMassEntityManager> UTypedElementDatabase::GetActiveMutableEditorEntityManager()
{
	return ActiveEditorEntityManager;
}

TSharedPtr<const FMassEntityManager> UTypedElementDatabase::GetActiveEditorEntityManager() const
{
	return ActiveEditorEntityManager;
}

FTypedElementOnDataStorageCreation& UTypedElementDatabase::OnCreation()
{
	return OnCreationDelegate;
}

FTypedElementOnDataStorageDestruction& UTypedElementDatabase::OnDestruction()
{
	return OnDestructionDelegate;
}

FTypedElementOnDataStorageUpdate& UTypedElementDatabase::OnUpdate()
{
	return OnUpdateDelegate;
}

bool UTypedElementDatabase::IsAvailable() const
{
	return ActiveEditorWorld && ActiveEditorEntityManager;
}

TypedElementTableHandle UTypedElementDatabase::RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList)
{
	return RegisterTable(ColumnList, {});
}

TypedElementTableHandle UTypedElementDatabase::RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList, const FName Name)
{
	if (ActiveEditorEntityManager)
	{
		if (Name.IsValid())
		{
			if (!TableNameLookup.Contains(Name))
			{
				TypedElementTableHandle Result = Tables.Num();
				Tables.Add(ActiveEditorEntityManager->CreateArchetype(ColumnList, Name));
				TableNameLookup.Add(Name, Result);
				return Result;
			}
		}
		else
		{
			TypedElementTableHandle Result = Tables.Num();
			Tables.Add(ActiveEditorEntityManager->CreateArchetype(ColumnList, Name));
			return Result;
		}
	}
	return TypedElementInvalidTableHandle;
}

TypedElementRowHandle UTypedElementDatabase::AddRow(TypedElementTableHandle Table)
{
	checkf(Table < Tables.Num(), TEXT("Attempting to add a row to a non-existing table."));
	return ActiveEditorEntityManager ? 
		ActiveEditorEntityManager->CreateEntity(Tables[Table]).AsNumber() :
		TypedElementInvalidRowHandle;
}

TypedElementRowHandle UTypedElementDatabase::AddRow(FName TableName)
{
	TypedElementTableHandle* Table = TableNameLookup.Find(TableName);
	return Table ? AddRow(*Table) : TypedElementInvalidRowHandle;
}

bool UTypedElementDatabase::BatchAddRow(TypedElementTableHandle Table, int32 Count, TypedElementDataStorageCreationCallbackRef OnCreated)
{
	OnCreated.CheckCallable();
	checkf(Table < Tables.Num(), TEXT("Attempting to add multiple rows to a non-existing table."));
	if (ActiveEditorEntityManager)
	{
		TArray<FMassEntityHandle> Entities;
		Entities.Reserve(Count);
		TSharedRef<FMassEntityManager::FEntityCreationContext> Context = 
			ActiveEditorEntityManager->BatchCreateEntities(Tables[Table], Count, Entities);
		
		for (FMassEntityHandle Entity : Entities)
		{
			OnCreated(Entity.AsNumber());
		}

		return true;
	}
	return false;
}

bool UTypedElementDatabase::BatchAddRow(FName TableName, int32 Count, TypedElementDataStorageCreationCallbackRef OnCreated)
{
	TypedElementTableHandle* Table = TableNameLookup.Find(TableName);
	return Table ? BatchAddRow(*Table, Count, OnCreated) : false;
}

void UTypedElementDatabase::RemoveRow(TypedElementRowHandle Row)
{
	if (ActiveEditorEntityManager)
	{
		ActiveEditorEntityManager->DestroyEntity(FMassEntityHandle::FromNumber(Row));
	}
}

void* UTypedElementDatabase::GetColumnData(TypedElementRowHandle Row, const UScriptStruct* ColumnType)
{
	check(ColumnType);

	FMassEntityHandle Entity = FMassEntityHandle::FromNumber(Row);
	return ActiveEditorEntityManager && ActiveEditorEntityManager->IsEntityValid(Entity) ?
		ActiveEditorEntityManager->GetFragmentDataStruct(FMassEntityHandle::FromNumber(Row), ColumnType).GetMutableMemory() :
		nullptr;
}

void* UTypedElementDatabase::GetExternalSystemAddress(UClass* Target)
{
	return ActiveEditorWorld ? ActiveEditorWorld->GetSubsystemBase(Target) : nullptr;
}

void UTypedElementDatabase::Reset()
{
	OnDestructionDelegate.Broadcast();

	Tables.Reset();
	TableNameLookup.Reset();

	ActiveEditorWorld = nullptr;
	ActiveEditorEntityManager.Reset();
}