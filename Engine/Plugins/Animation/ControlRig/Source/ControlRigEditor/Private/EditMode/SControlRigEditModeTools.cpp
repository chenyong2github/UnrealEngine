// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigEditModeTools.h"
#include "ControlRigControlsProxy.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "ISequencer.h"
#include "PropertyHandle.h"
#include "ControlRig.h"
#include "ControlRigEditModeSettings.h"
#include "IDetailRootObjectCustomization.h"
#include "Modules/ModuleManager.h"
#include "ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "SControlPicker.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Rigs/FKControlRig.h"
#include "SControlRigBaseListWidget.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SControlRigTweenWidget.h"
#include "IControlRigEditorModule.h"
#include "Framework/Docking/TabManager.h"
#include "ControlRigEditorStyle.h"
#include "LevelEditor.h"
#include "EditorModeManager.h"
#include "InteractiveToolManager.h"
#include "EdModeInteractiveToolsContext.h"
#include "ControlRigSpaceChannelEditors.h"
#include "IKeyArea.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "ControlRigRootCustomization"

class FControlRigEditModeGenericDetails : public IDetailCustomization
{
public:
	FControlRigEditModeGenericDetails() = delete;
	FControlRigEditModeGenericDetails(FEditorModeTools* InModeTools) : ModeTools(InModeTools) {}
	
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance(FEditorModeTools* InModeTools)
	{
		return MakeShareable(new FControlRigEditModeGenericDetails(InModeTools));
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailLayout) override
	{
		TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
		DetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);

		TArray<UControlRigControlsProxy*> ProxiesBeingCustomized;
		for (TWeakObjectPtr<UObject> ObjectBeingCustomized : ObjectsBeingCustomized)
		{
			if (UControlRigControlsProxy* Proxy = Cast< UControlRigControlsProxy>(ObjectBeingCustomized.Get()))
			{
				ProxiesBeingCustomized.Add(Proxy);
			}
		}

		if (ProxiesBeingCustomized.Num() == 0)
		{
			return;
		}

		IDetailCategoryBuilder& Category = DetailLayout.EditCategory(TEXT("Control"), LOCTEXT("Channels", "Channels"));

		for (UControlRigControlsProxy* Proxy : ProxiesBeingCustomized)
		{
			FRigControlElement* ControlElement = Proxy->GetControlElement();
			if(ControlElement == nullptr)
			{
				continue;
			}
			
			FName ValuePropertyName = TEXT("Transform");
			if (ControlElement->Settings.ControlType == ERigControlType::Float)
			{
				ValuePropertyName = TEXT("Float");
			}
			else if (ControlElement->Settings.ControlType == ERigControlType::Integer)
			{
				if (ControlElement->Settings.ControlEnum == nullptr)
				{
					ValuePropertyName = TEXT("Integer");
				}
				else
				{
					ValuePropertyName = TEXT("Enum");
				}
			}
			else if (ControlElement->Settings.ControlType == ERigControlType::Bool)
			{
				ValuePropertyName = TEXT("Bool");
			}
			else if (ControlElement->Settings.ControlType == ERigControlType::Position ||
				ControlElement->Settings.ControlType == ERigControlType::Scale)
			{
				ValuePropertyName = TEXT("Vector");
			}
			else if (ControlElement->Settings.ControlType == ERigControlType::Vector2D)
			{
				ValuePropertyName = TEXT("Vector2D");
			}

			TSharedPtr<IPropertyHandle> ValuePropertyHandle = DetailLayout.GetProperty(ValuePropertyName, Proxy->GetClass());
			if (ValuePropertyHandle)
			{
				ValuePropertyHandle->SetPropertyDisplayName(FText::FromName(ControlElement->GetDisplayName()));
			}

			URigHierarchy* Hierarchy = Proxy->ControlRig->GetHierarchy();
			Hierarchy->ForEach<FRigControlElement>([Hierarchy, Proxy, &Category, this](FRigControlElement* ControlElement) -> bool
            {
				FName ParentControlName = NAME_None;
				FRigControlElement* ParentControlElement = Cast<FRigControlElement>(Hierarchy->GetFirstParent(ControlElement));
				if(ParentControlElement)
				{
					ParentControlName = ParentControlElement->GetName();
				}
				
				if (ParentControlName == ControlElement->GetName())
				{
					if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
					{
						if (UObject* NestedProxy = EditMode->ControlProxy->FindProxy(ControlElement->GetName()))
						{
							FName PropertyName(NAME_None);
							switch (ControlElement->Settings.ControlType)
							{
								case ERigControlType::Bool:
								{
									PropertyName = TEXT("Bool");
									break;
								}
								case ERigControlType::Float:
								{
									PropertyName = TEXT("Float");
									break;
								}
								case ERigControlType::Integer:
								{
									if (ControlElement->Settings.ControlEnum == nullptr)
									{
										PropertyName = TEXT("Integer");
									}
									else
									{
										PropertyName = TEXT("Enum");
									}
									break;
								}
								default:
								{
									break;
								}
							}

							if (PropertyName.IsNone())
							{
								return true;
							}

							TArray<UObject*> NestedProxies;
							NestedProxies.Add(NestedProxy);

							FAddPropertyParams Params;
							Params.CreateCategoryNodes(false);

							IDetailPropertyRow* NestedRow = Category.AddExternalObjectProperty(
								NestedProxies,
								PropertyName,
								EPropertyLocation::Advanced,
								Params);
							NestedRow->DisplayName(FText::FromName(ControlElement->Settings.DisplayName));

							Category.SetShowAdvanced(true);
						}
					}
				}
				return true;
			});
		}
	}
