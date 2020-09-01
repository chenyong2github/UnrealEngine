// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigHierarchyModifier.h"
#include "ControlRigBlueprint.h"

UControlRigHierarchyModifier::UControlRigHierarchyModifier()
{
	Container = nullptr;
}

#if WITH_EDITOR

TArray<FRigElementKey> UControlRigHierarchyModifier::GetElements() const
{
	TArray<FRigElementKey> Elements;
	if (Container != nullptr)
	{
		return Container->GetAllItems(false /* sort */);
	}
	return Elements;
}

FRigElementKey UControlRigHierarchyModifier::AddBone(const FName& InNewName, const FName& InParentName, ERigBoneType InType)
{
	if(Container != nullptr)
	{
		return Container->BoneHierarchy.Add(InNewName, InParentName, InType).GetElementKey();
	}
	return FRigElementKey();
}

FRigBone UControlRigHierarchyModifier::GetBone(const FRigElementKey& InKey)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Bone)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->BoneHierarchy.Bones[Index];
		}
	}
	return InvalidBone;
}

void UControlRigHierarchyModifier::SetBone(const FRigBone& InElement)
{
	if (Container != nullptr)
	{
		int32 Index = Container->GetIndex(InElement.GetElementKey());
		if (Index != INDEX_NONE)
		{
			Container->BoneHierarchy.Bones[Index] = InElement;
			Container->BoneHierarchy.Initialize();

			if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()))
			{
				Blueprint->PropagateHierarchyFromBPToInstances(true, true);
			}
		}
	}
}

FRigElementKey UControlRigHierarchyModifier::AddControl(
	const FName& InNewName,
	ERigControlType InControlType,
	const FName& InParentName,
	const FName& InSpaceName,
	const FName& InGizmoName,
	const FLinearColor& InGizmoColor
)
{
	if(Container != nullptr)
	{
		return Container->ControlHierarchy.Add(InNewName, InControlType, InParentName, InSpaceName, FTransform::Identity, FRigControlValue(), InGizmoName, FTransform::Identity, InGizmoColor).GetElementKey();
	}
	return FRigElementKey();
}


FRigControl UControlRigHierarchyModifier::GetControl(const FRigElementKey& InKey)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->ControlHierarchy.Controls[Index];
		}
	}
	return InvalidControl;
}

void UControlRigHierarchyModifier::SetControl(const FRigControl& InElement)
{
	if (Container != nullptr)
	{
		int32 Index = Container->GetIndex(InElement.GetElementKey());
		if (Index != INDEX_NONE)
		{
			Container->ControlHierarchy.Controls[Index] = InElement;
			Container->ControlHierarchy.Initialize();

			if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()))
			{
				Blueprint->PropagateHierarchyFromBPToInstances(true, true);
			}
		}
	}
}

bool UControlRigHierarchyModifier::GetControlValueBool(const FRigElementKey& InKey, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Get<bool>();
		}
	}
	return false;
}

int32 UControlRigHierarchyModifier::GetControlValueInt(const FRigElementKey& InKey, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Get<int32>();
		}
	}
	return 0;
}

float UControlRigHierarchyModifier::GetControlValueFloat(const FRigElementKey& InKey, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Get<float>();
		}
	}
	return 0.f;
}

FVector2D UControlRigHierarchyModifier::GetControlValueVector2D(const FRigElementKey& InKey, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Get<FVector2D>();
		}
	}
	return FVector2D::ZeroVector;
}

FVector UControlRigHierarchyModifier::GetControlValueVector(const FRigElementKey& InKey, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Get<FVector>();
		}
	}
	return FVector::ZeroVector;
}

FRotator UControlRigHierarchyModifier::GetControlValueRotator(const FRigElementKey& InKey, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Get<FRotator>();
		}
	}
	return FRotator::ZeroRotator;
}

