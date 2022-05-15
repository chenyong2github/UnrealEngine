// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataflowSchema.generated.h"

class UDataflow; 

UCLASS()
class DATAFLOWEDITOR_API UDataflowSchema : public UEdGraphSchema
{
	GENERATED_BODY()
public:
	UDataflowSchema();

	//~ Begin EdGraphSchema Interface
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const override;
	virtual void GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	//~ End EdGraphSchema Interface
};
