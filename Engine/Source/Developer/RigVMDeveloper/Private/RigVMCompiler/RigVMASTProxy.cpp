// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCompiler/RigVMASTProxy.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/Nodes/RigVMLibraryNode.h"

FString FRigVMCallstack::GetCallPath(bool bIncludeLast) const
{
	TArray<FString> Segments;
	for (UObject* Entry : Stack)
	{
		if (URigVMNode* Node = Cast<URigVMNode>(Entry))
		{
			Segments.Add(Node->GetName());
		}
		else if (URigVMPin* Pin = Cast<URigVMPin>(Entry))
		{
			Segments.Add(Pin->GetPinPath());
		}
	}

	if (!bIncludeLast)
	{
		Segments.Pop();
	}

	if (Segments.Num() == 0)
	{
		return FString();
	}

	FString Result = Segments[0];
	for (int32 PartIndex = 1; PartIndex < Segments.Num(); PartIndex++)
	{
		Result += TEXT("|") + Segments[PartIndex];
	}

	return Result;
}

int32 FRigVMCallstack::Num() const
{
	return Stack.Num();
}

const UObject* FRigVMCallstack::operator[](int32 InIndex) const
{
	return Stack[InIndex];
}

bool FRigVMCallstack::Contains(UObject* InEntry) const
{
	return Stack.Contains(InEntry);
}

FRigVMASTProxy FRigVMASTProxy::MakeFromUObject(UObject* InSubject)
{
	UObject* Subject = InSubject;

	FRigVMASTProxy Proxy;
	Proxy.Callstack.Stack.Reset();

	while (Subject != nullptr)
	{
		if (URigVMPin* Pin = Cast<URigVMPin>(Subject))
		{
			Subject = Pin->GetNode();
		}
		else if (URigVMNode* Node = Cast<URigVMNode>(Subject))
		{
			Subject = Node->GetGraph();
		}
		else if (URigVMGraph* Graph = Cast<URigVMGraph>(Subject))
		{
			Subject = Cast<URigVMLibraryNode>(Graph->GetOuter());
			if (Subject)
			{
				Proxy.Callstack.Stack.Insert(Subject, 0);
			}
		}
		else
		{
			break;
		}
	}

	Proxy.Callstack.Stack.Push(InSubject);

#if UE_BUILD_DEBUG
	Proxy.DebugName = Proxy.Callstack.GetCallPath();
#endif
	return Proxy;
}
