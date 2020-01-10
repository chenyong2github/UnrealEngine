// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetNodes/SGraphNodeK2Default.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UK2Node_DataprepAction;

class SGraphNodeK2DataprepAction : public SGraphNodeK2Default
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeK2DataprepAction) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UK2Node_DataprepAction* InActionNode);

	//  Add the widgets below the pins 
	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override;

private:
	TWeakObjectPtr<class UDataprepActionAsset> DataprepActionPtr;
};
