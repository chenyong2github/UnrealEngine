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

const UObject* FRigVMCallstack::Last() const
{
	if(Stack.IsEmpty())
	{
		return nullptr;
	}
	return Stack.Last();
}

const UObject* FRigVMCallstack::operator[](int32 InIndex) const
{
	return Stack[InIndex];
}

bool FRigVMCallstack::Contains(const UObject* InEntry) const
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

FRigVMASTProxy FRigVMASTProxy::MakeFromCallPath(const FString& InCallPath, UObject* InRootObject)
{
	if(InCallPath.IsEmpty() || InRootObject == nullptr)
	{
		return FRigVMASTProxy();
	}

	UObject* ParentObject = InRootObject;

	FRigVMASTProxy Proxy;
	Proxy.Callstack.Stack.Reset();

	FString Left;
	FString Right = InCallPath;

	while(!Right.IsEmpty() && ParentObject != nullptr)
	{
		if(!Right.Split(TEXT("|"), &Left, &Right))
		{
			Left = Right;
			Right.Empty();
		}

		if(URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(ParentObject))
		{
			ParentObject = LibraryNode->GetContainedGraph();
		}

		if(URigVMGraph* Graph = Cast<URigVMGraph>(ParentObject))
		{
			if(URigVMNode* FoundNode = Graph->FindNodeByName(*Left))
			{
				Proxy.Callstack.Stack.Push(FoundNode);
				ParentObject = FoundNode;
			}
			else if(URigVMPin* FoundPin = Graph->FindPin(Left))
			{
				Proxy.Callstack.Stack.Push(FoundPin);
				break;
			}
			else
			{
				return FRigVMASTProxy();
			}
		}
		else
		{
			return FRigVMASTProxy();
		}
	}

#if UE_BUILD_DEBUG
	Proxy.DebugName = Proxy.Callstack.GetCallPath();
#endif

	return Proxy;
}

FRigVMASTProxy FRigVMASTProxy::MakeFromCallstack(const FRigVMCallstack& InCallstack)
{
	FRigVMASTProxy Proxy;
	Proxy.Callstack = InCallstack;
	return Proxy;
}

FRigVMASTProxy FRigVMASTProxy::MakeFromCallstack(const TArray<UObject*>* InCallstack)
{
	check(InCallstack);
	FRigVMCallstack Callstack;
	Callstack.Stack = *InCallstack;
	return MakeFromCallstack(Callstack);
}
