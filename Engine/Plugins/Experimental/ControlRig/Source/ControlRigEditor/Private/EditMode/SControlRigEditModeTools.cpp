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

#define LOCTEXT_NAMESPACE "ControlRigRootCustomization"

void SControlRigEditModeTools::SetControlRig(UControlRig* ControlRig)
{
	ControlHierarchy->SetControlRig(ControlRig);
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
		DetailsViewArgs.bShowOptions = true;
		DetailsViewArgs.bShowModifiedPropertiesOption = true;
		DetailsViewArgs.bShowActorLabel = false;
		DetailsViewArgs.bCustomNameAreaLocation = true;
		DetailsViewArgs.bCustomFilterAreaLocation = true;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
		DetailsViewArgs.bShowScrollBar = false; // Don't need to show this, as we are putting it in a scroll box
	}

	DetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	DetailsView->SetKeyframeHandler(SharedThis(this));
	DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigEditModeTools::ShouldShowPropertyOnDetailCustomization));
	DetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigEditModeTools::IsReadOnlyPropertyOnDetailCustomization));

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			/* We don't do the picker nor the float controls but leeaving this here inc case we 
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(PickerExpander, SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(LOCTEXT("Picker_Header", "Controls"))
				.AreaTitleFont(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f))
				.BodyContent()
				[
					SAssignNew(ControlPicker, SControlPicker, InWorld)
				]
			]
			

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(PickerExpander, SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(LOCTEXT("CurveControl_Header", "Curve Controls"))
				.AreaTitleFont(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f))
				.BodyContent()
				[
					SAssignNew(CurveControlContainer, SCurveControlContainer, InEditMode.GetControlRig())
				]
			]
			*/

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
					SAssignNew(ControlHierarchy, SControlHierarchy, InEditMode.GetControlRig())
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				DetailsView.ToSharedRef()
			]
		]
	];

	// Bind notification when edit mode selection changes, so we can update picker
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		ControlRigEditMode->ModifiedEvent.AddSP(this, &SControlRigEditModeTools::HandleModifiedEvent);
	}	
}

void SControlRigEditModeTools::SetDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	DetailsView->SetObjects(InObjects);

	// Look for the first UControlRig
	UControlRig* Rig = nullptr;
	for (TWeakObjectPtr<UObject> ObjPtr : InObjects)
	{
		Rig = Cast<UControlRig>(ObjPtr.Get());
		if (Rig)
		{
			break;
		}
	}

	//ControlPicker->SetControlRig(Rig);

}

void SControlRigEditModeTools::SetSequencer(TWeakPtr<ISequencer> InSequencer)
{
	WeakSequencer = InSequencer.Pin();
}

bool SControlRigEditModeTools::IsPropertyKeyable(UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const
{
	if (InObjectClass && InObjectClass->IsChildOf(UControlRigTransformNoScaleControlProxy::StaticClass()) && InPropertyHandle.GetProperty() 
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
		bool bShow = InProperty.HasAnyPropertyFlags(CPF_Interp) || InProperty.HasMetaData(UControlRig::InputMetaName) || InProperty.HasMetaData(UControlRig::OutputMetaName);

	/*	// Show 'PickerIKTogglePos' properties
		bShow |= (InProperty.GetFName() == GET_MEMBER_NAME_CHECKED(FLimbControl, PickerIKTogglePos));
		bShow |= (InProperty.GetFName() == GET_MEMBER_NAME_CHECKED(FSpineControl, PickerIKTogglePos));
*/

		// Always show settings properties
		const UClass* OwnerClass = InProperty.GetOwner<UClass>();
		bShow |= OwnerClass == UControlRigEditModeSettings::StaticClass();
		bShow |= OwnerClass == UControlRigTransformControlProxy::StaticClass();		
		bShow |= OwnerClass == UControlRigTransformNoScaleControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigFloatControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVectorControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVector2DControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigBoolControlProxy::StaticClass();

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
		bool bShow = InProperty.HasAnyPropertyFlags(CPF_Interp) || InProperty.HasMetaData(UControlRig::InputMetaName);

		// Always show settings properties
		const UClass* OwnerClass = InProperty.GetOwner<UClass>();
		bShow |= OwnerClass == UControlRigEditModeSettings::StaticClass();
		bShow |= OwnerClass == UControlRigTransformControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigTransformNoScaleControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigFloatControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVectorControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVector2DControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigBoolControlProxy::StaticClass();

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

#undef LOCTEXT_NAMESPACE
