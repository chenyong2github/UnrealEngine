// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AUsdStageActor;
class FUsdLevelSequenceHelperImpl;
class ULevelSequence;
class UUsdPrimTwin;

namespace UE
{
	class FUsdStage;
}

/**
 * Builds and maintains the level sequence and subsequences for a Usd Stage
 */
class USDSTAGE_API FUsdLevelSequenceHelper
{
public:
	FUsdLevelSequenceHelper();
	UE_DEPRECATED(4.27, "This function is deprecated, use the default constructor and call Init and BindToUsdStageActor(optional) instead.")
	explicit FUsdLevelSequenceHelper(TWeakObjectPtr<AUsdStageActor> InStageActor);
	virtual ~FUsdLevelSequenceHelper();

	// Copy semantics are there for convenience only. Copied FUsdLevelSequenceHelper are empty and require a call to Init().
	FUsdLevelSequenceHelper(const FUsdLevelSequenceHelper& Other);
	FUsdLevelSequenceHelper& operator=(const FUsdLevelSequenceHelper& Other);

	FUsdLevelSequenceHelper(FUsdLevelSequenceHelper&& Other);
	FUsdLevelSequenceHelper& operator=(FUsdLevelSequenceHelper&& Other);

public:
	/** Creates the main level sequence and subsequences from the usd stage layers */
	ULevelSequence* Init(const UE::FUsdStage& UsdStage);

	/** Resets the helper, abandoning all managed LevelSequences */
	void Clear();

	UE_DEPRECATED(4.27, "This function is deprecated, use Init instead.")
	void InitLevelSequence(const UE::FUsdStage& UsdStage);

	/** Creates the time track for the StageActor */
	void BindToUsdStageActor(AUsdStageActor* StageActor);
	void UnbindFromUsdStageActor();

	/** Adds the necessary tracks for a given prim to the level sequence */
	void AddPrim(UUsdPrimTwin& PrimTwin);

	/** Removes any track associated with this prim */
	void RemovePrim(const UUsdPrimTwin& PrimTwin);

	/** Blocks updating the level sequences & tracks from object changes. */
	void StartMonitoringChanges();
	void StopMonitoringChanges();
	void BlockMonitoringChangesForThisTransaction();

	ULevelSequence* GetMainLevelSequence() const;
	TArray< ULevelSequence* > GetSubSequences() const;

private:
	TUniquePtr<FUsdLevelSequenceHelperImpl> UsdSequencerImpl;
};
