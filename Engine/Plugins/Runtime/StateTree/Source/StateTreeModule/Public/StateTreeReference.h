// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTypes.h"
#include "StateTreeReference.generated.h"

class UStateTree;

/**
 * Struct to hold reference to a StateTree asset along with values to parameterized it.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeReference
{
	GENERATED_BODY()

	FStateTreeReference();
	~FStateTreeReference();
	
	UPROPERTY(EditAnywhere, Category = "")
	TObjectPtr<UStateTree> StateTree = nullptr;

	UPROPERTY(EditAnywhere, Category = "", meta = (FixedLayout))
	FInstancedPropertyBag Parameters;

#if WITH_EDITOR
	void PostSerialize(const FArchive& Ar);

	/**
	 * Enforce self parameters to be compatible with those exposed by the selected StateTree asset.
	 */
	void SyncParameters() { SyncParameters(Parameters); }

	/**
	 * Sync provided parameters to be compatible with those exposed by the selected StateTree asset.
	 */
	void SyncParameters(FInstancedPropertyBag& ParametersToSync) const;

	/**
	 * Indicates if current parameters are compatible with those available in the selected StateTree asset.
	 * @return true when parameters requires to be synced to be compatible with those available in the selected StateTree asset, false otherwise.
	 */
	bool RequiresParametersSync() const;

private:
	FDelegateHandle PIEHandle;
#endif
};

#if WITH_EDITORONLY_DATA
template<>
struct TStructOpsTypeTraits<FStateTreeReference> : public TStructOpsTypeTraitsBase2<FStateTreeReference>
{
	enum
	{
		WithPostSerialize = true,
	};
};
#endif
