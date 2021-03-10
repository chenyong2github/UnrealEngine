// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraph.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "Graph/ControlRigGraphSchema.h"
#include "ControlRig.h"
#include "RigVMModel/RigVMGraph.h"
#include "ControlRigObjectVersion.h"
#include "Units/RigUnit.h"
#include "EdGraphNode_Comment.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"
#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"
#include "RigVMModel/Nodes/RigVMRerouteNode.h"

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
	bIsSelecting = false;
	bIsFunctionDefinition = false;
}

void UControlRigGraph::Initialize(UControlRigBlueprint* InBlueprint)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	InBlueprint->OnModified().RemoveAll(this);
	InBlueprint->OnModified().AddUObject(this, &UControlRigGraph::HandleModifiedEvent);
	InBlueprint->OnVMCompiled().RemoveAll(this);
	InBlueprint->OnVMCompiled().AddUObject(this, &UControlRigGraph::HandleVMCompiledEvent);

	URigHierarchy* Hierarchy = InBlueprint->Hierarchy;

	if(UControlRig* ControlRig = Cast<UControlRig>(InBlueprint->GetObjectBeingDebugged()))
	{
		Hierarchy = ControlRig->GetHierarchy();
	}

	if(Hierarchy)
	{
		CacheNameLists(Hierarchy, &InBlueprint->DrawContainer);
	}
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

	if (Ar.IsLoading())
	{
		Schema = UControlRigGraphSchema::StaticClass();
	}
}
#endif

#if WITH_EDITOR

TArray<TSharedPtr<FString>> UControlRigGraph::EmptyElementNameList;

void UControlRigGraph::CacheNameLists(URigHierarchy* InHierarchy, const FControlRigDrawContainer* DrawContainer)
{
	if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(GetOuter()))
	{
		return;
	}

	check(InHierarchy);
	check(DrawContainer);

	ElementNameLists.FindOrAdd(ERigElementType::All);
	ElementNameLists.FindOrAdd(ERigElementType::Bone);
	ElementNameLists.FindOrAdd(ERigElementType::Null);
	ElementNameLists.FindOrAdd(ERigElementType::Control);
	ElementNameLists.FindOrAdd(ERigElementType::Curve);
	ElementNameLists.FindOrAdd(ERigElementType::RigidBody);
	ElementNameLists.FindOrAdd(ERigElementType::Socket);

	TArray<TSharedPtr<FString>>& AllNameList = ElementNameLists.FindChecked(ERigElementType::All);
	TArray<TSharedPtr<FString>>& BoneNameList = ElementNameLists.FindChecked(ERigElementType::Bone);
	TArray<TSharedPtr<FString>>& NullNameList = ElementNameLists.FindChecked(ERigElementType::Null);
	TArray<TSharedPtr<FString>>& ControlNameList = ElementNameLists.FindChecked(ERigElementType::Control);
	TArray<TSharedPtr<FString>>& CurveNameList = ElementNameLists.FindChecked(ERigElementType::Curve);
	TArray<TSharedPtr<FString>>& RigidBodyNameList = ElementNameLists.FindChecked(ERigElementType::RigidBody);
	TArray<TSharedPtr<FString>>& SocketNameList = ElementNameLists.FindChecked(ERigElementType::Socket);
	
	CacheNameListForHierarchy<FRigBaseElement>(InHierarchy, AllNameList);
	CacheNameListForHierarchy<FRigBoneElement>(InHierarchy, BoneNameList);
	CacheNameListForHierarchy<FRigNullElement>(InHierarchy, NullNameList);
	CacheNameListForHierarchy<FRigControlElement>(InHierarchy, ControlNameList);
	CacheNameListForHierarchy<FRigCurveElement>(InHierarchy, CurveNameList);
	CacheNameListForHierarchy<FRigRigidBodyElement>(InHierarchy, RigidBodyNameList);
	CacheNameListForHierarchy<FRigSocketElement>(InHierarchy, SocketNameList);
	CacheNameList<FControlRigDrawContainer>(*DrawContainer, DrawingNameList);
}

