// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepFactories.h"

#include "DataPrepAsset.h"
#include "DataPrepContentConsumer.h"

#include "AssetRegistryModule.h"
#include "AssetTypeCategories.h"
#include "UObject/UObjectIterator.h"

UDataprepAssetFactory::UDataprepAssetFactory()
{
	SupportedClass = UDataprepAsset::StaticClass();

	bCreateNew = true;
	bText = false;
	bEditorImport = false;
}

UObject * UDataprepAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext *Warn)
{
	check( InClass->IsChildOf( UDataprepAsset::StaticClass() ) );
	UDataprepAsset* DataprepAsset = NewObject<UDataprepAsset>(InParent, InClass, InName, Flags | RF_Transactional);

	FAssetRegistryModule::AssetCreated( DataprepAsset );
	DataprepAsset->MarkPackageDirty();

	return DataprepAsset;
}
