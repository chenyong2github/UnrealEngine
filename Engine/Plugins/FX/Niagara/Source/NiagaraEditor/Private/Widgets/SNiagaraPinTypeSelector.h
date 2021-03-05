// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "Misc/Attribute.h"
#include "EdGraph/EdGraphPin.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateColor.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "NiagaraTypes.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SCompoundWidget.h"

class NIAGARAEDITOR_API SNiagaraPinTypeSelector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraPinTypeSelector)
	{}
	SLATE_END_ARGS()
public:
	void Construct(const FArguments& InArgs, UEdGraphPin * InGraphPin);

protected:
	virtual TSharedRef<SWidget>	GetMenuContent();
	FText GetTooltipText() const;

private:
	UEdGraphPin* Pin = nullptr;
	TSharedPtr<SComboButton> SelectorButton;
};