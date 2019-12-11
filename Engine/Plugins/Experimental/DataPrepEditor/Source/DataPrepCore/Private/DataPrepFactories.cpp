// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepFactories.h"

#include "DataPrepAsset.h"
#include "DataprepAssetInstance.h"
#include "DataPrepContentConsumer.h"

#include "AssetRegistryModule.h"
#include "AssetTypeCategories.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

UDataprepAssetFactory::UDataprepAssetFactory()
{
	SupportedClass = UDataprepAsset::StaticClass();

	bCreateNew = true;
	bText = false;
	bEditorImport = false;
}

bool UDataprepAssetFactory::ShouldShowInNewMenu() const
{
	// If there is no consumer don't show this factory
	TArray< UClass* > PotentialClasses;
	GetDerivedClasses( UDataprepContentConsumer::StaticClass(), PotentialClasses, true );

	for ( UClass* ChildClass : PotentialClasses )
	{
		if ( ChildClass && !ChildClass->HasAnyClassFlags( CLASS_CompiledFromBlueprint | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Abstract ) && ChildClass->HasAllClassFlags( CLASS_Native ) )
		{
			return true;
		}
	}

	return false;
}

UObject * UDataprepAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext *Warn)
{
	check( InClass->IsChildOf( UDataprepAsset::StaticClass() ) );

	// Find potential Consumer classes
	TArray<UClass*> ConsumerClasses;
	for( TObjectIterator< UClass > It ; It ; ++It )
	{
		UClass* CurrentClass = (*It);

		if ( !CurrentClass->HasAnyClassFlags( CLASS_Abstract ) )
		{
			if( CurrentClass->IsChildOf( UDataprepContentConsumer::StaticClass() ) )
			{
				ConsumerClasses.Add( CurrentClass );
			}
		}
	}

	if(ConsumerClasses.Num() == 0)
	{
		return nullptr;
	}

	UDataprepAsset* DataprepAsset = NewObject<UDataprepAsset>(InParent, InClass, InName, Flags | RF_Transactional);
	check(DataprepAsset);

	// Initialize Dataprep asset's consumer
	if(ConsumerClasses.Num() == 1)
	{
		DataprepAsset->SetConsumer( ConsumerClasses[0], /* bNotifyChanges = */ false );
	}
	else
	{
		// #ueent_todo: Propose user to choose from the list of Consumers.
		DataprepAsset->SetConsumer( ConsumerClasses[0], /* bNotifyChanges = */ false );
	}
	check( DataprepAsset->GetConsumer() );

	// Temp code for the nodes development
	// Initialize Dataprep asset's blueprint
	DataprepAsset->CreateBlueprint();
	// end of temp code for nodes development

	DataprepAsset->CreateParameterization();

	FAssetRegistryModule::AssetCreated( DataprepAsset );
	DataprepAsset->MarkPackageDirty();

	return DataprepAsset;
}

UDataprepAssetInstanceFactory::UDataprepAssetInstanceFactory()
{
	SupportedClass = UDataprepAssetInstance::StaticClass();

	bCreateNew = false;
	bText = false;
	bEditorImport = false;
}

UObject* UDataprepAssetInstanceFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if(UDataprepAssetInterface* DataprepAssetParent = Cast<UDataprepAssetInterface>(InitialParent))
	{
		UDataprepAssetInstance* DataprepAssetInstance = NewObject<UDataprepAssetInstance>(InParent, InClass, InName, Flags);

		if(DataprepAssetInstance && DataprepAssetParent->GetConsumer())
		{
			if(DataprepAssetInstance->SetParent(DataprepAssetParent, /* bNotifyChanges = */ false))
			{
				FAssetRegistryModule::AssetCreated( DataprepAssetInstance );
				DataprepAssetInstance->MarkPackageDirty();

				return DataprepAssetInstance;
			}
		}
	}

	return nullptr;
}