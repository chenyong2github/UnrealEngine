// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VCamOutputProviderBase.h"
#include "ImageProviders/RemoteSessionMediaOutput.h"
#include "IRemoteSessionRole.h"
#include "RemoteSession.h"
#include "Slate/SceneViewport.h"
#include "Engine/TextureRenderTarget2D.h"

#if WITH_EDITOR
#include "LevelEditorViewport.h"
#endif

#include "VCamOutputRemoteSession.generated.h"

UCLASS(meta = (DisplayName = "RemoteSession"))
class VCAMCORE_API UVCamOutputRemoteSession : public UVCamOutputProviderBase
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
	int32 PortNumber = IRemoteSessionModule::kDefaultPort;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	bool bUseRenderTargetFromComposure = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (EditCondition = "bUseRenderTargetFromComposure"))
	UTextureRenderTarget2D* ComposureRenderTarget = nullptr;

protected:
	virtual void CreateUMG() override;

	UPROPERTY(Transient)
	URemoteSessionMediaOutput* MediaOutput = nullptr;

	UPROPERTY(Transient)
	URemoteSessionMediaCapture* MediaCapture = nullptr;

private:
	TSharedPtr<IRemoteSessionUnmanagedRole> RemoteSessionHost;

	FHitResult LastViewportTouchResult;

	void CreateRemoteSession();
	void DestroyRemoteSession();

	void FindSceneViewport(TWeakPtr<SWindow>& OutInputWindow, TWeakPtr<FSceneViewport>& OutSceneViewport) const;

	void OnRemoteSessionChannelChange(IRemoteSessionRole* Role, TWeakPtr<IRemoteSessionChannel> Channel, ERemoteSessionChannelChange Change);
	void OnImageChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance);
	void OnInputChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance);
	void OnTouchEventOutsideUMG(const FVector2D& InViewportPosition);

	bool DeprojectScreenToWorld(const FVector2D& InScreenPosition, FVector& OutWorldPosition, FVector& OutWorldDirection) const;

#if WITH_EDITOR
	FLevelEditorViewportClient* GetLevelViewportClient() const;
	FViewport* GetActiveViewport() const;
#endif
};
