// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AUsdStageActor;
enum class EUsdInitialLoadSet;
class STextComboBox;

#if USE_USD_SDK

DECLARE_DELEGATE_OneParam( FOnInitialLoadSetChanged, EUsdInitialLoadSet );

class SUsdStageInfo : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SUsdStageInfo ) {}
		SLATE_EVENT( FOnInitialLoadSetChanged, OnInitialLoadSetChanged )
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, AUsdStageActor* UsdStageActor );
	void RefreshStageInfos( AUsdStageActor* UsdStageActor );

	EUsdInitialLoadSet GetInitialLoadSet() const { return StageInfos.InitialLoadSet; }

private:
	void OnInitialLoadSetSelectionChanged( TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo );

	FOnInitialLoadSetChanged OnInitialLoadSetChanged;
	TArray< TSharedPtr< FString > > InitialLoadSetStrings;

	struct FStageInfos
	{
		FText RootLayerDisplayName;
		EUsdInitialLoadSet InitialLoadSet;
	};

	FStageInfos StageInfos;

	FText GetRootLayerDisplayName() const { return StageInfos.RootLayerDisplayName; }

	TSharedPtr< STextComboBox > InitialLoadSetWidget;
};

#endif // #if USE_USD_SDK
