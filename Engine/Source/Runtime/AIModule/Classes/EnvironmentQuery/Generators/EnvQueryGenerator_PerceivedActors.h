// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataProviders/AIDataProvider.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "Perception/AISense.h"
#include "EnvQueryGenerator_PerceivedActors.generated.h"


/** Gathers actors perceived by context */
UCLASS(meta = (DisplayName = "Perceived Actors"))
class AIMODULE_API UEnvQueryGenerator_PerceivedActors : public UEnvQueryGenerator
{
	GENERATED_UCLASS_BODY()

protected:
	/** If set will be used to filter results */
	UPROPERTY(EditDefaultsOnly, Category=Generator)
	TSubclassOf<AActor> AllowedActorClass;

	/** Additional distance limit imposed on the items generated. Perception's range limit still applies. */
	UPROPERTY(EditDefaultsOnly, Category=Generator)
	FAIDataProviderFloatValue SearchRadius;

	/** The perception listener to use as a source of information */
	UPROPERTY(EditAnywhere, Category=Generator)
	TSubclassOf<UEnvQueryContext> ListenerContext;

	/** If set will be used to filter gathered results so that only actors perceived with a given sense are considered */
	UPROPERTY(EditAnywhere, Category = Generator)
	TSubclassOf<UAISense> SenseToUse;

	virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;

	virtual FText GetDescriptionTitle() const override;
	virtual FText GetDescriptionDetails() const override;
};
