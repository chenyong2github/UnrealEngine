// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoneIndices.h"
#include "Containers/UnrealString.h"
#include "GPUSkinPublicDefs.h"
#include "Math/NumericLimits.h"
#include "Serialization/Archive.h"
#include "Templates/TypeHash.h"

/**
The maximum number of inline bone weights.
*/
static constexpr int32 MaxInlineBoneWeightCount = MAX_TOTAL_INFLUENCES;

class FBoneWeight
{
public:
	/**
	* Default constructor. Deliberately leaves the member values uninitialized for performance.
	*/
	FBoneWeight() = default;

	FBoneWeight(const FBoneWeight&) = default;
	FBoneWeight(FBoneWeight&&) noexcept = default;

	FBoneWeight& operator=(const FBoneWeight&) = default;
	FBoneWeight& operator=(FBoneWeight&&) = default;

	/**
	* Returns true if this object's bone weight values are equal to the other container's values.
	* @note Only equality comparison are supported. Relational comparisons are meaningless.
	*/
	bool operator==(const FBoneWeight& InBoneWeight) const
	{
		return BoneIndex == InBoneWeight.BoneIndex && RawWeight == InBoneWeight.RawWeight;
	}

	/**
	* Returns true if this object's bone weight values are not equal to the other container's values.
	* @note Only equality comparison are supported. Relational comparisons are meaningless.
	*/
	bool operator!=(const FBoneWeight& InBoneWeight) const
	{
		return BoneIndex != InBoneWeight.BoneIndex || RawWeight != InBoneWeight.RawWeight;
	}

	/**
	* The maximum raw weight value for a bone influence.
	*/
	static constexpr int32 MaxRawWeight = TNumericLimits<uint16>::Max();

	/**
	* A constructor for old-style bone weights where the weight was stored as an unsigned eight
	* bit integer.
	*/
	explicit FBoneWeight(FBoneIndexType InBoneIndex, uint8 InWeight)
	    : BoneIndex(InBoneIndex), RawWeight((InWeight << 8) | InWeight)
	{
	}

	/**
	* A constructor for bone weights where the weight is stored as an unsigned sixteen
	* bit integer. This is the natural storage format for this container.
	*/
	explicit FBoneWeight(FBoneIndexType InBoneIndex, uint16 InRawWeight)
	    : BoneIndex(InBoneIndex), RawWeight(InRawWeight)
	{
	}

	/**
	* A constructor for bone weights which converts the weight from a float value in the
	* [0-1] range. Values outside of that range are clamped to the range.
	*/
	explicit FBoneWeight(FBoneIndexType InBoneIndex, float InWeight)
	    : BoneIndex(InBoneIndex)
	{
		SetWeight(InWeight);
	}

	/**
	* Set the bone stored index.
	*/
	void SetBoneIndex(FBoneIndexType InBoneIndex)
	{
		BoneIndex = InBoneIndex;
	}

	/**
	* Returns the bone stored index.
	*/
	FBoneIndexType GetBoneIndex() const
	{
		return BoneIndex;
	}

	/**
	* Set stored weight as a float. Values outside the [0-1] range are clamped to that range.
	* Undefined float values will result in an undefined weight.
	*/
	void SetWeight(float InWeight)
	{
		InWeight = FMath::Clamp(InWeight, 0.0f, 1.0f);
		RawWeight = uint16(InWeight * float(MaxRawWeight) + 0.5f);
	}

	/**
	* Returns the stored weight value as a float in the [0-1] range.
	*/
	float GetWeight() const
	{
		return RawWeight / float(MaxRawWeight);
	}

	/**
	* Set the stored weight in the raw format. This avoids any floating point conversion.
	*/
	void SetRawWeight(uint16 InRawWeight)
	{
		RawWeight = InRawWeight;
	}

	/**
	* Returns the stored weight in the container's raw format, avoiding floating point
	* conversion.
	*/
	uint16 GetRawWeight() const
	{
		return RawWeight;
	}

	friend FArchive& operator<<(FArchive& Ar, FBoneWeight& V)
	{
		Ar << V.BoneIndex;
		Ar << V.RawWeight;

		return Ar;
	}

	/**
	* A helper function to return a human-readable version of the bone weight.
	*/
	FString ToString() const
	{
		return FString::Printf(TEXT("<%d, %g>"), BoneIndex, GetWeight());
	}

private:
	friend uint32 GetTypeHash(const FBoneWeight& InBoneWeight);

	FBoneIndexType BoneIndex;
	uint16 RawWeight;
};

