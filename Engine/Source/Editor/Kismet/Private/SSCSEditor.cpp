﻿// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.


#include "BlueprintEditorPrivatePCH.h"
#include "Editor/PropertyEditor/Public/IDetailsView.h"
#include "Editor/UnrealEd/Public/Kismet2/BlueprintEditorUtils.h"
#include "Editor/UnrealEd/Public/Kismet2/ComponentEditorUtils.h"
#include "BlueprintUtilities.h"
#include "ComponentAssetBroker.h"
#include "Editor/ClassViewer/Public/ClassViewerFilter.h"

#include "SSCSEditor.h"
#include "SKismetInspector.h"
#include "SSCSEditorViewport.h"
#include "SComponentClassCombo.h"
#include "PropertyPath.h"

#include "AssetSelection.h"
#include "Editor/SceneOutliner/Private/SSocketChooser.h"
#include "ScopedTransaction.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "ClassIconFinder.h"

#include "ObjectTools.h"

#include "IDocumentation.h"
#include "Kismet2NameValidators.h"
#include "TutorialMetaData.h"
#include "SInlineEditableTextBlock.h"
#include "GenericCommands.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Selection.h"

#include "Engine/InheritableComponentHandler.h"

#include "CreateBlueprintFromActorDialog.h"

#include "BPVariableDragDropAction.h"

#include "SNotificationList.h"
#include "NotificationManager.h"

#include "GameProjectGenerationModule.h"
#include "FeaturedClasses.inl"

#include "HotReloadInterface.h"
#include "AssetRegistryModule.h"
#include "SCreateAssetFromObject.h"

#include "SourceCodeNavigation.h"

#define LOCTEXT_NAMESPACE "SSCSEditor"

DEFINE_LOG_CATEGORY_STATIC(LogSCSEditor, Log, All);

static const FName SCS_ColumnName_ComponentClass( "ComponentClass" );
static const FName SCS_ColumnName_Asset( "Asset" );
static const FName SCS_ColumnName_Mobility( "Mobility" );

//////////////////////////////////////////////////////////////////////////
// SSCSEditorDragDropTree
void SSCSEditorDragDropTree::Construct( const FArguments& InArgs )
{
	SCSEditor = InArgs._SCSEditor;

	STreeView<FSCSEditorTreeNodePtrType>::FArguments BaseArgs;
	BaseArgs.OnGenerateRow( InArgs._OnGenerateRow )
			.OnItemScrolledIntoView( InArgs._OnItemScrolledIntoView )
			.OnGetChildren( InArgs._OnGetChildren )
			.TreeItemsSource( InArgs._TreeItemsSource )
			.ItemHeight( InArgs._ItemHeight )
			.OnContextMenuOpening( InArgs._OnContextMenuOpening )
			.OnMouseButtonDoubleClick( InArgs._OnMouseButtonDoubleClick )
			.OnSelectionChanged( InArgs._OnSelectionChanged )
			.OnExpansionChanged( InArgs._OnExpansionChanged )
			.SelectionMode( InArgs._SelectionMode )
			.HeaderRow( InArgs._HeaderRow )
			.ClearSelectionOnClick( InArgs._ClearSelectionOnClick )
			.ExternalScrollbar( InArgs._ExternalScrollbar );

	STreeView<FSCSEditorTreeNodePtrType>::Construct( BaseArgs );
}

FReply SSCSEditorDragDropTree::OnDragOver( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	FReply Handled = FReply::Unhandled();

	if (SCSEditor != nullptr)
	{
		TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
		if (Operation.IsValid() && (Operation->IsOfType<FExternalDragOperation>() || Operation->IsOfType<FAssetDragDropOp>()))
		{
			Handled = AssetUtil::CanHandleAssetDrag(DragDropEvent);

			if (!Handled.IsEventHandled())
			{
				if (Operation->IsOfType<FAssetDragDropOp>())
				{
					const auto& AssetDragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(Operation);

					for (const FAssetData& AssetData : AssetDragDropOp->AssetData)
					{
						if (UClass* AssetClass = AssetData.GetClass())
						{
							if (AssetClass->IsChildOf(UClass::StaticClass()))
							{
								Handled = FReply::Handled();
								break;
							}
						}
					}
				}
			}
		}
	}

	return Handled;
}

FReply SSCSEditor::TryHandleAssetDragDropOperation(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (Operation.IsValid() && (Operation->IsOfType<FExternalDragOperation>() || Operation->IsOfType<FAssetDragDropOp>()))
	{
		TArray< FAssetData > DroppedAssetData = AssetUtil::ExtractAssetDataFromDrag(Operation);
		const int32 NumAssets = DroppedAssetData.Num();

		if (NumAssets > 0)
		{
			GWarn->BeginSlowTask(LOCTEXT("LoadingAssets", "Loading Asset(s)"), true);

			for (int32 DroppedAssetIdx = 0; DroppedAssetIdx < NumAssets; ++DroppedAssetIdx)
			{
				const FAssetData& AssetData = DroppedAssetData[DroppedAssetIdx];

				if (!AssetData.IsAssetLoaded())
				{
					GWarn->StatusUpdate(DroppedAssetIdx, NumAssets, FText::Format(LOCTEXT("LoadingAsset", "Loading Asset {0}"), FText::FromName(AssetData.AssetName)));
				}

				UClass* AssetClass = AssetData.GetClass();
				UObject* Asset = AssetData.GetAsset();

				UBlueprint* BPClass = Cast<UBlueprint>(Asset);
				UClass* PotentialComponentClass = nullptr;
				UClass* PotentialActorClass = nullptr;

				if ((BPClass != nullptr) && (BPClass->GeneratedClass != nullptr))
				{
					if (BPClass->GeneratedClass->IsChildOf(UActorComponent::StaticClass()))
					{
						PotentialComponentClass = BPClass->GeneratedClass;
					}
					else if (BPClass->GeneratedClass->IsChildOf(AActor::StaticClass()))
					{
						PotentialActorClass = BPClass->GeneratedClass;
					}
				}
				else if (AssetClass->IsChildOf(UClass::StaticClass()))
				{
					UClass* AssetAsClass = CastChecked<UClass>(Asset);
					if (AssetAsClass->IsChildOf(UActorComponent::StaticClass()))
					{
						PotentialComponentClass = AssetAsClass;
					}
					else if (AssetAsClass->IsChildOf(AActor::StaticClass()))
					{
						PotentialActorClass = AssetAsClass;
					}
				}

				TSubclassOf<UActorComponent> MatchingComponentClassForAsset = FComponentAssetBrokerage::GetPrimaryComponentForAsset(AssetClass);
				if (MatchingComponentClassForAsset != nullptr)
				{
					AddNewComponent(MatchingComponentClassForAsset, Asset);
				}
				else if ((PotentialComponentClass != nullptr) && !PotentialComponentClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_Abstract | CLASS_NewerVersionExists))
				{
					if (PotentialComponentClass->HasMetaData(FBlueprintMetadata::MD_BlueprintSpawnableComponent))
					{
						AddNewComponent(PotentialComponentClass, nullptr);
					}
				}
				else if ((PotentialActorClass != nullptr) && !PotentialActorClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_Abstract | CLASS_NewerVersionExists))
				{
					if (UChildActorComponent* ChildActorComponent = Cast<UChildActorComponent>(AddNewComponent(UChildActorComponent::StaticClass(), nullptr)))
					{
						ChildActorComponent->ChildActorClass = PotentialActorClass;
					}
				}
			}

			GWarn->EndSlowTask();
		}

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SSCSEditorDragDropTree::OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) 
{
	if (SCSEditor != nullptr)
	{
		return SCSEditor->TryHandleAssetDragDropOperation(DragDropEvent);
	}
	else
	{
		return FReply::Unhandled();
	}
}



//////////////////////////////////////////////////////////////////////////
// FSCSRowDragDropOp - The drag-drop operation triggered when dragging a row in the components tree

class FSCSRowDragDropOp : public FKismetVariableDragDropAction
{
public:
	DRAG_DROP_OPERATOR_TYPE(FSCSRowDragDropOp, FKismetVariableDragDropAction)

	/** Available drop actions */
	enum EDropActionType
	{
		DropAction_None,
		DropAction_AttachTo,
		DropAction_DetachFrom,
		DropAction_MakeNewRoot,
		DropAction_AttachToOrMakeNewRoot
	};

	/** Node(s) that we started the drag from */
	TArray<FSCSEditorTreeNodePtrType> SourceNodes;

	/** The type of drop action that's pending while dragging */
	EDropActionType PendingDropAction;

	static TSharedRef<FSCSRowDragDropOp> New(FName InVariableName, UStruct* InVariableSource, FNodeCreationAnalytic AnalyticCallback);
};

TSharedRef<FSCSRowDragDropOp> FSCSRowDragDropOp::New(FName InVariableName, UStruct* InVariableSource, FNodeCreationAnalytic AnalyticCallback)
{
	TSharedPtr<FSCSRowDragDropOp> Operation = MakeShareable(new FSCSRowDragDropOp);
	Operation->VariableName = InVariableName;
	Operation->VariableSource = InVariableSource;
	Operation->AnalyticCallback = AnalyticCallback;
	Operation->Construct();
	return Operation.ToSharedRef();
}

//////////////////////////////////////////////////////////////////////////
// FSCSEditorTreeNode

FSCSEditorTreeNode::FSCSEditorTreeNode(FSCSEditorTreeNode::ENodeType InNodeType)
	: NodeType(InNodeType)
	, bNonTransactionalRename(false)
{
}

FName FSCSEditorTreeNode::GetNodeID() const
{
	FName ItemName = GetVariableName();
	if (ItemName == NAME_None)
	{
		UActorComponent* ComponentTemplateOrInstance = GetComponentTemplate();
		if (ComponentTemplateOrInstance != nullptr)
		{
			ItemName = ComponentTemplateOrInstance->GetFName();
		}
	}
	return ItemName;
}

FName FSCSEditorTreeNode::GetVariableName() const
{
	return NAME_None;
}

FString FSCSEditorTreeNode::GetDisplayString() const
{
	return TEXT("GetDisplayString not overridden");
}

FText FSCSEditorTreeNode::GetDisplayName() const
{
	return LOCTEXT("GetDisplayNameNotOverridden", "GetDisplayName not overridden");
}

class USCS_Node* FSCSEditorTreeNode::GetSCSNode() const
{
	return nullptr;
}

UActorComponent* FSCSEditorTreeNode::GetComponentTemplate() const
{
	return nullptr;
}

UActorComponent* FSCSEditorTreeNode::GetEditableComponentTemplate(UBlueprint* ActualEditedBlueprint)
{
	return nullptr;
}

UBlueprint* FSCSEditorTreeNode::GetBlueprint() const
{
	USCS_Node* SCS_Node = GetSCSNode();
	UActorComponent* ComponentTemplate = GetComponentTemplate();

	if(SCS_Node)
	{
		USimpleConstructionScript* SCS = SCS_Node->GetSCS();
		if(SCS)
		{
			return SCS->GetBlueprint();
		}
	}
	else if(ComponentTemplate)
	{
		AActor* CDO = ComponentTemplate->GetOwner();
		if(CDO)
		{
			check(CDO->GetClass());

			return Cast<UBlueprint>(CDO->GetClass()->ClassGeneratedBy);
		}
	}

	return NULL;
}

FSCSEditorTreeNode::ENodeType FSCSEditorTreeNode::GetNodeType() const
{
	return NodeType;
}

bool FSCSEditorTreeNode::IsAttachedTo(FSCSEditorTreeNodePtrType InNodePtr) const
{ 
	FSCSEditorTreeNodePtrType TestParentPtr = ParentNodePtr;
	while(TestParentPtr.IsValid())
	{
		if(TestParentPtr == InNodePtr)
		{
			return true;
		}

		TestParentPtr = TestParentPtr->ParentNodePtr;
	}

	return false; 
}


FSCSEditorTreeNodePtrType FSCSEditorTreeNode::FindClosestParent(TArray<FSCSEditorTreeNodePtrType> InNodes)
{
	uint32 MinDepth = MAX_uint32;
	FSCSEditorTreeNodePtrType ClosestParentNodePtr;

	for(int32 i = 0; i < InNodes.Num() && MinDepth > 1; ++i)
	{
		if(InNodes[i].IsValid())
		{
			uint32 CurDepth = 0;
			if(InNodes[i]->FindChild(GetComponentTemplate(), true, &CurDepth).IsValid())
			{
				if(CurDepth < MinDepth)
				{
					MinDepth = CurDepth;
					ClosestParentNodePtr = InNodes[i];
				}
			}
		}
	}

	return ClosestParentNodePtr;
}

void FSCSEditorTreeNode::AddChild(FSCSEditorTreeNodePtrType InChildNodePtr)
{
	USCS_Node* SCS_Node = GetSCSNode();
	UActorComponent* ComponentTemplate = GetComponentTemplate();

	// Ensure the node is not already parented elsewhere
	if(InChildNodePtr->GetParent().IsValid())
	{
		InChildNodePtr->GetParent()->RemoveChild(InChildNodePtr);
	}

	// Add the given node as a child and link its parent
	Children.AddUnique(InChildNodePtr);
	InChildNodePtr->ParentNodePtr = AsShared();

	// Add a child node to the SCS tree node if not already present
	USCS_Node* SCS_ChildNode = InChildNodePtr->GetSCSNode();
	if(SCS_ChildNode != NULL)
	{
		// Get the SCS instance that owns the child node
		USimpleConstructionScript* SCS = SCS_ChildNode->GetSCS();
		if(SCS != NULL)
		{
			// If the parent is also a valid SCS node
			if(SCS_Node != NULL)
			{
				// If the parent and child are both owned by the same SCS instance
				if(SCS_Node->GetSCS() == SCS)
				{
					// Add the child into the parent's list of children
					if(!SCS_Node->ChildNodes.Contains(SCS_ChildNode))
					{
						SCS_Node->AddChildNode(SCS_ChildNode);
					}
				}
				else
				{
					// Adds the child to the SCS root set if not already present
					SCS->AddNode(SCS_ChildNode);

					// Set parameters to parent this node to the "inherited" SCS node
					SCS_ChildNode->SetParent(SCS_Node);
				}
			}
			else if(ComponentTemplate != NULL)
			{
				// Adds the child to the SCS root set if not already present
				SCS->AddNode(SCS_ChildNode);

				// Set parameters to parent this node to the native component template
				SCS_ChildNode->SetParent(Cast<USceneComponent>(ComponentTemplate));
			}
			else
			{
				// Adds the child to the SCS root set if not already present
				SCS->AddNode(SCS_ChildNode);
			}
		}
	}
	else if (IsInstanced())
	{
		USceneComponent* ChildInstance = Cast<USceneComponent>(InChildNodePtr->GetComponentTemplate());
		if (ensure(ChildInstance != nullptr))
		{
			USceneComponent* ParentInstance = Cast<USceneComponent>(GetComponentTemplate());
			if (ensure(ParentInstance != nullptr))
			{
				// Handle attachment at the instance level
				if (ChildInstance->AttachParent != ParentInstance)
				{
					AActor* Owner = ParentInstance->GetOwner();
					if (Owner->GetRootComponent() == ChildInstance)
					{
						Owner->SetRootComponent(ParentInstance);
					}
					ChildInstance->AttachTo(ParentInstance, NAME_None, EAttachLocation::KeepWorldPosition);
				}
			}
		}
	}
}

FSCSEditorTreeNodePtrType FSCSEditorTreeNode::AddChild(USCS_Node* InSCSNode, bool bInIsInherited)
{
	// Ensure that the given SCS node is valid
	check(InSCSNode != NULL);

	// If it doesn't already exist as a child node
	FSCSEditorTreeNodePtrType ChildNodePtr = FindChild(InSCSNode);
	if(!ChildNodePtr.IsValid())
	{
		// Add a child node to the SCS editor tree
		ChildNodePtr = MakeShareable(new FSCSEditorTreeNodeComponent(InSCSNode, bInIsInherited));
		AddChild(ChildNodePtr);
	}

	return ChildNodePtr;
}

FSCSEditorTreeNodePtrType FSCSEditorTreeNode::AddChildFromComponent(UActorComponent* InComponentTemplate)
{
	// Ensure that the given component template is valid
	check(InComponentTemplate != NULL);

	// If it doesn't already exist in the SCS editor tree
	FSCSEditorTreeNodePtrType ChildNodePtr = FindChild(InComponentTemplate);
	if(!ChildNodePtr.IsValid())
	{
		// Add a child node to the SCS editor tree
		ChildNodePtr = FactoryNodeFromComponent(InComponentTemplate);
		AddChild(ChildNodePtr);
	}

	return ChildNodePtr;
}

// Tries to find a SCS node that was likely responsible for creating the specified instance component.  Note: This is not always possible to do!
USCS_Node* FSCSEditorTreeNode::FindSCSNodeForInstance(UActorComponent* InstanceComponent, UClass* ClassToSearch)
{ 
	if ((ClassToSearch != nullptr) && InstanceComponent->IsCreatedByConstructionScript())
	{
		for (UClass* TestClass = ClassToSearch; TestClass->ClassGeneratedBy != nullptr; TestClass = TestClass->GetSuperClass())
		{
			if (UBlueprint* TestBP = Cast<UBlueprint>(TestClass->ClassGeneratedBy))
			{
				if (TestBP->SimpleConstructionScript != nullptr)
				{
					if (USCS_Node* Result = TestBP->SimpleConstructionScript->FindSCSNode(InstanceComponent->GetFName()))
					{
						return Result;
					}
				}
			}
		}
	}

	return nullptr;
}

FSCSEditorTreeNodePtrType FSCSEditorTreeNode::FactoryNodeFromComponent(UActorComponent* InComponent)
{
	check(InComponent);

	bool bComponentIsInAnInstance = false;

	AActor* Owner = InComponent->GetOwner();
	if ((Owner != nullptr) && !Owner->HasAllFlags(RF_ClassDefaultObject))
	{
		bComponentIsInAnInstance = true;
	}

	if (bComponentIsInAnInstance)
	{
		if (InComponent->CreationMethod == EComponentCreationMethod::Instance)
		{
			return MakeShareable(new FSCSEditorTreeNodeInstanceAddedComponent(Owner, InComponent->GetFName()));
		}
		else
		{
			return MakeShareable(new FSCSEditorTreeNodeInstancedInheritedComponent(Owner, InComponent->GetFName()));
		}
	}

	// Not an instanced component, either an SCS node or a native component in BP edit mode
	return MakeShareable(new FSCSEditorTreeNodeComponent(InComponent));
}

FSCSEditorTreeNodePtrType FSCSEditorTreeNode::FindChild(const USCS_Node* InSCSNode, bool bRecursiveSearch, uint32* OutDepth) const
{
	FSCSEditorTreeNodePtrType Result;

	// Ensure that the given SCS node is valid
	if(InSCSNode != NULL)
	{
		// Look for a match in our set of child nodes
		for(int32 ChildIndex = 0; ChildIndex < Children.Num() && !Result.IsValid(); ++ChildIndex)
		{
			if(InSCSNode == Children[ChildIndex]->GetSCSNode())
			{
				Result = Children[ChildIndex];
			}
			else if(bRecursiveSearch)
			{
				Result = Children[ChildIndex]->FindChild(InSCSNode, true, OutDepth);
			}
		}
	}

	if(OutDepth && Result.IsValid())
	{
		*OutDepth += 1;
	}

	return Result;
}

FSCSEditorTreeNodePtrType FSCSEditorTreeNode::FindChild(const UActorComponent* InComponentTemplate, bool bRecursiveSearch, uint32* OutDepth) const
{
	FSCSEditorTreeNodePtrType Result;

	// Ensure that the given component template is valid
	if(InComponentTemplate != NULL)
	{
		// Look for a match in our set of child nodes
		for(int32 ChildIndex = 0; ChildIndex < Children.Num() && !Result.IsValid(); ++ChildIndex)
		{
			if(InComponentTemplate == Children[ChildIndex]->GetComponentTemplate())
			{
				Result = Children[ChildIndex];
			}
			else if(bRecursiveSearch)
			{
				Result = Children[ChildIndex]->FindChild(InComponentTemplate, true, OutDepth);
			}
		}
	}

	if(OutDepth && Result.IsValid())
	{
		*OutDepth += 1;
	}

	return Result;
}

FSCSEditorTreeNodePtrType FSCSEditorTreeNode::FindChild(const FName& InVariableOrInstanceName, bool bRecursiveSearch, uint32* OutDepth) const
{
	FSCSEditorTreeNodePtrType Result;

	// Ensure that the given name is valid
	if(InVariableOrInstanceName != NAME_None)
	{
		// Look for a match in our set of child nodes
		for(int32 ChildIndex = 0; ChildIndex < Children.Num() && !Result.IsValid(); ++ChildIndex)
		{
			FName ItemName = Children[ChildIndex]->GetVariableName();
			if(ItemName == NAME_None)
			{
				UActorComponent* ComponentTemplateOrInstance = Children[ChildIndex]->GetComponentTemplate();
				check(ComponentTemplateOrInstance != nullptr);
				ItemName = ComponentTemplateOrInstance->GetFName();
			}

			if(InVariableOrInstanceName == ItemName)
			{
				Result = Children[ChildIndex];
			}
			else if(bRecursiveSearch)
			{
				Result = Children[ChildIndex]->FindChild(InVariableOrInstanceName, true, OutDepth);
			}
		}
	}

	if(OutDepth && Result.IsValid())
	{
		*OutDepth += 1;
	}

	return Result;
}

void FSCSEditorTreeNode::RemoveChild(FSCSEditorTreeNodePtrType InChildNodePtr)
{
	// Remove the given node as a child and reset its parent link
	Children.Remove(InChildNodePtr);
	InChildNodePtr->ParentNodePtr.Reset();
	InChildNodePtr->RemoveMeAsChild();
}

void FSCSEditorTreeNode::OnRequestRename(bool bTransactional)
{
	bNonTransactionalRename = !bTransactional;
	RenameRequestedDelegate.ExecuteIfBound();
}

void FSCSEditorTreeNode::OnCompleteRename(const FText& InNewName)
{
}

//////////////////////////////////////////////////////////////////////////
// FSCSEditorTreeNodeComponentBase

FName FSCSEditorTreeNodeComponentBase::GetVariableName() const
{
	FName VariableName = NAME_None;

	USCS_Node* SCS_Node = GetSCSNode();
	UActorComponent* ComponentTemplate = GetComponentTemplate();

	if (IsInstanced() && (SCS_Node == nullptr) && (ComponentTemplate != nullptr))
	{
		if (ComponentTemplate->GetOwner())
		{
			SCS_Node = FindSCSNodeForInstance(ComponentTemplate, ComponentTemplate->GetOwner()->GetClass());
		}
	}

	if (SCS_Node != NULL)
	{
		// Use the same variable name as is obtained by the compiler
		VariableName = SCS_Node->GetVariableName();
	}
	else if (ComponentTemplate != NULL)
	{
		// Try to find the component anchor variable name (first looks for an exact match then scans for any matching variable that points to the archetype in the CDO)
		VariableName = FComponentEditorUtils::FindVariableNameGivenComponentInstance(ComponentTemplate);
	}

	return VariableName;
}

