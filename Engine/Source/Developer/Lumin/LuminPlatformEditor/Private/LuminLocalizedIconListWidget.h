// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "DetailLayoutBuilder.h"
#include "Widgets/Views/SListView.h"
#include "LuminLocalizedIconWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SLuminLocalizedIconListWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLuminLocalizedIconListWidget)
		{
		}
		SLATE_ATTRIBUTE(FString, GameLuminPath)
		SLATE_ATTRIBUTE(IDetailLayoutBuilder*, DetailLayoutBuilder)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	FReply Add();
	FReply RemoveAll();
	void RemoveIconWidget(SLuminLocalizedIconWidget* Icon);
	void SaveIconData();

private:
	TArray<TSharedPtr<SLuminLocalizedIconWidget>> Items;
	TSharedPtr< SListView< TSharedPtr<SLuminLocalizedIconWidget> > > ListViewWidget;
	IDetailLayoutBuilder* DetailLayoutBuilder;
	FString GameLuminPath;
	TSharedPtr<IPropertyHandle> LocalizedIconInfosProp;

	void LoadIconData();
	TSharedRef<ITableRow> OnGenerateRowForList(TSharedPtr<SLuminLocalizedIconWidget> Item, const TSharedRef<STableViewBase>& OwnerTable);
	FString LocalizedIconInfoToString(const FLocalizedIconInfo& LocalizedIconInfo);
};
