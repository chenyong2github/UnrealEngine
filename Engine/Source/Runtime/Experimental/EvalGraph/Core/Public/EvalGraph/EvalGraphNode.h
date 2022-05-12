// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvalGraph/EvalGraphInputOutput.h"
#include "EvalGraph/EvalGraphNodeParameters.h"

class FConnectionTypeBase;


namespace Eg
{

	struct EVALGRAPHCORE_API FNodeParameters {
		FName Name;
	};


	//
	// FNode
	//
	class FNode
	{
		friend class FGraph;
		friend class FConnectionTypeBase;

		FGuid Guid;
		FName Name;

		TArray< FConnectionTypeBase* > Inputs;
		TArray< FConnectionTypeBase* > Outputs;
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


		virtual void Evaluate(const FContext& Context, FConnectionTypeBase*) { ensure(false); }
		void InvalidateOutputs();

		virtual void SerializeInternal(FArchive& Ar) {};

	protected:
		void AddBaseInput(FConnectionTypeBase* InPtr) { Inputs.Add(InPtr); }
		const TArray< FConnectionTypeBase* >& GetInputs() const { return Inputs; }
		TArray< FConnectionTypeBase* >& GetInputs() { return Inputs; }

		void AddBaseOutput(FConnectionTypeBase* InPtr) { Outputs.Add(InPtr); }
		const TArray< FConnectionTypeBase* >& GetOutputs() const { return Outputs; }
		TArray< FConnectionTypeBase* >& GetOutputs() { return Outputs; }

	};
}

#define EVAL_GRAPH_REGISTER_CREATION_FACTORY(A) Eg::FNodeFactory::GetInstance()->RegisterNode(Eg::A::Type, [](const Eg::FNewNodeParameters& InParam) {return new Eg::A({InParam.Name}, InParam.Guid); });

#define EVAL_GRAPH_DEFINE_EXTERNAL(A) FName A::Type = #A;

#define EVAL_GRAPH_DEFINE_INTERNAL(A) public:\
 static FName Type;\
 virtual FName GetType() const { return Type; }\
 private:



