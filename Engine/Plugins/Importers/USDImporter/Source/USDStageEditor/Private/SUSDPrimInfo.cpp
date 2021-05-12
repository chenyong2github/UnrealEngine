// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDPrimInfo.h"

#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "Algo/Find.h"
#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SUSDPrimPropertiesList.h"
#include "Widgets/SUSDReferencesList.h"
#include "Widgets/SUSDVariantSetsList.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#if USE_USD_SDK

#define LOCTEXT_NAMESPACE "SUSDPrimInfo"

namespace UsdPrimInfoWidgetConstants
{
	const FMargin CategoryHeaderPadding( 4.0f, 4.0f, 4.0f, 4.0f );
}

void SUsdPrimInfo::Construct( const FArguments& InArgs, const UE::FUsdStage& UsdStage, const TCHAR* PrimPath )
{
	TSharedRef< SWidget > VariantSetsWidget = GenerateVariantSetsWidget( UsdStage, PrimPath );
	TSharedRef< SWidget > ReferencesListWidget = GenerateReferencesListWidget( UsdStage, PrimPath );

	ChildSlot
	[
		SNew( SVerticalBox )

		+SVerticalBox::Slot()
		.FillHeight( 1.f )
		[
			SNew( SBox )
			.Content()
			[
				SAssignNew( PropertiesList, SUsdPrimPropertiesList, UsdStage, PrimPath )
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew( VariantSetsBox, SBox )
			.Content()
			[
				VariantSetsWidget
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew( ReferencesBox, SBox )
			.Content()
			[
				ReferencesListWidget
			]
		]
	];

	SetPrimPath( UsdStage, PrimPath );
}

void SUsdPrimInfo::SetPrimPath( const UE::FUsdStage& UsdStage, const TCHAR* PrimPath )
{
	if ( PropertiesList )
	{
		PropertiesList->SetPrimPath( UsdStage, PrimPath );
	}

	if ( VariantsList )
	{
		VariantsList->SetPrimPath( UsdStage, PrimPath );
	}

	if ( VariantSetsBox )
	{
		VariantSetsBox->SetContent( GenerateVariantSetsWidget( UsdStage, PrimPath ) );
	}

	if ( ReferencesBox )
	{
		ReferencesBox->SetContent( GenerateReferencesListWidget( UsdStage, PrimPath ) );
	}
}

TSharedRef< SWidget > SUsdPrimInfo::GenerateVariantSetsWidget( const UE::FUsdStage& UsdStage, const TCHAR* PrimPath )
{
	TSharedRef< SWidget > VariantSetsWidget = SNullWidget::NullWidget;

	if ( !UsdStage )
	{
		return VariantSetsWidget;
	}

	UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath( PrimPath ) );

	if ( UsdPrim && UsdPrim.HasVariantSets() )
	{
		SAssignNew( VariantSetsWidget, SVerticalBox )

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew( VariantsList, SVariantsList, UsdStage, PrimPath )
		];
	}

	return VariantSetsWidget;
}

TSharedRef< SWidget > SUsdPrimInfo::GenerateReferencesListWidget( const UE::FUsdStage& UsdStage, const TCHAR* PrimPath )
{
	TSharedRef< SWidget > ReferencesListWidget = SNullWidget::NullWidget;

	if ( !UsdStage )
	{
		return ReferencesListWidget;
	}

	UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath( PrimPath ) );

	if ( UsdPrim && UsdPrim.HasAuthoredReferences() )
	{
		SAssignNew( ReferencesListWidget, SVerticalBox )

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SUsdReferencesList, UsdStage, PrimPath )
		];
	}

	return ReferencesListWidget;
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