FString FSCSEditorTreeNodeComponentBase::GetDisplayString() const
{
	FName VariableName = GetVariableName();
	UActorComponent* ComponentTemplate = GetComponentTemplate();

	// Only display SCS node variable names in the tree if they have not been autogenerated
	if (VariableName != NAME_None)
	{
		return VariableName.ToString();
	}
	else if (IsNative() && (ComponentTemplate != NULL))
	{
		return ComponentTemplate->GetFName().ToString();
	}
	else
	{
		FString UnnamedString = LOCTEXT("UnnamedToolTip", "Unnamed").ToString();
		FString NativeString = IsNative() ? LOCTEXT("NativeToolTip", "Native ").ToString() : TEXT("");

		if (ComponentTemplate != NULL)
		{
			return FString::Printf(TEXT("[%s %s%s]"), *UnnamedString, *NativeString, *ComponentTemplate->GetClass()->GetName());
		}
		else
		{
			return FString::Printf(TEXT("[%s %s]"), *UnnamedString, *NativeString);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// FSCSEditorTreeNodeInstancedInheritedComponent

FSCSEditorTreeNodeInstancedInheritedComponent::FSCSEditorTreeNodeInstancedInheritedComponent(AActor* Owner, FName InComponentName)
{
	InstancedComponentName = InComponentName;
	check(InstancedComponentName != NAME_None);	// ...otherwise IsRootActor() can return a false positive.

	InstancedComponentOwnerPtr = Owner;
}

bool FSCSEditorTreeNodeInstancedInheritedComponent::IsNative() const
{
	if (UActorComponent* Template = GetComponentTemplate())
	{
		return Template->CreationMethod == EComponentCreationMethod::Native;
	}
	else
	{
		return false;
	}
}

bool FSCSEditorTreeNodeInstancedInheritedComponent::IsRootComponent() const
{
	UActorComponent* ComponentTemplate = GetComponentTemplate();

	if (AActor* OwnerActor = InstancedComponentOwnerPtr.Get())
	{
		if (OwnerActor->GetRootComponent() == ComponentTemplate)
		{
			return true;
		}
	}

	return false;
}

bool FSCSEditorTreeNodeInstancedInheritedComponent::IsInheritedSCS() const
{
	return false;
}

bool FSCSEditorTreeNodeInstancedInheritedComponent::IsDefaultSceneRoot() const
{
	return false;
}

FText FSCSEditorTreeNodeInstancedInheritedComponent::GetDisplayName() const
{
	FName VariableName = GetVariableName();
	if (VariableName != NAME_None)
	{
		return FText::FromName(VariableName);
	}

	return FText::GetEmpty();
}

UActorComponent* FSCSEditorTreeNodeInstancedInheritedComponent::GetComponentTemplate() const
{
	if (InstancedComponentOwnerPtr.IsValid())
	{
		TInlineComponentArray<UActorComponent*> Components;
		InstancedComponentOwnerPtr.Get()->GetComponents(Components);

		for (auto It = Components.CreateConstIterator(); It; ++It)
		{
			UActorComponent* ComponentInstance = *It;
			if (ComponentInstance->GetFName() == InstancedComponentName)
			{
				return ComponentInstance;
			}
		}
	}

	return nullptr;
}

UActorComponent* FSCSEditorTreeNodeInstancedInheritedComponent::GetEditableComponentTemplate(UBlueprint* ActualEditedBlueprint)
{
	if (CanEditDefaults())
	{
		return GetComponentTemplate();
	}

	return nullptr;
}

//////////////////////////////////////////////////////////////////////////
// FSCSEditorTreeNodeInstanceAddedComponent

FSCSEditorTreeNodeInstanceAddedComponent::FSCSEditorTreeNodeInstanceAddedComponent(AActor* Owner, FName InComponentName)
{
	InstancedComponentName = InComponentName;
	check(InstancedComponentName != NAME_None);	// ...otherwise IsRootActor() can return a false positive.

	InstancedComponentOwnerPtr = Owner;
}

bool FSCSEditorTreeNodeInstanceAddedComponent::IsRootComponent() const
{
	bool bIsRoot = true;
	UActorComponent* ComponentTemplate = GetComponentTemplate();

	if (ComponentTemplate != NULL)
	{
		AActor* CDO = ComponentTemplate->GetOwner();
		if (CDO != NULL)
		{
			// Evaluate to TRUE if we have a valid component reference that matches the native root component
			bIsRoot = (ComponentTemplate == CDO->GetRootComponent());
		}
	}

	return bIsRoot;
}

bool FSCSEditorTreeNodeInstanceAddedComponent::IsDefaultSceneRoot() const
{
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(GetComponentTemplate()))
	{
		return SceneComponent->GetFName() == USceneComponent::GetDefaultSceneRootVariableName();
	}

	return false;
}

FString FSCSEditorTreeNodeInstanceAddedComponent::GetDisplayString() const
{
	return InstancedComponentName.ToString();
}

FText FSCSEditorTreeNodeInstanceAddedComponent::GetDisplayName() const
{
	return FText::FromName(InstancedComponentName);
}

UActorComponent* FSCSEditorTreeNodeInstanceAddedComponent::GetComponentTemplate() const
{
	if (InstancedComponentOwnerPtr.IsValid())
	{
		TInlineComponentArray<UActorComponent*> Components;
		InstancedComponentOwnerPtr.Get()->GetComponents(Components);

		for (auto It = Components.CreateConstIterator(); It; ++It)
		{
			UActorComponent* ComponentInstance = *It;
			if (ComponentInstance->GetFName() == InstancedComponentName)
			{
				return ComponentInstance;
			}
		}
	}

	return nullptr;
}

UActorComponent* FSCSEditorTreeNodeInstanceAddedComponent::GetEditableComponentTemplate(UBlueprint* ActualEditedBlueprint)
{
	return GetComponentTemplate();
}

void FSCSEditorTreeNodeInstanceAddedComponent::RemoveMeAsChild()
{
	USceneComponent* ChildInstance = Cast<USceneComponent>(GetComponentTemplate());
	check(ChildInstance != nullptr);

	// Handle detachment at the instance level
	ChildInstance->DetachFromParent(true);
}

void FSCSEditorTreeNodeInstanceAddedComponent::OnCompleteRename(const FText& InNewName)
{
	FScopedTransaction* TransactionContext = NULL;
	if (!GetAndClearNonTransactionalRenameFlag())
	{
		TransactionContext = new FScopedTransaction(LOCTEXT("RenameComponentVariable", "Rename Component Variable"));
	}

	UActorComponent* ComponentInstance = GetComponentTemplate();
	check(ComponentInstance != nullptr);

	ERenameFlags RenameFlags = REN_DontCreateRedirectors;
	if (!TransactionContext)
	{
		RenameFlags |= REN_NonTransactional;
	}

	ComponentInstance->Rename(*InNewName.ToString(), nullptr, RenameFlags);
	InstancedComponentName = *InNewName.ToString();

	if (TransactionContext)
	{
		delete TransactionContext;
	}
}


//////////////////////////////////////////////////////////////////////////
// FSCSEditorTreeNodeComponent

FSCSEditorTreeNodeComponent::FSCSEditorTreeNodeComponent(USCS_Node* InSCSNode, bool bInIsInheritedSCS)
	: bIsInheritedSCS(bInIsInheritedSCS)
	, SCSNodePtr(InSCSNode)
	, ComponentTemplatePtr((InSCSNode != nullptr) ? InSCSNode->ComponentTemplate : nullptr)
{
}

FSCSEditorTreeNodeComponent::FSCSEditorTreeNodeComponent(UActorComponent* InComponentTemplate)
	: bIsInheritedSCS(false)
	, SCSNodePtr(nullptr)
	, ComponentTemplatePtr(InComponentTemplate)
{
	check(InComponentTemplate != nullptr);

	AActor* Owner = InComponentTemplate->GetOwner();
	if (Owner != nullptr)
	{
		ensureMsg(Owner->HasAllFlags(RF_ClassDefaultObject), TEXT("Use a different node class for instanced components"));
	}
}

bool FSCSEditorTreeNodeComponent::IsNative() const
{
	return GetSCSNode() == NULL && GetComponentTemplate() != NULL;
}

bool FSCSEditorTreeNodeComponent::IsRootComponent() const
{
	bool bIsRoot = true;
	USCS_Node* SCS_Node = GetSCSNode();
	UActorComponent* ComponentTemplate = GetComponentTemplate();

	if (SCS_Node != NULL)
	{
		USimpleConstructionScript* SCS = SCS_Node->GetSCS();
		if (SCS != NULL)
		{
			// Evaluate to TRUE if we have an SCS node reference, it is contained in the SCS root set and does not have an external parent
			bIsRoot = SCS->GetRootNodes().Contains(SCS_Node) && SCS_Node->ParentComponentOrVariableName == NAME_None;
		}
	}
	else if (ComponentTemplate != NULL)
	{
		AActor* CDO = ComponentTemplate->GetOwner();
		if (CDO != NULL)
		{
			// Evaluate to TRUE if we have a valid component reference that matches the native root component
			bIsRoot = (ComponentTemplate == CDO->GetRootComponent());
		}
	}

	return bIsRoot;
}

bool FSCSEditorTreeNodeComponent::IsInheritedSCS() const
{
	return bIsInheritedSCS;
}

bool FSCSEditorTreeNodeComponent::IsDefaultSceneRoot() const
{
	if (USCS_Node* SCS_Node = GetSCSNode())
	{
		USimpleConstructionScript* SCS = SCS_Node->GetSCS();
		if (SCS != nullptr)
		{
			return SCS_Node == SCS->GetDefaultSceneRootNode();
		}
	}

	return false;
}

bool FSCSEditorTreeNodeComponent::CanEditDefaults() const
{
	bool bCanEdit = false;

	if (!IsNative())
	{
		USCS_Node* SCS_Node = GetSCSNode();
		bCanEdit = (SCS_Node != NULL);
	}
	else if (UActorComponent* ComponentTemplate = GetComponentTemplate())
	{
		// Evaluate to TRUE for native nodes if it is bound to a member variable and that variable has either EditDefaultsOnly or EditAnywhere flags set
		check(ComponentTemplate->GetOwner());
		UClass* OwnerClass = ComponentTemplate->GetOwner()->GetActorClass();
		if (OwnerClass != NULL)
		{
			UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(OwnerClass);
			if (Blueprint != NULL && Blueprint->ParentClass != NULL)
			{
				for (TFieldIterator<UProperty> It(Blueprint->ParentClass); It; ++It)
				{
					UProperty* Property = *It;
					if (UObjectProperty* ObjectProp = Cast<UObjectProperty>(Property))
					{
						//must be editable
						if ((Property->PropertyFlags & (CPF_Edit)) == 0)
						{
							continue;
						}

						UObject* ParentCDO = Blueprint->ParentClass->GetDefaultObject();

						if (!ComponentTemplate->GetClass()->IsChildOf(ObjectProp->PropertyClass))
						{
							continue;
						}

						UObject* Object = ObjectProp->GetObjectPropertyValue(ObjectProp->ContainerPtrToValuePtr<void>(ParentCDO));
						bCanEdit = Object != NULL && Object->GetFName() == ComponentTemplate->GetFName();

						if (bCanEdit)
						{
							break;
						}
					}
				}
			}
		}
	}

	return bCanEdit;
}

FText FSCSEditorTreeNodeComponent::GetDisplayName() const
{
	FName VariableName = GetVariableName();
	if (VariableName != NAME_None)
	{
		return FText::FromName(VariableName);
	}
	return FText::GetEmpty();
}

class USCS_Node* FSCSEditorTreeNodeComponent::GetSCSNode() const
{
	return SCSNodePtr.Get();
}

UActorComponent* FSCSEditorTreeNodeComponent::GetComponentTemplate() const
{
	return ComponentTemplatePtr.Get();
}

UActorComponent* FSCSEditorTreeNodeComponent::GetEditableComponentTemplate(UBlueprint* ActualEditedBlueprint)
{
	if (CanEditDefaults())
	{
		if (!IsNative() && IsInheritedSCS())
		{
			if (ActualEditedBlueprint != nullptr)
			{
				return INTERNAL_GetOverridenComponentTemplate(ActualEditedBlueprint, true);
			}
			else
			{
				return nullptr;
			}
		}

		return GetComponentTemplate();
	}

	return nullptr;
}

UActorComponent* FSCSEditorTreeNode::FindComponentInstanceInActor(const AActor* InActor) const
{
	USCS_Node* SCS_Node = GetSCSNode();
	UActorComponent* ComponentTemplate = GetComponentTemplate();

	UActorComponent* ComponentInstance = NULL;
	if (InActor != NULL)
	{
		if (SCS_Node != NULL)
		{
			FName VariableName = SCS_Node->GetVariableName();
			if (VariableName != NAME_None)
			{
				UWorld* World = InActor->GetWorld();
				UObjectPropertyBase* Property = FindField<UObjectPropertyBase>(InActor->GetClass(), VariableName);
				if (Property != NULL)
				{
					// Return the component instance that's stored in the property with the given variable name
					ComponentInstance = Cast<UActorComponent>(Property->GetObjectPropertyValue_InContainer(InActor));
				}
				else if (World != nullptr && World->WorldType == EWorldType::Preview)
				{
					// If this is the preview actor, return the cached component instance that's being used for the preview actor prior to recompiling the Blueprint
					ComponentInstance = SCS_Node->EditorComponentInstance;
				}
			}
		}
		else if (ComponentTemplate != NULL)
		{
			// Look for a native component instance with a name that matches the template name
			TInlineComponentArray<UActorComponent*> Components;
			InActor->GetComponents(Components);

			for (auto It = Components.CreateConstIterator(); It; ++It)
			{
				UActorComponent* Component = *It;
				if (Component->GetFName() == ComponentTemplate->GetFName())
				{
					ComponentInstance = Component;
					break;
				}
			}
		}
	}

	return ComponentInstance;
}

void FSCSEditorTreeNodeComponent::OnCompleteRename(const FText& InNewName)
{
	FScopedTransaction* TransactionContext = NULL;
	if (!GetAndClearNonTransactionalRenameFlag())
	{
		TransactionContext = new FScopedTransaction(LOCTEXT("RenameComponentVariable", "Rename Component Variable"));
	}

	FBlueprintEditorUtils::RenameComponentMemberVariable(GetBlueprint(), GetSCSNode(), FName(*InNewName.ToString()));

	if (TransactionContext)
	{
		delete TransactionContext;
	}
}

void FSCSEditorTreeNodeComponent::RemoveMeAsChild()
{
	// Remove the SCS node from the SCS tree, if present
	if (USCS_Node* SCS_ChildNode = GetSCSNode())
	{
		USimpleConstructionScript* SCS = SCS_ChildNode->GetSCS();
		if (SCS != NULL)
		{
			SCS->RemoveNode(SCS_ChildNode);
		}
	}
}

UActorComponent* FSCSEditorTreeNodeComponent::INTERNAL_GetOverridenComponentTemplate(UBlueprint* Blueprint, bool bCreateIfNecessary) const
{
	UActorComponent* OverridenComponent = NULL;

	FComponentKey Key(GetSCSNode());

	const bool BlueprintCanOverrideComponentFormKey = Key.IsValid()
		&& Blueprint
		&& Blueprint->ParentClass
		&& Blueprint->ParentClass->IsChildOf(Key.OwnerClass);

	if (BlueprintCanOverrideComponentFormKey)
	{
		UInheritableComponentHandler* InheritableComponentHandler = Blueprint->GetInheritableComponentHandler(bCreateIfNecessary);
		if (InheritableComponentHandler)
		{
			OverridenComponent = InheritableComponentHandler->GetOverridenComponentTemplate(Key);
			if (!OverridenComponent && bCreateIfNecessary)
			{
				OverridenComponent = InheritableComponentHandler->CreateOverridenComponentTemplate(Key);
			}
		}
	}
	return OverridenComponent;
}

//////////////////////////////////////////////////////////////////////////
// FSCSEditorTreeNodeRootActor

FName FSCSEditorTreeNodeRootActor::GetNodeID() const
{
	if (Actor)
	{
		return Actor->GetFName();
	}
	return NAME_None;
}

void FSCSEditorTreeNodeRootActor::OnCompleteRename(const FText& InNewName)
{
	if (Actor && Actor->IsActorLabelEditable() && !InNewName.ToString().Equals(Actor->GetActorLabel(), ESearchCase::CaseSensitive))
	{
		const FScopedTransaction Transaction(LOCTEXT("SCSEditorRenameActorTransaction", "Rename Actor"));
		Actor->SetActorLabel(InNewName.ToString());
	}
}

//////////////////////////////////////////////////////////////////////////
// SSCS_RowWidget

void SSCS_RowWidget::Construct( const FArguments& InArgs, TSharedPtr<SSCSEditor> InSCSEditor, FSCSEditorTreeNodePtrType InNodePtr, TSharedPtr<STableViewBase> InOwnerTableView  )
{
	check(InNodePtr.IsValid());

	SCSEditor = InSCSEditor;
	TreeNodePtr = InNodePtr;

	bool bIsSeparator = InNodePtr->GetNodeType() == FSCSEditorTreeNode::SeparatorNode;
	
	auto Args = FSuperRowType::FArguments()
		.Style(bIsSeparator ?
				&FEditorStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.NoHoverTableRow") :
				&FEditorStyle::Get().GetWidgetStyle<FTableRowStyle>("SceneOutliner.TableViewRow")) //@todo create editor style for the SCS tree
		.Padding(FMargin(0.f, 0.f, 0.f, 4.f))
		.ShowSelection(!bIsSeparator);

	SMultiColumnTableRow<FSCSEditorTreeNodePtrType>::Construct( Args, InOwnerTableView.ToSharedRef() );
}

SSCS_RowWidget::~SSCS_RowWidget()
{
	// Clear delegate when widget goes away
	//Ask SCSEditor if Node is still active, if it isn't it might have been collected so we can't do anything to it
	TSharedPtr<SSCSEditor> Editor = SCSEditor.Pin();
	if(Editor.IsValid())
	{
		USCS_Node* SCS_Node = GetNode()->GetSCSNode();
		if(SCS_Node != NULL && Editor->IsNodeInSimpleConstructionScript(SCS_Node))
		{
			SCS_Node->SetOnNameChanged(FSCSNodeNameChanged());
		}
	}
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SSCS_RowWidget::GenerateWidgetForColumn( const FName& ColumnName )
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();
	
	if(ColumnName == SCS_ColumnName_ComponentClass)
	{
		// Setup a default icon brush.
		const FSlateBrush* ComponentIcon = FEditorStyle::GetBrush("SCS.NativeComponent");
		if(NodePtr->GetComponentTemplate() != NULL)
		{
			ComponentIcon = FClassIconFinder::FindIconForClass( NodePtr->GetComponentTemplate()->GetClass(), TEXT("SCS.Component") );
		}

		InlineWidget =
			SNew(SInlineEditableTextBlock)
				.Text(this, &SSCS_RowWidget::GetNameLabel)
				.OnVerifyTextChanged( this, &SSCS_RowWidget::OnNameTextVerifyChanged )
				.OnTextCommitted( this, &SSCS_RowWidget::OnNameTextCommit )
				.IsSelected( this, &SSCS_RowWidget::IsSelectedExclusively )
				.IsReadOnly(!NodePtr->CanRename() || (SCSEditor.IsValid() && !SCSEditor.Pin()->IsEditingAllowed()));

		NodePtr->SetRenameRequestedDelegate(FSCSEditorTreeNode::FOnRenameRequested::CreateSP(InlineWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode));
		
		TSharedRef<SToolTip> Tooltip = CreateToolTipWidget();

		return	SNew(SHorizontalBox)
				.ToolTip(Tooltip)
				+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SExpanderArrow, SharedThis(this))
					]
				+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(ComponentIcon)
						.ColorAndOpacity(this, &SSCS_RowWidget::GetColorTintForIcon)
					]
				+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(2, 0, 0, 0)
					[
						InlineWidget.ToSharedRef()
					];
	}
	else if(ColumnName == SCS_ColumnName_Asset)
	{
		return
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 0, 0)
			[
				SNew(STextBlock)
				.Visibility(this, &SSCS_RowWidget::GetAssetVisibility)
				.Text(this, &SSCS_RowWidget::GetAssetName)
				.ToolTipText(this, &SSCS_RowWidget::GetAssetPath)
			];
	}
	else if (ColumnName == SCS_ColumnName_Mobility)
	{
		if (NodePtr->GetNodeType() == FSCSEditorTreeNode::ComponentNode)
		{
			TSharedPtr<SToolTip> MobilityTooltip = SNew(SToolTip)
				.Text(this, &SSCS_RowWidget::GetMobilityToolTipText);

			return SNew(SHorizontalBox)
				.ToolTip(MobilityTooltip)
				.Visibility(EVisibility::Visible) // so we still get tooltip text for an empty SHorizontalBox
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SImage)
					.Image(this, &SSCS_RowWidget::GetMobilityIconImage)
					.ToolTip(MobilityTooltip)
				];
		}
		else
		{
			return SNew(SSpacer);
		}
	}
	else
	{
		return	SNew(STextBlock)
				.Text( LOCTEXT("UnknownColumn", "Unknown Column") );
	}
}

void SSCS_RowWidget::AddToToolTipInfoBox(const TSharedRef<SVerticalBox>& InfoBox, const FText& Key, TSharedRef<SWidget> ValueIcon, const TAttribute<FText>& Value, bool bImportant)
{
	InfoBox->AddSlot()
		.AutoHeight()
		.Padding(0, 1)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), bImportant ? "SCSEditor.ComponentTooltip.ImportantLabel" : "SCSEditor.ComponentTooltip.Label")
				.Text(FText::Format(LOCTEXT("AssetViewTooltipFormat", "{0}:"), Key))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ValueIcon
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), bImportant ? "SCSEditor.ComponentTooltip.ImportantValue" : "SCSEditor.ComponentTooltip.Value")
				.Text(Value)
			]
		];
}

TSharedRef<SToolTip> SSCS_RowWidget::CreateToolTipWidget() const
{
	// Create a box to hold every line of info in the body of the tooltip
	TSharedRef<SVerticalBox> InfoBox = SNew(SVerticalBox);

	// 
	if (FSCSEditorTreeNode* TreeNode = GetNode().Get())
	{
		if (TreeNode->GetNodeType() == FSCSEditorTreeNode::ComponentNode)
		{
			// Add the tooltip
			if (UActorComponent* Template = TreeNode->GetComponentTemplate())
			{
				UClass* TemplateClass = Template->GetClass();
				FText ClassTooltip = TemplateClass->GetToolTipText(/*bShortTooltip=*/ true);

				InfoBox->AddSlot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(FMargin(0, 2, 0, 4))
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "SCSEditor.ComponentTooltip.ClassDescription")
						.Text(ClassTooltip)
						.WrapTextAt(400.0f)
					];
			}

			// Add introduction point
			AddToToolTipInfoBox(InfoBox, LOCTEXT("TooltipAddType", "Source"), SNullWidget::NullWidget, TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SSCS_RowWidget::GetComponentAddSourceToolTipText)), false);
			if (TreeNode->IsInherited())
			{
				AddToToolTipInfoBox(InfoBox, LOCTEXT("TooltipIntroducedIn", "Introduced in"), SNullWidget::NullWidget, TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SSCS_RowWidget::GetIntroducedInToolTipText)), false);
			}

			// Add mobility
			TSharedRef<SImage> MobilityIcon = SNew(SImage).Image(this, &SSCS_RowWidget::GetMobilityIconImage);
			AddToToolTipInfoBox(InfoBox, LOCTEXT("TooltipMobility", "Mobility"), MobilityIcon, TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SSCS_RowWidget::GetMobilityToolTipText)), false);

			// Add asset if applicable to this node
			if (GetAssetVisibility() == EVisibility::Visible)
			{
				InfoBox->AddSlot()[SNew(SSpacer).Size(FVector2D(1.0f, 8.0f))];
				AddToToolTipInfoBox(InfoBox, LOCTEXT("TooltipAsset", "Asset"), SNullWidget::NullWidget, TAttribute<FText>(this, &SSCS_RowWidget::GetAssetName), false);
			}
		}
	}

	TSharedRef<SBorder> TooltipContent = SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		.Padding(0)
		[
			SNew(SVerticalBox)
			
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2)
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "SCSEditor.ComponentTooltip.Title")
						.Text(this, &SSCS_RowWidget::GetTooltipText)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("NoBorder"))
				.Padding(2)
				[
					InfoBox
				]
			]
		];

	return IDocumentation::Get()->CreateToolTip(TAttribute<FText>(this, &SSCS_RowWidget::GetTooltipText), TooltipContent, InfoBox, GetDocumentationLink(), GetDocumentationExcerptName());
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FSlateBrush const* SSCS_RowWidget::GetMobilityIconImage() const
{
	if (FSCSEditorTreeNode* TreeNode = GetNode().Get())
	{
		if (USceneComponent* SceneComponentTemplate = Cast<USceneComponent>(TreeNode->GetComponentTemplate()))
		{
			if (SceneComponentTemplate->Mobility == EComponentMobility::Movable)
			{
				return FEditorStyle::GetBrush(TEXT("ClassIcon.MovableMobilityIcon"));
			}
			else if (SceneComponentTemplate->Mobility == EComponentMobility::Stationary)
			{
				return FEditorStyle::GetBrush(TEXT("ClassIcon.StationaryMobilityIcon"));
			}

			// static components don't get an icon (because static is the most common
			// mobility type, and we'd like to keep the icon clutter to a minimum)
		}
	}

	return nullptr;
}

FText SSCS_RowWidget::GetMobilityToolTipText() const
{
	FText MobilityToolTip = LOCTEXT("ErrorNoMobilityTooltip", "Invalid component");

	if (FSCSEditorTreeNode* TreeNode = TreeNodePtr.Get())
	{
		if (USceneComponent* SceneComponentTemplate = Cast<USceneComponent>(TreeNode->GetComponentTemplate()))
		{
			if (SceneComponentTemplate->Mobility == EComponentMobility::Movable)
			{
				MobilityToolTip = LOCTEXT("MovableMobilityTooltip", "Movable");
			}
			else if (SceneComponentTemplate->Mobility == EComponentMobility::Stationary)
			{
				MobilityToolTip = LOCTEXT("StationaryMobilityTooltip", "Stationary");
			}
			else if (SceneComponentTemplate->Mobility == EComponentMobility::Static)
			{
				MobilityToolTip = LOCTEXT("StaticMobilityTooltip", "Static");
			}
			else
			{
				// make sure we're the mobility type we're expecting (we've handled Movable & Stationary)
				ensureMsgf(false, TEXT("Unhandled mobility type [%d], is this a new type that we don't handle here?"), SceneComponentTemplate->Mobility.GetValue());
				MobilityToolTip = LOCTEXT("UnknownMobilityTooltip", "Component with unknown mobility");
			}
		}
		else
		{
			MobilityToolTip = LOCTEXT("NoMobilityTooltip", "Non-scene component");
		}
	}

	return MobilityToolTip;
}

