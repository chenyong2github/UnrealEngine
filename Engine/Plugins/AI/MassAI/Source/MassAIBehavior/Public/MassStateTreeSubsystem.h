// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassStateTreeSubsystem.generated.h"

class UStateTree;

/**
* A subsystem managing StateTree assets in Mass
*/
UCLASS()
class MASSAIBEHAVIOR_API UMassStateTreeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:

	// USubsystem BEGIN
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// USubsystem END

	/** Registers a StateTree asset to be used */
	FMassStateTreeHandle RegisterStateTreeAsset(UStateTree* StateTree);

	/** @return StateTree asset based on a handle */
	UStateTree* GetRegisteredStateTreeAsset(FMassStateTreeHandle Handle) { return RegisteredStateTrees[Handle.GetIndex()]; }

	/** @return Array of registered StateTree assets */
	TConstArrayView<UStateTree*> GetRegisteredStateTreeAssets() const { return RegisteredStateTrees; }
	
protected:

	/** Array of registered (in use) StateTrees */
	UPROPERTY(Transient)
	TArray<UStateTree*> RegisteredStateTrees;
};
