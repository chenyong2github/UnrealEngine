// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

namespace UE
{
	class FUsdStage;
}

#if USE_USD_SDK

class AUsdStageActor;

class SUsdPrimInfo : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SUsdPrimInfo ) {}
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, const UE::FUsdStage& UsdStage, const TCHAR* PrimPath );

	void SetPrimPath( const UE::FUsdStage& UsdStage, const TCHAR* PrimPath );

protected:
	TSharedRef< SWidget > GenerateVariantSetsWidget( const UE::FUsdStage& UsdStage, const TCHAR* PrimPath );
	TSharedRef< SWidget > GenerateReferencesListWidget( const UE::FUsdStage& UsdStage, const TCHAR* PrimPath );

private:
	TSharedPtr< class SUsdPrimPropertiesList > PropertiesList;
	TSharedPtr< class SVariantsList > VariantsList;
	TSharedPtr< class SBox > VariantSetsBox;
	TSharedPtr< class SBox > ReferencesBox;
};

#endif // #if USE_USD_SDK
