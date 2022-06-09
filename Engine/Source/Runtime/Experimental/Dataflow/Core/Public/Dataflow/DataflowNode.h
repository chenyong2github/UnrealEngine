// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowConnection.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "UObject/StructOnScope.h"

#include "DataflowNode.generated.h"

namespace Dataflow {
	struct DATAFLOWCORE_API FNodeParameters {
		FName Name;
	};
	class FGraph;
}


//
// FNode
//
USTRUCT()
struct DATAFLOWCORE_API FDataflowNode
{
	GENERATED_USTRUCT_BODY()

	friend class Dataflow::FGraph;
	friend class Dataflow::FConnection;

	FGuid Guid;
	FName Name;

	TArray< Dataflow::FConnection* > Inputs;
	TArray< Dataflow::FConnection* > Outputs;

	FDataflowNode()
		: Guid(FGuid())
		, Name("Invalid")
	{}

	FDataflowNode(const Dataflow::FNodeParameters& Param, FGuid InGuid = FGuid::NewGuid())
		: Guid(InGuid)
		, Name(Param.Name)
	{}

	virtual ~FDataflowNode() {}

	FGuid GetGuid() const { return Guid; }
	FName GetName() const { return Name; }
	void SetName(FName InName) { Name = InName; }

	virtual FName GetType() const { check(true); return FName("invalid"); }
	virtual FName GetDisplayName() const { return ""; }
	virtual FName GetCategory() const { return ""; }
	virtual FString GetTags() const { return ""; }
	virtual FString GetToolTip() const { return ""; }


	TArray<Dataflow::FPin> GetPins() const;
	Dataflow::FConnection* FindInput(FName Name) const;
	Dataflow::FConnection* FindOutput(FName Name) const;

	virtual void Evaluate(const  Dataflow::FContext& Context, Dataflow::FConnection*) const { ensure(false); }
	void InvalidateOutputs();

	void AddInput(Dataflow::FConnection* InPtr);
	const TArray< Dataflow::FConnection* >& GetInputs() const { return Inputs; }
	TArray< Dataflow::FConnection* >& GetInputs() { return Inputs; }

	void AddOutput(Dataflow::FConnection* InPtr);
	const TArray< Dataflow::FConnection* >& GetOutputs() const { return Outputs; }
	TArray< Dataflow::FConnection* >& GetOutputs() { return Outputs; }

	virtual void SerializeInternal(FArchive& Ar) { check(false); }
	virtual FStructOnScope* NewScructOnScope() { return nullptr; }
};

namespace Dataflow
{
	//
	// Used these macros to register dataflow nodes. 
	//

#define DATAFLOW_NODE_REGISTER_CREATION_FACTORY(A)								\
	FNodeFactory::GetInstance()->RegisterNode(									\
		{A::StaticType(),A::StaticDisplay(),A::StaticCategory(),				\
			A::StaticTags(),A::StaticToolTip()},								\
		[](const FNewNodeParameters& InParam)									\
			{return new A({InParam.Name}, InParam.Guid); });

#define DATAFLOW_NODE_DEFINE_INTERNAL(TYPE, DISPLAY_NAME, CATEGORY, TAGS)		\
public:																			\
	static FName StaticType() {return #TYPE;}									\
	static FName StaticDisplay() {return DISPLAY_NAME;}							\
	static FName StaticCategory() {return CATEGORY;}							\
	static FString StaticTags() {return TAGS;}									\
	static FString StaticToolTip() {return FString("Create a dataflow node.");}	\
	virtual FName GetType() const { return #TYPE; }								\
	virtual FStructOnScope* NewScructOnScope() override {						\
	   return new FStructOnScope(TYPE::StaticStruct(), (uint8*)this);}			\
	virtual void SerializeInternal(FArchive& Ar) override {						\
		UScriptStruct* const Struct = TYPE::StaticStruct();						\
		Struct->SerializeTaggedProperties(Ar, (uint8*)this,						\
		Struct, nullptr);}														\
	virtual FName GetDisplayName() const override { return TYPE::StaticDisplay(); }	\
	virtual FName GetCategory() const override { return TYPE::StaticCategory(); }	\
	virtual FString GetTags() const override { return TYPE::StaticTags(); }			\
	virtual FString GetToolTip() const override { return TYPE::StaticToolTip(); }	\
	TYPE() {}																		\
private:


}

