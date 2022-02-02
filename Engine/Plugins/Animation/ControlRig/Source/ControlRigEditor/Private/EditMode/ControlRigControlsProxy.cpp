// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigControlsProxy.h"
#include "EditorModeManager.h"
#include "ControlRigEditMode.h"
#include "Sequencer/ControlRigSequence.h"
#include "Rigs/RigHierarchy.h"

#include "Components/SkeletalMeshComponent.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "MovieSceneCommonHelpers.h"
#include "PropertyHandle.h"

#if WITH_EDITOR

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "SEnumCombo.h"


void UControlRigControlsProxy::SetIsMultiple(bool bIsVal)
{ 
	bIsMultiple = bIsVal; 
	if (bIsMultiple)
	{
		FString DisplayString = TEXT("Multiple");
		FName DisplayName(*DisplayString);
		Name = DisplayName;
	}
	else
	{
		Name = ControlName;
	}
}


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
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
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
		FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement && ControlRig.IsValid())
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
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid() && ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
	}
}
#endif

FRigControlElement* UControlRigControlsProxy::GetControlElement() const
{
	if(ControlRig.IsValid())
	{
		return ControlRig->GetHierarchy()->Find<FRigControlElement>(FRigElementKey(ControlName, ERigElementType::Control));
	}
	return nullptr;
}

void UControlRigTransformControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformControlProxy, Transform))
		|| (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformControlProxy, Transform)))
	{
		FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			FTransform RealTransform = Transform; //Transform is FEulerTransform
			ControlRig->SetControlValue<FRigControlValue::FTransform_Float>(ControlName, RealTransform, true, EControlRigSetKey::DoNotCare,false);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigTransformControlProxy::ValueChanged()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid())
	{
		Modify();
		const FName PropertyName("Transform");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		const FTransform NewTransform = ControlRig.Get()->GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransform_Float>().ToTransform();
		const FEulerTransform EulerTransform = NewTransform;
		Binding.CallFunction<FEulerTransform>(*this, EulerTransform);
	}
}

#if WITH_EDITOR
void UControlRigTransformControlProxy::PostEditUndo()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid() && ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		FTransform RealTransform = Transform; //Transform is FEulerTransform
		ControlRig->SetControlValue<FRigControlValue::FTransform_Float>(ControlName, RealTransform, true, EControlRigSetKey::Never,false);
	}
}
#endif

void UControlRigTransformControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		FTransform RealTransform = Transform; //Transform is FEulerTransform
		ControlRig->SetControlValue<FRigControlValue::FTransform_Float>(ControlName, RealTransform, true, EControlRigSetKey::Always,false);
	}
}

void UControlRigTransformNoScaleControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformNoScaleControlProxy, Transform))
		|| (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformNoScaleControlProxy, Transform)))
	{
		FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<FRigControlValue::FTransformNoScale_Float>(ControlName, Transform, true, EControlRigSetKey::DoNotCare,false);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigTransformNoScaleControlProxy::ValueChanged()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		Modify();
		const FName PropertyName("Transform");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		const FTransformNoScale NewTransform = ControlRig.Get()->GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransformNoScale_Float>().ToTransform();
		Binding.CallFunction<FTransformNoScale>(*this, NewTransform);
	}
}

#if WITH_EDITOR
void UControlRigTransformNoScaleControlProxy::PostEditUndo()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid() && ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FRigControlValue::FTransformNoScale_Float>(ControlName, Transform, true, EControlRigSetKey::Never,false);
	}
}
#endif

void UControlRigTransformNoScaleControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		ControlRig->SetControlValue<FRigControlValue::FTransformNoScale_Float>(ControlName, Transform, true, EControlRigSetKey::Always,false);
	}
}


void UControlRigEulerTransformControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigEulerTransformControlProxy, Transform))
		|| (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigEulerTransformControlProxy, Transform)))
	{
		FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<FRigControlValue::FEulerTransform_Float>(ControlName, Transform, true, EControlRigSetKey::DoNotCare,false);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigEulerTransformControlProxy::ValueChanged()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		Modify();
		const FName PropertyName("Transform");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		const FEulerTransform NewTransform = ControlRig.Get()->GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FRigControlValue::FEulerTransform_Float>().ToTransform();

		Binding.CallFunction<FEulerTransform>(*this, NewTransform);

	}
}

