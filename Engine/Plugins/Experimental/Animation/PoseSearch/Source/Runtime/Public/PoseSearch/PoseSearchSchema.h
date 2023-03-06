// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoneContainer.h"
#include "Engine/DataAsset.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearchSchema.generated.h"

struct FBoneReference;
class UMirrorDataTable;

UENUM()
enum class EPoseSearchDataPreprocessor : int32
{
	None,
	Normalize,
	NormalizeOnlyByDeviation,

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

USTRUCT()
struct FPoseSearchSchemaColorPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Colors", meta = (ExcludeFromHash))
	FLinearColor Query = FLinearColor::Blue;

	UPROPERTY(EditAnywhere, Category = "Colors", meta = (ExcludeFromHash))
	FLinearColor Result = FLinearColor::Yellow;
};

/**
* Specifies the format of a pose search index. At runtime, queries are built according to the schema for searching.
*/
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental, meta = (DisplayName = "Motion Database Config"), CollapseCategories)
class POSESEARCH_API UPoseSearchSchema : public UDataAsset, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:
	// @todo: used only for indexing: cache it somewhere else
	UPROPERTY(EditAnywhere, Category = "Schema")
	TObjectPtr<USkeleton> Skeleton;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "240"), Category = "Schema")
	int32 SampleRate = 30;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Schema")
	TArray<TObjectPtr<UPoseSearchFeatureChannel>> Channels;

	// FinalizedChannels gets populated with UPoseSearchFeatureChannel(s) from Channels and additional injected ones during the Finalize
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPoseSearchFeatureChannel>> FinalizedChannels;

	// If set, this schema will support mirroring pose search databases
	UPROPERTY(EditAnywhere, Category = "Schema")
	TObjectPtr<UMirrorDataTable> MirrorDataTable;

	UPROPERTY(EditAnywhere, Category = "Schema")
	EPoseSearchDataPreprocessor DataPreprocessor = EPoseSearchDataPreprocessor::Normalize;

	UPROPERTY(Transient)
	int32 SchemaCardinality = 0;

	// @todo: used only for indexing: cache it somewhere else
	UPROPERTY(Transient)
	TArray<FBoneReference> BoneReferences;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndicesWithParents;

	// cost added to the continuing pose from databases that uses this schema
	UPROPERTY(EditAnywhere, Category = "Bias")
	float ContinuingPoseCostBias = 0.f;

	// base cost added to all poses from databases that uses this schema. it can be overridden by UAnimNotifyState_PoseSearchModifyCost
	UPROPERTY(EditAnywhere, Category = "Bias")
	float BaseCostBias = 0.f;

	// If there's a mirroring mismatch between the currently playing asset and a search candidate, this cost will be 
	// added to the candidate, making it less likely to be selected
	UPROPERTY(EditAnywhere, Category = "Bias")
	float MirrorMismatchCostBias = 0.f;

	// cost added to all poses from looping assets of databases that uses this schema
	UPROPERTY(EditAnywhere, Category = "Bias")
	float LoopingCostBias = 0.f;

	// how many times the animation assets of the database using this schema will be indexed
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1"), Category = "Permutations")
	int32 NumberOfPermutations = 1;

	// delta time between every permutation indexing
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "240"), Category = "Permutations")
	int32 PermutationsSampleRate = 30;

	// starting offset of the "PermutationTime" from the "SamplingTime" of the first permutation.
	// subsequent permutations will have PermutationTime = SamplingTime + PermutationsTimeOffset + PermutationIndex / PermutationsSampleRate
	UPROPERTY(EditAnywhere, Category = "Permutations")
	float PermutationsTimeOffset = 0.f;

	UPROPERTY(EditAnywhere, Category = "Debug", meta = (ExcludeFromHash))
	TArray<FPoseSearchSchemaColorPreset> ColorPresets;
	
	// if bInjectAdditionalDebugChannels is true, channels will be asked to injecting additional channels into this schema.
	// the original intent is to add UPoseSearchFeatureChannel_Position(s) to help with the complexity of the debug drawing
	// (the database will have all the necessary positions to draw lines at the right location and time)
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bInjectAdditionalDebugChannels;

	bool IsValid () const;

	float GetSamplingInterval() const { return 1.0f / SampleRate; }

	TConstArrayView<TObjectPtr<UPoseSearchFeatureChannel>> GetChannels() const { return FinalizedChannels; }

	template <typename FindPredicateType>
	const UPoseSearchFeatureChannel* FindChannel(FindPredicateType FindPredicate) const
	{
		return FindChannelRecursive(GetChannels(), FindPredicate);
	}

	template<typename ChannelType>
	const ChannelType* FindFirstChannelOfType() const
	{
		return static_cast<const ChannelType*>(FindChannel([this](const UPoseSearchFeatureChannel* Channel) -> const UPoseSearchFeatureChannel* { return Cast<ChannelType>(Channel); }));
	}

	// UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostLoad() override;

	int8 AddBoneReference(const FBoneReference& BoneReference);

	// IBoneReferenceSkeletonProvider
	USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const;

	FBoneIndexType GetBoneIndexType(int8 SchemaBoneIdx) const;

	bool IsRootBone(int8 SchemaBoneIdx) const;
	
private:
	template <typename FindPredicateType>
	static const UPoseSearchFeatureChannel* FindChannelRecursive(TConstArrayView<TObjectPtr<UPoseSearchFeatureChannel>> Channels, FindPredicateType FindPredicate)
	{
		for (const TObjectPtr<UPoseSearchFeatureChannel>& ChannelPtr : Channels)
		{
			if (ChannelPtr)
			{
				if (const UPoseSearchFeatureChannel* Channel = FindPredicate(ChannelPtr))
				{
					return Channel;
				}

				if (const UPoseSearchFeatureChannel* Channel = FindChannelRecursive(ChannelPtr->GetSubChannels(), FindPredicate))
				{
					return Channel;
				}
			}
		}
		return nullptr;
	}

	void Finalize();
	void ResolveBoneReferences();
};
