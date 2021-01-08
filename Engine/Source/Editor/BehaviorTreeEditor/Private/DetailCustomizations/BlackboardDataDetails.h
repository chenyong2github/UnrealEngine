// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Types/SlateEnums.h"
#include "Widgets/Views/SListView.h"

class IDetailLayoutBuilder;
class IPropertyHandle;
class ITableRow;
class STableViewBase;
class SComboButton;
class UBlackboardData;

/** Delegate used to retrieve current blackboard selection */
DECLARE_DELEGATE_RetVal_OneParam(int32, FOnGetSelectedBlackboardItemIndex, bool& /* bIsInherited */);

class FBlackboardDataDetails : public IDetailCustomization
{
public:
	FBlackboardDataDetails(FOnGetSelectedBlackboardItemIndex InOnGetSelectedBlackboardItemIndex, UBlackboardData* InBlackboardData)
		: OnGetSelectedBlackboardItemIndex(InOnGetSelectedBlackboardItemIndex)
		, BlackboardData(InBlackboardData)
	{}

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance(FOnGetSelectedBlackboardItemIndex InOnGetSelectedBlackboardItemIndex, UBlackboardData* InBlackboardData);

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailLayout ) override;

private:
	FText OnGetKeyCategoryText() const;
	void OnKeyCategoryTextCommitted(const FText& InNewText, ETextCommit::Type InTextCommit);
	void OnKeyCategorySelectionChanged(TSharedPtr<FText> ProposedSelection, ESelectInfo::Type /*SelectInfo*/);
	TSharedRef<ITableRow> MakeKeyCategoryViewWidget(TSharedPtr<FText> Item, const TSharedRef< STableViewBase >& OwnerTable);
	void PopulateKeyCategories();

private:
	/** Delegate used to retrieve current blackboard selection */
	FOnGetSelectedBlackboardItemIndex			OnGetSelectedBlackboardItemIndex;
	TSharedPtr<IPropertyHandle>					KeyHandle;
	TSharedPtr<SListView<TSharedPtr<FText>>>	KeyCategoryListView;
	TSharedPtr<SComboButton>					KeyCategoryComboButton;
	TArray<TSharedPtr<FText>>					KeyCategorySource;
	TWeakObjectPtr<UBlackboardData>				BlackboardData;
};
