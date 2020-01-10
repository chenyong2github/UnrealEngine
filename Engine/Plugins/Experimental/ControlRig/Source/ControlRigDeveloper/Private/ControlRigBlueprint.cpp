// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Modules/ModuleManager.h"
#include "Engine/SkeletalMesh.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "ControlRig.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "IControlRigEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ControlRigBlueprintUtils.h"
#include "Settings/ControlRigSettings.h"
#endif//WITH_EDITOR

#define LOCTEXT_NAMESPACE "ControlRigBlueprint"

UControlRigBlueprint::UControlRigBlueprint()
{
	bSuspendModelNotificationsForSelf = false;
	bSuspendModelNotificationsForOthers = false;
	Model = nullptr;
	ModelController = nullptr;

#if WITH_EDITORONLY_DATA
	GizmoLibrary = UControlRigSettings::Get()->DefaultGizmoLibrary;
#endif
}

void UControlRigBlueprint::InitializeModel()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Model == nullptr || ModelController == nullptr)
	{
		Model = NewObject<UControlRigModel>(this);
		ModelController = NewObject<UControlRigController>(this);
		ModelController->SetModel(Model);
		ModelController->OnModified().AddUObject(this, &UControlRigBlueprint::HandleModelModified);

		for (int32 i = 0; i < UbergraphPages.Num(); ++i)
		{
			if (UControlRigGraph* Graph = Cast<UControlRigGraph>(UbergraphPages[i]))
			{
				Graph->Initialize(this);
			}
		}

		HierarchyContainer.OnElementAdded.AddUObject(this, &UControlRigBlueprint::HandleOnElementAdded);
		HierarchyContainer.OnElementRemoved.AddUObject(this, &UControlRigBlueprint::HandleOnElementRemoved);
		HierarchyContainer.OnElementRenamed.AddUObject(this, &UControlRigBlueprint::HandleOnElementRenamed);
		HierarchyContainer.OnElementReparented.AddUObject(this, &UControlRigBlueprint::HandleOnElementReparented);
		HierarchyContainer.OnElementSelected.AddUObject(this, &UControlRigBlueprint::HandleOnElementSelected);
	}
}

UControlRigBlueprintGeneratedClass* UControlRigBlueprint::GetControlRigBlueprintGeneratedClass() const
{
	UControlRigBlueprintGeneratedClass* Result = Cast<UControlRigBlueprintGeneratedClass>(*GeneratedClass);
	return Result;
}

UControlRigBlueprintGeneratedClass* UControlRigBlueprint::GetControlRigBlueprintSkeletonClass() const
{
	UControlRigBlueprintGeneratedClass* Result = Cast<UControlRigBlueprintGeneratedClass>(*SkeletonGeneratedClass);
	return Result;
}

UClass* UControlRigBlueprint::GetBlueprintClass() const
{
	return UControlRigBlueprintGeneratedClass::StaticClass();
}

void UControlRigBlueprint::LoadModulesRequiredForCompilation() 
{
}

void UControlRigBlueprint::MakePropertyLink(const FString& InSourcePropertyPath, const FString& InDestPropertyPath, int32 InSourceLinkIndex, int32 InDestLinkIndex)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	PropertyLinks.AddUnique(FControlRigBlueprintPropertyLink(InSourcePropertyPath, InDestPropertyPath, InSourceLinkIndex, InDestLinkIndex));
}

USkeletalMesh* UControlRigBlueprint::GetPreviewMesh() const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (!PreviewSkeletalMesh.IsValid())
	{
		PreviewSkeletalMesh.LoadSynchronous();
	}

	return PreviewSkeletalMesh.Get();
}

void UControlRigBlueprint::SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty/*=true*/)
{
	if(bMarkAsDirty)
	{
		Modify();
	}

	PreviewSkeletalMesh = PreviewMesh;
}

void UControlRigBlueprint::GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	IControlRigEditorModule::Get().GetTypeActions(this, ActionRegistrar);
}

void UControlRigBlueprint::GetInstanceActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	IControlRigEditorModule::Get().GetInstanceActions(this, ActionRegistrar);
}

void UControlRigBlueprint::SetObjectBeingDebugged(UObject* NewObject)
{
	UControlRig* PreviousRigBeingDebugged = Cast<UControlRig>(GetObjectBeingDebugged());
	if (PreviousRigBeingDebugged && PreviousRigBeingDebugged != NewObject)
	{
		PreviousRigBeingDebugged->DrawInterface = nullptr;
		PreviousRigBeingDebugged->ControlRigLog = nullptr;
	}

	Super::SetObjectBeingDebugged(NewObject);
}