const TArray<TSharedPtr<FString>>& UControlRigGraph::GetElementNameList(ERigElementType InElementType) const
{
	if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(GetOuter()))
	{
		return OuterGraph->GetElementNameList(InElementType);
	}
	
	if(InElementType == ERigElementType::None)
	{
		return EmptyElementNameList;
	}
	
	if(!ElementNameLists.Contains(InElementType))
	{
		UControlRigBlueprint* Blueprint = GetBlueprint();
		if(Blueprint == nullptr)
		{
			return EmptyElementNameList;
		}

		UControlRigGraph* MutableThis = (UControlRigGraph*)this;
		MutableThis->CacheNameLists(Blueprint->Hierarchy, &Blueprint->DrawContainer);
	}
	return ElementNameLists.FindChecked(InElementType);
}

const TArray<TSharedPtr<FString>>& UControlRigGraph::GetElementNameList(URigVMPin* InPin) const
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

const TArray<TSharedPtr<FString>>& UControlRigGraph::GetDrawingNameList(URigVMPin* InPin) const
{
	if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(GetOuter()))
	{
		return OuterGraph->GetDrawingNameList(InPin);
	}
	return DrawingNameList;
}

void UControlRigGraph::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (bSuspendModelNotifications)
	{
		return;
	}

	// only make sure to receive notifs for this graph - unless
	// we are on a template graph (used by node spawners)
	if (GetModel() != InGraph && TemplateController == nullptr)
	{
		return;
	}

	switch (InNotifType)
	{
		case ERigVMGraphNotifType::GraphChanged:
		{
			for (URigVMNode* Node : InGraph->GetNodes())
			{
				UEdGraphNode* EdNode = FindNodeForModelNodeName(Node->GetFName());
				if (EdNode != nullptr)
				{
					RemoveNode(EdNode);
				}
			}
			break;
		}
		case ERigVMGraphNotifType::NodeSelectionChanged:
		{
			if (bIsSelecting)
			{
				return;
			}
			TGuardValue<bool> SelectionGuard(bIsSelecting, true);

			TSet<const UEdGraphNode*> NodeSelection;
			for (FName NodeName : InGraph->GetSelectNodes())
			{
				if (UEdGraphNode* EdNode = FindNodeForModelNodeName(NodeName))
				{
					NodeSelection.Add(EdNode);
				}
			}
			SelectNodeSet(NodeSelection);
			break;
		}
		case ERigVMGraphNotifType::NodeAdded:
		{
			if (URigVMNode* ModelNode = Cast<URigVMNode>(InSubject))
			{
				if (!ModelNode->IsVisibleInUI())
				{
					if (URigVMInjectionInfo* Injection = ModelNode->GetInjectionInfo())
					{
						if (URigVMPin* ModelPin = Injection->GetPin())
						{
							URigVMNode* ParentModelNode = ModelPin->GetNode();
							if (ParentModelNode)
							{
								UEdGraphNode* EdNode = FindNodeForModelNodeName(ParentModelNode->GetFName());
								if (EdNode)
									{
									if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(EdNode))
									{
										RigNode->ReconstructNode_Internal(true);
									}
								}
							}
						}
					}
					break;
				}

				if (URigVMCommentNode* CommentModelNode = Cast<URigVMCommentNode>(ModelNode))
				{
					UEdGraphNode_Comment* NewNode = NewObject<UEdGraphNode_Comment>(this, CommentModelNode->GetFName());
					AddNode(NewNode, false);

					NewNode->CreateNewGuid();
					NewNode->PostPlacedNewNode();
					NewNode->AllocateDefaultPins();

					NewNode->NodePosX = ModelNode->GetPosition().X;
					NewNode->NodePosY = ModelNode->GetPosition().Y;
					NewNode->NodeWidth = ModelNode->GetSize().X;
					NewNode->NodeHeight = ModelNode->GetSize().Y;
					NewNode->CommentColor = ModelNode->GetNodeColor();
					NewNode->NodeComment = CommentModelNode->GetCommentText();
					NewNode->SetFlags(RF_Transactional);
					NewNode->GetNodesUnderComment();
				}
				else if (URigVMRerouteNode* RerouteModelNode = Cast<URigVMRerouteNode>(ModelNode))
				{
					UControlRigGraphNode* NewNode = NewObject<UControlRigGraphNode>(this, ModelNode->GetFName());
					AddNode(NewNode, false);

					NewNode->ModelNodePath = ModelNode->GetNodePath();
					NewNode->CreateNewGuid();
					NewNode->PostPlacedNewNode();
					NewNode->AllocateDefaultPins();

					NewNode->NodePosX = ModelNode->GetPosition().X;
					NewNode->NodePosY = ModelNode->GetPosition().Y;

					NewNode->SetFlags(RF_Transactional);
					NewNode->AllocateDefaultPins();

					if (UEdGraphPin* ValuePin = NewNode->FindPin(ModelNode->FindPin("Value")->GetPinPath()))
					{
						NewNode->SetColorFromModel(GetSchema()->GetPinTypeColor(ValuePin->PinType));
					}
				}
				else // struct, library, parameter + variable
				{
					UControlRigGraphNode* NewNode = NewObject<UControlRigGraphNode>(this, ModelNode->GetFName());
					AddNode(NewNode, false);

					NewNode->ModelNodePath = ModelNode->GetNodePath();
					NewNode->CreateNewGuid();
					NewNode->PostPlacedNewNode();
					NewNode->AllocateDefaultPins();

					NewNode->NodePosX = ModelNode->GetPosition().X;
					NewNode->NodePosY = ModelNode->GetPosition().Y;
					NewNode->SetColorFromModel(ModelNode->GetNodeColor());
					NewNode->SetFlags(RF_Transactional);
					NewNode->AllocateDefaultPins();
				}
			}
			break;
		}
		case ERigVMGraphNotifType::NodeRemoved:
		{
			if (URigVMNode* ModelNode = Cast<URigVMNode>(InSubject))
			{
				if (URigVMInjectionInfo* Injection = ModelNode->GetInjectionInfo())
				{
					if (URigVMPin* ModelPin = Injection->GetPin())
					{
						URigVMNode* ParentModelNode = ModelPin->GetNode();
						if (ParentModelNode)
						{
							UEdGraphNode* EdNode = FindNodeForModelNodeName(ParentModelNode->GetFName());
							if (EdNode)
							{
								if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(EdNode))
								{
									RigNode->ReconstructNode_Internal(true);
								}
							}
						}
					}
					break;
				}

				UEdGraphNode* EdNode = FindNodeForModelNodeName(ModelNode->GetFName());
				if (EdNode)
				{
					RemoveNode(EdNode, true);
					NotifyGraphChanged();
				}
			}
			break;
		}
		case ERigVMGraphNotifType::NodePositionChanged:
		{
			if (URigVMNode* ModelNode = Cast<URigVMNode>(InSubject))
			{
				UEdGraphNode* EdNode = FindNodeForModelNodeName(ModelNode->GetFName());
				if (EdNode)
				{
					// No need to call Node->Modify(), since control rig has its own undo/redo system see RigVMControllerActions.cpp
					EdNode->NodePosX = (int32)ModelNode->GetPosition().X;
					EdNode->NodePosY = (int32)ModelNode->GetPosition().Y;
				}
			}
			break;
		}
		case ERigVMGraphNotifType::NodeSizeChanged:
		{
			if (URigVMNode* ModelNode = Cast<URigVMNode>(InSubject))
			{
				UEdGraphNode_Comment* EdNode = Cast<UEdGraphNode_Comment>(FindNodeForModelNodeName(ModelNode->GetFName()));
				if (EdNode)
				{
					// No need to call Node->Modify(), since control rig has its own undo/redo system see RigVMControllerActions.cpp
					EdNode->NodeWidth = (int32)ModelNode->GetSize().X;
					EdNode->NodeHeight = (int32)ModelNode->GetSize().Y;
				}
			}
			break;
		}
		case ERigVMGraphNotifType::RerouteCompactnessChanged:
		{
			if (URigVMRerouteNode* ModelNode = Cast<URigVMRerouteNode>(InSubject))
			{
				UEdGraphNode* EdNode = Cast<UEdGraphNode>(FindNodeForModelNodeName(ModelNode->GetFName()));
				if (EdNode)
				{
					if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(EdNode))
					{
						// start at index 2 (the subpins below the top level value pin)
						// and hide the pins (or show them if they were hidden previously)
						for (int32 PinIndex = 2; PinIndex < RigNode->Pins.Num(); PinIndex++)
						{
							RigNode->Pins[PinIndex]->bHidden = !ModelNode->GetShowsAsFullNode();
						}
						NotifyGraphChanged();
					}
				}
			}
			break;
		}
		case ERigVMGraphNotifType::NodeColorChanged:
		{
			if (URigVMNode* ModelNode = Cast<URigVMNode>(InSubject))
			{
				if (ModelNode->IsA<URigVMLibraryNode>())
				{
					if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(FindNodeForModelNodeName(ModelNode->GetFName())))
					{
						RigNode->SetColorFromModel(ModelNode->GetNodeColor());
					}
				}
				else if(UEdGraphNode_Comment * EdComment = Cast<UEdGraphNode_Comment>(FindNodeForModelNodeName(ModelNode->GetFName())))
				{
					EdComment->CommentColor = ModelNode->GetNodeColor();
				}
			}
			break;
		}
		case ERigVMGraphNotifType::CommentTextChanged:
		{
			if (URigVMCommentNode* ModelNode = Cast<URigVMCommentNode>(InSubject))
			{
				UEdGraphNode_Comment* EdNode = Cast<UEdGraphNode_Comment>(FindNodeForModelNodeName(ModelNode->GetFName()));
				if (EdNode)
				{
					EdNode->OnUpdateCommentText(ModelNode->GetCommentText());
				}
			}
			break;
		}
		case ERigVMGraphNotifType::LinkAdded:
		case ERigVMGraphNotifType::LinkRemoved:
		{
			bool AddLink = InNotifType == ERigVMGraphNotifType::LinkAdded;

			if (URigVMLink* Link = Cast<URigVMLink>(InSubject))
			{
				URigVMPin* SourcePin = Link->GetSourcePin();
				URigVMPin* TargetPin = Link->GetTargetPin();

				if (SourcePin)
				{
					SourcePin = SourcePin->GetOriginalPinFromInjectedNode();
				}
				if (TargetPin)
				{
					TargetPin = TargetPin->GetOriginalPinFromInjectedNode();
				}

				if (SourcePin && TargetPin && SourcePin != TargetPin)
				{
					UControlRigGraphNode* SourceRigNode = Cast<UControlRigGraphNode>(FindNodeForModelNodeName(SourcePin->GetNode()->GetFName()));
					UControlRigGraphNode* TargetRigNode = Cast<UControlRigGraphNode>(FindNodeForModelNodeName(TargetPin->GetNode()->GetFName()));

					if (SourceRigNode != nullptr && TargetRigNode != nullptr)
					{
						FString SourcePinPath = SourcePin->GetPinPath();
						FString TargetPinPath = TargetPin->GetPinPath();
						UEdGraphPin* SourceRigPin = SourceRigNode->FindPin(*SourcePinPath, EGPD_Output);
						UEdGraphPin* TargetRigPin = TargetRigNode->FindPin(*TargetPinPath, EGPD_Input);

						if (SourceRigPin != nullptr && TargetRigPin != nullptr)
						{
							if (AddLink)
							{
								SourceRigPin->MakeLinkTo(TargetRigPin);
							}
							else
							{
								SourceRigPin->BreakLinkTo(TargetRigPin);
							}

							for (int32 LinkedPinIndex = 0; LinkedPinIndex < SourceRigPin->LinkedTo.Num();)
							{
								if (SourceRigPin->LinkedTo[LinkedPinIndex])
								{
									++LinkedPinIndex;
								}
								else
								{
									SourceRigPin->LinkedTo.RemoveAtSwap(LinkedPinIndex);
								}
							}

							for (int32 LinkedPinIndex = 0; LinkedPinIndex < TargetRigPin->LinkedTo.Num();)
							{
								if (TargetRigPin->LinkedTo[LinkedPinIndex])
								{
									++LinkedPinIndex;
								}
								else
								{
									TargetRigPin->LinkedTo.RemoveAtSwap(LinkedPinIndex);
								}
							}
						}
					}
				}
			}
			break;
		}
		case ERigVMGraphNotifType::PinDefaultValueChanged:
		{
			if (URigVMPin* ModelPin = Cast<URigVMPin>(InSubject))
			{
				if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(FindNodeForModelNodeName(ModelPin->GetNode()->GetFName())))
				{
					UEdGraphPin* RigNodePin = RigNode->FindPin(ModelPin->GetPinPath());
					if (RigNodePin == nullptr)
					{
						break;
					}

					RigNode->SetupPinDefaultsFromModel(RigNodePin);

					if (Cast<URigVMVariableNode>(ModelPin->GetNode()))
					{
						if (ModelPin->GetName() == TEXT("Variable"))
						{
							RigNode->InvalidateNodeTitle();
							RigNode->ReconstructNode_Internal(true);
						}
					}
					else if (Cast<URigVMParameterNode>(ModelPin->GetNode()))
					{
						if (ModelPin->GetName() == TEXT("Parameter"))
						{
							RigNode->InvalidateNodeTitle();
							RigNode->ReconstructNode_Internal(true);
						}
					}
					else if (Cast<URigVMUnitNode>(ModelPin->GetNode()))
					{
						RigNode->InvalidateNodeTitle();
					}
				}
				else if (URigVMInjectionInfo* Injection = ModelPin->GetNode()->GetInjectionInfo())
				{
					if (Injection->InputPin != ModelPin->GetRootPin())
					{
						if (URigVMPin* InjectionPin = Injection->GetPin())
						{
							URigVMNode* ParentModelNode = InjectionPin->GetNode();
							if (ParentModelNode)
							{
								UEdGraphNode* HostEdNode = FindNodeForModelNodeName(ParentModelNode->GetFName());
								if (HostEdNode)
								{
									if (UControlRigGraphNode* HostRigNode = Cast<UControlRigGraphNode>(HostEdNode))
									{
										HostRigNode->ReconstructNode_Internal(true);
									}
								}
							}
						}
					}
				}
			}
			break;
		}
		case ERigVMGraphNotifType::PinArraySizeChanged:
		case ERigVMGraphNotifType::PinDirectionChanged:
		case ERigVMGraphNotifType::PinTypeChanged:
		case ERigVMGraphNotifType::PinIndexChanged:
		case ERigVMGraphNotifType::PinBoundVariableChanged:
		case ERigVMGraphNotifType::PinAdded:
		case ERigVMGraphNotifType::PinRemoved:
		case ERigVMGraphNotifType::PinRenamed:
		{
			if (URigVMPin* ModelPin = Cast<URigVMPin>(InSubject))
			{
				if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(FindNodeForModelNodeName(ModelPin->GetNode()->GetFName())))
				{
					RigNode->ReconstructNode_Internal(true);
				}
			}
			break;
		}
		case ERigVMGraphNotifType::NodeRenamed:
		{
			if (URigVMNode* ModelNode = Cast<URigVMNode>(InSubject))
			{
				if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(FindNodeForModelNodeName(ModelNode->GetPreviousFName())))
				{
					RigNode->Rename(*ModelNode->GetName());
					RigNode->ModelNodePath = ModelNode->GetNodePath();
					RigNode->InvalidateNodeTitle();
					RigNode->ReconstructNode_Internal(true);
				}
			}
			break;
		}
		case ERigVMGraphNotifType::VariableRenamed:
		case ERigVMGraphNotifType::NodeReferenceChanged:
		{
			if (URigVMNode* ModelNode = Cast<URigVMNode>(InSubject))
			{
				if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(FindNodeForModelNodeName(ModelNode->GetFName())))
				{
					RigNode->InvalidateNodeTitle();
				}
			}
			break;
		}
		case ERigVMGraphNotifType::NodeSelected:
		{
			if (URigVMCommentNode* ModelNode = Cast<URigVMCommentNode>(InSubject))
			{
				// UEdGraphNode_Comment cannot access RigVMCommentNode's selection state, so we have to manually toggle its selection state
				// UControlRigGraphNode does not need this step because it overrides the IsSelectedInEditor() method
				UEdGraphNode_Comment* EdNode = Cast<UEdGraphNode_Comment>(FindNodeForModelNodeName(ModelNode->GetFName()));
				if (EdNode)
				{
					GSelectedObjectAnnotation.Set(EdNode);
				}
			}
			break;
		}
		case ERigVMGraphNotifType::NodeDeselected:
		{
			if (URigVMCommentNode* ModelNode = Cast<URigVMCommentNode>(InSubject))
			{
				UEdGraphNode_Comment* EdNode = Cast<UEdGraphNode_Comment>(FindNodeForModelNodeName(ModelNode->GetFName()));
				if (EdNode)
				{
					GSelectedObjectAnnotation.Clear(EdNode);
				}
			}
			break;
		}
		case ERigVMGraphNotifType::PinExpansionChanged:
		default:
		{
			break;
		}
	}
}

