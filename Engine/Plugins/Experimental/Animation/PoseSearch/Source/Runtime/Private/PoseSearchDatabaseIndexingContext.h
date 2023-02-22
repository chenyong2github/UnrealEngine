// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "PoseSearch/PoseSearchAssetIndexer.h"
#include "PoseSearch/PoseSearchAssetSampler.h"

struct FPoseSearchIndexBase;
class UPoseSearchDatabase;
namespace UE::DerivedData { class FRequestOwner; }

namespace UE::PoseSearch
{

struct FDatabaseIndexingContext
{
	bool IndexDatabase(FPoseSearchIndexBase& SearchIndexBase, const UPoseSearchDatabase& Database, UE::DerivedData::FRequestOwner& Owner);

private:
	FAssetSamplingContext SamplingContext;
	TArray<TSharedPtr<FAssetSamplerBase>> Samplers;
	TArray<FAssetIndexer> Indexers;
};

} // namespace UE::PoseSearch

#endif // WITH_EDITOR