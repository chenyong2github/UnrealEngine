// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepAsset.h"

#include "DataprepActionAsset.h"
#include "DataprepCoreLogCategory.h"
#include "DataPrepRecipe.h"

#include "AssetRegistryModule.h"
#ifdef WITH_EDITOR
#include "Editor.h"
#endif //WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"

namespace DataprepAssetUtil
{
	void DeleteRegisteredAsset(UObject* Asset)
	{
		if(Asset != nullptr)
		{
			Asset->Rename( nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional );

			Asset->ClearFlags(RF_Standalone | RF_Public);
			Asset->RemoveFromRoot();
			Asset->MarkPendingKill();

			FAssetRegistryModule::AssetDeleted( Asset ) ;
		}
	}
}


// UDataprepAsset =================================================================

UDataprepAsset::UDataprepAsset()
{
#if WITH_EDITORONLY_DATA
	Consumer = nullptr;
#endif

	// Temp code for the nodes development
	DataprepRecipeBP = nullptr;
	// end of temp code for nodes development
}

void UDataprepAsset::PostInitProperties()
{
	Super::PostInitProperties();

	if(HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad) == false)
	{
		// Set DataprepAsset's consumer to the first registered consumer
		for( TObjectIterator< UClass > It ; It ; ++It )
		{
			UClass* CurrentClass = (*It);

			if ( !CurrentClass->HasAnyClassFlags( CLASS_Abstract ) )
			{
				if( CurrentClass->IsChildOf( UDataprepContentConsumer::StaticClass() ) )
				{
					FString BaseName = GetName() + TEXT("_Consumer");
					FName ConsumerName = MakeUniqueObjectName( this, CurrentClass, *BaseName );
					Consumer = NewObject< UDataprepContentConsumer >( this, CurrentClass, ConsumerName, RF_Transactional );
					check( Consumer );

					FAssetRegistryModule::AssetCreated( Consumer );
					Consumer->MarkPackageDirty();

					break;
				}
			}
		}

		// Begin: Temp code for the nodes development
		const FString DesiredName = GetName() + TEXT("_Recipe");
		FName BlueprintName = MakeUniqueObjectName( GetOutermost(), UBlueprint::StaticClass(), *DesiredName );

		DataprepRecipeBP = FKismetEditorUtilities::CreateBlueprint( UDataprepRecipe::StaticClass(), this, BlueprintName, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass() );
		check( DataprepRecipeBP );

		// This blueprint is not the asset of the package
		DataprepRecipeBP->ClearFlags( RF_Standalone );

		FAssetRegistryModule::AssetCreated( DataprepRecipeBP );

		DataprepRecipeBP->MarkPackageDirty();

		DataprepRecipeBP->OnChanged().AddUObject( this, &UDataprepAsset::OnBlueprintChanged );
		// End: Temp code for the nodes development
	}
}

void UDataprepAsset::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );

	if( Ar.IsLoading() )
	{
		if(Consumer != nullptr)
		{
			Consumer->GetOnChanged().AddUObject( this, &UDataprepAsset::OnConsumerChanged );
		}

		check(DataprepRecipeBP);
		DataprepRecipeBP->OnChanged().AddUObject( this, &UDataprepAsset::OnBlueprintChanged );

		for( FDataprepAssetProducer& Producer : Producers )
		{
			if(Producer.Producer)
			{
				Producer.Producer->GetOnChanged().AddUObject( this, &UDataprepAsset::OnProducerChanged );
			}
		}
	}
}

void UDataprepAsset::RunProducers(const UDataprepContentProducer::ProducerContext& InContext, TArray< TWeakObjectPtr< UObject > >& OutAssets)
{
	if( Producers.Num() == 0 )
	{
		return;
	}

	OutAssets.Empty();

	FDataprepProgressTask Task( *InContext.ProgressReporterPtr, NSLOCTEXT( "DataprepAsset", "RunProducers", "Importing ..." ), (float)Producers.Num(), 1.0f );

	for ( FDataprepAssetProducer& AssetProducer : Producers )
	{
		if( UDataprepContentProducer* Producer = AssetProducer.Producer )
		{
			Task.ReportNextStep( FText::Format( NSLOCTEXT( "DataprepAsset", "ProducerReport", "Importing {0} ..."), FText::FromString( Producer->GetName() ) ) );

			// Run producer if enabled and, if superseded, superseder is disabled
			const bool bIsOkToRun = AssetProducer.bIsEnabled &&	( AssetProducer.SupersededBy == INDEX_NONE || !Producers[AssetProducer.SupersededBy].bIsEnabled );

			if ( bIsOkToRun )
			{
				FString OutReason;
				if (Producer->Initialize( InContext, OutReason ))
				{
					if ( Producer->Produce() )
					{
						const TArray< TWeakObjectPtr< UObject > >& ProducerAssets = Producer->GetAssets();
						if (ProducerAssets.Num() > 0)
						{
							OutAssets.Append( ProducerAssets );
						}
					}
					else
					{
						OutReason = FText::Format(NSLOCTEXT("DataprepAsset", "ProducerRunFailed", "{0} failed to run."), FText::FromString( Producer->GetName() ) ).ToString();
					}
				}

				Producer->Reset();

				if( !OutReason.IsEmpty() )
				{
					// #ueent_todo: Log that producer has failed
				}
			}
		}
		else
		{
			Task.ReportNextStep( NSLOCTEXT( "DataprepAsset", "ProducerReport", "Skipped invalid producer ...") );
		}
	}
}

bool UDataprepAsset::RunConsumer( const UDataprepContentConsumer::ConsumerContext& InContext, FString& OutReason)
{
	if(Consumer)
	{
		if( !Consumer->Initialize( InContext, OutReason ) )
		{
			return false;
		}

		// #ueent_todo: Update state of entry: finalizing

		if ( !Consumer->Run() )
		{
			// #ueent_todo: Inform execution has failed
			return false;
		}

		Consumer->Reset();

		return true;
	}

	return false;
}

bool UDataprepAsset::AddProducer(UClass* ProducerClass)
{
	if( ProducerClass && ProducerClass->IsChildOf( UDataprepContentProducer::StaticClass() ) )
	{
		UDataprepContentProducer* Producer = NewObject< UDataprepContentProducer >( this, ProducerClass, NAME_None, RF_Transactional );
		FAssetRegistryModule::AssetCreated( Producer );
		Producer->MarkPackageDirty();

		int32 ProducerNextIndex = Producers.Num();
		Producers.Emplace( Producer, true );

		Producer->GetOnChanged().AddUObject(this, &UDataprepAsset::OnProducerChanged);
		MarkPackageDirty();

		OnChanged.Broadcast( FDataprepAssetChangeType::ProducerAdded, ProducerNextIndex );

		return true;
	}

	return false;
}

bool UDataprepAsset::RemoveProducer(int32 IndexToRemove)
{
	if( Producers.IsValidIndex( IndexToRemove ) )
	{
		if(UDataprepContentProducer* Producer = Producers[IndexToRemove].Producer)
		{
			Producer->GetOnChanged().RemoveAll( this );

			DataprepAssetUtil::DeleteRegisteredAsset( Producer );
		}

		Producers.RemoveAt( IndexToRemove );

		// Array of producers superseded by removed producer
		TArray<int32> ProducersToRevisit;
		ProducersToRevisit.Reserve( Producers.Num() );

		if( Producers.Num() == 1 )
		{
			Producers[0].SupersededBy = INDEX_NONE;
		}
		else if(Producers.Num() > 1)
		{
			// Update value stored in SupersededBy property where applicable
			for( int32 Index = 0; Index < Producers.Num(); ++Index )
			{
				FDataprepAssetProducer& AssetProducer = Producers[Index];

				if( AssetProducer.SupersededBy == IndexToRemove )
				{
					AssetProducer.SupersededBy = INDEX_NONE;
					ProducersToRevisit.Add( Index );
				}
				else if( AssetProducer.SupersededBy > IndexToRemove )
				{
					--AssetProducer.SupersededBy;
				}
			}
		}

		MarkPackageDirty();

		OnChanged.Broadcast( FDataprepAssetChangeType::ProducerRemoved, IndexToRemove );

		// Update superseding status for producers depending on removed producer
		bool bChangeAll = false;

		for( int32 ProducerIndex : ProducersToRevisit )
		{
			bool bLocalChangeAll = false;
			ValidateProducerChanges( ProducerIndex, bLocalChangeAll );
			bChangeAll |= bLocalChangeAll;
		}

		// Notify observes on additional changes
		if( bChangeAll )
		{
			OnChanged.Broadcast( FDataprepAssetChangeType::ProducerModified, INDEX_NONE );
		}
		else
		{
			for( int32 ProducerIndex : ProducersToRevisit )
			{
				OnChanged.Broadcast( FDataprepAssetChangeType::ProducerModified, ProducerIndex );
			}
		}

		return true;
	}

	return false;
}

void UDataprepAsset::EnableProducer(int32 Index, bool bValue)
{
	if( Producers.IsValidIndex( Index ) )
	{
		Producers[Index].bIsEnabled = bValue;

		MarkPackageDirty();

		// Relay change notification to observers of this object
		OnChanged.Broadcast( FDataprepAssetChangeType::ProducerModified, Index );
	}
}

bool UDataprepAsset::EnableAllProducers(bool bValue)
{
	if( Producers.Num() > 0 )
	{
		for( FDataprepAssetProducer& Producer : Producers )
		{
			Producer.bIsEnabled = bValue;
		}

		MarkPackageDirty();

		OnChanged.Broadcast( FDataprepAssetChangeType::ProducerModified, INDEX_NONE );

		return true;
	}

	return false;
}

