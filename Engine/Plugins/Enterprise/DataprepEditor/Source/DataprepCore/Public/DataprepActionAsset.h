// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Delegates/DelegateCombinations.h"
#include "Engine/World.h"
#include "Delegates/IDelegateInstance.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "DataprepActionAsset.generated.h"

// Forward Declarations
class AActor;
class IDataprepLogger;
class IDataprepProgressReporter;
class UDataprepActionAsset;
class UDataprepFetcher;
class UDataprepFilter;
class UDataprepOperation;

struct FDataprepOperationContext;

template <class T>
class TSubclassOf;

namespace DataprepActionAsset
{
	/**
	 * Callback function used to confirm continuation after executing an operation or a filter
	 * @param ActionAsset			The action asset checking for continuation 
	 * @param OperationExecuted		Executed operation if not null 
	 * @param FilterExecuted		Executed filter if not null
	 */
	typedef TFunction<bool(UDataprepActionAsset* /* ActionAsset */, UDataprepOperation* /* OperationExecuted */, UDataprepFilter* /* FilterExecuted */)> FCanExecuteNextStepFunc;

	/**
	 * Callback used to report a global change to the content it is working on
	 * @param ActionAsset		The action asset reporting the change 
	 * @param bWorldChanged		Indicates changes happened in the world
	 * @param bAssetChanged		Indicates the set of assets has changed
	 * @param NewAssets			New set of assets. Only valid if bAssetChanged is true
	 */
	typedef TFunction<void(const UDataprepActionAsset* /* ActionAsset */, bool /* bWorldChanged */, bool /* bAssetChanged */, const TArray< TWeakObjectPtr<UObject> >& /* NewAssets */)> FActionsContextChangedFunc;
}

UCLASS(Experimental)
class UDataprepActionStep : public UObject
{
	GENERATED_BODY()

public:
	// The operation will only be not null if the step is a operation
	UPROPERTY()
	UDataprepOperation* Operation;

	// The Filter will only be not null if the step is a Filter/Selector
	UPROPERTY()
	UDataprepFilter* Filter;

	UPROPERTY()
	bool bIsEnabled;
};

/** Structure to pass execution context to action */
struct FDataprepActionContext
{
	FDataprepActionContext() {}

	FDataprepActionContext& SetWorld( UWorld* InWorld )
	{ 
		WorldPtr = TWeakObjectPtr<UWorld>(InWorld);
		return *this;
	}

	FDataprepActionContext& SetAssets( TArray< TWeakObjectPtr< UObject > >& InAssets )
	{
		Assets.Empty( InAssets.Num() );
		Assets.Append( InAssets );
		return *this;
	}

	FDataprepActionContext& SetProgressReporter( const TSharedPtr< IDataprepProgressReporter >& InProgressReporter )
	{
		ProgressReporterPtr = InProgressReporter;
		return *this;
	}

	FDataprepActionContext& SetLogger( const TSharedPtr< IDataprepLogger >& InLogger )
	{
		LoggerPtr = InLogger;
		return *this;
	}

	FDataprepActionContext& SetTransientContentFolder( const FString& InTransientContentFolder )
	{
		TransientContentFolder = InTransientContentFolder;
		return *this;
	}

	FDataprepActionContext& SetCanExecuteNextStep( DataprepActionAsset::FCanExecuteNextStepFunc InCanExecuteNextStepFunc )
	{
		ContinueCallback = InCanExecuteNextStepFunc;
		return *this;
	}

	FDataprepActionContext& SetActionsContextChanged( DataprepActionAsset::FActionsContextChangedFunc InActionsContextChangedFunc )
	{
		ContextChangedCallback = InActionsContextChangedFunc;
		return *this;
	}

	/** Hold onto the world the consumer will process */
	TWeakObjectPtr< UWorld > WorldPtr;

	/** Set of assets the consumer will process */
	TSet< TWeakObjectPtr< UObject > > Assets;

	/** Path to transient content folder where were created */
	FString TransientContentFolder;

	/** Hold onto the reporter that the consumer should use to report progress */
	TSharedPtr< IDataprepProgressReporter > ProgressReporterPtr;

	/** Hold onto the logger that the consumer should use to log messages */
	TSharedPtr<  IDataprepLogger > LoggerPtr;

	/** Delegate called by an action after the execution of each step */
	DataprepActionAsset::FCanExecuteNextStepFunc ContinueCallback;