FTransform UControlRigHierarchyModifier::GetControlValueTransform(const FRigElementKey& InKey, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->ControlHierarchy.Controls[Index].GetTransformFromValue(InValueType);
		}
	}
	return FTransform::Identity;
}

void UControlRigHierarchyModifier::SetControlValueBool(const FRigElementKey& InKey, bool InValue, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Set<bool>(InValue);
		}
	}
}

void UControlRigHierarchyModifier::SetControlValueInt(const FRigElementKey& InKey, int32 InValue, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Set<int32>(InValue);
		}
	}
}

void UControlRigHierarchyModifier::SetControlValueFloat(const FRigElementKey& InKey, float InValue, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Set<float>(InValue);
		}
	}
}

void UControlRigHierarchyModifier::SetControlValueVector2D(const FRigElementKey& InKey, FVector2D InValue, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Set<FVector2D>(InValue);
		}
	}
}

void UControlRigHierarchyModifier::SetControlValueVector(const FRigElementKey& InKey, FVector InValue, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Set<FVector>(InValue);
		}
	}
}

void UControlRigHierarchyModifier::SetControlValueRotator(const FRigElementKey& InKey, FRotator InValue, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			Container->ControlHierarchy.Controls[Index].GetValue(InValueType).Set<FRotator>(InValue);
		}
	}
}

void UControlRigHierarchyModifier::SetControlValueTransform(const FRigElementKey& InKey, FTransform InValue, ERigControlValueType InValueType)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Control)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			Container->ControlHierarchy.Controls[Index].SetValueFromTransform(InValue, InValueType);
		}
	}
}

FRigElementKey UControlRigHierarchyModifier::AddSpace
(
	const FName& InNewName,
	ERigSpaceType InSpaceType,
	const FName& InParentName
)
{
	if(Container != nullptr)
	{
		return Container->SpaceHierarchy.Add(InNewName, InSpaceType, InParentName).GetElementKey();
	}
	return FRigElementKey();
}

FRigSpace UControlRigHierarchyModifier::GetSpace(const FRigElementKey& InKey)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Space)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->SpaceHierarchy.Spaces[Index];
		}
	}
	return InvalidSpace;
}

void UControlRigHierarchyModifier::SetSpace(const FRigSpace& InElement)
{
	if (Container != nullptr)
	{
		int32 Index = Container->GetIndex(InElement.GetElementKey());
		if (Index != INDEX_NONE)
		{
			Container->SpaceHierarchy.Spaces[Index] = InElement;
			Container->SpaceHierarchy.Initialize();

			if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()))
			{
				Blueprint->PropagateHierarchyFromBPToInstances(true, true);
			}
		}
	}
}

FRigElementKey UControlRigHierarchyModifier::AddCurve(const FName& InNewName, float InValue)
{
	if(Container != nullptr)
	{
		return Container->CurveContainer.Add(InNewName, InValue).GetElementKey();
	}
	return FRigElementKey();
}

FRigCurve UControlRigHierarchyModifier::GetCurve(const FRigElementKey& InKey)
{
	if (Container != nullptr && InKey.Type == ERigElementType::Curve)
	{
		int32 Index = Container->GetIndex(InKey);
		if (Index != INDEX_NONE)
		{
			return Container->CurveContainer.Curves[Index];
		}
	}
	return InvalidCurve;
}

void UControlRigHierarchyModifier::SetCurve(const FRigCurve& InElement)
{
	if (Container != nullptr)
	{
		int32 Index = Container->GetIndex(InElement.GetElementKey());
		if (Index != INDEX_NONE)
		{
			Container->CurveContainer.Curves[Index] = InElement;
			Container->CurveContainer.Initialize();

			if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()))
			{
				Blueprint->PropagateHierarchyFromBPToInstances(true, true);
			}
		}
	}
}

