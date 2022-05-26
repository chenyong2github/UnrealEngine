// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowConnection.h"
#include "Dataflow/DataflowNodeParameters.h"

namespace Dataflow
{
	class FProperty;

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
		friend class FProperty;

		FGuid Guid;
		FName Name;

		TArray< FConnection* > Inputs;
		TArray< FConnection* > Outputs;
		TArray< FProperty* > Properties;
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

		virtual void Evaluate(const FContext& Context, FConnection*) const { ensure(false); }
		void InvalidateOutputs();

		virtual void SerializeInternal(FArchive& Ar);

		void AddProperty(FProperty* InPtr);
		const TArray< FProperty* >& GetProperties() const { return Properties; }
		TArray< FProperty* >& GetProperties() { return Properties; }

		void AddInput(FConnection* InPtr);
		const TArray< FConnection* >& GetInputs() const { return Inputs; }
		TArray< FConnection* >& GetInputs() { return Inputs; }

		void AddOutput(FConnection* InPtr);
		const TArray< FConnection* >& GetOutputs() const { return Outputs; }
		TArray< FConnection* >& GetOutputs() { return Outputs; }

	};

//
// Used these macros to register dataflow nodes. 
//

#define DATAFLOW_NODE_REGISTER_CREATION_FACTORY(A)					\
	FNodeFactory::GetInstance()->RegisterNode(					\
		A::StaticType(),										\
		[](const FNewNodeParameters& InParam)					\
			{return new A({InParam.Name}, InParam.Guid); });

#define DATAFLOW_NODE_DEFINE_INTERNAL(A)								\
public:															\
   static FName StaticType() {return #A;}						\
   virtual FName GetType() const { return #A; }					\
private:


}

