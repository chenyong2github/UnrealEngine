// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigCurveContainer.h"
#include "ControlRig.h"
#include "HelperUtil.h"
#include "Animation/Skeleton.h"

////////////////////////////////////////////////////////////////////////////////
// FRigCurveContainer
////////////////////////////////////////////////////////////////////////////////

FRigCurveContainer::FRigCurveContainer()
	: Container(nullptr)
	, bSuspendNotifications(false)
{
}

FRigCurveContainer& FRigCurveContainer::operator= (const FRigCurveContainer &InOther)
{
#if WITH_EDITOR
	if (!bSuspendNotifications)
	{
		for (int32 Index = Num() - 1; Index >= 0; Index--)
		{
			FRigCurve CurveToRemove = Curves[Index];
			OnCurveRemoved.Broadcast(Container, FRigElementKey(CurveToRemove.Name, ERigElementType::Curve));
		}
	}
#endif

	Curves.Reset();
	Curves.Append(InOther.Curves);
	NameToIndexMapping.Reset();
	RefreshMapping();

#if WITH_EDITOR
	if (!bSuspendNotifications)
	{
		for (const FRigCurve& CurveAdded : Curves)
		{
			OnCurveAdded.Broadcast(Container, FRigElementKey(CurveAdded.Name, ERigElementType::Curve));
		}
	}
#endif

	return *this;
}

FName FRigCurveContainer::GetSafeNewName(const FName& InPotentialNewName) const
{
	FName Name = InPotentialNewName;
	int32 Suffix = 1;
	while(!IsNameAvailable(Name))
	{
		Name = *FString::Printf(TEXT("%s_%d"), *InPotentialNewName.ToString(), ++Suffix);
	}
	return Name;
}

FRigCurve& FRigCurveContainer::Add(const FName& InNewName, float InValue)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FRigCurve NewCurve;
	NewCurve.Name = GetSafeNewName(InNewName);
	NewCurve.Value = InValue;
	FName NewCurveName = NewCurve.Name;
	Curves.Add(NewCurve);
	RefreshMapping();

#if WITH_EDITOR
	if (!bSuspendNotifications)
	{
		OnCurveAdded.Broadcast(Container, NewCurve.GetElementKey());
	}
#endif

	int32 Index = GetIndex(NewCurveName);
	return Curves[Index];
}

FRigCurve FRigCurveContainer::Remove(const FName& InName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FRigCurve RemovedCurve;

	int32 IndexToDelete = GetIndex(InName);
	ensure(IndexToDelete != INDEX_NONE);
	Select(InName, false);
	RemovedCurve = Curves[IndexToDelete];
	Curves.RemoveAt(IndexToDelete);
	RefreshMapping();

#if WITH_EDITOR
	if (!bSuspendNotifications)
	{
		OnCurveRemoved.Broadcast(Container, RemovedCurve.GetElementKey());
	}
#endif

	return RemovedCurve;
}

FName FRigCurveContainer::GetName(int32 InIndex) const
{
	if (Curves.IsValidIndex(InIndex))
	{
		return Curves[InIndex].Name;
	}

	return NAME_None;
}

int32 FRigCurveContainer::GetIndexSlow(const FName& InName) const
{
	for (int32 Index = 0; Index < Curves.Num(); ++Index)
	{
		if (Curves[Index].Name == InName)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void FRigCurveContainer::SetValue(const FName& InName, const float InValue)
{
	SetValue(GetIndex(InName), InValue);
}

void FRigCurveContainer::SetValue(int32 InIndex, const float InValue)
{
	if (Curves.IsValidIndex(InIndex))
	{
		FRigCurve& Curve = Curves[InIndex];
		Curve.Value = InValue;
	}
}

float FRigCurveContainer::GetValue(const FName& InName) const
{
	return GetValue(GetIndex(InName));
}

float FRigCurveContainer::GetValue(int32 InIndex) const
{
	if (Curves.IsValidIndex(InIndex))
	{
		return Curves[InIndex].Value;
	}

	return 0.f;
}

FName FRigCurveContainer::Rename(const FName& InOldName, const FName& InNewName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (InOldName != InNewName)
	{
		const int32 Found = GetIndex(InOldName);
		if (Found != INDEX_NONE)
		{
			FName NewName = GetSafeNewName(InNewName);

			bool bWasSelected = IsSelected(InOldName);
			if(bWasSelected)
			{
				Select(InOldName, false);
			}

			Curves[Found].Name = NewName;
			RefreshMapping();

#if WITH_EDITOR
			if (!bSuspendNotifications)
			{
				OnCurveRenamed.Broadcast(Container, RigElementType(), InOldName, NewName);
			}
#endif
			if(bWasSelected)
			{
				Select(NewName, true);
			}
			return NewName;
		}
	}
	
	return NAME_None;
}

void FRigCurveContainer::RefreshMapping()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	NameToIndexMapping.Empty();
	for (int32 Index = 0; Index < Curves.Num(); ++Index)
	{
		Curves[Index].Index = Index;
		NameToIndexMapping.Add(Curves[Index].Name, Index);
	}
}

void FRigCurveContainer::Initialize()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	RefreshMapping();
	ResetValues();
}

