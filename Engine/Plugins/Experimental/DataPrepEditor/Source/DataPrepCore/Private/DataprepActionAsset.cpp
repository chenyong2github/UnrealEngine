// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepActionAsset.h"

// Dataprep include
#include "DataPrepOperation.h"
#include "DataprepCoreLogCategory.h"
#include "DataprepCorePrivateUtils.h"
#include "SelectionSystem/DataprepFilter.h"

// Engine include
#include "Templates/SubclassOf.h"
#include "UObject/ObjectMacros.h"

#ifdef WITH_EDITOR
#include "Editor.h"
#endif //WITH_EDITOR

UDataprepActionAsset::UDataprepActionAsset()
{
#ifdef WITH_EDITOR
	OnAssetDeletedHandle = FEditorDelegates::OnAssetsDeleted.AddUObject( this, &UDataprepActionAsset::OnClassesRemoved );
#endif //WITH_EDITOR
}

UDataprepActionAsset::~UDataprepActionAsset()
{
#ifdef WITH_EDITOR
	FEditorDelegates::OnAssetsDeleted.Remove( OnAssetDeletedHandle );
#endif //WITH_EDITOR
}

void UDataprepActionAsset::Execute(const TArray<UObject*>& InObjects)
{
	// Make a copy of the objects to act on
	CurrentlySelectedObjects = InObjects;

	// Execute steps sequentially
	for ( UDataprepActionStep* Step : Steps )
	{
		if ( Step && Step->bIsEnabled )
		{
			if ( UDataprepOperation* Operation = Step->Operation )
			{
				Operation->Execute( CurrentlySelectedObjects );
			}
			else if ( UDataprepFilter* Filter = Step->Filter )
			{
				CurrentlySelectedObjects = Filter->FilterObjects( CurrentlySelectedObjects );
			}
		}
	}

	// Reset list of selected objects
	CurrentlySelectedObjects.Reset();
}

int32 UDataprepActionAsset::AddOperation(const TSubclassOf<UDataprepOperation>& OperationClass)
{
	UClass* Class = OperationClass;
	if ( Class )
	{
		Modify();
		UDataprepActionStep* ActionStep = NewObject< UDataprepActionStep >( this, UDataprepActionStep::StaticClass(), NAME_None, RF_Transactional );
		ActionStep->Operation = NewObject< UDataprepOperation >( ActionStep, Class, NAME_None, RF_Transactional );
		ActionStep->bIsEnabled = true;
		Steps.Add( ActionStep );
		OnStepsChanged.Broadcast();
		return Steps.Num() - 1;
	}

	ensure( false );
	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::AddOperation: The Operation Class is invalid") );
	// Invalid subclass
	return INDEX_NONE;
}

int32 UDataprepActionAsset::AddFilterWithAFetcher(const TSubclassOf<UDataprepFilter>& InFilterClass, const TSubclassOf<UDataprepFetcher>& InFetcherClass)
{
	UClass* FilterClass = InFilterClass;
	UClass* FetcherClass = InFetcherClass;

	if ( FilterClass && FetcherClass )
	{
		UDataprepFilter* Filter = FilterClass->GetDefaultObject<UDataprepFilter>();
		if ( Filter && FetcherClass->IsChildOf( Filter->GetAcceptedFetcherClass() ) )
		{
			Modify();
			UDataprepActionStep* ActionStep = NewObject< UDataprepActionStep >( this, UDataprepActionStep::StaticClass(), NAME_None, RF_Transactional );
			ActionStep->Filter = NewObject< UDataprepFilter >( ActionStep, FilterClass, NAME_None, RF_Transactional );
			ActionStep->Filter->SetFetcher( InFetcherClass );
			ActionStep->bIsEnabled = true;
			Steps.Add( ActionStep );
			OnStepsChanged.Broadcast();
			return Steps.Num() - 1;
		}
		else
		{
			UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::AddFilterWithAFetcher: The Fetcher Class is not compatible with the Filter Class") );
		}
	}
	else
	{
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::AddFilterWithAFetcher: At least one of the class arguments is invalid") );
	}

	ensure( false );
	// Invalid
	return INDEX_NONE;
}

