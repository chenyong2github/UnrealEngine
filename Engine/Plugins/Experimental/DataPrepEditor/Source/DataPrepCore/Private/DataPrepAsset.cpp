// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepAsset.h"

#include "DataprepActionAsset.h"
#include "DataprepCoreLogCategory.h"
#include "DataprepCoreUtils.h"
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

// FDataprepAssetAction =================================================================

FDataprepAssetAction::~FDataprepAssetAction()
{
	UnbindDataprepAssetFromAction();
}

FDataprepAssetAction& FDataprepAssetAction::operator=(const FDataprepAssetAction& Other)
{
	bIsEnabled = Other.bIsEnabled;
	DataprepAssetPtr = Other.DataprepAssetPtr;
	SetActionAsset( Other.ActionAsset );
	return *this;
}

FDataprepAssetAction& FDataprepAssetAction::operator=(FDataprepAssetAction&& Other)
{
	bIsEnabled = Other.bIsEnabled;
	DataprepAssetPtr = Other.DataprepAssetPtr;
	SetActionAsset( Other.ActionAsset );
	Other.SetActionAsset( nullptr );
	return *this;
}

void FDataprepAssetAction::SetActionAsset(UDataprepActionAsset* InActionAsset)
{
	if ( ActionAsset != InActionAsset )
	{
		UnbindDataprepAssetFromAction();
	}

	ActionAsset = InActionAsset;
	BindDataprepAssetToAction();

}

void FDataprepAssetAction::BindDataprepAssetToAction()
{
	if ( ActionAsset )
	{
		OnOperationOrderChangedHandle = ActionAsset->GetOnStepsOrderChanged().AddRaw( this, &FDataprepAssetAction::OnActionOperationsOrderChanged );
	}
}

void FDataprepAssetAction::UnbindDataprepAssetFromAction()
{
	if ( ActionAsset && OnOperationOrderChangedHandle.IsValid() )
	{
		ActionAsset->GetOnStepsOrderChanged().Remove( OnOperationOrderChangedHandle );
	}
}

void FDataprepAssetAction::OnActionOperationsOrderChanged()
{
	UDataprepAsset* DataprepAsset = DataprepAssetPtr.Get();
	if (DataprepAsset)
	{
		DataprepAsset->GetOnActionOperationsOrderChanged().Broadcast( ActionAsset );
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

#ifdef WITH_EDITOR
	OnAssetDeletedHandle = FEditorDelegates::OnAssetsDeleted.AddLambda( [this](const TArray<UClass *>& DeletedClasses)
		{
			for (UClass* Class : DeletedClasses)
			{
				if (Class->IsChildOf<UDataprepAsset>())
				{
					RemoveInvalidActions();
					break;
				}
			}
		});
#endif //WITH_EDITOR
}

UDataprepAsset::~UDataprepAsset()
{
#ifdef WITH_EDITOR
	FEditorDelegates::OnAssetsDeleted.Remove( OnAssetDeletedHandle );
#endif //WITH_EDITOR
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
					Consumer = NewObject< UDataprepContentConsumer >( GetOutermost(), CurrentClass, NAME_None, RF_Transactional );
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
		check(Consumer);
		Consumer->GetOnChanged().AddUObject( this, &UDataprepAsset::OnConsumerChanged );

		check(DataprepRecipeBP);
		DataprepRecipeBP->OnChanged().AddUObject( this, &UDataprepAsset::OnBlueprintChanged );

		for( FDataprepAssetProducer& Producer : Producers )
		{
			Producer.Producer->GetOnChanged().AddUObject( this, &UDataprepAsset::OnProducerChanged );
		}
	}
}

int32 UDataprepAsset::AddAction()
{
	UDataprepActionAsset* Action = NewObject< UDataprepActionAsset >( this );
	Actions.Emplace( Action, true, *this );
	OnActionsOrderChanged.Broadcast();
	return Actions.Num();
}

UDataprepActionAsset* UDataprepAsset::GetAction(int32 Index)
{
	// const_cast to avoid code duplication
	return const_cast< UDataprepActionAsset* >( static_cast< const UDataprepAsset* >( this )->GetAction( Index ) );
}

const UDataprepActionAsset* UDataprepAsset::GetAction(int32 Index) const
{
	if ( Actions.IsValidIndex( Index ) )
	{
		return Actions[ Index ].GetActionAsset();
	}

	UE_LOG( LogDataprepCore, Error, TEXT("DataprepAsset::GetAction: the Index is out of range") );
	return nullptr;
}

int32 UDataprepAsset::GetActionsCount() const
{
	return Actions.Num();
}

bool UDataprepAsset::IsActionEnabled(int32 Index) const
{
	if ( Actions.IsValidIndex( Index ) )
	{
		return Actions[ Index ].IsEnabled();
	}

	UE_LOG( LogDataprepCore, Error, TEXT("DataprepAsset::IsActionEnabled: the Index is out of range") );
	return false;
}

void UDataprepAsset::EnableAction(int32 Index, bool bEnable)
{
	if ( Actions.IsValidIndex( Index ) )
	{
		Actions[ Index ].Enable( bEnable );
	}
	else
	{
		UE_LOG( LogDataprepCore, Error, TEXT("DataprepAsset::EnableAction: the Index is out of range") );
	}
}

