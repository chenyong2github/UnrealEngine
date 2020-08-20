// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VCamOutputProviderBase.h"

#if WITH_EDITOR
#endif

#include "VCamOutputViewport.generated.h"

UCLASS(meta = (DisplayName = "Viewport"))
class VCAMCORE_API UVCamOutputViewport : public UVCamOutputProviderBase
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

protected:
	virtual void CreateUMG() override;

private:
};