/**
* A hashing function to allow the FBoneWeight class to be used with hashing containers (e.g.
* TSet or TMap).
*/
inline uint32 GetTypeHash(const FBoneWeight& InBoneWeight)
{
	return HashCombine(GetTypeHash(uint16(InBoneWeight.BoneIndex)),
	    GetTypeHash(InBoneWeight.RawWeight));
}


/** Specifies the method for which the bone weights are normalized after the bone weight
  * list is modified.
  */
enum class EBoneWeightNormalizeType
{
	/** No normalization is performed. The sum of the weight values can exceed 1.0 */
	None,

	/** Normalization is only performed if the sum of the weights exceeds 1.0 */
	AboveOne,

	/** Normalization is always performed such that the sum of the weights is always equal to 1.0 */
	Always
};


class FBoneWeightsSettings
{
public:
	FBoneWeightsSettings() = default;

	/** Set the normalization type when manipulating the weight values in FBoneWeights */
	FBoneWeightsSettings& SetNormalizeType(EBoneWeightNormalizeType InNormalizeType)
	{
		NormalizeType = InNormalizeType;
		return *this;
	}

	/** Returns the current normalization type for these settings */
	EBoneWeightNormalizeType GetNormalizeType() const { return NormalizeType; }

	/** Sets the maximum number of weights that can be applied to a single FBoneWeights object.
	  * When weights are added, the smallest weights, past this limit, are dropped.
	  */
	FBoneWeightsSettings& SetMaxWeightCount(int32 InMaxWeightCount)
	{
		MaxWeightCount = FMath::Max(1, InMaxWeightCount);
		return *this;
	}

	/** Returns the maximum number of weights for these settings */
	int32 GetMaxWeightCount() const { return MaxWeightCount; }

	/** Sets the minimum influence allowed. Any bone weight value below this limit will be
	  * ignored. The threshold value is clamped to the half-closed interval (0, 1] since
	  * weight values of zero indicate no influence at all and are ignored completely.
	  */
	FBoneWeightsSettings& SetWeightThreshold(float InWeightThreshold)
	{
		InWeightThreshold = FMath::Clamp(InWeightThreshold, 0.0f, 1.0f);
		WeightThreshold = uint16(InWeightThreshold * FBoneWeight::MaxRawWeight + 0.5f);
		WeightThreshold = FMath::Max(WeightThreshold, uint16(1));
		return *this;
	}

	/** Returns the weight threshold as a float value between (0, 1]. */
	float GetWeightThreshold() const
	{
		return WeightThreshold / float(TNumericLimits<uint16>::Max());
	}

	/** Returns the raw weight threshold. This is the value used internally for weight
	  * computation.
	  */
	uint16 GetRawWeightThreshold() const
	{
		return WeightThreshold;
	}

private:
	EBoneWeightNormalizeType NormalizeType = EBoneWeightNormalizeType::Always;
	int32 MaxWeightCount = MaxInlineBoneWeightCount;
	uint16 WeightThreshold = 257; // == uint8(1) converted to 16 bit using 'v | v << 8'.
};

/// A simple container for per-vertex influence of bones and their weights.
class FBoneWeights
{
	using BoneWeightsTempAllocatorT = TInlineAllocator<MaxInlineBoneWeightCount>;
	using BoneWeightArrayT = TArray<FBoneWeight, BoneWeightsTempAllocatorT>;

public:
	FBoneWeights() = default;

	/**
	  * Returns true if all of this container's values and count are equal to the other
	  * container's values and count.
	  * @note Only equality comparison are supported. Relational comparisons are meaningless.
	  */
	bool operator==(const FBoneWeights& InBoneWeight) const
	{
		return BoneWeights == InBoneWeight.BoneWeights;
	}

	/**
	* Returns true if this container's values are equal to the other container's values.
	  @note Only equality comparison are supported. Relational comparisons are meaningless.
	*/
	bool operator!=(const FBoneWeights& InBoneWeight) const
	{
		return BoneWeights != InBoneWeight.BoneWeights;
	}

	/** Set a new bone weight. If an existing weight exists with the same bone index, it's
  	  * weight value is replaced with the weight value of the given entry. Otherwise a 
      * new index is added. In both cases the new entry is subject to the given settings, which
	  * may influence what the entry's final weight value is and whether it gets thrown away
      * for not passing the threshold value. If the weight was successfully incorporated, then
	  * this function returns true. Otherwise it returns false.
	  */
	ANIMATIONCORE_API bool SetBoneWeight(
	    FBoneWeight InBoneWeight,
	    const FBoneWeightsSettings& InSettings = {});

