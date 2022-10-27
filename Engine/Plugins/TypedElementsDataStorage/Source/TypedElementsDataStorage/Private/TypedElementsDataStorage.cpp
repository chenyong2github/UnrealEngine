// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementsDataStorage.h"

#include "Elements/Framework/TypedElementRegistry.h"
#include "Misc/CoreDelegates.h"
#include "TypedElementDatabase.h"
#include "TypedElementDatabaseCompatibility.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "FTypedElementsDataStorageModule"

void FTypedElementsDataStorageModule::StartupModule()
{
	FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddLambda(
		[this]()
		{
			if (!bInitialized)
			{
				Database = NewObject<UTypedElementDatabase>();
				Database->Initialize();

				DatabaseCompatibility = NewObject<UTypedElementDatabaseCompatibility>();
				DatabaseCompatibility->Initialize(Database.Get());

				UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
				check(Registry);
				Registry->SetDataStorage(Database.Get());
				Registry->SetDataStorageCompatibility(DatabaseCompatibility.Get());

				bInitialized = true;
			}
		});
}

void FTypedElementsDataStorageModule::ShutdownModule()
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
	if (Registry) // If the registry has already been destroyed there's no point in clearing the reference.
	{
		Registry->SetDataStorage(nullptr);
		Registry->SetDataStorageCompatibility(nullptr);
	}

	DatabaseCompatibility->Deinitialize();
	Database->Deinitialize();
	bInitialized = false;
}

void FTypedElementsDataStorageModule::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (bInitialized)
	{
		Collector.AddReferencedObject(Database);
		Collector.AddReferencedObject(DatabaseCompatibility);
	}
}

FString FTypedElementsDataStorageModule::GetReferencerName() const
{
	return TEXT("Typed Elements: Data Storage Module");
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTypedElementsDataStorageModule, TypedElementsDataStorage)