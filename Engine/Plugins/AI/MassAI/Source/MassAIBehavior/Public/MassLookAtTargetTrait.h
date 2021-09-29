// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassLookAtTargetTrait.generated.h"

UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta=(DisplayName="Look At Target"))
class UMassLookAtTargetTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;
};
