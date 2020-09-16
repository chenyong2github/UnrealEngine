// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDStageInfo.h"

#include "USDConversionUtils.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"

#include "EditorDirectories.h"
#include "EditorStyleSet.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Text/STextBlock.h"

#if USE_USD_SDK


#define LOCTEXT_NAMESPACE "UsdStageInfo"

void SUsdStageInfo::Construct( const FArguments& InArgs, AUsdStageActor* InUsdStageActor )
{
	RefreshStageInfos( InUsdStageActor );

	if ( InUsdStageActor )
	{
		// Just adapt the event signature as we don't use the ChangedFields yet
		InUsdStageActor->GetUsdListener().GetOnStageInfoChanged().AddLambda( [ this, InUsdStageActor ]( const TArray<FString>& ChangedFields )
		{
			this->RefreshStageInfos( InUsdStageActor );
		});
	}

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
		.HAlign(HAlign_Left)
		[
			SNew( SHorizontalBox )

			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			.Padding(2.f, 2.f)
			[
				SNew( STextBlock )
				.Text( LOCTEXT( "MetersPerUnit", "Meters per unit" ) )
			]
			+SHorizontalBox::Slot()
			.Padding(2.f, 2.f)
			[
				SNew( SEditableTextBox )
				.HintText( LOCTEXT( "Unset", "Unset" ) )
				.Text( this, &SUsdStageInfo::GetMetersPerUnit )
				.OnTextCommitted( this, &SUsdStageInfo::OnMetersPerUnitCommitted )
			]
		]
	];
}

void SUsdStageInfo::RefreshStageInfos( AUsdStageActor* InUsdStageActor )
{
	UsdStageActor = InUsdStageActor;
	StageInfos.RootLayerDisplayName = LOCTEXT( "NoUsdStage", "No Stage Available" );

	if ( !InUsdStageActor )
	{
		return;
	}

	if ( const UE::FUsdStage& UsdStage = UsdStageActor->GetUsdStage() )
	{
		StageInfos.RootLayerDisplayName = FText::FromString( UsdStage.GetRootLayer().GetDisplayName() );
		StageInfos.MetersPerUnit = UsdUtils::GetUsdStageMetersPerUnit( UsdStage );
	}
}

SUsdStageInfo::~SUsdStageInfo()
{
	if ( AUsdStageActor* StageActor = UsdStageActor.Get() )
	{
		StageActor->GetUsdListener().GetOnStageInfoChanged().RemoveAll( this );
	}
}

FText SUsdStageInfo::GetMetersPerUnit() const
{
	if ( StageInfos.MetersPerUnit )
	{
		return FText::FromString( LexToSanitizedString( StageInfos.MetersPerUnit.GetValue() ) );
	}
	else
	{
		return FText();
	}
}

void SUsdStageInfo::OnMetersPerUnitCommitted( const FText& InUnitsPerMeterText, ETextCommit::Type InCommitInfo )
{
	if ( UsdStageActor.IsValid() )
	{
		float MetersPerUnit = 0.01f;
		LexFromString( MetersPerUnit, *InUnitsPerMeterText.ToString() );

		MetersPerUnit = FMath::Clamp( MetersPerUnit, 0.001f, 1000.f );

		UsdUtils::SetUsdStageMetersPerUnit( UsdStageActor->GetUsdStage(), MetersPerUnit );
		RefreshStageInfos( UsdStageActor.Get() );
	}
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