protected:
	FEditorModeTools* ModeTools = nullptr;
};

void SControlRigEditModeTools::SetControlRig(UControlRig* ControlRig)
{
	if(ViewportRig.IsValid())
	{
		ViewportRig->ControlSelected().RemoveAll(this);
	}

	SequencerRig = ControlRig;
	ViewportRig = ControlRig;
	if (SequencerRig.IsValid())
	{
		if (UControlRig* InteractionRig = SequencerRig->GetInteractionRig())
		{
			ViewportRig = InteractionRig;
		}
	}

	TArray<TWeakObjectPtr<>> Objects;
	Objects.Add(SequencerRig);
	RigOptionsDetailsView->SetObjects(Objects);

	HierarchyTreeView->RefreshTreeView(true);

	if (ViewportRig.IsValid())
	{
		ViewportRig->ControlSelected().AddRaw(this, &SControlRigEditModeTools::OnRigElementSelected);
	}
}

const URigHierarchy* SControlRigEditModeTools::GetHierarchy() const
{
	if(ViewportRig.IsValid())
	{
		return ViewportRig->GetHierarchy();
	}
	if(SequencerRig.IsValid())
	{
		return ViewportRig->GetHierarchy();
	}
	return nullptr;
}

void SControlRigEditModeTools::Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode,UWorld* InWorld)
{
	bIsChangingRigHierarchy = false;
	
	// initialize settings view
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = true;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bSearchInitialKeyFocus = true;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bShowModifiedPropertiesOption = true;
		DetailsViewArgs.bCustomNameAreaLocation = true;
		DetailsViewArgs.bCustomFilterAreaLocation = true;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
		DetailsViewArgs.bShowScrollBar = false; // Don't need to show this, as we are putting it in a scroll box
	}
	
	ModeTools = InEditMode.GetModeManager();

	ControlDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigEditModeTools::ShouldShowPropertyOnDetailCustomization));
	ControlDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigEditModeTools::IsReadOnlyPropertyOnDetailCustomization));
	ControlDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	RigOptionsDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	RigOptionsDetailsView->SetKeyframeHandler(SharedThis(this));
	RigOptionsDetailsView->OnFinishedChangingProperties().AddSP(this, &SControlRigEditModeTools::OnRigOptionFinishedChange);

	DisplaySettings.bShowBones = false;
	DisplaySettings.bShowControls = true;
	DisplaySettings.bShowNulls = false;
	DisplaySettings.bShowReferences = false;
	DisplaySettings.bShowRigidBodies = false;
	DisplaySettings.bHideParentsOnFilter = true;
	DisplaySettings.bFlattenHierarchyOnFilter = true;

	FRigTreeDelegates RigTreeDelegates;
	RigTreeDelegates.OnGetHierarchy = FOnGetRigTreeHierarchy::CreateSP(this, &SControlRigEditModeTools::GetHierarchy);
	RigTreeDelegates.OnGetDisplaySettings = FOnGetRigTreeDisplaySettings::CreateSP(this, &SControlRigEditModeTools::GetDisplaySettings);
	RigTreeDelegates.OnSelectionChanged = FOnRigTreeSelectionChanged::CreateSP(this, &SControlRigEditModeTools::HandleSelectionChanged);

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(PickerExpander, SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(LOCTEXT("Picker_Header", "Controls"))
				.AreaTitleFont(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f))
				.BodyContent()
				[
					SAssignNew(HierarchyTreeView, SRigHierarchyTreeView)
					.RigTreeDelegates(RigTreeDelegates)
				]
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				ControlDetailsView.ToSharedRef()
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(PickerExpander, SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(LOCTEXT("Picker_SpaceWidget", "Spaces"))
				.AreaTitleFont(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f))
				.Padding(FMargin(8.f))
				.HeaderContent()
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Picker_SpaceWidget", "Spaces"))
						.Font(FCoreStyle::Get().GetFontStyle("ExpandableArea.TitleFont"))
					]
					
					+SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SSpacer)
					]

					+SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.Padding(0.f, 2.f, 8.f, 2.f)
					[
						SNew(SButton)
						.ContentPadding(0.0f)
						.ButtonStyle(FEditorStyle::Get(), "NoBorder")
						.OnClicked(this, &SControlRigEditModeTools::HandleAddSpaceClicked)
						.Cursor(EMouseCursor::Default)
						.ToolTipText(LOCTEXT("AddSpace", "Add Space"))
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush(TEXT("Icons.PlusCircle")))
						]
					]
				]
				.BodyContent()
				[
					SAssignNew(SpacePickerWidget, SRigSpacePickerWidget)
					.AllowDelete(true)
					.AllowReorder(true)
					.AllowAdd(false)
					.ShowBakeButton(true)
					.GetControlCustomization(this, &SControlRigEditModeTools::HandleGetControlElementCustomization)
					.OnActiveSpaceChanged(this, &SControlRigEditModeTools::HandleActiveSpaceChanged)
					.OnSpaceListChanged(this, &SControlRigEditModeTools::HandleSpaceListChanged)
					.OnBakeButtonClicked(this, &SControlRigEditModeTools::OnBakeControlsToNewSpaceButtonClicked)
					// todo: implement GetAdditionalSpacesDelegate to pull spaces from sequencer
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(RigOptionExpander, SExpandableArea)
				.InitiallyCollapsed(false)
				.Visibility(this, &SControlRigEditModeTools::GetRigOptionExpanderVisibility)
				.AreaTitle(LOCTEXT("RigOption_Header", "Rig Options"))
				.AreaTitleFont(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f))
				.BodyContent()
				[
					RigOptionsDetailsView.ToSharedRef()
				]
			]
		]
	];

	HierarchyTreeView->RefreshTreeView(true);
}

