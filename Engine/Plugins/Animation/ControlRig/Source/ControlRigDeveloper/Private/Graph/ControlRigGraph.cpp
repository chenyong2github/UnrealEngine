// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraph.h"
#include "ControlRigBlueprint.h"
#include "ControlRig.h"
#include "RigVMModel/RigVMGraph.h"
#include "Units/RigUnit.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ControlRigGraph)

#define LOCTEXT_NAMESPACE "ControlRigGraph"

UControlRigGraph::UControlRigGraph()
{
	bSuspendModelNotifications = false;
	bIsTemporaryGraphForCopyPaste = false;
	LastHierarchyTopologyVersion = INDEX_NONE;
	bIsFunctionDefinition = false;
}

void UControlRigGraph::InitializeFromBlueprint(URigVMBlueprint* InBlueprint)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::InitializeFromBlueprint(InBlueprint);

	const UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(InBlueprint);
	URigHierarchy* Hierarchy = ControlRigBlueprint->Hierarchy;

	if(UControlRig* ControlRig = Cast<UControlRig>(ControlRigBlueprint->GetObjectBeingDebugged()))
	{
		Hierarchy = ControlRig->GetHierarchy();
	}

	if(Hierarchy)
	{
		CacheNameLists(Hierarchy, &ControlRigBlueprint->DrawContainer, ControlRigBlueprint->ShapeLibraries);
	}
}

#if WITH_EDITOR

TArray<TSharedPtr<FString>> UControlRigGraph::EmptyElementNameList;

void UControlRigGraph::CacheNameLists(URigHierarchy* InHierarchy, const FRigVMDrawContainer* DrawContainer, TArray<TSoftObjectPtr<UControlRigShapeLibrary>> ShapeLibraries)
{
	if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(GetOuter()))
	{
		return;
	}

	check(InHierarchy);
	check(DrawContainer);

	if(LastHierarchyTopologyVersion != InHierarchy->GetTopologyVersion())
	{
		ElementNameLists.FindOrAdd(ERigElementType::All);
		ElementNameLists.FindOrAdd(ERigElementType::Bone);
		ElementNameLists.FindOrAdd(ERigElementType::Null);
		ElementNameLists.FindOrAdd(ERigElementType::Control);
		ElementNameLists.FindOrAdd(ERigElementType::Curve);
		ElementNameLists.FindOrAdd(ERigElementType::RigidBody);
		ElementNameLists.FindOrAdd(ERigElementType::Reference);

		TArray<TSharedPtr<FString>>& AllNameList = ElementNameLists.FindChecked(ERigElementType::All);
		TArray<TSharedPtr<FString>>& BoneNameList = ElementNameLists.FindChecked(ERigElementType::Bone);
		TArray<TSharedPtr<FString>>& NullNameList = ElementNameLists.FindChecked(ERigElementType::Null);
		TArray<TSharedPtr<FString>>& ControlNameList = ElementNameLists.FindChecked(ERigElementType::Control);
		TArray<TSharedPtr<FString>>& CurveNameList = ElementNameLists.FindChecked(ERigElementType::Curve);
		TArray<TSharedPtr<FString>>& RigidBodyNameList = ElementNameLists.FindChecked(ERigElementType::RigidBody);
		TArray<TSharedPtr<FString>>& ReferenceNameList = ElementNameLists.FindChecked(ERigElementType::Reference);
		
		CacheNameListForHierarchy<FRigBaseElement>(InHierarchy, AllNameList, false);
		CacheNameListForHierarchy<FRigBoneElement>(InHierarchy, BoneNameList, false);
		CacheNameListForHierarchy<FRigNullElement>(InHierarchy, NullNameList, false);
		CacheNameListForHierarchy<FRigControlElement>(InHierarchy, ControlNameList, false);
		CacheNameListForHierarchy<FRigControlElement>(InHierarchy, ControlNameListWithoutAnimationChannels, true);
		CacheNameListForHierarchy<FRigCurveElement>(InHierarchy, CurveNameList, false);
		CacheNameListForHierarchy<FRigRigidBodyElement>(InHierarchy, RigidBodyNameList, false);
		CacheNameListForHierarchy<FRigReferenceElement>(InHierarchy, ReferenceNameList, false);

		LastHierarchyTopologyVersion = InHierarchy->GetTopologyVersion();
	}
	CacheNameList<FRigVMDrawContainer>(*DrawContainer, DrawingNameList);

	EntryNameList.Reset();
	EntryNameList.Add(MakeShared<FString>(FName(NAME_None).ToString()));

	if(const UControlRigBlueprint* Blueprint = CastChecked<UControlRigBlueprint>(GetBlueprint()))
	{
		const TArray<FName> EntryNames = Blueprint->GetRigVMClient()->GetEntryNames();
		for (const FName& EntryName : EntryNames)
		{
			EntryNameList.Add(MakeShared<FString>(EntryName.ToString()));
		}
	}

	ShapeNameList.Reset();
	ShapeNameList.Add(MakeShared<FString>(FName(NAME_None).ToString()));

	TMap<FString, FString> LibraryNameMap;
	if(UControlRig* ControlRig = InHierarchy->GetTypedOuter<UControlRig>())
	{
		LibraryNameMap = ControlRig->ShapeLibraryNameMap;
	}

	for(const TSoftObjectPtr<UControlRigShapeLibrary>& ShapeLibrary : ShapeLibraries)
	{
		if(ShapeLibrary.IsNull() || !ShapeLibrary.IsValid())
		{
			ShapeLibrary.LoadSynchronous();
		}

		if(ShapeLibrary.IsNull() || !ShapeLibrary.IsValid())
		{
			continue;
		}

		const bool bUseNameSpace = ShapeLibraries.Num() > 1;
		FString LibraryName = ShapeLibrary->GetName();
		if(const FString* RemappedName = LibraryNameMap.Find(LibraryName))
		{
			LibraryName = *RemappedName;
		}
		
		const FString NameSpace = bUseNameSpace ? LibraryName + TEXT(".") : FString();
		ShapeNameList.Add(MakeShared<FString>(UControlRigShapeLibrary::GetShapeName(ShapeLibrary.Get(), bUseNameSpace, LibraryNameMap, ShapeLibrary->DefaultShape)));
		for (const FControlRigShapeDefinition& Shape : ShapeLibrary->Shapes)
		{
			ShapeNameList.Add(MakeShared<FString>(UControlRigShapeLibrary::GetShapeName(ShapeLibrary.Get(), bUseNameSpace, LibraryNameMap, Shape)));
		}
	}
}

