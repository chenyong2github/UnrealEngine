// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Views/SListView.h"

#if USE_USD_SDK

struct FUsdVariantSet : public TSharedFromThis< FUsdVariantSet >
{
	FString SetName;
	TSharedPtr< FString > VariantSelection;
	TArray< TSharedPtr< FString > > Variants;
};

class SUsdVariantRow : public SMultiColumnTableRow< TSharedPtr< FUsdVariantSet > >
{
public:
	DECLARE_DELEGATE_OneParam( FOnVariantSelectionChanged, const TSharedRef< FUsdVariantSet >& );

public:
	SLATE_BEGIN_ARGS( SUsdVariantRow )
		: _OnVariantSelectionChanged()
		{
		}

		SLATE_EVENT( FOnVariantSelectionChanged, OnVariantSelectionChanged )

	SLATE_END_ARGS()

public:
	void Construct( const FArguments& InArgs, TSharedPtr< FUsdVariantSet > InVariantSet, const TSharedRef< STableViewBase >& OwnerTable );

	virtual TSharedRef< SWidget > GenerateWidgetForColumn( const FName& ColumnName ) override;

protected:
	FOnVariantSelectionChanged OnVariantSelectionChanged;

protected:
	void OnSelectionChanged( TSharedPtr< FString > NewValue, ESelectInfo::Type SelectInfo );

private:
	FString PrimPath;
	TSharedPtr< FUsdVariantSet > VariantSet;
};

class SVariantsList : public SListView< TSharedPtr< FUsdVariantSet > >
{
	SLATE_BEGIN_ARGS( SVariantsList ) {}
	SLATE_END_ARGS()

public:
	void Construct( const FArguments& InArgs, const TCHAR* InPrimPath );
	void SetPrimPath( const TCHAR* InPrimPath );

protected:
	void UpdateVariantSets( const TCHAR* InPrimPath );
	TSharedRef< ITableRow > OnGenerateRow( TSharedPtr< FUsdVariantSet > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable );

	void OnVariantSelectionChanged( const TSharedRef< FUsdVariantSet >& VariantSet );

private:
	FString PrimPath;

	TArray< TSharedPtr< FUsdVariantSet > > VariantSets;
	TSharedPtr< SHeaderRow > HeaderRowWidget;
};

#endif // #if USE_USD_SDK