bool UControlRigHierarchyModifier::RemoveElement(const FRigElementKey& InElement)
{
	if(Container == nullptr)
	{
		return false;
	}

	if (Container->GetIndex(InElement) == INDEX_NONE)
	{
		return false;
	}

	switch (InElement.Type)
	{
		case ERigElementType::Bone:
		{
			Container->BoneHierarchy.Remove(InElement.Name);
			return true;
		}
		case ERigElementType::Control:
		{
			Container->ControlHierarchy.Remove(InElement.Name);
			return true;
		}
		case ERigElementType::Space:
		{
			Container->SpaceHierarchy.Remove(InElement.Name);
			return true;
		}
		case ERigElementType::Curve:
		{
			Container->CurveContainer.Remove(InElement.Name);
			return true;
		}
		default:
		{
			ensure(false);
			break;
		}
	}
	return false;
}

FRigElementKey UControlRigHierarchyModifier::RenameElement(const FRigElementKey& InElement, const FName& InNewName)
{
	if(Container == nullptr)
	{
		return FRigElementKey();
	}

	if (Container->GetIndex(InElement) == INDEX_NONE)
	{
		return FRigElementKey();
	}

	switch (InElement.Type)
	{
		case ERigElementType::Bone:
		{
			FName NewName = Container->BoneHierarchy.Rename(InElement.Name, InNewName);
			return FRigElementKey(NewName, ERigElementType::Bone);
		}
		case ERigElementType::Control:
		{
			FName NewName = Container->ControlHierarchy.Rename(InElement.Name, InNewName);
			return FRigElementKey(NewName, ERigElementType::Control);
		}
		case ERigElementType::Space:
		{
			FName NewName = Container->SpaceHierarchy.Rename(InElement.Name, InNewName);
			return FRigElementKey(NewName, ERigElementType::Space);
		}
		case ERigElementType::Curve:
		{
			FName NewName = Container->CurveContainer.Rename(InElement.Name, InNewName);
			return FRigElementKey(NewName, ERigElementType::Curve);
		}
		default:
		{
			ensure(false);
			break;
		}
	}
	return FRigElementKey();
}

