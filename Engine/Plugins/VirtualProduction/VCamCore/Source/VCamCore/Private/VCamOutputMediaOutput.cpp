// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamOutputMediaOutput.h"

#if WITH_EDITOR
#endif

void UVCamOutputMediaOutput::InitializeSafe()
{
	Super::InitializeSafe();
}

void UVCamOutputMediaOutput::Destroy()
{
	Super::Destroy();
}

void UVCamOutputMediaOutput::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void UVCamOutputMediaOutput::SetActive(const bool bInActive)
{
	if (bInActive)
	{
		StartCapturing();
	}
	else
	{
		StopCapturing();
	}

	Super::SetActive(bInActive);
}

void UVCamOutputMediaOutput::CreateUMG()
{
	Super::CreateUMG();
}

void UVCamOutputMediaOutput::StartCapturing()
{
	if (OutputConfig)
	{
		MediaCapture = OutputConfig->CreateMediaCapture();
		if (MediaCapture)
		{
			FMediaCaptureOptions Options;
			Options.bResizeSourceBuffer = true;
			MediaCapture->CaptureActiveSceneViewport(Options);
		}
		else
		{
			UE_LOG(LogVCamOutputProvider, Warning, TEXT("MediaOutput mode failed to create MediaCapture"));
		}
	}
	else
	{
		UE_LOG(LogVCamOutputProvider, Warning, TEXT("MediaOutput mode missing valid OutputConfig"));
	}
}

void UVCamOutputMediaOutput::StopCapturing()
{
	if (MediaCapture)
	{
		MediaCapture->StopCapture(false);
		MediaCapture->ConditionalBeginDestroy();
		MediaCapture = nullptr;
	}
}

#if WITH_EDITOR
void UVCamOutputMediaOutput::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;

	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		static FName NAME_OutputConfig = GET_MEMBER_NAME_CHECKED(UVCamOutputMediaOutput, OutputConfig);

		if (Property->GetFName() == NAME_OutputConfig)
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
