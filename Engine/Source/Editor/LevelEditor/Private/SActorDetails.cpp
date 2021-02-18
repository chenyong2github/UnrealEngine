// Copyright Epic Games, Inc. All Rights Reserved.

#include "SActorDetails.h"
#include "Widgets/SBoxPanel.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "EditorStyleSet.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Engine/Selection.h"
#include "UnrealEdGlobals.h"
#include "LevelEditor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "SSCSEditor.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "LevelEditorGenericDetails.h"
#include "ScopedTransaction.h"
#include "SourceCodeNavigation.h"
#include "Widgets/Docking/SDockTab.h"
#include "DetailsViewObjectFilter.h"
#include "IDetailRootObjectCustomization.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Framework/EngineElementsLibrary.h"

class SActorDetailsUneditableComponentWarning : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SActorDetailsUneditableComponentWarning)
		: _WarningText()
		, _OnHyperlinkClicked()
	{}
		
		/** The rich text to show in the warning */
		SLATE_ATTRIBUTE(FText, WarningText)

		/** Called when the hyperlink in the rich text is clicked */
		SLATE_EVENT(FSlateHyperlinkRun::FOnClick, OnHyperlinkClicked)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(2)
				[
					SNew(SImage)
					.Image(FEditorStyle::Get().GetBrush("Icons.Warning"))
				]
				+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(2)
					[
						SNew(SRichTextBlock)
						.DecoratorStyleSet(&FEditorStyle::Get())
						.Justification(ETextJustify::Left)
						.TextStyle(FEditorStyle::Get(), "DetailsView.BPMessageTextStyle")
						.Text(InArgs._WarningText)
						.AutoWrapText(true)
						+ SRichTextBlock::HyperlinkDecorator(TEXT("HyperlinkDecorator"), InArgs._OnHyperlinkClicked)
					]
			]
		];
	}
};

void SActorDetails::Construct(const FArguments& InArgs, UTypedElementSelectionSet* InSelectionSet, const FName TabIdentifier, TSharedPtr<FUICommandList> InCommandList, TSharedPtr<FTabManager> InTabManager)
{
	SelectionSet = InSelectionSet;
	checkf(SelectionSet, TEXT("SActorDetails must be constructed with a valid selection set!"));

	FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &SActorDetails::OnObjectsReplaced);

	FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditor.OnComponentsEdited().AddRaw(this, &SActorDetails::OnComponentsEditedInWorld);

	FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = true;
	DetailsViewArgs.bLockable = true;
	DetailsViewArgs.bAllowFavoriteSystem = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ComponentsAndActorsUseNameArea;
	DetailsViewArgs.NotifyHook = GUnrealEd;
	DetailsViewArgs.ViewIdentifier = TabIdentifier;
	DetailsViewArgs.bCustomNameAreaLocation = true;
	DetailsViewArgs.bCustomFilterAreaLocation = true;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;
	DetailsViewArgs.HostCommandList = InCommandList;
	DetailsViewArgs.HostTabManager = InTabManager;
	DetailsView = PropPlugin.CreateDetailView(DetailsViewArgs);

	auto IsPropertyVisible = [](const FPropertyAndParent& PropertyAndParent)
	{
		// For details views in the level editor all properties are the instanced versions
		if(PropertyAndParent.Property.HasAllPropertyFlags(CPF_DisableEditOnInstance))
		{
			return false;
		}

		return true;
	};

	DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda(IsPropertyVisible));
	DetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SActorDetails::IsPropertyReadOnly));
	DetailsView->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateSP(this, &SActorDetails::IsPropertyEditingEnabled));

	// Set up a delegate to call to add generic details to the view
	DetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FLevelEditorGenericDetails::MakeInstance));

	GEditor->RegisterForUndo(this);

	ComponentsBox = SNew(SBox)
		.Padding(FMargin(2,0,2,0))
		.Visibility(this, &SActorDetails::GetComponentsBoxVisibility)
		[
			SAssignNew(SCSEditor, SSCSEditor)
			.EditorMode(EComponentEditorMode::ActorInstance)
			.AllowEditing(this, &SActorDetails::GetAllowComponentTreeEditing)
			.ActorContext(this, &SActorDetails::GetActorContext)
			.OnSelectionUpdated(this, &SActorDetails::OnSCSEditorTreeViewSelectionChanged)
			.OnItemDoubleClicked(this, &SActorDetails::OnSCSEditorTreeViewItemDoubleClicked)
			.OnObjectReplaced(this, &SActorDetails::OnSCSEditorTreeViewObjectReplaced)
		];

	TSharedRef<SWidget> ButtonBox = SCSEditor->GetToolButtonsBox().ToSharedRef();
	DetailsView->SetNameAreaCustomContent( ButtonBox );

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.Padding(10.f, 4.f, 0.f, 0.f)
		.AutoHeight()
		[
			DetailsView->GetNameAreaWidget().ToSharedRef()
		]
		+SVerticalBox::Slot()
		[
			SAssignNew(DetailsSplitter, SSplitter)
			.MinimumSlotHeight(80.0f)
			.Orientation(Orient_Vertical)
			.Style(FEditorStyle::Get(), "SplitterDark")
			.PhysicalSplitterHandleSize(2.0f)
			+ SSplitter::Slot()
			[
				SNew( SVerticalBox )
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding( FMargin(0,0,0,1) )
				[
					SNew(SActorDetailsUneditableComponentWarning)
					.Visibility(this, &SActorDetails::GetUCSComponentWarningVisibility)
					.WarningText(NSLOCTEXT("SActorDetails", "BlueprintUCSComponentWarning", "Components created by the User Construction Script can only be edited in the <a id=\"HyperlinkDecorator\" style=\"DetailsView.BPMessageHyperlinkStyle\">Blueprint</>"))
					.OnHyperlinkClicked(this, &SActorDetails::OnBlueprintedComponentWarningHyperlinkClicked)
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding( FMargin(0,0,0,1) )
				[
					SNew(SActorDetailsUneditableComponentWarning)
					.Visibility(this, &SActorDetails::GetInheritedBlueprintComponentWarningVisibility)
					.WarningText(NSLOCTEXT("SActorDetails", "BlueprintUneditableInheritedComponentWarning", "Components flagged as not editable when inherited must be edited in the <a id=\"HyperlinkDecorator\" style=\"DetailsView.BPMessageHyperlinkStyle\">Blueprint</>"))
					.OnHyperlinkClicked(this, &SActorDetails::OnBlueprintedComponentWarningHyperlinkClicked)
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding( FMargin(0,0,0,1) )
				[
					SNew(SActorDetailsUneditableComponentWarning)
					.Visibility(this, &SActorDetails::GetNativeComponentWarningVisibility)
					.WarningText(NSLOCTEXT("SActorDetails", "UneditableNativeComponentWarning", "Native components are editable when declared as a FProperty in <a id=\"HyperlinkDecorator\" style=\"DetailsView.BPMessageHyperlinkStyle\">C++</>"))
					.OnHyperlinkClicked(this, &SActorDetails::OnNativeComponentWarningHyperlinkClicked)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					DetailsView->GetFilterAreaWidget().ToSharedRef()
				]
				+ SVerticalBox::Slot()
				[
					DetailsView.ToSharedRef()
				]
			]
		]
	];

	DetailsSplitter->AddSlot(0)
	.Value(.2f)
	[
		ComponentsBox.ToSharedRef()
	];

	// Immediately update (otherwise we will appear empty)
	RefreshSelection(/*bForceRefresh*/true);
}

SActorDetails::~SActorDetails()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}

	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);

	RemoveBPComponentCompileEventDelegate();

	FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
	if (LevelEditor != nullptr)
	{
		LevelEditor->OnComponentsEdited().RemoveAll(this);
	}
}

bool SActorDetails::IsObservingSelectionSet(const UTypedElementSelectionSet* InSelectionSet) const
{
	return SelectionSet == InSelectionSet;
}

void SActorDetails::RefreshSelection(const bool bForceRefresh)
{
	if (bSelectionGuard)
	{
		return;
	}

	TArray<TTypedElement<UTypedElementDetailsInterface>> DetailsElements;
	DetailsElements.Reserve(SelectionSet->GetNumSelectedElements());
	SelectionSet->ForEachSelectedElement<UTypedElementDetailsInterface>([&DetailsElements](const TTypedElement<UTypedElementDetailsInterface>& InDetailsElement)
	{
		DetailsElements.Add(InDetailsElement);
		return true;
	});

	bHasSelectionOverride = false;
	SelectionOverrideActors.Reset();

	RefreshTopLevelElements(DetailsElements, bForceRefresh, /*bOverrideLock*/false);
}

