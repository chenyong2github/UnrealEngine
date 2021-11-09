// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"
#include "MassSmartObjectUserTrait.generated.h"

/**
 * Trait to allow an entity to interact with SmartObjects
 */
UCLASS(meta = (DisplayName = "SmartObject User"))
class MASSSMARTOBJECTS_API UMassSmartObjectUserTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;
};