int32 UControlRigGraph::GetInstructionIndex(UControlRigGraphNode* InNode)
{
	if (const int32* FoundIndex = CachedInstructionIndices.Find(InNode->GetModelNode()))
	{
		return *FoundIndex;
	}

	struct Local
	{
		static int32 GetInstructionIndex(URigVMNode* InModelNode, const FRigVMByteCode* InByteCode, TMap<URigVMNode*, int32>& Indices)
		{
			if (InModelNode == nullptr)
			{
				return INDEX_NONE;
			}

			if (const int32* ExistingIndex = Indices.Find(InModelNode))
			{
				return *ExistingIndex;
			}

			Indices.Add(InModelNode, INDEX_NONE);

			int32 InstructionIndex = InByteCode->GetFirstInstructionIndexForSubject(InModelNode);
			if (InstructionIndex != INDEX_NONE)
			{
				Indices.FindOrAdd(InModelNode) = InstructionIndex;
				return InstructionIndex;
			}

			TArray<URigVMNode*> NodesToCheck;
			if (URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(InModelNode))
			{
				NodesToCheck.Append(LibraryNode->GetContainedNodes());
			}

			if (InModelNode->IsA<URigVMFunctionReturnNode>() ||
				InModelNode->IsA<URigVMRerouteNode>())
			{
				NodesToCheck.Append(InModelNode->GetLinkedSourceNodes());
			}

			if (InModelNode->IsA<URigVMFunctionEntryNode>() ||
				InModelNode->IsA<URigVMRerouteNode>())
			{
				NodesToCheck.Append(InModelNode->GetLinkedTargetNodes());
			}

			int32 MinimumInstructionIndex = INDEX_NONE;
			for (URigVMNode* NodeToCheck : NodesToCheck)
			{
				int32 ContainedInstructionIndex = GetInstructionIndex(NodeToCheck, InByteCode, Indices);
				if (ContainedInstructionIndex != INDEX_NONE)
				{
					if (ContainedInstructionIndex < MinimumInstructionIndex || MinimumInstructionIndex == INDEX_NONE)
					{
						MinimumInstructionIndex = ContainedInstructionIndex;
					}
				}
			}

			Indices.FindOrAdd(InModelNode) = MinimumInstructionIndex;
			return MinimumInstructionIndex;
		}
	};

	if (const FRigVMByteCode* ByteCode = GetController()->GetCurrentByteCode())
	{
		return Local::GetInstructionIndex(InNode->GetModelNode(), ByteCode, CachedInstructionIndices);
	}

	return INDEX_NONE;
}

