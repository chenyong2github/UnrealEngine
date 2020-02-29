// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Optional.h"
#include "Types/SlateEnums.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AUsdStageActor;

#if USE_USD_SDK

class SUsdStageInfo : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SUsdStageInfo ) {}
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, AUsdStageActor* InUsdStageActor );
	void RefreshStageInfos( AUsdStageActor* InUsdStageActor );

private:
	struct FStageInfos
	{
		FText RootLayerDisplayName;
		TOptional< float > MetersPerUnit;
	};

	FStageInfos StageInfos;
	TWeakObjectPtr< AUsdStageActor > UsdStageActor;

	FText GetRootLayerDisplayName() const { return StageInfos.RootLayerDisplayName; }
	FText GetMetersPerUnit() const;

	void OnMetersPerUnitCommitted( const FText& InUnitsPerMeterText, ETextCommit::Type InCommitInfo );
};

#endif // #if USE_USD_SDK