FText SSCS_RowWidget::GetComponentAddSourceToolTipText() const
{
	FText NodeType;
	
	if (FSCSEditorTreeNode* TreeNode = TreeNodePtr.Get())
	{
		if (TreeNode->IsInherited())
		{
			if (TreeNode->IsNative())
			{
				NodeType = LOCTEXT("InheritedNativeComponent", "Inherited (C++)");
			}
			else
			{
				NodeType = LOCTEXT("InheritedBlueprintComponent", "Inherited (Blueprint)");
			}
		}
		else
		{
			if (TreeNode->IsInstanced())
			{
				NodeType = LOCTEXT("ThisInstanceAddedComponent", "This actor instance");
			}
			else
			{
				NodeType = LOCTEXT("ThisBlueprintAddedComponent", "This Blueprint");
			}
		}
	}

	return NodeType;
}

FText SSCS_RowWidget::GetIntroducedInToolTipText() const
{
	FText IntroducedInTooltip = LOCTEXT("IntroducedInThisBPTooltip", "this class");

	if (FSCSEditorTreeNode* TreeNode = TreeNodePtr.Get())
	{
		if (TreeNode->IsInherited())
		{
			if (UActorComponent* ComponentTemplate = TreeNode->GetComponentTemplate())
			{
				UClass* BestClass = nullptr;
				AActor* OwningActor = ComponentTemplate->GetOwner();

				if (TreeNode->IsNative() && (OwningActor != nullptr))
				{
					for (UClass* TestClass = OwningActor->GetClass(); TestClass != AActor::StaticClass(); TestClass = TestClass->GetSuperClass())
					{
						if (TreeNode->FindComponentInstanceInActor(Cast<AActor>(TestClass->GetDefaultObject())))
						{
							BestClass = TestClass;
						}
						else
						{
							break;
						}
					}
				}
				else if (!TreeNode->IsNative())
				{
					USCS_Node* SCSNode = TreeNode->GetSCSNode();

					if ((SCSNode == nullptr) && (OwningActor != nullptr))
					{
						SCSNode = FSCSEditorTreeNode::FindSCSNodeForInstance(ComponentTemplate, OwningActor->GetClass());
					}

					if (SCSNode != nullptr)
					{
						if (UBlueprint* OwningBP = SCSNode->GetSCS()->GetBlueprint())
						{
							BestClass = OwningBP->GeneratedClass;
						}
					}
				}

				if (BestClass == nullptr)
				{
					ensure(ComponentTemplate->IsCreatedByConstructionScript());
					IntroducedInTooltip = LOCTEXT("IntroducedInUnknownError", "Unknown Blueprint Class (via an Add Component call)");
				}
				else
				{
					IntroducedInTooltip = FBlueprintEditorUtils::GetFriendlyClassDisplayName(BestClass);
				}
			}
			else
			{
				IntroducedInTooltip = LOCTEXT("IntroducedInNoTemplateError", "[no component template found]");
			}
		}
		else if (TreeNode->IsInstanced())
		{
			IntroducedInTooltip = LOCTEXT("IntroducedInThisActorInstanceTooltip", "this actor instance");
		}
	}

	return IntroducedInTooltip;
}

FText SSCS_RowWidget::GetAssetName() const
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();

	FText AssetName = LOCTEXT("None", "None");
	if(NodePtr.IsValid() && NodePtr->GetComponentTemplate())
	{
		UObject* Asset = FComponentAssetBrokerage::GetAssetFromComponent(NodePtr->GetComponentTemplate());
		if(Asset != NULL)
		{
			AssetName = FText::FromString(Asset->GetName());
		}
	}

	return AssetName;
}

FText SSCS_RowWidget::GetAssetPath() const
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();

	FText AssetName = LOCTEXT("None", "None");
	if(NodePtr.IsValid() && NodePtr->GetComponentTemplate())
	{
		UObject* Asset = FComponentAssetBrokerage::GetAssetFromComponent(NodePtr->GetComponentTemplate());
		if(Asset != NULL)
		{
			AssetName = FText::FromString(Asset->GetPathName());
		}
	}

	return AssetName;
}


EVisibility SSCS_RowWidget::GetAssetVisibility() const
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();

	if(NodePtr.IsValid() && NodePtr->GetComponentTemplate() && FComponentAssetBrokerage::SupportsAssets(NodePtr->GetComponentTemplate()))
	{
		return EVisibility::Visible;
	}
	else
	{
		return EVisibility::Hidden;
	}
}

FSlateColor SSCS_RowWidget::GetColorTintForIcon() const
{
	const FLinearColor InheritedBlueprintComponentColor(0.08f, 0.35f, 0.6f);
	const FLinearColor InstancedInheritedBlueprintComponentColor(0.08f, 0.35f, 0.6f);
	const FLinearColor InheritedNativeComponentColor(0.7f, 0.9f, 0.7f);
	const FLinearColor IntroducedHereColor(FLinearColor::White);

	FSCSEditorTreeNodePtrType NodePtr = GetNode();
	if (NodePtr->IsInherited())
	{
		if (NodePtr->IsNative())
		{
			return InheritedNativeComponentColor;
		}
		else if (NodePtr->IsInstanced())
		{
			return InstancedInheritedBlueprintComponentColor;
		}
		else
		{
			return InheritedBlueprintComponentColor;
		}
	}
	else
	{
		return IntroducedHereColor;
	}
}

TSharedPtr<SWidget> SSCS_RowWidget::BuildSceneRootDropActionMenu(FSCSEditorTreeNodePtrType DroppedNodePtr)
{
	check(SCSEditor.IsValid());
	FMenuBuilder MenuBuilder(true, SCSEditor.Pin()->CommandList);

	MenuBuilder.BeginSection("SceneRootNodeDropActions", LOCTEXT("SceneRootNodeDropActionContextMenu", "Drop Actions"));
	{
		const FText DroppedVariableNameText = FText::FromName( DroppedNodePtr->GetVariableName() );
		const FText NodeVariableNameText = FText::FromName( GetNode()->GetVariableName() );

		bool bDroppedInSameBlueprint = true;
		if (SCSEditor.Pin()->GetEditorMode() == EComponentEditorMode::BlueprintSCS)
		{
			bDroppedInSameBlueprint = DroppedNodePtr->GetBlueprint() == GetBlueprint();
		}

		MenuBuilder.AddMenuEntry(
			LOCTEXT("DropActionLabel_AttachToRootNode", "Attach"),
			bDroppedInSameBlueprint 
			? FText::Format( LOCTEXT("DropActionToolTip_AttachToRootNode", "Attach {0} to {1}."), DroppedVariableNameText, NodeVariableNameText )
			: FText::Format( LOCTEXT("DropActionToolTip_AttachToRootNodeFromCopy", "Copy {0} to a new variable and attach it to {1}."), DroppedVariableNameText, NodeVariableNameText ),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSCS_RowWidget::OnAttachToDropAction, DroppedNodePtr),
				FCanExecuteAction()));
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DropActionLabel_MakeNewRootNode", "Make New Root"),
			bDroppedInSameBlueprint
			? FText::Format( LOCTEXT("DropActionToolTip_MakeNewRootNode", "Make {0} the new root."), DroppedVariableNameText )
			: FText::Format( LOCTEXT("DropActionToolTip_MakeNewRootNodeFromCopy", "Copy {0} to a new variable and make it the new root."), DroppedVariableNameText ),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSCS_RowWidget::OnMakeNewRootDropAction, DroppedNodePtr),
				FCanExecuteAction()));
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

FReply SSCS_RowWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && GetNode()->GetNodeType() != FSCSEditorTreeNode::SeparatorNode)
	{
		FReply Reply = SMultiColumnTableRow<FSCSEditorTreeNodePtrType>::OnMouseButtonDown( MyGeometry, MouseEvent );
		return Reply.DetectDrag( SharedThis(this) , EKeys::LeftMouseButton );
	}
	else
	{
		return FReply::Unhandled();
	}
}

FReply SSCS_RowWidget::OnDragDetected( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	auto SCSEditorPtr = SCSEditor.Pin();
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton)
		&& SCSEditorPtr.IsValid()
		&& SCSEditorPtr->IsEditingAllowed()) //can only drag when editing
	{
		TArray<TSharedPtr<FSCSEditorTreeNode>> SelectedNodePtrs = SCSEditorPtr->GetSelectedNodes();
		if (SelectedNodePtrs.Num() == 0)
		{
			SelectedNodePtrs.Add(GetNode());
		}

		TSharedPtr<FSCSEditorTreeNode> FirstNode = SelectedNodePtrs[0];
		if (FirstNode->GetNodeType() == FSCSEditorTreeNode::ComponentNode)
		{
			UBlueprint* Blueprint = FirstNode->GetBlueprint();
			const FName VariableName = FirstNode->GetVariableName();
			UStruct* VariableScope = (Blueprint != nullptr) ? Blueprint->SkeletonGeneratedClass : nullptr;

			TSharedRef<FSCSRowDragDropOp> Operation = FSCSRowDragDropOp::New(VariableName, VariableScope, FNodeCreationAnalytic());
			Operation->SetCtrlDrag(true); // Always put a getter
			Operation->PendingDropAction = FSCSRowDragDropOp::DropAction_None;
			Operation->SourceNodes = SelectedNodePtrs;

			return FReply::Handled().BeginDragDrop(Operation);
		}
	}
	
	return FReply::Unhandled();
}

void SSCS_RowWidget::OnDragEnter( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (!Operation.IsValid())
	{
		return;
	}

	TSharedPtr<FSCSRowDragDropOp> DragRowOp = DragDropEvent.GetOperationAs<FSCSRowDragDropOp>();
	if (DragRowOp.IsValid())
	{
		check(SCSEditor.IsValid());
		
		FText Message;
		FSlateColor IconColor = FLinearColor::White;
		
		for (const auto& SelectedNodePtr : DragRowOp->SourceNodes)
		{
			if (!SelectedNodePtr->CanReparent())
			{
				// We set the tooltip text here because it won't change across entry/leave events
				if (DragRowOp->SourceNodes.Num() == 1)
				{
					if (!SelectedNodePtr->IsSceneComponent())
					{
						Message = LOCTEXT("DropActionToolTip_Error_CannotReparent_NotSceneComponent", "The selected component is not a scene component and cannot be attached to other components.");
					}
					else if (SelectedNodePtr->IsInherited())
					{
						Message = LOCTEXT("DropActionToolTip_Error_CannotReparent_Inherited", "The selected component is inherited and cannot be reordered here.");
					}
					else
					{
						Message = LOCTEXT("DropActionToolTip_Error_CannotReparent", "The selected component cannot be moved.");
					}
				}
				else
				{
					Message = LOCTEXT("DropActionToolTip_Error_CannotReparentMultiple", "One or more of the selected components cannot be attached.");
				}
				break;
			}
		}

		if (Message.IsEmpty())
		{
			FSCSEditorTreeNodePtrType SceneRootNodePtr = SCSEditor.Pin()->SceneRootNodePtr;
			check(SceneRootNodePtr.IsValid());

			FSCSEditorTreeNodePtrType NodePtr = GetNode();
			if ((NodePtr->GetNodeType() == FSCSEditorTreeNode::SeparatorNode) || (NodePtr->GetNodeType() == FSCSEditorTreeNode::RootActorNode))
			{
				// Don't show a feedback message if over a node that makes no sense, such as a separator or the instance node
				Message = LOCTEXT("DropActionToolTip_FriendlyError_DragToAComponent", "Drag to another component in order to attach to that component or become the root component.\nDrag to a Blueprint graph in order to drop a reference.");
			}

			// Validate each selected node being dragged against the node that belongs to this row. Exit the loop if we have a valid tooltip OR a valid pending drop action once all nodes in the selection have been validated.
			for (auto SourceNodeIter = DragRowOp->SourceNodes.CreateConstIterator(); SourceNodeIter && (Message.IsEmpty() || DragRowOp->PendingDropAction != FSCSRowDragDropOp::DropAction_None); ++SourceNodeIter)
			{
				FSCSEditorTreeNodePtrType DraggedNodePtr = *SourceNodeIter;
				check(DraggedNodePtr.IsValid());

				// Reset the pending drop action each time through the loop
				DragRowOp->PendingDropAction = FSCSRowDragDropOp::DropAction_None;

				// Get the component template objects associated with each node
				USceneComponent* HoveredTemplate = Cast<USceneComponent>(NodePtr->GetComponentTemplate());
				USceneComponent* DraggedTemplate = Cast<USceneComponent>(DraggedNodePtr->GetComponentTemplate());

				if (DraggedNodePtr == NodePtr)
				{
					// Attempted to drag and drop onto self
					if (DragRowOp->SourceNodes.Num() > 1)
					{
						Message = FText::Format(LOCTEXT("DropActionToolTip_Error_CannotAttachToSelfWithMultipleSelection", "Cannot attach the selected components here because it would result in {0} being attached to itself. Remove it from the selection and try again."), DraggedNodePtr->GetDisplayName());
					}
					else
					{
						Message = FText::Format(LOCTEXT("DropActionToolTip_Error_CannotAttachToSelf", "Cannot attach {0} to itself."), DraggedNodePtr->GetDisplayName());
					}
				}
				else if (NodePtr->IsAttachedTo(DraggedNodePtr))
				{
					// Attempted to drop a parent onto a child
					if (DragRowOp->SourceNodes.Num() > 1)
					{
						Message = FText::Format(LOCTEXT("DropActionToolTip_Error_CannotAttachToChildWithMultipleSelection", "Cannot attach the selected components here because it would result in {0} being attached to one of its children. Remove it from the selection and try again."), DraggedNodePtr->GetDisplayName());
					}
					else
					{
						Message = FText::Format(LOCTEXT("DropActionToolTip_Error_CannotAttachToChild", "Cannot attach {0} to one of its children."), DraggedNodePtr->GetDisplayName());
					}
				}
				else if (HoveredTemplate == NULL || DraggedTemplate == NULL)
				{
					if (HoveredTemplate == nullptr)
					{
						// Can't attach non-USceneComponent types
						Message = LOCTEXT("DropActionToolTip_Error_NotAttachable_NotSceneComponent", "Cannot attach to this component as it is not a scene component.");
					}
					else
					{
						// Can't attach non-USceneComponent types
						Message = LOCTEXT("DropActionToolTip_Error_NotAttachable", "Cannot attach to this component.");
					}
				}
				else if (NodePtr == SceneRootNodePtr)
				{
					bool bCanMakeNewRoot = false;
					bool bCanAttachToRoot = !NodePtr->IsDefaultSceneRoot()
						&& !DraggedNodePtr->IsDirectlyAttachedTo(NodePtr)
						&& HoveredTemplate->CanAttachAsChild(DraggedTemplate, NAME_None)
						&& DraggedTemplate->Mobility >= HoveredTemplate->Mobility
						&& (!HoveredTemplate->IsEditorOnly() || DraggedTemplate->IsEditorOnly());

					if (!NodePtr->CanReparent() && (!NodePtr->IsDefaultSceneRoot() || NodePtr->IsInherited()))
					{
						// Cannot make the dropped node the new root if we cannot reparent the current root
						Message = LOCTEXT("DropActionToolTip_Error_CannotReparentRootNode", "The root component in this Blueprint is inherited and cannot be replaced.");
					}
					else if (DraggedTemplate->IsEditorOnly() && !HoveredTemplate->IsEditorOnly())
					{
						// can't have a new root that's editor-only (when children would be around in-game)
						Message = LOCTEXT("DropActionToolTip_Error_CannotReparentEditorOnly", "Cannot re-parent game components under editor-only ones.");
					}
					else if (DraggedTemplate->Mobility > HoveredTemplate->Mobility)
					{
						// can't have a new root that's movable if the existing root is static or stationary
						Message = LOCTEXT("DropActionToolTip_Error_CannotReparentNonMovable", "Cannot replace a non-movable scene root with a movable component.");
					}
					else if (DragRowOp->SourceNodes.Num() > 1)
					{
						Message = LOCTEXT("DropActionToolTip_Error_CannotAssignMultipleRootNodes", "Cannot replace the scene root with multiple components. Please select only a single component and try again.");
					}
					else
					{
						bCanMakeNewRoot = true;
					}

					if (bCanMakeNewRoot && bCanAttachToRoot)
					{
						// User can choose to either attach to the current root or make the dropped node the new root
						Message = LOCTEXT("DropActionToolTip_AttachToOrMakeNewRoot", "Drop here to see available actions.");
						DragRowOp->PendingDropAction = FSCSRowDragDropOp::DropAction_AttachToOrMakeNewRoot;
					}
					else if (SCSEditor.Pin()->GetEditorMode() == EComponentEditorMode::BlueprintSCS && DraggedNodePtr->GetBlueprint() != GetBlueprint())
					{
						if (bCanMakeNewRoot)
						{
							// Only available action is to copy the dragged node to the other Blueprint and make it the new root
							Message = FText::Format(LOCTEXT("DropActionToolTip_DropMakeNewRootNodeFromCopy", "Drop here to copy {0} to a new variable and make it the new root."), DraggedNodePtr->GetDisplayName());
							DragRowOp->PendingDropAction = FSCSRowDragDropOp::DropAction_MakeNewRoot;
						}
						else if (bCanAttachToRoot)
						{
							// Only available action is to copy the dragged node(s) to the other Blueprint and attach it to the root
							if (DragRowOp->SourceNodes.Num() > 1)
							{
								Message = FText::Format(LOCTEXT("DropActionToolTip_AttachToThisNodeFromCopyWithMultipleSelection", "Drop here to copy the selected components to new variables and attach them to {0}."), NodePtr->GetDisplayName());
							}
							else
							{
								Message = FText::Format(LOCTEXT("DropActionToolTip_AttachToThisNodeFromCopy", "Drop here to copy {0} to a new variable and attach it to {1}."), DraggedNodePtr->GetDisplayName(), NodePtr->GetDisplayName());
							}

							DragRowOp->PendingDropAction = FSCSRowDragDropOp::DropAction_AttachTo;
						}
					}
					else if (bCanMakeNewRoot)
					{
						// Only available action is to make the dragged node the new root
						Message = FText::Format(LOCTEXT("DropActionToolTip_DropMakeNewRootNode", "Drop here to make {0} the new root."), DraggedNodePtr->GetDisplayName());
						DragRowOp->PendingDropAction = FSCSRowDragDropOp::DropAction_MakeNewRoot;
					}
					else if (bCanAttachToRoot)
					{
						// Only available action is to attach the dragged node(s) to the root
						if (DragRowOp->SourceNodes.Num() > 1)
						{
							Message = FText::Format(LOCTEXT("DropActionToolTip_AttachToThisNodeWithMultipleSelection", "Drop here to attach the selected components to {0}."), NodePtr->GetDisplayName());
						}
						else
						{
							Message = FText::Format(LOCTEXT("DropActionToolTip_AttachToThisNode", "Drop here to attach {0} to {1}."), DraggedNodePtr->GetDisplayName(), NodePtr->GetDisplayName());
						}

						DragRowOp->PendingDropAction = FSCSRowDragDropOp::DropAction_AttachTo;
					}
				}
				else if (DraggedNodePtr->IsDirectlyAttachedTo(NodePtr)) // if dropped onto parent
				{
					// Detach the dropped node(s) from the current node and reattach to the root node
					if (DragRowOp->SourceNodes.Num() > 1)
					{
						Message = FText::Format(LOCTEXT("DropActionToolTip_DetachFromThisNodeWithMultipleSelection", "Drop here to detach the selected components from {0}."), NodePtr->GetDisplayName());
					}
					else
					{
						Message = FText::Format(LOCTEXT("DropActionToolTip_DetachFromThisNode", "Drop here to detach {0} from {1}."), DraggedNodePtr->GetDisplayName(), NodePtr->GetDisplayName());
					}

					DragRowOp->PendingDropAction = FSCSRowDragDropOp::DropAction_DetachFrom;
				}
				else if (!DraggedTemplate->IsEditorOnly() && HoveredTemplate->IsEditorOnly())
				{
					// can't have a game component child nested under an editor-only one
					Message = LOCTEXT("DropActionToolTip_Error_CannotAttachToEditorOnly", "Cannot attach game components to editor-only ones.");
				}
				else if ((DraggedTemplate->Mobility == EComponentMobility::Static) && ((HoveredTemplate->Mobility == EComponentMobility::Movable) || (HoveredTemplate->Mobility == EComponentMobility::Stationary)))
				{
					// Can't attach Static components to mobile ones
					Message = LOCTEXT("DropActionToolTip_Error_CannotAttachStatic", "Cannot attach Static components to movable ones.");
				}
				else if ((DraggedTemplate->Mobility == EComponentMobility::Stationary) && (HoveredTemplate->Mobility == EComponentMobility::Movable))
				{
					// Can't attach Static components to mobile ones
					Message = LOCTEXT("DropActionToolTip_Error_CannotAttachStationary", "Cannot attach Stationary components to movable ones.");
				}
				else if (HoveredTemplate->CanAttachAsChild(DraggedTemplate, NAME_None))
				{
					// Attach the dragged node(s) to this node
					if (DraggedNodePtr->GetBlueprint() != GetBlueprint())
					{
						if (DragRowOp->SourceNodes.Num() > 1)
						{
							Message = FText::Format(LOCTEXT("DropActionToolTip_AttachToThisNodeFromCopyWithMultipleSelection", "Drop here to copy the selected nodes to new variables and attach to {0}."), NodePtr->GetDisplayName());
						}
						else
						{
							Message = FText::Format(LOCTEXT("DropActionToolTip_AttachToThisNodeFromCopy", "Drop here to copy {0} to a new variable and attach it to {1}."), DraggedNodePtr->GetDisplayName(), NodePtr->GetDisplayName());
						}
					}
					else if (DragRowOp->SourceNodes.Num() > 1)
					{
						Message = FText::Format(LOCTEXT("DropActionToolTip_AttachToThisNodeWithMultipleSelection", "Drop here to attach the selected nodes to {0}."), NodePtr->GetDisplayName());
					}
					else
					{
						Message = FText::Format(LOCTEXT("DropActionToolTip_AttachToThisNode", "Drop here to attach {0} to {1}."), DraggedNodePtr->GetDisplayName(), NodePtr->GetDisplayName());
					}

					DragRowOp->PendingDropAction = FSCSRowDragDropOp::DropAction_AttachTo;
				}
				else
				{
					// The dropped node cannot be attached to the current node
					Message = FText::Format(LOCTEXT("DropActionToolTip_Error_TooManyAttachments", "Unable to attach {0} to {1}."), DraggedNodePtr->GetDisplayName(), NodePtr->GetDisplayName());
				}
			}
		}

		const FSlateBrush* StatusSymbol = DragRowOp->PendingDropAction != FSCSRowDragDropOp::DropAction_None
			? FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK"))
			: FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));

		if (Message.IsEmpty())
		{
			DragRowOp->SetFeedbackMessage(nullptr);
		}
		else
		{
			DragRowOp->SetSimpleFeedbackMessage(StatusSymbol, FLinearColor::White, Message);
		}
	}
	else if ( Operation->IsOfType<FExternalDragOperation>() || Operation->IsOfType<FAssetDragDropOp>() )
	{
		// defer to the tree widget's handler for this type of operation
		TSharedPtr<SSCSEditor> PinnedEditor = SCSEditor.Pin();
		if ( PinnedEditor.IsValid() && PinnedEditor->SCSTreeWidget.IsValid() )
		{
			PinnedEditor->SCSTreeWidget->OnDragEnter( MyGeometry, DragDropEvent );
		}
	}
}

void SSCS_RowWidget::OnDragLeave( const FDragDropEvent& DragDropEvent )
{
	TSharedPtr<FSCSRowDragDropOp> DragRowOp = DragDropEvent.GetOperationAs<FSCSRowDragDropOp>();
	if (DragRowOp.IsValid())
	{
		bool bCanReparentAllNodes = true;
		for(auto SourceNodeIter = DragRowOp->SourceNodes.CreateConstIterator(); SourceNodeIter && bCanReparentAllNodes; ++SourceNodeIter)
		{
			FSCSEditorTreeNodePtrType DraggedNodePtr = *SourceNodeIter;
			check(DraggedNodePtr.IsValid());

			bCanReparentAllNodes = DraggedNodePtr->CanReparent();
		}

		// Only clear the tooltip text if all dragged nodes support it
		if(bCanReparentAllNodes)
		{
			TSharedPtr<SWidget> NoWidget;
			DragRowOp->SetFeedbackMessage(NoWidget);
			DragRowOp->PendingDropAction = FSCSRowDragDropOp::DropAction_None;
		}
	}
}

