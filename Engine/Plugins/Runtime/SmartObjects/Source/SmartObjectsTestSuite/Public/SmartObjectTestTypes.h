// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmartObjectDefinition.h"
#include "SmartObjectTypes.h"
#include "SmartObjectCollection.h"
#include "SmartObjectSubsystem.h"
#include "SmartObjectTestTypes.generated.h"

/**
 * Concrete definition class for testing purposes
 */
UCLASS(HideDropdown)
class SMARTOBJECTSTESTSUITE_API USmartObjectTestBehaviorDefinition : public USmartObjectBehaviorDefinition
{
	GENERATED_BODY()
};

/**
 * Test-time SmartObjectSubsystem override, aimed at encapsulating test-time smart object instances and functionality
 */
UCLASS(HideDropdown)
class USmartObjectTestSubsystem : public USmartObjectSubsystem
{
	GENERATED_BODY()

public:
	void RebuildAndInitializeForTesting(const TSharedPtr<FMassEntityManager>& InEntityManager);
	FMassEntityManager* GetEntityManagerForTesting();

protected:
#if WITH_EDITOR
	virtual void SpawnMissingCollection() override;
#endif // WITH_EDITOR
	virtual bool ShouldCreateSubsystem(UObject* Outer) const { return false; }
	virtual void CleanupRuntime() override;
};

/**
 * Test-time ASmartObjectCollection override, aimed at encapsulating test-time smart object instances and functionality
 */
UCLASS(HideDropdown)
class ASmartObjectTestCollection : public ASmartObjectCollection
{
	GENERATED_BODY()

public:
	ASmartObjectTestCollection();

	virtual bool RegisterWithSubsystem(const FString& Context) override;
	virtual bool UnregisterWithSubsystem(const FString& Context) override;
};

/**
 * Some user data to assign to a slot definition
 */
USTRUCT(meta=(Hidden))
struct FSmartObjectSlotTestDefinitionData: public FSmartObjectSlotDefinitionData
{
	GENERATED_BODY()

	float SomeSharedFloat= 0.f;
};

/**
 * Some user runtime data to assign to a slot instance
 */
USTRUCT(meta=(Hidden))
struct FSmartObjectSlotTestRuntimeData : public FSmartObjectSlotStateData
{
	GENERATED_BODY()

	float SomePerInstanceSharedFloat = 0.0f;
};