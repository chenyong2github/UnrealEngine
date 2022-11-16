// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMBuildData.h"

#include "RigVMModel/RigVMFunctionLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMBuildData)

FRigVMReferenceNodeData::FRigVMReferenceNodeData(URigVMFunctionReferenceNode* InReferenceNode)
{
	check(InReferenceNode);
	ReferenceNodePtr = TSoftObjectPtr<URigVMFunctionReferenceNode>(InReferenceNode);
	ReferenceNodePath = ReferenceNodePtr.ToString();
	ReferencedHeader = InReferenceNode->GetReferencedFunctionHeader();
}

TSoftObjectPtr<URigVMFunctionReferenceNode> FRigVMReferenceNodeData::GetReferenceNodeObjectPath()
{
	if(ReferenceNodePtr.IsNull())
	{
		ReferenceNodePtr = TSoftObjectPtr<URigVMFunctionReferenceNode>(ReferenceNodePath);
	}
	return ReferenceNodePtr;
}

URigVMFunctionReferenceNode* FRigVMReferenceNodeData::GetReferenceNode()
{
	if(ReferenceNodePtr.IsNull())
	{
		ReferenceNodePtr = TSoftObjectPtr<URigVMFunctionReferenceNode>(ReferenceNodePath);
	}
	if(!ReferenceNodePtr.IsValid())
	{
		ReferenceNodePtr.LoadSynchronous();
	}
	if(ReferenceNodePtr.IsValid())
	{
		return ReferenceNodePtr.Get();
	}
	return nullptr;
}

URigVMBuildData::URigVMBuildData()
: UObject()
, bIsRunningUnitTest(false)
{
}

const FRigVMFunctionReferenceArray* URigVMBuildData::FindFunctionReferences(const FRigVMGraphFunctionIdentifier& InFunction) const
{
	return GraphFunctionReferences.Find(InFunction);
}

void URigVMBuildData::ForEachFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                               TFunction<void(URigVMFunctionReferenceNode*)> PerReferenceFunction) const
{
	if (const FRigVMFunctionReferenceArray* ReferencesEntry = FindFunctionReferences(InFunction))
	{
		for (int32 ReferenceIndex = 0; ReferenceIndex < ReferencesEntry->Num(); ReferenceIndex++)
		{
			const TSoftObjectPtr<URigVMFunctionReferenceNode>& Reference = ReferencesEntry->operator [](ReferenceIndex);
			if (!Reference.IsValid())
			{
				Reference.LoadSynchronous();
			}
			if (Reference.IsValid())
			{
				PerReferenceFunction(Reference.Get());
			}
		}
	}
}

void URigVMBuildData::ForEachFunctionReferenceSoftPtr(const FRigVMGraphFunctionIdentifier& InFunction,
                                                      TFunction<void(TSoftObjectPtr<URigVMFunctionReferenceNode>)> PerReferenceFunction) const
{
	if (const FRigVMFunctionReferenceArray* ReferencesEntry = FindFunctionReferences(InFunction))
	{
		for (int32 ReferenceIndex = 0; ReferenceIndex < ReferencesEntry->Num(); ReferenceIndex++)
		{
			const TSoftObjectPtr<URigVMFunctionReferenceNode>& Reference = ReferencesEntry->operator [](ReferenceIndex);
			PerReferenceFunction(Reference);
		}
	}
}

void URigVMBuildData::UpdateReferencesForFunctionReferenceNode(URigVMFunctionReferenceNode* InReferenceNode)
{
	check(InReferenceNode);

	if(InReferenceNode->GetOutermost() == GetTransientPackage())
	{
		return;
	}

	const FRigVMGraphFunctionHeader& Header = InReferenceNode->GetReferencedFunctionHeader();
	FRigVMFunctionReferenceArray* ReferencesEntry = GraphFunctionReferences.Find(Header.LibraryPointer);
	if (ReferencesEntry == nullptr)
	{
		Modify();
		GraphFunctionReferences.Add(Header.LibraryPointer);
		ReferencesEntry = GraphFunctionReferences.Find(Header.LibraryPointer);
	}

	const FString ReferenceNodePathName = InReferenceNode->GetPathName();
	for (int32 ReferenceIndex = 0; ReferenceIndex < ReferencesEntry->Num(); ReferenceIndex++)
	{
		const TSoftObjectPtr<URigVMFunctionReferenceNode>& Reference = ReferencesEntry->operator [](ReferenceIndex);
		if(Reference.ToString() == ReferenceNodePathName)
		{
			return;
		}
	}

	Modify();
	ReferencesEntry->FunctionReferences.Add(InReferenceNode);
	MarkPackageDirty();

}

void URigVMBuildData::RegisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction, URigVMFunctionReferenceNode* InReference)
{
	if(InReference == nullptr)
	{
		return;
	}

	const TSoftObjectPtr<URigVMFunctionReferenceNode> ReferenceKey(InReference);

	RegisterFunctionReference(InFunction, ReferenceKey);
}

