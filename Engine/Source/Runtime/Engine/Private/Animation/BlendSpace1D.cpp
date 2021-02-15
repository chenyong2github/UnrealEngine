// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BlendSpace1D.cpp: 1D BlendSpace functionality
=============================================================================*/ 

#include "Animation/BlendSpace1D.h"

UBlendSpace1D::UBlendSpace1D(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DimensionIndices = { 0 };
}

EBlendSpaceAxis UBlendSpace1D::GetAxisToScale() const
{
	return bScaleAnimation ? BSA_X : BSA_None;
}

