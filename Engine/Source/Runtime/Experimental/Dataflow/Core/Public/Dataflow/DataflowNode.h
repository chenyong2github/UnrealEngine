// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowConnection.h"
#include "Dataflow/DataflowNodeParameters.h"

namespace Dataflow
{

	struct DATAFLOWCORE_API FNodeParameters {
		FName Name;
	};


	//
	// FNode
	//
	class DATAFLOWCORE_API FNode
	{
		friend class FGraph;
		friend class FConnection;

		FGuid Guid;
		FName Name;

		TArray< FConnection* > Inputs;
		TArray< FConnection* > Outputs;
	public:

		FNode(const FNodeParameters& Param, FGuid InGuid = FGuid::NewGuid())
			: Guid(InGuid)
			, Name(Param.Name)
		{}

		virtual ~FNode() {}

		FName GetName() const { return Name; }
		void SetName(FName InName) { Name = InName; }

		virtual FName GetType() const { check(true); return FName("invalid"); }
		FGuid GetGuid() const { return Guid; }

		TArray<FPin> GetPins() const;
		FConnection* FindInput(FName Name) const;
		FConnection* FindOutput(FName Name) const;

		virtual void Evaluate(const FContext& Context, FConnection*) { ensure(false); }
		void InvalidateOutputs();

		virtual void SerializeInternal(FArchive& Ar) {};

		void AddInput(FConnection* InPtr);
		const TArray< FConnection* >& GetInputs() const { return Inputs; }
		TArray< FConnection* >& GetInputs() { return Inputs; }

		void AddOutput(FConnection* InPtr);
		const TArray< FConnection* >& GetOutputs() const { return Outputs; }
		TArray< FConnection* >& GetOutputs() { return Outputs; }

	};
}

#define DATAFLOW_REGISTER_CREATION_FACTORY(A) Dataflow::FNodeFactory::GetInstance()->RegisterNode(Dataflow::A::Type, [](const Dataflow::FNewNodeParameters& InParam) {return new Dataflow::A({InParam.Name}, InParam.Guid); });

#define DATAFLOW_DEFINE_EXTERNAL(A) FName A::Type = #A;

#define DATAFLOW_DEFINE_INTERNAL(A) public:\
 static FName Type;\
 virtual FName GetType() const { return Type; }\
 private:



