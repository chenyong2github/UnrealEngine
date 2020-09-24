// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Core/RigUnit_Name.h"
#include "Units/RigUnitContext.h"

FRigUnit_NameConcat_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if(A.IsNone())
	{
		Result = B;
	}
	else if(B.IsNone())
	{
		Result = A;
	}
	else
	{
		Result = *(A.ToString() + B.ToString());
	}
}

FRigUnit_NameTruncate_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Remainder = Name;
	Chopped = NAME_None;

    if(Name.IsNone() || Count <= 0)
    {
    	return;
    }

	FString String = Name.ToString();
	if (FromEnd)
	{
		Remainder = *String.LeftChop(Count);
		Chopped = *String.Right(Count);
	}
	else
	{
		Remainder = *String.RightChop(Count);
		Chopped = *String.Left(Count);
	}
}

FRigUnit_NameReplace_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Result = Name;

	if (!Old.IsNone() && !New.IsNone())
	{
		Result = *Name.ToString().Replace(*Old.ToString(), *New.ToString(), ESearchCase::CaseSensitive);
	}
}

FRigUnit_EndsWith_Execute()
{
	Result = Name.ToString().EndsWith(Ending.ToString());
}

FRigUnit_StartsWith_Execute()
{
	Result = Name.ToString().StartsWith(Start.ToString());
}

FRigUnit_Contains_Execute()
{
	Result = Name.ToString().Contains(Search.ToString());
}