FReply SSCS_RowWidget::OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (!Operation.IsValid())
	{
		return FReply::Handled();
	}
	
	if (Operation->IsOfType<FSCSRowDragDropOp>() && (Cast<USceneComponent>(GetNode()->GetComponentTemplate()) != nullptr))
	{
		TSharedPtr<FSCSRowDragDropOp> DragRowOp = StaticCastSharedPtr<FSCSRowDragDropOp>( Operation );	
		check(DragRowOp.IsValid());

		switch(DragRowOp->PendingDropAction)
		{
		case FSCSRowDragDropOp::DropAction_AttachTo:
			OnAttachToDropAction(DragRowOp->SourceNodes);
			break;
			
		case FSCSRowDragDropOp::DropAction_DetachFrom:
			OnDetachFromDropAction(DragRowOp->SourceNodes);
			break;

		case FSCSRowDragDropOp::DropAction_MakeNewRoot:
			check(DragRowOp->SourceNodes.Num() == 1);
			OnMakeNewRootDropAction(DragRowOp->SourceNodes[0]);
			break;

		case FSCSRowDragDropOp::DropAction_AttachToOrMakeNewRoot:
			check(DragRowOp->SourceNodes.Num() == 1);
			FSlateApplication::Get().PushMenu(
				SharedThis(this),
				BuildSceneRootDropActionMenu(DragRowOp->SourceNodes[0]).ToSharedRef(),
				FSlateApplication::Get().GetCursorPos(),
				FPopupTransitionEffect(FPopupTransitionEffect::TypeInPopup)
			);
			break;

		case FSCSRowDragDropOp::DropAction_None:
		default:
			break;
		}
	}
	else if (Operation->IsOfType<FExternalDragOperation>() || Operation->IsOfType<FAssetDragDropOp>())
	{
		// defer to the tree widget's handler for this type of operation
		TSharedPtr<SSCSEditor> PinnedEditor = SCSEditor.Pin();
		if ( PinnedEditor.IsValid() && PinnedEditor->SCSTreeWidget.IsValid() )
		{
			PinnedEditor->SCSTreeWidget->OnDrop( MyGeometry, DragDropEvent );
		}
	}

	return FReply::Handled();
}

void SSCS_RowWidget::OnAttachToDropAction(const TArray<FSCSEditorTreeNodePtrType>& DroppedNodePtrs)
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();

	check(NodePtr.IsValid());
	check(DroppedNodePtrs.Num() > 0);

	TSharedPtr<SSCSEditor> SCSEditorPtr = SCSEditor.Pin();
	check(SCSEditorPtr.IsValid());

	bool bRegenerateTreeNodes = false;
	const FScopedTransaction TransactionContext(DroppedNodePtrs.Num() > 1 ? LOCTEXT("AttachComponents", "Attach Components") : LOCTEXT("AttachComponent", "Attach Component"));

	if (SCSEditorPtr->GetEditorMode() == EComponentEditorMode::BlueprintSCS)
	{
		// Get the current Blueprint context
		UBlueprint* Blueprint = GetBlueprint();
		check(Blueprint);

		// Get the current "preview" Actor instance
		AActor* PreviewActor = SCSEditorPtr->PreviewActor.Get();
		check(PreviewActor);

		for(const auto& DroppedNodePtr : DroppedNodePtrs)
		{
			// Clone the component if it's being dropped into a different SCS
			if(DroppedNodePtr->GetBlueprint() != Blueprint)
			{
				bRegenerateTreeNodes = true;

				check(DroppedNodePtr.IsValid());
				UActorComponent* ComponentTemplate = DroppedNodePtr->GetComponentTemplate();
				check(ComponentTemplate);

				// Note: This will mark the Blueprint as structurally modified
				UActorComponent* ClonedComponent = SCSEditorPtr->AddNewComponent(ComponentTemplate->GetClass(), NULL);
				check(ClonedComponent);

				//Serialize object properties using write/read operations.
				TArray<uint8> SavedProperties;
				FObjectWriter Writer(ComponentTemplate, SavedProperties);
				FObjectReader(ClonedComponent, SavedProperties);

				// Attach the copied node to the target node (this will also detach it from the root if necessary)
				FSCSEditorTreeNodePtrType NewNodePtr = SCSEditorPtr->GetNodeFromActorComponent(ClonedComponent);
				if(NewNodePtr.IsValid())
				{
					NodePtr->AddChild(NewNodePtr);
				}
			}
			else
			{
				// Get the associated component template if it is a scene component, so we can adjust the transform
				USceneComponent* SceneComponentTemplate = Cast<USceneComponent>(DroppedNodePtr->GetComponentTemplate());

				// Check for a valid parent node
				FSCSEditorTreeNodePtrType ParentNodePtr = DroppedNodePtr->GetParent();
				if(ParentNodePtr.IsValid())
				{
					// Detach the dropped node from its parent
					ParentNodePtr->RemoveChild(DroppedNodePtr);

					// If the associated component template is a scene component, maintain its preview world position
					if(SceneComponentTemplate)
					{
						// Save current state
						SceneComponentTemplate->Modify();

						// Reset the attach socket name
						SceneComponentTemplate->AttachSocketName = NAME_None;
						USCS_Node* SCS_Node = DroppedNodePtr->GetSCSNode();
						if(SCS_Node)
						{
							SCS_Node->Modify();
							SCS_Node->AttachToName = NAME_None;
						}

						// Attempt to locate a matching registered instance of the component template in the Actor context that's being edited
						USceneComponent* InstancedSceneComponent = Cast<USceneComponent>(DroppedNodePtr->FindComponentInstanceInActor(PreviewActor));
						if(InstancedSceneComponent && InstancedSceneComponent->IsRegistered())
						{
							// If we find a match, save off the world position
							FTransform ComponentToWorld = InstancedSceneComponent->GetComponentToWorld();
							SceneComponentTemplate->RelativeLocation = ComponentToWorld.GetTranslation();
							SceneComponentTemplate->RelativeRotation = ComponentToWorld.Rotator();
							SceneComponentTemplate->RelativeScale3D = ComponentToWorld.GetScale3D();
						}
					}
				}

				// Attach the dropped node to the given node
				NodePtr->AddChild(DroppedNodePtr);

				// Attempt to locate a matching instance of the parent component template in the Actor context that's being edited
				USceneComponent* ParentSceneComponent = Cast<USceneComponent>(NodePtr->FindComponentInstanceInActor(PreviewActor));
				if(SceneComponentTemplate && ParentSceneComponent && ParentSceneComponent->IsRegistered())
				{
					// If we find a match, calculate its new position relative to the scene root component instance in its current scene
					FTransform ComponentToWorld(SceneComponentTemplate->RelativeRotation, SceneComponentTemplate->RelativeLocation, SceneComponentTemplate->RelativeScale3D);
					FTransform ParentToWorld = ParentSceneComponent->GetSocketTransform(SceneComponentTemplate->AttachSocketName);
					FTransform RelativeTM = ComponentToWorld.GetRelativeTransform(ParentToWorld);

					// Store new relative location value (if not set to absolute)
					if(!SceneComponentTemplate->bAbsoluteLocation)
					{
						SceneComponentTemplate->RelativeLocation = RelativeTM.GetTranslation();
					}

					// Store new relative rotation value (if not set to absolute)
					if(!SceneComponentTemplate->bAbsoluteRotation)
					{
						SceneComponentTemplate->RelativeRotation = RelativeTM.Rotator();
					}

					// Store new relative scale value (if not set to absolute)
					if(!SceneComponentTemplate->bAbsoluteScale)
					{
						SceneComponentTemplate->RelativeScale3D = RelativeTM.GetScale3D();
					}
				}
			}
		}
	}
	else    // EComponentEditorMode::ActorInstance
	{
		for(const auto& DroppedNodePtr : DroppedNodePtrs)
		{
			// Check for a valid parent node
			FSCSEditorTreeNodePtrType ParentNodePtr = DroppedNodePtr->GetParent();
			if(ParentNodePtr.IsValid())
			{
				// Detach the dropped node from its parent
				ParentNodePtr->RemoveChild(DroppedNodePtr);
			}

			// Attach the dropped node to the given node
			NodePtr->AddChild(DroppedNodePtr);
		}
	}

	check(SCSEditorPtr->SCSTreeWidget.IsValid());
	SCSEditorPtr->SCSTreeWidget->SetItemExpansion(NodePtr, true);

	PostDragDropAction(bRegenerateTreeNodes);
}

void SSCS_RowWidget::OnDetachFromDropAction(const TArray<FSCSEditorTreeNodePtrType>& DroppedNodePtrs)
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();

	check(NodePtr.IsValid());
	check(DroppedNodePtrs.Num() > 0);

	TSharedPtr<SSCSEditor> SCSEditorPtr = SCSEditor.Pin();
	check(SCSEditorPtr.IsValid());

	const FScopedTransaction TransactionContext(DroppedNodePtrs.Num() > 1 ? LOCTEXT("DetachComponents", "Detach Components") : LOCTEXT("DetachComponent", "Detach Component"));

	if (SCSEditorPtr->GetEditorMode() == EComponentEditorMode::BlueprintSCS)
	{
		// Get the current "preview" Actor instance
		AActor* PreviewActor = SCSEditorPtr->PreviewActor.Get();
		check(PreviewActor);

		for(const auto& DroppedNodePtr : DroppedNodePtrs)
		{
			check(DroppedNodePtr.IsValid());

			// Detach the node from its parent
			NodePtr->RemoveChild(DroppedNodePtr);

			// If the associated component template is a scene component, maintain its current world position
			USceneComponent* SceneComponentTemplate = Cast<USceneComponent>(DroppedNodePtr->GetComponentTemplate());
			if(SceneComponentTemplate)
			{
				// Save current state
				SceneComponentTemplate->Modify();

				// Reset the attach socket name
				SceneComponentTemplate->AttachSocketName = NAME_None;
				USCS_Node* SCS_Node = DroppedNodePtr->GetSCSNode();
				if(SCS_Node)
				{
					SCS_Node->Modify();
					SCS_Node->AttachToName = NAME_None;
				}

				// Attempt to locate a matching instance of the component template in the Actor context that's being edited
				USceneComponent* InstancedSceneComponent = Cast<USceneComponent>(DroppedNodePtr->FindComponentInstanceInActor(PreviewActor));
				if(InstancedSceneComponent && InstancedSceneComponent->IsRegistered())
				{
					// If we find a match, save off the world position
					FTransform ComponentToWorld = InstancedSceneComponent->GetComponentToWorld();
					SceneComponentTemplate->RelativeLocation = ComponentToWorld.GetTranslation();
					SceneComponentTemplate->RelativeRotation = ComponentToWorld.Rotator();
					SceneComponentTemplate->RelativeScale3D = ComponentToWorld.GetScale3D();
				}
			}

			// Attach the dropped node to the current scene root node
			check(SCSEditorPtr->SceneRootNodePtr.IsValid());
			SCSEditorPtr->SceneRootNodePtr->AddChild(DroppedNodePtr);

			// Attempt to locate a matching instance of the scene root component template in the Actor context that's being edited
			USceneComponent* InstancedSceneRootComponent = Cast<USceneComponent>(SCSEditorPtr->SceneRootNodePtr->FindComponentInstanceInActor(PreviewActor));
			if(SceneComponentTemplate && InstancedSceneRootComponent && InstancedSceneRootComponent->IsRegistered())
			{
				// If we find a match, calculate its new position relative to the scene root component instance in the preview scene
				FTransform ComponentToWorld(SceneComponentTemplate->RelativeRotation, SceneComponentTemplate->RelativeLocation, SceneComponentTemplate->RelativeScale3D);
				FTransform ParentToWorld = InstancedSceneRootComponent->GetSocketTransform(SceneComponentTemplate->AttachSocketName);
				FTransform RelativeTM = ComponentToWorld.GetRelativeTransform(ParentToWorld);

				// Store new relative location value (if not set to absolute)
				if(!SceneComponentTemplate->bAbsoluteLocation)
				{
					SceneComponentTemplate->RelativeLocation = RelativeTM.GetTranslation();
				}

				// Store new relative rotation value (if not set to absolute)
				if(!SceneComponentTemplate->bAbsoluteRotation)
				{
					SceneComponentTemplate->RelativeRotation = RelativeTM.Rotator();
				}

				// Store new relative scale value (if not set to absolute)
				if(!SceneComponentTemplate->bAbsoluteScale)
				{
					SceneComponentTemplate->RelativeScale3D = RelativeTM.GetScale3D();
				}
			}
		}
	}
	else    // EComponentEditorMode::ActorInstance
	{
		for(const auto& DroppedNodePtr : DroppedNodePtrs)
		{
			check(DroppedNodePtr.IsValid());

			// Detach the node from its parent
			NodePtr->RemoveChild(DroppedNodePtr);

			// Attach the dropped node to the current scene root node
			check(SCSEditorPtr->SceneRootNodePtr.IsValid());
			SCSEditorPtr->SceneRootNodePtr->AddChild(DroppedNodePtr);
		}
	}
	
	PostDragDropAction(false);
}

void SSCS_RowWidget::OnMakeNewRootDropAction(FSCSEditorTreeNodePtrType DroppedNodePtr)
{
	TSharedPtr<SSCSEditor> SCSEditorPtr = SCSEditor.Pin();
	check(SCSEditorPtr.IsValid());

	// Get the current scene root node
	FSCSEditorTreeNodePtrType& SceneRootNodePtr = SCSEditorPtr->SceneRootNodePtr;

	FSCSEditorTreeNodePtrType NodePtr = GetNode();

	check(NodePtr.IsValid() && NodePtr == SceneRootNodePtr);
	check(DroppedNodePtr.IsValid());

	// Create a transaction record
	const FScopedTransaction TransactionContext(LOCTEXT("MakeNewSceneRoot", "Make New Scene Root"));

	if (SCSEditorPtr->GetEditorMode() == EComponentEditorMode::BlueprintSCS)
	{
		// Get the current Blueprint context
		UBlueprint* Blueprint = GetBlueprint();
		check(Blueprint != NULL && Blueprint->SimpleConstructionScript != nullptr);

		// Remember whether or not we're replacing the default scene root
		bool bWasDefaultSceneRoot = SceneRootNodePtr.IsValid() && SceneRootNodePtr->IsDefaultSceneRoot();

		// Clone the component if it's being dropped into a different SCS
		if(DroppedNodePtr->GetBlueprint() != Blueprint)
		{
			UActorComponent* ComponentTemplate = DroppedNodePtr->GetComponentTemplate();
			check(ComponentTemplate);

			// Note: This will mark the Blueprint as structurally modified
			UActorComponent* ClonedComponent = SCSEditorPtr->AddNewComponent(ComponentTemplate->GetClass(), NULL);
			check(ClonedComponent);

			//Serialize object properties using write/read operations.
			TArray<uint8> SavedProperties;
			FObjectWriter Writer(ComponentTemplate, SavedProperties);
			FObjectReader(ClonedComponent, SavedProperties);

			DroppedNodePtr = SCSEditorPtr->GetNodeFromActorComponent(ClonedComponent);
			check(DroppedNodePtr.IsValid());
		}

		if(DroppedNodePtr->GetParent().IsValid()
			&& DroppedNodePtr->GetBlueprint() == Blueprint)
		{
			// Remove the dropped node from its existing parent
			DroppedNodePtr->GetParent()->RemoveChild(DroppedNodePtr);

			// If the associated component template is a scene component, reset its transform since it will now become the root
			USceneComponent* SceneComponentTemplate = Cast<USceneComponent>(DroppedNodePtr->GetComponentTemplate());
			if(SceneComponentTemplate)
			{
				// Save current state
				SceneComponentTemplate->Modify();

				// Reset the attach socket name
				SceneComponentTemplate->AttachSocketName = NAME_None;
				USCS_Node* SCS_Node = DroppedNodePtr->GetSCSNode();
				if(SCS_Node)
				{
					SCS_Node->Modify();
					SCS_Node->AttachToName = NAME_None;
				}

				// Reset the relative transform
				SceneComponentTemplate->SetRelativeLocation(FVector::ZeroVector);
				SceneComponentTemplate->SetRelativeRotation(FRotator::ZeroRotator);
				SceneComponentTemplate->SetRelativeScale3D(FVector(1.f));
			}
		}

		if(!bWasDefaultSceneRoot)
		{
			check(SceneRootNodePtr->CanReparent());

			// Remove the current scene root node from the SCS context
			Blueprint->SimpleConstructionScript->RemoveNode(SceneRootNodePtr->GetSCSNode());
		}

		// Save old root node
		FSCSEditorTreeNodePtrType OldSceneRootNodePtr;
		if(!bWasDefaultSceneRoot)
		{
			OldSceneRootNodePtr = SceneRootNodePtr;
		}

		// Set node we are dropping as new root
		SceneRootNodePtr = DroppedNodePtr;

		// Add dropped node to the SCS context
		Blueprint->SimpleConstructionScript->AddNode(SceneRootNodePtr->GetSCSNode());

		// Set old root as child of new root
		if(OldSceneRootNodePtr.IsValid())
		{
			SceneRootNodePtr->AddChild(OldSceneRootNodePtr);
		}
	}
	else    // EComponentEditorMode::ActorInstance
	{
		if(DroppedNodePtr->GetParent().IsValid())
		{
			// Remove the dropped node from its existing parent
			DroppedNodePtr->GetParent()->RemoveChild(DroppedNodePtr);
		}

		check(SceneRootNodePtr->CanReparent());

		// Save old root node
		FSCSEditorTreeNodePtrType OldSceneRootNodePtr = SceneRootNodePtr;

		// Set node we are dropping as new root
		SceneRootNodePtr = DroppedNodePtr;

		// Set old root as child of new root
		check(OldSceneRootNodePtr.IsValid());
		SceneRootNodePtr->AddChild(OldSceneRootNodePtr);
	}

	PostDragDropAction(true);
}

void SSCS_RowWidget::PostDragDropAction(bool bRegenerateTreeNodes)
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();

	TSharedPtr<SSCSEditor> PinnedEditor = SCSEditor.Pin();
	if(PinnedEditor.IsValid())
	{
		PinnedEditor->UpdateTree(bRegenerateTreeNodes);

		PinnedEditor->RefreshSelectionDetails();

		if (PinnedEditor->GetEditorMode() == EComponentEditorMode::BlueprintSCS)
		{
			if(NodePtr.IsValid())
			{
				UBlueprint* Blueprint = GetBlueprint();
				if(Blueprint != nullptr)
				{
					FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint);
				}
			}
		}
	}
}

FText SSCS_RowWidget::GetNameLabel() const
{
	if( InlineWidget.IsValid() && !InlineWidget->IsInEditMode() )
	{
		FSCSEditorTreeNodePtrType NodePtr = GetNode();
		if(NodePtr->IsInherited())
		{
			return FText::Format(LOCTEXT("NativeComponentFormatString","{0} (Inherited)"), FText::FromString(GetNode()->GetDisplayString()));
		}
	}

	// NOTE: Whatever this returns also becomes the variable name
	return FText::FromString(GetNode()->GetDisplayString());
}

FText SSCS_RowWidget::GetTooltipText() const
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();

	if (NodePtr->IsDefaultSceneRoot())
	{
		if (NodePtr->IsInherited())
		{
			return LOCTEXT("InheritedDefaultSceneRootToolTip", "This is the default scene root component. It cannot be copied, renamed or deleted.\nIt has been inherited from the parent class, so its properties cannot be edited here.\nNew scene components will automatically be attached to it.");
		}
		else
		{
			return LOCTEXT("DefaultSceneRootToolTip", "This is the default scene root component. It cannot be copied, renamed or deleted.\nAdding a new scene component will automatically replace it as the new root.");
		}
	}
	else
	{
		UClass* Class = ( NodePtr->GetComponentTemplate() != nullptr ) ? NodePtr->GetComponentTemplate()->GetClass() : nullptr;
		const FText ClassDisplayName = FBlueprintEditorUtils::GetFriendlyClassDisplayName(Class);
		const FText ComponentDisplayName = NodePtr->GetDisplayName();


		FFormatNamedArguments Args;
		Args.Add(TEXT("ClassName"), ClassDisplayName);
		Args.Add(TEXT("NodeName"), FText::FromString(NodePtr->GetDisplayString()));

		return FText::Format(LOCTEXT("ComponentTooltip", "{NodeName} ({ClassName})"), Args);
	}
}

FString SSCS_RowWidget::GetDocumentationLink() const
{
	check(SCSEditor.IsValid());

	FSCSEditorTreeNodePtrType NodePtr = GetNode();
	if ((NodePtr == SCSEditor.Pin()->SceneRootNodePtr) || NodePtr->IsInherited())
	{
		return TEXT("Shared/Editors/BlueprintEditor/ComponentsMode");
	}

	return TEXT("");
}

FString SSCS_RowWidget::GetDocumentationExcerptName() const
{
	check(SCSEditor.IsValid());

	FSCSEditorTreeNodePtrType NodePtr = GetNode();
	if (NodePtr == SCSEditor.Pin()->SceneRootNodePtr)
	{
		return TEXT("RootComponent");
	}
	else if (NodePtr->IsNative())
	{
		return TEXT("NativeComponents");
	}
	else if (NodePtr->IsInherited())
	{
		return TEXT("InheritedComponents");
	}

	return TEXT("");
}

UBlueprint* SSCS_RowWidget::GetBlueprint() const
{
	check(SCSEditor.IsValid());
	return SCSEditor.Pin()->GetBlueprint();
}

ESelectionMode::Type SSCS_RowWidget::GetSelectionMode() const
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();
	if (NodePtr->GetNodeType() == FSCSEditorTreeNode::SeparatorNode)
	{
		return ESelectionMode::None;
	}
	
	return SMultiColumnTableRow<FSCSEditorTreeNodePtrType>::GetSelectionMode();
}

bool SSCS_RowWidget::OnNameTextVerifyChanged(const FText& InNewText, FText& OutErrorMessage)
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();
	UBlueprint* Blueprint = GetBlueprint();

	if (!InNewText.IsEmpty())
	{
		AActor* ExistingNameSearchScope = NodePtr->GetComponentTemplate()->GetOwner();
		if ((ExistingNameSearchScope == nullptr) && (Blueprint != nullptr))
		{
			ExistingNameSearchScope = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
		}

		if (!FComponentEditorUtils::IsValidVariableNameString(NodePtr->GetComponentTemplate(), InNewText.ToString()))
		{
			OutErrorMessage = LOCTEXT("RenameFailed_EngineReservedName", "This name is reserved for engine use.");
			return false;
		}
		else if (!FComponentEditorUtils::IsComponentNameAvailable(InNewText.ToString(), ExistingNameSearchScope, NodePtr->GetComponentTemplate()))
		{
			OutErrorMessage = LOCTEXT("RenameFailed_ExistingName", "Another component already has the same name.");
			return false;
		}
	}

	TSharedPtr<INameValidatorInterface> NameValidator;
	if (Blueprint != nullptr)
	{
		NameValidator = MakeShareable(new FKismetNameValidator(GetBlueprint(), NodePtr->GetVariableName()));
	}
	else
	{
		NameValidator = MakeShareable(new FStringSetNameValidator(NodePtr->GetComponentTemplate()->GetName()));
	}

	EValidatorResult ValidatorResult = NameValidator->IsValid(InNewText.ToString());
	if (ValidatorResult == EValidatorResult::AlreadyInUse)
	{
		OutErrorMessage = FText::Format(LOCTEXT("RenameFailed_InUse", "{0} is in use by another variable or function!"), InNewText);
	}
	else if (ValidatorResult == EValidatorResult::EmptyName)
	{
		OutErrorMessage = LOCTEXT("RenameFailed_LeftBlank", "Names cannot be left blank!");
	}
	else if (ValidatorResult == EValidatorResult::TooLong)
	{
		OutErrorMessage = LOCTEXT("RenameFailed_NameTooLong", "Names must have fewer than 100 characters!");
	}

	if (OutErrorMessage.IsEmpty())
	{
		return true;
	}

	return false;
}

void SSCS_RowWidget::OnNameTextCommit(const FText& InNewName, ETextCommit::Type InTextCommit)
{
	GetNode()->OnCompleteRename(InNewName);

	// No need to call UpdateTree() in SCS editor mode; it will already be called by MBASM internally
	check(SCSEditor.IsValid());
	TSharedPtr<SSCSEditor> PinnedEditor = SCSEditor.Pin();
	if (PinnedEditor.IsValid() && PinnedEditor->GetEditorMode() == EComponentEditorMode::ActorInstance)
	{
		PinnedEditor->UpdateTree(false);
	}
}

//////////////////////////////////////////////////////////////////////////
// SSCS_RowWidget_ActorRoot

TSharedRef<SWidget> SSCS_RowWidget_ActorRoot::GenerateWidgetForColumn(const FName& ColumnName)
{
	FSCSEditorTreeNodePtrType NodePtr = GetNode();

	// We've removed the other columns for now,  implement them for the root actor if necessary
	ensure(ColumnName == SCS_ColumnName_ComponentClass);

	// Create the name field
	TSharedPtr<SInlineEditableTextBlock> InlineWidget =
		SNew(SInlineEditableTextBlock)
		.Text(this, &SSCS_RowWidget_ActorRoot::GetActorDisplayText)
		.OnVerifyTextChanged(this, &SSCS_RowWidget_ActorRoot::OnVerifyActorLabelChanged)
		.OnTextCommitted(this, &SSCS_RowWidget_ActorRoot::OnNameTextCommit)
		.IsSelected(this, &SSCS_RowWidget_ActorRoot::IsSelectedExclusively)
		.IsReadOnly(!NodePtr->CanRename() || (SCSEditor.IsValid() && !SCSEditor.Pin()->IsEditingAllowed()));

	NodePtr->SetRenameRequestedDelegate(FSCSEditorTreeNode::FOnRenameRequested::CreateSP(InlineWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode));

	return SNew(SHorizontalBox)
		.ToolTip(CreateToolTipWidget())

	+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
		[
			SNew(SImage)
			.Image(this, &SSCS_RowWidget_ActorRoot::GetActorIcon)
		]

	+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f)
		[
			InlineWidget.ToSharedRef()
		]

	+SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(this, &SSCS_RowWidget_ActorRoot::GetActorContextText)
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