UEdGraphNode* UControlRigGraph::FindNodeForModelNodeName(const FName& InModelNodeName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	for (UEdGraphNode* EdNode : Nodes)
	{
		if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(EdNode))
		{
			if (RigNode->ModelNodePath == InModelNodeName.ToString())
			{
				return EdNode;
			}
		}
		else
		{
			if (EdNode->GetFName() == InModelNodeName)
			{
				return EdNode;
			}
		}
	}
	return nullptr;
}

UControlRigBlueprint* UControlRigGraph::GetBlueprint() const
{
	if (UControlRigGraph* OuterGraph = Cast<UControlRigGraph>(GetOuter()))
	{
		return OuterGraph->GetBlueprint();
	}
	return Cast<UControlRigBlueprint>(GetOuter());
}

URigVMGraph* UControlRigGraph::GetModel() const
{
	if (UControlRigBlueprint* Blueprint = GetBlueprint())
	{
		return Blueprint->GetModel(this);
	}
	return nullptr;
}

URigVMController* UControlRigGraph::GetController() const
{
	if (UControlRigBlueprint* Blueprint = GetBlueprint())
	{
		return Blueprint->GetOrCreateController(this);
	}
	return nullptr;
}

URigVMController* UControlRigGraph::GetTemplateController()
{
	if (TemplateController == nullptr)
	{
		TemplateController = GetBlueprint()->GetTemplateController();
		TemplateController->OnModified().RemoveAll(this);
		TemplateController->OnModified().AddUObject(this, &UControlRigGraph::HandleModifiedEvent);
	}
	return TemplateController;
}

