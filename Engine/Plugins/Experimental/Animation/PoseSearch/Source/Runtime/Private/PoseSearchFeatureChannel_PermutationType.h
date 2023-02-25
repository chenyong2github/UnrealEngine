// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearchFeatureChannel_Group.h"
#include "PoseSearchFeatureChannel_PermutationType.generated.h"

UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EPermutationType : uint8
{
	UseOriginTime,
	UsePermutationTime,
	UseOriginToPermutationTime,
};

UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "Permutation Type Channel"), CollapseCategories)
class POSESEARCH_API UPoseSearchFeatureChannel_PermutationType : public UPoseSearchFeatureChannel_GroupBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	EPermutationType PermutationType = EPermutationType::UseOriginToPermutationTime;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Settings")
	TArray<TObjectPtr<UPoseSearchFeatureChannel>> SubChannels;

	// UPoseSearchFeatureChannel_GroupBase interface
	virtual TArrayView<TObjectPtr<UPoseSearchFeatureChannel>> GetSubChannels() override { return SubChannels; }
	virtual TConstArrayView<TObjectPtr<UPoseSearchFeatureChannel>> GetSubChannels() const override { return SubChannels; }

	// UPoseSearchFeatureChannel interface
	virtual void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const override;

#if WITH_EDITOR
	virtual void IndexAsset(UE::PoseSearch::FAssetIndexer& Indexer) const override;
	virtual FString GetLabel() const override;
#endif
};