TSharedRef<SToolTip> SSCS_RowWidget_ActorRoot::CreateToolTipWidget() const
{
	// Create a box to hold every line of info in the body of the tooltip
	TSharedRef<SVerticalBox> InfoBox = SNew(SVerticalBox);

	// Add class
	AddToToolTipInfoBox(InfoBox, LOCTEXT("TooltipClass", "Class"), SNullWidget::NullWidget, TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SSCS_RowWidget_ActorRoot::GetActorClassNameText)), false);

	// Add super class
	AddToToolTipInfoBox(InfoBox, LOCTEXT("TooltipSuperClass", "Parent Class"), SNullWidget::NullWidget, TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SSCS_RowWidget_ActorRoot::GetActorSuperClassNameText)), false);

	// Add mobility
	AddToToolTipInfoBox(InfoBox, LOCTEXT("TooltipMobility", "Mobility"), SNullWidget::NullWidget, TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SSCS_RowWidget_ActorRoot::GetActorMobilityText)), false);

	TSharedRef<SBorder> TooltipContent = SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		.Padding(0)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4)
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "SCSEditor.ComponentTooltip.Title")
						.Text(this, &SSCS_RowWidget_ActorRoot::GetActorDisplayText)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("NoBorder"))
				.Padding(4)
				[
					InfoBox
				]
			]
		];

	return IDocumentation::Get()->CreateToolTip(TAttribute<FText>(this, &SSCS_RowWidget_ActorRoot::GetActorDisplayText), TooltipContent, InfoBox, TEXT(""), TEXT(""));
}

bool SSCS_RowWidget_ActorRoot::OnVerifyActorLabelChanged(const FText& InLabel, FText& OutErrorMessage)
{
	FText TrimmedLabel = FText::TrimPrecedingAndTrailing(InLabel);

	if (TrimmedLabel.IsEmpty())
	{
		OutErrorMessage = LOCTEXT("RenameFailed_LeftBlank", "Names cannot be left blank");
		return false;
	}

	if (TrimmedLabel.ToString().Len() >= NAME_SIZE)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("CharCount"), NAME_SIZE);
		OutErrorMessage = FText::Format(LOCTEXT("RenameFailed_TooLong", "Names must be less than {CharCount} characters long."), Arguments);
		return false;
	}

	return true;
}

const FSlateBrush* SSCS_RowWidget_ActorRoot::GetActorIcon() const
{
	if (SCSEditor.IsValid())
	{
		TSharedPtr<SSCSEditor> SCSEditorPtr = SCSEditor.Pin();
		if (SCSEditorPtr->ActorContext.IsSet())
		{
			return FClassIconFinder::FindIconForActor(SCSEditorPtr->GetActorContext());
		}
	}
	return nullptr;
}

FText SSCS_RowWidget_ActorRoot::GetActorDisplayText() const
{
	if (SCSEditor.IsValid())
	{
		TSharedPtr<SSCSEditor> SCSEditorPtr = SCSEditor.Pin();
		if (SCSEditorPtr->ActorContext.IsSet())
		{
			AActor* DefaultActor = SCSEditorPtr->ActorContext.Get();
			if( DefaultActor )
			{
				FString Name;
				UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(DefaultActor->GetClass());
				if(Blueprint != nullptr)
				{
					Blueprint->GetName(Name);
				}
				else
				{
					Name = DefaultActor->GetActorLabel();
				}
				return FText::FromString(Name);
			}
		}
	}
	return FText::GetEmpty();
}

FText SSCS_RowWidget_ActorRoot::GetActorContextText() const
{
	if (SCSEditor.IsValid())
	{
		TSharedPtr<SSCSEditor> SCSEditorPtr = SCSEditor.Pin();
		if (AActor* DefaultActor = SCSEditorPtr->GetActorContext())
		{
			if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(DefaultActor->GetClass()))
			{
				return LOCTEXT("ActorContext_self", " (self)");
			}
			else
			{
				return LOCTEXT("ActorContext_Instance", " (Instance)");
			}
		}
	}
	return FText::GetEmpty();
}

FText SSCS_RowWidget_ActorRoot::GetActorClassNameText() const
{
	FText Text;
	if (SCSEditor.IsValid())
	{
		TSharedPtr<SSCSEditor> SCSEditorPtr = SCSEditor.Pin();
		if (AActor* DefaultActor = SCSEditorPtr->GetActorContext())
		{
			Text = FText::FromString(DefaultActor->GetClass()->GetName());
		}
	}

	return Text;
}

FText SSCS_RowWidget_ActorRoot::GetActorSuperClassNameText() const
{
	FText Text;
	if (SCSEditor.IsValid())
	{
		TSharedPtr<SSCSEditor> SCSEditorPtr = SCSEditor.Pin();
		if (AActor* DefaultActor = SCSEditorPtr->GetActorContext())
		{
			Text = FText::FromString(DefaultActor->GetClass()->GetSuperClass()->GetName());
		}
	}

	return Text;
}

FText SSCS_RowWidget_ActorRoot::GetActorMobilityText() const
{
	FText Text;
	if (SCSEditor.IsValid())
	{
		TSharedPtr<SSCSEditor> SCSEditorPtr = SCSEditor.Pin();
		if (AActor* DefaultActor = SCSEditorPtr->GetActorContext())
		{
			USceneComponent* RootComponent = DefaultActor->GetRootComponent();

			if ((RootComponent == nullptr) && (SCSEditorPtr->SceneRootNodePtr.IsValid()))
			{
				RootComponent = Cast<USceneComponent>(SCSEditorPtr->SceneRootNodePtr->GetComponentTemplate());
			}

			if (RootComponent != nullptr)
			{
				if (RootComponent->Mobility == EComponentMobility::Static)
				{
					Text = LOCTEXT("ComponentMobility_Static", "Static");
				}
				else if (RootComponent->Mobility == EComponentMobility::Stationary)
				{
					Text = LOCTEXT("ComponentMobility_Stationary", "Stationary");
				}
				else if (RootComponent->Mobility == EComponentMobility::Movable)
				{
					Text = LOCTEXT("ComponentMobility_Movable", "Movable");
				}
			}
			else
			{
				Text = LOCTEXT("ComponentMobility_NoRoot", "No root component, unknown mobility");
			}
		}
	}

	return Text;
}

//////////////////////////////////////////////////////////////////////////
// SSCS_RowWidget_Separator


TSharedRef<SWidget> SSCS_RowWidget_Separator::GenerateWidgetForColumn(const FName& ColumnName)
{
	return SNew(SBox)
		.Padding(1.f)
		[
			SNew(SBorder)
			.Padding(FEditorStyle::GetMargin(TEXT("Menu.Separator.Padding")))
			.BorderImage(FEditorStyle::GetBrush(TEXT("Menu.Separator")))
		];
}

//////////////////////////////////////////////////////////////////////////
// SSCSEditor

