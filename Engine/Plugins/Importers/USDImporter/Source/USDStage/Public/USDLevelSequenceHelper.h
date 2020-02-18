// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/sdf/changeList.h"
#include "pxr/usd/usd/stage.h"

#include "USDIncludesEnd.h"
#endif // USE_USD_SDK

class AUsdStageActor;
class FUsdLevelSequenceHelperImpl;

class FUsdLevelSequenceHelper
{
public:
	FUsdLevelSequenceHelper();
	explicit FUsdLevelSequenceHelper(TWeakObjectPtr<AUsdStageActor> InStageActor);
	virtual ~FUsdLevelSequenceHelper();

#if USE_USD_SDK
	void InitLevelSequence(const pxr::UsdStageRefPtr& UsdStage);
	void UpdateLevelSequence(const pxr::UsdStageRefPtr& UsdStage);
#endif // USE_USD_SDK

private:
	TUniquePtr<FUsdLevelSequenceHelperImpl> UsdSequencerImpl;
};