const TArray<TSharedPtr<FString>>* UControlRigGraph::GetElementNameList(ERigElementType InElementType) const
{
	if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(GetOuter()))
	{
		return OuterGraph->GetElementNameList(InElementType);
	}
	
	if(InElementType == ERigElementType::None)
	{
		return &EmptyElementNameList;
	}
	
	if(!ElementNameLists.Contains(InElementType))
	{
		const UControlRigBlueprint* Blueprint = CastChecked<UControlRigBlueprint>(GetBlueprint());
		if(Blueprint == nullptr)
		{
			return &EmptyElementNameList;
		}

		UControlRigGraph* MutableThis = (UControlRigGraph*)this;
		URigHierarchy* Hierarchy = Blueprint->Hierarchy;
		if(UControlRig* ControlRig = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged()))
		{
			Hierarchy = ControlRig->GetHierarchy();
		}	
			
		MutableThis->CacheNameLists(Hierarchy, &Blueprint->DrawContainer, Blueprint->ShapeLibraries);
	}
	return &ElementNameLists.FindChecked(InElementType);
}

const TArray<TSharedPtr<FString>>* UControlRigGraph::GetElementNameList(URigVMPin* InPin) const
{
	if (InPin)
	{
		if (URigVMPin* ParentPin = InPin->GetParentPin())
		{
			if (ParentPin->GetCPPTypeObject() == FRigElementKey::StaticStruct())
			{
				if (URigVMPin* TypePin = ParentPin->FindSubPin(TEXT("Type")))
				{
					FString DefaultValue = TypePin->GetDefaultValue();
					if (!DefaultValue.IsEmpty())
					{
						ERigElementType Type = (ERigElementType)StaticEnum<ERigElementType>()->GetValueByNameString(DefaultValue);
						return GetElementNameList(Type);
					}
				}
			}
		}
	}

	return GetBoneNameList(nullptr);
}

const TArray<TSharedPtr<FString>> UControlRigGraph::GetSelectedElementsNameList() const
{
	TArray<TSharedPtr<FString>> Result;
	if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(GetOuter()))
	{
		return OuterGraph->GetSelectedElementsNameList();
	}
	
	const UControlRigBlueprint* Blueprint = CastChecked<UControlRigBlueprint>(GetBlueprint());
	if(Blueprint == nullptr)
	{
		return Result;
	}
	
	TArray<FRigElementKey> Keys = Blueprint->Hierarchy->GetSelectedKeys();
	for (const FRigElementKey& Key : Keys)
	{
		FString ValueStr;
		FRigElementKey::StaticStruct()->ExportText(ValueStr, &Key, nullptr, nullptr, PPF_None, nullptr);
		Result.Add(MakeShared<FString>(ValueStr));
	}
	
	return Result;
}

const TArray<TSharedPtr<FString>>* UControlRigGraph::GetDrawingNameList(URigVMPin* InPin) const
{
	if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(GetOuter()))
	{
		return OuterGraph->GetDrawingNameList(InPin);
	}
	return &DrawingNameList;
}

const TArray<TSharedPtr<FString>>* UControlRigGraph::GetEntryNameList(URigVMPin* InPin) const
{
	if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(GetOuter()))
	{
		return OuterGraph->GetEntryNameList(InPin);
	}
	return &EntryNameList;
}

const TArray<TSharedPtr<FString>>* UControlRigGraph::GetShapeNameList(URigVMPin* InPin) const
{
	if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(GetOuter()))
	{
		return OuterGraph->GetShapeNameList(InPin);
	}
	return &ShapeNameList;
}

#endif

bool UControlRigGraph::HandleModifiedEvent_Internal(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if(!Super::HandleModifiedEvent_Internal(InNotifType, InGraph, InSubject))
	{
		return false;
	}

	switch (InNotifType)
	{
		case ERigVMGraphNotifType::PinDefaultValueChanged:
		{
			if (URigVMPin* ModelPin = Cast<URigVMPin>(InSubject))
			{
				if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(FindNodeForModelNodeName(ModelPin->GetNode()->GetFName())))
				{
					if (Cast<URigVMUnitNode>(ModelPin->GetNode()))
					{
						// if the node contains a rig element key - invalidate the node
						if(RigNode->GetAllPins().ContainsByPredicate([](UEdGraphPin* Pin) -> bool
						{
							return Pin->PinType.PinSubCategoryObject == FRigElementKey::StaticStruct();
						}))
						{
							// we do this to enforce the refresh of the element name widgets
							RigNode->SynchronizeGraphPinValueWithModelPin(ModelPin);
						}
					}
				}
			}
			break;
		}
		default:
		{
			break;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

