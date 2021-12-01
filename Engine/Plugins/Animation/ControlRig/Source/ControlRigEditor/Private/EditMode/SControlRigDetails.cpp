// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigDetails.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AssetData.h"
#include "EditorStyleSet.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"
#include "ScopedTransaction.h"
#include "ControlRig.h"
#include "UnrealEdGlobals.h"
#include "ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "UnrealEd/Public/Selection.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "ControlRigControlsProxy.h"
#include "ControlRigEditModeSettings.h"
#include "Modules/ModuleManager.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "ControlRigDetails"

void FControlRigEditModeGenericDetails::CustomizeDetails(class IDetailLayoutBuilder& DetailLayout)
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

	if (ProxiesBeingCustomized.Num() == 0 || ProxiesBeingCustomized[0]->GetControlElement() == nullptr)
	{
		return;
	}
	FText ControlText = FText::FromName(ProxiesBeingCustomized[0]->GetName());

	if (ProxiesBeingCustomized.Num() > 1)
	{
		if (ProxiesBeingCustomized[0]->GetClass() == UControlRigTransformControlProxy::StaticClass())
		{
			ControlText = LOCTEXT("TransformChannels", "Transform Channels");
		}
		else if (ProxiesBeingCustomized[0]->GetClass() == UControlRigTransformNoScaleControlProxy::StaticClass())
		{
			ControlText = LOCTEXT("TransformNoScaleChannels", "TransformNoScale Channels");
		}
		else if (ProxiesBeingCustomized[0]->GetClass() == UControlRigEulerTransformControlProxy::StaticClass())
		{
			ControlText = LOCTEXT("EulerTransformChannels", "Euler Transform Channels");
		}
		else if (ProxiesBeingCustomized[0]->GetClass() == UControlRigFloatControlProxy::StaticClass())
		{
			ControlText = LOCTEXT("FloatChannels", "Float Channels");
		}
		else if (ProxiesBeingCustomized[0]->GetClass() == UControlRigVectorControlProxy::StaticClass())
		{
			ControlText = LOCTEXT("VectorChannels", "Vector Channels");
		}
		else if (ProxiesBeingCustomized[0]->GetClass() == UControlRigVector2DControlProxy::StaticClass())
		{
			ControlText = LOCTEXT("Vector2DChannels", "Vector2D Channels");
		}
		else if (ProxiesBeingCustomized[0]->GetClass() == UControlRigBoolControlProxy::StaticClass())
		{
			ControlText = LOCTEXT("BoolChannels", "Bool Channels");
		}
		else if (ProxiesBeingCustomized[0]->GetClass() == UControlRigEnumControlProxy::StaticClass())
		{
			ControlText = LOCTEXT("EnumChannels", "Enum Channels");
		}
		else if (ProxiesBeingCustomized[0]->GetClass() == UControlRigIntegerControlProxy::StaticClass())
		{
			ControlText = LOCTEXT("IntegerChannels", "Integer Channels");
		}
	}

	IDetailCategoryBuilder& Category = DetailLayout.EditCategory(TEXT("Control"), ControlText);
	for (UControlRigControlsProxy* Proxy : ProxiesBeingCustomized)
	{
		FRigControlElement* ControlElement = Proxy->GetControlElement();
		if (ControlElement == nullptr)
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
		else if (ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
		{
			ValuePropertyName = TEXT("EulerTransform");
		}
		else if (ControlElement->Settings.ControlType == ERigControlType::TransformNoScale)
		{
			ValuePropertyName = TEXT("TransformNoScale");
		}

		TSharedPtr<IPropertyHandle> ValuePropertyHandle = DetailLayout.GetProperty(ValuePropertyName, Proxy->GetClass());
		if (ValuePropertyHandle)
		{
			ValuePropertyHandle->SetPropertyDisplayName(FText::FromName(Proxy->GetName()));
		}

		URigHierarchy* Hierarchy = Proxy->ControlRig->GetHierarchy();
		Hierarchy->ForEach<FRigControlElement>([Hierarchy, Proxy, &Category, this](FRigControlElement* ControlElement) -> bool
			{
				FName ParentControlName = NAME_None;
				FRigControlElement* ParentControlElement = Cast<FRigControlElement>(Hierarchy->GetFirstParent(ControlElement));
				if (ParentControlElement)
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

void SControlRigDetails::Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode)
{
	ModeTools = InEditMode.GetModeManager();
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
		DetailsViewArgs.bAllowMultipleTopLevelObjects = false;
		DetailsViewArgs.bShowScrollBar = false; // Don't need to show this, as we are putting it in a scroll box
	}

	ControlEulerTransformDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlEulerTransformDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlEulerTransformDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigDetails::ShouldShowPropertyOnDetailCustomization));
	ControlEulerTransformDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization));
	ControlEulerTransformDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	ControlTransformDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlTransformDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlTransformDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigDetails::ShouldShowPropertyOnDetailCustomization));
	ControlTransformDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization));
	ControlTransformDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	ControlTransformNoScaleDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlTransformNoScaleDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlTransformNoScaleDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigDetails::ShouldShowPropertyOnDetailCustomization));
	ControlTransformNoScaleDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization));
	ControlTransformNoScaleDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	ControlFloatDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlFloatDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlFloatDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigDetails::ShouldShowPropertyOnDetailCustomization));
	ControlFloatDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization));
	ControlFloatDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	ControlEnumDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlEnumDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlEnumDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigDetails::ShouldShowPropertyOnDetailCustomization));
	ControlEnumDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization));
	ControlEnumDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	ControlIntegerDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlIntegerDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlIntegerDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigDetails::ShouldShowPropertyOnDetailCustomization));
	ControlIntegerDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization));
	ControlIntegerDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	ControlBoolDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlBoolDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlBoolDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigDetails::ShouldShowPropertyOnDetailCustomization));
	ControlBoolDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization));
	ControlBoolDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	ControlVectorDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlVectorDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlVectorDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigDetails::ShouldShowPropertyOnDetailCustomization));
	ControlVectorDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization));
	ControlVectorDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	ControlVector2DDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlVector2DDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlVector2DDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigDetails::ShouldShowPropertyOnDetailCustomization));
	ControlVector2DDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization));
	ControlVector2DDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	ChildSlot
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
			SNew(SVerticalBox)

			
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ControlEulerTransformDetailsView.ToSharedRef()
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ControlTransformDetailsView.ToSharedRef()
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ControlTransformNoScaleDetailsView.ToSharedRef()
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ControlBoolDetailsView.ToSharedRef()
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ControlIntegerDetailsView.ToSharedRef()
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ControlEnumDetailsView.ToSharedRef()
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ControlVectorDetailsView.ToSharedRef()
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ControlVector2DDetailsView.ToSharedRef()
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ControlFloatDetailsView.ToSharedRef()
				]

			]
		];

	SetEditMode(InEditMode);
}

