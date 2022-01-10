// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassObserverProcessor.h"
#include "MassMoveTargetFragmentInitializer.generated.h"


UCLASS()
class MASSAIMOVEMENT_API UMassMoveTargetFragmentInitializer : public UMassFragmentInitializer
{
	GENERATED_BODY()
public:
	UMassMoveTargetFragmentInitializer();
	
protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery InitializerQuery;
};
