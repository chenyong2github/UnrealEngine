// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXInputPortReference.h"

#include "CoreMinimal.h"
#include "SGraphPin.h"

class SDMXPortSelector;


class DMXBLUEPRINTGRAPH_API SDMXInputPortReferenceGraphPin
	: public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SDMXInputPortReferenceGraphPin)
	{}
	
	SLATE_END_ARGS()

	/**  Slate widget construction method */
	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

	//~ Begin SGraphPin Interface
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override;
	//~ End SGraphPin Interface

private:
	void OnPortSelected() const;

	FDMXInputPortReference GetPinValue() const;

	void SetPinValue(const FDMXInputPortReference& InputPortReference) const;

	TSharedPtr<SDMXPortSelector> PortSelector;
};