void SControlRigEditModeTools::SetDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	ControlDetailsView->SetObjects(InObjects);
}

void SControlRigEditModeTools::SetSequencer(TWeakPtr<ISequencer> InSequencer)
{
	WeakSequencer = InSequencer.Pin();
}

bool SControlRigEditModeTools::IsPropertyKeyable(const UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const
{
	if (InObjectClass && InObjectClass->IsChildOf(UControlRigTransformNoScaleControlProxy::StaticClass()) && InObjectClass->IsChildOf(UControlRigEulerTransformControlProxy::StaticClass()) && InPropertyHandle.GetProperty()
		&& InPropertyHandle.GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformControlProxy, Transform)) 
	{
		return true;
	}
	FCanKeyPropertyParams CanKeyPropertyParams(InObjectClass, InPropertyHandle);
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (Sequencer.IsValid() && Sequencer->CanKeyProperty(CanKeyPropertyParams))
	{
		return true;
	}

	return false;
}

bool SControlRigEditModeTools::IsPropertyKeyingEnabled() const
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (Sequencer.IsValid() && Sequencer->GetFocusedMovieSceneSequence())
	{
		return true;
	}

	return false;
}

bool SControlRigEditModeTools::IsPropertyAnimated(const IPropertyHandle& PropertyHandle, UObject *ParentObject) const
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (Sequencer.IsValid() && Sequencer->GetFocusedMovieSceneSequence())
	{
		FGuid ObjectHandle = Sequencer->GetHandleToObject(ParentObject);
		if (ObjectHandle.IsValid()) 
		{
			UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
			FProperty* Property = PropertyHandle.GetProperty();
			TSharedRef<FPropertyPath> PropertyPath = FPropertyPath::CreateEmpty();
			PropertyPath->AddProperty(FPropertyInfo(Property));
			FName PropertyName(*PropertyPath->ToString(TEXT(".")));
			TSubclassOf<UMovieSceneTrack> TrackClass; //use empty @todo find way to get the UMovieSceneTrack from the Property type.
			return MovieScene->FindTrack(TrackClass, ObjectHandle, PropertyName) != nullptr;
		}
	}
	return false;
}

