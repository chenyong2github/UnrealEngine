// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Views/SListView.h"

#if USE_USD_SDK

#include "USDMemory.h"

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/usd/prim.h"

#include "USDIncludesEnd.h"

struct FUsdReference : public TSharedFromThis< FUsdReference >
{
	FString AssetPath;
};

class SUsdReferenceRow : public SMultiColumnTableRow< TSharedPtr< FUsdReference > >
{
public:
	SLATE_BEGIN_ARGS( SUsdReferenceRow ) {}
	SLATE_END_ARGS()

public:
	void Construct( const FArguments& InArgs, TSharedPtr< FUsdReference > InReference, const TSharedRef< STableViewBase >& OwnerTable );

	virtual TSharedRef< SWidget > GenerateWidgetForColumn( const FName& ColumnName ) override;

private:
	FString PrimPath;
	TSharedPtr< FUsdReference > Reference;
};

class SUsdReferencesList : public SListView< TSharedPtr< FUsdReference > >
{
	SLATE_BEGIN_ARGS( SUsdReferencesList ) {}
	SLATE_END_ARGS()

public:
	void Construct( const FArguments& InArgs, const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath );
	void SetPrimPath( const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath );

protected:
	void UpdateReferences( const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath );
	TSharedRef< ITableRow > OnGenerateRow( TSharedPtr< FUsdReference > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable );

private:
	TArray< TSharedPtr< FUsdReference > > References;
	TSharedPtr< SHeaderRow > HeaderRowWidget;
};

#endif // #if USE_USD_SDK