void UControlRigBlueprint::PostLoad()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::PostLoad();

	// remove all non-controlrig-graphs
	TArray<UEdGraph*> NewUberGraphPages;
	for (UEdGraph* Graph : UbergraphPages)
	{
		UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
		if (RigGraph)
		{
			NewUberGraphPages.Add(RigGraph);
		}
		else
		{
			Graph->MarkPendingKill();
			Graph->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders);
		}
	}
	UbergraphPages = NewUberGraphPages;
}

void UControlRigBlueprint::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	Super::PostTransacted(TransactionEvent);

	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		if (TransactionEvent.GetChangedProperties().Contains(TEXT("HierarchyContainer")))
		{
			PropagateHierarchyFromBPToInstances(true);
			HierarchyContainer.OnElementChanged.Broadcast(&HierarchyContainer, FRigElementKey());

			// make sure the bone name list is up 2 date for the editor graph
			for (UEdGraph* Graph : UbergraphPages)
			{
				UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
				if (RigGraph == nullptr)
				{
					continue;
				}
				RigGraph->CacheNameLists(&HierarchyContainer);
			}
		}
	}
}


UControlRigModel::FModifiedEvent& UControlRigBlueprint::OnModified()
{
	return _ModifiedEvent;
}

void UControlRigBlueprint::PopulateModelFromGraph(const UControlRigGraph* InGraph)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Model != nullptr)
	{
		return;
	}

	TGuardValue<bool> ReentrantGuardSelf(bSuspendModelNotificationsForSelf, true);
	{
		TGuardValue<bool> ReentrantGuardOthers(bSuspendModelNotificationsForOthers, true);

		InitializeModel();
		ModelController->Clear();
		NodeToParameterType.Reset();

		for (const UEdGraphNode* Node : InGraph->Nodes)
		{
			if (const UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
			{
				FName NodeName = RigNode->GetPropertyName();
				FVector2D NodePosition = FVector2D((float)RigNode->NodePosX, (float)RigNode->NodePosY);
				UScriptStruct* UnitStruct = RigNode->GetUnitScriptStruct();
				if (UnitStruct)
				{
					FName TypeName = UnitStruct->GetFName();
					ModelController->AddNode(TypeName, NodePosition, NodeName, false);
				}
				else if (NodeName != NAME_None) // check if this is a variable
				{
					FName DataType = RigNode->PinType.PinCategory;
					if (DataType == NAME_None)
					{
						continue;
					}
					if (DataType == UEdGraphSchema_K2::PC_Struct)
					{
						DataType = NAME_None;

						UScriptStruct* DataStruct = Cast<UScriptStruct>(RigNode->PinType.PinSubCategoryObject);
						if (DataStruct != nullptr)
						{
							DataType = DataStruct->GetFName();
						}
					}

					EControlRigModelParameterType ParameterType = (EControlRigModelParameterType)RigNode->ParameterType;
					if (ParameterType == EControlRigModelParameterType::None)
					{
						ParameterType = EControlRigModelParameterType::Hidden;
					}
					ModelController->AddParameter(NodeName, DataType, ParameterType, NodePosition, false);
					NodeToParameterType.Add(NodeName, ParameterType);
				}
				else
				{
					continue;
				}

				for (UEdGraphPin* Pin : RigNode->Pins)
				{
					FString Left, Right;
					Model->SplitPinPath(Pin->GetName(), Left, Right);

					if (Pin->Direction == EGPD_Input && Pin->PinType.ContainerType == EPinContainerType::Array)
					{
						int32 ArraySize = Pin->SubPins.Num();
						ModelController->SetArrayPinSize(NodeName, *Right, ArraySize, FString(), false);
					}
					if (RigNode->IsPinExpanded(Pin->GetName()))
					{
						ModelController->ExpandPin(NodeName, *Right, Pin->Direction == EGPD_Input, true, false);
					}
					if (!Pin->DefaultValue.IsEmpty() && Pin->Direction == EGPD_Input)
					{
						ModelController->SetPinDefaultValue(NodeName, *Right, Pin->DefaultValue, false, false);
					}
				}
			}
			else if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
			{
				FVector2D NodePosition = FVector2D((float)CommentNode->NodePosX, (float)CommentNode->NodePosY);
				FVector2D NodeSize = FVector2D((float)CommentNode->NodeWidth, (float)CommentNode->NodeHeight);
				ModelController->AddComment(CommentNode->GetFName(), CommentNode->NodeComment, NodePosition, NodeSize, CommentNode->CommentColor, false);
			}
		}

		for (const UEdGraphNode* Node : InGraph->Nodes)
		{
			if (const UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(Node))
			{
				for (const UEdGraphPin* Pin : RigNode->Pins)
				{
					if (Pin->Direction == EGPD_Input)
					{
						continue;
					}

					FString Left, Right;
					Model->SplitPinPath(Pin->GetName(), Left, Right);
					for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						const UControlRigGraphNode* LinkedRigNode = Cast<UControlRigGraphNode>(LinkedPin->GetOwningNode());
						if (LinkedRigNode != nullptr)
						{
							FString LinkedLeft, LinkedRight;
							Model->SplitPinPath(LinkedPin->GetName(), LinkedLeft, LinkedRight);
							ModelController->MakeLink(RigNode->PropertyName, *Right, LinkedRigNode->PropertyName, *LinkedRight, nullptr, false);
						}
					}
				}
			}
		}
	}
}