void SControlRigEditModeTools::OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle)
{
	if (WeakSequencer.IsValid() && !WeakSequencer.Pin()->IsAllowedToChange())
	{
		return;
	}

	TArray<UObject*> Objects;
	KeyedPropertyHandle.GetOuterObjects(Objects);
	for (UObject *Object : Objects)
	{
		UControlRigControlsProxy* Proxy = Cast< UControlRigControlsProxy>(Object);
		if (Proxy)
	{
			Proxy->SetKey(KeyedPropertyHandle);
		}
	}
}

bool SControlRigEditModeTools::ShouldShowPropertyOnDetailCustomization(const FPropertyAndParent& InPropertyAndParent) const
{
	auto ShouldPropertyBeVisible = [](const FProperty& InProperty)
	{
		bool bShow = InProperty.HasAnyPropertyFlags(CPF_Interp) || InProperty.HasMetaData(FRigVMStruct::InputMetaName) || InProperty.HasMetaData(FRigVMStruct::OutputMetaName);

	/*	// Show 'PickerIKTogglePos' properties
		bShow |= (InProperty.GetFName() == GET_MEMBER_NAME_CHECKED(FLimbControl, PickerIKTogglePos));
		bShow |= (InProperty.GetFName() == GET_MEMBER_NAME_CHECKED(FSpineControl, PickerIKTogglePos));
*/

		// Always show settings properties
		const UClass* OwnerClass = InProperty.GetOwner<UClass>();
		bShow |= OwnerClass == UControlRigEditModeSettings::StaticClass();
		bShow |= OwnerClass == UControlRigTransformControlProxy::StaticClass();		
		bShow |= OwnerClass == UControlRigTransformNoScaleControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigEulerTransformControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigFloatControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVectorControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVector2DControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigBoolControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigEnumControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigIntegerControlProxy::StaticClass();

		return bShow;
	};

	bool bContainsVisibleProperty = false;
	if (InPropertyAndParent.Property.IsA<FStructProperty>())
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(&InPropertyAndParent.Property);
		for (TFieldIterator<FProperty> PropertyIt(StructProperty->Struct); PropertyIt; ++PropertyIt)
		{
			if (ShouldPropertyBeVisible(**PropertyIt))
			{
				return true;
			}
		}
	}

	return ShouldPropertyBeVisible(InPropertyAndParent.Property) || 
		(InPropertyAndParent.ParentProperties.Num() > 0 && ShouldPropertyBeVisible(*InPropertyAndParent.ParentProperties[0]));
}

bool SControlRigEditModeTools::IsReadOnlyPropertyOnDetailCustomization(const FPropertyAndParent& InPropertyAndParent) const
{
	auto ShouldPropertyBeEnabled = [](const FProperty& InProperty)
	{
		bool bShow = InProperty.HasAnyPropertyFlags(CPF_Interp) || InProperty.HasMetaData(FRigVMStruct::InputMetaName);

		// Always show settings properties
		const UClass* OwnerClass = InProperty.GetOwner<UClass>();
		bShow |= OwnerClass == UControlRigEditModeSettings::StaticClass();
		bShow |= OwnerClass == UControlRigTransformControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigTransformNoScaleControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigEulerTransformControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigFloatControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVectorControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVector2DControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigBoolControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigEnumControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigIntegerControlProxy::StaticClass();


		return bShow;
	};

	bool bContainsVisibleProperty = false;
	if (InPropertyAndParent.Property.IsA<FStructProperty>())
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(&InPropertyAndParent.Property);
		for (TFieldIterator<FProperty> PropertyIt(StructProperty->Struct); PropertyIt; ++PropertyIt)
		{
			if (ShouldPropertyBeEnabled(**PropertyIt))
			{
				return false;
			}
		}
	}

	return !(ShouldPropertyBeEnabled(InPropertyAndParent.Property) || 
		(InPropertyAndParent.ParentProperties.Num() > 0 && ShouldPropertyBeEnabled(*InPropertyAndParent.ParentProperties[0])));
}

static bool bPickerChangingSelection = false;

void SControlRigEditModeTools::OnManipulatorsPicked(const TArray<FName>& Manipulators)
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		if (!bPickerChangingSelection)
		{
			TGuardValue<bool> SelectGuard(bPickerChangingSelection, true);
			ControlRigEditMode->ClearRigElementSelection((uint32)ERigElementType::Control);
			ControlRigEditMode->SetRigElementSelection(ERigElementType::Control, Manipulators, true);
		}
	}
}

