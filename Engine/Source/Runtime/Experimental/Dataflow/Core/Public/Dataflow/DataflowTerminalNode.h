// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosLog.h"
#include "CoreMinimal.h"
#include "Dataflow/DataflowConnection.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNode.h"

#include "DataflowTerminalNode.generated.h"

struct FDataflowInput;
struct FDataflowOutput;

/**
* FDataflowTerminalNode
*		Base class for terminal nodes within the Dataflow graph. 
* 
*		Terminal Nodes allow for non-const access to UObjects as
*       edges in the graph. They are used to push data out to
*       asset or the world from the calling client. Terminals
*       may not have outputs, they are only leaf nodes in the 
*       evaluation graph. 
*/
USTRUCT()
struct DATAFLOWCORE_API FDataflowTerminalNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()

	FDataflowTerminalNode()
		: Super() { }

	FDataflowTerminalNode(const Dataflow::FNodeParameters& Param, FGuid InGuid = FGuid::NewGuid())
		: Super(Param,InGuid) {
	}

	virtual ~FDataflowTerminalNode() { }

	//
	// Connections
	//

	virtual void AddOutput(FDataflowOutput* InPtr) override { ensure(false); }

	//
	// Error Checking
	//
	virtual bool ValidateConnections() override ;

	//
	// Evaluate
	//

	virtual void Evaluate(Dataflow::FContext& Context) const { ensure(false); }

private:
	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override
	{
		Evaluate(Context);
	};

};