void UControlRigBlueprint::RebuildGraphFromModel()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TGuardValue<bool> SelfGuard(bSuspendModelNotificationsForSelf, true);
	check(ModelController);
	ModelController->ResendAllNotifications();
}

void UControlRigBlueprint::HandleModelModified(const UControlRigModel* InModel, EControlRigModelNotifType InType, const void* InPayload)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (!bSuspendModelNotificationsForSelf)
	{

#if WITH_EDITOR

		switch (InType)
		{
			case EControlRigModelNotifType::ModelCleared:
			{
				LastNameFromNotification = NAME_None;
				NodeToParameterType.Reset();

				for (const FControlRigModelNode& Node : InModel->Nodes())
				{
					FBlueprintEditorUtils::RemoveMemberVariable(this, Node.Name);
				}
				break;
			}
			case EControlRigModelNotifType::NodeAdded:
			{
				LastNameFromNotification = NAME_None;

				const FControlRigModelNode* Node = (const FControlRigModelNode*)InPayload;
				bool bValidNode = false;
				if (Node != nullptr)
				{
					LastNameFromNotification = Node->Name;
					switch (Node->NodeType)
					{
						case EControlRigModelNodeType::Parameter:
						{
							FControlRigBlueprintUtils::AddPropertyMember(this, Node->Pins[0].Type, *Node->Name.ToString());
							HandleModelModified(InModel, EControlRigModelNotifType::NodeChanged, InPayload);
							NodeToParameterType.Add(Node->Name, Node->ParameterType);
							bValidNode = true;
							break;
						}
						case EControlRigModelNodeType::Function:
						{
							FControlRigBlueprintUtils::AddUnitMember(this, Node->UnitStruct(), Node->Name);
							bValidNode = true;
							break;
						}
						default:
						{
							break;
						}
					}
				}

				if (bValidNode)
				{
					FBlueprintEditorUtils::MarkBlueprintAsModified(this);

					if (Node != nullptr)
					{
						if (Node->IsParameter())
						{
							UpdateParametersOnControlRig();
						}
					}
				}
				break;
			}
			case EControlRigModelNotifType::NodeRemoved:
			{
				LastNameFromNotification = NAME_None;
					
				const FControlRigModelNode* Node = (const FControlRigModelNode*)InPayload;
				if (Node != nullptr)
				{
					LastNameFromNotification = Node->Name;
					FBlueprintEditorUtils::RemoveMemberVariable(this, Node->Name);
					FBlueprintEditorUtils::MarkBlueprintAsModified(this);
					WatchedPins.Reset();
					NodeToParameterType.Remove(Node->Name);
				}
				break;
			}
			case EControlRigModelNotifType::NodeChanged:
			{
				LastNameFromNotification = NAME_None;
				
				const FControlRigModelNode* Node = (const FControlRigModelNode*)InPayload;
				if (Node != nullptr)
				{
					LastNameFromNotification = Node->Name;

					if (Node->IsParameter())
					{
						FProperty* Property = GeneratedClass->FindPropertyByName(Node->Name);
						if (Property != nullptr)
						{
							EControlRigModelParameterType WasParameterType = EControlRigModelParameterType::Hidden;
							if (NodeToParameterType.Contains(Node->Name))
							{
								WasParameterType = NodeToParameterType.FindChecked(Node->Name);
							}
							else
							{
								bool bWasInput = Property->HasMetaData(UControlRig::AnimationInputMetaName);
								bool bWasOutput = Property->HasMetaData(UControlRig::AnimationOutputMetaName);
								if (bWasInput)
								{
									WasParameterType = EControlRigModelParameterType::Input;
								}
								else if (bWasOutput)
								{
									WasParameterType = EControlRigModelParameterType::Output;
								}
								NodeToParameterType.Add(Node->Name, WasParameterType);
							}

							if (WasParameterType != Node->ParameterType)
							{
								Property->RemoveMetaData(UControlRig::AnimationInputMetaName);
								Property->RemoveMetaData(UControlRig::AnimationOutputMetaName);

								if (Node->ParameterType == EControlRigModelParameterType::Input)
								{
									Property->SetMetaData(UControlRig::AnimationInputMetaName, TEXT("True"));
								}
								else if (Node->ParameterType == EControlRigModelParameterType::Output)
								{
									Property->SetMetaData(UControlRig::AnimationOutputMetaName, TEXT("True"));
								}

								NodeToParameterType[Node->Name] = Node->ParameterType;
								FBlueprintEditorUtils::MarkBlueprintAsModified(this);
								UpdateParametersOnControlRig();
							}
						}
					}
				}

				break;
			}
			case EControlRigModelNotifType::NodeRenamed:
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(this);
				break;
			}
			case EControlRigModelNotifType::PinAdded:
			case EControlRigModelNotifType::PinRemoved:
			{
				const FControlRigModelPin* Pin = (const FControlRigModelPin*)InPayload;
				if (Pin)
				{
					if (Pin->ParentIndex != INDEX_NONE)
					{
						const FControlRigModelNode& Node = InModel->Nodes()[Pin->Node];
						const FControlRigModelPin& ParentPin = Node.Pins[Pin->ParentIndex];
						if (ParentPin.IsArray())
						{
							FString PinPath = InModel->GetPinPath(ParentPin.GetPair(), true);

							if (InType == EControlRigModelNotifType::PinAdded)
							{
								PerformArrayOperation(PinPath, [](FScriptArrayHelper& InArrayHelper, int32 InArrayIndex)
								{
									InArrayHelper.AddValue();
									return true;
								}, true, true);
							}
							else
							{
								PerformArrayOperation(PinPath, [](FScriptArrayHelper& InArrayHelper, int32 InArrayIndex)
								{
									// for now let's remove the last one
									InArrayHelper.RemoveValues(InArrayHelper.Num() - 1, 1);
									//InArrayHelper.RemoveValues(InArrayIndex, 0);
									return true;
								}, true, true);
							}
						}
					}
				}
				break;
			}
			case EControlRigModelNotifType::PinChanged:
			{
				const FControlRigModelPin* Pin = (const FControlRigModelPin*)InPayload;
				if (Pin)
				{
					if (UClass* MyControlRigClass = GeneratedClass)
					{
						if (UObject* DefaultObject = MyControlRigClass->GetDefaultObject(false))
						{
							DefaultObject->SetFlags(RF_Transactional);

							FString PinPath = InModel->GetPinPath(Pin->GetPair(), true);
							FString DefaultValueString = Pin->DefaultValue;
							if (DefaultValueString.Len() > 0)
							{
								FCachedPropertyPath PropertyPath(PinPath);
								if (PropertyPathHelpers::SetPropertyValueFromString(DefaultObject, PropertyPath, DefaultValueString))
								{
									if (Pin->bIsConstant)
									{
										FBlueprintEditorUtils::MarkBlueprintAsModified(this);
									}
									else
									{
										MarkPackageDirty();
									}
								}

								TArray<UObject*> ArchetypeInstances;
								DefaultObject->GetArchetypeInstances(ArchetypeInstances);

								for (UObject* ArchetypeInstance : ArchetypeInstances)
								{
									PropertyPathHelpers::SetPropertyValueFromString(ArchetypeInstance, PropertyPath, DefaultValueString);
								}
							}
						}
					}
				}
				break;
			}
			case EControlRigModelNotifType::LinkAdded:
			case EControlRigModelNotifType::LinkRemoved:
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(this);
				break;
			}
			default:
			{
				break;
			}
		}
	}

	if (!bSuspendModelNotificationsForOthers)
	{
		if (_ModifiedEvent.IsBound())
		{
			_ModifiedEvent.Broadcast(InModel, InType, InPayload);
		}
	}

