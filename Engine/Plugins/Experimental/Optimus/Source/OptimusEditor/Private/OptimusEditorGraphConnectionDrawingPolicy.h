// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConnectionDrawingPolicy.h"

class FOptimusEditorGraphConnectionDrawingPolicy :
	public FConnectionDrawingPolicy
{
public:
	FOptimusEditorGraphConnectionDrawingPolicy(
		int32 InBackLayerID,
		int32 InFrontLayerID,
		float ZoomFactor,
		const FSlateRect& InClippingRect,
		FSlateWindowElementList& InDrawElements,
		UEdGraph* InGraphObj
		);
	
	~FOptimusEditorGraphConnectionDrawingPolicy() override {}
	
	void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;
private:
	UEdGraph* Graph;
};
