// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

struct FTraceFileInfo
{
	FString FilePath;
	FDateTime ModifiedTime;
	bool bIsFromTraceStore;

	bool operator <(const FTraceFileInfo& rhs)
	{
		return this->ModifiedTime > rhs.ModifiedTime;
	}
};

/**
 *  Shows a list of recent traces in a menu.
 */
class SRecentTracesList : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SRecentTracesList) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FString& InStorePath);

	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FTraceFileInfo> Item, const TSharedRef<STableViewBase>& OwnerTable);

private:
	void PopulateRecentTracesList();

private:
	TSharedPtr<SListView<TSharedPtr<FTraceFileInfo>>> ListView;
	
	TArray<TSharedPtr<FTraceFileInfo>> Traces;

	FString StorePath;
};
