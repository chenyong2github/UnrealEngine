// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityEQSSpawnPointsGenerator.h"
#include "MassSpawnerTypes.h"
#include "VisualLogger/VisualLogger.h"


UMassEntityEQSSpawnPointsGenerator::UMassEntityEQSSpawnPointsGenerator()
{
	EQSRequest.RunMode = EEnvQueryRunMode::AllMatching; 
}

void UMassEntityEQSSpawnPointsGenerator::GenerateSpawnPoints(UObject& QueryOwner, int32 Count, FFinishedGeneratingSpawnPointsSignature& FinishedGeneratingSpawnPointsDelegate) const
{
	// Need to copy the request as it is called inside a CDO and CDO states cannot be changed.
	FEQSParametrizedQueryExecutionRequest EQSRequestInstanced = EQSRequest;
	if (EQSRequestInstanced.IsValid() == false)
	{
		EQSRequestInstanced.InitForOwnerAndBlackboard(QueryOwner, /*BBAsset=*/nullptr);
		if (!ensureMsgf(EQSRequestInstanced.IsValid(), TEXT("Query request initialization can fail due to missing parameters. See the runtime log for details")))
		{
			return;
		}
	}

	FQueryFinishedSignature Delegate = FQueryFinishedSignature::CreateUObject(this, &UMassEntityEQSSpawnPointsGenerator::OnEQSQueryFinished, Count, FinishedGeneratingSpawnPointsDelegate);
	EQSRequestInstanced.Execute(QueryOwner, /*BlackboardComponent=*/nullptr, Delegate);
}

void UMassEntityEQSSpawnPointsGenerator::OnEQSQueryFinished(TSharedPtr<FEnvQueryResult> Result, int32 Count, FFinishedGeneratingSpawnPointsSignature FinishedGeneratingSpawnPointsDelegate) const
{
	TArray<FVector> Locations;

	if (Result.IsValid() == false || Result->IsSuccessful() == false)
	{
		UE_VLOG_UELOG(this, LogMassSpawner, Error, TEXT("EQS query failed or result is invalid"));
	}
	else
	{
		Result->GetAllAsLocations(Locations);
	}

	// Randomize them
	FRandomStream RandomStream(GFrameNumber);
	for (int32 I = 0; I < Locations.Num(); ++I)
	{
		const int32 J = RandomStream.RandHelper(Locations.Num());
		Locations.Swap(I, J);
	}

	// If we generated too many, shrink it.
	if (Locations.Num() > Count)
	{
		Locations.SetNum(Count);
	}

	FinishedGeneratingSpawnPointsDelegate.Execute(Locations);
}