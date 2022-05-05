// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaProcessImagesOptions.h"

UImgMediaProcessImagesOptions::UImgMediaProcessImagesOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MipLevelTints.Add(FLinearColor::Red);
	MipLevelTints.Add(FLinearColor::Green);
	MipLevelTints.Add(FLinearColor::Blue);
	MipLevelTints.Add(FLinearColor::Yellow);
	MipLevelTints.Add(FLinearColor(0.0f, 1.0f, 1.0f));
	MipLevelTints.Add(FLinearColor(1.0f, 0.0f, 1.0f));
}

