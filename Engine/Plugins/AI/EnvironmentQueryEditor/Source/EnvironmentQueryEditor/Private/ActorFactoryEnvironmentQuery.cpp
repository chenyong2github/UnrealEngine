// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorFactoryEnvironmentQuery.h"

#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EQSTestingPawn.h"

#define LOCTEXT_NAMESPACE "ActorFactoryEnvironmentQuery"

UActorFactoryEnvironmentQuery::UActorFactoryEnvironmentQuery()
{
	DisplayName = LOCTEXT("EnvironmentQueryActorDisplayName", "Environment Query");
	NewActorClass = AEQSTestingPawn::StaticClass();
}

void UActorFactoryEnvironmentQuery::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	UEnvQuery* EnvQuery = CastChecked<UEnvQuery>(Asset);
	AEQSTestingPawn* NewEQSTestingPawn = CastChecked<AEQSTestingPawn>(NewActor);

	NewEQSTestingPawn->QueryTemplate = EnvQuery;
	NewEQSTestingPawn->QueryTemplate->CollectQueryParams(*NewEQSTestingPawn, NewEQSTestingPawn->QueryConfig);
	NewEQSTestingPawn->RunEQSQuery();
}

bool UActorFactoryEnvironmentQuery::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (!AssetData.IsValid() || !AssetData.GetClass()->IsChildOf(UEnvQuery::StaticClass()))
	{
		OutErrorMsg = LOCTEXT("NoEnvironmentQuery", "A valid environment query must be specified.");
		return false;
	}

	return true;
}

UObject* UActorFactoryEnvironmentQuery::GetAssetFromActorInstance(AActor* ActorInstance)
{
	return CastChecked<AEQSTestingPawn>(ActorInstance)->QueryTemplate;
}

#undef LOCTEXT_NAMESPACE