// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeSchema.h"
#include "StateTreeComponentSchema.generated.h"

/**
 * StateTree for Actors with StateTree component. 
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "StateTree Component", CommonSchema))
class UStateTreeComponentSchema : public UStateTreeSchema
{
	GENERATED_BODY()

protected:
	virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override;
	virtual bool IsClassAllowed(const UClass* InScriptStruct) const override;
	virtual bool IsExternalItemAllowed(const UStruct& InStruct) const override;
};