	bool SetBoneWeight(
	    FBoneIndexType InBone,
	    float InWeight,
	    const FBoneWeightsSettings& InSettings = {})
	{
		return SetBoneWeight(FBoneWeight{InBone, InWeight}, InSettings);
	}

	/** Removes a specific bone from the list of weights, re-normalizing and pruning bones,
	  * if needed.
	  */
	ANIMATIONCORE_API bool RemoveBoneWeight(
	    FBoneIndexType InBone,
	    const FBoneWeightsSettings& InSettings = {});

	/** Force normalization of weights. This is useful if a set of operations were performed
	  * with no normalization, for efficiency, and normalization is needed post-operation.
	  */
	ANIMATIONCORE_API void Renormalize(const FBoneWeightsSettings& InSettings = {});

	/** A helper to create a FBoneWeights container from FSoftSkinVertex data structure. */
	ANIMATIONCORE_API static FBoneWeights Create(
	    const FBoneIndexType InBones[MaxInlineBoneWeightCount],
	    const uint8 InWeights[MaxInlineBoneWeightCount],
	    const FBoneWeightsSettings& InSettings = {});

	/** A helper to create a FBoneWeights container from separated bone index and weight arrays.
	  * The size of the two arrays *must* be the same -- otherwise the behavior is undefined.
	  */
	ANIMATIONCORE_API static FBoneWeights Create(
	    const FBoneIndexType* InBones,
	    const float* InWeights,
	    int32 NumEntries,
	    const FBoneWeightsSettings& InSettings = {});

	/** A helper to create FBoneWeights container from a TArray of FBoneWeight objects. */
	ANIMATIONCORE_API static FBoneWeights Create(
	    TArrayView<const FBoneWeight> BoneWeights,
	    const FBoneWeightsSettings& InSettings = {});

	/** Blend two bone weights together, making sure to add in every influence from both,
	  * using the given settings. The bias value should lie on the [0,1] interval. Values outside
	  * that range may give unwanted results. 
	  */
	ANIMATIONCORE_API static FBoneWeights Blend(
	    const FBoneWeights& InA,
	    const FBoneWeights& InB,
	    float InBias,
	    const FBoneWeightsSettings& InSettings = {});

	// Ranged-based for loop compatibility -- but only the const version.
	using RangedForConstIteratorType = BoneWeightArrayT::RangedForConstIteratorType;

	RangedForConstIteratorType begin() const { return BoneWeights.begin(); }
	RangedForConstIteratorType end() const { return BoneWeights.end(); }

	/** Return the number of bone weights in this container.
	  * @return The number of bone weights.
	  */
	int32 Num() const { return BoneWeights.Num(); }

	/** Return the weight at index Index. Using an index value less than zero -- or equal
	  * to or greater than the result of Num() -- is an undefined operation.
	  * @param Index The index of the desired bone weight value.
	  * @return The bone weight value for that index, or undefined if outside the valid range.
	  */
	const FBoneWeight& operator[](int32 Index) { return BoneWeights.operator[](Index); }


	// -- Helper functions

	/** Find the bone weight corresponding to the given bone index. If a bone weight with this
	  * index doesn't exist
	  */
	int32 FindWeightIndexByBone(FBoneIndexType InBoneIndex) const
	{
		return BoneWeights.IndexOfByPredicate(
		    [InBoneIndex](const FBoneWeight& BW) { return InBoneIndex == BW.GetBoneIndex(); });
	}

	friend FArchive& operator<<(FArchive& Ar, FBoneWeights& V)
	{
		Ar << V.BoneWeights;

		return Ar;
	}

	FString ToString() const
	{
		FString Result(TEXT("["));
		Result.Append(FString::JoinBy(BoneWeights, TEXT(", "), [](const FBoneWeight& V) { return V.ToString(); }));
		Result.Append(TEXT("]"));
		return Result;
	}

private:
	static FBoneWeights CreateFromArrayView(
	    TArrayView<FBoneWeight> BoneWeights,
	    const FBoneWeightsSettings& InSettings = {});

	void SortWeights();
	bool CullWeights(const FBoneWeightsSettings& InSettings);
	void NormalizeWeights(EBoneWeightNormalizeType InNormalizeType);

	friend uint32 GetTypeHash(const FBoneWeights& InBoneWeights);

	/// List of bone weights, in order of descending weight.
	BoneWeightArrayT BoneWeights;
};

inline uint32 GetTypeHash(const FBoneWeights& InBoneWeights)
{
	uint32 Hash = GetTypeHash(InBoneWeights.BoneWeights.Num());
	for (const FBoneWeight& BoneWeight : InBoneWeights)
	{
		Hash = HashCombine(Hash, GetTypeHash(BoneWeight));
	}
	return Hash;
}
