// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigCurveContainer.h"
#include "ControlRig.h"
#include "HelperUtil.h"

////////////////////////////////////////////////////////////////////////////////
// FRigCurveContainer
////////////////////////////////////////////////////////////////////////////////

FRigCurveContainer::FRigCurveContainer()
#if WITH_EDITOR
	:Container(nullptr)
#endif
{
}

FRigCurveContainer& FRigCurveContainer::operator= (const FRigCurveContainer &InOther)
{
#if WITH_EDITOR
	for (int32 Index = Num() - 1; Index >= 0; Index--)
	{
		FRigCurve CurveToRemove = Curves[Index];
		OnCurveRemoved.Broadcast(Container, RigElementType(), CurveToRemove.Name);
	}
#endif

	Curves.Reset();
	Curves.Append(InOther.Curves);
	NameToIndexMapping.Reset();
	RefreshMapping();

#if WITH_EDITOR
	for (const FRigCurve& CurveAdded : Curves)
	{
		OnCurveAdded.Broadcast(Container, RigElementType(), CurveAdded.Name);
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

FRigCurve& FRigCurveContainer::Add(const FName& InNewName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FRigCurve NewCurve;
	NewCurve.Name = GetSafeNewName(InNewName);
	NewCurve.Value = 0.f;
	Curves.Add(NewCurve);
	RefreshMapping();

#if WITH_EDITOR
	OnCurveAdded.Broadcast(Container, RigElementType(), NewCurve.Name);
#endif

	int32 Index = GetIndex(NewCurve.Name);
	return Curves[Index];
}

FRigCurve FRigCurveContainer::Remove(const FName& InName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FRigCurve RemovedCurve;

	int32 IndexToDelete = GetIndex(InName);
	ensure(IndexToDelete != INDEX_NONE);
	RemovedCurve = Curves[IndexToDelete];
	Curves.RemoveAt(IndexToDelete);
	RefreshMapping();

#if WITH_EDITOR
	OnCurveRemoved.Broadcast(Container, RigElementType(), RemovedCurve.Name);
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
			Curves[Found].Name = NewName;
			RefreshMapping();

#if WITH_EDITOR
			OnCurveRenamed.Broadcast(Container, RigElementType(), InOldName, NewName);
#endif
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

