// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosLog.h"
#include "CoreMinimal.h"
#include "Dataflow/DataflowConnection.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "UObject/StructOnScope.h"

#include "DataflowNode.generated.h"

struct FDataflowInput;
struct FDataflowOutput;

namespace Dataflow {
	struct DATAFLOWCORE_API FNodeParameters {
		FName Name;
	};
	class FGraph;
}


/**
* FNode
*		Base class for node based evaluation within the Dataflow graph. 
* 
* Note : Do NOT create mutable variables in the classes derived from FDataflowNode. The state
*        is stored on the FContext. The Evaluate is const to allow support for multithreaded
*        evaluation. 
*/
USTRUCT()
struct DATAFLOWCORE_API FDataflowNode
{
	GENERATED_USTRUCT_BODY()

	friend class Dataflow::FGraph;
	friend struct FDataflowConnection;

	FGuid Guid;
	FName Name;

	TMap< int, FDataflowConnection* > Inputs;
	TMap< int, FDataflowConnection* > Outputs;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	bool bActive = true;

	FDataflowNode()
		: Guid(FGuid())
		, Name("Invalid")
	{
	}

	FDataflowNode(const Dataflow::FNodeParameters& Param, FGuid InGuid = FGuid::NewGuid())
		: Guid(InGuid)
		, Name(Param.Name)
	{
	}

	virtual ~FDataflowNode() { ClearInputs(); ClearOutputs(); }

	FGuid GetGuid() const { return Guid; }
	FName GetName() const { return Name; }
	void SetName(FName InName) { Name = InName; }

	virtual FName GetType() const { check(true); return FName("invalid"); }
	virtual FName GetDisplayName() const { return ""; }
	virtual FName GetCategory() const { return ""; }
	virtual FString GetTags() const { return ""; }
	virtual FString GetToolTip() const { return ""; }

	//
	// Connections
	//

	TArray<Dataflow::FPin> GetPins() const;


	void AddInput(FDataflowConnection* InPtr);

	FDataflowInput* FindInput(FName Name);
	FDataflowInput* FindInput(void* Reference);
	const FDataflowInput* FindInput(const void* Reference) const;
	const FDataflowInput* GetInput(const void* Reference) const;
	TArray< FDataflowConnection* > GetInputs() const;
	void ClearInputs();

	void AddOutput(FDataflowConnection* InPtr);
	FDataflowOutput* FindOutput(FName Name);
	FDataflowOutput* FindOutput(void* Reference);
	const FDataflowOutput* FindOutput(const void* Reference) const;
	const FDataflowOutput* GetOutput(const void* Reference) const;
	TArray< FDataflowConnection* > GetOutputs() const;
	void ClearOutputs();



	//
	//  Struct Support
	//

	virtual void SerializeInternal(FArchive& Ar) { check(false); }
	virtual FStructOnScope* NewScructOnScope() { return nullptr; }

	/** Register the Input and Outputs after the creation in the factory */
	void RegisterInputConnection(const void*);
	void RegisterOutputConnection(const void*);

	//
	// Evaluation
	//
	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput*) const { ensure(false); }

	/**
	*   GetValue(...)
	*
	*	Get the value of the Reference output, invoking up stream evaluations if not 
	*   cached in the contexts data store. 
	* 
	*   @param Context : The evaluation context that holds the data store.
	*   @param Value : The value to store in the contexts data store.
	*   @param Reference : Pointer to a member of this node that corresponds with the output to set.
	*/
	template<class T> const T& GetValue(Dataflow::FContext& Context, const T* Reference) const
	{
		return GetInput(Reference)->template GetValueAsInput<T>(Context, *Reference);
	}

	/** 
	*   SetValue(...)
	*
	*	Set the value of the Reference output.
	* 
	*   @param Context : The evaluation context that holds the data store.
	*   @param Value : The value to store in the contexts data store. 
	*   @param Reference : Pointer to a member of this node that corresponds with the output to set. 
	*/
	template<class T> void SetValue(Dataflow::FContext& Context, const T& Value, const T* Reference) const
	{
		GetOutput(Reference)->template SetValue<T>(Value, Context);
	}

	void InvalidateOutputs();

	bool ValidateConnections();

};

namespace Dataflow
{
	//
	// Use these macros to register dataflow nodes. 
	//

#define DATAFLOW_NODE_REGISTER_CREATION_FACTORY(A)									\
	FNodeFactory::GetInstance()->RegisterNode(										\
		{A::StaticType(),A::StaticDisplay(),A::StaticCategory(),					\
			A::StaticTags(),A::StaticToolTip()},									\
		[](const FNewNodeParameters& InParam){										\
				A* Val=new A({InParam.Name}, InParam.Guid);							\
				Val->ValidateConnections(); return Val;});

#define DATAFLOW_NODE_DEFINE_INTERNAL(TYPE, DISPLAY_NAME, CATEGORY, TAGS)			\
public:																				\
	static FName StaticType() {return #TYPE;}										\
	static FName StaticDisplay() {return DISPLAY_NAME;}								\
	static FName StaticCategory() {return CATEGORY;}								\
	static FString StaticTags() {return TAGS;}										\
	static FString StaticToolTip() {return FString("Create a dataflow node.");}		\
	virtual FName GetType() const { return #TYPE; }									\
	virtual FStructOnScope* NewScructOnScope() override {							\
	   return new FStructOnScope(TYPE::StaticStruct(), (uint8*)this);}				\
	virtual void SerializeInternal(FArchive& Ar) override {							\
		UScriptStruct* const Struct = TYPE::StaticStruct();							\
		Struct->SerializeTaggedProperties(Ar, (uint8*)this,							\
		Struct, nullptr);}															\
	virtual FName GetDisplayName() const override { return TYPE::StaticDisplay(); }	\
	virtual FName GetCategory() const override { return TYPE::StaticCategory(); }	\
	virtual FString GetTags() const override { return TYPE::StaticTags(); }			\
	virtual FString GetToolTip() const override { return TYPE::StaticToolTip(); }	\
	TYPE() {}																		\
private:


}