void SActorDetails::OverrideSelection(const TArray<AActor*>& InActors, const bool bForceRefresh)
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	TArray<TTypedElement<UTypedElementDetailsInterface>> DetailsElements;
	DetailsElements.Reserve(SelectionSet->GetNumSelectedElements());
	for (AActor* Actor : InActors)
	{
		if (FTypedElementHandle ActorElementHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor))
		{
			if (TTypedElement<UTypedElementDetailsInterface> ActorDetailsHandle = Registry->GetElement<UTypedElementDetailsInterface>(ActorElementHandle))
			{
				DetailsElements.Add(MoveTemp(ActorDetailsHandle));
			}
		}
	}

	bHasSelectionOverride = true;
	SelectionOverrideActors = InActors;

	RefreshTopLevelElements(DetailsElements, bForceRefresh, /*bOverrideLock*/false);
}

void SActorDetails::RefreshTopLevelElements(TArrayView<const TTypedElement<UTypedElementDetailsInterface>> InDetailsElements, const bool bForceRefresh, const bool bOverrideLock)
{
	// Nothing to do if this view is locked!
	if (DetailsView->IsLocked() && !bOverrideLock)
	{
		return;
	}

	// Build the array of top-level elements to edit
	TopLevelElements.Reset(InDetailsElements.Num());
	for (const TTypedElement<UTypedElementDetailsInterface>& DetailsElement : InDetailsElements)
	{
		if (DetailsElement.IsTopLevelElement())
		{
			if (TUniquePtr<ITypedElementDetailsObject> ElementDetailsObject = DetailsElement.GetDetailsObject())
			{
				TopLevelElements.Add(MoveTemp(ElementDetailsObject));
			}
		}
	}

	// Update the underlying details view
	SetElementDetailsObjects(TopLevelElements, bForceRefresh, bOverrideLock);

	// Update the SCS tree if we were asked to edit a single actor
	if (AActor* Actor = GetActorContext())
	{
		// Enable the selection guard to prevent OnTreeSelectionChanged() from altering the editor's component selection
		TGuardValue<bool> SelectionGuard(bSelectionGuard, true);
		SCSEditor->UpdateTree();
		UpdateComponentTreeFromEditorSelection();
	}

	// Draw attention to this tab if needed
	if (TSharedPtr<FTabManager> TabManager = DetailsView->GetHostTabManager())
	{
		TSharedPtr<SDockTab> Tab = TabManager->FindExistingLiveTab(DetailsView->GetIdentifier());
		if (Tab.IsValid() && !Tab->IsForeground())
		{
			Tab->FlashTab();
		}
	}
}

void SActorDetails::RefreshSCSTreeElements(TArrayView<const TSharedPtr<class FSCSEditorTreeNode>> InSelectedNodes, const bool bForceRefresh, const bool bOverrideLock)
{
	// Nothing to do if this view is locked!
	if (DetailsView->IsLocked() && !bOverrideLock)
	{
		return;
	}

	// Does the SCS tree have components selected?
	TArray<UActorComponent*> Components;
	if (AActor* Actor = GetActorContext())
	{
		for (const FSCSEditorTreeNodePtrType& SelectedNode : InSelectedNodes)
		{
			if (SelectedNode)
			{
				if (SelectedNode->GetNodeType() == FSCSEditorTreeNode::RootActorNode)
				{
					// If the actor node is selected then we ignore the component selection
					Components.Reset();
					break;
				}

				if (SelectedNode->GetNodeType() == FSCSEditorTreeNode::ComponentNode)
				{
					if (UActorComponent* Component = SelectedNode->FindComponentInstanceInActor(Actor))
					{
						Components.Add(Component);
					}
				}
			}
		}
	}

	SCSTreeElements.Reset(Components.Num());
	if (Components.Num() > 0)
	{
		UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
		for (UActorComponent* Component : Components)
		{
			if (FTypedElementHandle ComponentElementHandle = UEngineElementsLibrary::AcquireEditorComponentElementHandle(Component))
			{
				if (TTypedElement<UTypedElementDetailsInterface> ComponentDetailsHandle = Registry->GetElement<UTypedElementDetailsInterface>(ComponentElementHandle))
				{
					if (TUniquePtr<ITypedElementDetailsObject> ElementDetailsObject = ComponentDetailsHandle.GetDetailsObject())
					{
						SCSTreeElements.Add(MoveTemp(ElementDetailsObject));
					}
				}
			}
		}

		// Use the component elements
		SetElementDetailsObjects(SCSTreeElements, bForceRefresh, bOverrideLock);
	}
	else
	{
		// Use the top-level elements
		SetElementDetailsObjects(TopLevelElements, bForceRefresh, bOverrideLock);
	}
}