void SControlRigEditModeTools::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	if (bPickerChangingSelection)
	{
		return;
	}

	TGuardValue<bool> SelectGuard(bPickerChangingSelection, true);
	switch (InNotifType)
	{
		case ERigVMGraphNotifType::NodeSelected:
		case ERigVMGraphNotifType::NodeDeselected:
		{
			URigVMNode* Node = Cast<URigVMNode>(InSubject);
			if (Node)
			{
				// those are not yet implemented yet
				// ControlPicker->SelectManipulator(Node->Name, InType == EControlRigModelNotifType::NodeSelected);
			}
			break;
		}
		default:
		{
			break;
		}
	}
}

void SControlRigEditModeTools::HandleSelectionChanged(TSharedPtr<FRigTreeElement> Selection, ESelectInfo::Type SelectInfo)
{
	if(bIsChangingRigHierarchy)
	{
		return;
	}
	
	const URigHierarchy* Hierarchy = GetHierarchy();
	if (Hierarchy)
	{
		URigHierarchyController* Controller = ((URigHierarchy*)Hierarchy)->GetController(true);
		check(Controller);
		
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

		const TArray<FRigElementKey> NewSelection = HierarchyTreeView->GetSelectedKeys();
		if(!Controller->SetSelection(NewSelection))
		{
			return;
		}
	}
}

void SControlRigEditModeTools::OnRigElementSelected(UControlRig* Subject, FRigControlElement* ControlElement, bool bSelected)
{
	const FRigElementKey Key = ControlElement->GetKey();
	for (int32 RootIndex = 0; RootIndex < HierarchyTreeView->GetRootElements().Num(); ++RootIndex)
	{
		TSharedPtr<FRigTreeElement> Found = HierarchyTreeView->FindElement(Key, HierarchyTreeView->GetRootElements()[RootIndex]);
		if (Found.IsValid())
		{
			HierarchyTreeView->SetItemSelection(Found, bSelected, ESelectInfo::OnNavigation);
			
			TArray<TSharedPtr<FRigTreeElement>> SelectedItems = HierarchyTreeView->GetSelectedItems();
			for (TSharedPtr<FRigTreeElement> SelectedItem : SelectedItems)
			{
				HierarchyTreeView->SetExpansionRecursive(SelectedItem, false, true);
			}

			if (SelectedItems.Num() > 0)
			{
				HierarchyTreeView->RequestScrollIntoView(SelectedItems.Last());
			}
		}
	}

	if (UControlRig* ControlRig = SequencerRig.Get())
	{
		// get the selected controls
		TArray<FRigElementKey> SelectedControls = ControlRig->GetHierarchy()->GetSelectedKeys(ERigElementType::Control);
		SpacePickerWidget->SetControls(ControlRig->GetHierarchy(), SelectedControls);
	}
}

const FRigControlElementCustomization* SControlRigEditModeTools::HandleGetControlElementCustomization(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey)
{
	if (UControlRig* ControlRig = SequencerRig.Get())
	{
		return ControlRig->GetControlCustomization(InControlKey);
	}
	return nullptr;
}

void SControlRigEditModeTools::HandleActiveSpaceChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey,
	const FRigElementKey& InSpaceKey)
{

	if (WeakSequencer.IsValid())
	{
		if (UControlRig* ControlRig = SequencerRig.Get())
		{
			FString FailureReason;
			URigHierarchy::TElementDependencyMap DependencyMap = InHierarchy->GetDependenciesForVM(ControlRig->GetVM());
			if(!InHierarchy->CanSwitchToParent(InControlKey, InSpaceKey, DependencyMap, &FailureReason))
			{
				// notification
				FNotificationInfo Info(FText::FromString(FailureReason));
				Info.bFireAndForget = true;
				Info.FadeOutDuration = 2.0f;
				Info.ExpireDuration = 8.0f;

				const TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
				NotificationPtr->SetCompletionState(SNotificationItem::CS_Fail);
				return;
			}
			
			if (const FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InControlKey))
			{
				ISequencer* Sequencer = WeakSequencer.Pin().Get();
				if (Sequencer)
				{
					FScopedTransaction Transaction(LOCTEXT("KeyControlRigSpace", "Key Control Rig Space"));
					ControlRig->Modify();

					FSpaceChannelAndSection SpaceChannelAndSection = FControlRigSpaceChannelHelpers::FindSpaceChannelAndSectionForControl(ControlRig, InControlKey.Name, Sequencer, true /*bCreateIfNeeded*/);
					if (SpaceChannelAndSection.SpaceChannel)
					{
						const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
						const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
						FFrameNumber CurrentTime = FrameTime.GetFrame();
						FControlRigSpaceChannelHelpers::SequencerKeyControlRigSpaceChannel(ControlRig, Sequencer, SpaceChannelAndSection.SpaceChannel, SpaceChannelAndSection.SectionToKey, CurrentTime, InHierarchy, InControlKey, InSpaceKey);
					}
				}
			}
		}
	}
}

