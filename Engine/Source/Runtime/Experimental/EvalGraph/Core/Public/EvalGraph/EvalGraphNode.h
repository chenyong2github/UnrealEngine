// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvalGraph/EvalGraphConnectionBase.h"
#include "EvalGraph/EvalGraphNodeParameters.h"

namespace Eg
{

	struct EVALGRAPHCORE_API FNodeParameters {
		FName Name;
	};


	//
	// FNode
	//
	class EVALGRAPHCORE_API FNode
	{
		friend class FGraph;
		friend class FConnectionBase;

		FGuid Guid;
		FName Name;

		TArray< FConnectionBase* > Inputs;
		TArray< FConnectionBase* > Outputs;
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
		FConnectionBase* FindInput(FName Name) const;
		FConnectionBase* FindOutput(FName Name) const;

		virtual void Evaluate(const FContext& Context, FConnectionBase*) { ensure(false); }
		void InvalidateOutputs();

		virtual void SerializeInternal(FArchive& Ar) {};

		void AddBaseInput(FConnectionBase* InPtr);
		const TArray< FConnectionBase* >& GetInputs() const { return Inputs; }
		TArray< FConnectionBase* >& GetInputs() { return Inputs; }

		void AddBaseOutput(FConnectionBase* InPtr);
		const TArray< FConnectionBase* >& GetOutputs() const { return Outputs; }
		TArray< FConnectionBase* >& GetOutputs() { return Outputs; }

	};
}

#define EVAL_GRAPH_REGISTER_CREATION_FACTORY(A) Eg::FNodeFactory::GetInstance()->RegisterNode(Eg::A::Type, [](const Eg::FNewNodeParameters& InParam) {return new Eg::A({InParam.Name}, InParam.Guid); });

#define EVAL_GRAPH_DEFINE_EXTERNAL(A) FName A::Type = #A;

#define EVAL_GRAPH_DEFINE_INTERNAL(A) public:\
 static FName Type;\
 virtual FName GetType() const { return Type; }\
 private:



