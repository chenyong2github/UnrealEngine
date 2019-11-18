// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SUSDStageInfo.h"

#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "EditorDirectories.h"
#include "EditorStyleSet.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Text/STextBlock.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"

#include "USDIncludesEnd.h"


#define LOCTEXT_NAMESPACE "UsdStageInfo"

void SUsdStageInfo::Construct( const FArguments& InArgs, AUsdStageActor* UsdStageActor )
{
	OnInitialLoadSetChanged = InArgs._OnInitialLoadSetChanged;

	InitialLoadSetStrings.Reset();
	InitialLoadSetStrings.Add( MakeShared< FString >( TEXT("Load All") ) );
	InitialLoadSetStrings.Add( MakeShared< FString >( TEXT("Load None") ) );

	RefreshStageInfos( UsdStageActor );

	ChildSlot
	[
		SNew( SVerticalBox )

		+SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.Padding(2.f, 2.f)
		[
			SNew( STextBlock )
			.TextStyle( FEditorStyle::Get(), "LargeText" )
			.Text( this, &SUsdStageInfo::GetRootLayerDisplayName )
			.Font( FEditorStyle::GetFontStyle( "ContentBrowser.SourceTreeItemFont" ) )
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.Padding(2.f, 2.f)
		[
			SNew( SHorizontalBox )

			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign( HAlign_Left )
			.Padding( 2.f, 2.f )
			[
				SAssignNew( InitialLoadSetWidget, STextComboBox )
				.OptionsSource( &InitialLoadSetStrings )
				.InitiallySelectedItem( InitialLoadSetStrings[ (int32)StageInfos.InitialLoadSet ] )
				.OnSelectionChanged( this, &SUsdStageInfo::OnInitialLoadSetSelectionChanged )
			]
		]
	];
}

void SUsdStageInfo::RefreshStageInfos( AUsdStageActor* UsdStageActor )
{
	StageInfos.RootLayerDisplayName = LOCTEXT( "NoUsdStage", "No Stage Available" );

	if ( !UsdStageActor )
	{
		return;
	}

	StageInfos.InitialLoadSet = UsdStageActor->InitialLoadSet;

	if ( InitialLoadSetWidget && InitialLoadSetStrings.IsValidIndex( (int32)StageInfos.InitialLoadSet ) )
	{
		InitialLoadSetWidget->SetSelectedItem( InitialLoadSetStrings[ (int32)StageInfos.InitialLoadSet ] );
	}

	if ( const pxr::UsdStageRefPtr& UsdStage = UsdStageActor->GetUsdStage() )
	{
		TUsdStore< std::string > UsdDisplayName = UsdStage->GetRootLayer()->GetDisplayName();
		StageInfos.RootLayerDisplayName = FText::FromString( UsdToUnreal::ConvertString( UsdDisplayName.Get() ) );
	}
}

void SUsdStageInfo::OnInitialLoadSetSelectionChanged( TSharedPtr< FString > NewValue, ESelectInfo::Type SelectInfo )
{
	StageInfos.InitialLoadSet = (EUsdInitialLoadSet)InitialLoadSetStrings.Find( NewValue );
	OnInitialLoadSetChanged.ExecuteIfBound( StageInfos.InitialLoadSet );
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
