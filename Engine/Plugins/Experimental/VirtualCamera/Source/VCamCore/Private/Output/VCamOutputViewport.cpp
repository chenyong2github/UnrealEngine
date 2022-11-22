// Copyright Epic Games, Inc. All Rights Reserved.

#include "Output/VCamOutputViewport.h"

void UVCamOutputViewport::CreateUMG()
{
	DisplayType = EVPWidgetDisplayType::Viewport;

	Super::CreateUMG();
}
