// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRigConstraint Runtime Implementation
 *
 */

#include "IKRigConstraint.h"

void UIKRigConstraint::Setup(const FIKRigTransformModifier& InOutTransformModifier)
{
	SetupInternal(InOutTransformModifier);
	bInitialized = true;
}

void UIKRigConstraint::SetAndApplyConstraint(FIKRigTransformModifier& InOutTransformModifier)
{
	if (!bInitialized)
	{
		Setup(const_cast<FIKRigTransformModifier&>(InOutTransformModifier));
	}

	Apply(InOutTransformModifier, nullptr);
}
