// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Queue.h"
#include "Field/FieldSystemTypes.h"
#include "Math/Vector.h"

/**
* FFieldContext
*   The Context is passed into the field evaluation pipeline during evaluation. The Nodes
*   will have access to the samples and indices for evaluation. The MetaData is a optional
*   data package that the nodes will use during evaluation, the context does not assume
*   ownership of the metadata but assumes it will remain in scope during evaluation.
*/
struct CHAOS_API ContextIndex
{
	ContextIndex(int32 InSample = INDEX_NONE, int32 InResult = INDEX_NONE)
		: Sample(InSample)
		, Result(InResult)
	{}

	static void ContiguousIndices(
		TArray<ContextIndex>& Array,
		const int NumParticles,
		const bool bForce = true)
	{
		if(bForce)
		{
			Array.SetNum(NumParticles);
			for(int32 i = 0; i < Array.Num(); ++i)
			{
				Array[i].Result = i;
				Array[i].Sample = i;
			}
		}
	}

	int32 Sample;
	int32 Result;
};

/**
* MetaData
*
* Metadata is used to attach state based information to the field evaluation 
* pipeline. Contexts and Commands can store metadata that can be used by
* the Evaluate() of the field node, or during the processing of the command.
*/


class CHAOS_API FFieldSystemMetaData {
public:

	enum CHAOS_API EMetaType
	{
		ECommandData_None = 0,
		ECommandData_ProcessingResolution,
		ECommandData_Results,
		ECommandData_Iteration,
		ECommandData_Culling,
		ECommandData_Filter
	};


	virtual ~FFieldSystemMetaData() {};
	virtual EMetaType Type() const = 0;
	virtual FFieldSystemMetaData* NewCopy() const = 0;
};


class CHAOS_API FFieldSystemMetaDataProcessingResolution : public FFieldSystemMetaData {
public:
	FFieldSystemMetaDataProcessingResolution(EFieldResolutionType ProcessingResolutionIn) : ProcessingResolution(ProcessingResolutionIn) {};
	virtual ~FFieldSystemMetaDataProcessingResolution() {};
	virtual EMetaType Type() const { return EMetaType::ECommandData_ProcessingResolution; }
	virtual FFieldSystemMetaData* NewCopy() const { return new FFieldSystemMetaDataProcessingResolution(ProcessingResolution); }

	EFieldResolutionType ProcessingResolution;
};

class CHAOS_API FFieldSystemMetaDataFilter : public FFieldSystemMetaData {
public:
	FFieldSystemMetaDataFilter(EFieldFilterType FilterTypeIn) : FilterType(FilterTypeIn) {};
	virtual ~FFieldSystemMetaDataFilter() {};
	virtual EMetaType Type() const { return EMetaType::ECommandData_Filter; }
	virtual FFieldSystemMetaData* NewCopy() const { return new FFieldSystemMetaDataFilter(FilterType); }

	EFieldFilterType FilterType;
};

template<class T>
class CHAOS_API FFieldSystemMetaDataResults : public FFieldSystemMetaData {
public:
	FFieldSystemMetaDataResults(const TArrayView<T>& ResultsIn) : Results(ResultsIn) {};
	virtual ~FFieldSystemMetaDataResults() {};
	virtual EMetaType Type() const { return EMetaType::ECommandData_Results; }
	virtual FFieldSystemMetaData* NewCopy() const { return new FFieldSystemMetaDataResults(Results); }

	const TArrayView<T>& Results;
};

class CHAOS_API FFieldSystemMetaDataIteration : public FFieldSystemMetaData {
public:
	FFieldSystemMetaDataIteration(int32 IterationsIn) : Iterations(IterationsIn) {};
	virtual ~FFieldSystemMetaDataIteration() {};
	virtual EMetaType Type() const { return EMetaType::ECommandData_Iteration; }
	virtual FFieldSystemMetaData* NewCopy() const { return new FFieldSystemMetaDataIteration(Iterations); }

	int32 Iterations;
};

class CHAOS_API FFieldSystemMetaDataCulling : public FFieldSystemMetaData
{
public:
	explicit FFieldSystemMetaDataCulling(const int32 PotentialSize)
		: bCullingActive(false)
		, MaxSize(PotentialSize)
	{
		EvaluatedIndexBuffer.Reserve(MaxSize);
	};

	virtual ~FFieldSystemMetaDataCulling() = default;

	virtual EMetaType Type() const
	{
		return EMetaType::ECommandData_Culling;
	}
	virtual FFieldSystemMetaData* NewCopy() const
	{
		return new FFieldSystemMetaDataCulling(MaxSize);
	}

	bool bCullingActive;
	int32 MaxSize;
	TArray<ContextIndex> EvaluatedIndexBuffer;
};

struct CHAOS_API FFieldContext
{
	typedef  TMap<FFieldSystemMetaData::EMetaType, TUniquePtr<FFieldSystemMetaData> > UniquePointerMap;
	typedef  TMap<FFieldSystemMetaData::EMetaType, FFieldSystemMetaData * > PointerMap;