bool UDataprepAsset::MoveAction(int32 ActionIndex, int32 DestinationIndex)
{
	if ( DataprepCoreUtils::MoveArrayElement( Actions, ActionIndex, DestinationIndex ) )
	{
		OnActionsOrderChanged.Broadcast();
		return true;
	}

	if ( !Actions.IsValidIndex( ActionIndex ) )
	{
		UE_LOG( LogDataprepCore, Error, TEXT("DataprepAsset::MoveAction: the ActionIndex is out of range") );
	}
	if ( !Actions.IsValidIndex( DestinationIndex ) )
	{
		UE_LOG( LogDataprepCore, Error, TEXT("DataprepAsset::MoveAction: the Destination Index is out of range") );
	}
	if ( ActionIndex == DestinationIndex )
	{
		UE_LOG( LogDataprepCore, Error, TEXT("DataprepAsset::MoveAction: an action shouldn't be move at the location it currently is") );
	}
	return false;
}

bool UDataprepAsset::RemoveAction(int32 Index)
{
	if ( Actions.IsValidIndex( Index ) )
	{
		Actions.RemoveAt( Index );
		OnActionsOrderChanged.Broadcast();
		return true;
	}

	UE_LOG( LogDataprepCore, Error, TEXT("DataprepAsset::RemoveAction: the Index is out of range") );
	return false;
}

void UDataprepAsset::RemoveInvalidActions()
{
	bool bWasActionsModified = false;
	for ( int32 i = 0; i < Actions.Num(); i++ )
	{
		UDataprepActionAsset* Action = Actions[ i ].GetActionAsset();
		if ( !Action || Action->IsPendingKill() )
		{
			Actions.RemoveAt( i );
			i--;
			bWasActionsModified = true;
		}
	}

	if ( bWasActionsModified )
	{
		OnActionsOrderChanged.Broadcast();
	}
}

void UDataprepAsset::RunProducers(const UDataprepContentProducer::ProducerContext& InContext, TArray< TWeakObjectPtr< UObject > >& OutAssets)
{
	OutAssets.Empty();

	for ( FDataprepAssetProducer& AssetProducer : Producers )
	{
		const bool bIsOkToRun = AssetProducer.Producer && AssetProducer.bIsEnabled &&
			( AssetProducer.SupersededBy == INDEX_NONE || !Producers[AssetProducer.SupersededBy].bIsEnabled );

		if ( bIsOkToRun )
		{
			FString OutReason;
			if (AssetProducer.Producer->Initialize( InContext, OutReason ))
			{
				if ( AssetProducer.Producer->Produce() )
				{
					const TArray< TWeakObjectPtr< UObject > >& ProducerAssets = AssetProducer.Producer->GetAssets();
					if (ProducerAssets.Num() > 0)
					{
						OutAssets.Append( ProducerAssets );
					}
				}
				else
				{
					OutReason = FText::Format(NSLOCTEXT("DataprepAsset", "ProducerRunFailed", "{0} failed to run."), FText::FromString( AssetProducer.Producer->GetName() ) ).ToString();
				}
			}

			AssetProducer.Producer->Reset();

			if( !OutReason.IsEmpty() )
			{
				// #ueent_todo: Log that producer has failed
			}
		}
	}
}

bool UDataprepAsset::RunConsumer( const UDataprepContentConsumer::ConsumerContext& InContext, FString& OutReason)
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

bool UDataprepAsset::AddProducer(UClass* ProducerClass)
{
	if( ProducerClass && ProducerClass->IsChildOf( UDataprepContentProducer::StaticClass() ) )
	{
		UDataprepContentProducer* Producer = NewObject< UDataprepContentProducer >( GetOutermost(), ProducerClass, NAME_None, RF_Transactional );
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
		UDataprepContentProducer* Producer = Producers[IndexToRemove].Producer;

		Producer->GetOnChanged().RemoveAll( this );

		DataprepAssetUtil::DeleteRegisteredAsset( Producer );

		Producers.RemoveAt( IndexToRemove );

		if(Producers.Num() == 1)
		{
			Producers[0].SupersededBy = INDEX_NONE;
		}
		else if(Producers.Num() > 1)
		{
			// Update value stored in SupersededBy property where applicable
			for( FDataprepAssetProducer& AssetProducer : Producers )
			{
				if( AssetProducer.SupersededBy == IndexToRemove )
				{
					AssetProducer.SupersededBy = INDEX_NONE;
				}
				else if( AssetProducer.SupersededBy > IndexToRemove )
				{
					--AssetProducer.SupersededBy;
				}
			}
		}

		MarkPackageDirty();

		OnChanged.Broadcast( FDataprepAssetChangeType::ProducerRemoved, IndexToRemove );

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

		Consumer = NewObject< UDataprepContentConsumer >( GetOutermost(), NewConsumerClass, NAME_None, RF_Transactional );
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

			if( !SupersedingAssetProducer.Producer->Supersede( InAssetProducer.Producer ) )
			{
				InAssetProducer.SupersededBy = INDEX_NONE;
			}
		}

		// Check if producer is now superseded by any other producer
		int32 SupersederIndex = 0;
		for( FDataprepAssetProducer& AssetProducer : Producers )
		{
			if( AssetProducer.Producer != InAssetProducer.Producer &&
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