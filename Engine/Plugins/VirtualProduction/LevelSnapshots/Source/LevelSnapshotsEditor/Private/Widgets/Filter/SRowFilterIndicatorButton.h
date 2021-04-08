// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UConjunctionFilter;

enum class EEditorFilterBehavior : uint8;

class SRowFilterIndicatorButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRowFilterIndicatorButton)
	{}
	SLATE_DEFAULT_SLOT(FArguments, Content)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UConjunctionFilter* InManagedFilter);

	void SetEditorFilterBehavior(const EEditorFilterBehavior InFilterBehavior, const bool bIncludeChildren = true);

private:
	FReply OnFilterClickedOnce();

private:
	TWeakObjectPtr<UConjunctionFilter> ManagedFilterWeakPtr;
};
