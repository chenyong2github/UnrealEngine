// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowInputOutput.h"

#include "ChaosLog.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNode.h"


FDataflowInput FDataflowInput::NoOpInput = FDataflowInput();
FDataflowOutput FDataflowOutput::NoOpOutput = FDataflowOutput();


FDataflowInput::FDataflowInput(const Dataflow::FInputParameters& Param, FGuid InGuid)
	: FDataflowConnection(Dataflow::FPin::EDirection::INPUT, Param.Type, Param.Name, Param.Owner, Param.Property, InGuid)
	, Connection(nullptr)
{
}



bool FDataflowInput::AddConnection(FDataflowConnection* InOutput)
{
	ensure(Connection == nullptr);
	if (ensure(InOutput->GetType() == this->GetType()))
	{
		Connection = (FDataflowOutput*)InOutput;
		return true;
	}
	return false;
}

bool FDataflowInput::RemoveConnection(FDataflowConnection* InOutput)
{
	if (ensure(Connection == (FDataflowOutput*)InOutput))
	{
		Connection = nullptr;
		return true;
	}
	return false;
}

TArray< FDataflowConnection* > FDataflowInput::GetConnectedOutputs()
{
	TArray<FDataflowConnection* > RetList;
	if (FDataflowConnection* Conn = GetConnection())
	{
		RetList.Add(Conn);
	}
	return RetList;
}

const TArray< const FDataflowConnection* > FDataflowInput::GetConnectedOutputs() const
{
	TArray<const FDataflowConnection* > RetList;
	if (const FDataflowConnection* Conn = GetConnection())
	{
		RetList.Add(Conn);
	}
	return RetList;
}

void FDataflowInput::Invalidate()
{
	OwningNode->InvalidateOutputs();
}

//
//
//  Output
//
//
//


FDataflowOutput::FDataflowOutput(const Dataflow::FOutputParameters& Param, FGuid InGuid)
	: FDataflowConnection(Dataflow::FPin::EDirection::OUTPUT, Param.Type, Param.Name, Param.Owner, Param.Property, InGuid)
{
}

const TArray<FDataflowInput*>& FDataflowOutput::GetConnections() const { return Connections; }
TArray<FDataflowInput*>& FDataflowOutput::GetConnections() { return Connections; }

const TArray< const FDataflowConnection*> FDataflowOutput::GetConnectedInputs() const
{
	TArray<const FDataflowConnection*> RetList;
	RetList.Reserve(Connections.Num());
	for (FDataflowInput* Ptr : Connections) 
	{ 
		RetList.Add(Ptr); 
	}
	return RetList;
}

TArray< FDataflowConnection*> FDataflowOutput::GetConnectedInputs()
{
	TArray<FDataflowConnection*> RetList;
	RetList.Reserve(Connections.Num());
	for (FDataflowInput* Ptr : Connections) 
	{ 
		RetList.Add(Ptr); 
	}
	return RetList;
}

bool FDataflowOutput::AddConnection(FDataflowConnection* InOutput)
{
	if (ensure(InOutput->GetType() == this->GetType()))
	{
		Connections.Add((FDataflowInput*)InOutput);
		return true;
	}
	return false;
}

bool FDataflowOutput::RemoveConnection(FDataflowConnection* InInput)
{
	Connections.RemoveSwap((FDataflowInput*)InInput); return true;
}

bool FDataflowOutput::Evaluate(Dataflow::FContext& Context) const
{
	check(OwningNode);

	if (OwningNode->bActive)
	{
		OwningNode->Evaluate(Context, this);
		if (!Context.HasData(CacheKey()))
		{
			ensureMsgf(false, TEXT("Failed to evaluate output (%s:%s)"), *OwningNode->GetName().ToString(), *GetName().ToString());
		}
	}
	return OwningNode->bActive;
}

void FDataflowOutput::Invalidate()
{
	if (CacheKeyValue != UINT_MAX)
	{
		CacheKeyValue = UINT_MAX;
		for (FDataflowConnection* Con : GetConnections())
		{
			Con->Invalidate();
		}
	}
}