#endif
}

bool UControlRigBlueprint::UpdateParametersOnControlRig(UControlRig* InRig)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TArray<UObject*> ArchetypeInstances;

	if (InRig == nullptr)
	{
		if (UClass* MyControlRigClass = GeneratedClass)
		{
			InRig = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false));
			if (InRig == nullptr)
			{
				return false;
			}
			InRig->Modify();
			InRig->GetArchetypeInstances(ArchetypeInstances);
		}
	}

	check(InRig);

	InRig->InputProperties.Reset();
	InRig->OutputProperties.Reset();

	if (Model == nullptr)
	{
		return false;
	}

	for(const FControlRigModelNode& Node : Model->Nodes())
	{
		if (!Node.IsParameter())
		{
			continue;
		}

		FCachedPropertyPath NewCachedProperty(Node.Name.ToString());
		if (!NewCachedProperty.Resolve(InRig))
		{
			continue;
		}

		ensure(NewCachedProperty.IsFullyResolved());

		if (Node.ParameterType == EControlRigModelParameterType::Input)
		{
			InRig->InputProperties.Add(Node.Name, NewCachedProperty);
		}
		else if (Node.ParameterType == EControlRigModelParameterType::Output)
		{
			InRig->OutputProperties.Add(Node.Name, NewCachedProperty);
		}
	}

	InRig->ResolveInputOutputProperties();

	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			UpdateParametersOnControlRig(InstancedControlRig);
		}
	}

	return true;
}

