// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "MassObserverProcessor.h"
#include "MassProcessor.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementProcessorAdaptors.generated.h"

struct FTypedElementDatabaseExtendedQuery;

USTRUCT()
struct FTypedElementQueryProcessorData
{
	GENERATED_BODY()

	FTypedElementQueryProcessorData() = default;
	explicit FTypedElementQueryProcessorData(UMassProcessor& Owner);

	EMassProcessingPhase MapToMassProcessingPhase(ITypedElementDataStorageInterface::EQueryTickPhase Phase) const;
	FString GetProcessorName(FString RootProcessorName) const;

	void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);

	FTypedElementDatabaseExtendedQuery* ParentQuery{ nullptr };
	FMassEntityQuery Query;
};

/**
 * Adapts processor queries callback for MASS.
 */
UCLASS()
class UTypedElementQueryProcessorCallbackAdapterProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UTypedElementQueryProcessorCallbackAdapterProcessor();

	FMassEntityQuery& GetQuery();
	void ConfigureQueryCallback(FTypedElementDatabaseExtendedQuery& Query);

protected:
	void ConfigureQueries() override;
	void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& TargetParentQuery) override;
	
	void PostInitProperties() override;
	FString GetProcessorName() const override;

private:
	UPROPERTY(transient)
	FTypedElementQueryProcessorData Data;
};

/**
 * Adapts observer queries callback for MASS.
 */
UCLASS()
class UTypedElementQueryObserverCallbackAdapterProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UTypedElementQueryObserverCallbackAdapterProcessor();

	FMassEntityQuery& GetQuery();
	const UScriptStruct* GetObservedType() const;
	EMassObservedOperation GetObservedOperation() const;
	void ConfigureQueryCallback(FTypedElementDatabaseExtendedQuery& Query);

protected:
	void ConfigureQueries() override;
	void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& TargetParentQuery) override;

	void PostInitProperties() override;
	void Register() override;
	FString GetProcessorName() const override;

private:
	UPROPERTY(transient)
	FTypedElementQueryProcessorData Data;
};
