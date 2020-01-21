// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AUsdStageActor;

#if USE_USD_SDK

class SUsdStageInfo : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SUsdStageInfo ) {}
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, AUsdStageActor* UsdStageActor );
	void RefreshStageInfos( AUsdStageActor* UsdStageActor );

private:
	struct FStageInfos
	{
		FText RootLayerDisplayName;
	};

	FStageInfos StageInfos;

	FText GetRootLayerDisplayName() const { return StageInfos.RootLayerDisplayName; }
};

#endif // #if USE_USD_SDK
