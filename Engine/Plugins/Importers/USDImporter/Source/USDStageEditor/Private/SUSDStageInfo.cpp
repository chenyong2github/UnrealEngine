// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDStageInfo.h"

#include "USDConversionUtils.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"

#include "EditorDirectories.h"
#include "EditorStyleSet.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if USE_USD_SDK


#define LOCTEXT_NAMESPACE "UsdStageInfo"

void SUsdStageInfo::Construct( const FArguments& InArgs, AUsdStageActor* InUsdStageActor )
{
	RefreshStageInfos( InUsdStageActor );

	if ( InUsdStageActor )
	{
		InUsdStageActor->GetUsdListener().GetOnObjectsChanged().AddSP(this, &SUsdStageInfo::OnObjectsChanged );
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
				.IsReadOnly_Lambda([this]()
				{
					if ( const AUsdStageActor* StageActor = UsdStageActor.Get() )
					{
						return !( bool ) StageActor->GetUsdStage();
					}
					return true;
				})
				.OnTextCommitted( this, &SUsdStageInfo::OnMetersPerUnitCommitted )
			]
		]
	];
}

void SUsdStageInfo::RefreshStageInfos( AUsdStageActor* InUsdStageActor )
{
	UsdStageActor = InUsdStageActor;

	if ( InUsdStageActor )
	{
		if ( const UE::FUsdStage& UsdStage = static_cast< const AUsdStageActor* >( UsdStageActor.Get() )->GetUsdStage() )
		{
			StageInfos.RootLayerDisplayName = FText::FromString( UsdStage.GetRootLayer().GetDisplayName() );
			StageInfos.MetersPerUnit = UsdUtils::GetUsdStageMetersPerUnit( UsdStage );
			return;
		}
	}

	StageInfos.RootLayerDisplayName = LOCTEXT( "NoUsdStage", "No Stage Available" );
	StageInfos.MetersPerUnit.Reset();
}

SUsdStageInfo::~SUsdStageInfo()
{
	if ( AUsdStageActor* StageActor = UsdStageActor.Get() )
	{
		StageActor->GetUsdListener().GetOnObjectsChanged().RemoveAll( this );
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

void SUsdStageInfo::OnObjectsChanged( const UsdUtils::FObjectChangesByPath& InfoChanges, const UsdUtils::FObjectChangesByPath& ResyncChanges )
{
	bool bHasStageChanges = false;
	for ( const TPair<FString, TArray<UsdUtils::FObjectChangeNotice>>& Change : InfoChanges )
	{
		if ( Change.Key == TEXT( "/" ) )
		{
			bHasStageChanges = true;
			break;
		}
	}
	if ( !bHasStageChanges )
	{
		for ( const TPair<FString, TArray<UsdUtils::FObjectChangeNotice>>& Change : ResyncChanges )
		{
			if ( Change.Key == TEXT( "/" ) )
			{
				bHasStageChanges = true;
				break;
			}
		}
	}

	AUsdStageActor* StageActor = UsdStageActor.Get();
	if ( bHasStageChanges && StageActor )
	{
		RefreshStageInfos( StageActor );
	}
}

void SUsdStageInfo::OnMetersPerUnitCommitted( const FText& InUnitsPerMeterText, ETextCommit::Type InCommitInfo )
{
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	float MetersPerUnit = 0.01f;
	LexFromString( MetersPerUnit, *InUnitsPerMeterText.ToString() );

	MetersPerUnit = FMath::Clamp( MetersPerUnit, 0.001f, 1000.f );

	// Need a transaction here as this change may trigger actor/component spawning, which need to be in the undo buffer
	// Sadly undoing these transactions won't undo the actual stage changes yet though, so the metersPerUnit display and the
	// state of the stage will be desynced...
	FScopedTransaction Transaction( FText::Format(
		LOCTEXT( "SetMetersPerUnitTransaction", "Set USD stage metersPerUnit to '{0}'" ),
		MetersPerUnit
	) );

	UsdUtils::SetUsdStageMetersPerUnit( static_cast< const AUsdStageActor* >( UsdStageActor.Get() )->GetUsdStage(), MetersPerUnit );
	RefreshStageInfos( UsdStageActor.Get() );
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
