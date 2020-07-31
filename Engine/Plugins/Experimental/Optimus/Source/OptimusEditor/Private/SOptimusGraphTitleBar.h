// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "OptimusEditorDelegates.h"

class FOptimusEditor;
class SScrollBox;
class UEdGraph;
template<typename> class SBreadcrumbTrail;

class SOptimusGraphTitleBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOptimusGraphTitleBar)
	{}
		SLATE_ARGUMENT(TWeakPtr<FOptimusEditor>, OptimusEditor)
		SLATE_EVENT(FOptimusGraphEvent, OnDifferentGraphCrumbClicked)
	SLATE_END_ARGS()

	~SOptimusGraphTitleBar();

	void Construct(const FArguments& InArgs);

	// Forcibly refresh the title bar
	void Refresh();

private:
	void RebuildBreadcrumbTrail();

	const FSlateBrush* GetGraphTypeIcon() const;

	static FText GetTitleForOneCrumb(const UOptimusEditorGraph* Graph);

	void OnBreadcrumbClicked(UEdGraph* const& Graph);

	FText GetDeformerTitle() const;
	EVisibility IsDeformerTitleVisible() const;

	// The owning graph editor widget.
	TWeakPtr<FOptimusEditor> OptimusEditor;

	// The scroll box that kicks in if the trail exceeds the widget's visible box.
	TSharedPtr<SScrollBox> BreadcrumbTrailScrollBox;

	// Breadcrumb trail widget
	TSharedPtr< SBreadcrumbTrail<UEdGraph*> > BreadcrumbTrail;

	// Callback for switching graph levels.
	FOptimusGraphEvent OnDifferentGraphCrumbClicked;
};
