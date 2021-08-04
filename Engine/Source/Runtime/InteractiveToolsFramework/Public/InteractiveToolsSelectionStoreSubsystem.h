// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InteractiveToolStorableSelection.h"
#include "Subsystems/EngineSubsystem.h"

#include "InteractiveToolsSelectionStoreSubsystem.generated.h"

class FSubsystemCollectionBase;

/**
 * Stores an arbitrary storable selection object so that it can be accessed across
 * tools, modes, and potentially asset editors.
 * When possible, this subsystem should be accessed through a relevant api class (for instance, through
 * a tool manager), so that if the implementation changes, changes will be constrained to the api class.
 *
 * Note that because engine subsystems get initialized on module load, InteractiveToolsFramework
 * must be loaded for the subsystem to work.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveToolsSelectionStoreSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:

	// These classes are currently not used- they are a space to add parameters if we decide to change
	// the operation of this subsystem. Don't use them.
	struct FStoreParams
	{};
	struct FRetrieveParams
	{};
	struct FClearParams
	{};

	virtual void Initialize(FSubsystemCollectionBase& Collection) override
	{
		// We support being called Modify() on and being part of undo transactions.
		SetFlags(RF_Transactional);
	}

	virtual void Deinitialize() override 
	{
		ClearStoredSelection();
	}

	/**
	 * Sets the current selection object.
	 *
	 * @param StorableSelection Pointer to hold on to.
	 * @param Params Currently not used, kept to make future changes easier.
	 */
	void SetStoredSelection(const UInteractiveToolStorableSelection* StorableSelection, 
		const FStoreParams& Params = FStoreParams())
	{
		StoredSelection = StorableSelection;
	}

	/**
	 * Removes the hold on the current selection object.
	 *
	 * @param Params Currently not used, kept to make future changes easier.
	 */
	void ClearStoredSelection(const FClearParams& Params = FClearParams())
	{
		StoredSelection = nullptr;
	}

	/**
	 * Retrieves a pointer to the currently stored selection object.
	 *
	 * @param Params Currently not used, kept to make future changes easier.
	 */
	const UInteractiveToolStorableSelection* GetStoredSelection(const FRetrieveParams& Params = FRetrieveParams()) const
	{
		return StoredSelection;
	}

private:
	UPROPERTY()
	TObjectPtr<const UInteractiveToolStorableSelection> StoredSelection = nullptr;
};