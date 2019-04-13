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

void FControlRigOperator::Resolve(UObject* OuterObject)
{
	if (CachedPropertyPath1.IsValid() && !CachedPropertyPath1.IsFullyResolved())
	{
		CachedPropertyPath1.Resolve(OuterObject);
	}
	if (CachedPropertyPath2.IsValid() && !CachedPropertyPath2.IsFullyResolved())
	{
		CachedPropertyPath2.Resolve(OuterObject);
	}
}