void UControlRigGraph::HandleVMCompiledEvent(UBlueprint* InBlueprint, URigVM* InVM)
{
	CachedInstructionIndices.Reset();
}

#endif

FControlRigPublicFunctionData UControlRigGraph::GetPublicFunctionData() const
{
	FControlRigPublicFunctionData Data;

	FString Prefix, ModelNodeName;
	if(!URigVMNode::SplitNodePathAtEnd(ModelNodePath, Prefix, ModelNodeName))
	{
		ModelNodeName = ModelNodePath;
	}
	Data.Name = *ModelNodeName;

	if(URigVMGraph* RigGraph = GetModel())
	{
		if(URigVMCollapseNode* FunctionNode = Cast<URigVMCollapseNode>(RigGraph->GetOuter()))
		{
			Data.Category = FunctionNode->GetNodeCategory();
			Data.Keywords = FunctionNode->GetNodeKeywords();
			
			for(URigVMPin* Pin : FunctionNode->GetPins())
			{
				FControlRigPublicFunctionArg Arg;
				Arg.Name = Pin->GetFName();
				Arg.bIsArray = Pin->IsArray();
				Arg.Direction = Pin->GetDirection();
				Arg.CPPType = *Pin->GetCPPType();
				if(Pin->GetCPPTypeObject())
				{
					Arg.CPPTypeObjectPath = *Pin->GetCPPTypeObject()->GetPathName();
				}
				Data.Arguments.Add(Arg);
			}
		}
	}

	return Data;
}

#undef LOCTEXT_NAMESPACE