void FRigCurveContainer::Reset()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Curves.Reset();
}

void FRigCurveContainer::ResetValues()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// update parent index
	for (int32 Index = 0; Index < Curves.Num(); ++Index)
	{
		Curves[Index].Value = 0.f;
	}
}

bool FRigCurveContainer::Select(const FName& InName, bool bSelect)
{
	if(GetIndex(InName) == INDEX_NONE)
	{
		return false;
	}

	if(bSelect == IsSelected(InName))
	{
		return false;
	}

	if(bSelect)
	{
		Selection.Add(InName);
	}
	else
	{
		Selection.Remove(InName);
	}

	if (!bSuspendNotifications)
	{
		OnCurveSelected.Broadcast(Container, FRigElementKey(InName, RigElementType()), bSelect);
	}

	return true;
}

bool FRigCurveContainer::ClearSelection()
{
	TArray<FName> TempSelection;
	TempSelection.Append(Selection);
	for(const FName& SelectedName : TempSelection)
	{
		Select(SelectedName, false);
	}
	return TempSelection.Num() > 0;
}

TArray<FName> FRigCurveContainer::CurrentSelection() const
{
	TArray<FName> TempSelection;
	TempSelection.Append(Selection);
	return TempSelection;
}

bool FRigCurveContainer::IsSelected(const FName& InName) const
{
	return Selection.Contains(InName);
}

#if WITH_EDITOR

TArray<FRigElementKey> FRigCurveContainer::ImportCurvesFromSkeleton(const USkeleton* InSkeleton, const FName& InNameSpace, bool bRemoveObsoleteCurves, bool bSelectCurves, bool bNotify)
{
	check(InSkeleton);

	TGuardValue<bool> SuspendNotification(bSuspendNotifications, !bNotify);

	TArray<FRigElementKey> Keys;

	const FSmartNameMapping* SmartNameMapping = InSkeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);

	TArray<FName> NameArray;
	SmartNameMapping->FillNameArray(NameArray);
	for (int32 Index = 0; Index < NameArray.Num(); ++Index)
	{
		FName Name = NameArray[Index];
		if (!InNameSpace.IsNone())
		{
			Name = *FString::Printf(TEXT("%s::%s"), *InNameSpace.ToString(), *Name.ToString());
		}

		if (GetIndexSlow(Name) == INDEX_NONE)
		{
			Add(Name);
		}

		Select(Name, true);
		Keys.Add(FRigElementKey(Name, ERigElementType::Curve));
	}

	return Keys;
}


#endif

FRigPose FRigCurveContainer::GetPose() const
{
	FRigPose Pose;
	AppendToPose(Pose);
	return Pose;
}

void FRigCurveContainer::SetPose(FRigPose& InPose)
{
	for(FRigPoseElement& Element : InPose)
	{
		if(Element.Index.GetKey().Type == ERigElementType::Curve)
		{
			if(Element.Index.UpdateCache(Container))
			{
				Curves[Element.Index.GetIndex()].Value = Element.CurveValue;
			}
		}
	}
}

void FRigCurveContainer::AppendToPose(FRigPose& InOutPose) const
{
	for(const FRigCurve& Curve : Curves)
	{
		FRigPoseElement Element;
		if(Element.Index.UpdateCache(Curve.GetElementKey(), Container))
		{
			Element.CurveValue = Curve.Value;
			InOutPose.Elements.Add(Element);
		}
	}
}