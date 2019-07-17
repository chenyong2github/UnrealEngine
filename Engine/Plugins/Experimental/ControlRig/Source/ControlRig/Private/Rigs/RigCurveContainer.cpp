// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigCurveContainer.h"
#include "ControlRig.h"
#include "HelperUtil.h"

////////////////////////////////////////////////////////////////////////////////
// FRigCurveContainer
////////////////////////////////////////////////////////////////////////////////

FRigCurveContainer::FRigCurveContainer()
{
}

FRigCurveContainer& FRigCurveContainer::operator= (const FRigCurveContainer &InOther)
{
	/*
#if WITH_EDITOR
	for (int32 Index = Num() - 1; Index >= 0; Index--)
	{
		FRigSpace SpaceToRemove = Spaces[Index];
		OnSpaceRemoved.Broadcast(Container, ERigHierarchyElementType::Space, SpaceToRemove.Name);
	}
#endif
	*/

	Curves.Reset();
	Curves.Append(InOther.Curves);
	NameToIndexMapping.Reset();
	RefreshMapping();

	/*
#if WITH_EDITOR
	for (const FRigSpace& SpaceAdded : Spaces)
	{
		OnSpaceAdded.Broadcast(Container, ERigHierarchyElementType::Space, SpaceAdded.Name);
	}
#endif
	*/

	return *this;
}

void FRigCurveContainer::Add(const FName& InNewName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	int32 Found = GetIndex(InNewName);
	if (Found == INDEX_NONE)
	{
		FRigCurve NewCurve;
		NewCurve.Name = InNewName;
		NewCurve.Value = 0.f;
		Curves.Add(NewCurve);
		RefreshMapping();
	}
}

void FRigCurveContainer::Remove(const FName& InName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	int32 IndexToDelete = GetIndex(InName);
	if (IndexToDelete != INDEX_NONE)
	{
		Curves.RemoveAt(IndexToDelete);
		RefreshMapping();
	}
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

void FRigCurveContainer::Rename(const FName& InOldName, const FName& InNewName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (InOldName != InNewName)
	{
		const int32 Found = GetIndex(InOldName);
		const int32 NewNameFound = GetIndex(InNewName);
		// if I have new name, and didn't find new name
		if (Found != INDEX_NONE && NewNameFound == INDEX_NONE)
		{
			Curves[Found].Name = InNewName;
			RefreshMapping();
		}
	}
}

void FRigCurveContainer::RefreshMapping()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	NameToIndexMapping.Empty();
	for (int32 Index = 0; Index < Curves.Num(); ++Index)
	{
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

