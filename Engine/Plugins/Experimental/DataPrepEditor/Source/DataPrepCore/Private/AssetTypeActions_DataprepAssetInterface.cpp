// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_DataprepAssetInterface.h"

#include "DataprepAssetInstance.h"
#include "DataprepAssetProducers.h"
#include "DataprepCoreLibrary.h"
#include "DataPrepCoreModule.h"
#include "DataPrepFactories.h"

#include "Developer/AssetTools/Public/IAssetTools.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"

// UI
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DataprepAssetInterface"

uint32 FAssetTypeActions_DataprepAssetInterface::GetCategories()
{
	return IDataprepCoreModule::DataprepCategoryBit;
}

FText FAssetTypeActions_DataprepAssetInterface::GetName() const
{
	return LOCTEXT( "Name", "Dataprep Interface" );
}

UClass* FAssetTypeActions_DataprepAssetInterface::GetSupportedClass() const
{
	return UDataprepAssetInterface::StaticClass();
}

void FAssetTypeActions_DataprepAssetInterface::CreateInstance(TArray<TWeakObjectPtr<UDataprepAssetInterface>> DataprepAssetInterfaces)
{
	// Code is inspired from FAssetTypeActions_MaterialInterface::ExecuteNewMIC
	const FString DefaultSuffix = TEXT("_Inst");

	if(DataprepAssetInterfaces.Num() == 1)
	{
		if(UDataprepAssetInterface* Object = DataprepAssetInterfaces[0].Get())
		{
			// Determine an appropriate and unique name 
			FString Name;
			FString PackageName;
			CreateUniqueAssetName(Object->GetOutermost()->GetName(), DefaultSuffix, PackageName, Name);

			// Create the factory used to generate the asset
			UDataprepAssetInstanceFactory* Factory = NewObject<UDataprepAssetInstanceFactory>();
			Factory->InitialParent = Object;

			// Create asset in 
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			ContentBrowserModule.Get().CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackageName), UDataprepAssetInstance::StaticClass(), Factory);
		}
	}
	else
	{
		TArray<UObject*> AssetsToSync;
		for(auto AssetIt = DataprepAssetInterfaces.CreateConstIterator(); AssetIt; ++AssetIt)
		{
			if(UDataprepAssetInterface* DataprepAssetInterface = (*AssetIt).Get())
			{
				// Determine an appropriate name
				FString Name;
				FString PackageName;
				CreateUniqueAssetName(DataprepAssetInterface->GetOutermost()->GetName(), DefaultSuffix, PackageName, Name);

				// Create the factory used to generate the asset
				UDataprepAssetInstanceFactory* Factory = NewObject<UDataprepAssetInstanceFactory>();
				Factory->InitialParent = DataprepAssetInterface;

				IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
				UObject* NewAsset = AssetTools.CreateAsset(Name, FPackageName::GetLongPackagePath(PackageName), UDataprepAssetInstance::StaticClass(), Factory);

				if(NewAsset)
				{
					AssetsToSync.Add(NewAsset);
				}
			}
		}

		if(AssetsToSync.Num() > 0)
		{
			FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync, /*bAllowLockedBrowsers=*/true);
		}
	}
}

void FAssetTypeActions_DataprepAssetInterface::ExecuteDataprepAssets(TArray<TWeakObjectPtr<UDataprepAssetInterface>> DataprepAssetInterfaces)
{
	for(TWeakObjectPtr<UDataprepAssetInterface>& DataprepAssetInterfacePtr : DataprepAssetInterfaces)
	{
		if( UDataprepAssetInterface* DataprepAssetInterface = DataprepAssetInterfacePtr.Get() )
		{
			// Nothing to do if the Dataprep asset does not have any inputs
			if(DataprepAssetInterface->GetProducers()->GetProducersCount() > 0)
			{
				UDataprepCoreLibrary::ExecuteWithReporting( DataprepAssetInterface );
			}
		}
	}
}

void FAssetTypeActions_DataprepAssetInterface::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	auto DataprepAssetInterfaces = GetTypedWeakObjectPtrs<UDataprepAssetInterface>(InObjects);

	if(DataprepAssetInterfaces.Num() == 0)
	{
		return;
	}

	// #ueent_remark: An instance of an instance is not supported for 4.24.
	// Do not expose 'Create Instance' menu entry if at least one Dataprep asset is an instance
	bool bContainsAnInstance  = false;
	for (UObject* Object : InObjects)
	{
		if (Object && Object->GetClass() == UDataprepAssetInstance::StaticClass())
		{
			bContainsAnInstance = true;
			break;
		}
	}

	if (!bContainsAnInstance)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateInstance", "Create Instance"),
			LOCTEXT("CreateInstanceTooltip", "Creates a parameterized Dataprep asset using this Dataprep asset as a base."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAssetTypeActions_DataprepAssetInterface::CreateInstance, DataprepAssetInterfaces),
				FCanExecuteAction()
			)
		);
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("RunAsset", "Execute"),
		LOCTEXT("RunAssetTooltip", "Runs the Dataprep asset's producers, execute its recipe, finally runs the consumer"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_DataprepAssetInterface::ExecuteDataprepAssets, DataprepAssetInterfaces),
			FCanExecuteAction()
		)
	);
}

#undef LOCTEXT_NAMESPACE