	/** Delegate called by an action if the working content has changed after the execution of an operation */
	DataprepActionAsset::FActionsContextChangedFunc ContextChangedCallback;
};

// Delegates
DECLARE_MULTICAST_DELEGATE(FOnStepsOrderChanged)

UCLASS(Experimental)
class DATAPREPCORE_API UDataprepActionAsset : public UObject
{
	GENERATED_BODY()

public:

	UDataprepActionAsset();

	virtual ~UDataprepActionAsset();

	/**
	 * Execute the action on a specific set of objects
	 * @param Objects The objects on which the action will operate
	 */
	UFUNCTION(BlueprintCallable, Category = "Execution")
	void Execute(const TArray<UObject*>& InObjects);

	/**
	 * Execute the action
	 * @param InActionsContext	Shared context which the action's steps will be executed on 
	 * @param SpecificStep		Specific step to execute within the action
	 * @param bOnly				If true (default), only the specific step is executed,
	 *							otherwise the action is executed up to the specific step
	 */
	void ExecuteAction(const TSharedPtr<FDataprepActionContext>& InActionsContext, UDataprepActionStep* SpecificStep = nullptr, bool bSpecificStepOnly = true);

	/**
	 * Add an operation to the action
	 * @param OperationClass The class of the operation
	 * @return The index of the added operation or index none if the class is 
	 */
	int32 AddOperation(const TSubclassOf<UDataprepOperation>& OperationClass);

	/**
	 * Add a filter and setup it's fetcher
	 * @param FilterClass The type of filter we want
	 * @param FetcherClass The type of fetcher that we want. 
	 * @return The index of the added filter or index none if the classes are incompatible or invalid
	 * Note that fetcher most be compatible with the filter
	 */
	int32 AddFilterWithAFetcher(const TSubclassOf<UDataprepFilter>& FilterClass, const TSubclassOf<UDataprepFetcher>& FetcherClass);

	/**
	 * Add a copy of the step to the action
	 * @param ActionStep The step we want to duplicate in the action
	 * @return The index of the added step or index none if the step is invalid
	 */
	int32 AddStep(const UDataprepActionStep* ActionStep);

	/**
	 * Access to a step of the action
	 * @param Index the index of the desired step
	 * @return A pointer to the step if it exist, otherwise nullptr
	 */
	TWeakObjectPtr<UDataprepActionStep> GetStep(int32 Index);

	/**
	 * Access to a step of the action
	 * @param Index the index of the desired step
	 * @return A const pointer to the operation if it exist, otherwise nullptr
	 */
	const TWeakObjectPtr<UDataprepActionStep> GetStep(int32 Index) const;

	/**
	 * Get the number of steps of this action 
	 * @return The number of steps
	 */
	int32 GetStepsCount() const;

	/**
	 * Get enabled status of an operation
	 * @param Index The index of the operation
	 * @return True if the operation is enabled. Always return false if the operation index is invalid
	 */
	bool IsStepEnabled(int32 Index) const;

	/**
	 * Allow to set the enabled state of a step
	 * @param Index The index of the step
	 * @param bEnable The new enabled state of the step
	 */
	void EnableStep(int32 Index, bool bEnable);

	/**
	 * Move a step to another spot in the order of steps
	 * This operation take O(n) time. Where n is the absolute value of StepIndex - DestinationIndex
	 * @param StepIndex The Index of the step to move
	 * @param DestinationIndex The index of where the step will be move to
	 * @return True if the step was move
	 */
	bool MoveStep(int32 StepIndex, int32 DestinationIndex);

	/**
	 * Remove a step from the action
	 * @param Index The index of the step to remove
	 * @return True if a step was removed
	 */
	bool RemoveStep(int32 Index);

	/**
	 * Allow an observer to be notified when the steps order changed that also include adding and removing steps
	 * @return The delegate that will be broadcasted when the steps order changed
	 */
	FOnStepsOrderChanged& GetOnStepsOrderChanged();

	UPROPERTY(Transient)
	bool bExecutionInterrupted;

	/** Getter and Setter on the UI text of the action */
	const TCHAR* GetLabel() { return *Label; }
	void SetLabel( const TCHAR* InLabel ) { Label = InLabel ? InLabel : TEXT(""); }

	/**
	 * Do the necessary notification so that the dataprep system can react properly to removal of this action
	 */
	void NotifyDataprepSystemsOfRemoval();

private:

