// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * EMeshResult is returned by various mesh/graph operations to either indicate success,
 * or communicate which type of error ocurred (some errors are recoverable, and some not).
 */
enum class EMeshResult
{
	Ok = 0,
	Failed_NotAVertex = 1,
	Failed_NotATriangle = 2,
	Failed_NotAnEdge = 3,

	Failed_BrokenTopology = 10,
	Failed_HitValenceLimit = 11,

	Failed_IsBoundaryEdge = 20,
	Failed_FlippedEdgeExists = 21,
	Failed_IsBowtieVertex = 22,
	Failed_InvalidNeighbourhood = 23,       // these are all failures for CollapseEdge
	Failed_FoundDuplicateTriangle = 24,
	Failed_CollapseTetrahedron = 25,
	Failed_CollapseTriangle = 26,
	Failed_NotABoundaryEdge = 27,
	Failed_SameOrientation = 28,

	Failed_WouldCreateBowtie = 30,
	Failed_VertexAlreadyExists = 31,
	Failed_CannotAllocateVertex = 32,
	Failed_VertexStillReferenced = 33,

	Failed_WouldCreateNonmanifoldEdge = 50,
	Failed_TriangleAlreadyExists = 51,
	Failed_CannotAllocateTriangle = 52,

	Failed_UnrecoverableError = 1000
};



/**
 * EOperationValidationResult is meant to be returned by Validate() functions of 
 * Operation classes (eg like ExtrudeMesh, etc) to indicate whether the operation
 * can be successfully applied.
 */
enum class EOperationValidationResult
{
	Ok = 0,
	Failed_UnknownReason = 1,

	Failed_InvalidTopology = 2
};


/**
 * EValidityCheckFailMode is passed to CheckValidity() functions of various classes 
 * to specify how validity checks should fail.
 */
enum class EValidityCheckFailMode 
{ 
	/** Function returns false if a failure is encountered */
	ReturnOnly = 0,
	/** Function check()'s if a failure is encountered */
	Check = 1,
	/** Function ensure()'s if a failure is encountered */
	Ensure = 2
};





/**
 * TIndexMap stores mappings between indices, which are assumed to be an integer type.
 * Both forward and backward mapping are stored
 *
 * @todo make either mapping optional
 * @todo optionally support using flat arrays instead of TMaps
 * @todo constructors that pick between flat array and TMap modes depending on potential size/sparsity of mapped range
 * @todo identity and shift modes that don't actually store anything
 */
template<typename IntType>
struct TIndexMap
{
protected:
	TMap<IntType, IntType> ForwardMap;
	TMap<IntType, IntType> ReverseMap;
	bool bWantForward;
	bool bWantReverse;
	IntType InvalidID;

public:

	TIndexMap()
	{
		bWantForward = bWantReverse = true;
		InvalidID = (IntType)-9999999;
	}

	void Reset()
	{
		ForwardMap.Reset();
		ReverseMap.Reset();
	}

	/** @return the value used to indicate "invalid" in the mapping */
	inline IntType GetInvalidID() const { return InvalidID; }

	TMap<IntType, IntType>& GetForwardMap() { return ForwardMap; }
	const TMap<IntType, IntType>& GetForwardMap() const { return ForwardMap; }

	TMap<IntType, IntType>& GetReverseMap() { return ReverseMap; }
	const TMap<IntType, IntType>& GetReverseMap() const { return ReverseMap; }

	/** add mapping from one index to another */
	inline void Add(IntType FromID, IntType ToID)
	{
		ForwardMap.Add(FromID, ToID);
		ReverseMap.Add(ToID, FromID);
	}

	/** @return true if we can map forward from this value */
	inline bool ContainsFrom(IntType FromID) const
	{
		check(bWantForward);
		return ForwardMap.Contains(FromID);
	}

	/** @return true if we can reverse-map from this value */
	inline bool ContainsTo(IntType ToID) const
	{
		check(bWantReverse);
		return ReverseMap.Contains(ToID);
	}


	/** @return forward-map of input value */
	inline IntType GetTo(IntType FromID) const
	{
		check(bWantForward);
		const IntType* FoundVal = ForwardMap.Find(FromID);
		return (FoundVal == nullptr) ? InvalidID : *FoundVal;
	}

	/** @return reverse-map of input value */
	inline IntType GetFrom(IntType ToID) const
	{
		check(bWantReverse);
		const IntType* FoundVal = ReverseMap.Find(ToID);
		return (FoundVal == nullptr) ? InvalidID : *FoundVal;
	}

	/** @return forward-map of input value or null if not found */
	inline const IntType* FindTo(IntType FromID) const
	{
		check(bWantForward);
		return ForwardMap.Find(FromID);
	}

	/** @return reverse-map of input value or null if not found */
	inline const IntType* FindFrom(IntType ToID) const
	{
		check(bWantReverse);
		return ReverseMap.Find(ToID);
	}


	void Reserve(int NumElements)
	{
		if (bWantForward)
		{
			ForwardMap.Reserve(NumElements);
		}
		if (bWantReverse)
		{
			ReverseMap.Reserve(NumElements);
		}
	}
};
typedef TIndexMap<int> FIndexMapi;