bool UControlRigHierarchyModifier::ReparentElement(const FRigElementKey& InElement, const FRigElementKey& InNewParent)
{
	if(Container == nullptr)
	{
		return false;
	}

	if (Container->GetIndex(InElement) == INDEX_NONE)
	{
		return false;
	}

	switch (InElement.Type)
	{
		case ERigElementType::Bone:
		{
			if (InNewParent.Type == ERigElementType::Bone)
			{
				return Container->BoneHierarchy.Reparent(InElement.Name, InNewParent.Name);
			}
			break;
		}
		case ERigElementType::Control:
		{
			if (InNewParent.Type == ERigElementType::Control)
			{
				if (InNewParent.Name != NAME_None)
				{
					Container->ControlHierarchy.SetSpace(InElement.Name, NAME_None);
				}
				return Container->ControlHierarchy.Reparent(InElement.Name, InNewParent.Name);
			}
			else if (InNewParent.Type == ERigElementType::Space)
			{
				Container->ControlHierarchy.SetSpace(InElement.Name, InNewParent.Name);
				return true;
			}
			break;
		}
		case ERigElementType::Space:
		{
			if (InNewParent.Name == NAME_None)
			{
				return Container->SpaceHierarchy.Reparent(InElement.Name, ERigSpaceType::Global, InNewParent.Name);
			}
			else if (InNewParent.Type == ERigElementType::Bone)
			{
				return Container->SpaceHierarchy.Reparent(InElement.Name, ERigSpaceType::Bone, InNewParent.Name);
			}
			else if (InNewParent.Type == ERigElementType::Control)
			{
				return Container->SpaceHierarchy.Reparent(InElement.Name, ERigSpaceType::Control, InNewParent.Name);
			}
			else if (InNewParent.Type == ERigElementType::Space)
			{
				return Container->SpaceHierarchy.Reparent(InElement.Name, ERigSpaceType::Space, InNewParent.Name);
			}
			break;
		}
		case ERigElementType::Curve:
		{
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}
	return false;
}

TArray<FRigElementKey> UControlRigHierarchyModifier::GetSelection() const
{
	TArray<FRigElementKey> Elements;
	if (Container != nullptr)
	{
		return Container->CurrentSelection();
	}
	return Elements;
}

bool UControlRigHierarchyModifier::Select(const FRigElementKey& InKey, bool bSelect)
{
	if(Container == nullptr)
	{
		return false;
	}
	return Container->Select(InKey, bSelect);
}

bool UControlRigHierarchyModifier::ClearSelection()
{
	if(Container == nullptr)
	{
		return false;
	}
	return Container->ClearSelection();
}

bool UControlRigHierarchyModifier::IsSelected(const FRigElementKey& InKey) const
{
	if(Container == nullptr)
	{
		return false;
	}
	return Container->IsSelected(InKey);
}

void UControlRigHierarchyModifier::Initialize(bool bResetTransforms)
{
	if(Container == nullptr)
	{
		return;
	}
	Container->Initialize(bResetTransforms);
}

void UControlRigHierarchyModifier::Reset()
{
	if(Container == nullptr)
	{
		return;
	}
	Container->Reset();
}

void UControlRigHierarchyModifier::ResetTransforms()
{
	if(Container == nullptr)
	{
		return;
	}
	Container->ResetTransforms();
}

FTransform UControlRigHierarchyModifier::GetInitialTransform(const FRigElementKey& InKey) const
{
	if(Container == nullptr)
	{
		return FTransform::Identity;
	}
	return Container->GetInitialTransform(InKey);
}

void UControlRigHierarchyModifier::SetInitialTransform(const FRigElementKey& InKey, const FTransform& InTransform)
{
	if(Container == nullptr)
	{
		return;
	}
	return Container->SetInitialTransform(InKey, InTransform);
}

FTransform UControlRigHierarchyModifier::GetInitialGlobalTransform(const FRigElementKey& InKey) const
{
	if(Container == nullptr)
	{
		return FTransform::Identity;
	}
	return Container->GetInitialGlobalTransform(InKey);

}

void UControlRigHierarchyModifier::SetInitialGlobalTransform(const FRigElementKey& InKey, const FTransform& InTransform)
{
	if(Container == nullptr)
	{
		return;
	}
	return Container->SetInitialGlobalTransform(InKey, InTransform);
}

FTransform UControlRigHierarchyModifier::GetLocalTransform(const FRigElementKey& InKey) const
{
	if(Container == nullptr)
	{
		return FTransform::Identity;
	}
	return Container->GetLocalTransform(InKey);

}

void UControlRigHierarchyModifier::SetLocalTransform(const FRigElementKey& InKey, const FTransform& InTransform)
{
	if(Container == nullptr)
	{
		return;
	}
	return Container->SetLocalTransform(InKey, InTransform);
}

FTransform UControlRigHierarchyModifier::GetGlobalTransform(const FRigElementKey& InKey) const
{
	if(Container == nullptr)
	{
		return FTransform::Identity;
	}
	return Container->GetGlobalTransform(InKey);

}

void UControlRigHierarchyModifier::SetGlobalTransform(const FRigElementKey& InKey, const FTransform& InTransform)
{
	if(Container == nullptr)
	{
		return;
	}
	return Container->SetGlobalTransform(InKey, InTransform);
}

FString UControlRigHierarchyModifier::ExportToText(const TArray<FRigElementKey>& InElementsToExport) const
{
	if(Container == nullptr)
	{
		return FString();
	}
	return Container->ExportToText(InElementsToExport);
}

TArray<FRigElementKey> UControlRigHierarchyModifier::ImportFromText(const FString& InContent, ERigHierarchyImportMode InImportMode, bool bSelectNewElements)
{
	if(Container == nullptr)
	{
		return TArray<FRigElementKey>();
	}
	return Container->ImportFromText(InContent, InImportMode, bSelectNewElements);
}

#endif