SControlRigDetails::~SControlRigDetails()
{
	//base class handles control rig related cleanup
}


void SControlRigDetails::HandleControlAdded(UControlRig* ControlRig, bool bIsAdded)
{
	FControlRigBaseDockableView::HandleControlAdded(ControlRig, bIsAdded);
	UpdateProxies();
}

void SControlRigDetails::HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected)
{
	FControlRigBaseDockableView::HandleControlSelected(Subject, InControl, bSelected);
	UpdateProxies();
}

void SControlRigDetails::UpdateProxies()
{
	//proxies that are in edit mode are also listening to the same messages so they may not be set up yet so need to wait
	GEditor->GetTimerManager()->SetTimerForNextTick([this]()
	{
		TArray<TWeakObjectPtr<>> Eulers;
		TArray<TWeakObjectPtr<>> Transforms;
		TArray<TWeakObjectPtr<>> TransformNoScales;
		TArray<TWeakObjectPtr<>> Floats;
		TArray<TWeakObjectPtr<>> Vectors;
		TArray<TWeakObjectPtr<>> Vector2Ds;
		TArray<TWeakObjectPtr<>> Bools;
		TArray<TWeakObjectPtr<>> Integers;
		TArray<TWeakObjectPtr<>> Enums;
		if (UControlRig* ControlRig = GetControlRig())
		{
			if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
			{
				if (UControlRigDetailPanelControlProxies* ControlProxy = EditMode->GetDetailProxies())
				{
					const TArray<UControlRigControlsProxy*>& Proxies = ControlProxy->GetSelectedProxies();
					for (UControlRigControlsProxy* Proxy : Proxies)
					{
						if (Proxy->GetClass() == UControlRigTransformControlProxy::StaticClass())
						{
							Transforms.Add(Proxy);
						}
						else if (Proxy->GetClass() == UControlRigTransformNoScaleControlProxy::StaticClass())
						{
							TransformNoScales.Add(Proxy);
						}
						else if (Proxy->GetClass() == UControlRigEulerTransformControlProxy::StaticClass())
						{
							Eulers.Add(Proxy);
						}
						else if (Proxy->GetClass() == UControlRigFloatControlProxy::StaticClass())
						{
							Floats.Add(Proxy);
						}
						else if (Proxy->GetClass() == UControlRigVectorControlProxy::StaticClass())
						{
							Vectors.Add(Proxy);
						}
						else if (Proxy->GetClass() == UControlRigVector2DControlProxy::StaticClass())
						{
							Vector2Ds.Add(Proxy);
						}
						else if (Proxy->GetClass() == UControlRigBoolControlProxy::StaticClass())
						{
							Bools.Add(Proxy);
						}
						else if (Proxy->GetClass() == UControlRigEnumControlProxy::StaticClass())
						{
							Enums.Add(Proxy);
						}
						else if (Proxy->GetClass() == UControlRigIntegerControlProxy::StaticClass())
						{
							Integers.Add(Proxy);
						}
					}
				}
				for (TWeakObjectPtr<>& Object : Transforms)
				{
					UControlRigControlsProxy* Proxy = Cast<UControlRigControlsProxy>(Object.Get());
					if (Proxy)
					{
						Proxy->SetIsMultiple(Transforms.Num() > 1);
					}
				}
			}
		}
		SetTransformDetailsObjects(Transforms);
		SetTransformNoScaleDetailsObjects(TransformNoScales);
		SetEulerTransformDetailsObjects(Eulers);
		SetFloatDetailsObjects(Floats);
		SetVectorDetailsObjects(Vectors);
		SetVector2DDetailsObjects(Vector2Ds);
		SetBoolDetailsObjects(Bools);
		SetIntegerDetailsObjects(Integers);
		SetEnumDetailsObjects(Enums);
	});
}