void SActorDetails::SetElementDetailsObjects(TArrayView<const TUniquePtr<ITypedElementDetailsObject>> InElementDetailsObjects, const bool bForceRefresh, const bool bOverrideLock)
{
	TArray<UObject*> DetailsObjects;
	DetailsObjects.Reserve(InElementDetailsObjects.Num());
	for (const TUniquePtr<ITypedElementDetailsObject>& ElementDetailsObject : InElementDetailsObjects)
	{
		if (UObject* DetailsObject = ElementDetailsObject->GetObject())
		{
			DetailsObjects.Add(DetailsObject);
		}
	}
	DetailsView->SetObjects(DetailsObjects, bForceRefresh, bOverrideLock);
}

void SActorDetails::PostUndo(bool bSuccess)
{
	// Enable the selection guard to prevent OnTreeSelectionChanged() from altering the editor's component selection
	TGuardValue<bool> SelectionGuard(bSelectionGuard, true);
	
	// Refresh the tree and update the selection to match the world
	SCSEditor->UpdateTree();
	UpdateComponentTreeFromEditorSelection();
}

void SActorDetails::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

void SActorDetails::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (const TUniquePtr<ITypedElementDetailsObject>& TopLevelElement : TopLevelElements)
	{
		TopLevelElement->AddReferencedObjects(Collector);
	}
}

FString SActorDetails::GetReferencerName() const
{
	return TEXT("SActorDetails");
}

void SActorDetails::SetActorDetailsRootCustomization(TSharedPtr<FDetailsViewObjectFilter> ActorDetailsObjectFilter, TSharedPtr<IDetailRootObjectCustomization> ActorDetailsRootCustomization)
{
	DetailsView->SetObjectFilter(ActorDetailsObjectFilter);
	DetailsView->SetRootObjectCustomizationInstance(ActorDetailsRootCustomization);
	DetailsView->ForceRefresh();
}

void SActorDetails::SetSCSEditorUICustomization(TSharedPtr<ISCSEditorUICustomization> ActorDetailsSCSEditorUICustomization)
{
	if (SCSEditor.IsValid())
	{
		SCSEditor->SetUICustomization(ActorDetailsSCSEditorUICustomization);
	}
}

void SActorDetails::OnComponentsEditedInWorld()
{
	if (AActor* Actor = GetActorContext())
	{
		if (SelectionSet->IsElementSelected(UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor), FTypedElementIsSelectedOptions()))
		{
			// The component composition of the observed actor has changed, so rebuild the node tree
			TGuardValue<bool> SelectionGuard(bSelectionGuard, true);

			// Refresh the tree and update the selection to match the world
			SCSEditor->UpdateTree();

			DetailsView->ForceRefresh();
		}
	}
}

bool SActorDetails::GetAllowComponentTreeEditing() const
{
	return GEditor->PlayWorld == nullptr;
}

AActor* SActorDetails::GetActorContext() const
{
	return TopLevelElements.Num() == 1
		? Cast<AActor>(TopLevelElements[0]->GetObject())
		: nullptr;
}

