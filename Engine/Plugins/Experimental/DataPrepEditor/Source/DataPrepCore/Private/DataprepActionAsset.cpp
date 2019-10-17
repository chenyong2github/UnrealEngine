// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepActionAsset.h"

// Dataprep include
#include "DataPrepOperation.h"
#include "DataprepCoreLogCategory.h"
#include "DataprepCorePrivateUtils.h"
#include "DataprepCoreUtils.h"
#include "IDataprepLogger.h"
#include "IDataprepProgressReporter.h"
#include "SelectionSystem/DataprepFilter.h"

// Engine include
#include "ActorEditorUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "LevelSequence.h"
#include "Materials/MaterialInterface.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectMacros.h"

#ifdef WITH_EDITOR
#include "Editor.h"
#endif //WITH_EDITOR

UDataprepActionAsset::UDataprepActionAsset()
	: ContextPtr( nullptr )
{
#ifdef WITH_EDITOR
	OnAssetDeletedHandle = FEditorDelegates::OnAssetsDeleted.AddUObject( this, &UDataprepActionAsset::OnClassesRemoved );
#endif //WITH_EDITOR

	bExecutionInterrupted = false;

	if(!HasAnyFlags(RF_ClassDefaultObject))
	{
		OperationContext = MakeShareable( new FDataprepOperationContext() );

		OperationContext->Context = MakeShareable( new FDataprepContext() );

		OperationContext->AddAssetDelegate = FDataprepAddAsset::CreateUObject( this, &UDataprepActionAsset::OnAddAsset);
		OperationContext->CreateActorDelegate = FDataprepCreateActor::CreateUObject( this, &UDataprepActionAsset::OnCreateActor);
		OperationContext->RemoveObjectDelegate = FDataprepRemoveObject::CreateUObject( this, &UDataprepActionAsset::OnRemoveObject);
		OperationContext->DeleteObjectsDelegate = FDataprepDeleteObjects::CreateUObject( this, &UDataprepActionAsset::OnDeleteObjects);
	}
}

UDataprepActionAsset::~UDataprepActionAsset()
{
#ifdef WITH_EDITOR
	FEditorDelegates::OnAssetsDeleted.Remove( OnAssetDeletedHandle );
#endif //WITH_EDITOR
}

