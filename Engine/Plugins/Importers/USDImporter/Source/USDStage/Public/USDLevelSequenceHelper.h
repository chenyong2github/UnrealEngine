// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AUsdStageActor;
class FUsdLevelSequenceHelperImpl;
class UUsdPrimTwin;

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

	/** Adds the necessary tracks for a given prim to the level sequence */
	void AddPrim(UUsdPrimTwin& PrimTwin);

	/** Removes any track associated with this prim */
	void RemovePrim(const UUsdPrimTwin& PrimTwin);

	void StartMonitoringChanges();
	void StopMonitoringChanges();

private:
	TUniquePtr<FUsdLevelSequenceHelperImpl> UsdSequencerImpl;
};
