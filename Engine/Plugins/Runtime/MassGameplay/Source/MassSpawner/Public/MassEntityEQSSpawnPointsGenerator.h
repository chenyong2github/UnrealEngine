// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "MassEntitySpawnPointsGeneratorBase.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "MassEntityEQSSpawnPointsGenerator.generated.h"

/**
 * Describes the SpawnPoints Generator when we want to leverage the points given by an EQS Query
 */
UCLASS(BlueprintType, meta=(DisplayName="EQS SpawnPoints Generator"))
class MASSSPAWNER_API UMassEntityEQSSpawnPointsGenerator : public UMassEntitySpawnPointsGeneratorBase
{
	GENERATED_BODY()

public:
	virtual void GenerateSpawnPoints(UObject& QueryOwner, int32 Count, FFinishedGeneratingSpawnPointsSignature& FinishedGeneratingSpawnPointsDelegate) const override;

protected:
	void OnEQSQueryFinished(TSharedPtr<FEnvQueryResult> Result, int32 Count, FFinishedGeneratingSpawnPointsSignature FinishedGeneratingSpawnPointsDelegate) const;

	UPROPERTY(Category = "Query", EditAnywhere)
	FEQSParametrizedQueryExecutionRequest EQSRequest;
};