	FFieldContext() = delete;

	FFieldContext(const FFieldContext&) = delete;
	FFieldContext(FFieldContext&&) = delete;
	FFieldContext& operator =(const FFieldContext&) = delete;
	FFieldContext & operator =(FFieldContext&&) = delete;

	FFieldContext(const TArrayView< ContextIndex >& SampleIndicesIn, const TArrayView<FVector>& SamplesIn,
		const UniquePointerMap & MetaDataIn, const float TimeSecondsIn)
		: SampleIndices(SampleIndicesIn)
		, Samples(SamplesIn)
		, TimeSeconds(TimeSecondsIn)

	{
		for (const TPair<FFieldSystemMetaData::EMetaType, TUniquePtr<FFieldSystemMetaData>>& Meta : MetaDataIn)
		{
			MetaData.Add(Meta.Key) = Meta.Value.Get();
		}

		CullingData = MakeUnique<FFieldSystemMetaDataCulling>(SampleIndices.Num());
		MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_Culling, CullingData.Get());
	}
	FFieldContext(const TArrayView< ContextIndex >& SampleIndicesIn, const TArrayView<FVector>& SamplesIn,
		const PointerMap & MetaDataIn, const float TimeSecondsIn)
		: SampleIndices(SampleIndicesIn)
		, Samples(SamplesIn)
		, MetaData(MetaDataIn)
		, TimeSeconds(TimeSecondsIn)
	{
		CullingData = MakeUnique<FFieldSystemMetaDataCulling>(SampleIndices.Num());
		MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_Culling, CullingData.Get());
	}

	TArrayView<ContextIndex> GetEvaluatedSamples()
	{
		if(!CullingData->bCullingActive)
		{
			// No culling took place
			return SampleIndices;
		}

		// Culling fields created an evaluation set
		return MakeArrayView(CullingData->EvaluatedIndexBuffer);
	}

	//
	// Ryan - TODO: This concept of having discreet sample data needs to change.  
	// I think we'd be better off supplying lambda accessors which can be specialized 
	// for each respective use case.  That means the method by which this data is 
	// traversed also needs to change; possibly to some load balanced threaded iterator 
	// or task based paradigm.

	const TArrayView<ContextIndex>& SampleIndices;
	const TArrayView<FVector>& Samples;
	PointerMap MetaData;
	TUniquePtr<FFieldSystemMetaDataCulling> CullingData;
	float TimeSeconds;
};

/** 
 * Limits the application of a meta data object to a single scope.
 * This has the effect of exposing metadata to downstream nodes but making sure
 * upstream nodes cannot see it.
 */
class CHAOS_API FScopedFieldContextMetaData
{
	FScopedFieldContextMetaData() = delete;
	FScopedFieldContextMetaData(const FScopedFieldContextMetaData&) = delete;
	FScopedFieldContextMetaData(FScopedFieldContextMetaData&&) = delete;
	FScopedFieldContextMetaData& operator=(const FScopedFieldContextMetaData&) = delete;
	FScopedFieldContextMetaData& operator=(FScopedFieldContextMetaData&&) = delete;

public:

	explicit FScopedFieldContextMetaData(FFieldContext& InContext, FFieldSystemMetaData* InMetaData)
		: TargetContext(InContext)
	{
		check(InMetaData);
		MetaType = InMetaData->Type();
		TargetContext.MetaData.Add(MetaType, InMetaData);
	}

	~FScopedFieldContextMetaData()
	{
		TargetContext.MetaData.Remove(MetaType);
	}

private:
	FFieldSystemMetaData::EMetaType MetaType;
	FFieldContext& TargetContext;
};

/**
* FFieldNodeBase
*
*  Abstract base class for the field node evaluation. 
*
*/
class CHAOS_API FFieldNodeBase
{

public:

	enum EFieldType
	{
		EField_None = 0,
		EField_Results,
		EField_Int32,
		EField_Float,
		EField_FVector,
	};

	enum ESerializationType
	{
		FieldNode_Null = 0,
		FieldNode_FUniformInteger,
		FieldNode_FRadialIntMask,
		FieldNode_FUniformScalar,
		FieldNode_FRadialFalloff,
		FieldNode_FPlaneFalloff,
		FieldNode_FBoxFalloff,
		FieldNode_FNoiseField,
		FieldNode_FUniformVector,
		FieldNode_FRadialVector,
		FieldNode_FRandomVector,
		FieldNode_FSumScalar,
		FieldNode_FSumVector,
		FieldNode_FConversionField,
		FieldNode_FCullingField,
		FieldNode_FWaveScalar,
		FieldNode_FReturnResultsTerminal
	};

	FFieldNodeBase() : Name("") {}
	virtual ~FFieldNodeBase() {}
	virtual EFieldType Type() const { check(false); return EFieldType::EField_None; }
	virtual ESerializationType SerializationType() const { check(false); return ESerializationType::FieldNode_Null; }
	virtual FFieldNodeBase * NewCopy() const = 0;
	virtual void Serialize(FArchive& Ar) { Ar << Name; }
	virtual bool operator==(const FFieldNodeBase& Node) { return Name.IsEqual(Node.GetName()); }

