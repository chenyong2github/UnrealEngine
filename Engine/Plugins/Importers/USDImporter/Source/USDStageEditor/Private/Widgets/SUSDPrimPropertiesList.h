// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Views/SListView.h"

#if USE_USD_SDK

struct FUsdPrimProperty : public TSharedFromThis< FUsdPrimProperty >
{
	FString Label;
	FString Value;
};

class SUsdPrimPropertiesList : public SListView< TSharedPtr< FUsdPrimProperty > >
{
	SLATE_BEGIN_ARGS( SUsdPrimPropertiesList ) {}
	SLATE_END_ARGS()

public:
	void Construct( const FArguments& InArgs, const TCHAR* InPrimPath );
	void SetPrimPath( const TCHAR* InPrimPath );

protected:
	TSharedRef< ITableRow > OnGenerateRow( TSharedPtr< FUsdPrimProperty > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable );
	void GeneratePropertiesList( const TCHAR* InPrimPath );

private:
	FString PrimPath;

	TArray< TSharedPtr< FUsdPrimProperty > > PrimProperties;
	TSharedPtr< SHeaderRow > HeaderRowWidget;
};

#endif // USE_USD_SDK
