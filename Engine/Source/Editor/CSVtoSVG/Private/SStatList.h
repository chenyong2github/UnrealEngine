// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
class STableViewBase;

class SStatList : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SStatList)
	{
	}

	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	void UpdateStatList(const TArray<FString>& StatNames);

	TArray<FString> GetSelectedStats() const;

private:

	TSharedRef<ITableRow> OnGenerateWidgetForList(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	TSharedPtr<SListView<TSharedPtr<FString>>> StatListView;
	TArray<TSharedPtr<FString>> StatList;
};
