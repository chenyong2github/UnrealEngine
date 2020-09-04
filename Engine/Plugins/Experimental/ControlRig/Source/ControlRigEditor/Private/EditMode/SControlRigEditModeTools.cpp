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
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "SControlHierarchy.h"
#include "SControlPicker.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Rigs/FKControlRig.h"

#define LOCTEXT_NAMESPACE "ControlRigRootCustomization"

class FControlRigEditModeGenericDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FControlRigEditModeGenericDetails);
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
			FName ValuePropertyName = TEXT("Transform");
			if (Proxy->RigControl->ControlType == ERigControlType::Float)
			{
				ValuePropertyName = TEXT("Float");
			}
			else if (Proxy->RigControl->ControlType == ERigControlType::Integer)
			{
				if (Proxy->RigControl->ControlEnum == nullptr)
				{
					ValuePropertyName = TEXT("Integer");
				}
				else
				{
					ValuePropertyName = TEXT("Enum");
				}
			}
			else if (Proxy->RigControl->ControlType == ERigControlType::Bool)
			{
				ValuePropertyName = TEXT("Bool");
			}
			else if (Proxy->RigControl->ControlType == ERigControlType::Position ||
				Proxy->RigControl->ControlType == ERigControlType::Scale)
			{
				ValuePropertyName = TEXT("Vector");
			}
			else if (Proxy->RigControl->ControlType == ERigControlType::Vector2D)
			{
				ValuePropertyName = TEXT("Vector2D");
			}

			TSharedPtr<IPropertyHandle> ValuePropertyHandle = DetailLayout.GetProperty(ValuePropertyName, Proxy->GetClass());
			if (ValuePropertyHandle)
			{
				ValuePropertyHandle->SetPropertyDisplayName(FText::FromName(Proxy->RigControl->GetDisplayName()));
			}

			for (const FRigControl& ChildControl : Proxy->ControlRig->GetControlHierarchy())
			{
				if (ChildControl.ParentName == Proxy->RigControl->Name)
				{
					if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName)))
					{
						if (UObject* NestedProxy = EditMode->ControlProxy->FindProxy(ChildControl.Name))
						{
							FName PropertyName(NAME_None);
							switch (ChildControl.ControlType)
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
									if (ChildControl.ControlEnum == nullptr)
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
								continue;
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
							NestedRow->DisplayName(FText::FromName(ChildControl.GetDisplayName()));

							Category.SetShowAdvanced(true);
						}
					}
				}
			}
		}
	}
};

void SControlRigEditModeTools::SetControlRig(UControlRig* ControlRig)
{
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

	ControlHierarchy->SetControlRig(ViewportRig.Get());
}

void SControlRigEditModeTools::Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode,UWorld* InWorld)
{
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
		DetailsViewArgs.bShowActorLabel = false;
		DetailsViewArgs.bCustomNameAreaLocation = true;
		DetailsViewArgs.bCustomFilterAreaLocation = true;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
		DetailsViewArgs.bShowScrollBar = false; // Don't need to show this, as we are putting it in a scroll box
	}

	ControlDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigEditModeTools::ShouldShowPropertyOnDetailCustomization));
	ControlDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigEditModeTools::IsReadOnlyPropertyOnDetailCustomization));
	ControlDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance));

	RigOptionsDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	RigOptionsDetailsView->SetKeyframeHandler(SharedThis(this));
	RigOptionsDetailsView->OnFinishedChangingProperties().AddSP(this, &SControlRigEditModeTools::OnRigOptionFinishedChange);

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
					SAssignNew(ControlHierarchy, SControlHierarchy, InEditMode.GetControlRig(true))
				]
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				ControlDetailsView.ToSharedRef()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(RigOptionExpander, SExpandableArea)
				.InitiallyCollapsed(true)
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
}

void SControlRigEditModeTools::SetDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	ControlDetailsView->SetObjects(InObjects);
}

void SControlRigEditModeTools::SetSequencer(TWeakPtr<ISequencer> InSequencer)
{
	WeakSequencer = InSequencer.Pin();
}

bool SControlRigEditModeTools::IsPropertyKeyable(UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const
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
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
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

	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName)))
	{
		EditMode->SetObjects_Internal();
	}
}

#undef LOCTEXT_NAMESPACE
