// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SUSDPrimInfo.h"

#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

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
#include "USDIncludesStart.h"

#include "pxr/usd/sdf/namespaceEdit.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/references.h"
#include "pxr/usd/usd/variantSets.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"

#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "SUSDPrimInfo"

namespace UsdPrimInfoWidgetConstants
{
	const FMargin CategoryHeaderPadding( 4.0f, 4.0f, 4.0f, 4.0f );
}

void SUsdPrimInfo::Construct( const FArguments& InArgs, const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath )
{	
	TSharedRef< SWidget > VariantSetsWidget = GenerateVariantSetsWidget( UsdStage, PrimPath );
	TSharedRef< SWidget > ReferencesListWidget = GenerateVariantSetsWidget( UsdStage, PrimPath );

	ChildSlot
	[
		SNew( SVerticalBox )

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SVerticalBox )

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew( SBorder )
				.BorderImage( &FCoreStyle::Get().GetWidgetStyle< FHeaderRowStyle >("TableView.Header").BackgroundBrush )
				.Padding( UsdPrimInfoWidgetConstants::CategoryHeaderPadding )
				[
					SNew( STextBlock )
					.Font( FEditorStyle::GetFontStyle( TEXT("DetailsView.CategoryFontStyle") ) )
					.Text( LOCTEXT( "Details", "Details" ) )
				]
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew( PropertiesList, SUsdPrimPropertiesList, PrimPath )
			]

			+SVerticalBox::Slot()
			[
				SNew( SSpacer )
				.Size( FVector2D( 0.f, 10.f ) )
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

void SUsdPrimInfo::SetPrimPath( const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath )
{
	if ( PropertiesList )
	{
		PropertiesList->SetPrimPath( PrimPath );
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

TSharedRef< SWidget > SUsdPrimInfo::GenerateVariantSetsWidget( const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath )
{
	TSharedRef< SWidget > VariantSetsWidget = SNullWidget::NullWidget;

	if ( !UsdStage.Get() )
	{
		return VariantSetsWidget;
	}

	TUsdStore< pxr::UsdPrim > UsdPrim = UsdStage.Get()->GetPrimAtPath( UnrealToUsd::ConvertPath( PrimPath ).Get() );

	if ( UsdPrim.Get() && UsdPrim.Get().HasVariantSets() )
	{
		SAssignNew( VariantSetsWidget, SVerticalBox )

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SBorder )
			.BorderImage( &FCoreStyle::Get().GetWidgetStyle< FHeaderRowStyle >("TableView.Header").BackgroundBrush  )
			.Padding( UsdPrimInfoWidgetConstants::CategoryHeaderPadding )
			[
				SNew( STextBlock )
				.Font( FEditorStyle::GetFontStyle( TEXT("DetailsView.CategoryFontStyle") ) )
				.Text( LOCTEXT( "Variants", "Variants" ) )
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew( VariantsList, SVariantsList, PrimPath )
		];
	}

	return VariantSetsWidget;
}

TSharedRef< SWidget > SUsdPrimInfo::GenerateReferencesListWidget( const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath )
{
	TSharedRef< SWidget > ReferencesListWidget = SNullWidget::NullWidget;

	if ( !UsdStage.Get() )
	{
		return ReferencesListWidget;
	}

	TUsdStore< pxr::UsdPrim > UsdPrim = UsdStage.Get()->GetPrimAtPath( UnrealToUsd::ConvertPath( PrimPath ).Get() );

	if ( UsdPrim.Get() && UsdPrim.Get().HasAuthoredReferences() )
	{
		SAssignNew( ReferencesListWidget, SVerticalBox )

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SBorder )
			.BorderImage( &FCoreStyle::Get().GetWidgetStyle< FHeaderRowStyle >("TableView.Header").BackgroundBrush  )
			.Padding( UsdPrimInfoWidgetConstants::CategoryHeaderPadding )
			[
				SNew( STextBlock )
				.Font( FEditorStyle::GetFontStyle( TEXT("DetailsView.CategoryFontStyle") ) )
				.Text( LOCTEXT( "References", "References" ) )
			]
		]

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