void URigVMBuildData::RegisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                                TSoftObjectPtr<URigVMFunctionReferenceNode> InReference)
{
	if(InReference.IsNull())
	{
		return;
	}

	if(FRigVMFunctionReferenceArray* ReferenceEntry = GraphFunctionReferences.Find(InFunction))
	{
		if(ReferenceEntry->FunctionReferences.Contains(InReference))
		{
			return;
		}

		Modify();
		ReferenceEntry->FunctionReferences.Add(InReference);
	}
	else
	{
		Modify();
		FRigVMFunctionReferenceArray NewReferenceEntry;
		NewReferenceEntry.FunctionReferences.Add(InReference);
		GraphFunctionReferences.Add(InFunction, NewReferenceEntry);
	}
	
	MarkPackageDirty();
}

void URigVMBuildData::RegisterFunctionReference(FRigVMReferenceNodeData InReferenceNodeData)
{
	if (InReferenceNodeData.ReferencedHeader.IsValid())
	{
		return RegisterFunctionReference(InReferenceNodeData.ReferencedHeader.LibraryPointer, InReferenceNodeData.GetReferenceNodeObjectPath());
	}
	
	check(!InReferenceNodeData.ReferencedFunctionPath_DEPRECATED.IsEmpty());

	TSoftObjectPtr<URigVMLibraryNode> LibraryNodePtr = TSoftObjectPtr<URigVMLibraryNode>(InReferenceNodeData.ReferencedFunctionPath_DEPRECATED);
	bool bFound = false;
	for (TPair< FRigVMGraphFunctionIdentifier, FRigVMFunctionReferenceArray >& Pair : GraphFunctionReferences)
	{
		if (Pair.Key.LibraryNode == LibraryNodePtr.ToSoftObjectPath())
		{
			Pair.Value.FunctionReferences.Add(InReferenceNodeData.GetReferenceNodeObjectPath());
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		FRigVMGraphFunctionIdentifier Pointer(nullptr, LibraryNodePtr.ToSoftObjectPath());
		if (!LibraryNodePtr.IsValid())
		{
			LibraryNodePtr.LoadSynchronous();
		}
		if (LibraryNodePtr.IsValid())
		{
			Pointer.HostObject = Cast<UObject>(LibraryNodePtr.Get()->GetFunctionHeader().GetFunctionHost());
		}
		FRigVMFunctionReferenceArray RefArray;
		RefArray.FunctionReferences.Add(InReferenceNodeData.GetReferenceNodeObjectPath());
		GraphFunctionReferences.Add(Pointer, RefArray);
	}
}

void URigVMBuildData::UnregisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                                  URigVMFunctionReferenceNode* InReference)
{
	if(InReference == nullptr)
	{
		return;
	}

	const TSoftObjectPtr<URigVMFunctionReferenceNode> ReferenceKey(InReference);

	return UnregisterFunctionReference(InFunction, ReferenceKey);
}

void URigVMBuildData::UnregisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                                  TSoftObjectPtr<URigVMFunctionReferenceNode> InReference)
{
	if(InReference.IsNull())
	{
		return;
	}

	if(FRigVMFunctionReferenceArray* ReferenceEntry = GraphFunctionReferences.Find(InFunction))
	{
		if(!ReferenceEntry->FunctionReferences.Contains(InReference))
		{
			return;
		}

		Modify();
		ReferenceEntry->FunctionReferences.Remove(InReference);
		MarkPackageDirty();
	}
}

void URigVMBuildData::ClearInvalidReferences()
{
	if (bIsRunningUnitTest)
	{
		return;
	}
	
	Modify();
	
	// check each function's each reference
	int32 NumRemoved = 0;
	for (TTuple<FRigVMGraphFunctionIdentifier, FRigVMFunctionReferenceArray>& FunctionReferenceInfo : GraphFunctionReferences)
	{
		FRigVMFunctionReferenceArray* ReferencesEntry = &FunctionReferenceInfo.Value;

		static FString sTransientPackagePrefix;
		if(sTransientPackagePrefix.IsEmpty())
		{
			sTransientPackagePrefix = GetTransientPackage()->GetPathName();
		}
		static const FString sTempPrefix = TEXT("/Temp/");

		NumRemoved += ReferencesEntry->FunctionReferences.RemoveAll([](TSoftObjectPtr<URigVMFunctionReferenceNode> Referencer)
		{
			// ignore keys / references within the transient package
			const FString ReferencerString = Referencer.ToString();
			return ReferencerString.StartsWith(sTransientPackagePrefix) || ReferencerString.StartsWith(sTempPrefix);
		});
	}

	if (NumRemoved > 0)
	{
		MarkPackageDirty();
	}
}



