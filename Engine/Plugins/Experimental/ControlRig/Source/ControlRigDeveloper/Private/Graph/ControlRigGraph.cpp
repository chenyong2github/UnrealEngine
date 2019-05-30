// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraph.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "Graph/ControlRigGraphSchema.h"
#include "ControlRig.h"
#include "ControlRigModel.h"
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
	bSuspendModelNotifications = false;
	bIsTemporaryGraphForCopyPaste = false;
}

void UControlRigGraph::Initialize(UControlRigBlueprint* InBlueprint)
{
	InBlueprint->OnModified().RemoveAll(this);
	InBlueprint->OnModified().AddUObject(this, &UControlRigGraph::HandleModelModified);
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
#if WITH_EDITORONLY_DATA
			for (UEdGraphNode* Node : Nodes)
			{
				UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node);
				if (RigNode != nullptr)
				{
					// store the nodes connected to outputs of hierarchy refs.
					// this is done for backwards compatibility
					if (RigNode->HasAnyFlags(RF_NeedPostLoad))
					{
						RigNode->CacheHierarchyRefConnectionsOnPostLoad();
					}
				}
			}
#endif

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

	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	if (RigBlueprint)
	{
		if (GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::RemovalOfHierarchyRefPins)
		{
			if (BlueprintOnCompiledHandle.IsValid())
			{
				Blueprint->OnCompiled().Remove(BlueprintOnCompiledHandle);
			}
			BlueprintOnCompiledHandle = Blueprint->OnCompiled().AddUObject(this, &UControlRigGraph::OnBlueprintCompiledPostLoad);
		}

		RigBlueprint->PopulateModelFromGraph(this);
	}
}

