// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Views/SListView.h"
#include "USDPrimAttributesViewModel.h"

#if USE_USD_SDK


class SUsdPrimPropertiesList : public SListView< TSharedPtr< FUsdPrimAttributeViewModel > >
{
public:
	SLATE_BEGIN_ARGS( SUsdPrimPropertiesList ) {}
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
		SLATE_ATTRIBUTE(FText, NameColumnText)
	SLATE_END_ARGS()

public:
	void Construct( const FArguments& InArgs );
	void SetPrimPath( const UE::FUsdStageWeak& UsdStage, const TCHAR* InPrimPath );

	TArray<FString> GetSelectedPropertyNames() const;
	void SetSelectedPropertyNames( const TArray<FString>& NewSelection );

	UE::FUsdStageWeak GetUsdStage() const;
	FString GetPrimPath() const;

protected:
	TSharedRef< ITableRow > OnGenerateRow( TSharedPtr< FUsdPrimAttributeViewModel > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable );
	void GeneratePropertiesList( const UE::FUsdStageWeak& UsdStage, const TCHAR* InPrimPath );

	void Sort(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type NewSortMode);
	EColumnSortMode::Type GetColumnSortMode(const FName ColumnId) const;

private:
	FUsdPrimAttributesViewModel ViewModel;
	TSharedPtr< SHeaderRow > HeaderRowWidget;
};

#endif // USE_USD_SDK
