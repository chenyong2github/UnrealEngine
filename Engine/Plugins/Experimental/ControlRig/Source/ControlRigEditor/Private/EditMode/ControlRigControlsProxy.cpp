// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigControlsProxy.h"
#include "EditorModeManager.h"
#include "ControlRigEditMode.h"
#include "Sequencer/ControlRigSequence.h"
#include "Rigs/RigControlHierarchy.h"

#include "Components/SkeletalMeshComponent.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "MovieSceneCommonHelpers.h"
#include "PropertyHandle.h"

#if WITH_EDITOR

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "SEnumCombobox.h"

void FControlRigEnumControlProxyValueDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TArray<UObject*> Objects;
	InStructPropertyHandle->GetOuterObjects(Objects);
	ensure(Objects.Num() == 1); // This is in here to ensure we are only showing the modifier details in the blueprint editor

	for (UObject* Object : Objects)
	{
		if (Object->IsA<UControlRigEnumControlProxy>())
		{
			ProxyBeingCustomized = Cast<UControlRigEnumControlProxy>(Object);
		}
	}

	check(ProxyBeingCustomized);

	HeaderRow
	.NameContent()
	[
		InStructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SEnumComboBox, ProxyBeingCustomized->Enum.EnumType)
		.OnEnumSelectionChanged(SEnumComboBox::FOnEnumSelectionChanged::CreateSP(this, &FControlRigEnumControlProxyValueDetails::OnEnumValueChanged, InStructPropertyHandle))
		.CurrentValue(this, &FControlRigEnumControlProxyValueDetails::GetEnumValue)
		.Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
	];
}

void FControlRigEnumControlProxyValueDetails::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

int32 FControlRigEnumControlProxyValueDetails::GetEnumValue() const
{
	if (ProxyBeingCustomized)
	{
		return ProxyBeingCustomized->Enum.EnumIndex;
	}
	return 0;
}

void FControlRigEnumControlProxyValueDetails::OnEnumValueChanged(int32 InValue, ESelectInfo::Type InSelectInfo, TSharedRef<IPropertyHandle> InStructHandle){
	if (ProxyBeingCustomized)
	{
		ProxyBeingCustomized->Enum.EnumIndex = InValue;
		InStructHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	}
}

#endif

void UControlRigControlsProxy::SelectionChanged(bool bInSelected)
{
	if (RigControl)
	{
		Modify();
		const FName PropertyName("bSelected");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		Binding.CallFunction<bool>(*this, bInSelected);
	}
}

void UControlRigControlsProxy::CheckEditModeOnSelectionChange(UControlRig *InControlRig)
{
#if WITH_EDITOR
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		if (ControlRigEditMode->GetControlRig(false) != InControlRig)
		{
			ControlRigEditMode->SetObjects(InControlRig, nullptr, nullptr);
		}
	}
#endif
}

void UControlRigControlsProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigControlsProxy, bSelected))
	{
		if (RigControl && ControlRig.IsValid())
		{
			if (bSelected)
			{
				CheckEditModeOnSelectionChange(ControlRig.Get());
			}
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SelectControl(ControlName, bSelected);
			ControlRig->Evaluate_AnyThread();
		}
	}
}


#if WITH_EDITOR
void UControlRigControlsProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
	}
}
#endif


void UControlRigTransformControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformControlProxy, Transform))
		|| (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformControlProxy, Transform)))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<FTransform>(ControlName, Transform, true, EControlRigSetKey::DoNotCare);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigTransformControlProxy::ValueChanged()
{
	if (RigControl)
	{
		Modify();
		const FName PropertyName("Transform");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		FTransform NewTransform = RigControl->Value.Get<FTransform>();

		Binding.CallFunction<FTransform>(*this, NewTransform);

	}
}

#if WITH_EDITOR
void UControlRigTransformControlProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FTransform>(ControlName, Transform, true, EControlRigSetKey::Never);
	}
}
#endif

void UControlRigTransformControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{

	if (RigControl)
	{
		ControlRig->SetControlValue<FTransform>(ControlName, Transform, true, EControlRigSetKey::Always);
	}
}

void UControlRigTransformNoScaleControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformNoScaleControlProxy, Transform))
		|| (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformNoScaleControlProxy, Transform)))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<FTransformNoScale>(ControlName, Transform, true, EControlRigSetKey::DoNotCare);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigTransformNoScaleControlProxy::ValueChanged()
{
	if (RigControl)
	{
		Modify();
		const FName PropertyName("Transform");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		FTransformNoScale NewTransform = RigControl->Value.Get<FTransformNoScale>();
		Binding.CallFunction<FTransformNoScale>(*this, NewTransform);
	}
}

#if WITH_EDITOR
void UControlRigTransformNoScaleControlProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FTransformNoScale>(ControlName, Transform, true, EControlRigSetKey::Never);
	}
}
#endif

void UControlRigTransformNoScaleControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	if (RigControl)
	{
		ControlRig->SetControlValue<FTransformNoScale>(ControlName, Transform, true, EControlRigSetKey::Always);
	}
}


void UControlRigEulerTransformControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigEulerTransformControlProxy, Transform))
		|| (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigEulerTransformControlProxy, Transform)))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<FEulerTransform>(ControlName, Transform, true, EControlRigSetKey::DoNotCare);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigEulerTransformControlProxy::ValueChanged()
{
	if (RigControl)
	{
		Modify();
		const FName PropertyName("Transform");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		FEulerTransform NewTransform = RigControl->Value.Get<FEulerTransform>();

		Binding.CallFunction<FEulerTransform>(*this, NewTransform);

	}
}

#if WITH_EDITOR
void UControlRigEulerTransformControlProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FEulerTransform>(ControlName, Transform, true, EControlRigSetKey::Never);
	}
}
#endif

void UControlRigEulerTransformControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	if (RigControl)
	{
		ControlRig->SetControlValue<FEulerTransform>(ControlName, Transform, true, EControlRigSetKey::Always);
	}
}

void UControlRigFloatControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigFloatControlProxy, Float))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<float>(ControlName, Float, true, EControlRigSetKey::DoNotCare);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigFloatControlProxy::ValueChanged()
{
	if (RigControl)
	{
		Modify();
		const FName PropertyName("Float");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		float Val = RigControl->Value.Get<float>();

		Binding.CallFunction<float>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigFloatControlProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<float>(ControlName, Float, true, EControlRigSetKey::Never);
	}
}
#endif


void UControlRigFloatControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	if (RigControl)
	{
		ControlRig->SetControlValue<float>(ControlName, Float, true, EControlRigSetKey::Always);
	}
}

void UControlRigIntegerControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigIntegerControlProxy, Integer))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<int32>(ControlName, Integer, true, EControlRigSetKey::DoNotCare);
			ControlRig->Evaluate_AnyThread();
		}
	}
}

void UControlRigIntegerControlProxy::ValueChanged()
{
	if (RigControl)
	{
		Modify();
		const FName PropertyName("Integer");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		int32 Val = RigControl->Value.Get<int32>();

		Binding.CallFunction<int32>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigIntegerControlProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<int32>(ControlName, Integer, true, EControlRigSetKey::Never);
	}
}
#endif

void UControlRigIntegerControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	if (RigControl)
	{
		ControlRig->SetControlValue<int32>(ControlName, Integer, true, EControlRigSetKey::Always);
	}
}

void UControlRigEnumControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigEnumControlProxy, Enum))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<int32>(ControlName, Enum.EnumIndex, true, EControlRigSetKey::DoNotCare);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigEnumControlProxy::ValueChanged()
{
	if (RigControl)
	{
		Modify();
		const FName PropertyName("Enum");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());

		FControlRigEnumControlProxyValue Val;
		Val.EnumType = RigControl->ControlEnum;
		Val.EnumIndex = RigControl->Value.Get<int32>();

		Binding.CallFunction<FControlRigEnumControlProxyValue>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigEnumControlProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<int32>(ControlName, Enum.EnumIndex, true, EControlRigSetKey::Never);
	}
}
#endif

void UControlRigEnumControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	if (RigControl)
	{
		ControlRig->SetControlValue<int32>(ControlName, Enum.EnumIndex, true, EControlRigSetKey::Always);
	}
}

void UControlRigVectorControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigVectorControlProxy, Vector))
		|| (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigVectorControlProxy, Vector)))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<FVector>(ControlName, Vector, true, EControlRigSetKey::DoNotCare);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigVectorControlProxy::ValueChanged()
{
	if (RigControl)
	{
		Modify();
		const FName PropertyName("Vector");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		FVector Val = RigControl->Value.Get<FVector>();

		Binding.CallFunction<FVector>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigVectorControlProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FVector>(ControlName, Vector, true, EControlRigSetKey::Never);
	}
}
#endif


void UControlRigVectorControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	if (RigControl)
	{
		ControlRig->SetControlValue<FVector>(ControlName, Vector, true, EControlRigSetKey::Always);
	}
}

void UControlRigVector2DControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigVector2DControlProxy, Vector2D))
		|| ((PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigVector2DControlProxy, Vector2D))))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<FVector2D>(ControlName, Vector2D, true, EControlRigSetKey::DoNotCare);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigVector2DControlProxy::ValueChanged()
{
	if (RigControl)
	{
		Modify();
		const FName PropertyName("Vector2D");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		FVector2D Val = RigControl->Value.Get<FVector2D>();

		Binding.CallFunction<FVector2D>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigVector2DControlProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FVector2D>(ControlName, Vector2D, true, EControlRigSetKey::Never);
	}
}
#endif


void UControlRigVector2DControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	if (RigControl)
	{
		ControlRig->SetControlValue<FVector2D>(ControlName, Vector2D, true, EControlRigSetKey::Always);
	}
}


void UControlRigBoolControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigBoolControlProxy, Bool))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<bool>(ControlName, Bool, true, EControlRigSetKey::DoNotCare);
			ControlRig->Evaluate_AnyThread();
		}
	}
}

void UControlRigBoolControlProxy::ValueChanged()
{
	if (RigControl)
	{
		Modify();
		const FName PropertyName("Bool");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		bool Val = RigControl->Value.Get<bool>();

		Binding.CallFunction<bool>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigBoolControlProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<bool>(ControlName, Bool, true, EControlRigSetKey::Never);
	}
}
#endif


void UControlRigBoolControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	if (RigControl)
	{
		ControlRig->SetControlValue<bool>(ControlName, Bool, true, EControlRigSetKey::Always);
	}
}


//////UControlDetailPanelControlProxies////////

UControlRigControlsProxy* UControlRigDetailPanelControlProxies::FindProxy(const FName& Name) const
{
	UControlRigControlsProxy* const* Proxy = AllProxies.Find(Name);
	if (Proxy &&  Proxy[0])
	{
		return Proxy[0];
	}
	return nullptr;
}

