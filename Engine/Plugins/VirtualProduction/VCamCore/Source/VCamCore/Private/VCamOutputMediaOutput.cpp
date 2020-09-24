// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamOutputMediaOutput.h"

void UVCamOutputMediaOutput::Activate()
{
	StartCapturing();

	Super::Activate();
}

void UVCamOutputMediaOutput::Deactivate()
{
	StopCapturing();

	Super::Deactivate();
}

void UVCamOutputMediaOutput::StartCapturing()
{
	if (OutputConfig)
	{
		MediaCapture = OutputConfig->CreateMediaCapture();
		if (MediaCapture)
		{
			TSharedPtr<FSceneViewport> SceneViewport = GetTargetSceneViewport();
			if (SceneViewport.IsValid())
			{
				FMediaCaptureOptions Options;
				Options.bResizeSourceBuffer = true;
				MediaCapture->CaptureSceneViewport(SceneViewport, Options);
			}
			else
			{
				UE_LOG(LogVCamOutputProvider, Warning, TEXT("MediaOutput mode failed to find valid SceneViewport"));
			}
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
