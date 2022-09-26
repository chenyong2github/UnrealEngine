// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectPreviewScene.h"

FCustomizableObjectPreviewScene::FCustomizableObjectPreviewScene(ConstructionValues CVS, float InFloorOffset)
	: FAdvancedPreviewScene(CVS, InFloorOffset)
{

}


FCustomizableObjectPreviewScene::~FCustomizableObjectPreviewScene()
{

}


UStaticMeshComponent* FCustomizableObjectPreviewScene::GetSkyComponent()
{
	return SkyComponent;
}
