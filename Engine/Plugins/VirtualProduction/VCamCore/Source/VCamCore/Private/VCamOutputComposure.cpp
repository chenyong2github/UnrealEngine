// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamOutputComposure.h"

#if WITH_EDITOR
#endif

void UVCamOutputComposure::InitializeSafe()
{
	Super::InitializeSafe();
}

void UVCamOutputComposure::Destroy()
{
	Super::Destroy();
}

void UVCamOutputComposure::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void UVCamOutputComposure::SetActive(const bool InActive)
{
	Super::SetActive(InActive);
}

void UVCamOutputComposure::CreateUMG()
{
	DisplayType = EVPWidgetDisplayType::Composure;

	// Super must be here so that the UMGWidget is already created
	Super::CreateUMG();

	if (UMGWidget)
	{
		UMGWidget->PostProcessDisplayType.ComposureLayerTargets.Empty();

		for (TSoftObjectPtr<ACompositingElement> Layer : LayerTargets)
		{
			if (Layer.IsValid())
			{
				UMGWidget->PostProcessDisplayType.ComposureLayerTargets.Emplace(Layer.Get());
			}
		}
	}
	else
	{
		UE_LOG(LogVCamOutputProvider, Error, TEXT("Composure mode - Super::CreateUMG must be called first!"));
	}
}

#if WITH_EDITOR
void UVCamOutputComposure::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;

	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		static FName NAME_LayerTargets = GET_MEMBER_NAME_CHECKED(UVCamOutputComposure, LayerTargets);
		static FName NAME_RenderTarget = GET_MEMBER_NAME_CHECKED(UVCamOutputComposure, RenderTarget);

		if (Property->GetFName() == NAME_LayerTargets ||
			Property->GetFName() == NAME_RenderTarget)
		{
			if (bIsActive)
			{
				SetActive(false);
				SetActive(true);
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
