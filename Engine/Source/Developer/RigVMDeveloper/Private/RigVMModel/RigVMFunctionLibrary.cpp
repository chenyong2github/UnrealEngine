// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMFunctionLibrary.h"

URigVMFunctionLibrary::URigVMFunctionLibrary()
: URigVMGraph()
{
}

FString URigVMFunctionLibrary::GetNodePath() const
{
	return FString::Printf(TEXT("FunctionLibrary::%s"), *Super::GetNodePath());
}

TArray<URigVMLibraryNode*> URigVMFunctionLibrary::GetFunctions() const
{
	TArray<URigVMLibraryNode*> Functions;

	for (URigVMNode* Node : GetNodes())
	{
		// we only allow library nodes under a function library graph
		URigVMLibraryNode* LibraryNode = CastChecked<URigVMLibraryNode>(Node);
		Functions.Add(LibraryNode);
	}

	return Functions;
}

URigVMLibraryNode* URigVMFunctionLibrary::FindFunction(const FName& InFunctionName) const
{
	FString FunctionNameStr = InFunctionName.ToString();
	if (FunctionNameStr.StartsWith(TEXT("FunctionLibrary::|")))
	{
		FunctionNameStr.RightChopInline(18);
	}
	return Cast<URigVMLibraryNode>(FindNodeByName(*FunctionNameStr));
}

TArray< TSoftObjectPtr<URigVMFunctionReferenceNode> > URigVMFunctionLibrary::GetReferencesForFunction(const FName& InFunctionName)
{
	TArray< TSoftObjectPtr<URigVMFunctionReferenceNode> > Result;

	if (URigVMLibraryNode* Function = FindFunction(InFunctionName))
	{
		FRigVMFunctionReferenceArray* ReferencesEntry = FunctionReferences.Find(Function);
		if (ReferencesEntry)
		{
			for (int32 ReferenceIndex = 0; ReferenceIndex < ReferencesEntry->Num(); ReferenceIndex++)
			{
				const TSoftObjectPtr<URigVMFunctionReferenceNode>& Reference = ReferencesEntry->operator [](ReferenceIndex);
				Result.Add(TSoftObjectPtr<URigVMFunctionReferenceNode>(Reference.GetUniqueID()));
			}
		}
	}

	return Result;
}

TArray< FString > URigVMFunctionLibrary::GetReferencePathsForFunction(const FName& InFunctionName)
{
	TArray< FString > Result;

	if (URigVMLibraryNode* Function = FindFunction(InFunctionName))
	{
		FRigVMFunctionReferenceArray* ReferencesEntry = FunctionReferences.Find(Function);
		if (ReferencesEntry)
		{
			for (int32 ReferenceIndex = 0; ReferenceIndex < ReferencesEntry->Num(); ReferenceIndex++)
			{
				const TSoftObjectPtr<URigVMFunctionReferenceNode>& Reference = ReferencesEntry->operator [](ReferenceIndex);
				Result.Add(Reference.ToString());
			}
		}
	}

	return Result;
}

URigVMLibraryNode* URigVMFunctionLibrary::FindPreviouslyLocalizedFunction(URigVMLibraryNode* InFunctionToLocalize)
{
	if(InFunctionToLocalize == nullptr)
	{
		return nullptr;
	}
	
	const FString PathName = InFunctionToLocalize->GetPathName();

	if(!LocalizedFunctions.Contains((PathName)))
	{
		return nullptr;
	}

	URigVMLibraryNode* LocalizedFunction = LocalizedFunctions.FindChecked(PathName);

	// once we found the function - let's make sure it's notation is right
	if(LocalizedFunction->GetPins().Num() != InFunctionToLocalize->GetPins().Num())
	{
		return nullptr;
	}
	for(int32 PinIndex=0; PinIndex < InFunctionToLocalize->GetPins().Num(); PinIndex++)
	{
		URigVMPin* PinA = InFunctionToLocalize->GetPins()[PinIndex];
		URigVMPin* PinB = LocalizedFunction->GetPins()[PinIndex];

		if((PinA->GetFName() != PinB->GetFName()) ||
			(PinA->GetCPPType() != PinB->GetCPPType()) ||
			(PinA->GetCPPTypeObject() != PinB->GetCPPTypeObject()) ||
			(PinA->IsArray() != PinB->IsArray()))
		{
			return nullptr;
		}
	}
	
	return LocalizedFunction;
}
