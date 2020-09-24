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
}

void UControlRigGraph::Initialize(UControlRigBlueprint* InBlueprint)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	InBlueprint->OnModified().RemoveAll(this);
	InBlueprint->OnModified().AddUObject(this, &UControlRigGraph::HandleModifiedEvent);
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

void UControlRigGraph::CacheNameLists(const FRigHierarchyContainer* HierarchyContainer, const FControlRigDrawContainer* DrawContainer)
{
	check(HierarchyContainer);
	check(DrawContainer);

	CacheNameList<FRigBoneHierarchy>(HierarchyContainer->BoneHierarchy, BoneNameList);
	CacheNameList<FRigControlHierarchy>(HierarchyContainer->ControlHierarchy, ControlNameList);
	CacheNameList<FRigSpaceHierarchy>(HierarchyContainer->SpaceHierarchy, SpaceNameList);
	CacheNameList<FRigCurveContainer>(HierarchyContainer->CurveContainer, CurveNameList);
	CacheNameList<FControlRigDrawContainer>(*DrawContainer, DrawingNameList);
}

const TArray<TSharedPtr<FString>>& UControlRigGraph::GetBoneNameList(URigVMPin* InPin) const
{
	return BoneNameList;
}

const TArray<TSharedPtr<FString>>& UControlRigGraph::GetControlNameList(URigVMPin* InPin) const
{
	return ControlNameList;
}

const TArray<TSharedPtr<FString>>& UControlRigGraph::GetSpaceNameList(URigVMPin* InPin) const
{
	return SpaceNameList;
}

const TArray<TSharedPtr<FString>>& UControlRigGraph::GetCurveNameList(URigVMPin* InPin) const
{
	return CurveNameList;
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

const TArray<TSharedPtr<FString>>& UControlRigGraph::GetElementNameList(ERigElementType InElementType) const
{
	switch (InElementType)
	{
		case ERigElementType::Bone:
		{
			return GetBoneNameList();
		}
		case ERigElementType::Control:
		{
			return GetControlNameList();
		}
		case ERigElementType::Space:
		{
			return GetSpaceNameList();
		}
		case ERigElementType::Curve:
		{
			return GetCurveNameList();
		}
	}

	return GetBoneNameList(nullptr);
}

const TArray<TSharedPtr<FString>>& UControlRigGraph::GetDrawingNameList(URigVMPin* InPin) const
{
	return DrawingNameList;
}

void UControlRigGraph::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (bSuspendModelNotifications)
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
				else // struct, parameter + variable
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
				UEdGraphNode_Comment* EdNode = Cast<UEdGraphNode_Comment>(FindNodeForModelNodeName(ModelNode->GetFName()));
				if (EdNode)
				{
					EdNode->CommentColor = ModelNode->GetNodeColor();
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

				SourcePin = SourcePin->GetOriginalPinFromInjectedNode();
				TargetPin = TargetPin->GetOriginalPinFromInjectedNode();

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
					else if (Cast<URigVMStructNode>(ModelPin->GetNode()))
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
		case ERigVMGraphNotifType::VariableRenamed:
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


URigVMController* UControlRigGraph::GetTemplateController()
{
	if (TemplateModel == nullptr)
	{
		TemplateModel = NewObject<URigVMGraph>(this, TEXT("TemplateModel"));
	}
	if (TemplateController == nullptr)
	{
		TemplateController = NewObject<URigVMController>(this, TEXT("TemplateController"));
		TemplateController->SetExecuteContextStruct(FControlRigExecuteContext::StaticStruct());
		TemplateController->SetGraph(TemplateModel);
		TemplateController->EnableReporting(false);
		TemplateController->OnModified().AddUObject(this, &UControlRigGraph::HandleModifiedEvent);

		TemplateController->SetFlags(RF_Transient);
		TemplateModel->SetFlags(RF_Transient);
	}
	return TemplateController;
}

#endif

#undef LOCTEXT_NAMESPACE