void SActorDetails::OnSCSEditorTreeViewSelectionChanged(const TArray<FSCSEditorTreeNodePtrType>& SelectedNodes)
{
	if (bSelectionGuard)
	{
		// Preventing selection changes from having an effect...
		return;
	}

	if (SelectedNodes.Num() == 0)
	{
		// Don't respond to de-selecting everything...
		return;
	}
	
	AActor* Actor = GetActorContext();
	if (!Actor)
	{
		// The SCS editor requires an actor context...
		return;
	}

	if (SelectedNodes.Num() > 1 && SelectedBPComponentBlueprint.IsValid())
	{
		// Remove the compilation delegate if we are no longer displaying the full details for a single blueprint component.
		RemoveBPComponentCompileEventDelegate();
	}
	else if (SelectedNodes.Num() == 1 && SelectedNodes[0]->GetNodeType() == FSCSEditorTreeNode::ComponentNode)
	{
		// Add delegate to monitor blueprint component compilation if we have a full details view ( i.e. single selection )
		if (UActorComponent* Component = SelectedNodes[0]->FindComponentInstanceInActor(Actor))
		{
			if (UBlueprintGeneratedClass* ComponentBPGC = Cast<UBlueprintGeneratedClass>(Component->GetClass()))
			{
				if (UBlueprint* ComponentBlueprint = Cast<UBlueprint>(ComponentBPGC->ClassGeneratedBy))
				{
					AddBPComponentCompileEventDelegate(ComponentBlueprint);
				}
			}
		}
	}

	// We only actually update the editor selection state if we're not locked
	if (!DetailsView->IsLocked())
	{
		TArray<FTypedElementHandle> NewEditorSelection;
		NewEditorSelection.Add(UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor));

		for (const FSCSEditorTreeNodePtrType& SelectedNode : SelectedNodes)
		{
			if (SelectedNode)
			{
				if (SelectedNode->GetNodeType() == FSCSEditorTreeNode::RootActorNode)
				{
					// If the actor node is selected then we ignore the component selection
					NewEditorSelection.Reset();
					NewEditorSelection.Add(UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor));
					break;
				}

				if (SelectedNode->GetNodeType() == FSCSEditorTreeNode::ComponentNode)
				{
					if (UActorComponent* Component = SelectedNode->FindComponentInstanceInActor(Actor))
					{
						NewEditorSelection.Add(UEngineElementsLibrary::AcquireEditorComponentElementHandle(Component));
					}
				}
			}
		}

		// Note: this transaction should not take place if we are in the middle of executing an undo or redo because it would clear the top of the transaction stack.
		const bool bShouldActuallyTransact = !GIsTransacting;
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "ClickingOnComponentInTree", "Clicking on Component (tree view)"), bShouldActuallyTransact);

		// Enable the selection guard to prevent OnEditorSelectionChanged() from altering the contents of the SCSTreeWidget
		TGuardValue<bool> SelectionGuard(bSelectionGuard, true);
		SelectionSet->SetSelection(NewEditorSelection, FTypedElementSelectionOptions());
		SelectionSet->NotifyPendingChanges(); // Fire while still under the selection guard
	}

	// Update the underlying details view
	RefreshSCSTreeElements(SelectedNodes, /*bForceRefresh*/false, DetailsView->IsLocked());
}

void SActorDetails::OnSCSEditorTreeViewItemDoubleClicked(const TSharedPtr<class FSCSEditorTreeNode> ClickedNode)
{
	if (ClickedNode && ClickedNode->GetNodeType() == FSCSEditorTreeNode::ComponentNode)
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(ClickedNode->GetComponentTemplate()))
		{
			const bool bActiveViewportOnly = false;
			GEditor->MoveViewportCamerasToComponent(SceneComponent, bActiveViewportOnly);
		}
	}
}

void SActorDetails::OnSCSEditorTreeViewObjectReplaced()
{
	// Enable the selection guard to prevent OnTreeSelectionChanged() from altering the editor's component selection
	TGuardValue<bool> SelectionGuard(bSelectionGuard, true);
	SCSEditor->UpdateTree();
}

void SActorDetails::UpdateComponentTreeFromEditorSelection()
{
	if (DetailsView->IsLocked())
	{
		return;
	}

	// Enable the selection guard to prevent OnTreeSelectionChanged() from altering the editor's component selection
	TGuardValue<bool> SelectionGuard(bSelectionGuard, true);

	TSharedPtr<SSCSTreeType>& SCSTreeWidget = SCSEditor->SCSTreeWidget;

	// Update the tree selection to match the level editor component selection
	SCSTreeWidget->ClearSelection();
	SelectionSet->ForEachSelectedObject<UActorComponent>([this, &SCSTreeWidget](UActorComponent* InComponent)
	{
		FSCSEditorTreeNodePtrType SCSTreeNode = SCSEditor->GetNodeFromActorComponent(InComponent, false);
		if (SCSTreeNode && SCSTreeNode->GetComponentTemplate())
		{
			SCSTreeWidget->RequestScrollIntoView(SCSTreeNode);
			SCSTreeWidget->SetItemSelection(SCSTreeNode, true);
			check(InComponent == SCSTreeNode->GetComponentTemplate());
		}
		return true;
	});

	TArray<TSharedPtr<FSCSEditorTreeNode>> SelectedNodes = SCSEditor->GetSelectedNodes();
	if (SelectedNodes.Num() == 0)
	{
		SCSEditor->SelectRoot();
		SelectedNodes = SCSEditor->GetSelectedNodes();
	}

	// Update the underlying details view
	RefreshSCSTreeElements(SelectedNodes, bSelectedComponentRecompiled, /*bOverrideLock*/false);
}