void SControlRigEditModeTools::HandleSpaceListChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey,
	const TArray<FRigElementKey>& InSpaceList)
{
	if (UControlRig* ControlRig = SequencerRig.Get())
	{
		if (const FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InControlKey))
		{
			FRigControlElementCustomization ControlCustomization = *ControlRig->GetControlCustomization(InControlKey);
			ControlCustomization.AvailableSpaces = InSpaceList;
			ControlCustomization.RemovedSpaces.Reset();

			// remember  the elements which are in the asset's available list but removed by the user
			for (const FRigElementKey& AvailableSpace : ControlElement->Settings.Customization.AvailableSpaces)
			{
				if (!ControlCustomization.AvailableSpaces.Contains(AvailableSpace))
				{
					ControlCustomization.RemovedSpaces.Add(AvailableSpace);
				}
			}

			ControlRig->SetControlCustomization(InControlKey, ControlCustomization);

			if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
			{
				const TGuardValue<bool> SuspendGuard(EditMode->bSuspendHierarchyNotifs, true);
				InHierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
			}
			else
			{
				InHierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
			}

			SpacePickerWidget->RefreshContents();
		}
	}
}

FReply SControlRigEditModeTools::HandleAddSpaceClicked()
{
	return SpacePickerWidget->HandleAddElementClicked();
}

FReply SControlRigEditModeTools::OnBakeControlsToNewSpaceButtonClicked()
{
	if (SpacePickerWidget->GetHierarchy() == nullptr)
	{
		return FReply::Unhandled();
	}
	if (SpacePickerWidget->GetControls().Num() == 0)
	{
		return FReply::Unhandled();
	}
	if (!SequencerRig.Get())
	{
		return FReply::Unhandled();
	}
	ISequencer* Sequencer = WeakSequencer.Pin().Get();
	if (Sequencer == nullptr || Sequencer->GetFocusedMovieSceneSequence() == nullptr || Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene() == nullptr)
	{
		return FReply::Unhandled();
	}
	UControlRig* ControlRig = SequencerRig.Get();

	FRigSpacePickerBakeSettings Settings;
	//Find default target space, just use first control and find space at current sequencer time
	//Then Find range

	// FindSpaceChannelAndSectionForControl() will trigger RecreateCurveEditor(), which will deselect the controls
	// but in theory the selection will be recovered in the next tick, so here we just cache the selected controls
	// and use it throughout this function. If this deselection is causing other problems, this part could use a revisit.
	TArray<FRigElementKey> ControlKeys = SpacePickerWidget->GetControls();

	FSpaceChannelAndSection SpaceChannelAndSection = FControlRigSpaceChannelHelpers::FindSpaceChannelAndSectionForControl(ControlRig, ControlKeys[0].Name, Sequencer, true /*bCreateIfNeeded*/);
	if (SpaceChannelAndSection.SpaceChannel != nullptr)
	{
		const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
		const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
		FFrameNumber CurrentTime = FrameTime.GetFrame();
		FMovieSceneControlRigSpaceBaseKey Value;
		using namespace UE::MovieScene;
		URigHierarchy* RigHierarchy = SpacePickerWidget->GetHierarchy();
		Settings.TargetSpace = URigHierarchy::GetDefaultParentKey();
		
		TRange<FFrameNumber> Range = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->GetPlaybackRange();
		TArray<FFrameNumber> Keys;
		TArray < FKeyHandle> KeyHandles;

		Settings.StartFrame = Range.GetLowerBoundValue();
		Settings.EndFrame = Range.GetUpperBoundValue();
		if (Keys.Num() > 0)
		{
			int32 Index = Algo::LowerBound(Keys, CurrentTime);
			if (Index >= 0 && Index < (Keys.Num() - 1))
			{
				Settings.StartFrame = Keys[Index];
				Settings.EndFrame = Keys[Index + 1];

			}
		}

		TSharedRef<SRigSpacePickerBakeWidget> BakeWidget =
			SNew(SRigSpacePickerBakeWidget)
			.Settings(Settings)
			.Hierarchy(SpacePickerWidget->GetHierarchy())
			.Controls(ControlKeys) // use the cached controls here since the selection is not recovered until next tick.
			.Sequencer(Sequencer)
			.GetControlCustomization(this, &SControlRigEditModeTools::HandleGetControlElementCustomization)
			.OnBake_Lambda([Sequencer, ControlRig,TickResolution](URigHierarchy* InHierarchy, TArray<FRigElementKey> InControls, FRigSpacePickerBakeSettings InSettings)
				{
					TArray<FFrameNumber> Frames;
					
					const FFrameRate& FrameRate = Sequencer->GetFocusedDisplayRate();
					FFrameNumber FrameRateInFrameNumber = TickResolution.AsFrameNumber(FrameRate.AsInterval());
					for (FFrameNumber& Frame = InSettings.StartFrame; Frame <= InSettings.EndFrame; Frame += FrameRateInFrameNumber)
					{
						Frames.Add(Frame);
					}
					FScopedTransaction Transaction(LOCTEXT("BakeControlToSpace", "Bake Control In Space"));
					for (const FRigElementKey& ControlKey : InControls)
					{
						//when baking we will now create a channel if one doesn't exist, was causing confusion
						FSpaceChannelAndSection SpaceChannelAndSection = FControlRigSpaceChannelHelpers::FindSpaceChannelAndSectionForControl(ControlRig, ControlKey.Name, Sequencer, true /*bCreateIfNeeded*/);
						if (SpaceChannelAndSection.SpaceChannel)
						{
							FControlRigSpaceChannelHelpers::SequencerBakeControlInSpace(ControlRig, Sequencer, SpaceChannelAndSection.SpaceChannel, SpaceChannelAndSection.SectionToKey,
								Frames, InHierarchy, ControlKey, InSettings);
						}
					}
					return FReply::Handled();
				});

		return BakeWidget->OpenDialog(true);
	}
	return FReply::Unhandled();

}

