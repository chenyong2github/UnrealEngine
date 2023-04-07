// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "MassExecutionContext.h"
#include "MassObserverProcessor.h"
#include "MassProcessor.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementProcessorAdaptors.generated.h"

struct FTypedElementDatabaseExtendedQuery;

struct FPhasePreOrPostAmbleExecutor
{
	FPhasePreOrPostAmbleExecutor(FMassEntityManager& EntityManager, float DeltaTime);
	~FPhasePreOrPostAmbleExecutor();

	void ExecuteQuery(ITypedElementDataStorageInterface::FQueryDescription& Description, FMassEntityQuery& NativeQuery,
		ITypedElementDataStorageInterface::QueryCallbackRef Callback);

	FMassExecutionContext Context;
};

USTRUCT()
struct FTypedElementQueryProcessorData
{
	GENERATED_BODY()

	FTypedElementQueryProcessorData() = default;
	explicit FTypedElementQueryProcessorData(UMassProcessor& Owner);

	static EMassProcessingPhase MapToMassProcessingPhase(ITypedElementDataStorageInterface::EQueryTickPhase Phase);
	FString GetProcessorName() const;

	static ITypedElementDataStorageInterface::FQueryResult Execute(
		ITypedElementDataStorageInterface::DirectQueryCallbackRef& Callback,
		ITypedElementDataStorageInterface::FQueryDescription& Description, 
		FMassEntityQuery& NativeQuery, 
		FMassEntityManager& EntityManager);
	void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);

	static bool PrepareCachedDependenciesOnQuery(
		ITypedElementDataStorageInterface::FQueryDescription& Description, FMassExecutionContext& Context);

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
