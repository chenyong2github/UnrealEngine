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

	FName GetName() const { return Name; }
	void SetName(FName InName) { Name = InName; }

	virtual FString GetDisplayName() { return ""; }
	virtual FString GetCatagory() { return ""; }
	virtual FString GetTags() { return ""; }

	virtual FName GetType() const { check(true); return FName("invalid"); }
	FGuid GetGuid() const { return Guid; }

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

#define DATAFLOW_NODE_REGISTER_CREATION_FACTORY(A)						\
	FNodeFactory::GetInstance()->RegisterNode(							\
		A::StaticType(),													\
		[](const FNewNodeParameters& InParam)							\
			{return new A({InParam.Name}, InParam.Guid); });

#define DATAFLOW_NODE_DEFINE_INTERNAL(TYPE, DISPLAY_NAME, CATAGORY, TAGS)\
public:																	\
	static FName StaticType() {return #TYPE;}							\
	static FName StaticDisplay() {return DISPLAY_NAME;}					\
	virtual FName GetType() const { return #TYPE; }						\
	virtual FStructOnScope* NewScructOnScope() override {				\
	   return new FStructOnScope(TYPE::StaticStruct(), (uint8*)this);}	\
	virtual void SerializeInternal(FArchive& Ar) override {				\
		UScriptStruct* const Struct = TYPE::StaticStruct();				\
		Struct->SerializeTaggedProperties(Ar, (uint8*)this,				\
		Struct, nullptr);}												\
	virtual FString GetDisplayName() override { return DISPLAY_NAME; }	\
	virtual FString GetCatagory() override { return CATAGORY; }			\
	virtual FString GetTags() override { return TAGS; }					\
	TYPE() {}															\
private:


}

