// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigDefines.h"
#include "PropertyPathHelpers.h"

FControlRigOperator FControlRigOperator::MakeUnresolvedCopy(const FControlRigOperator& ToCopy)
{
	FControlRigOperator Op;
	Op.OpCode = ToCopy.OpCode;
	Op.CachedPropertyPath1 = FCachedPropertyPath::MakeUnresolvedCopy(ToCopy.CachedPropertyPath1);
	Op.CachedPropertyPath2 = FCachedPropertyPath::MakeUnresolvedCopy(ToCopy.CachedPropertyPath2);
	return Op;
}

bool FControlRigOperator::Resolve(UObject* OuterObject)
{
	if (CachedPropertyPath1.IsValid() && !CachedPropertyPath1.IsFullyResolved())
	{
		if (!CachedPropertyPath1.Resolve(OuterObject))
		{
			return false;
		}
	}
	if (CachedPropertyPath2.IsValid() && !CachedPropertyPath2.IsFullyResolved())
	{
		if (!CachedPropertyPath2.Resolve(OuterObject))
		{
			return false;
		}
	}
	return true;
}