void SSCSEditor::Construct( const FArguments& InArgs )
{
	EditorMode = InArgs._EditorMode;
	ActorContext = InArgs._ActorContext;
	AllowEditing = InArgs._AllowEditing;
	PreviewActor = InArgs._PreviewActor;
	ActorMenuExtender = InArgs._ActorMenuExtender;
	OnSelectionUpdated = InArgs._OnSelectionUpdated;
	OnItemDoubleClicked = InArgs._OnItemDoubleClicked;
	OnHighlightPropertyInDetailsView = InArgs._OnHighlightPropertyInDetailsView;
	bUpdatingSelection = false;

	CommandList = MakeShareable( new FUICommandList );
	CommandList->MapAction( FGenericCommands::Get().Cut,
		FUIAction( FExecuteAction::CreateSP( this, &SSCSEditor::CutSelectedNodes ), 
		FCanExecuteAction::CreateSP( this, &SSCSEditor::CanCutNodes ) ) 
		);
	CommandList->MapAction( FGenericCommands::Get().Copy,
		FUIAction( FExecuteAction::CreateSP( this, &SSCSEditor::CopySelectedNodes ), 
		FCanExecuteAction::CreateSP( this, &SSCSEditor::CanCopyNodes ) ) 
		);
	CommandList->MapAction( FGenericCommands::Get().Paste,
		FUIAction( FExecuteAction::CreateSP( this, &SSCSEditor::PasteNodes ), 
		FCanExecuteAction::CreateSP( this, &SSCSEditor::CanPasteNodes ) ) 
		);
	CommandList->MapAction( FGenericCommands::Get().Duplicate,
		FUIAction( FExecuteAction::CreateSP( this, &SSCSEditor::OnDuplicateComponent ), 
		FCanExecuteAction::CreateSP( this, &SSCSEditor::CanDuplicateComponent ) ) 
		);

	CommandList->MapAction( FGenericCommands::Get().Delete,
		FUIAction( FExecuteAction::CreateSP( this, &SSCSEditor::OnDeleteNodes ), 
		FCanExecuteAction::CreateSP( this, &SSCSEditor::CanDeleteNodes ) ) 
		);

	CommandList->MapAction( FGenericCommands::Get().Rename,
			FUIAction( FExecuteAction::CreateSP( this, &SSCSEditor::OnRenameComponent, true ), // true = transactional (i.e. undoable)
			FCanExecuteAction::CreateSP( this, &SSCSEditor::CanRenameComponent ) ) 
		);

	FSlateBrush const* MobilityHeaderBrush = FEditorStyle::GetBrush(TEXT("ClassIcon.ComponentMobilityHeaderIcon"));
	
	TSharedPtr<SHeaderRow> HeaderRow = SNew(SHeaderRow)
		+ SHeaderRow::Column(SCS_ColumnName_ComponentClass)
		.DefaultLabel(LOCTEXT("Class", "Class"))
		.FillWidth(4);
	
	SCSTreeWidget = SNew(SSCSTreeType)
		.ToolTipText(LOCTEXT("DropAssetToAddComponent", "Drop asset here to add a component."))
		.SCSEditor(this)
		.TreeItemsSource(&RootNodes)
		.SelectionMode(ESelectionMode::Multi)
		.OnGenerateRow(this, &SSCSEditor::MakeTableRowWidget)
		.OnGetChildren(this, &SSCSEditor::OnGetChildrenForTree)
		.OnSelectionChanged(this, &SSCSEditor::OnTreeSelectionChanged)
		.OnContextMenuOpening(this, &SSCSEditor::CreateContextMenu)
		.OnItemScrolledIntoView(this, &SSCSEditor::OnItemScrolledIntoView)
		.OnMouseButtonDoubleClick(this, &SSCSEditor::HandleItemDoubleClicked)
		.ClearSelectionOnClick(InArgs._EditorMode == EComponentEditorMode::BlueprintSCS ? true : false)
		.ItemHeight(24)
		.HeaderRow
		(
			HeaderRow
		);

	SCSTreeWidget->GetHeaderRow()->SetVisibility(EVisibility::Collapsed);

	TSharedPtr<SWidget> Contents;

	FMenuBuilder EditBlueprintMenuBuilder( true, NULL );

	EditBlueprintMenuBuilder.BeginSection( NAME_None, LOCTEXT("EditBlueprintMenu_ExistingBlueprintHeader", "Existing Blueprint" ) );

	EditBlueprintMenuBuilder.AddMenuEntry
	(
		LOCTEXT("OpenBlueprintEditor", "Open Blueprint Editor"),
		LOCTEXT("OpenBlueprintEditor_ToolTip", "Opens the blueprint editor for this asset"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SSCSEditor::OnOpenBlueprintEditor, /*bForceCodeEditing=*/ false))
	);

	EditBlueprintMenuBuilder.AddMenuEntry
	(
		LOCTEXT("OpenBlueprintEditorScriptMode", "Add or Edit Script"),
		LOCTEXT("OpenBlueprintEditorScriptMode_ToolTip", "Opens the blueprint editor for this asset, showing the event graph"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SSCSEditor::OnOpenBlueprintEditor, /*bForceCodeEditing=*/ true))
	);

	EditBlueprintMenuBuilder.BeginSection(NAME_None, LOCTEXT("EditBlueprintMenu_InstanceHeader", "Instance modifications"));

	EditBlueprintMenuBuilder.AddMenuEntry
	(
		LOCTEXT("PushChangesToBlueprint", "Apply Instance Changes to Blueprint"),
		TAttribute<FText>(this, &SSCSEditor::OnGetApplyChangesToBlueprintTooltip),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SSCSEditor::OnApplyChangesToBlueprint))
	);

	EditBlueprintMenuBuilder.AddMenuEntry
	(
		LOCTEXT("ResetToDefault", "Reset Instance Changes to Blueprint Default"),
		TAttribute<FText>(this, &SSCSEditor::OnGetResetToBlueprintDefaultsTooltip),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SSCSEditor::OnResetToBlueprintDefaults))
	);

	EditBlueprintMenuBuilder.BeginSection( NAME_None, LOCTEXT("EditBlueprintMenu_NewHeader", "Create New" ) );
	//EditBlueprintMenuBuilder.AddMenuSeparator();

	EditBlueprintMenuBuilder.AddMenuEntry
	(
		LOCTEXT("CreateChildBlueprint", "Create Child Blueprint Class"),
		LOCTEXT("CreateChildBlueprintTooltip", "Creates a Child Blueprint Class based on the current Blueprint, allowing you to create variants easily.  This replaces the current actor instance with a new one based on the new Child Blueprint Class." ),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SSCSEditor::PromoteToBlueprint))
	);

	bool bHideComponentClassCombo = InArgs._HideComponentClassCombo.Get();

	Contents = SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.Padding(0.0f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.Padding(0)
		[
			SNew(SBorder)
			.Padding(0)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ComponentsPanel")))
			.BorderBackgroundColor( FLinearColor( .6,.6,.6, 1.0f ) )
			[
				SNew(SHorizontalBox)
				
				+ SHorizontalBox::Slot()
				.Padding( 3.0f, 3.0f )
				.AutoWidth()
				.HAlign(HAlign_Left)
				[
					SNew(SComponentClassCombo)
					// Hack 4.7 Child actor components are not allowed to be added via the actor details panel 
					.AllowChildActorComponent(EditorMode==EComponentEditorMode::BlueprintSCS)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Actor.AddComponent")))
					.Visibility(bHideComponentClassCombo ? EVisibility::Hidden : EVisibility::Visible)
					.OnComponentClassSelected(this, &SSCSEditor::PerformComboAddClass)
					.ToolTipText(LOCTEXT("AddComponent_Tooltip", "Adds a new component to this actor"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				.Padding( 3.0f, 3.0f )
				[
					SNew( SButton )
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Actor.ConvertToBlueprint")))
					.Visibility( this, &SSCSEditor::GetPromoteToBlueprintButtonVisibility )
					.OnClicked( this, &SSCSEditor::OnPromoteToBlueprintClicked )
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Primary")
					.ContentPadding(FMargin(10,0))
					.ToolTip(IDocumentation::Get()->CreateToolTip(
						LOCTEXT("PromoteToBluerprintTooltip","Converts this actor into a reusable Blueprint Class that can have script behavior" ),
						NULL,
						TEXT("Shared/LevelEditor"),
						TEXT("ConvertToBlueprint")))
					[
						SNew(SHorizontalBox)
						
						//+SHorizontalBox::Slot()
						//.VAlign(VAlign_Center)
						//.AutoWidth()
						//.Padding(2.f, 1.f)
						//[
						//	SNew(SImage)
						//	.Image( FEditorStyle::Get().GetBrush( "ClassIcon.BlueprintCore" ) )
						//]

						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(3.f)
						.AutoWidth()
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
							.Font( FEditorStyle::Get().GetFontStyle( "FontAwesome.10" ) )
							.Text(FString(TEXT("\xf085")) /*fa-cogs*/)
						]

						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(3.f)
						.AutoWidth()
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
							//.Text( LOCTEXT("PromoteToBlueprint", "Add Script") )
							.Text(LOCTEXT("PromoteToBlueprint", "Blueprint/Add Script"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding( 3.0f, 3.0f )
				.HAlign(HAlign_Right)
				[
					SNew(SComboButton)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Actor.EditBlueprint")))
					.Visibility(this, &SSCSEditor::GetEditBlueprintButtonVisibility)
					.ContentPadding(FMargin(0))
					.ComboButtonStyle(FEditorStyle::Get(), "ToolbarComboButton")
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Primary")
					.ForegroundColor(FLinearColor::White)
					.ButtonContent()
					[
						SNew( SHorizontalBox )
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding( 2.0f, 0.0f )
						[
							SNew( SImage )
							.Image( FEditorStyle::Get().GetBrush( "ClassIcon.BlueprintCore" ) )
						]
						.Padding( 2.0f, 0.0f )
						+ SHorizontalBox::Slot()
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
							.Text(LOCTEXT("EditBlueprint", "Edit Blueprint"))
						]

					]
					.MenuContent()
					[
						EditBlueprintMenuBuilder.MakeWidget()
					]
				]
			]
		]

		+ SVerticalBox::Slot()
		.Padding(0.0f, 0.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FEditorStyle::GetBrush("SCSEditor.TreePanel"))
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ComponentsPanel")))
			[
				SCSTreeWidget.ToSharedRef()
			]
		]
	];

	this->ChildSlot
	[
		Contents.ToSharedRef()
	];

	// Refresh the tree widget
	UpdateTree();

	if (EditorMode == EComponentEditorMode::ActorInstance)
	{
		GEngine->OnLevelComponentRequestRename().AddSP(this, &SSCSEditor::OnLevelComponentRequestRename);
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


void SSCSEditor::OnLevelComponentRequestRename(const UActorComponent* InComponent)
{
	TArray< FSCSEditorTreeNodePtrType > SelectedItems = SCSTreeWidget->GetSelectedItems();
	
	FSCSEditorTreeNodePtrType Node = GetNodeFromActorComponent(InComponent);
	if (SelectedItems.Contains(Node) && CanRenameComponent())
	{
		OnRenameComponent(true);
	}
}

UBlueprint* SSCSEditor::GetBlueprint() const
{
	if (AActor* Actor = GetActorContext())
	{
		UClass* ActorClass = Actor->GetClass();
		check(ActorClass != nullptr);

		return Cast<UBlueprint>(ActorClass->ClassGeneratedBy);
	}

	return nullptr;
}

void SSCSEditor::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (EditorMode == EComponentEditorMode::ActorInstance)
	{
		TFunction<bool (const TArray<FSCSEditorTreeNodePtrType>&, int32&)> AreAnyNodesInvalidLambda = [&](const TArray<FSCSEditorTreeNodePtrType>& InNodes, int32& OutNumValidNodes) -> bool
		{
			bool bFoundInvalidNode = false;
			for(auto NodeIt = InNodes.CreateConstIterator(); NodeIt && !bFoundInvalidNode; ++NodeIt)
			{
				bool bIsComponent = (*NodeIt)->GetNodeType() == FSCSEditorTreeNode::ComponentNode;

				const UActorComponent* InstancedComponent = (*NodeIt)->GetComponentTemplate();
				bFoundInvalidNode = (bIsComponent && (!InstancedComponent || InstancedComponent->IsPendingKill())) || AreAnyNodesInvalidLambda((*NodeIt)->GetChildren(), OutNumValidNodes);
				if (bIsComponent)
				{
					++OutNumValidNodes;
				}
			}

			return bFoundInvalidNode;
		};

		if (const AActor* ActorInstance = GetActorContext())
		{
			int32 NumComponentInstances = 0;
			for (auto CompIt = ActorInstance->GetComponents().CreateConstIterator(); CompIt; ++CompIt)
			{
				// Don't count editor-only components, because we don't show them
				if (!(*CompIt)->IsEditorOnly())
				{
					++NumComponentInstances;
				}
			}

			int32 NumComponentNodes = 0;
			if (AreAnyNodesInvalidLambda(GetRootNodes(), NumComponentNodes) || NumComponentNodes != NumComponentInstances)
			{
				UE_LOG(LogSCSEditor, Log, TEXT("Calling UpdateTree() from Tick()."));

				UpdateTree();
			}
		}
	}
}

FReply SSCSEditor::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedRef<ITableRow> SSCSEditor::MakeTableRowWidget( FSCSEditorTreeNodePtrType InNodePtr, const TSharedRef<STableViewBase>& OwnerTable )
{
	// Setup a meta tag for this node
	FGraphNodeMetaData TagMeta(TEXT("TableRow"));
	if (InNodePtr.IsValid() && InNodePtr->GetComponentTemplate() != NULL )
	{
		TagMeta.FriendlyName = FString::Printf(TEXT("TableRow,%s,0"), *InNodePtr->GetComponentTemplate()->GetReadableName());
	}

	// Create the node of the appropriate type
	if (InNodePtr->GetNodeType() == FSCSEditorTreeNode::RootActorNode)
	{
		return SNew(SSCS_RowWidget_ActorRoot, SharedThis(this), InNodePtr, OwnerTable);
	}
	else if (InNodePtr->GetNodeType() == FSCSEditorTreeNode::SeparatorNode)
	{
		return SNew(SSCS_RowWidget_Separator, SharedThis(this), InNodePtr, OwnerTable);
	}

	return SNew(SSCS_RowWidget, SharedThis(this), InNodePtr, OwnerTable)
		.AddMetaData<FTutorialMetaData>(TagMeta);
}

void SSCSEditor::GetSelectedItemsForContextMenu(TArray<FComponentEventConstructionData>& OutSelectedItems) const
{
	TArray<FSCSEditorTreeNodePtrType> SelectedTreeItems = SCSTreeWidget->GetSelectedItems();
	for ( auto NodeIter = SelectedTreeItems.CreateConstIterator(); NodeIter; ++NodeIter )
	{
		FComponentEventConstructionData NewItem;
		auto TreeNode = *NodeIter;
		NewItem.VariableName = TreeNode->GetVariableName();
		NewItem.Component = TreeNode->GetComponentTemplate();
		OutSelectedItems.Add(NewItem);
	}
}

TSharedPtr< SWidget > SSCSEditor::CreateContextMenu()
{
	TArray<FSCSEditorTreeNodePtrType> SelectedNodes = SCSTreeWidget->GetSelectedItems();

	if (SelectedNodes.Num() > 0 || CanPasteNodes())
	{
		const bool CloseAfterSelection = true;
		FMenuBuilder MenuBuilder( CloseAfterSelection, CommandList, ActorMenuExtender );

		bool bShowJustPasteOption = false;

		if (SelectedNodes.Num() > 0)
		{
			if (SelectedNodes.Num() == 1 && SelectedNodes[0]->GetNodeType() == FSCSEditorTreeNode::RootActorNode)
			{
				if (EditorMode == EComponentEditorMode::BlueprintSCS)
				{
					bShowJustPasteOption = false;
				}
				else
				{
					// Display the Actor menu
					MenuBuilder.BeginSection("MainSection");
					MenuBuilder.EndSection();
				}
			}
			else
			{
				for (auto SelectedNode : SelectedNodes)
				{
					if (SelectedNode->GetNodeType() != FSCSEditorTreeNode::ComponentNode)
					{
						bShowJustPasteOption = true;
						break;
					}
				}
				if (!bShowJustPasteOption)
				{
					TArray<UActorComponent*> SelectedComponents;
					TArray<FSCSEditorTreeNodePtrType> SelectedNodes = GetSelectedNodes();
					for (int32 i = 0; i < SelectedNodes.Num(); ++i)
					{
						// Get the current selected node reference
						FSCSEditorTreeNodePtrType SelectedNodePtr = SelectedNodes[i];
						check(SelectedNodePtr.IsValid());

						// Get the component template associated with the selected node
						UActorComponent* ComponentTemplate = SelectedNodePtr->GetComponentTemplate();
						if (ComponentTemplate)
						{
							SelectedComponents.Add(ComponentTemplate);
						}
					}

					if (EditorMode == EComponentEditorMode::BlueprintSCS)
					{
						// Collect the classes of all selected objects
						TArray<UClass*> SelectionClasses;
						for( auto NodeIter = SelectedNodes.CreateConstIterator(); NodeIter; ++NodeIter )
						{
							auto TreeNode = *NodeIter;
							if( UActorComponent* ComponentTemplate = TreeNode->GetComponentTemplate() )
							{
								SelectionClasses.Add(ComponentTemplate->GetClass());
							}
						}

						if ( SelectionClasses.Num() )
						{
							// Find the common base class of all selected classes
							UClass* SelectedClass = UClass::FindCommonBase( SelectionClasses );
							// Build an event submenu if we can generate events
							if( FBlueprintEditorUtils::CanClassGenerateEvents( SelectedClass ))
							{
								MenuBuilder.AddSubMenu(	LOCTEXT("AddEventSubMenu", "Add Event"), 
									LOCTEXT("ActtionsSubMenu_ToolTip", "Add Event"), 
									FNewMenuDelegate::CreateStatic( &SSCSEditor::BuildMenuEventsSection,
									GetBlueprint(), SelectedClass, FCanExecuteAction::CreateSP(this, &SSCSEditor::IsEditingAllowed),
									FGetSelectedObjectsDelegate::CreateSP(this, &SSCSEditor::GetSelectedItemsForContextMenu)));
							}
						}
					}					

					FComponentEditorUtils::FillComponentContextMenuOptions(MenuBuilder, SelectedComponents);
				}
			}
		}
		else
		{
			bShowJustPasteOption = true;
		}

		if (bShowJustPasteOption)
		{
			MenuBuilder.BeginSection("ComponentActions", LOCTEXT("ComponentContextMenu", "Component Actions") );
			{
				MenuBuilder.AddMenuEntry( FGenericCommands::Get().Paste );
			}
			MenuBuilder.EndSection();
		}

		return MenuBuilder.MakeWidget();
	}
	return TSharedPtr<SWidget>();
}

void SSCSEditor::BuildMenuEventsSection(FMenuBuilder& Menu, UBlueprint* Blueprint, UClass* SelectedClass, FCanExecuteAction CanExecuteActionDelegate, FGetSelectedObjectsDelegate GetSelectedObjectsDelegate)
{
	// Get Selected Nodes
	TArray<FComponentEventConstructionData> SelectedNodes;
	GetSelectedObjectsDelegate.ExecuteIfBound( SelectedNodes );

	struct FMenuEntry
	{
		FText		Label;
		FText		ToolTip;
		FUIAction	UIAction;
	};

	TArray< FMenuEntry > Actions;
	TArray< FMenuEntry > NodeActions;
	// Build Events entries
	for (TFieldIterator<UMulticastDelegateProperty> PropertyIt(SelectedClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		UProperty* Property = *PropertyIt;

		// Check for multicast delegates that we can safely assign
		if (!Property->HasAnyPropertyFlags(CPF_Parm) && Property->HasAllPropertyFlags(CPF_BlueprintAssignable))
		{
			FName EventName = Property->GetFName();
			int32 ComponentEventViewEntries = 0;
			// Add View Event Per Component
			for (auto NodeIter = SelectedNodes.CreateConstIterator(); NodeIter; ++NodeIter )
			{
				if( NodeIter->Component.IsValid() )
				{
					FName VariableName = NodeIter->VariableName;
					UObjectProperty* VariableProperty = FindField<UObjectProperty>( Blueprint->SkeletonGeneratedClass, VariableName );

					if( VariableProperty && FKismetEditorUtilities::FindBoundEventForComponent( Blueprint, EventName, VariableProperty->GetFName() ))
					{
						FMenuEntry NewEntry;
						NewEntry.Label = ( SelectedNodes.Num() > 1 ) ?	FText::Format( LOCTEXT("ViewEvent_ToolTipFor", "{0} for {1}"), FText::FromName( EventName ), FText::FromName( VariableName )) : 
																		FText::Format( LOCTEXT("ViewEvent_ToolTip", "{0}"), FText::FromName( EventName ));
						NewEntry.UIAction =	FUIAction(FExecuteAction::CreateStatic( &SSCSEditor::ViewEvent, Blueprint, EventName, *NodeIter ), CanExecuteActionDelegate);
						NodeActions.Add( NewEntry );
						ComponentEventViewEntries++;
					}
				}
			}
			if( ComponentEventViewEntries < SelectedNodes.Num() )
			{
			// Create menu Add entry
				FMenuEntry NewEntry;
				NewEntry.Label = FText::Format( LOCTEXT("AddEvent_ToolTip", "Add {0}" ), FText::FromName( EventName ));
				NewEntry.UIAction =	FUIAction(FExecuteAction::CreateStatic( &SSCSEditor::CreateEventsForSelection, Blueprint, EventName, GetSelectedObjectsDelegate), CanExecuteActionDelegate);
				Actions.Add( NewEntry );
		}
	}
}
	// Build Menu Sections
	Menu.BeginSection("AddComponentActions", LOCTEXT("AddEventHeader", "Add Event"));
	for (auto ItemIter = Actions.CreateConstIterator(); ItemIter; ++ItemIter )
	{
		Menu.AddMenuEntry( ItemIter->Label, ItemIter->ToolTip, FSlateIcon(), ItemIter->UIAction );
	}
	Menu.EndSection();
	Menu.BeginSection("ViewComponentActions", LOCTEXT("ViewEventHeader", "View Existing Events"));
	for (auto ItemIter = NodeActions.CreateConstIterator(); ItemIter; ++ItemIter )
	{
		Menu.AddMenuEntry( ItemIter->Label, ItemIter->ToolTip, FSlateIcon(), ItemIter->UIAction );
	}
	Menu.EndSection();
}

void SSCSEditor::CreateEventsForSelection(UBlueprint* Blueprint, FName EventName, FGetSelectedObjectsDelegate GetSelectedObjectsDelegate)
{	
	if (EventName != NAME_None)
	{
		TArray<FComponentEventConstructionData> SelectedNodes;
		GetSelectedObjectsDelegate.ExecuteIfBound(SelectedNodes);

		for (auto SelectionIter = SelectedNodes.CreateConstIterator(); SelectionIter; ++SelectionIter)
		{
			ConstructEvent( Blueprint, EventName, *SelectionIter );
		}
	}
}

void SSCSEditor::ConstructEvent(UBlueprint* Blueprint, const FName EventName, const FComponentEventConstructionData EventData)
{
	// Find the corresponding variable property in the Blueprint
	UObjectProperty* VariableProperty = FindField<UObjectProperty>(Blueprint->SkeletonGeneratedClass, EventData.VariableName );

	if( VariableProperty )
	{
		if (!FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, EventName, VariableProperty->GetFName()))
		{
			FKismetEditorUtilities::CreateNewBoundEventForComponent(EventData.Component.Get(), EventName, Blueprint, VariableProperty);
		}
	}
}

void SSCSEditor::ViewEvent(UBlueprint* Blueprint, const FName EventName, const FComponentEventConstructionData EventData)
{
	// Find the corresponding variable property in the Blueprint
	UObjectProperty* VariableProperty = FindField<UObjectProperty>(Blueprint->SkeletonGeneratedClass, EventData.VariableName );

	if( VariableProperty )
	{
		const UK2Node_ComponentBoundEvent* ExistingNode = FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, EventName, VariableProperty->GetFName());
		if (ExistingNode)
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(ExistingNode);
		}
	}
}

bool SSCSEditor::CanDuplicateComponent() const
{
	if(!IsEditingAllowed())
	{
		return false;
	}

	return CanCopyNodes();
}

void SSCSEditor::OnDuplicateComponent()
{
	TArray<FSCSEditorTreeNodePtrType> SelectedNodes = SCSTreeWidget->GetSelectedItems();
	if(SelectedNodes.Num() > 0)
	{
		const FScopedTransaction Transaction(SelectedNodes.Num() > 1 ? LOCTEXT("DuplicateComponents", "Duplicate Components") : LOCTEXT("DuplicateComponent", "Duplicate Component"));

		for (int32 i = 0; i < SelectedNodes.Num(); ++i)
		{
			UActorComponent* ComponentTemplate = SelectedNodes[i]->GetComponentTemplate();
			if(ComponentTemplate != NULL)
			{
				UActorComponent* CloneComponent = AddNewComponent(ComponentTemplate->GetClass(), ComponentTemplate);
				UActorComponent* OriginalComponent = ComponentTemplate;

				// If we've duplicated a scene component, attempt to reposition the duplicate in the hierarchy if the original
				// was attached to another scene component as a child. By default, the duplicate is attached to the scene root node.
				USceneComponent* NewSceneComponent = Cast<USceneComponent>(CloneComponent);
				if(NewSceneComponent != NULL)
				{
					if (EditorMode == EComponentEditorMode::BlueprintSCS)
					{
						// Ensure that any native attachment relationship inherited from the original copy is removed (to prevent a GLEO assertion)
						NewSceneComponent->DetachFromParent(true);
					}
					
					// Attempt to locate the original node in the SCS tree
					FSCSEditorTreeNodePtrType OriginalNodePtr = FindTreeNode(OriginalComponent);
					if(OriginalNodePtr.IsValid())
					{
						// If the original node was parented, attempt to add the duplicate as a child of the same parent node
						FSCSEditorTreeNodePtrType ParentNodePtr = OriginalNodePtr->GetParent();
						if(ParentNodePtr.IsValid() && ParentNodePtr != SceneRootNodePtr)
						{
							// Locate the duplicate node (as a child of the current scene root node), and switch it to be a child of the original node's parent
							FSCSEditorTreeNodePtrType NewChildNodePtr = SceneRootNodePtr->FindChild(NewSceneComponent);
							if(NewChildNodePtr.IsValid())
							{
								// Note: This method will handle removal from the scene root node as well
								ParentNodePtr->AddChild(NewChildNodePtr);
							}
						}
					}
				}
			}
		}
	}
}

void SSCSEditor::OnGetChildrenForTree( FSCSEditorTreeNodePtrType InNodePtr, TArray<FSCSEditorTreeNodePtrType>& OutChildren )
{
	OutChildren.Empty();

	if(InNodePtr.IsValid())
	{
		OutChildren = InNodePtr->GetChildren();
	}
}


UActorComponent* SSCSEditor::PerformComboAddClass(TSubclassOf<UActorComponent> ComponentClass, EComponentCreateAction::Type ComponentCreateAction, UObject* AssetOverride)
{
	UClass* NewClass = ComponentClass;

	UActorComponent* NewComponent = nullptr;

	if( ComponentCreateAction == EComponentCreateAction::CreateNewCPPClass )
	{
		NewClass = CreateNewCPPComponent( ComponentClass );
	}
	else if( ComponentCreateAction == EComponentCreateAction::CreateNewBlueprintClass )
	{
		NewClass = CreateNewBPComponent( ComponentClass );
	}

	if( NewClass != nullptr )
	{
		FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();
		USelection* Selection =  GEditor->GetSelectedObjects();

		bool bAddedComponent = false;

		// This adds components according to the type selected in the drop down. If the user
		// has the appropriate objects selected in the content browser then those are added,
		// else we go down the previous route of adding components by type.
		//
		// Furthermore don't try to match up assets for USceneComponent it will match lots of things and doesn't have any nice behavior for asset adds 
		if (Selection->Num() > 0 && !AssetOverride && NewClass != USceneComponent::StaticClass())
		{
			for(FSelectionIterator ObjectIter(*Selection); ObjectIter; ++ObjectIter)
			{
				UObject* Object = *ObjectIter;
				UClass*  Class	= Object->GetClass();

				TArray< TSubclassOf<UActorComponent> > ComponentClasses = FComponentAssetBrokerage::GetComponentsForAsset(Object);

				// if the selected asset supports the selected component type then go ahead and add it
				for(int32 ComponentIndex = 0; ComponentIndex < ComponentClasses.Num(); ComponentIndex++)
				{
					if(ComponentClasses[ComponentIndex]->IsChildOf(NewClass))
					{
						NewComponent = AddNewComponent(NewClass, Object);
						bAddedComponent = true;
						break;
					}
				}
			}
		}

		if(!bAddedComponent)
		{
			// As the SCS splits up the scene and actor components, can now add directly
			NewComponent = AddNewComponent(NewClass, AssetOverride);
		}
	}

	return NewComponent;
}

TArray<FSCSEditorTreeNodePtrType>  SSCSEditor::GetSelectedNodes() const
{
	TArray<FSCSEditorTreeNodePtrType> SelectedTreeNodes = SCSTreeWidget->GetSelectedItems();

	struct FCompareSelectedSCSEditorTreeNodes
	{
		FORCEINLINE bool operator()(const FSCSEditorTreeNodePtrType& A, const FSCSEditorTreeNodePtrType& B) const
		{
			return B.IsValid() && B->IsAttachedTo(A);
		}
	};

	// Ensure that nodes are ordered from parent to child (otherwise they are sorted in the order that they were selected)
	SelectedTreeNodes.Sort(FCompareSelectedSCSEditorTreeNodes());

	return SelectedTreeNodes;
}

FSCSEditorTreeNodePtrType SSCSEditor::GetNodeFromActorComponent(const UActorComponent* ActorComponent, bool bIncludeAttachedComponents) const
{
	FSCSEditorTreeNodePtrType NodePtr;

	if(ActorComponent)
	{
		if (EditorMode == EComponentEditorMode::BlueprintSCS)
		{
			// If the given component instance is not already an archetype object
			if (!ActorComponent->IsTemplate())
			{
				// Get the component owner's class object
				check(ActorComponent->GetOwner() != NULL);
				UClass* OwnerClass = ActorComponent->GetOwner()->GetActorClass();

				// If the given component is one that's created during Blueprint construction
				if (ActorComponent->IsCreatedByConstructionScript())
				{
					TArray<UBlueprint*> ParentBPStack;

					// Check the entire Class hierarchy for the node
					UBlueprint::GetBlueprintHierarchyFromClass(OwnerClass, ParentBPStack);

					for(int32 StackIndex = ParentBPStack.Num() - 1; StackIndex >= 0; --StackIndex)
					{
						if(ParentBPStack[StackIndex]->SimpleConstructionScript)
						{
							// Attempt to locate an SCS node with a variable name that matches the name of the given component
							TArray<USCS_Node*> AllNodes = ParentBPStack[StackIndex]->SimpleConstructionScript->GetAllNodes();
							for (int32 i = 0; i < AllNodes.Num(); ++i)
							{
								USCS_Node* SCS_Node = AllNodes[i];

								check(SCS_Node != NULL);
								if (SCS_Node->VariableName == ActorComponent->GetFName())
								{
									// We found a match; redirect to the component archetype instance that may be associated with a tree node
									ActorComponent = SCS_Node->ComponentTemplate;
									break;
								}
							}

						}

					}
				}
				else
				{
					// Get the class default object
					const AActor* CDO = Cast<AActor>(OwnerClass->GetDefaultObject());
					if (CDO)
					{
						// Iterate over the Components array and attempt to find a component with a matching name
						TInlineComponentArray<UActorComponent*> Components;
						CDO->GetComponents(Components);

						for (auto It = Components.CreateConstIterator(); It; ++It)
						{
							UActorComponent* ComponentTemplate = *It;
							if (ComponentTemplate->GetFName() == ActorComponent->GetFName())
							{
								// We found a match; redirect to the component archetype instance that may be associated with a tree node
								ActorComponent = ComponentTemplate;
								break;
							}
						}
					}
				}
			}
		}

		// If we have a valid component archetype instance, attempt to find a tree node that corresponds to it
		if ((EditorMode == EComponentEditorMode::BlueprintSCS && ActorComponent->IsTemplate()) || EditorMode == EComponentEditorMode::ActorInstance)
		{
			const TArray<FSCSEditorTreeNodePtrType>& Nodes = GetRootNodes();
			for (int32 i = 0; i < Nodes.Num() && !NodePtr.IsValid(); i++)
			{
				NodePtr = FindTreeNode(ActorComponent, Nodes[i]);
			}
		}

		// If we didn't find it in the tree, step up the chain to the parent of the given component and recursively see if that is in the tree (unless the flag is false)
		if(!NodePtr.IsValid() && bIncludeAttachedComponents)
		{
			const USceneComponent* SceneComponent = Cast<const USceneComponent>(ActorComponent);
			if(SceneComponent && SceneComponent->AttachParent)
			{
				return GetNodeFromActorComponent(SceneComponent->AttachParent, bIncludeAttachedComponents);
			}
		}
	}

	return NodePtr;
}

void SSCSEditor::SelectRoot()
{
	const TArray<FSCSEditorTreeNodePtrType>& Nodes = GetRootNodes();
	if (Nodes.Num() > 0)
	{
		SCSTreeWidget->SetSelection(Nodes[0]);
	}
}

void SSCSEditor::SelectNode(FSCSEditorTreeNodePtrType InNodeToSelect, bool IsCntrlDown) 
{
	if(SCSTreeWidget.IsValid() && InNodeToSelect.IsValid())
	{
		if(!IsCntrlDown)
		{
			SCSTreeWidget->SetSelection(InNodeToSelect);
		}
		else
		{
			SCSTreeWidget->SetItemSelection(InNodeToSelect, !SCSTreeWidget->IsItemSelected(InNodeToSelect));
		}
	}
}

static FSCSEditorTreeNode* FindRecursive( FSCSEditorTreeNode* Node, FName Name )
{
	if (Node->GetVariableName() == Name)
	{
		return Node;
	}
	else
	{
		for (const auto& Child : Node->GetChildren())
		{
			if (auto Result = FindRecursive(Child.Get(), Name))
			{
				return Result;
			}
		}
	}

	return nullptr;
}

void SSCSEditor::HighlightTreeNode(FName TreeNodeName, const class FPropertyPath& Property)
{
	for( const auto& Node : GetRootNodes() )
	{
		if( auto FoundNode = FindRecursive( Node.Get(), TreeNodeName ) )
		{
			SelectNode(FoundNode->AsShared(), false);

			if (Property != FPropertyPath())
			{
				// Invoke the delegate to highlight the property
				OnHighlightPropertyInDetailsView.ExecuteIfBound(Property);
			}

			return;
		}
	}
	
	ClearSelection();
}

void SSCSEditor::HighlightTreeNode(const USCS_Node* Node, FName Property)
{
	check(Node);
	auto TreeNode = FindTreeNode( Node );
	check( TreeNode.IsValid() );
	SelectNode( TreeNode, false );
	if( Property != FName() )
	{
		UActorComponent* Component = TreeNode->GetComponentTemplate();
		UProperty* CurrentProp = FindField<UProperty>(Component->GetClass(), Property);
		FPropertyPath Path;
		if( CurrentProp )
		{
			FPropertyInfo NewInfo = { CurrentProp, -1 };
			Path.ExtendPath(NewInfo);
		}

		// Invoke the delegate to highlight the property
		OnHighlightPropertyInDetailsView.ExecuteIfBound( Path );
	}
}

void SSCSEditor::UpdateTree(bool bRegenerateTreeNodes)
{
	check(SCSTreeWidget.IsValid());

	if(bRegenerateTreeNodes)
	{
		// Obtain the set of expandable tree nodes that are currently collapsed
		TSet<FSCSEditorTreeNodePtrType> CollapsedTreeNodes;
		GetCollapsedNodes(SceneRootNodePtr, CollapsedTreeNodes);

		// Obtain the list of selected items
		TArray<FSCSEditorTreeNodePtrType> SelectedTreeNodes = SCSTreeWidget->GetSelectedItems();

		// Clear the current tree
		if (SelectedTreeNodes.Num() != 0)
		{
			SCSTreeWidget->ClearSelection();
		}
		RootNodes.Empty();
		RootComponentNodes.Empty();

		// Reset the scene root node
		SceneRootNodePtr.Reset();

		TSharedPtr<FSCSEditorTreeNode> ActorTreeNode = MakeShareable(new FSCSEditorTreeNodeRootActor(GetActorContext(),EditorMode == EComponentEditorMode::ActorInstance));

		RootNodes.Add(ActorTreeNode);
		RootNodes.Add(MakeShareable(new FSCSEditorTreeNodeSeparator()));

		// Build the tree data source according to what mode we're in
		if (EditorMode == EComponentEditorMode::BlueprintSCS)
		{
			// Get the class default object
			AActor* CDO = NULL;
			TArray<UBlueprint*> ParentBPStack;

			if(AActor* Actor = GetActorContext())
			{
				UClass* ActorClass = Actor->GetClass();
				if(ActorClass != nullptr)
				{
					CDO = ActorClass->GetDefaultObject<AActor>();

					// If it's a Blueprint-generated class, also get the inheritance stack
					UBlueprint::GetBlueprintHierarchyFromClass(ActorClass, ParentBPStack);
				}
			}

			if(CDO != NULL)
			{
				
				TInlineComponentArray<UActorComponent*> Components;
				CDO->GetComponents(Components);

				// Add the native root component
				USceneComponent* RootComponent = CDO->GetRootComponent();
				if(RootComponent != NULL)
				{
					Components.Remove(RootComponent);
					AddTreeNodeFromComponent(RootComponent);
				}
				
				// Add the rest of the native base class SceneComponent hierarchy
				for(auto CompIter = Components.CreateIterator(); CompIter; ++CompIter)
				{
					USceneComponent* SceneComp = Cast<USceneComponent>(*CompIter);
					if(SceneComp != NULL)
					{
						AddTreeNodeFromComponent(SceneComp);
					}
				}

				// Add native ActorComponent nodes that aren't SceneComponents
				bool bSeparatorAdded = false;
				for(auto CompIter = Components.CreateIterator(); CompIter; ++CompIter)
				{
					UActorComponent* ActorComp = *CompIter;
					if (!ActorComp->IsA<USceneComponent>())
					{
						if (!bSeparatorAdded)
						{
							bSeparatorAdded = true;
							RootNodes.Add(MakeShareable(new FSCSEditorTreeNodeSeparator()));
						}
						AddRootComponentTreeNode(ActorComp);
					}
				}
			}

			// Add the full SCS tree node hierarchy (including SCS nodes inherited from parent blueprints)
			for(int32 StackIndex = ParentBPStack.Num() - 1; StackIndex >= 0; --StackIndex)
			{
				if(ParentBPStack[StackIndex]->SimpleConstructionScript != NULL)
				{
					const TArray<USCS_Node*>& SCS_RootNodes = ParentBPStack[StackIndex]->SimpleConstructionScript->GetRootNodes();
					for(int32 NodeIndex = 0; NodeIndex < SCS_RootNodes.Num(); ++NodeIndex)
					{
						USCS_Node* SCS_Node = SCS_RootNodes[NodeIndex];
						check(SCS_Node != NULL);

						if(SCS_Node->ParentComponentOrVariableName != NAME_None)
						{
							USceneComponent* ParentComponent = SCS_Node->GetParentComponentTemplate(ParentBPStack[0]);
							if(ParentComponent != NULL)
							{
								FSCSEditorTreeNodePtrType ParentNodePtr = FindTreeNode(ParentComponent);
								if(ParentNodePtr.IsValid())
								{
									AddTreeNode(SCS_Node, ParentNodePtr, StackIndex > 0);
								}
							}
						}
						else
						{
							AddTreeNode(SCS_Node, SceneRootNodePtr, StackIndex > 0);
						}
					}
				}
			}
		}
		else    // EComponentEditorMode::ActorInstance
		{
			// Get the actor instance that we're editing
			if (AActor* ActorInstance = GetActorContext())
			{
				// Get the full set of instanced components
				TInlineComponentArray<UActorComponent*> Components;
				ActorInstance->GetComponents(Components);

				// Add the root component first (it may not be the first one)
				USceneComponent* RootComponent = ActorInstance->GetRootComponent();
				if(RootComponent != nullptr)
				{
					Components.Remove(RootComponent);
					AddTreeNodeFromComponent(RootComponent);
				}
				
				// Now add the rest of the instanced scene component hierarchy
				for(auto CompIter = Components.CreateIterator(); CompIter; ++CompIter)
				{
					USceneComponent* SceneComp = Cast<USceneComponent>(*CompIter);
					if(SceneComp != nullptr && !SceneComp->IsEditorOnly())
					{
						AddTreeNodeFromComponent(SceneComp);
					}
				}

				// Add all non-scene component instances to the root set first
				bool bSeparatorAdded = false;
				for(auto CompIter = Components.CreateIterator(); CompIter; ++CompIter)
				{
					UActorComponent* ActorComp = *CompIter;
					if (!ActorComp->IsA<USceneComponent>() && !ActorComp->IsEditorOnly())
					{
						if (!bSeparatorAdded)
						{
							bSeparatorAdded = true;
							RootNodes.Add(MakeShareable(new FSCSEditorTreeNode(FSCSEditorTreeNode::SeparatorNode)));
						}
						AddRootComponentTreeNode(ActorComp);
					}
				}
			}
		}

		// Restore the previous expansion state on the new tree nodes
		TArray<FSCSEditorTreeNodePtrType> CollapsedTreeNodeArray = CollapsedTreeNodes.Array();
		for(int i = 0; i < CollapsedTreeNodeArray.Num(); ++i)
		{
			// Look for a component match in the new hierarchy; if found, mark it as collapsed to match the previous setting
			FSCSEditorTreeNodePtrType NodeToExpandPtr = FindTreeNode(CollapsedTreeNodeArray[i]->GetComponentTemplate());
			if(NodeToExpandPtr.IsValid())
			{
				SCSTreeWidget->SetItemExpansion(NodeToExpandPtr, false);
			}
		}

		if(SelectedTreeNodes.Num() > 0)
		{
			// Restore the previous selection state on the new tree nodes
			for (int i = 0; i < SelectedTreeNodes.Num(); ++i)
			{
				if (SelectedTreeNodes[i]->GetNodeType() == FSCSEditorTreeNode::RootActorNode)
				{
					SCSTreeWidget->SetItemSelection(ActorTreeNode, true);
				}
				else
				{
					FSCSEditorTreeNodePtrType NodeToSelectPtr = FindTreeNode(SelectedTreeNodes[i]->GetComponentTemplate());
					if (NodeToSelectPtr.IsValid())
					{
						SCSTreeWidget->SetItemSelection(NodeToSelectPtr, true);
					}
				}
			}

			if (GetEditorMode() != EComponentEditorMode::BlueprintSCS)
			{
				TArray<FSCSEditorTreeNodePtrType> NewSelectedTreeNodes = SCSTreeWidget->GetSelectedItems();
				if (NewSelectedTreeNodes.Num() == 0)
				{
					SCSTreeWidget->SetItemSelection(GetRootNodes()[0], true);
				}
			}
		}

		// If we have a pending deferred rename request, redirect it to the new tree node
		if(DeferredRenameRequest != NAME_None)
		{
			FSCSEditorTreeNodePtrType NodeToRenamePtr = FindTreeNode(DeferredRenameRequest);
			if(NodeToRenamePtr.IsValid())
			{
				SCSTreeWidget->RequestScrollIntoView(NodeToRenamePtr);
			}
		}
	}

	// refresh widget
	SCSTreeWidget->RequestTreeRefresh();
}

const TArray<FSCSEditorTreeNodePtrType>& SSCSEditor::GetRootNodes() const
{
	return RootNodes;
}

TSharedPtr<FSCSEditorTreeNode> SSCSEditor::AddRootComponentTreeNode(UActorComponent* ActorComp)
{
	TSharedPtr<FSCSEditorTreeNode> NewTreeNode;
	if (RootTreeNode.IsValid())
	{
		NewTreeNode = RootTreeNode->AddChildFromComponent(ActorComp);
	}
	else
	{
		NewTreeNode = FSCSEditorTreeNode::FactoryNodeFromComponent(ActorComp);
		RootNodes.Add(NewTreeNode);
	}

	RootComponentNodes.Add(NewTreeNode);

	return NewTreeNode;
}

class FComponentClassParentFilter : public IClassViewerFilter
{
public:
	FComponentClassParentFilter(const TSubclassOf<UActorComponent>& InComponentClass) : ComponentClass(InComponentClass) {}

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs ) override
	{
		return InClass->IsChildOf(ComponentClass);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return InUnloadedClassData->IsChildOf(ComponentClass);
	}

	TSubclassOf<UActorComponent> ComponentClass;
};

typedef FComponentClassParentFilter FNativeComponentClassParentFilter;

class FBlueprintComponentClassParentFilter : public FComponentClassParentFilter
{
public:
	FBlueprintComponentClassParentFilter(const TSubclassOf<UActorComponent>& InComponentClass) : FComponentClassParentFilter(InComponentClass) {}

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs ) override
	{
		return FComponentClassParentFilter::IsClassAllowed(InInitOptions, InClass, InFilterFuncs) && FKismetEditorUtilities::CanCreateBlueprintOfClass(InClass);
	}
};

