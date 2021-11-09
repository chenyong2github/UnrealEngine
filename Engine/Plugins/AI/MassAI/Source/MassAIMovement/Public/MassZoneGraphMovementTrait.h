// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassEntityTraitBase.h"
#include "MassMovementSubsystem.h"
#include "MassAIMovementTypes.h"
#include "MassZoneGraphMovementTrait.generated.h"

UCLASS(meta = (DisplayName = "ZoneGraph Movement"))
class MASSAIMOVEMENT_API UMassZoneGraphMovementTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;

	UPROPERTY(Category="Movement", EditAnywhere)
	FMassMovementConfigRef Config;
};
