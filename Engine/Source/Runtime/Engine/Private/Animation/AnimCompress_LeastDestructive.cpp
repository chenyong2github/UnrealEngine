// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AnimCompress_LeastDestructive.cpp: Uses the Bitwise compressor with really light settings
=============================================================================*/ 

#include "Animation/AnimCompress_LeastDestructive.h"
#include "Animation/AnimCompress_BitwiseCompressOnly.h"

UAnimCompress_LeastDestructive::UAnimCompress_LeastDestructive(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Description = TEXT("Least Destructive");
	TranslationCompressionFormat = ACF_None;
	RotationCompressionFormat = ACF_Float96NoW;
}
