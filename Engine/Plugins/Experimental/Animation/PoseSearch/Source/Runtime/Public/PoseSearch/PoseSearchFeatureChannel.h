// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoHash.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "PoseSearchFeatureChannel.generated.h"

class UPoseSearchSchema;
struct FPoseSearchPoseMetadata;
struct FPoseSearchFeatureVectorBuilder;

UENUM(BlueprintType)
enum class EInputQueryPose : uint8
{
	// use character pose to compose the query
	UseCharacterPose,

	// if available reuse continuing pose from the database to compose the query or else UseCharacterPose
	UseContinuingPose,

	// if available reuse and interpolate continuing pose from the database to compose the query or else UseCharacterPose
	UseInterpolatedContinuingPose,

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

namespace UE::PoseSearch
{

struct FAssetIndexingOutput;
struct FDebugDrawParams;
struct FSearchContext;
class IAssetIndexer;

#if WITH_EDITOR
// data structure collecting the internal layout representation of UPoseSearchFeatureChannel,
// so we can aggregate data from different FPoseSearchIndex and calculate mean deviation with a homogeneous data set (ComputeChannelsDeviations
struct FFeatureChannelLayoutSet
{
	// data structure holding DataOffset and Cardinality to find the data of the DebugName (channel breakdown / layout) in the related SearchIndexBases[SchemaIndex]
	struct FEntry
	{
		FString DebugName; // for easier debugging
		int32 SchemaIndex = -1; // index of the associated Schemas / SearchIndexBases used as input of the algorithm
		int32 DataOffset = -1; // data offset from the base of SearchIndexBases[SchemaIndex].Values.GetData() from where the data associated to this Item starts
		int32 Cardinality = -1; // data cardinality
	};

	// FIoHash is the hash associated to the channel data breakdown (e.g.: it could be a single SampledBones at a specific SampleTimes for a UPoseSearchFeatureChannel_Pose)
	TMap<FIoHash, TArray<FEntry>> EntriesMap;
	int32 CurrentSchemaIndex = -1;
	TWeakObjectPtr<const UPoseSearchSchema> CurrentSchema;

	void Add(FString DebugName, FIoHash Id, int32 DataOffset, int32 Cardinality)
	{
		check(DataOffset >= 0 && Cardinality >= 0 && CurrentSchemaIndex >= 0);
		TArray<FEntry>& Entries = EntriesMap.FindOrAdd(Id);

		// making sure all the FEntry associated with the same Id have the same Cardinality
		check(Entries.IsEmpty() || Entries[0].Cardinality == Cardinality);
		Entries.Add({ DebugName, CurrentSchemaIndex, DataOffset, Cardinality });
	}
};
#endif // WITH_EDITOR

class POSESEARCH_API ICostBreakDownData
{
public:
	virtual ~ICostBreakDownData() {}

	// returns the size of the dataset
	virtual int32 Num() const = 0;

	// returns true if Index-th cost data vector is associated with Schema
	virtual bool IsCostVectorFromSchema(int32 Index, const UPoseSearchSchema* Schema) const = 0;

	// returns the Index-th cost data vector
	virtual TConstArrayView<float> GetCostVector(int32 Index, const UPoseSearchSchema* Schema) const = 0;

	// every breakdown section start by calling BeginBreakDownSection...
	virtual void BeginBreakDownSection(const FText& Label) = 0;

	// ...then add as many SetCostBreakDown into the section...
	virtual void SetCostBreakDown(float CostBreakDown, int32 Index, const UPoseSearchSchema* Schema) = 0;

	// ...to finally wrap the section up by calling EndBreakDownSection
	virtual void EndBreakDownSection(const FText& Label) = 0;

	// true if want the channel to be verbose and generate the cost breakdown labels
	virtual bool IsVerbose() const { return true; }

	// most common case implementation
	void AddEntireBreakDownSection(const FText& Label, const UPoseSearchSchema* Schema, int32 DataOffset, int32 Cardinality);
};

} // namespace UE::PoseSearch

class POSESEARCH_API IPoseFilter
{
public:
	virtual ~IPoseFilter() {}

	// if true this filter will be evaluated
	virtual bool IsPoseFilterActive() const { return false; }
	
	// if it returns false the pose candidate will be discarded
	virtual bool IsPoseValid(TConstArrayView<float> PoseValues, TConstArrayView<float> QueryValues, int32 PoseIdx, const FPoseSearchPoseMetadata& Metadata) const { return true; }
};

//////////////////////////////////////////////////////////////////////////
// Feature channels interface
UCLASS(Abstract, BlueprintType, EditInlineNew)
class POSESEARCH_API UPoseSearchFeatureChannel : public UObject, public IBoneReferenceSkeletonProvider, public IPoseFilter
{
	GENERATED_BODY()

public:
	int32 GetChannelCardinality() const { checkSlow(ChannelCardinality >= 0); return ChannelCardinality; }
	int32 GetChannelDataOffset() const { checkSlow(ChannelDataOffset >= 0); return ChannelDataOffset; }

	// Called during UPoseSearchSchema::Finalize to prepare the schema for this channel
	virtual void InitializeSchema(UPoseSearchSchema* Schema) PURE_VIRTUAL(UPoseSearchFeatureChannel::InitializeSchema, );
	
	// Called at database build time to collect feature weights.
	// Weights is sized to the cardinality of the schema and the feature channel should write
	// its weights at the channel's data offset. Channels should provide a weight for each dimension.
	virtual void FillWeights(TArray<float>& Weights) const PURE_VIRTUAL(UPoseSearchFeatureChannel::FillWeights, );

	// Called at database build time to populate pose vectors with this channel's data
	virtual void IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const PURE_VIRTUAL(UPoseSearchFeatureChannel::IndexAsset, );

	// Called at runtime to add this channel's data to the query pose vector
	virtual void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const PURE_VIRTUAL(UPoseSearchFeatureChannel::BuildQuery, );

	// Draw this channel's data for the given pose vector
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const PURE_VIRTUAL(UPoseSearchFeatureChannel::DebugDraw, );

#if WITH_EDITOR
	virtual void PopulateChannelLayoutSet(UE::PoseSearch::FFeatureChannelLayoutSet& FeatureChannelLayoutSet) const;
	virtual void ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData, const UPoseSearchSchema* Schema) const;
#endif

private:
	// IBoneReferenceSkeletonProvider interface
	// Note this function is exclusively for FBoneReference details customization
	class USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

protected:
	friend class ::UPoseSearchSchema;

	UPROPERTY(meta = (ExcludeFromHash))
	int32 ChannelDataOffset = INDEX_NONE;

	UPROPERTY(meta = (ExcludeFromHash))
	int32 ChannelCardinality = INDEX_NONE;
};