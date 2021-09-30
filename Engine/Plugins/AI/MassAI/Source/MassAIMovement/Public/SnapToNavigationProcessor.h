// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "SnapToNavigationProcessor.generated.h"

class ANavigationData;

UCLASS()
class MASSAIMOVEMENT_API USnapToNavigationProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	USnapToNavigationProcessor();

protected:	
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;
	virtual void Initialize(UObject& InOwner) override;

	TWeakObjectPtr<ANavigationData> WeakNavData;

	FMassEntityQuery EntityQuery;
};
