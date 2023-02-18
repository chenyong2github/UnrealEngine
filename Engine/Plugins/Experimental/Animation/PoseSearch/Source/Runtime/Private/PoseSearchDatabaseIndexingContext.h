// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "PoseSearch/PoseSearchAssetIndexer.h"
#include "PoseSearch/PoseSearchAssetSampler.h"

struct FPoseSearchIndexBase;
class UPoseSearchDatabase;

namespace UE::PoseSearch
{

struct FDatabaseIndexingContext
{
	FPoseSearchIndexBase* SearchIndexBase = nullptr;
	FAssetSamplingContext SamplingContext;
	TArray<TSharedPtr<FAssetSamplerBase>> Samplers;
	TArray<FAssetIndexer> Indexers;

	void Prepare(const UPoseSearchDatabase* Database);
	bool IndexAssets();
	void JoinIndex();
	float CalculateMinCostAddend() const;
};

} // namespace UE::PoseSearch

#endif // WITH_EDITOR