bool UControlRigBlueprint::PerformArrayOperation(const FString& InPropertyPath, TFunctionRef<bool(FScriptArrayHelper&,int32)> InOperation, bool bCallModify, bool bPropagateToInstances)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (UClass* MyControlRigClass = GeneratedClass)
	{
		if(UObject* DefaultObject = MyControlRigClass->GetDefaultObject(false))
		{
			if(bCallModify)
			{
				DefaultObject->SetFlags(RF_Transactional);
				DefaultObject->Modify();
			}

			FCachedPropertyPath CachedPropertyPath(InPropertyPath);
			if(PropertyPathHelpers::PerformArrayOperation(DefaultObject, CachedPropertyPath, InOperation))
			{
				if(bCallModify)
				{
					FBlueprintEditorUtils::MarkBlueprintAsModified(this);

					if(bPropagateToInstances)
					{
						TArray<UObject*> ArchetypeInstances;
						DefaultObject->GetArchetypeInstances(ArchetypeInstances);
							
						for (UObject* ArchetypeInstance : ArchetypeInstances)
						{
							PropertyPathHelpers::PerformArrayOperation(ArchetypeInstance, CachedPropertyPath, InOperation);
						}
					}
				}
				return true;
			}
		}
	}

	return false;
}

void UControlRigBlueprint::CleanupBoneHierarchyDeprecated()
{
	if (Hierarchy_DEPRECATED.Num() > 0)
	{
		HierarchyContainer.BoneHierarchy = Hierarchy_DEPRECATED;
		Hierarchy_DEPRECATED.Reset();
	}

	if (CurveContainer_DEPRECATED.Num() > 0)
	{
		HierarchyContainer.CurveContainer = CurveContainer_DEPRECATED;
		CurveContainer_DEPRECATED.Reset();
	}

}

void UControlRigBlueprint::PropagatePoseFromInstanceToBP()
{
	if (UClass* MyControlRigClass = GeneratedClass)
	{
		if (UControlRig* DebuggedRig = Cast<UControlRig>(GetObjectBeingDebugged()))
		{
			/* todo */
		}
	}
}

void UControlRigBlueprint::PropagateHierarchyFromBPToInstances(bool bInitialize)
{
	if (UClass* MyControlRigClass = GeneratedClass)
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			if (bInitialize)
			{
				HierarchyContainer.Initialize();
				HierarchyContainer.ResetTransforms();
			}

			DefaultObject->Hierarchy = HierarchyContainer;
			DefaultObject->Initialize(true);

			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);
			for (UObject* ArchetypeInstance : ArchetypeInstances)
			{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
				{
					InstanceRig->Hierarchy = HierarchyContainer;
					InstanceRig->Initialize(true);
				}
			}
		}
	}
}

#if WITH_EDITOR

void UControlRigBlueprint::HandleOnElementAdded(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey)
{
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey)
{
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName)
{
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementReparented(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName)
{
	PropagateHierarchyFromBPToInstances();
}

void UControlRigBlueprint::HandleOnElementSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected)
{
}

#endif

#undef LOCTEXT_NAMESPACE

