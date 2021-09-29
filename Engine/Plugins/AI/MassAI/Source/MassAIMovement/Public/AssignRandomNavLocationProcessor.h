// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "AI/Navigation/NavigationTypes.h"
#include "AssignRandomNavLocationProcessor.generated.h"


class UNavigationSystemV1;

UCLASS()
class MASSAIMOVEMENT_API UAssignRandomNavLocationProcessor : public UPipeProcessor
{
	GENERATED_BODY()

public:
	UAssignRandomNavLocationProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;
	virtual void Initialize(UObject& InOwner) override;

	UPROPERTY()
	UNavigationSystemV1* NavigationSystem = nullptr;

	UPROPERTY(EditAnywhere, Category="Processor")
	float Radius = 5000.f;

	FVector Origin = FNavigationSystem::InvalidLocation;

	FLWComponentQuery EntityQuery;
};