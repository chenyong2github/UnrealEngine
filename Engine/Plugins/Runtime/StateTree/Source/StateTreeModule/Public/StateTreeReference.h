// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "StateTreeTypes.h"
#include "StateTreeReference.generated.h"

class UStateTree;

/**
 * Struct to hold reference to a StateTree asset along with values to parameterized it.
 */
USTRUCT()
struct FStateTreeReference
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="")
	TObjectPtr<UStateTree> StateTree = nullptr;

	UPROPERTY(EditDefaultsOnly, Category="", meta=(FixedLayout))
	FInstancedPropertyBag Parameters;

#if WITH_EDITOR
	/**
	 * Make sure that parameters are still compatible with those available in the selected StateTree asset.
	 * @return true when parameters were 'fixed' to be in sync, false if they were already synced (i.e. unchanged).
	 */
	bool SyncParameters();
#endif
};


/**
 * Utility class wrapping a StateTreeReference to handle Editor actions to keep parameters synced.
 */
UCLASS(EditInlineNew)
class STATETREEMODULE_API UStateTreeReferenceWrapper : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category="")
	FStateTreeReference StateTreeReference;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	
	FDelegateHandle PostCompileHandle;
	FDelegateHandle PIEHandle;
#endif
};