void UControlRigDetailPanelControlProxies::AddProxy(const FName& Name, UControlRig* ControlRig, FRigControl* RigControl)
{
	UControlRigControlsProxy* Proxy = FindProxy(Name);
	if (!Proxy && RigControl != nullptr)
	{
		switch(RigControl->ControlType)
		{
			case ERigControlType::Transform:
			{
				Proxy = NewObject<UControlRigTransformControlProxy>(GetTransientPackage(), NAME_None);
				break;

			}
			case ERigControlType::TransformNoScale:
			{
				Proxy = NewObject<UControlRigTransformNoScaleControlProxy>(GetTransientPackage(), NAME_None);
				break;

			}
			case ERigControlType::EulerTransform:
			{
				Proxy = NewObject<UControlRigEulerTransformControlProxy>(GetTransientPackage(), NAME_None);
				break;
			}
			case ERigControlType::Float:
			{
				Proxy = NewObject<UControlRigFloatControlProxy>(GetTransientPackage(), NAME_None);
				break;

			}
			case ERigControlType::Integer:
			{
				if(RigControl->ControlEnum == nullptr)
				{
					Proxy = NewObject<UControlRigIntegerControlProxy>(GetTransientPackage(), NAME_None);
				}
				else
				{
					UControlRigEnumControlProxy* EnumProxy = NewObject<UControlRigEnumControlProxy>(GetTransientPackage(), NAME_None);
					EnumProxy->Enum.EnumType = RigControl->ControlEnum;
					Proxy = EnumProxy;
				}
				break;

			}
			case ERigControlType::Position:
			case ERigControlType::Rotator:
			case ERigControlType::Scale:
			{
				Proxy = NewObject<UControlRigVectorControlProxy>(GetTransientPackage(), NAME_None);
				break;

			}
			case ERigControlType::Vector2D:
			{
				Proxy = NewObject<UControlRigVector2DControlProxy>(GetTransientPackage(), NAME_None);
				break;

			}
			case ERigControlType::Bool:
			{
				Proxy = NewObject<UControlRigBoolControlProxy>(GetTransientPackage(), NAME_None);
				break;

			}
			default:
				break;
		}
		if (Proxy)
		{
			Proxy->SetFlags(RF_Transactional);
			Proxy->SetName(Name);
			Proxy->RigControl = RigControl;
			Proxy->ControlRig = ControlRig;
			Proxy->ValueChanged();
			AllProxies.Add(Name, Proxy);
		}

	}
}

void UControlRigDetailPanelControlProxies::RemoveProxy(const FName& Name)
{
	UControlRigControlsProxy* ExistingProxy = FindProxy(Name);
	if (ExistingProxy)
	{
		ExistingProxy->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders);
		ExistingProxy->MarkPendingKill();
	}
	AllProxies.Remove(Name);
}

void UControlRigDetailPanelControlProxies::RemoveAllProxies()
{
	for (TPair<FName, UControlRigControlsProxy * >Pair : AllProxies)
	{
		UControlRigControlsProxy* ExistingProxy = Pair.Value;
		ExistingProxy->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders);
		ExistingProxy->MarkPendingKill();
	}
	AllProxies.Empty();
	SelectedProxies.SetNum(0);
}

void UControlRigDetailPanelControlProxies::RecreateAllProxies(UControlRig* InControlRig)
{
	RemoveAllProxies();
	const TArray<FRigControl>& Controls = InControlRig->AvailableControls();
	for (const FRigControl& RigControl : Controls)
	{
		FRigControl* PRigControl = const_cast<FRigControl*>(&RigControl);
		if(PRigControl->bGizmoEnabled && PRigControl->bAnimatable)
		{
			AddProxy(RigControl.Name, InControlRig, PRigControl);
		}
	}
}

void UControlRigDetailPanelControlProxies::ProxyChanged(const FName& Name)
{
	UControlRigControlsProxy* Proxy = FindProxy(Name);
	if (Proxy)
	{
		Modify();
		Proxy->ValueChanged();
	}
}

void UControlRigDetailPanelControlProxies::SelectProxy(const FName& Name, bool bSelected)
{
	UControlRigControlsProxy* Proxy = FindProxy(Name);
	if (Proxy)
	{
		Modify();
		if (bSelected)
		{
			if (!SelectedProxies.Contains(Proxy))
			{
				//don't show more than 5 for performance
				if (SelectedProxies.Num() < 5)
				{
					SelectedProxies.Add(Proxy);
				}
			}
		}
		else
		{
			SelectedProxies.Remove(Proxy);
		}
		Proxy->SelectionChanged(bSelected);
	}
}

