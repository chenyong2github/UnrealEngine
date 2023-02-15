// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "InstancedStruct.h"
#include "PoseSearch/PoseSearchAssetSampler.h"
#include "PoseSearchAssetIndexer.h"

struct FPoseSearchIndexBase;
class UPoseSearchDatabase;

namespace UE::PoseSearch
{

struct FDatabaseIndexingContext
{
	FPoseSearchIndexBase* SearchIndexBase = nullptr;
	FAssetSamplingContext SamplingContext;
	TArray<FInstancedStruct> Samplers;
	TArray<FAssetIndexer> Indexers;

	void Prepare(const UPoseSearchDatabase* Database);
	bool IndexAssets();
	void JoinIndex();
	float CalculateMinCostAddend() const;
};

} // namespace UE::PoseSearch

#endif // WITH_EDITOR