bool SActorDetails::IsPropertyReadOnly(const FPropertyAndParent& PropertyAndParent) const
{
	bool bIsReadOnly = false;
	for (const FSCSEditorTreeNodePtrType& Node : SCSEditor->GetSelectedNodes())
	{
		UActorComponent* Component = Node->GetComponentTemplate();
		if (Component && Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript)
		{
			TSet<const FProperty*> UCSModifiedProperties;
			Component->GetUCSModifiedProperties(UCSModifiedProperties);
			if (UCSModifiedProperties.Contains(&PropertyAndParent.Property) || 
				(PropertyAndParent.ParentProperties.Num() > 0 && UCSModifiedProperties.Contains(PropertyAndParent.ParentProperties[0])))
			{
				bIsReadOnly = true;
				break;
			}
		}
	}
	return bIsReadOnly;
}

bool SActorDetails::IsPropertyEditingEnabled() const
{
	FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	if (!LevelEditor.AreObjectsEditable(DetailsView->GetSelectedObjects()))
	{
		return false;
	}

	bool bIsEditable = true;
	for (const FSCSEditorTreeNodePtrType& Node : SCSEditor->GetSelectedNodes())
	{
		bIsEditable = Node->CanEdit();
		if (!bIsEditable)
		{
			break;
		}
	}
	return bIsEditable;
}

void SActorDetails::OnBlueprintedComponentWarningHyperlinkClicked(const FSlateHyperlinkRun::FMetadata& Metadata)
{
	UBlueprint* Blueprint = SCSEditor->GetBlueprint();
	if (Blueprint)
	{
		// Open the blueprint
		GEditor->EditObject(Blueprint);
	}
}

void SActorDetails::OnNativeComponentWarningHyperlinkClicked(const FSlateHyperlinkRun::FMetadata& Metadata)
{
	// Find the closest native parent
	UBlueprint* Blueprint = SCSEditor->GetBlueprint();
	UClass* ParentClass = Blueprint ? *Blueprint->ParentClass : GetActorContext()->GetClass();
	while (ParentClass && !ParentClass->HasAllClassFlags(CLASS_Native))
	{
		ParentClass = ParentClass->GetSuperClass();
	}

	if (ParentClass)
	{
		FString NativeParentClassHeaderPath;
		const bool bFileFound = FSourceCodeNavigation::FindClassHeaderPath(ParentClass, NativeParentClassHeaderPath)
			&& ( IFileManager::Get().FileSize(*NativeParentClassHeaderPath) != INDEX_NONE );
		if (bFileFound)
		{
			const FString AbsoluteHeaderPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*NativeParentClassHeaderPath);
			FSourceCodeNavigation::OpenSourceFile(AbsoluteHeaderPath);
		}
	}
}

