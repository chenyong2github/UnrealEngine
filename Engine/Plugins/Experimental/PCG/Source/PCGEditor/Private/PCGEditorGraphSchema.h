// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphSchema.h"

#include "PCGEditorGraphSchema.generated.h"

UCLASS()
class UPCGEditorGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	//~ Begin EdGraphSchema Interface
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	//~ End EdGraphSchema Interface
};
