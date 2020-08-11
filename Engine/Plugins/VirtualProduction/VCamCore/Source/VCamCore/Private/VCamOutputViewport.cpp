// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamOutputViewport.h"

#if WITH_EDITOR
#endif

void UVCamOutputViewport::InitializeSafe()
{
	Super::InitializeSafe();
}

void UVCamOutputViewport::Destroy()
{
	Super::Destroy();
}

void UVCamOutputViewport::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void UVCamOutputViewport::SetActive(const bool InActive)
{
	Super::SetActive(InActive);
}

void UVCamOutputViewport::CreateUMG()
{
	DisplayType = EVPWidgetDisplayType::Viewport;

	Super::CreateUMG();
}

#if WITH_EDITOR
void UVCamOutputViewport::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;

	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		// Act on property changes here
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
