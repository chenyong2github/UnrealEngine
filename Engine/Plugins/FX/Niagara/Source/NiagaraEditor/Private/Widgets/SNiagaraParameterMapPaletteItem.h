// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "SGraphPalette.h"
#include "NiagaraActions.h"
#include "EdGraph/EdGraphNodeUtils.h"

class SNiagaraParameterNameTextBlock;

class SNiagaraParameterMapPalleteItem : public SGraphPaletteItem
{
public:

	DECLARE_DELEGATE_TwoParams(FOnItemRenamed, const FText&, TSharedRef<struct FNiagaraParameterAction>)

	SLATE_BEGIN_ARGS(SNiagaraParameterMapPalleteItem)
	{}
		SLATE_EVENT(FOnItemRenamed, OnItemRenamed)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, FCreateWidgetForActionData* const InCreateData);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual FText GetItemTooltip() const override;

protected:
	/** Callback when rename text is committed */
	virtual void OnNameTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit) override;

	FText GetReferenceCount() const;

private:
	FOnItemRenamed OnItemRenamed;
	TSharedPtr<SNiagaraParameterNameTextBlock> ParameterNameTextBlock;
	mutable FText ToolTipCache;
	mutable FText CreatedToolTipCache;
};