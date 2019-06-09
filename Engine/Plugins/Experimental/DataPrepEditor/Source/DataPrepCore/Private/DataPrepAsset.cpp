// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepAsset.h"

#include "DataprepActionAsset.h"
#include "DataprepCoreLogCategory.h"
#include "DataprepCoreUtils.h"

#ifdef WITH_EDITOR
#include "Editor.h"
#endif //WITH_EDITOR

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
		OnOperationOrderChandedHandle = ActionAsset->GetOnStepsOrderChanged().AddRaw( this, &FDataprepAssetAction::OnActionOperationsOrderChanged );
	}
}

void FDataprepAssetAction::UnbindDataprepAssetFromAction()
{
	if ( ActionAsset && OnOperationOrderChandedHandle.IsValid() )
	{
		ActionAsset->GetOnStepsOrderChanged().Remove( OnOperationOrderChandedHandle );
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

FOnStepsOrderChanged& UDataprepAsset::GetOnActionsOrderChanged()
{
	return OnActionsOrderChanged;
}

FOnActionOperationsOrderChanged& UDataprepAsset::GetOnActionOperationsOrderChanged()
{
	return OnActionOperationsOrderChanged;
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
		if (AssetProducer.Producer && AssetProducer.bIsEnabled)
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
