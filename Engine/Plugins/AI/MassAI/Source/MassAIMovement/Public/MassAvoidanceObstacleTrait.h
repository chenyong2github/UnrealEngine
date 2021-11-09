// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassEntityTraitBase.h"
#include "MassAvoidanceObstacleTrait.generated.h"

UCLASS(meta = (DisplayName = "Dynamic Obstacle"))
class MASSAIMOVEMENT_API UMassAvoidanceObstacleTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;

	UPROPERTY(EditAnywhere, Category = Mass)
	bool bExtendToEdgeObstacle = false;
};