// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

class FConcertSessionItem;
class SWidget;
class SInlineEditableTextBlock;

class SSessionRow : public SMultiColumnTableRow<TSharedPtr<FConcertSessionItem>>
{
public:
	using FDoubleClickFunc = TFunction<void(TSharedPtr<FConcertSessionItem>)>;
	using FRenameFunc = TFunction<void(TSharedPtr<FConcertSessionItem>, const FString&)>;
	using FIsDefaultSession = TFunction<bool(TSharedPtr<FConcertSessionItem>)>;

	SLATE_BEGIN_ARGS(SSessionRow)
		: _OnDoubleClickFunc()
		, _OnRenameFunc()
		, _HighlightText()
		, _IsSelected(false)
	{}

	SLATE_ARGUMENT(FDoubleClickFunc, OnDoubleClickFunc)
	SLATE_ARGUMENT(FRenameFunc, OnRenameFunc)
	SLATE_ARGUMENT(FIsDefaultSession, IsDefaultSession)
	
	SLATE_ATTRIBUTE(FText, HighlightText)
	SLATE_ATTRIBUTE(bool, IsSelected)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FConcertSessionItem> InItem, const TSharedRef<STableViewBase>& InOwnerTableView);
	
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;
	virtual FReply OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

	void OnSessionNameCommitted(const FText& NewSessionName, ETextCommit::Type CommitType);

private:
	
	void OnBeginEditingSessionName() { SessionNameText->EnterEditingMode(); }
	bool OnValidatingSessionName(const FText& NewSessionName, FText& OutError);

	TWeakPtr<FConcertSessionItem> Item;

	/** Invoked when the user double click on the row */
	FDoubleClickFunc DoubleClickFunc;
	/** Invoked when the user commit the session rename. (This will send the request to server) */
	FRenameFunc RenameFunc;
	/** Given a session checks whether it is the default session the client is supposed to join */
	FIsDefaultSession IsDefaultSession;
	
	TAttribute<FText> HighlightText;
	TAttribute<bool> IsSelected;
	TSharedPtr<SInlineEditableTextBlock> SessionNameText;
};
