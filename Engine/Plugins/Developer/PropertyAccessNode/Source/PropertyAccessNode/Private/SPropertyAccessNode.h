// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetNodes/SGraphNodeK2Base.h"

class UK2Node_PropertyAccess;

class SPropertyAccessNode : public SGraphNodeK2Base
{
public:
	SLATE_BEGIN_ARGS(SPropertyAccessNode) {}
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, UK2Node_PropertyAccess* InNode);

	// SGraphNode interface
	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> InMainBox) override;

private:
	// Helper for property/function binding
	bool CanBindProperty(FProperty* InProperty) const;
};