UClass* SSCSEditor::CreateNewCPPComponent( TSubclassOf<UActorComponent> ComponentClass )
{
	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));

	FString AddedClassName;
	auto OnCodeAddedToProject = [&AddedClassName](const FString& ClassName, const FString& ClassPath, const FString& ModuleName)
	{
		if(!ClassName.IsEmpty() && !ClassPath.IsEmpty())
		{
			AddedClassName = FString::Printf(TEXT("/Script/%s.%s"), *ModuleName, *ClassName);
		}
	};

	FGameProjectGenerationModule::Get().OpenAddCodeToProjectDialog(
		FAddToProjectConfig()
		.WindowTitle(LOCTEXT("AddNewC++Component", "Add C++ Component"))
		.ParentWindow(ParentWindow)
		.Modal()
		.OnAddedToProject(FOnAddedToProject::CreateLambda(OnCodeAddedToProject))
		.FeatureComponentClasses()
		.AllowableParents(MakeShareable( new FNativeComponentClassParentFilter(ComponentClass) ))
		.DefaultClassPrefix(TEXT("New"))
	);


	return LoadClass<UActorComponent>(nullptr, *AddedClassName, nullptr, LOAD_None, nullptr);
}

UClass* SSCSEditor::CreateNewBPComponent(TSubclassOf<UActorComponent> ComponentClass)
{
	UClass* NewClass = nullptr;

	auto OnAddedToProject = [&](const FString& ClassName, const FString& PackagePath, const FString& ModuleName)
	{
		if(!ClassName.IsEmpty() && !PackagePath.IsEmpty())
		{
			if (UPackage* Package = FindPackage(nullptr, *PackagePath))
			{
				if (UBlueprint* NewBP = FindObjectFast<UBlueprint>(Package, *ClassName))	
				{
					NewClass = NewBP->GeneratedClass;

					TArray<UObject*> Objects;
					Objects.Emplace(NewBP);
					GEditor->SyncBrowserToObjects(Objects);

					// Open the editor for the new blueprint
					FAssetEditorManager::Get().OpenEditorForAsset(NewBP);
				}
			}
		}
	};

	FGameProjectGenerationModule::Get().OpenAddBlueprintToProjectDialog(
		FAddToProjectConfig()
		.WindowTitle(LOCTEXT("AddNewBlueprintComponent", "Add Blueprint Component"))
		.ParentWindow(FSlateApplication::Get().FindWidgetWindow(SharedThis(this)))
		.Modal()
		.AllowableParents(MakeShareable( new FBlueprintComponentClassParentFilter(ComponentClass) ))
		.FeatureComponentClasses()
		.OnAddedToProject(FOnAddedToProject::CreateLambda(OnAddedToProject))
		.DefaultClassPrefix(TEXT("New"))
	);

	return NewClass;
}

void SSCSEditor::ClearSelection()
{
	if ( bUpdatingSelection == false )
	{
		check(SCSTreeWidget.IsValid());
		SCSTreeWidget->ClearSelection();
	}
}

void SSCSEditor::SaveSCSCurrentState( USimpleConstructionScript* SCSObj )
{
	if( SCSObj )
	{
		SCSObj->Modify();

		const TArray<USCS_Node*>& SCS_RootNodes = SCSObj->GetRootNodes();
		for(int32 i = 0; i < SCS_RootNodes.Num(); ++i)
		{
			SaveSCSNode( SCS_RootNodes[i] );
		}
	}
}

void SSCSEditor::SaveSCSNode( USCS_Node* Node )
{
	if( Node )
	{
		Node->Modify();

		for( int32 i=0; i<Node->ChildNodes.Num(); i++ )
		{
			SaveSCSNode( Node->ChildNodes[i] );
		}
	}
}

bool SSCSEditor::IsEditingAllowed() const
{
	return AllowEditing.Get() && nullptr == GEditor->PlayWorld;
}

UActorComponent* SSCSEditor::AddNewComponent( UClass* NewComponentClass, UObject* Asset  )
{
	const FScopedTransaction Transaction( LOCTEXT("AddComponent", "Add Component") );

	UActorComponent* NewComponent = nullptr;
	UActorComponent* ComponentTemplate = Cast<UActorComponent>(Asset);

	if (ComponentTemplate)
	{
		Asset = nullptr;
	}

	if (EditorMode == EComponentEditorMode::BlueprintSCS)
	{
		UBlueprint* Blueprint = GetBlueprint();
		check(Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr);
		
		Blueprint->Modify();
		SaveSCSCurrentState(Blueprint->SimpleConstructionScript);

		FName NewVariableName = Asset != nullptr ? Asset->GetFName() : NAME_None;
		NewComponent = AddNewNode(Blueprint->SimpleConstructionScript->CreateNode(NewComponentClass, NewVariableName), Asset, true);

		if (ComponentTemplate)
		{
			//Serialize object properties using write/read operations.
			TArray<uint8> SavedProperties;
			FObjectWriter Writer(ComponentTemplate, SavedProperties);
			FObjectReader(NewComponent, SavedProperties);
			NewComponent->UpdateComponentToWorld();
		}
	}
	else    // EComponentEditorMode::ActorInstance
	{
		if (ComponentTemplate)
		{
			// Create a duplicate of the provided template
			NewComponent = AddNewNodeForInstancedComponent(FComponentEditorUtils::DuplicateComponent(ComponentTemplate), nullptr);
		}
		else if (AActor* ActorInstance = GetActorContext())
		{
			// No template, so create a wholly new component
			ActorInstance->Modify();

			// Create an appropriate name for the new component
			FName NewComponentName = NAME_None;
			if (Asset)
			{
				NewComponentName = *FComponentEditorUtils::GenerateValidVariableNameFromAsset(Asset, ActorInstance);
			}
			else
			{
				NewComponentName = *FComponentEditorUtils::GenerateValidVariableName(NewComponentClass, ActorInstance);
			}

			// Construct the new component and attach as needed
			auto NewInstanceComponent = NewObject<UActorComponent>(ActorInstance, NewComponentClass, NewComponentName, RF_Transactional);
			if (USceneComponent* NewSceneComponent = Cast<USceneComponent>(NewInstanceComponent))
			{
				USceneComponent* RootComponent = ActorInstance->GetRootComponent();
				if (RootComponent)
				{
					NewSceneComponent->AttachTo(RootComponent, NAME_None, EAttachLocation::KeepRelativeOffset);
				}
				else
				{
					ActorInstance->SetRootComponent(NewSceneComponent);
				}
			}

			// If the component was created from/for a particular asset, assign it now
			if (Asset)
			{
				FComponentAssetBrokerage::AssignAssetToComponent(NewInstanceComponent, Asset);
			}

			// Add to SerializedComponents array so it gets saved
			ActorInstance->AddInstanceComponent(NewInstanceComponent);
			NewInstanceComponent->RegisterComponent();

			NewComponent = AddNewNodeForInstancedComponent(NewInstanceComponent, Asset);
		}
	}

	return NewComponent;
}

UActorComponent* SSCSEditor::AddNewNode(USCS_Node* NewNode, UObject* Asset, bool bMarkBlueprintModified, bool bSetFocusToNewItem)
{
	check(NewNode != nullptr);

	if(Asset)
	{
		FComponentAssetBrokerage::AssignAssetToComponent(NewNode->ComponentTemplate, Asset);
	}

	FSCSEditorTreeNodePtrType NewNodePtr;

	UBlueprint* Blueprint = GetBlueprint();
	check(Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr);

	// Reset the scene root node if it's set to the default one that's managed by the SCS
	if(SceneRootNodePtr.IsValid() && SceneRootNodePtr->GetSCSNode() == Blueprint->SimpleConstructionScript->GetDefaultSceneRootNode())
	{
		SceneRootNodePtr.Reset();
	}

	// Add the new node to the editor tree
	NewNodePtr = AddTreeNode(NewNode, SceneRootNodePtr, false);

	// Potentially adjust variable names for any child blueprints
	if(NewNode->VariableName != NAME_None)
	{
		FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, NewNode->VariableName);
	}
	
	if(bSetFocusToNewItem)
	{
		// Select and request a rename on the new component
		SCSTreeWidget->SetSelection(NewNodePtr);
		OnRenameComponent(false);
	}

	// Will call UpdateTree as part of OnBlueprintChanged handling
	if(bMarkBlueprintModified)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else
	{
		UpdateTree();
	}

	return NewNode->ComponentTemplate;
}

UActorComponent* SSCSEditor::AddNewNodeForInstancedComponent(UActorComponent* NewInstanceComponent, UObject* Asset, bool bSetFocusToNewItem)
{
	check(NewInstanceComponent != nullptr);

	FSCSEditorTreeNodePtrType NewNodePtr;

	// Add the new node to the editor tree
	USceneComponent* NewSceneComponent = Cast<USceneComponent>(NewInstanceComponent);
	if(NewSceneComponent != nullptr)
	{
		NewNodePtr = AddTreeNodeFromComponent(NewSceneComponent);

		// Remove the old scene root node if it's set to the default one
		if(SceneRootNodePtr.IsValid() && SceneRootNodePtr->IsDefaultSceneRoot())
		{
			RemoveComponentNode(SceneRootNodePtr);
			RootNodes.Remove( SceneRootNodePtr );
			SceneRootNodePtr.Reset();
		}
	}
	else
	{
		NewNodePtr = AddRootComponentTreeNode(NewInstanceComponent);
	}

	if(bSetFocusToNewItem)
	{
		// Select and request a rename on the new component
		SCSTreeWidget->SetSelection(NewNodePtr);
		OnRenameComponent(false);
	}

	UpdateTree(false);


	return NewInstanceComponent;
}

bool SSCSEditor::IsComponentSelected(const UPrimitiveComponent* PrimComponent) const
{
	check(PrimComponent != NULL);

	FSCSEditorTreeNodePtrType NodePtr = GetNodeFromActorComponent(PrimComponent);
	if(NodePtr.IsValid() && SCSTreeWidget.IsValid())
	{
		return SCSTreeWidget->IsItemSelected(NodePtr);
	}

	return false;
}

void SSCSEditor::SetSelectionOverride(UPrimitiveComponent* PrimComponent) const
{
	PrimComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateSP(this, &SSCSEditor::IsComponentSelected);
	PrimComponent->PushSelectionToProxy();
}

bool SSCSEditor::CanCutNodes() const
{
	return CanCopyNodes() && CanDeleteNodes();
}

void SSCSEditor::CutSelectedNodes()
{
	TArray<FSCSEditorTreeNodePtrType> SelectedNodes = GetSelectedNodes();
	const FScopedTransaction Transaction( SelectedNodes.Num() > 1 ? LOCTEXT("CutComponents", "Cut Components") : LOCTEXT("CutComponent", "Cut Component") );

	CopySelectedNodes();
	OnDeleteNodes();
}

bool SSCSEditor::CanCopyNodes() const
{
	TArray<UActorComponent*> ComponentsToCopy;
	TArray<FSCSEditorTreeNodePtrType> SelectedNodes = GetSelectedNodes();
	for (int32 i = 0; i < SelectedNodes.Num(); ++i)
	{
		// Get the current selected node reference
		FSCSEditorTreeNodePtrType SelectedNodePtr = SelectedNodes[i];
		check(SelectedNodePtr.IsValid());

		// Get the component template associated with the selected node
		UActorComponent* ComponentTemplate = SelectedNodePtr->GetComponentTemplate();
		if (ComponentTemplate)
		{
			ComponentsToCopy.Add(ComponentTemplate);
		}
	}

	// Copy the components to the clipboard
	return FComponentEditorUtils::CanCopyComponents(ComponentsToCopy);
}

void SSCSEditor::CopySelectedNodes()
{
	// Distill the selected nodes into a list of components to copy
	TArray<UActorComponent*> ComponentsToCopy;
	TArray<FSCSEditorTreeNodePtrType> SelectedNodes = GetSelectedNodes();
	for (int32 i = 0; i < SelectedNodes.Num(); ++i)
	{
		// Get the current selected node reference
		FSCSEditorTreeNodePtrType SelectedNodePtr = SelectedNodes[i];
		check(SelectedNodePtr.IsValid());

		// Get the component template associated with the selected node
		UActorComponent* ComponentTemplate = SelectedNodePtr->GetComponentTemplate();
		if (ComponentTemplate)
		{
			ComponentsToCopy.Add(ComponentTemplate);
		}
	}

	// Copy the components to the clipboard
	FComponentEditorUtils::CopyComponents(ComponentsToCopy);
}

bool SSCSEditor::CanPasteNodes() const
{
	if(!IsEditingAllowed())
	{
		return false;
	}

	return FComponentEditorUtils::CanPasteComponents(Cast<USceneComponent>(SceneRootNodePtr->GetComponentTemplate()), SceneRootNodePtr->IsDefaultSceneRoot());
}

void SSCSEditor::PasteNodes()
{
	const FScopedTransaction Transaction(LOCTEXT("PasteComponents", "Paste Component(s)"));

	if (EditorMode == EComponentEditorMode::BlueprintSCS)
	{
		// Get the components to paste from the clipboard
		TMap<FName, FName> ParentMap;
		TMap<FName, UActorComponent*> NewObjectMap;
		FComponentEditorUtils::GetComponentsFromClipboard(ParentMap, NewObjectMap, true);
		
		// Clear the current selection
		SCSTreeWidget->ClearSelection();

		// Get the blueprint that's being edited
		UBlueprint* Blueprint = GetBlueprint();
		check(Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr);

		Blueprint->Modify();
		SaveSCSCurrentState(Blueprint->SimpleConstructionScript);

		// Create a new tree node for each new (pasted) component
		TMap<FName, FSCSEditorTreeNodePtrType> NewNodeMap;
		for (auto NewObjectIt = NewObjectMap.CreateIterator(); NewObjectIt; ++NewObjectIt)
		{
			// Get the component object instance
			UActorComponent* NewActorComponent = NewObjectIt->Value;
			check(NewActorComponent);

			// Relocate the instance from the transient package to the BPGC and assign it a unique object name
			NewActorComponent->Rename(NULL, Blueprint->GeneratedClass, REN_DontCreateRedirectors | REN_DoNotDirty);

			// Create a new SCS node to contain the new component and add it to the tree
			NewActorComponent = AddNewNode(Blueprint->SimpleConstructionScript->CreateNode(NewActorComponent), NULL, false, false);

			if (NewActorComponent)
			{
				// Locate the node that corresponds to the new component template or instance
				FSCSEditorTreeNodePtrType NewNodePtr = FindTreeNode(NewActorComponent);
				if (NewNodePtr.IsValid())
				{
					// Add the new node to the node map
					NewNodeMap.Add(NewObjectIt->Key, NewNodePtr);

					// Update the selection to include the new node
					SCSTreeWidget->SetItemSelection(NewNodePtr, true);
				}
			}
		}

		// Restore the node hierarchy from the original copy
		for (auto NodeIt = NewNodeMap.CreateConstIterator(); NodeIt; ++NodeIt)
		{
			// If an entry exists in the set of known parent nodes for the current node
			if (ParentMap.Contains(NodeIt->Key))
			{
				// Get the parent node name
				FName ParentName = ParentMap[NodeIt->Key];
				if (NewNodeMap.Contains(ParentName))
				{
					// Reattach the current node to the parent node (this will also handle detachment from the scene root node)
					NewNodeMap[ParentName]->AddChild(NodeIt->Value);

					// Ensure that the new node is expanded to show the child node(s)
					SCSTreeWidget->SetItemExpansion(NewNodeMap[ParentName], true);
				}
			}
		}

		// Modify the Blueprint generated class structure (this will also call UpdateTree() as a result)
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else    // EComponentEditorMode::ActorInstance
	{
		// Determine where in the hierarchy to paste
		USceneComponent* TargetComponent = nullptr;
		auto SelectedNodes = GetSelectedNodes();
		for (int32 i = 0; i < SelectedNodes.Num(); ++i)
		{
			FSCSEditorTreeNodePtrType SelectedNodePtr = SelectedNodes[i];
			check(SelectedNodePtr.IsValid());

			if (auto SceneComponent = Cast<USceneComponent>(SelectedNodePtr->GetComponentTemplate()))
			{
				TargetComponent = SceneComponent;
				break;
			}
		}

		// Paste the components
		TArray<UActorComponent*> PastedComponents;
		FComponentEditorUtils::PasteComponents(PastedComponents, GetActorContext(), TargetComponent);

		if (PastedComponents.Num() > 0)
		{
			// We only want the pasted node(s) to be selected
			SCSTreeWidget->ClearSelection();
			UpdateTree();

			// Select the nodes that correspond to the pasted components
			for (auto PastedComponent : PastedComponents)
			{
				FSCSEditorTreeNodePtrType PastedNode = GetNodeFromActorComponent(PastedComponent);
				if (PastedNode.IsValid())
				{
					SCSTreeWidget->SetItemSelection(PastedNode, true);
				}
			}
		}
	}
}

bool SSCSEditor::CanDeleteNodes() const
{
	if(!IsEditingAllowed())
	{
		return false;
	}

	TArray<FSCSEditorTreeNodePtrType> SelectedNodes = SCSTreeWidget->GetSelectedItems();
	for (int32 i = 0; i < SelectedNodes.Num(); ++i)
	{
		if (!SelectedNodes[i]->CanDelete()) {return false;}
	}
	return SelectedNodes.Num() > 0;
}

void SSCSEditor::OnDeleteNodes()
{
	const FScopedTransaction Transaction( LOCTEXT("RemoveComponents", "Remove Components") );

	if (EditorMode == EComponentEditorMode::BlueprintSCS)
	{
		// Remove node from SCS
		UBlueprint* Blueprint = GetBlueprint();
		FThumbnailRenderingInfo* RenderInfo = nullptr;
		TArray<FSCSEditorTreeNodePtrType> SelectedNodes = SCSTreeWidget->GetSelectedItems();
		for (int32 i = 0; i < SelectedNodes.Num(); ++i)
		{
			auto Node = SelectedNodes[i];

			USCS_Node* SCS_Node = Node->GetSCSNode();
			if(SCS_Node != nullptr)
			{
				USimpleConstructionScript* SCS = SCS_Node->GetSCS();
				check(SCS != nullptr && Blueprint == SCS->GetBlueprint());

				if(Blueprint == nullptr)
				{
					Blueprint = SCS->GetBlueprint();
					check(Blueprint != nullptr);

					// Get the current render info for the blueprint. If this is NULL then the blueprint is not currently visualizable (no visible primitive components)
					FThumbnailRenderingInfo* RenderInfo = GUnrealEd->GetThumbnailManager()->GetRenderingInfo( Blueprint );

					// Saving objects for restoring purpose.
					Blueprint->Modify();
					SaveSCSCurrentState( SCS );
				}
			}

			RemoveComponentNode(Node);
		}

		// Will call UpdateTree as part of OnBlueprintChanged handling
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

		// If we had a thumbnail before we deleted any components, check to see if we should clear it
		// If we deleted the final visualizable primitive from the blueprint, GetRenderingInfo should return NULL
		FThumbnailRenderingInfo* NewRenderInfo = GUnrealEd->GetThumbnailManager()->GetRenderingInfo( Blueprint );
		if ( RenderInfo && !NewRenderInfo )
		{
			// We removed the last visible primitive component, clear the thumbnail
			const FString BPFullName = FString::Printf(TEXT("%s %s"), *Blueprint->GetClass()->GetName(), *Blueprint->GetPathName());
			UPackage* BPPackage = Blueprint->GetOutermost();
			ThumbnailTools::CacheEmptyThumbnail( BPFullName, BPPackage );
		}
	}
	else    // EComponentEditorMode::ActorInstance
	{
		if (AActor* ActorInstance = GetActorContext())
		{
			ActorInstance->Modify();
		}

		TArray<UActorComponent*> ComponentsToDelete;
		TArray<FSCSEditorTreeNodePtrType> SelectedNodes = GetSelectedNodes();
		for (int32 i = 0; i < SelectedNodes.Num(); ++i)
		{
			// Get the current selected node reference
			FSCSEditorTreeNodePtrType SelectedNodePtr = SelectedNodes[i];
			check(SelectedNodePtr.IsValid());

			// Get the component template associated with the selected node
			UActorComponent* ComponentTemplate = SelectedNodePtr->GetComponentTemplate();
			if (ComponentTemplate)
			{
				ComponentsToDelete.Add(ComponentTemplate);
			}
		}

		UActorComponent* ComponentToSelect = nullptr;
		int32 NumDeletedComponents = FComponentEditorUtils::DeleteComponents(ComponentsToDelete, ComponentToSelect);
		if (NumDeletedComponents > 0)
		{
			if (ComponentToSelect)
			{
				FSCSEditorTreeNodePtrType NodeToSelect = GetNodeFromActorComponent(ComponentToSelect);
				if (NodeToSelect.IsValid())
				{
					SCSTreeWidget->SetSelection(NodeToSelect);
				}
			}

			// Rebuild the tree view to reflect the new component hierarchy
			UpdateTree();
		}
	}

	// Do this AFTER marking the Blueprint as modified
	UpdateSelectionFromNodes(SCSTreeWidget->GetSelectedItems());
}

void SSCSEditor::RemoveComponentNode(FSCSEditorTreeNodePtrType InNodePtr)
{
	check(InNodePtr.IsValid());

	if (EditorMode == EComponentEditorMode::BlueprintSCS)
	{
		USCS_Node* SCS_Node = InNodePtr->GetSCSNode();
		if(SCS_Node != NULL)
		{
			// Clear selection if current
			if (SCSTreeWidget->GetSelectedItems().Contains(InNodePtr))
			{
				SCSTreeWidget->ClearSelection();
			}

			USimpleConstructionScript* SCS = SCS_Node->GetSCS();
			check(SCS != nullptr);

			// Remove any instances of variable accessors from the blueprint graphs
			UBlueprint* Blueprint = SCS->GetBlueprint();
			if(Blueprint != nullptr)
			{
				FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, InNodePtr->GetVariableName());
			}

			// Remove node from SCS tree
			SCS->RemoveNodeAndPromoteChildren(SCS_Node);

			// Clear the delegate
			SCS_Node->SetOnNameChanged(FSCSNodeNameChanged());
		}
	}
	else    // EComponentEditorMode::ActorInstance
	{
		AActor* ActorInstance = GetActorContext();

		UActorComponent* ComponentInstance = InNodePtr->GetComponentTemplate();
		if ((ActorInstance != nullptr) && (ComponentInstance != nullptr))
		{
			// Clear selection if current
			if (SCSTreeWidget->GetSelectedItems().Contains(InNodePtr))
			{
				SCSTreeWidget->ClearSelection();
			}

			// Destroy the component instance
			ComponentInstance->Modify();
			ComponentInstance->DestroyComponent(true);
		}
	}
}

