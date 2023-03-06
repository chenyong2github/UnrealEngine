// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoHash.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "DrawDebugHelpers.h"
#include "PoseSearchFeatureChannel.generated.h"

class UPoseSearchSchema;
struct FPoseSearchPoseMetadata;
struct FPoseSearchFeatureVectorBuilder;

UENUM()
enum class EComponentStrippingVector : uint8
{
	// no component stripping
	None,

	// stripping X and Y components (matching only on the horizontal plane) 
	StripXY,

	// stripping Z (matching only vertically - caring only about the height of the feature) 
	StripZ,
};

UENUM()
enum class EInputQueryPose : uint8
{
	// use character pose to compose the query
	UseCharacterPose,

	// if available reuse continuing pose from the database to compose the query or else UseCharacterPose
	UseContinuingPose,

	// if available reuse and interpolate continuing pose from the database to compose the query or else UseCharacterPose
	UseInterpolatedContinuingPose,
};

namespace UE::PoseSearch
{

struct FDebugDrawParams;
struct FSearchContext;

#if WITH_EDITOR
class FAssetIndexer;
#endif // WITH_EDITOR

/** Helper class for extracting and encoding features into a float buffer */
class POSESEARCH_API FFeatureVectorHelper
{
public:
	static int32 GetVectorCardinality(EComponentStrippingVector ComponentStrippingVector);
	static void EncodeVector(TArrayView<float> Values, int32 DataOffset, const FVector& Vector, EComponentStrippingVector ComponentStrippingVector);
	static void EncodeVector(TArrayView<float> Values, int32 DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue, bool bNormalize, EComponentStrippingVector ComponentStrippingVector);
	static FVector DecodeVector(TConstArrayView<float> Values, int32 DataOffset, EComponentStrippingVector ComponentStrippingVector);

	static void EncodeVector2D(TArrayView<float> Values, int32 DataOffset, const FVector2D& Vector2D);
	static void EncodeVector2D(TArrayView<float> Values, int32 DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue);
	static FVector2D DecodeVector2D(TConstArrayView<float> Values, int32 DataOffset);

	static void EncodeFloat(TArrayView<float> Values, int32 DataOffset, const float Value);
	static void EncodeFloat(TArrayView<float> Values, int32 DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue);
	static float DecodeFloat(TConstArrayView<float> Values, int32 DataOffset);
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
	virtual void Finalize(UPoseSearchSchema* Schema) PURE_VIRTUAL(UPoseSearchFeatureChannel::Finalize, );
	
	// Called at runtime to add this channel's data to the query pose vector
	virtual void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const PURE_VIRTUAL(UPoseSearchFeatureChannel::BuildQuery, );

	// UPoseSearchFeatureChannels can hold sub channels
	virtual TArrayView<TObjectPtr<UPoseSearchFeatureChannel>> GetSubChannels() { return TArrayView<TObjectPtr<UPoseSearchFeatureChannel>>(); }
	virtual TConstArrayView<TObjectPtr<UPoseSearchFeatureChannel>> GetSubChannels() const { return TConstArrayView<TObjectPtr<UPoseSearchFeatureChannel>>(); }

	// @todo: should this API be under ENABLE_DRAW_DEBUG?
	virtual void AddDependentChannels(UPoseSearchSchema* Schema) const {}

#if ENABLE_DRAW_DEBUG
	// API called before DebugDraw to collect shared channel informations such as decoded positions form the PoseVector
	virtual void PreDebugDraw(UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const {}

	// Draw this channel's data for the given pose vector
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const {}
#endif //ENABLE_DRAW_DEBUG

#if WITH_EDITOR
	// Called at database build time to collect feature weights.
	// Weights is sized to the cardinality of the schema and the feature channel should write
	// its weights at the channel's data offset. Channels should provide a weight for each dimension.
	virtual void FillWeights(TArray<float>& Weights) const PURE_VIRTUAL(UPoseSearchFeatureChannel::FillWeights, );

	// Called at database build time to populate pose vectors with this channel's data
	virtual void IndexAsset(UE::PoseSearch::FAssetIndexer& Indexer) const PURE_VIRTUAL(UPoseSearchFeatureChannel::IndexAsset, );

	// returns the FString used editor side to identify this UPoseSearchFeatureChannel (for instance in the pose search debugger)
	virtual FString GetLabel() const;
	virtual bool CanBeNormalizedWith(const UPoseSearchFeatureChannel* Other) const;
	const UPoseSearchSchema* GetSchema() const;
#endif

private:
	// IBoneReferenceSkeletonProvider interface
	// Note this function is exclusively for FBoneReference details customization
	USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

protected:
	friend class ::UPoseSearchSchema;

	UPROPERTY(Transient)
	int32 ChannelDataOffset = INDEX_NONE;

	UPROPERTY(Transient)
	int32 ChannelCardinality = INDEX_NONE;
};