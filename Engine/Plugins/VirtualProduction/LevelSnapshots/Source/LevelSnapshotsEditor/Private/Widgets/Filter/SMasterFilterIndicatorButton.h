// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FLevelSnapshotsEditorFilters;
class UDisjunctiveNormalFormFilter;

enum class EEditorFilterBehavior : uint8;

class SMasterFilterIndicatorButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMasterFilterIndicatorButton)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UDisjunctiveNormalFormFilter* InUserDefinedFilters);

	void SetUserDefinedFilters(UDisjunctiveNormalFormFilter* InUserDefinedFilters);

	void SetEditorFilterBehavior(const EEditorFilterBehavior InFilterBehavior, const bool bIncludeChildren = true);

private:
	
	FReply OnFilterClickedOnce();

	TWeakObjectPtr<UDisjunctiveNormalFormFilter> UserDefinedFiltersWeakPtr;
};