void SSCSEditor::UpdateSelectionFromNodes(const TArray<FSCSEditorTreeNodePtrType> &SelectedNodes)
{
	bUpdatingSelection = true;

	// Notify that the selection has updated
	OnSelectionUpdated.ExecuteIfBound(SelectedNodes);

	bUpdatingSelection = false;
}

void SSCSEditor::RefreshSelectionDetails()
{
	UpdateSelectionFromNodes(SCSTreeWidget->GetSelectedItems());
}

void SSCSEditor::OnTreeSelectionChanged(FSCSEditorTreeNodePtrType, ESelectInfo::Type /*SelectInfo*/)
{
	UpdateSelectionFromNodes(SCSTreeWidget->GetSelectedItems());
}

bool SSCSEditor::IsNodeInSimpleConstructionScript( USCS_Node* Node ) const
{
	check(Node);

	USimpleConstructionScript* NodeSCS = Node->GetSCS();
	if(NodeSCS != NULL)
	{
		return NodeSCS->GetAllNodes().Contains(Node);
	}
	
	return false;
}

FSCSEditorTreeNodePtrType SSCSEditor::AddTreeNode(USCS_Node* InSCSNode, FSCSEditorTreeNodePtrType InParentNodePtr, const bool bIsInheritedSCS)
{
	FSCSEditorTreeNodePtrType NewNodePtr;

	check(InSCSNode != NULL);
	check(InSCSNode->ComponentTemplate != NULL);
	checkf(InSCSNode->ParentComponentOrVariableName == NAME_None
		|| (!InSCSNode->bIsParentComponentNative && InParentNodePtr->GetSCSNode() != NULL && InParentNodePtr->GetSCSNode()->VariableName == InSCSNode->ParentComponentOrVariableName)
		|| (InSCSNode->bIsParentComponentNative && InParentNodePtr->GetComponentTemplate() != NULL && InParentNodePtr->GetComponentTemplate()->GetFName() == InSCSNode->ParentComponentOrVariableName),
			TEXT("Failed to add SCS node %s to tree:\n- bIsParentComponentNative=%d\n- Stored ParentComponentOrVariableName=%s\n- Actual ParentComponentOrVariableName=%s"),
				*InSCSNode->VariableName.ToString(),
				!!InSCSNode->bIsParentComponentNative,
				*InSCSNode->ParentComponentOrVariableName.ToString(),
				!InSCSNode->bIsParentComponentNative
					? (InParentNodePtr->GetSCSNode() != NULL ? *InParentNodePtr->GetSCSNode()->VariableName.ToString() : TEXT("NULL"))
					: (InParentNodePtr->GetComponentTemplate() != NULL ? *InParentNodePtr->GetComponentTemplate()->GetFName().ToString() : TEXT("NULL")));
	
	// Determine whether or not the given node is inherited from a parent Blueprint
	USimpleConstructionScript* NodeSCS = InSCSNode->GetSCS();

	if(InSCSNode->ComponentTemplate->IsA(USceneComponent::StaticClass()))
	{
		FSCSEditorTreeNodePtrType ParentPtr = InParentNodePtr.IsValid() ? InParentNodePtr : SceneRootNodePtr;
		if(ParentPtr.IsValid())
		{
			// do this first, because we need a FSCSEditorTreeNodePtrType for the new node
			NewNodePtr = ParentPtr->AddChild(InSCSNode, bIsInheritedSCS);

			bool bParentIsEditorOnly = ParentPtr->GetComponentTemplate()->IsEditorOnly();
			// if you can't nest this new node under the proposed parent (then swap the two)
			if (bParentIsEditorOnly && !InSCSNode->ComponentTemplate->IsEditorOnly() && ParentPtr->CanReparent())
			{
				FSCSEditorTreeNodePtrType OldParentPtr = ParentPtr;
				ParentPtr = OldParentPtr->GetParent();

				OldParentPtr->RemoveChild(NewNodePtr);
				NodeSCS->RemoveNode(OldParentPtr->GetSCSNode());

				// if the grandparent node is invalid (assuming this means that the parent node was the scene-root)
				if (!ParentPtr.IsValid())
				{
					check(OldParentPtr == SceneRootNodePtr);
					SceneRootNodePtr = NewNodePtr;
					NodeSCS->AddNode(SceneRootNodePtr->GetSCSNode());
				}
				else 
				{
					ParentPtr->AddChild(NewNodePtr);
				}

				// move the proposed parent in as a child to the new node
				NewNodePtr->AddChild(OldParentPtr);
			} // if bParentIsEditorOnly...

			// Expand parent nodes by default
			SCSTreeWidget->SetItemExpansion(ParentPtr, true);
		}
		//else, if !SceneRootNodePtr.IsValid(), make it the scene root node if it has not been set yet
		else 
		{
			// Create a new root node
			if (RootTreeNode.IsValid())
			{
				NewNodePtr = RootTreeNode->AddChild(InSCSNode, bIsInheritedSCS);
			}
			else
			{
				NewNodePtr = MakeShareable(new FSCSEditorTreeNodeComponent(InSCSNode, bIsInheritedSCS));
				RootNodes.Add(NewNodePtr);
			}
			
			NodeSCS->AddNode(InSCSNode);
			
			// Add it to the root set
			RootComponentNodes.Insert(NewNodePtr, 0);

			// Make it the scene root node
			SceneRootNodePtr = NewNodePtr;

			// Expand the scene root node by default
			SCSTreeWidget->SetItemExpansion(SceneRootNodePtr, true);
		}
	}
	else
	{
		// If the given SCS node does not contain a scene component template, we create a new root node
		if (RootTreeNode.IsValid())
		{
			NewNodePtr = RootTreeNode->AddChild(InSCSNode, bIsInheritedSCS);
		}
		else
		{
			NewNodePtr = MakeShareable(new FSCSEditorTreeNodeComponent(InSCSNode, bIsInheritedSCS));
			RootNodes.Add(NewNodePtr);
		}

		RootComponentNodes.Add(NewNodePtr);

		// If the SCS root node array does not already contain the given node, this will add it (this should only occur after node creation)
		if(NodeSCS != NULL)
		{
			NodeSCS->AddNode(InSCSNode);
		}
	}

	// Recursively add the given SCS node's child nodes
	for(int32 NodeIndex = 0; NodeIndex < InSCSNode->ChildNodes.Num(); ++NodeIndex)
	{
		AddTreeNode(InSCSNode->ChildNodes[NodeIndex], NewNodePtr, bIsInheritedSCS);
	}

	return NewNodePtr;
}

FSCSEditorTreeNodePtrType SSCSEditor::AddTreeNodeFromComponent(USceneComponent* InSceneComponent)
{
	FSCSEditorTreeNodePtrType NewNodePtr;

	check(InSceneComponent != NULL);
	ensure(!InSceneComponent->HasAnyFlags(RF_PendingKill));

	// If the given component has a parent, and if we're not in "instance" mode OR the owner of the parent matches the Actor instance we're editing
	if(InSceneComponent->AttachParent != NULL
		&& (EditorMode != EComponentEditorMode::ActorInstance || InSceneComponent->AttachParent->GetOwner() == GetActorContext()))
	{
		// Attempt to find the parent node in the current tree
		FSCSEditorTreeNodePtrType ParentNodePtr = FindTreeNode(InSceneComponent->AttachParent);
		if(!ParentNodePtr.IsValid())
		{
			// Recursively add the parent node to the tree if it does not exist yet
			ParentNodePtr = AddTreeNodeFromComponent(InSceneComponent->AttachParent);
		}

		// Add a new tree node for the given scene component
		check(ParentNodePtr.IsValid());
		NewNodePtr = ParentNodePtr->AddChildFromComponent(InSceneComponent);

		// Expand parent nodes by default
		SCSTreeWidget->SetItemExpansion(ParentNodePtr, true);
	}
	else
	{
		// Make it the scene root node if it has not been set yet
		if(!SceneRootNodePtr.IsValid())
		{
			// Create a new root node
			NewNodePtr = AddRootComponentTreeNode(InSceneComponent);

			// Make it the scene root node
			SceneRootNodePtr = NewNodePtr;

			// Expand the scene root node by default
			SCSTreeWidget->SetItemExpansion(SceneRootNodePtr, true);
		}
		else if (SceneRootNodePtr->GetComponentTemplate() != InSceneComponent)
		{
			NewNodePtr = SceneRootNodePtr->AddChildFromComponent(InSceneComponent);
		}
	}

	return NewNodePtr;
}

FSCSEditorTreeNodePtrType SSCSEditor::FindTreeNode(const USCS_Node* InSCSNode, FSCSEditorTreeNodePtrType InStartNodePtr) const
{
	FSCSEditorTreeNodePtrType NodePtr;
	if(InSCSNode != NULL)
	{
		// Start at the scene root node if none was given
		if(!InStartNodePtr.IsValid())
		{
			InStartNodePtr = SceneRootNodePtr;
		}

		if(InStartNodePtr.IsValid())
		{
			// Check to see if the given SCS node matches the given tree node
			if(InStartNodePtr->GetSCSNode() == InSCSNode)
			{
				NodePtr = InStartNodePtr;
			}
			else
			{
				// Recursively search for the node in our child set
				NodePtr = InStartNodePtr->FindChild(InSCSNode);
				if(!NodePtr.IsValid())
				{
					for(int32 i = 0; i < InStartNodePtr->GetChildren().Num() && !NodePtr.IsValid(); ++i)
					{
						NodePtr = FindTreeNode(InSCSNode, InStartNodePtr->GetChildren()[i]);
					}
				}
			}
		}
	}

	return NodePtr;
}

FSCSEditorTreeNodePtrType SSCSEditor::FindTreeNode(const UActorComponent* InComponent, FSCSEditorTreeNodePtrType InStartNodePtr) const
{
	FSCSEditorTreeNodePtrType NodePtr;
	if(InComponent != NULL)
	{
		// Start at the scene root node if none was given
		if(!InStartNodePtr.IsValid())
		{
			InStartNodePtr = SceneRootNodePtr;
		}

		if(InStartNodePtr.IsValid())
		{
			// Check to see if the given component template matches the given tree node
			if(InStartNodePtr->GetComponentTemplate() == InComponent)
			{
				NodePtr = InStartNodePtr;
			}
			else
			{
				// Recursively search for the node in our child set
				NodePtr = InStartNodePtr->FindChild(InComponent);
				if(!NodePtr.IsValid())
				{
					for(int32 i = 0; i < InStartNodePtr->GetChildren().Num() && !NodePtr.IsValid(); ++i)
					{
						NodePtr = FindTreeNode(InComponent, InStartNodePtr->GetChildren()[i]);
					}
				}
			}
		}
	}

	return NodePtr;
}

FSCSEditorTreeNodePtrType SSCSEditor::FindTreeNode(const FName& InVariableOrInstanceName, FSCSEditorTreeNodePtrType InStartNodePtr) const
{
	FSCSEditorTreeNodePtrType NodePtr;
	if(InVariableOrInstanceName != NAME_None)
	{
		// Start at the scene root node if none was given
		if(!InStartNodePtr.IsValid())
		{
			InStartNodePtr = SceneRootNodePtr;
		}

		if(InStartNodePtr.IsValid())
		{
			FName ItemName = InStartNodePtr->GetNodeID();

			// Check to see if the given name matches the item name
			if(InVariableOrInstanceName == ItemName)
			{
				NodePtr = InStartNodePtr;
			}
			else
			{
				// Recursively search for the node in our child set
				NodePtr = InStartNodePtr->FindChild(InVariableOrInstanceName);
				if(!NodePtr.IsValid())
				{
					for(int32 i = 0; i < InStartNodePtr->GetChildren().Num() && !NodePtr.IsValid(); ++i)
					{
						NodePtr = FindTreeNode(InVariableOrInstanceName, InStartNodePtr->GetChildren()[i]);
					}
				}
			}
		}
	}

	return NodePtr;
}

void SSCSEditor::OnItemScrolledIntoView( FSCSEditorTreeNodePtrType InItem, const TSharedPtr<ITableRow>& InWidget)
{
	if(DeferredRenameRequest != NAME_None)
	{
		FName ItemName = InItem->GetNodeID();
		if(DeferredRenameRequest == ItemName)
		{
			DeferredRenameRequest = NAME_None;
			InItem->OnRequestRename(bIsDeferredRenameRequestTransactional);
		}
	}
}

void SSCSEditor::HandleItemDoubleClicked(FSCSEditorTreeNodePtrType InItem)
{
	// Notify that the selection has updated
	OnItemDoubleClicked.ExecuteIfBound(InItem);
}

void SSCSEditor::OnRenameComponent(bool bTransactional)
{
	TArray< FSCSEditorTreeNodePtrType > SelectedItems = SCSTreeWidget->GetSelectedItems();

	// Should already be prevented from making it here.
	check(SelectedItems.Num() == 1);

	DeferredRenameRequest = SelectedItems[0]->GetNodeID();
	bIsDeferredRenameRequestTransactional = bTransactional;

	SCSTreeWidget->RequestScrollIntoView(SelectedItems[0]);
}

bool SSCSEditor::CanRenameComponent() const
{
	return IsEditingAllowed() && SCSTreeWidget->GetSelectedItems().Num() == 1 && SCSTreeWidget->GetSelectedItems()[0]->CanRename();
}

void SSCSEditor::GetCollapsedNodes(const FSCSEditorTreeNodePtrType& InNodePtr, TSet<FSCSEditorTreeNodePtrType>& OutCollapsedNodes) const
{
	if(InNodePtr.IsValid())
	{
		const TArray<FSCSEditorTreeNodePtrType>& Children = InNodePtr->GetChildren();
		if(Children.Num() > 0)
		{
			if(!SCSTreeWidget->IsItemExpanded(InNodePtr))
			{
				OutCollapsedNodes.Add(InNodePtr);
			}

			for(int32 i = 0; i < Children.Num(); ++i)
			{
				GetCollapsedNodes(Children[i], OutCollapsedNodes);
			}
		}
	}
}

EVisibility SSCSEditor::GetPromoteToBlueprintButtonVisibility() const
{
	EVisibility Visibility = EVisibility::Collapsed;
	if (EditorMode == EComponentEditorMode::ActorInstance)
	{
		if (GetBlueprint() == nullptr)
		{
			Visibility = EVisibility::Visible;
		}
	}

	return Visibility;
}

EVisibility SSCSEditor::GetEditBlueprintButtonVisibility() const
{
	EVisibility Visibility = EVisibility::Collapsed;
	if (EditorMode == EComponentEditorMode::ActorInstance)
	{
		if (GetBlueprint() != nullptr)
		{
			Visibility = EVisibility::Visible;
		}
	}

	return Visibility;
}

FText SSCSEditor::OnGetApplyChangesToBlueprintTooltip() const
{
	int32 NumChangedProperties = 0;

	AActor* Actor = GetActorContext();
	UBlueprint* Blueprint = (Actor != nullptr) ? Cast<UBlueprint>(Actor->GetClass()->ClassGeneratedBy) : nullptr;

	if(Actor != NULL && Blueprint != NULL && Actor->GetClass()->ClassGeneratedBy == Blueprint)
	{
		AActor* BlueprintCDO = Actor->GetClass()->GetDefaultObject<AActor>();
		if(BlueprintCDO != NULL)
		{
			const auto CopyOptions = (EditorUtilities::ECopyOptions::Type)(EditorUtilities::ECopyOptions::PreviewOnly|EditorUtilities::ECopyOptions::OnlyCopyEditOrInterpProperties);
			NumChangedProperties += EditorUtilities::CopyActorProperties(Actor, BlueprintCDO, CopyOptions);
		}
		NumChangedProperties += Actor->GetInstanceComponents().Num();
	}


	if(NumChangedProperties == 0)
	{
		return LOCTEXT("DisabledPushToBlueprintDefaults_ToolTip", "Replaces the Blueprint's defaults with any altered property values.");
	}
	else if(NumChangedProperties > 1)
	{
		return FText::Format(LOCTEXT("PushToBlueprintDefaults_ToolTip", "Click to apply {0} changed properties to the Blueprint."), FText::AsNumber(NumChangedProperties));
	}
	else
	{
		return LOCTEXT("PushOneToBlueprintDefaults_ToolTip", "Click to apply 1 changed property to the Blueprint.");
	}
}

FText SSCSEditor::OnGetResetToBlueprintDefaultsTooltip() const
{
	int32 NumChangedProperties = 0;

	AActor* Actor = GetActorContext();
	UBlueprint* Blueprint = (Actor != nullptr) ? Cast<UBlueprint>(Actor->GetClass()->ClassGeneratedBy) : nullptr;
	if(Actor != NULL && Blueprint != NULL && Actor->GetClass()->ClassGeneratedBy == Blueprint)
	{
		AActor* BlueprintCDO = Actor->GetClass()->GetDefaultObject<AActor>();
		if(BlueprintCDO != NULL)
		{
			const auto CopyOptions = (EditorUtilities::ECopyOptions::Type)(EditorUtilities::ECopyOptions::PreviewOnly|EditorUtilities::ECopyOptions::OnlyCopyEditOrInterpProperties);
			NumChangedProperties += EditorUtilities::CopyActorProperties(BlueprintCDO, Actor, CopyOptions);
		}
		NumChangedProperties += Actor->GetInstanceComponents().Num();
	}

	if(NumChangedProperties == 0)
	{
		return LOCTEXT("DisabledResetBlueprintDefaults_ToolTip", "Resets altered properties back to their Blueprint default values.");
	}
	else if(NumChangedProperties > 1)
	{
		return FText::Format(LOCTEXT("ResetToBlueprintDefaults_ToolTip", "Click to reset {0} changed properties to their Blueprint default values."), FText::AsNumber(NumChangedProperties));
	}
	else
	{
		return LOCTEXT("ResetOneToBlueprintDefaults_ToolTip", "Click to reset 1 changed property to its Blueprint default value.");
	}
}

void SSCSEditor::OnOpenBlueprintEditor(bool bForceCodeEditing) const
{
	if (AActor* ActorInstance = GetActorContext())
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(ActorInstance->GetClass()->ClassGeneratedBy))
		{
			if (bForceCodeEditing && (Blueprint->UbergraphPages.Num() > 0))
			{
				FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Blueprint->UbergraphPages[0]);
			}
			else
			{
				FAssetEditorManager::Get().OpenEditorForAsset(Blueprint);
			}
		}
	}
}

void SSCSEditor::OnApplyChangesToBlueprint() const
{
	int32 NumChangedProperties = 0;

	AActor* Actor = GetActorContext();
	UBlueprint* Blueprint = (Actor != nullptr) ? Cast<UBlueprint>(Actor->GetClass()->ClassGeneratedBy) : nullptr;

	if (Actor != NULL && Blueprint != NULL && Actor->GetClass()->ClassGeneratedBy == Blueprint)
	{
		const FScopedTransaction Transaction(LOCTEXT("PushToBlueprintDefaults_Transaction", "Apply Changes to Blueprint"));

		// Perform the actual copy
		{
			AActor* BlueprintCDO = Actor->GetClass()->GetDefaultObject<AActor>();
			if (BlueprintCDO != NULL)
			{
				const auto CopyOptions = (EditorUtilities::ECopyOptions::Type)(EditorUtilities::ECopyOptions::OnlyCopyEditOrInterpProperties | EditorUtilities::ECopyOptions::PropagateChangesToArchetypeInstances);
				NumChangedProperties = EditorUtilities::CopyActorProperties(Actor, BlueprintCDO, CopyOptions);
				if (Actor->GetInstanceComponents().Num() > 0)
				{
					FKismetEditorUtilities::AddComponentsToBlueprint(Blueprint, Actor->GetInstanceComponents());
					NumChangedProperties += Actor->GetInstanceComponents().Num();
					Actor->ClearInstanceComponents(true);
				}
				if (NumChangedProperties > 0)
				{
					FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
					FKismetEditorUtilities::CompileBlueprint(Blueprint);
				}
			}
		}

		// Set up a notification record to indicate success/failure
		FNotificationInfo NotificationInfo(FText::GetEmpty());
		NotificationInfo.FadeInDuration = 1.0f;
		NotificationInfo.FadeOutDuration = 2.0f;
		NotificationInfo.bUseLargeFont = false;
		SNotificationItem::ECompletionState CompletionState;
		if (NumChangedProperties > 0)
		{
			if (NumChangedProperties > 1)
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("BlueprintName"), FText::FromName(Blueprint->GetFName()));
				Args.Add(TEXT("NumChangedProperties"), NumChangedProperties);
				Args.Add(TEXT("ActorName"), FText::FromString(Actor->GetActorLabel()));
				NotificationInfo.Text = FText::Format(LOCTEXT("PushToBlueprintDefaults_ApplySuccess", "Updated Blueprint {BlueprintName} ({NumChangedProperties} property changes applied from actor {ActorName})."), Args);
			}
			else
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("BlueprintName"), FText::FromName(Blueprint->GetFName()));
				Args.Add(TEXT("ActorName"), FText::FromString(Actor->GetActorLabel()));
				NotificationInfo.Text = FText::Format(LOCTEXT("PushOneToBlueprintDefaults_ApplySuccess", "Updated Blueprint {BlueprintName} (1 property change applied from actor {ActorName})."), Args);
			}
			CompletionState = SNotificationItem::CS_Success;
		}
		else
		{
			NotificationInfo.Text = LOCTEXT("PushToBlueprintDefaults_ApplyFailed", "No properties were copied");
			CompletionState = SNotificationItem::CS_Fail;
		}

		// Add the notification to the queue
		const auto Notification = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		Notification->SetCompletionState(CompletionState);
	}
}

void SSCSEditor::OnResetToBlueprintDefaults() const
{
	int32 NumChangedProperties = 0;

	AActor* Actor = GetActorContext();
	UBlueprint* Blueprint = (Actor != nullptr) ? Cast<UBlueprint>(Actor->GetClass()->ClassGeneratedBy) : nullptr;

	if ((Actor != NULL) && (Blueprint != NULL) && (Actor->GetClass()->ClassGeneratedBy == Blueprint))
	{
		const FScopedTransaction Transaction(LOCTEXT("ResetToBlueprintDefaults_Transaction", "Reset to Class Defaults"));

		{
			AActor* BlueprintCDO = Actor->GetClass()->GetDefaultObject<AActor>();
			if (BlueprintCDO != NULL)
			{
				const auto CopyOptions = (EditorUtilities::ECopyOptions::Type)(EditorUtilities::ECopyOptions::OnlyCopyEditOrInterpProperties | EditorUtilities::ECopyOptions::CallPostEditChangeProperty);
				NumChangedProperties = EditorUtilities::CopyActorProperties(BlueprintCDO, Actor, CopyOptions);
			}
			NumChangedProperties += Actor->GetInstanceComponents().Num();
			Actor->ClearInstanceComponents(true);
		}

		// Set up a notification record to indicate success/failure
		FNotificationInfo NotificationInfo(FText::GetEmpty());
		NotificationInfo.FadeInDuration = 1.0f;
		NotificationInfo.FadeOutDuration = 2.0f;
		NotificationInfo.bUseLargeFont = false;
		SNotificationItem::ECompletionState CompletionState;
		if (NumChangedProperties > 0)
		{
			if (NumChangedProperties > 1)
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("BlueprintName"), FText::FromName(Blueprint->GetFName()));
				Args.Add(TEXT("NumChangedProperties"), NumChangedProperties);
				Args.Add(TEXT("ActorName"), FText::FromString(Actor->GetActorLabel()));
				NotificationInfo.Text = FText::Format(LOCTEXT("ResetToBlueprintDefaults_ApplySuccess", "Reset {ActorName} ({NumChangedProperties} property changes applied from Blueprint {BlueprintName})."), Args);
			}
			else
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("BlueprintName"), FText::FromName(Blueprint->GetFName()));
				Args.Add(TEXT("ActorName"), FText::FromString(Actor->GetActorLabel()));
				NotificationInfo.Text = FText::Format(LOCTEXT("ResetOneToBlueprintDefaults_ApplySuccess", "Reset {ActorName} (1 property change applied from Blueprint {BlueprintName})."), Args);
			}
			CompletionState = SNotificationItem::CS_Success;
		}
		else
		{
			NotificationInfo.Text = LOCTEXT("ResetToBlueprintDefaults_Failed", "No properties were reset");
			CompletionState = SNotificationItem::CS_Fail;
		}

		// Add the notification to the queue
		const auto Notification = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		Notification->SetCompletionState(CompletionState);
	}
}

void SSCSEditor::PromoteToBlueprint() const
{
	bool bHarvest = false;
	FCreateBlueprintFromActorDialog::OpenDialog(bHarvest, GetActorContext());
}

FReply SSCSEditor::OnPromoteToBlueprintClicked()
{
	PromoteToBlueprint();
	return FReply::Handled();
}

const TArray<FSCSEditorTreeNodePtrType>& SSCSEditor::GetRootComponentNodes()
{
	return RootComponentNodes;
}

/** Returns the Actor context for which we are viewing/editing the SCS.  Can return null.  Should not be cached as it may change from frame to frame. */
AActor* SSCSEditor::GetActorContext() const
{
	return ActorContext.Get(nullptr);
}

#undef LOCTEXT_NAMESPACE

