// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraph.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "Graph/ControlRigGraphSchema.h"
#include "ControlRig.h"
#include "ControlRigObjectVersion.h"
#include "Units/RigUnit.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"

#if WITH_EDITOR
#include "Kismet2/BlueprintEditorUtils.h"
#include "ControlRigBlueprintUtils.h"
#include "BlueprintCompilationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigGraph"

UControlRigGraph::UControlRigGraph()
{
}

void UControlRigGraph::Initialize(UControlRigBlueprint* InBlueprint)
{

}

const UControlRigGraphSchema* UControlRigGraph::GetControlRigGraphSchema()
{
	return CastChecked<const UControlRigGraphSchema>(GetSchema());
}

#if WITH_EDITORONLY_DATA
void UControlRigGraph::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FControlRigObjectVersion::GUID);
}
#endif

#if WITH_EDITOR
void UControlRigGraph::PostLoad()
{
	FoundHierarchyRefVariableNodes.Reset();
	FoundHierarchyRefMutableNodes.Reset();
	FoundHierarchyRefConnections.Reset();

	UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter());
	if (Blueprint)
	{
		UClass* BlueprintClass = Blueprint->GeneratedClass;

		// perform fixes on the graph for backwards compatibility
		if (GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::RemovalOfHierarchyRefPins)
		{
			for (UEdGraphNode* Node : Nodes)
			{
				UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node);
				if (RigNode != nullptr)
				{
					UStructProperty* Property = Cast<UStructProperty>(BlueprintClass->FindPropertyByName(RigNode->GetPropertyName()));
					if (Property != nullptr)
					{
						// found the hierarchy ref variable
						if (Property->Struct == FRigHierarchyRef::StaticStruct())
						{
							FoundHierarchyRefVariableNodes.Add(RigNode);
						}
						// found a former "hierarchy ref" utilizing unit
						else if (Property->Struct->IsChildOf(FRigUnitMutable::StaticStruct()))
						{
							FoundHierarchyRefMutableNodes.Add(RigNode);
						}
						else
						{
							continue;
						}

						TArray<UControlRigGraphNode*> LinkedNodes;
						for(UEdGraphNode* LinkedNode : RigNode->HierarchyRefOutputConnections)
						{
							LinkedNodes.Add(CastChecked<UControlRigGraphNode>(LinkedNode));
						}
						FoundHierarchyRefConnections.Add(RigNode, LinkedNodes);
					}
				}
			}
		}
	}

	Super::PostLoad();

	if (Blueprint)
	{
		if (GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::RemovalOfHierarchyRefPins)
		{
			if (BlueprintOnCompiledHandle.IsValid())
			{
				Blueprint->OnCompiled().Remove(BlueprintOnCompiledHandle);
			}
			Blueprint->OnCompiled().AddUObject(this, &UControlRigGraph::OnBlueprintCompiledPostLoad);
		}
	}
}

void UControlRigGraph::OnBlueprintCompiledPostLoad(UBlueprint* InCompiledBlueprint)
{
	if (GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::RemovalOfHierarchyRefPins)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter());
		ensure(InCompiledBlueprint == Blueprint);
		Blueprint->OnCompiled().Remove(BlueprintOnCompiledHandle);
		BlueprintOnCompiledHandle.Reset();

		// create a new "begin execution" unit for each branch
		for (UControlRigGraphNode* RigNode : FoundHierarchyRefVariableNodes)
		{
			TArray<UControlRigGraphNode*>& ConnectedNodes = FoundHierarchyRefConnections.FindChecked(RigNode);
			for (UControlRigGraphNode* ConnectedNode : ConnectedNodes)
			{
				int32 NodePosX = ConnectedNode->NodePosX - 200;
				int32 NodePosY = ConnectedNode->NodePosY;

				FName MemberName = FControlRigBlueprintUtils::AddUnitMember(Blueprint, FRigUnit_BeginExecution::StaticStruct());
				if (MemberName != NAME_None)
				{
					UControlRigGraphNode* BeginExecutionNode = FControlRigBlueprintUtils::InstantiateGraphNodeForProperty(this, MemberName, FVector2D((float)NodePosX, (float)NodePosY));
					ensure(BeginExecutionNode);

					UEdGraphPin* OutputPin = BeginExecutionNode->Pins[0];
					for (UEdGraphPin* InputPin : ConnectedNode->Pins)
					{
						if (InputPin->Direction != EEdGraphPinDirection::EGPD_Input)
						{
							continue;
						}
						if (InputPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
						{
							continue;
						}
						if (InputPin->PinType.PinSubCategoryObject != FControlRigExecuteContext::StaticStruct())
						{
							continue;
						}

						GetControlRigGraphSchema()->TryCreateConnection(OutputPin, InputPin);
					}
				}
			}

			FBlueprintEditorUtils::RemoveNode(Blueprint, RigNode, true);
		}

		// wire up old hierarchy ref connections to new execution connections
		for (UControlRigGraphNode* RigNode : FoundHierarchyRefMutableNodes)
		{
			if (RigNode->GetExecutionVariableInfo().Num() > 0)
			{
				TSharedRef<FControlRigField> RigNodeExecutionInfo = RigNode->GetExecutionVariableInfo()[0];
				TArray<UControlRigGraphNode*>& ConnectedNodes = FoundHierarchyRefConnections.FindChecked(RigNode);
				for (UControlRigGraphNode* ConnectedNode : ConnectedNodes)
				{
					if (ConnectedNode->GetExecutionVariableInfo().Num() > 0)
					{
						TSharedRef<FControlRigField> ConnectedNodeExecutionInfo = ConnectedNode->GetExecutionVariableInfo()[0];
						if (RigNodeExecutionInfo->OutputPin && ConnectedNodeExecutionInfo->InputPin)
						{
							GetControlRigGraphSchema()->TryCreateConnection(RigNodeExecutionInfo->OutputPin, ConnectedNodeExecutionInfo->InputPin);
						}
					}
				}
			}
		}

		for (UEdGraphNode* Node : Nodes)
		{
			UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node);
			if (RigNode != nullptr)
			{
				RigNode->ReconstructNode();
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

		FoundHierarchyRefVariableNodes.Reset();
		FoundHierarchyRefMutableNodes.Reset();
		FoundHierarchyRefConnections.Reset();

		FNotificationInfo Info(LOCTEXT("ControlRigUpdatedHelpMessage", "The Control Rig has automatically been updated to use execution pins. You will need to compile and re-save."));
		Info.bFireAndForget = true;
		Info.FadeOutDuration = 10.0f;
		Info.ExpireDuration = 0.0f;

		TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
		NotificationPtr->SetCompletionState(SNotificationItem::CS_Success);

	}
}

void UControlRigGraph::CacheBoneNameList(const FRigHierarchy& Hierarchy)
{
	TArray<FString> Names;
	for (const FRigBone& Bone : Hierarchy.Bones)
	{
		Names.Add(Bone.Name.ToString());
	}
	Names.Sort();

	BoneNameList.Reset();
	BoneNameList.Add(MakeShared<FString>(FName(NAME_None).ToString()));
	for (const FString& Name : Names)
	{
		BoneNameList.Add(MakeShared<FString>(Name));
	}
}

const TArray<TSharedPtr<FString>>& UControlRigGraph::GetBoneNameList() const
{
	return BoneNameList;
}

#endif

#undef LOCTEXT_NAMESPACE