EVisibility SControlRigEditModeTools::GetRigOptionExpanderVisibility() const
{
	if (UControlRig* ControlRig = SequencerRig.Get())
	{
		if (Cast<UFKControlRig>(ControlRig))
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Hidden;
}

void SControlRigEditModeTools::OnRigOptionFinishedChange(const FPropertyChangedEvent& PropertyChangedEvent)
{
	SetControlRig(SequencerRig.Get());

	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		EditMode->SetObjects_Internal();
	}
}

void SControlRigEditModeTools::CustomizeToolBarPalette(FToolBarBuilder& ToolBarBuilder)
{
	//TOGGLE SELECTED RIG CONTROLS
	ToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateLambda([this] {
			FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
			if (ControlRigEditMode)
			{
				ControlRigEditMode->SetOnlySelectRigControls(!ControlRigEditMode->GetOnlySelectRigControls());
			}
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this] {
			FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
			if (ControlRigEditMode)
			{
				return ControlRigEditMode->GetOnlySelectRigControls();
			}
			return false;
			})

		),
		NAME_None,
		LOCTEXT("OnlySelectControls", "Select"),
		LOCTEXT("OnlySelectControlsTooltip", "Only Select Control Rig Controls"),
		FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.OnlySelectControls")),
		EUserInterfaceActionType::ToggleButton
		);
	ToolBarBuilder.AddSeparator();

	//POSES
	ToolBarBuilder.AddToolBarButton(
		FExecuteAction::CreateSP(this, &SControlRigEditModeTools::MakePoseDialog),
		NAME_None,
		LOCTEXT("Poses", "Poses"),
		LOCTEXT("PosesTooltip", "Show Poses"),
		FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.PoseTool")),
		EUserInterfaceActionType::Button
	);
	ToolBarBuilder.AddSeparator();

	// Tweens
	ToolBarBuilder.AddToolBarButton(
		FExecuteAction::CreateSP(this, &SControlRigEditModeTools::MakeTweenDialog),
		NAME_None,
		LOCTEXT("Tweens", "Tweens"),
		LOCTEXT("TweensTooltip", "Create Tweens"),
		FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.TweenTool")),
		EUserInterfaceActionType::Button
	);

	// Snap
	ToolBarBuilder.AddToolBarButton(
		FExecuteAction::CreateSP(this, &SControlRigEditModeTools::MakeSnapperDialog),
		NAME_None,
		LOCTEXT("Snapper", "Snapper"),
		LOCTEXT("SnapperTooltip", "Snap child objects to a parent object over a set of frames"),
		FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.SnapperTool")),
		EUserInterfaceActionType::Button
	);

	// Motion Trail
	ToolBarBuilder.AddToolBarButton(
		FExecuteAction::CreateSP(this, &SControlRigEditModeTools::MakeMotionTrailDialog),
		NAME_None,
		LOCTEXT("MotionTrails", "Trails"),
		LOCTEXT("MotionTrailsTooltip", "Display motion trails for animated objects"),
		FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.EditableMotionTrails")),
		EUserInterfaceActionType::Button
	);
	//Pivot
	ToolBarBuilder.AddToolBarButton(
		FUIAction(
		FExecuteAction::CreateSP(this, &SControlRigEditModeTools::ToggleEditPivotMode),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([] {
				if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
				{
					TSharedPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule->GetLevelEditorInstance().Pin();

					if (LevelEditorPtr.IsValid())
					{
						FString ActiveToolName = LevelEditorPtr->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->GetActiveToolName(EToolSide::Left);
						if (ActiveToolName == TEXT("SequencerPivotTool"))
						{
							return true;
						}
					}
				}
				return false;

			})
		),
		NAME_None,
		LOCTEXT("TempPivot", "Pivot"),
		LOCTEXT("TempPivotTooltip", "Create a temporary pivot to rotate the selected Control"),
		FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.TemporaryPivot")),
		EUserInterfaceActionType::ToggleButton
		);
	
	ToolBarBuilder.AddSeparator();
}

