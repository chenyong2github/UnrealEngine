// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassEntityTraitBase.h"
#include "MassZoneGraphAnnotationTrait.generated.h"

UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "ZoneGraph Annotation"))
class MASSAIBEHAVIOR_API UMassZoneGraphAnnotationTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;
};
