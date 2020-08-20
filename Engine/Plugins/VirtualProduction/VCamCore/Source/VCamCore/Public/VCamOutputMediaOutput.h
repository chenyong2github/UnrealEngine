// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VCamOutputProviderBase.h"
#include "MediaCapture.h"
#include "MediaOutput.h"

#if WITH_EDITOR
#endif

#include "VCamOutputMediaOutput.generated.h"

UCLASS(meta = (DisplayName = "MediaOutput"))
class VCAMCORE_API UVCamOutputMediaOutput: public UVCamOutputProviderBase
{
	GENERATED_BODY()

public:
	virtual void InitializeSafe() override;
	virtual void Destroy() override;
	virtual void Tick(const float DeltaTime) override;
	virtual void SetActive(const bool InActive) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	UMediaOutput* OutputConfig;

protected:
	virtual void CreateUMG() override;

	UPROPERTY(Transient)
	UMediaCapture* MediaCapture = nullptr;

private:
	void StartCapturing();
	void StopCapturing();
};