void SControlRigEditModeTools::MakePoseDialog()
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		FGlobalTabmanager::Get()->TryInvokeTab(IControlRigEditorModule::ControlRigPoseTab);
	}
}

void SControlRigEditModeTools::MakeTweenDialog()
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		FGlobalTabmanager::Get()->TryInvokeTab(IControlRigEditorModule::ControlRigTweenTab);
	}
}

void SControlRigEditModeTools::MakeSnapperDialog()
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		FGlobalTabmanager::Get()->TryInvokeTab(IControlRigEditorModule::ControlRigSnapperTab);
	}
}

void SControlRigEditModeTools::MakeMotionTrailDialog()
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		FGlobalTabmanager::Get()->TryInvokeTab(IControlRigEditorModule::ControlRigMotionTrailTab);
	}
}

void SControlRigEditModeTools::ToggleEditPivotMode()
{
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
	{
		TSharedPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule->GetLevelEditorInstance().Pin();

		if (LevelEditorPtr.IsValid())
		{
			FString ActiveToolName = LevelEditorPtr->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->GetActiveToolName(EToolSide::Left);
			if(ActiveToolName == TEXT("SequencerPivotTool"))
			{
				LevelEditorPtr->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->DeactivateTool(EToolSide::Left, EToolShutdownType::Completed);
			}
			else
			{
				LevelEditorPtr->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->SelectActiveToolType(EToolSide::Left, TEXT("SequencerPivotTool"));
				LevelEditorPtr->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->ActivateTool(EToolSide::Left);

			}
		}
	}
}


/* MZ TODO
void SControlRigEditModeTools::MakeSelectionSetDialog()
{

	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		TSharedPtr<SWindow> ExistingWindow = SelectionSetWindow.Pin();
		if (ExistingWindow.IsValid())
		{
			ExistingWindow->BringToFront();
		}
		else
		{
			ExistingWindow = SNew(SWindow)
				.Title(LOCTEXT("SelectionSets", "Selection Set"))
				.HasCloseButton(true)
				.SupportsMaximize(false)
				.SupportsMinimize(false)
				.ClientSize(FVector2D(165, 200));
			TSharedPtr<SWindow> RootWindow = FGlobalTabmanager::Get()->GetRootWindow();
			if (RootWindow.IsValid())
			{
				FSlateApplication::Get().AddWindowAsNativeChild(ExistingWindow.ToSharedRef(), RootWindow.ToSharedRef());
			}
			else
			{
				FSlateApplication::Get().AddWindow(ExistingWindow.ToSharedRef());
			}

		}

		ExistingWindow->SetContent(
			SNew(SControlRigBaseListWidget)
		);
		SelectionSetWindow = ExistingWindow;
	}
}
*/
FText SControlRigEditModeTools::GetActiveToolName() const
{
	return  FText();
}

FText SControlRigEditModeTools::GetActiveToolMessage() const
{
	return  FText();
}

#undef LOCTEXT_NAMESPACE
