// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CurveContainer.h"
#include "ControlRig.h"
#include "HelperUtil.h"

////////////////////////////////////////////////////////////////////////////////
// FRigCurveContainer
////////////////////////////////////////////////////////////////////////////////

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

