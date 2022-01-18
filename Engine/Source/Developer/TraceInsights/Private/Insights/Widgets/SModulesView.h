// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FMenuBuilder;

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FModule;

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace ModulesViewColumns
{
	static const FName AddressRangeColumnName(TEXT("BaseAddress"));
	static const FName ModuleNameColumnName(TEXT("ModuleName"));
	static const FName SymbolsFileColumnName(TEXT("SymbolsFile"));
	static const FName StatusColumnName(TEXT("Status"));
	static const FName StatsColumnName(TEXT("Stats"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Modules window.
 */
class SModulesView : public SCompoundWidget
{
public:
	/** Default constructor. */
	SModulesView();

	/** Virtual destructor. */
	virtual ~SModulesView();

	void Reset();

	SLATE_BEGIN_ARGS(SModulesView) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget.
	 * @param InArgs - The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs);

	/**
	 * Ticks this widget. Override in derived classes, but always call the parent implementation.
	 *
	 * @param AllottedGeometry - The space allotted for this widget
	 * @param InCurrentTime - Current absolute real time
	 * @param InDeltaTime - Real time passed since last tick
	 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	TSharedPtr<FModule> GetSelectedModule() const;

protected:
	/** Generate a new list view row. */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FModule> InModule, const TSharedRef<STableViewBase>& OwnerTable);

	void SelectModule(TSharedPtr<FModule> Module);
	void OnMouseButtonClick(TSharedPtr<FModule> Module);
	void OnSelectionChanged(TSharedPtr<FModule> Module, ESelectInfo::Type SelectInfo);

	FText GetStatsText() const;
	FSlateColor GetStatsTextColor() const;

	TSharedPtr<SWidget> ListView_GetContextMenu();
	FText LoadSymbolsFile_Label() const;
	FText LoadSymbolsDirectory_Label() const;
	bool LoadSymbols_CanExecute() const;

	void LoadSymbols_Execute(TSharedPtr<FModule> Module, bool bOpenFile);

protected:
	/** The list view widget. */
	TSharedPtr<SListView<TSharedPtr<FModule>>> ListView;

	/** External scrollbar used to synchronize tree view position. */
	TSharedPtr<SScrollBar> ExternalScrollbar;

	/** List of modules (view model) to show in list view. */
	TArray<TSharedPtr<FModule>> Modules;

	/** Reference to ongoing load symbols task. */
	FGraphEventRef LoadSymbolsTask;

	/** Last time we queried about discovered modules. */
	double LastUpdateTime;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
