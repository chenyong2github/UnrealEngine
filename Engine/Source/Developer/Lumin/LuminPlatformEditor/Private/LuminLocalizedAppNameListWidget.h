// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "LuminLocalizedAppNameWidget.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SLuminLocalizedAppNameListWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLuminLocalizedAppNameListWidget)
		{
		}
		SLATE_ATTRIBUTE(FString, GameLuminPath)
		SLATE_ATTRIBUTE(IDetailLayoutBuilder*, DetailLayoutBuilder)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	FReply Add();
	FReply Remove(SLuminLocalizedAppNameWidget* LocalizedAppNameWidget);
	FReply RemoveAll();
	void Save();

private:
	TArray<TSharedPtr<SLuminLocalizedAppNameWidget>> Items;
	TSharedPtr< SListView< TSharedPtr<SLuminLocalizedAppNameWidget> > > ListViewWidget;
	IDetailLayoutBuilder* DetailLayoutBuilder;
	TSharedPtr<IPropertyHandle> LocalizedAppNamesProp;
	FString DefaultCultureName;

	void Load();
	TSharedRef<ITableRow> OnGenerateRowForList(TSharedPtr<SLuminLocalizedAppNameWidget> Item, const TSharedRef<STableViewBase>& OwnerTable);
	bool ItemToString(const FLocalizedAppName& LocalizedAppName, FString& OutString);
	bool StringToItem(const FString& String, FLocalizedAppName& OutLocalizedAppName);
};