void UControlRigGraph::OnBlueprintCompiledPostLoad(UBlueprint* InCompiledBlueprint)
{
	if (GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::RemovalOfHierarchyRefPins)
	{
		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetOuter());
		ensure(InCompiledBlueprint == RigBlueprint);
		RigBlueprint->OnCompiled().Remove(BlueprintOnCompiledHandle);
		BlueprintOnCompiledHandle.Reset();

		struct FOutStandingLink
		{
			FOutStandingLink() {}
			FOutStandingLink(const FName& InA, const FName& InB, const FName& InC, const FName& InD)
			{
				A = InA;
				B = InB;
				C = InC;
				D = InD;
			}

			FName A;
			FName B;
			FName C;
			FName D;
		};
		TArray<FOutStandingLink> OutStandingLinks;

		// create a new "begin execution" unit for each branch
		for (UControlRigGraphNode* RigNode : FoundHierarchyRefVariableNodes)
		{
			TArray<UControlRigGraphNode*>& ConnectedNodes = FoundHierarchyRefConnections.FindChecked(RigNode);
			for (UControlRigGraphNode* ConnectedNode : ConnectedNodes)
			{
				int32 NodePosX = ConnectedNode->NodePosX - 200;
				int32 NodePosY = ConnectedNode->NodePosY;

				if (RigBlueprint->ModelController->AddNode(FRigUnit_BeginExecution::StaticStruct()->GetFName(), FVector2D((float)NodePosX, (float)NodePosY)))
				{
					FName BeginExecNode = RigBlueprint->LastNameFromNotification;
					if (BeginExecNode != NAME_None)
					{
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
							if (InputPin->PinType.PinSubCategoryObject != FRigHierarchyRef::StaticStruct())
							{
								continue;
							}

							OutStandingLinks.Add(FOutStandingLink(BeginExecNode, TEXT("ExecuteContext"), ConnectedNode->PropertyName, TEXT("ExecuteContext")));
						}
					}
				}
			}

			FBlueprintEditorUtils::RemoveNode(RigBlueprint, RigNode, true);
		}

		// wire up old hierarchy ref connections to new execution connections
		for (UControlRigGraphNode* RigNode : FoundHierarchyRefMutableNodes)
		{
			for (UEdGraphPin* OutputPin : RigNode->Pins)
			{
				if (OutputPin->Direction != EEdGraphPinDirection::EGPD_Output)
				{
					continue;
				}
				if (OutputPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
				{
					continue;
				}
				if (OutputPin->PinType.PinSubCategoryObject != FRigHierarchyRef::StaticStruct())
				{
					continue;
				}

				for (UEdGraphPin* InputPin : OutputPin->LinkedTo)
				{
					if (InputPin->Direction != EEdGraphPinDirection::EGPD_Input)
					{
						continue;
					}
					if (InputPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
					{
						continue;
					}
					if (InputPin->PinType.PinSubCategoryObject != FRigHierarchyRef::StaticStruct())
					{
						continue;
					}

					UControlRigGraphNode* InputNode = Cast<UControlRigGraphNode>(InputPin->GetOwningNode());
					OutStandingLinks.Add(FOutStandingLink(RigNode->PropertyName, TEXT("ExecuteContext"), InputNode->PropertyName, TEXT("ExecuteContext")));
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

		for (const FOutStandingLink& Link : OutStandingLinks)
		{
			RigBlueprint->ModelController->MakeLink(Link.A, Link.B, Link.C, Link.D);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(RigBlueprint);

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


void UControlRigGraph::HandleModelModified(const UControlRigModel* InModel, EControlRigModelNotifType InType, const void* InPayload)
{
	if (bSuspendModelNotifications)
	{
		return;
	}

	switch (InType)
	{
		case EControlRigModelNotifType::ModelCleared:
		{
			for (const FControlRigModelNode& Node : InModel->Nodes())
			{
				UControlRigGraphNode* RigNode = FindNodeFromPropertyName(Node.Name);
				if (RigNode != nullptr)
				{
					RemoveNode(RigNode);
				}
			}
			Modify();
			break;
		}
		case EControlRigModelNotifType::NodeAdded:
		{
			const FControlRigModelNode* Node = (const FControlRigModelNode*)InPayload;
			if (Node != nullptr)
			{
				FEdGraphPinType PinType;
				if (Node->IsParameter())
				{
					PinType = Node->Pins[0].Type;
				}
				UEdGraphNode* EdNode = FControlRigBlueprintUtils::InstantiateGraphNodeForProperty(this, Node->Name, Node->Position, PinType);
				if (EdNode != nullptr)
				{
					EdNode->CreateNewGuid();
					if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(EdNode))
					{
						RigNode->ParameterType = (int32)Node->ParameterType;
					}
				}
			}
			break;
		}
		case EControlRigModelNotifType::NodeRemoved:
		{
			const FControlRigModelNode* Node = (const FControlRigModelNode*)InPayload;
			if (Node != nullptr)
			{
				UControlRigGraphNode* RigNode = FindNodeFromPropertyName(Node->Name);
				if (RigNode != nullptr)
				{
					RemoveNode(RigNode);
				}
			}
			break;
		}
		case EControlRigModelNotifType::NodeChanged:
		{
			const FControlRigModelNode* Node = (const FControlRigModelNode*)InPayload;
			if (Node != nullptr)
			{
				UControlRigGraphNode* RigNode = FindNodeFromPropertyName(Node->Name);
				if (RigNode != nullptr)
				{
					RigNode->NodePosX = (int32)Node->Position.X;
					RigNode->NodePosY = (int32)Node->Position.Y;
					RigNode->Modify();
					RigNode->ParameterType = (int32)Node->ParameterType;
				}
			}
			break;
		}
		case EControlRigModelNotifType::NodeRenamed:
		{
			const FControlRigModelNodeRenameInfo* Info = (const FControlRigModelNodeRenameInfo*)InPayload;
			if (Info != nullptr)
			{
				UControlRigGraphNode* RigNode = FindNodeFromPropertyName(Info->OldName);
				if (RigNode != nullptr)
				{
					RigNode->SetPropertyName(Info->NewName, true);
					RigNode->InvalidateNodeTitle();
					RigNode->Modify();
				}
			}
			break;
		}
		case EControlRigModelNotifType::PinAdded:
		case EControlRigModelNotifType::PinRemoved:
		{
			const FControlRigModelPin* Pin = (const FControlRigModelPin*)InPayload;
			if (Pin!= nullptr)
			{
				const FControlRigModelNode& Node = InModel->Nodes()[Pin->Node];
				UControlRigGraphNode* RigNode = FindNodeFromPropertyName(Node.Name);
				if (RigNode != nullptr)
				{
					RigNode->ReconstructNode();
				}
			}
			break;
		}
		case EControlRigModelNotifType::LinkAdded:
		case EControlRigModelNotifType::LinkRemoved:
		{
			bool AddLink = InType == EControlRigModelNotifType::LinkAdded;

			const FControlRigModelLink* Link = (const FControlRigModelLink*)InPayload;
			if (Link != nullptr)
			{
				const FControlRigModelNode& SourceNode = InModel->Nodes()[Link->Source.Node];
				const FControlRigModelPin& SourcePin = SourceNode.Pins[Link->Source.Pin];
				const FControlRigModelNode& TargetNode = InModel->Nodes()[Link->Target.Node];
				const FControlRigModelPin& TargetPin = TargetNode.Pins[Link->Target.Pin];

				UControlRigGraphNode* SourceRigNode = FindNodeFromPropertyName(SourceNode.Name);
				UControlRigGraphNode* TargetRigNode = FindNodeFromPropertyName(TargetNode.Name);

				if (SourceRigNode != nullptr && TargetRigNode != nullptr)
				{
					FString SourcePinPath = InModel->GetPinPath(Link->Source, true);
					FString TargetPinPath = InModel->GetPinPath(Link->Target, true);

					UEdGraphPin* SourceRigPin = SourceRigNode->FindPin(*SourcePinPath, EGPD_Output);
					UEdGraphPin* TargetRigPin = TargetRigNode->FindPin(*TargetPinPath, EGPD_Input);

					if (SourceRigPin != nullptr && TargetRigPin != nullptr)
					{
						if (AddLink)
						{
							SourceRigPin->MakeLinkTo(TargetRigPin);
							SourceRigPin->Modify();
							TargetRigPin->Modify();
						}
						else
						{
							SourceRigPin->BreakLinkTo(TargetRigPin);
							SourceRigPin->Modify();
							TargetRigPin->Modify();
						}
					}
				}

			}
			break;
		}
		case EControlRigModelNotifType::PinChanged:
		{
			const FControlRigModelPin* Pin = (const FControlRigModelPin*)InPayload;
			if (Pin != nullptr)
			{
				const FControlRigModelNode& Node = InModel->Nodes()[Pin->Node];
				UControlRigGraphNode* EdNode = Cast<UControlRigGraphNode>(FindNodeFromPropertyName(Node.Name));
				if (EdNode != nullptr)
				{
					FString PinPath = InModel->GetPinPath(Pin->GetPair());
					UEdGraphPin* EdPin = EdNode->FindPin(*PinPath, Pin->Direction);
					if (EdPin)
					{
						bool bShouldExpand = EdNode->IsPinExpanded(PinPath) != Pin->bExpanded;
						if (bShouldExpand)
						{
							// check if this is an input / output pin,
							// and in that case: don't expand (since it is handled by the input variant)
							if (Pin->Direction == EGPD_Output)
							{
								if (InModel->FindPinFromPath(PinPath, true /*input*/))
								{
									bShouldExpand = false;
								}
							}
						}
						if (bShouldExpand)
						{
							EdNode->SetPinExpansion(PinPath, Pin->bExpanded);
						}
						if (Pin->Direction == EGPD_Input)
						{
							if (!Pin->DefaultValue.IsEmpty())
							{
								if (Pin->Type.PinCategory == UEdGraphSchema_K2::PC_Object)
								{
									UClass* Class = Cast<UClass>(Pin->Type.PinSubCategoryObject);
									if (Class)
									{
										EdPin->DefaultObject = StaticFindObject(Class, ANY_PACKAGE, *Pin->DefaultValue);
									}
								}

								EdPin->DefaultValue = Pin->DefaultValue;
							}
						}
						EdPin->Modify();
					}
				}
			}
			break;
		}
		default:
		{
			// todo... other cases
			break;
		}
	}
}

UControlRigGraphNode* UControlRigGraph::FindNodeFromPropertyName(const FName& InPropertyName)
{
	for (UEdGraphNode* EdNode : Nodes)
	{
		UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(EdNode);
		if (RigNode != nullptr)
		{
			if (RigNode->PropertyName == InPropertyName)
			{
				return RigNode;
			}
		}
	}
	return nullptr;
}

#endif

#undef LOCTEXT_NAMESPACE
