// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassLookAtTrait.generated.h"

UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta=(DisplayName="Look At"))
class UMassLookAtTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;
};