	void OnClassesRemoved(const TArray<UClass*>& DeletedClasses);

	void RemoveInvalidOperations();

	/**
	 * Duplicate and add an asset to the Dataprep's and action's working set
	 * @param Asset			If not null, the asset will be duplicated
	 * @param AssetName		Name of the asset to create. Name collision will be performed before naming the asset
	 * @returns				The asset newly created
	 */
	UObject* OnAddAsset(const UObject* Asset, const TCHAR* AssetName);

	/**
	 * Create and add an asset to the Dataprep's and action's working set
	 * @param AssetClass	If Asset is null, an asset of the given class will be returned
	 * @param AssetName		Name of the asset to create. Name collision will be performed before naming the asset
	 * @returns				The asset newly created
	 */
	UObject* OnCreateAsset(UClass* AssetClass, const TCHAR* AssetName);

	/**
	 * Add an actor to the Dataprep's transient world and action's working set
	 * @param ActorClass	Class of the actor to create
	 * @param ActorName		Name of the actor to create. Name collision will be performed before naming the asset
	 * @returns				The actor newly created
	 */
	AActor* OnCreateActor(UClass* ActorClass, const TCHAR* ActorName);

	/**
	 * Remove an object from the Dataprep's and/or action's working set
	 * @param Object			Object to be removed from the working set 
	 * @param bLocalContext		If set to true, the object is removed from the current working set.
	 *							The object will not be accessible to any subsequent operation using the current context.
	 *							If set to false, the object is removed from the Dataprep's working set.
	 *							The object will not be accessible to any subsequent operation in the Dataprep's pipeline.
	 */
	void OnRemoveObject(UObject* Object, bool bLocalContext);

	/**
	 * Add an array of assets to the list of modified assets
	 * @param Assets		An array of assets which have been modifed
	 */
	void OnAssetsModified(TArray<UObject*> Assets);

	/**
	 * Delete an array of objects from the Dataprep's and action's working set
	 * @param Objects		The array of objects to delete
	 * @remark	The deletion of the object is deferred. However, if the object is not an asset, it is removed from
	 *			the Dataprep's transient world. If the object is an asset, it is moved to the transient package, no
	 *			action is taken to clean up any object referencing this asset.
	 * @remark	After execution, the object is not accessible by any subsequent operation in the Dataprep's pipeline.
	 */
	void OnDeleteObjects( TArray<UObject*> Objects);

	/**
	 * Executes the deletion and removal requested by an operation after its execution
	 * Notifies any observer about what has changed
	 */
	void ProcessWorkingSetChanged();

	/** Returns the outer to be used according to an asset's class */
	UObject* GetAssetOuterByClass( UClass* AssetClass );

	/** Add an asset to the execution context */
	void AddAssetToContext( UObject* NewAsset, const TCHAR* DesiredName );

	/** Array of operations and/or filters constituting this action */
	UPROPERTY()
	TArray<UDataprepActionStep*> Steps;

	/** Broadcasts any change to the stack of steps */
	FOnStepsOrderChanged OnStepsChanged;

	FDelegateHandle OnAssetDeletedHandle;

	/** Pointer to the context passed to the action for its execution */
	TSharedPtr<FDataprepActionContext> ContextPtr;

	/** Context passed to the operation for its execution */
	TSharedPtr<FDataprepOperationContext> OperationContext;

	/** Array of objects requested to be deleted by an operation */
	TArray<UObject*> ObjectsToDelete;

	/** Set of objects which have been modified during the execution of an operation */
	TSet< TWeakObjectPtr< UObject > > ModifiedAssets;

	/** Array of objects which have been added during the execution of an operation */
	TArray<UObject*> AddedObjects;

	/** Array of objects requested to be removed by an operation */
	TArray<TPair<UObject*,bool>> ObjectsToRemove;

	/** Marker to check if an operation has made any changes to the action's working set */
	bool bWorkingSetHasChanged;

	/** UI label of the action */
	FString Label;

	/** Package which static meshes will be added to */
	TWeakObjectPtr< UPackage > PackageForStaticMesh;

	/** Package which textures will be added to */
	TWeakObjectPtr< UPackage > PackageForTexture;

	/** Package which materials will be added to */
	TWeakObjectPtr< UPackage > PackageForMaterial;

	/** Package which level sequences will be added to */
	TWeakObjectPtr< UPackage > PackageForAnimation;
};