	FName GetName() const { return Name; }
	void  SetName(const FName & NameIn) { Name = NameIn; }

private:
	FName Name;
};


/**
* FieldNode<T>
*
*  Typed field nodes are used for the evaluation of specific types of data arrays.
*  For exampe, The FFieldNode<FVector>::Evaluate(...) will expect resutls 
*  of type TArrayView<FVector>, and an example implementation is the UniformVectorField.
*
*/
template<class T>
class FFieldNode : public FFieldNodeBase
{
public:
	
	virtual ~FFieldNode() {}

	virtual void Evaluate(FFieldContext&, TArrayView<T>& Results) const = 0;

	static EFieldType StaticType();
	virtual EFieldType Type() const { return StaticType(); }
};

template<> inline FFieldNodeBase::EFieldType FFieldNode<int32>::StaticType() { return EFieldType::EField_Int32; }
template<> inline FFieldNodeBase::EFieldType FFieldNode<float>::StaticType() { return EFieldType::EField_Float; }
template<> inline FFieldNodeBase::EFieldType FFieldNode<FVector>::StaticType() { return EFieldType::EField_FVector; }

/**
* FieldCommand
*
*   Field commands are issued on the game thread and trigger field
*   evaluation during game play. The Commands will store the root
*   node in the evaluation graph, and will trigger a full evaluation
*   of all the nodes in the graph. The MetaData within the command
*   will be passed to the evaluation of the field. 
*
*/
class CHAOS_API FFieldSystemCommand
{
public:
	FFieldSystemCommand()
		: TargetAttribute("")
		, RootNode(nullptr)
		, CommandName("")
		, TimeCreation(0.0)
	{}
	FFieldSystemCommand(FName TargetAttributeIn, FFieldNodeBase * RootNodeIn)
		: TargetAttribute(TargetAttributeIn)
		, RootNode(RootNodeIn)
		, CommandName("")
		, TimeCreation(0.0)
	{}

	// Commands are copied when moved from the one thread to 
	// another. This requires a full copy of all associated data. 
	FFieldSystemCommand(const FFieldSystemCommand& Other)
		: TargetAttribute(Other.RootNode ? Other.TargetAttribute:"")
		, RootNode(Other.RootNode?Other.RootNode->NewCopy():nullptr)
		, CommandName(Other.CommandName)
		, TimeCreation(Other.TimeCreation)
	{
		for (const TPair<FFieldSystemMetaData::EMetaType, TUniquePtr<FFieldSystemMetaData>>& Meta : Other.MetaData)
		{
			MetaData.Add(Meta.Key).Reset(Meta.Value->NewCopy());
		}
	}

	bool HasMetaData(const FFieldSystemMetaData::EMetaType Key) const
	{
		return MetaData.Contains(Key) && GetMetaData(Key) != nullptr;
	}

	const TUniquePtr<FFieldSystemMetaData>& GetMetaData(
		const FFieldSystemMetaData::EMetaType Key) const
	{
		return MetaData[Key];
	}

	template <class TMetaData>
	const TMetaData* GetMetaDataAs(const FFieldSystemMetaData::EMetaType Key) const
	{
		return static_cast<const TMetaData*>(GetMetaData(Key).Get());
	}

	void SetMetaData(const FFieldSystemMetaData::EMetaType Key, TUniquePtr<FFieldSystemMetaData>&& Value)
	{
		MetaData.Add(Key, MoveTemp(Value));
	}

	void SetMetaData(const FFieldSystemMetaData::EMetaType Key, FFieldSystemMetaData* Value)
	{
		MetaData[Key].Reset(Value);
	}

	void InitFieldNodes(const float& TimeSeconds, const FName& Name)
	{
		CommandName = Name;
		TimeCreation = TimeSeconds;
	}

	void Serialize(FArchive& Ar);
	bool operator==(const FFieldSystemCommand&);
	bool operator!=(const FFieldSystemCommand& Other) { return !this->operator==(Other); }

	FName TargetAttribute;
	TUniquePtr<FFieldNodeBase> RootNode;

	FName CommandName;
	float TimeCreation;

	TMap<FFieldSystemMetaData::EMetaType, TUniquePtr<FFieldSystemMetaData> > MetaData;
};



/*
* Equality testing for pointer wrapped FieldNodes
*/
template<class T>
bool FieldsEqual(const TUniquePtr<T>& NodeA, const TUniquePtr<T>& NodeB)
{
	if (NodeA.IsValid() == NodeB.IsValid())
	{
		if (NodeA.IsValid())
		{
			if (NodeA->SerializationType() == NodeB->SerializationType())
			{
				return NodeA->operator==(*NodeB);
			}
		}
		else
		{
			return true;
		}
	}
	return false;
}