bool UDataprepAsset::ReplaceConsumer(UClass* NewConsumerClass)
{
	if( NewConsumerClass && NewConsumerClass->IsChildOf(UDataprepContentConsumer::StaticClass()))
	{
		if(Consumer != nullptr)
		{
			Consumer->GetOnChanged().RemoveAll( this );
			DataprepAssetUtil::DeleteRegisteredAsset( Consumer );
		}

		FString BaseName = GetName() + TEXT("_Consumer");
		FName ConsumerName = MakeUniqueObjectName( this, NewConsumerClass, *BaseName );
		Consumer = NewObject< UDataprepContentConsumer >( this, NewConsumerClass, ConsumerName, RF_Transactional );
		check( Consumer );

		FAssetRegistryModule::AssetCreated( Consumer );
		Consumer->MarkPackageDirty();

		Consumer->GetOnChanged().AddUObject( this, &UDataprepAsset::OnConsumerChanged );
		MarkPackageDirty();

		OnChanged.Broadcast( FDataprepAssetChangeType::ConsumerModified, INDEX_NONE );

		return true;
	}

	return false;
}

void UDataprepAsset::OnConsumerChanged()
{
	MarkPackageDirty();

	// Broadcast change on consumer to observers of this object
	OnChanged.Broadcast( FDataprepAssetChangeType::ConsumerModified, INDEX_NONE );
}

void UDataprepAsset::OnProducerChanged( const UDataprepContentProducer* InProducer )
{
	int32 FoundIndex = 0;
	for( FDataprepAssetProducer& AssetProducer : Producers )
	{
		if( AssetProducer.Producer == InProducer )
		{
			break;
		}

		++FoundIndex;
	}

	// Verify found producer is not now superseded by another one
	if( FoundIndex < Producers.Num() )
	{
		bool bChangeAll = false;
		ValidateProducerChanges( FoundIndex, bChangeAll );

		MarkPackageDirty();

		// Relay change notification to observers of this object
		OnChanged.Broadcast( FDataprepAssetChangeType::ProducerModified, bChangeAll ? INDEX_NONE : FoundIndex );
	}
}

void UDataprepAsset::OnBlueprintChanged( UBlueprint* InBlueprint )
{
	if(InBlueprint == DataprepRecipeBP)
	{
		OnChanged.Broadcast( FDataprepAssetChangeType::BlueprintModified, INDEX_NONE );
	}
}

void UDataprepAsset::ValidateProducerChanges( int32 InIndex, bool &bChangeAll )
{
	bChangeAll = false;

	if( Producers.IsValidIndex( InIndex ) && Producers.Num() > 1 )
	{
		FDataprepAssetProducer& InAssetProducer = Producers[InIndex];

		// Check if input producer is still superseded if applicable
		if( InAssetProducer.SupersededBy != INDEX_NONE )
		{
			FDataprepAssetProducer& SupersedingAssetProducer = Producers[ InAssetProducer.SupersededBy ];

			if( SupersedingAssetProducer.Producer != nullptr && !SupersedingAssetProducer.Producer->Supersede( InAssetProducer.Producer ) )
			{
				InAssetProducer.SupersededBy = INDEX_NONE;
			}
		}

		// Check if producer is now superseded by any other producer
		int32 SupersederIndex = 0;
		for( FDataprepAssetProducer& AssetProducer : Producers )
		{
			if( AssetProducer.Producer != nullptr &&
				AssetProducer.Producer != InAssetProducer.Producer &&
				AssetProducer.bIsEnabled == true && 
				AssetProducer.SupersededBy == INDEX_NONE && 
				AssetProducer.Producer->Supersede( InAssetProducer.Producer ) )
			{
				// Disable found producer if another producer supersedes its production
				InAssetProducer.SupersededBy = SupersederIndex;
				break;
			}
			SupersederIndex++;
		}

		// If input producer superseded any other producer, check if this is still valid
		// Check if input producer does not supersede other producers
		if( InAssetProducer.Producer != nullptr )
		{
			for( FDataprepAssetProducer& AssetProducer : Producers )
			{
				if( AssetProducer.Producer != InAssetProducer.Producer )
				{
					if( AssetProducer.SupersededBy == InIndex )
					{
						if( !InAssetProducer.Producer->Supersede( AssetProducer.Producer ) )
						{
							bChangeAll = true;
							AssetProducer.SupersededBy = INDEX_NONE;
						}
					}
					else if( InAssetProducer.SupersededBy == INDEX_NONE && InAssetProducer.Producer->Supersede( AssetProducer.Producer ) )
					{
						bChangeAll = true;
						AssetProducer.SupersededBy = InIndex;
					}
				}
			}
		}
	}
}