void SControlRigDetails::SetEulerTransformDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	if (ControlEulerTransformDetailsView)
	{
		ControlEulerTransformDetailsView->SetObjects(InObjects);
	}
};

void SControlRigDetails::SetTransformDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	if (ControlTransformDetailsView)
	{
		ControlTransformDetailsView->SetObjects(InObjects);
	}
}

void SControlRigDetails::SetTransformNoScaleDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	if (ControlTransformNoScaleDetailsView)
	{
		ControlTransformNoScaleDetailsView->SetObjects(InObjects);
	}
}

void SControlRigDetails::SetFloatDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	if (ControlFloatDetailsView)
	{
		ControlFloatDetailsView->SetObjects(InObjects);
	}
}

void SControlRigDetails::SetBoolDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	if (ControlBoolDetailsView)
	{
		ControlBoolDetailsView->SetObjects(InObjects);
	}
}

void SControlRigDetails::SetIntegerDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	if (ControlIntegerDetailsView)
	{
		ControlIntegerDetailsView->SetObjects(InObjects);
	}
}
void SControlRigDetails::SetEnumDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	if (ControlVectorDetailsView)
	{
		ControlVectorDetailsView->SetObjects(InObjects);
	}
}

void SControlRigDetails::SetVectorDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	if (ControlEnumDetailsView)
	{
		ControlEnumDetailsView->SetObjects(InObjects);
	}
}

void SControlRigDetails::SetVector2DDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	if (ControlVector2DDetailsView)
	{
		ControlVector2DDetailsView->SetObjects(InObjects);
	}
}
bool SControlRigDetails::IsPropertyKeyable(const UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const
{
	if (InObjectClass && InObjectClass->IsChildOf(UControlRigTransformNoScaleControlProxy::StaticClass()) && InObjectClass->IsChildOf(UControlRigEulerTransformControlProxy::StaticClass()) && InPropertyHandle.GetProperty()
		&& InPropertyHandle.GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformControlProxy, Transform))
	{
		return true;
	}
	FCanKeyPropertyParams CanKeyPropertyParams(InObjectClass, InPropertyHandle);
	ISequencer* Sequencer = GetSequencer();
	if (Sequencer && Sequencer->CanKeyProperty(CanKeyPropertyParams))
	{
		return true;
	}

	return false;
}

bool SControlRigDetails::IsPropertyKeyingEnabled() const
{
	ISequencer* Sequencer = GetSequencer();
	if (Sequencer && Sequencer->GetFocusedMovieSceneSequence())
	{
		return true;
	}

	return false;
}

bool SControlRigDetails::IsPropertyAnimated(const IPropertyHandle& PropertyHandle, UObject *ParentObject) const
{
	ISequencer* Sequencer = GetSequencer();
	if (Sequencer && Sequencer->GetFocusedMovieSceneSequence())
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

void SControlRigDetails::OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle)
{
	ISequencer* Sequencer = GetSequencer();
	if (Sequencer && !Sequencer->IsAllowedToChange())
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

bool SControlRigDetails::ShouldShowPropertyOnDetailCustomization(const FPropertyAndParent& InPropertyAndParent) const
{
	auto ShouldPropertyBeVisible = [](const FProperty& InProperty)
	{
		bool bShow = InProperty.HasAnyPropertyFlags(CPF_Interp) || InProperty.HasMetaData(FRigVMStruct::InputMetaName) || InProperty.HasMetaData(FRigVMStruct::OutputMetaName);

		// Always show settings properties
		const UClass* OwnerClass = InProperty.GetOwner<UClass>();
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

bool SControlRigDetails::IsReadOnlyPropertyOnDetailCustomization(const FPropertyAndParent& InPropertyAndParent) const
{
	auto ShouldPropertyBeEnabled = [](const FProperty& InProperty)
	{
		bool bShow = InProperty.HasAnyPropertyFlags(CPF_Interp) || InProperty.HasMetaData(FRigVMStruct::InputMetaName);

		// Always show settings properties
		const UClass* OwnerClass = InProperty.GetOwner<UClass>();
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


#undef LOCTEXT_NAMESPACE
