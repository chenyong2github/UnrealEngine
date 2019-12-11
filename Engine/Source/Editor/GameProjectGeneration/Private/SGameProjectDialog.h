// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"
#include "Animation/CurveSequence.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Navigation/SBreadcrumbTrail.h"
#include "Widgets/Views/STileView.h"
#include "Widgets/Workflow/SWizard.h"

class SButton;
class SNewProjectWizard;
class SProjectBrowser;
struct FSlateBrush;
class STableViewBase;
struct FTemplateCategory;
class SRecentProjectBrowser;

/**
 * A dialog to create a new project or open an existing one
 */
class SGameProjectDialog
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SGameProjectDialog) {}
	SLATE_END_ARGS()

public:

	enum class EMode
	{
		Open,
		New,
		Both
	};

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs, EMode Mode );

	static void GetAllTemplateCategories(TArray<TSharedPtr<FTemplateCategory>>& OutCategories);

private:
	EMode DialogMode;

	TSharedPtr<SWizard> RootWizard;
	TSharedPtr<SProjectBrowser> ProjectBrowserPage;
	TSharedPtr<SNewProjectWizard> NewProjectWizard;
	TSharedPtr<SRecentProjectBrowser> RecentProjectBrowser;
	TSharedPtr<SBox> ProjectSettingsPage;

	TSharedPtr<STileView<TSharedPtr<FTemplateCategory>>> MajorCategoryTileView;
	TSharedPtr<STileView<TSharedPtr<FTemplateCategory>>> MinorCategoryTileView;

	TArray<TSharedPtr<FTemplateCategory>> MajorTemplateCategories;
	TArray<TSharedPtr<FTemplateCategory>> MinorTemplateCategories;

private:

	TSharedRef<SWidget> CreateLandingPage();

	TSharedRef<ITableRow> ConstructTile(TSharedPtr<FTemplateCategory> Item, const TSharedRef<STableViewBase>& TableView);

	FReply OnMoreProjectsClicked();

	void OnTemplateCategoryDoubleClick(TSharedPtr<FTemplateCategory> Item) const;
	void OnTemplateDoubleClick() const;

	void OnMajorTemplateCategorySelectionChanged(TSharedPtr<FTemplateCategory> Item, ESelectInfo::Type SelectType);
	void OnMinorTemplateCategorySelectionChanged(TSharedPtr<FTemplateCategory> Item, ESelectInfo::Type SelectType);
	void OnRecentProjectSelectionChanged(FString Item);

	int32 GetInitialPageIndex() const;
	int32 GetNextPageIndex(int32 Current) const;

	void OnCancelClicked() const;
	bool OnCanFinish() const;
	void OnFinishClicked();

	void OnEnterSettingsPage();

	EVisibility GetRecentProjectBrowserVisibility() const;
	EVisibility GetNoRecentProjectsLabelVisibility() const;

	FText GetFinishText() const;
	FText GetFinishTooltip() const;
	FText GetPageTitle(int32 Index) const;
};
