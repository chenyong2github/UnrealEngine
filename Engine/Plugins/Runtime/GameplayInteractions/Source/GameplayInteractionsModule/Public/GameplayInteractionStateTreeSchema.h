// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeSchema.h"
#include "GameplayInteractionStateTreeSchema.generated.h"

struct FStateTreeExternalDataDesc;

UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "Gameplay Interactions"))
class GAMEPLAYINTERACTIONSMODULE_API UGameplayInteractionStateTreeSchema : public UStateTreeSchema
{
	GENERATED_BODY()

public:
	UGameplayInteractionStateTreeSchema();

protected:
	virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override;
	virtual bool IsClassAllowed(const UClass* InScriptStruct) const override;
	virtual bool IsExternalItemAllowed(const UStruct& InStruct) const override;

	virtual TConstArrayView<FStateTreeExternalDataDesc> GetContextDataDescs() const override { return NamedExternalDataDescs; }

	/** List of named external data required by schema and provided to the state tree through the execution context. */
	UPROPERTY()
	TArray<FStateTreeExternalDataDesc> NamedExternalDataDescs;
};
