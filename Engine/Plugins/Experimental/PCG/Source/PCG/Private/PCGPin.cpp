// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGNode.h"

UPCGPin::UPCGPin(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetFlags(RF_Transactional);
}

bool UPCGPin::AddEdgeTo(UPCGPin* OtherPin)
{
	if (!OtherPin)
	{
		return false;
	}

	for (UPCGEdge* Edge : Edges)
	{
		if (Edge->GetOtherPin(this) == OtherPin)
		{
			return false;
		}
	}

	Modify();
	OtherPin->Modify();

	UPCGEdge* NewEdge = Edges.Add_GetRef(NewObject<UPCGEdge>(this));
	OtherPin->Edges.Add(NewEdge);

	NewEdge->InputPin = this;
	NewEdge->OutputPin = OtherPin;

	return true;
}

bool UPCGPin::BreakEdgeTo(UPCGPin* OtherPin)
{
	if (!OtherPin)
	{
		return false;
	}

	for (UPCGEdge* Edge : Edges)
	{
		if (Edge->GetOtherPin(this) == OtherPin)
		{
			Modify();
			OtherPin->Modify();

			ensure(OtherPin->Edges.Remove(Edge));
			Edges.Remove(Edge);
			return true;
		}
	}

	return false;
}

bool UPCGPin::BreakAllEdges()
{
	bool bChanged = false;

	if (!Edges.IsEmpty())
	{
		Modify();
	}

	for (UPCGEdge* Edge : Edges)
	{
		if (UPCGPin* OtherPin = Edge->GetOtherPin(this))
		{
			OtherPin->Modify();
			ensure(OtherPin->Edges.Remove(Edge));
			bChanged = true;
		}
	}

	Edges.Reset();

	return bChanged;
}

bool UPCGPin::IsConnected() const
{
	for (const UPCGEdge* Edge : Edges)
	{
		if (Edge->IsValid())
		{
			return true;
		}
	}

	return false;
}

int32 UPCGPin::EdgeCount() const
{
	int32 EdgeNum = 0;
	for (const UPCGEdge* Edge : Edges)
	{
		if (Edge->IsValid())
		{
			++EdgeNum;
		}
	}

	return EdgeNum;
}