EVisibility SActorDetails::GetComponentsBoxVisibility() const
{
	return GetActorContext() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SActorDetails::GetUCSComponentWarningVisibility() const
{
	bool bIsUneditableBlueprintComponent = false;

	// Check to see if any selected components are inherited from blueprint
	for (const FSCSEditorTreeNodePtrType& Node : SCSEditor->GetSelectedNodes())
	{
		if (!Node->IsNativeComponent())
		{
			UActorComponent* Component = Node->GetComponentTemplate();
			bIsUneditableBlueprintComponent = Component ? Component->CreationMethod == EComponentCreationMethod::UserConstructionScript : false;
			if (bIsUneditableBlueprintComponent)
			{
				break;
			}
		}
	}

	return bIsUneditableBlueprintComponent ? EVisibility::Visible : EVisibility::Collapsed;
}

bool NotEditableSetByBlueprint(UActorComponent* Component)
{
	// Determine if it is locked out from a blueprint or from the native
	UActorComponent* Archetype = CastChecked<UActorComponent>(Component->GetArchetype());
	while (Archetype)
	{
		if (Archetype->GetOuter()->IsA<UBlueprintGeneratedClass>() || Archetype->GetOuter()->GetClass()->HasAllClassFlags(CLASS_CompiledFromBlueprint))
		{
			if (!Archetype->bEditableWhenInherited)
			{
				return true;
			}

			Archetype = CastChecked<UActorComponent>(Archetype->GetArchetype());
		}
		else
		{
			Archetype = nullptr;
		}
	}

	return false;
}

EVisibility SActorDetails::GetInheritedBlueprintComponentWarningVisibility() const
{
	bool bIsUneditableBlueprintComponent = false;

	// Check to see if any selected components are inherited from blueprint
	for (const FSCSEditorTreeNodePtrType& Node : SCSEditor->GetSelectedNodes())
	{
		if (!Node->IsNativeComponent())
		{
			if (UActorComponent* Component = Node->GetComponentTemplate())
			{
				if (!Component->IsEditableWhenInherited() && Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript)
				{
					bIsUneditableBlueprintComponent = true;
					break;
				}
			}
		}
		else if (!Node->CanEdit() && NotEditableSetByBlueprint(Node->GetComponentTemplate()))
		{
			bIsUneditableBlueprintComponent = true;
			break;
		}
	}

	return bIsUneditableBlueprintComponent ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SActorDetails::GetNativeComponentWarningVisibility() const
{
	bool bIsUneditableNative = false;
	for (const FSCSEditorTreeNodePtrType& Node : SCSEditor->GetSelectedNodes())
	{
		// Check to see if the component is native and not editable
		if (Node->IsNativeComponent() && !Node->CanEdit() && !NotEditableSetByBlueprint(Node->GetComponentTemplate()))
		{
			bIsUneditableNative = true;
			break;
		}
	}
	
	return bIsUneditableNative ? EVisibility::Visible : EVisibility::Collapsed;
}

void SActorDetails::AddBPComponentCompileEventDelegate(UBlueprint* ComponentBlueprint)
{
	if(SelectedBPComponentBlueprint.Get() != ComponentBlueprint)
	{
		RemoveBPComponentCompileEventDelegate();
		SelectedBPComponentBlueprint = ComponentBlueprint;
		// Add blueprint component compilation event delegate
		if(!ComponentBlueprint->OnCompiled().IsBoundToObject(this))
		{
			ComponentBlueprint->OnCompiled().AddSP(this, &SActorDetails::OnBlueprintComponentCompiled);
		}
	}
}

void SActorDetails::RemoveBPComponentCompileEventDelegate()
{
	// Remove blueprint component compilation event delegate
	if(SelectedBPComponentBlueprint.IsValid())
	{
		SelectedBPComponentBlueprint.Get()->OnCompiled().RemoveAll(this);
		SelectedBPComponentBlueprint.Reset();
		bSelectedComponentRecompiled = false;
	}
}

void SActorDetails::OnBlueprintComponentCompiled(UBlueprint* ComponentBlueprint)
{
	TGuardValue<bool> SelectedComponentRecompiledGuard(bSelectedComponentRecompiled, true);
	UpdateComponentTreeFromEditorSelection();
}

void SActorDetails::OnObjectsReplaced(const TMap<UObject*, UObject*>& InReplacementObjects)
{
	if (bHasSelectionOverride && SelectionOverrideActors.Num() > 0)
	{
		bool bHasChanges = false;

		for (auto It = SelectionOverrideActors.CreateIterator(); It; ++It)
		{
			AActor*& Actor = *It;

			if (UObject* const* ReplacementObjectPtr = InReplacementObjects.Find(Actor))
			{
				bHasChanges = true;

				AActor* ReplacementActor = Cast<AActor>(*ReplacementObjectPtr);
				if (ReplacementActor)
				{
					Actor = ReplacementActor;
				}
				else
				{
					It.RemoveCurrent();
				}
			}
		}

		if (bHasChanges)
		{
			TArray<AActor*> NewSelection = SelectionOverrideActors;
			OverrideSelection(NewSelection);
		}
	}
}
