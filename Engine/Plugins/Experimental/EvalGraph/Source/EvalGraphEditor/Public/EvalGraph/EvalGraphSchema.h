// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EvalGraphSchema.generated.h"

class UEvalGraph; 

UCLASS()
class EVALGRAPHEDITOR_API UEvalGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()
public:
	UEvalGraphSchema();

	//~ Begin EdGraphSchema Interface
	virtual void GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	//~ End EdGraphSchema Interface
};
