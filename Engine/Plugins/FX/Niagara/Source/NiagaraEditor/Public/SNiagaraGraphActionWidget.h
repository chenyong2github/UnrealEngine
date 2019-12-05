// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "SGraphActionMenu.h"
#include "EdGraph/EdGraphSchema.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FCreateWidgetForActionData;

/** Custom widget for GraphActionMenu */
class NIAGARAEDITOR_API SNiagaraGraphActionWidget : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS( SNiagaraGraphActionWidget ) {}
		SLATE_ATTRIBUTE(FText, HighlightText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FCreateWidgetForActionData* InCreateData);
	virtual FReply OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;

private:
	/** The item that we want to display with this widget */
	TWeakPtr<struct FEdGraphSchemaAction> ActionPtr;
	/** Delegate executed when mouse button goes down */
	FCreateWidgetMouseButtonDown MouseButtonDownDelegate;
};