#if WITH_EDITOR
void UControlRigEulerTransformControlProxy::PostEditUndo()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid() && ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FRigControlValue::FEulerTransform_Float>(ControlName, Transform, true, EControlRigSetKey::Never,false);
	}
}
#endif

void UControlRigEulerTransformControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		ControlRig->SetControlValue<FRigControlValue::FEulerTransform_Float>(ControlName, Transform, true, EControlRigSetKey::Always,false);
	}
}

void UControlRigFloatControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigFloatControlProxy, Float))
	{
		FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<float>(ControlName, Float, true, EControlRigSetKey::DoNotCare,false);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigFloatControlProxy::ValueChanged()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		Modify();
		const FName PropertyName("Float");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		const float Val = ControlRig.Get()->GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Current).Get<float>();
		Binding.CallFunction<float>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigFloatControlProxy::PostEditUndo()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid() && ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<float>(ControlName, Float, true, EControlRigSetKey::Never,false);
	}
}
#endif


void UControlRigFloatControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		ControlRig->SetControlValue<float>(ControlName, Float, true, EControlRigSetKey::Always,false);
	}
}

void UControlRigIntegerControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigIntegerControlProxy, Integer))
	{
		FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<int32>(ControlName, Integer, true, EControlRigSetKey::DoNotCare,false);
			ControlRig->Evaluate_AnyThread();
		}
	}
}

void UControlRigIntegerControlProxy::ValueChanged()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		Modify();
		const FName PropertyName("Integer");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		const int32 Val = ControlRig.Get()->GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Current).Get<int32>();
		Binding.CallFunction<int32>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigIntegerControlProxy::PostEditUndo()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid() && ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<int32>(ControlName, Integer, true, EControlRigSetKey::Never,false);
	}
}
#endif

void UControlRigIntegerControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		ControlRig->SetControlValue<int32>(ControlName, Integer, true, EControlRigSetKey::Always,false);
	}
}

void UControlRigEnumControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigEnumControlProxy, Enum))
	{
		FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<int32>(ControlName, Enum.EnumIndex, true, EControlRigSetKey::DoNotCare,false);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigEnumControlProxy::ValueChanged()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		Modify();
		const FName PropertyName("Enum");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());

		FControlRigEnumControlProxyValue Val;
		Val.EnumType = ControlElement->Settings.ControlEnum;
		Val.EnumIndex = ControlRig.Get()->GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Current).Get<int32>();

		Binding.CallFunction<FControlRigEnumControlProxyValue>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigEnumControlProxy::PostEditUndo()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid() && ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<int32>(ControlName, Enum.EnumIndex, true, EControlRigSetKey::Never,false);
	}
}
#endif

void UControlRigEnumControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		ControlRig->SetControlValue<int32>(ControlName, Enum.EnumIndex, true, EControlRigSetKey::Always,false);
	}
}

void UControlRigVectorControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigVectorControlProxy, Vector))
		|| (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigVectorControlProxy, Vector)))
	{
		FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<FVector>(ControlName, Vector, true, EControlRigSetKey::DoNotCare,false);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigVectorControlProxy::ValueChanged()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		Modify();
		const FName PropertyName("Vector");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		const FVector Val = (FVector)ControlRig.Get()->GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
		Binding.CallFunction<FVector>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigVectorControlProxy::PostEditUndo()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid() && ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FVector3f>(ControlName, (FVector3f)Vector, true, EControlRigSetKey::Never,false);
	}
}
#endif


void UControlRigVectorControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		ControlRig->SetControlValue<FVector3f>(ControlName, (FVector3f)Vector, true, EControlRigSetKey::Always,false);
	}
}

void UControlRigVector2DControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigVector2DControlProxy, Vector2D))
		|| ((PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigVector2DControlProxy, Vector2D))))
	{
		FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<FVector3f>(ControlName, FVector3f(Vector2D.X, Vector2D.Y, 0.f), true, EControlRigSetKey::DoNotCare,false);
			ControlRig->Evaluate_AnyThread();

		}
	}
}

