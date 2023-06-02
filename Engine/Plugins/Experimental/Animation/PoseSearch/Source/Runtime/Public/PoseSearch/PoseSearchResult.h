// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearchCost.h"

struct FPoseSearchIndexAsset;
class UPoseSearchDatabase;
class UPoseSearchSchema;

namespace UE::PoseSearch
{

/**
* float buffer of features according to a UPoseSearchSchema layout.
* FFeatureVectorBuilder is used to build search queries at runtime and for adding samples during search index construction.
*/
struct FFeatureVectorBuilder
{
public:
	void Init(const UPoseSearchSchema* Schema);
	void Reset();

	const UPoseSearchSchema* GetSchema() const { return Schema.Get(); }

	TArray<float>& EditValues() { return Values; }
	TConstArrayView<float> GetValues() const { return Values; }

private:
	TObjectPtr<const UPoseSearchSchema> Schema;
	TArray<float> Values;
};
	
struct FSearchResult
{
	// best cost of the currently selected PoseIdx (it could be equal to ContinuingPoseCost)
	FPoseSearchCost PoseCost;
	int32 PoseIdx = INDEX_NONE;

	int32 PrevPoseIdx = INDEX_NONE;
	int32 NextPoseIdx = INDEX_NONE;

	// lerp value to find AssetTime from PrevPoseIdx -> AssetTime -> NextPoseIdx, within range [-0.5, 0.5]
	float LerpValue = 0.f;

	TObjectPtr<const UPoseSearchDatabase> Database;

	float AssetTime = 0.0f;

#if WITH_EDITORONLY_DATA
	FPoseSearchCost BruteForcePoseCost;
#endif // WITH_EDITORONLY_DATA

	// Attempts to set the internal state to match the provided asset time including updating the internal DbPoseIdx. 
	// If the provided asset time is out of bounds for the currently playing asset then this function will reset the 
	// state back to the default state.
	void Update(float NewAssetTime);

	bool IsValid() const;

	void Reset();

	const FPoseSearchIndexAsset* GetSearchIndexAsset(bool bMandatory = false) const;
};

} // namespace UE::PoseSearch