int32 UDataprepActionAsset::AddStep(const UDataprepActionStep* InActionStep)
{
	if ( InActionStep )
	{
		Modify();
		UDataprepActionStep* ActionStep = DuplicateObject<UDataprepActionStep>( InActionStep, this);
		Steps.Add( ActionStep );
		OnStepsChanged.Broadcast();
		return Steps.Num() - 1;
	}

	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::AddStep: The action step is invalid") );
	ensure(false);
	// Invalid
	return INDEX_NONE;
}

TWeakObjectPtr<UDataprepActionStep> UDataprepActionAsset::GetStep(int32 Index)
{
	// Avoid code duplication
	return static_cast< const UDataprepActionAsset* >( this )->GetStep( Index ) ;
}

const TWeakObjectPtr<UDataprepActionStep> UDataprepActionAsset::GetStep(int32 Index) const
{
	if ( Steps.IsValidIndex( Index ) )
	{
		return Steps[ Index ];
	}

	ensure( false );
	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::GetStep: The Index is out of range") );
	return nullptr;
}

int32 UDataprepActionAsset::GetStepsCount() const
{
	return Steps.Num();
}

bool UDataprepActionAsset::IsStepEnabled(int32 Index) const
{
	if (Steps.IsValidIndex(Index))
	{
		return Steps[Index]->bIsEnabled;
	}

	ensure( false );
	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::IsStepEnabled: The Index is out of range") );
	return false;
}

void UDataprepActionAsset::EnableStep(int32 Index, bool bEnable)
{
	if (Steps.IsValidIndex(Index))
	{
		Modify();
		Steps[Index]->bIsEnabled = bEnable;
	}
	else
	{
		ensure( false );
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::EnableStep: The Index is out of range") );
	}
}

bool UDataprepActionAsset::MoveStep(int32 StepIndex, int32 DestinationIndex)
{
	if ( Steps.IsValidIndex( StepIndex ) && Steps.IsValidIndex( DestinationIndex ) )
	{
		Modify();
	}

	if ( DataprepCorePrivateUtils::MoveArrayElement( Steps, StepIndex, DestinationIndex ) )
	{
		OnStepsChanged.Broadcast();
		return true;
	}

	if ( !Steps.IsValidIndex( StepIndex ) )
	{
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::MoveStep: The Step Index is out of range") );
	}
	if ( !Steps.IsValidIndex( DestinationIndex ) )
	{
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::MoveStep: The Destination Index is out of range") );
	}
	if ( StepIndex == DestinationIndex )
	{
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::MoveStep: a Step shouldn't be move at the location it currently is") );
	}

	ensure( false );
	return false;
}

bool UDataprepActionAsset::RemoveStep(int32 Index)
{
	if ( Steps.IsValidIndex( Index ) )
	{
		Modify();
		Steps.RemoveAt( Index );
		OnStepsChanged.Broadcast();
		return true;
	}

	ensure( false );
	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepActionAsset::RemoveStep: The Index is out of range") );
	return false;
}

FOnStepsOrderChanged& UDataprepActionAsset::GetOnStepsOrderChanged()
{
	return OnStepsChanged;
}

void UDataprepActionAsset::OnClassesRemoved(const TArray<UClass *>& DeletedClasses)
{
	for ( UClass* Class : DeletedClasses )
	{
		if ( Class->IsChildOf<UDataprepOperation>() )
		{
			RemoveInvalidOperations();
			break;
		}
	}
}

void UDataprepActionAsset::RemoveInvalidOperations()
{
	bool bWasOperationsModified = false;
	for ( int32 i = 0; i < Steps.Num(); i++ )
	{
		UDataprepOperation* Operation = Steps[ i ]->Operation;
		if ( !Operation || Operation->IsPendingKill() )
		{
			Steps.RemoveAt( i );
			i--;
			bWasOperationsModified = true;
		}
	}

	if ( bWasOperationsModified )
	{
		OnStepsChanged.Broadcast();
	}
}
