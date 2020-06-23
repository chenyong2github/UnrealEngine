// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


class AUsdStageActor;
class FUsdLevelSequenceHelperImpl;

namespace UE
{
	class FUsdStage;
}

class FUsdLevelSequenceHelper
{
public:
	FUsdLevelSequenceHelper();
	explicit FUsdLevelSequenceHelper(TWeakObjectPtr<AUsdStageActor> InStageActor);
	virtual ~FUsdLevelSequenceHelper();

	void InitLevelSequence(const UE::FUsdStage& UsdStage);
	void UpdateLevelSequence(const UE::FUsdStage& UsdStage);

private:
	TUniquePtr<FUsdLevelSequenceHelperImpl> UsdSequencerImpl;
};