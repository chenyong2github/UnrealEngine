// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassEntityTraitBase.h"
#include "MassLODTrait.generated.h"

UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "LOD"))
class MASSLOD_API UMassLODTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;
};