void UDataprepActionAsset::Execute(const TArray<UObject*>& InObjects)
{
	ContextPtr = MakeShareable( new FDataprepActionContext() );

	for(UObject* Object : InObjects)
	{
		if(Object && FDataprepCoreUtils::IsAsset(Object))
		{
			ContextPtr->Assets.Add(Object);
		}
	}

	// Make a copy of the objects to act on
	OperationContext->Context->Objects = InObjects;

	// Execute steps sequentially
	for ( UDataprepActionStep* Step : Steps )
	{
		if ( Step && Step->bIsEnabled )
		{
			if ( UDataprepOperation* Operation = Step->Operation )
			{
				Operation->Execute( OperationContext->Context->Objects );
				// #ueent_todo: Add steps to process, added, removed and deleted objects
			}
			else if ( UDataprepFilter* Filter = Step->Filter )
			{
				OperationContext->Context->Objects = Filter->FilterObjects( OperationContext->Context->Objects );
			}
		}
	}

	// Reset list of selected objects
	OperationContext->Context->Objects.Reset();

	ContextPtr.Reset();
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

void UDataprepActionAsset::ExecuteAction(const TSharedPtr<FDataprepActionContext>& InActionsContext, UDataprepActionStep* SpecificStep, bool bSpecificStepOnly)
{
	ContextPtr = InActionsContext;
	check(ContextPtr.IsValid());

	TArray<UObject*>& SelectedObjects = OperationContext->Context->Objects;

	// Collect all objects the action to work on
	SelectedObjects.Empty( ContextPtr->Assets.Num() );
	for(TWeakObjectPtr<UObject>& ObjectPtr : ContextPtr->Assets)
	{
		if(UObject* Object = ObjectPtr.Get())
		{
			SelectedObjects.Add( Object );
		}
	}
	TArray< AActor* > ActorsInWorld;
	DataprepCorePrivateUtils::GetActorsFromWorld( ContextPtr->WorldPtr.Get(), ActorsInWorld );
	SelectedObjects.Append( ActorsInWorld );

	OperationContext->DataprepLogger = ContextPtr->LoggerPtr;
	OperationContext->DataprepProgressReporter = ContextPtr->ProgressReporterPtr;

	auto ExecuteOneStep = [this, &SelectedObjects](UDataprepActionStep* Step)
	{
		if ( UDataprepOperation* Operation = Step->Operation )
		{
			// Cache number of assets and objects before execution
			int32 AssetsDiffCount = ContextPtr->Assets.Num();
			int32 ActorsDiffCount = OperationContext->Context->Objects.Num();

			TSharedRef<FDataprepOperationContext> OperationContextPtr = this->OperationContext.ToSharedRef();
			Operation->ExecuteOperation( OperationContextPtr );

			// Check that only UDataprepEditingOperation are changing the content
			if(this->bWorkingSetHasChanged)
			{
				ensure( Operation->IsA<UDataprepEditingOperation>() );
			}

			// Process the changes in the context if applicable
			AssetsDiffCount = ContextPtr->Assets.Num() - AssetsDiffCount;
			ActorsDiffCount = OperationContext->Context->Objects.Num() - ActorsDiffCount - AssetsDiffCount;
			this->ProcessWorkingSetChanged( AssetsDiffCount != 0, ActorsDiffCount != 0 );
		}
		else if ( UDataprepFilter* Filter = Step->Filter )
		{
			SelectedObjects = Filter->FilterObjects( SelectedObjects );
		}
	};

	if(SpecificStep && bSpecificStepOnly == true)
	{
		if ( SpecificStep->bIsEnabled )
		{
			ExecuteOneStep( SpecificStep );
		}
	}
	// Execute steps sequentially up to SpecificStep if applicable
	else
	{
		for ( UDataprepActionStep* Step : Steps )
		{
			if(Step != nullptr)
			{
				bWorkingSetHasChanged = false;

				if ( Step && Step->bIsEnabled )
				{
					ExecuteOneStep( Step );
				}

				if(ContextPtr->ContinueCallback && !ContextPtr->ContinueCallback(this, Step->Operation, Step->Filter))
				{
					break;
				}

				if(Step == SpecificStep)
				{
					break;
				}
			}
		}
	}

	SelectedObjects.Empty();
	ContextPtr.Reset();
}

UObject* UDataprepActionAsset::OnAddAsset(const UObject* Asset, UClass* AssetClass, const TCHAR* AssetName)
{
	UObject* NewAsset = nullptr;

	if(ContextPtr != nullptr)
	{
		UObject* Outer = GetAssetOuterByClass( Asset ? Asset->GetClass() : AssetClass );

		// #ueent_todo: Deal with name collision
		if(Asset == nullptr)
		{
			NewAsset = NewObject<UObject>( Outer, AssetClass, NAME_None, RF_Transient );
		}
		else
		{
			NewAsset = DuplicateObject< UObject >( Asset, Outer, NAME_None );
		}
		check( NewAsset );

		if(AssetName != nullptr)
		{
			FName UniqueName = MakeUniqueObjectName( Outer, NewAsset->GetClass(), AssetName );
			FDataprepCoreUtils::RenameObject( NewAsset, *UniqueName.ToString() );
		}

		// Add new asset to local and global contexts
		ContextPtr->Assets.Add( NewAsset );
		OperationContext->Context->Objects.Add( NewAsset );

		bWorkingSetHasChanged = true;
	}

	return NewAsset;
}

AActor* UDataprepActionAsset::OnCreateActor(UClass* ActorClass, const TCHAR* ActorName)
{
	if(ActorClass != nullptr && ContextPtr != nullptr)
	{
		AActor* Actor = ContextPtr->WorldPtr->SpawnActor<AActor>( ActorClass, FTransform::Identity );

		if(ActorName != nullptr)
		{
			FName UniqueName = MakeUniqueObjectName( Actor->GetOuter(), ActorClass, ActorName );
			FDataprepCoreUtils::RenameObject( Actor, *UniqueName.ToString() );
		}

		// Add new actor to local contexts
		OperationContext->Context->Objects.Add( Actor );

		bWorkingSetHasChanged = true;

		return Actor;
	}

	return nullptr;
}

void UDataprepActionAsset::OnRemoveObject(UObject* Object, bool bLocalContext)
{
	if(Object != nullptr && ContextPtr != nullptr)
	{
		ObjectsToRemove.Emplace(Object, bLocalContext);

		bWorkingSetHasChanged = true;
	}
}

void UDataprepActionAsset::OnDeleteObjects(TArray<UObject*> Objects)
{
	if(ContextPtr != nullptr)
	{
		for(UObject* Object : Objects)
		{
			// Mark object for deletion
			if(Object != nullptr)
			{
				ObjectsToDelete.Add(Object);
				bWorkingSetHasChanged = true;
			}
		}
	}
}

void UDataprepActionAsset::ProcessWorkingSetChanged( bool bAddedAssets, bool bAddedActors )
{
	if(bWorkingSetHasChanged && ContextPtr != nullptr)
	{
		bool bAssetsChanged = bAddedAssets;
		bool bWorldChanged = bAddedActors;

		TSet<UObject*> SelectedObjectSet( OperationContext->Context->Objects );

		if(ObjectsToRemove.Num() > 0)
		{
			for(TPair<UObject*, bool>& Pair : ObjectsToRemove)
			{
				if(SelectedObjectSet.Contains(Pair.Key))
				{
					SelectedObjectSet.Remove( Pair.Key );

					// Remove object from Dataprep's context
					if(!Pair.Value)
					{
						if(AActor* Actor = Cast<AActor>(Pair.Key))
						{
							ContextPtr->WorldPtr->RemoveActor( Actor, false );
							bWorldChanged = true;
						}
						else if( FDataprepCoreUtils::IsAsset(Pair.Key) )
						{
							bAssetsChanged = true;
							ContextPtr->Assets.Remove( Pair.Key );
						}
					}
				}
				else
				{
					// #ueent_todo: Warn object external to Dataprep's context has been removed
				}
			}

			ObjectsToRemove.Empty( ObjectsToRemove.Num() );
		}

		if(ObjectsToDelete.Num() > 0)
		{
			// Remove all objects to be deleted from action's and Dataprep's context
			for(UObject* Object : ObjectsToDelete)
			{
				if (AActor* Actor = Cast<AActor>(Object))
				{
					if (UWorld* World = Actor->GetWorld())
					{
						World->EditorDestroyActor( Actor, false );
					}
				}

				FDataprepCoreUtils::MoveToTransientPackage( Object );
				if(SelectedObjectSet.Contains(Object))
				{
					SelectedObjectSet.Remove( Object );

					// If object is an asset, remove from array of assets. 
					if(FDataprepCoreUtils::IsAsset(Object))
					{
						bAssetsChanged = true;
						ContextPtr->Assets.Remove( Object );
					}
					else
					{
						bWorldChanged = true;
					}
				}
				else
				{
					// #ueent_todo: Warn object external to Dataprep's context has been deleted
				}
			}

			FDataprepCoreUtils::PurgeObjects( MoveTemp(ObjectsToDelete) );
		}

		// Update action's context
		OperationContext->Context->Objects = SelectedObjectSet.Array();

		if( ContextPtr->ContextChangedCallback && (bAssetsChanged || bWorldChanged))
		{
			ContextPtr->ContextChangedCallback( this, bWorldChanged, bAssetsChanged, ContextPtr->Assets.Array() );
		}
	}

	bWorkingSetHasChanged = false;
}

UObject * UDataprepActionAsset::GetAssetOuterByClass(UClass* AssetClass)
{
	if(AssetClass == nullptr)
	{
		return nullptr;
	}

	UPackage* Package = nullptr;

	if( AssetClass->IsChildOf( UStaticMesh::StaticClass() ) )
	{
		Package = PackageForStaticMesh.Get();
		if(Package == nullptr)
		{
			PackageForStaticMesh = TWeakObjectPtr< UPackage >( NewObject< UPackage >( nullptr, *FPaths::Combine( ContextPtr->TransientContentFolder, TEXT("Geometries") ), RF_Transient ) );
			PackageForStaticMesh->FullyLoad();
			Package = PackageForStaticMesh.Get();
		}
	}
	else if( AssetClass->IsChildOf( UMaterialInterface::StaticClass() ) )
	{
		Package = PackageForMaterial.Get();
		if(Package == nullptr)
		{
			PackageForMaterial = TWeakObjectPtr< UPackage >( NewObject< UPackage >( nullptr, *FPaths::Combine( ContextPtr->TransientContentFolder, TEXT("Materials") ), RF_Transient ) );
			PackageForMaterial->FullyLoad();
			Package = PackageForMaterial.Get();
		}
	}
	else if( AssetClass->IsChildOf( UTexture::StaticClass() ) )
	{
		Package = PackageForTexture.Get();
		if(Package == nullptr)
		{
			PackageForTexture = TWeakObjectPtr< UPackage >( NewObject< UPackage >( nullptr, *FPaths::Combine( ContextPtr->TransientContentFolder, TEXT("Textures") ), RF_Transient ) );
			PackageForTexture->FullyLoad();
			Package = PackageForTexture.Get();
		}
	}
	else if( AssetClass->IsChildOf( ULevelSequence::StaticClass() ) )
	{
		Package = PackageForAnimation.Get();
		if(Package == nullptr)
		{
			PackageForAnimation = TWeakObjectPtr< UPackage >( NewObject< UPackage >( nullptr, *FPaths::Combine( ContextPtr->TransientContentFolder, TEXT("Animations") ), RF_Transient ) );
			PackageForAnimation->FullyLoad();
			Package = PackageForAnimation.Get();
		}
	}

	return Package;
}