void UControlRigVector2DControlProxy::ValueChanged()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		Modify();
		const FName PropertyName("Vector2D");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		const FVector3f TempValue = ControlRig.Get()->GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Current).Get<FVector3f>();
		const FVector2D Val(TempValue.X, TempValue.Y);
		Binding.CallFunction<FVector2D>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigVector2DControlProxy::PostEditUndo()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid() && ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FVector3f>(ControlName, FVector3f(Vector2D.X, Vector2D.Y, 0.f), true, EControlRigSetKey::Never,false);
	}
}
#endif


void UControlRigVector2DControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		ControlRig->SetControlValue<FVector3f>(ControlName, FVector3f(Vector2D.X, Vector2D.Y, 0.f), true, EControlRigSetKey::Always,false);
	}
}


void UControlRigBoolControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigBoolControlProxy, Bool))
	{
		FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			FControlRigInteractionScope InteractionScope(ControlRig.Get());
			ControlRig->SetControlValue<bool>(ControlName, Bool, true, EControlRigSetKey::DoNotCare,false);
			ControlRig->Evaluate_AnyThread();
		}
	}
}

void UControlRigBoolControlProxy::ValueChanged()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		Modify();
		const FName PropertyName("Bool");
		FTrackInstancePropertyBindings Binding(PropertyName, PropertyName.ToString());
		const bool Val = ControlRig.Get()->GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Current).Get<bool>();
		Binding.CallFunction<bool>(*this, Val);
	}
}

#if WITH_EDITOR
void UControlRigBoolControlProxy::PostEditUndo()
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig.IsValid() && ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlName, ERigElementType::Control)))
	{
		if (bSelected)
		{
			CheckEditModeOnSelectionChange(ControlRig.Get());
		}
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<bool>(ControlName, Bool, true, EControlRigSetKey::Never,false);
	}
}
#endif


void UControlRigBoolControlProxy::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement)
	{
		ControlRig->SetControlValue<bool>(ControlName, Bool, true, EControlRigSetKey::Always,false);
	}
}


//////UControlDetailPanelControlProxies////////

UControlRigControlsProxy* UControlRigDetailPanelControlProxies::FindProxy(const FName& Name) const
{
	TObjectPtr<UControlRigControlsProxy> const* Proxy = AllProxies.Find(Name);
	if (Proxy &&  Proxy[0])
	{
		return Proxy[0];
	}
	return nullptr;
}

void UControlRigDetailPanelControlProxies::AddProxy(const FName& Name, UControlRig* ControlRig, FRigControlElement* ControlElement)
{
	UControlRigControlsProxy* Proxy = FindProxy(Name);
	if (!Proxy && ControlElement != nullptr)
	{
		switch(ControlElement->Settings.ControlType)
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
				if(ControlElement->Settings.ControlEnum == nullptr)
				{
					Proxy = NewObject<UControlRigIntegerControlProxy>(GetTransientPackage(), NAME_None);
				}
				else
				{
					UControlRigEnumControlProxy* EnumProxy = NewObject<UControlRigEnumControlProxy>(GetTransientPackage(), NAME_None);
					EnumProxy->Enum.EnumType = ControlElement->Settings.ControlEnum;
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
		ExistingProxy->MarkAsGarbage();
	}
	AllProxies.Remove(Name);
}

void UControlRigDetailPanelControlProxies::RemoveAllProxies()
{
	for (TPair<FName, TObjectPtr<UControlRigControlsProxy> >Pair : AllProxies)
	{
		UControlRigControlsProxy* ExistingProxy = Pair.Value;
		if (ExistingProxy)
		{
			ExistingProxy->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders);
			ExistingProxy->MarkAsGarbage();
		}
	}
	AllProxies.Empty();
	SelectedProxies.SetNum(0);
}

void UControlRigDetailPanelControlProxies::RecreateAllProxies(UControlRig* InControlRig)
{
	RemoveAllProxies();
	TArray<FRigControlElement*> Controls = InControlRig->AvailableControls();
	for (FRigControlElement* ControlElement : Controls)
	{
		if(ControlElement->Settings.bShapeEnabled && ControlElement->Settings.bAnimatable)
		{
			AddProxy(ControlElement->GetName(), InControlRig, ControlElement);
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

