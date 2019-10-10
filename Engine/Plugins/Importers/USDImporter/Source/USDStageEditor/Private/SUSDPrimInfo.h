// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#if USE_USD_SDK

#include "USDMemory.h"

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"

#include "USDIncludesEnd.h"

class AUsdStageActor;

class SUsdPrimInfo : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SUsdPrimInfo ) {}
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath );

	void SetPrimPath( const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath );

protected:
	TSharedRef< SWidget > GenerateVariantSetsWidget( const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath );
	TSharedRef< SWidget > GenerateReferencesListWidget( const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath );

private:
	TSharedPtr< class SUsdPrimPropertiesList > PropertiesList;
	TSharedPtr< class SVariantsList > VariantsList;
	TSharedPtr< class SBox > VariantSetsBox;
	TSharedPtr< class SBox > ReferencesBox;
};

#endif // #if USE_USD_SDK
