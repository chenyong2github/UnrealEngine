// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRigConstraint Runtime Implementation
 *
 */

#include "IKRigConstraint.h"

void UIKRigConstraint::Setup(FIKRigTransformModifier& InOutTransformModifier)
{
	SetupInternal(InOutTransformModifier);
	bInitialized = true;
}

void UIKRigConstraint::SetAndApplyConstraint(FIKRigTransformModifier& InOutTransformModifier)
{
	if (!bInitialized)
	{
		Setup(InOutTransformModifier);
	}

	Apply(InOutTransformModifier, nullptr);
}
