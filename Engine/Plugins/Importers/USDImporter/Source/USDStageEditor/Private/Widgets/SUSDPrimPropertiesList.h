// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Views/SListView.h"
#include "USDPrimAttributesViewModel.h"

#if USE_USD_SDK

class SUsdPrimPropertiesList : public SListView< TSharedPtr< FUsdPrimAttributeViewModel > >
{
	SLATE_BEGIN_ARGS( SUsdPrimPropertiesList ) {}
	SLATE_END_ARGS()

public:
	void Construct( const FArguments& InArgs, const UE::FUsdStage& UsdStage, const TCHAR* InPrimPath );
	void SetPrimPath( const UE::FUsdStage& UsdStage, const TCHAR* InPrimPath );

protected:
	TSharedRef< ITableRow > OnGenerateRow( TSharedPtr< FUsdPrimAttributeViewModel > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable );
	void GeneratePropertiesList( const UE::FUsdStage& UsdStage, const TCHAR* InPrimPath );

private:
	FUsdPrimAttributesViewModel ViewModel;
	TSharedPtr< SHeaderRow > HeaderRowWidget;
};

#endif // USE_USD_SDK
