// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VCamOutputProviderBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CompositingElement.h"

#if WITH_EDITOR
#endif

#include "VCamOutputComposure.generated.h"

UCLASS(meta = (DisplayName = "Composure"))
class VCAMCORE_API UVCamOutputComposure : public UVCamOutputProviderBase
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
	TArray<TSoftObjectPtr<ACompositingElement>> LayerTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	UTextureRenderTarget2D* RenderTarget;

protected:
	virtual void CreateUMG() override;

private:
};
