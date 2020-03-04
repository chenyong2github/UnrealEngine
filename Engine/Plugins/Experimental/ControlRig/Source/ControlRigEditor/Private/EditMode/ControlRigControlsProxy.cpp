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

void UControlRigControlsProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigControlsProxy, bSelected))
	{
		if (RigControl && ControlRig.IsValid())
		{
			ControlRig->SelectControl(ControlName, bSelected);
		}
	}
}


#if WITH_EDITOR
void UControlRigControlsProxy::PostEditUndo()
{
	if (RigControl && ControlRig.IsValid() && ControlRig->GetControlHierarchy().GetIndex(ControlName) != INDEX_NONE)
	{
		ControlRig->SelectControl(ControlName, bSelected);
	}
}
#endif


void UControlRigTransformControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformControlProxy, Transform))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			ControlRig->SetControlValue<FTransform>(ControlName, Transform, true, EControlRigSetKey::DoNotCare);
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
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FTransform>(ControlName, Transform, true, EControlRigSetKey::DoNotCare);
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
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformControlProxy, Transform))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			ControlRig->SetControlValue<FTransformNoScale>(ControlName, Transform, true, EControlRigSetKey::DoNotCare);
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
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FTransformNoScale>(ControlName, Transform, true, EControlRigSetKey::DoNotCare);
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

void UControlRigFloatControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigFloatControlProxy, Float))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			ControlRig->SetControlValue<float>(ControlName, Float, true, EControlRigSetKey::DoNotCare);
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
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<float>(ControlName, Float, true, EControlRigSetKey::DoNotCare);
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


void UControlRigVectorControlProxy::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if ((PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigVectorControlProxy, Vector))
		|| (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigVectorControlProxy, Vector)))
	{
		if (RigControl && ControlRig.IsValid())
		{
			//MUST set through ControlRig
			ControlRig->SetControlValue<FVector>(ControlName, Vector, true, EControlRigSetKey::DoNotCare);
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
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FVector>(ControlName, Vector, true, EControlRigSetKey::DoNotCare);
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
			ControlRig->SetControlValue<FVector2D>(ControlName, Vector2D, true, EControlRigSetKey::DoNotCare);
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
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<FVector2D>(ControlName, Vector2D, true, EControlRigSetKey::DoNotCare);
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
			ControlRig->SetControlValue<bool>(ControlName, Bool, true, EControlRigSetKey::DoNotCare);
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
		ControlRig->SelectControl(ControlName, bSelected);
		ControlRig->SetControlValue<bool>(ControlName, Bool, true, EControlRigSetKey::DoNotCare);
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
		switch (RigControl->ControlType)
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
		case ERigControlType::Float:
		{
			Proxy = NewObject<UControlRigFloatControlProxy>(GetTransientPackage(), NAME_None);
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

