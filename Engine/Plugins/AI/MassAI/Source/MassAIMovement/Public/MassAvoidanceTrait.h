// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassEntityTraitBase.h"
#include "MassAvoidanceTrait.generated.h"

UCLASS(meta = (DisplayName = "Avoidance"))
class MASSAIMOVEMENT_API UMassAvoidanceTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;
};
