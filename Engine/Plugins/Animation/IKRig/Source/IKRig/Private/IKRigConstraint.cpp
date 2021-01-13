// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRigConstraint Runtime Implementation
 *
 */

#include "IKRigConstraint.h"

void UIKRigConstraint::Setup(const FIKRigTransforms& InOutTransformModifier)
{
	SetupInternal(InOutTransformModifier);
	bInitialized = true;
}

void UIKRigConstraint::SetAndApplyConstraint(FIKRigTransforms& InOutTransformModifier)
{
	if (!bInitialized)
	{
		Setup(const_cast<FIKRigTransforms&>(InOutTransformModifier));
	}

	Apply(InOutTransformModifier, nullptr);
}
