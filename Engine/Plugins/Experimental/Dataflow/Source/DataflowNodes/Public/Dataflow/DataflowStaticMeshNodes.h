// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"

namespace Dataflow
{

	/*
	class CHAOSFLESHENGINENODES_API ExampleNode : public FNode
	{
		DATAFLOW_NODE_DEFINE_INTERNAL(ExampleNode)

	public:
		typede ExampleDataSharedPtr DataType;

		TSharedPtr< class TOutput<DataType> > Output;

		ExampleNode(const FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
			: FNode(InParam, InGuid)
			, Output(new TOutput<DataType>(TOutputParameters<DataType>({ FName("ExampleOut"), this })))
		{}

		virtual void Evaluate(const FContext& Context, FConnection* Out) const override
		{
			Output->(<set example data>, Context);
		}
	};
	*/

	void RegisterStaticMeshNodes();

}

