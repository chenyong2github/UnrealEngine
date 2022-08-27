// Copyright Epic Games, Inc. All Rights Reserved.

#include "Options/GLTFPrebakeOptions.h"

UGLTFPrebakeOptions::UGLTFPrebakeOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ResetToDefault();
}

void UGLTFPrebakeOptions::ResetToDefault()
{
	DefaultMaterialBakeSize = EGLTFMaterialBakeSizePOT::POT_1024;
	DefaultMaterialBakeFilter = TF_Trilinear;
	DefaultMaterialBakeTiling = TA_Wrap;
}
