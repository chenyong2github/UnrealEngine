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

UCLASS(meta = (DisplayName = "Remote Session Output Provider"))
class VCAMCORE_API UVCamOutputRemoteSession : public UVCamOutputProviderBase
{
	GENERATED_BODY()

public:
	virtual void Initialize() override;
	virtual void Deinitialize() override;

	virtual void Activate() override;
	virtual void Deactivate() override;

	virtual void Tick(const float DeltaTime) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// Network port number - change this only if connecting multiple RemoteSession devices to the same PC
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	int32 PortNumber = IRemoteSessionModule::kDefaultPort;

	// Stream a separate render target instead of the default viewport (usually from Composure)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	bool bUseRenderTargetFromComposure = false;

	// TextureRenderTarget2D asset to stream
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (EditCondition = "bUseRenderTargetFromComposure"))
	UTextureRenderTarget2D* ComposureRenderTarget = nullptr;

protected:

	UPROPERTY(Transient)
	URemoteSessionMediaOutput* MediaOutput = nullptr;

	UPROPERTY(Transient)
	URemoteSessionMediaCapture* MediaCapture = nullptr;

private:
	TSharedPtr<IRemoteSessionUnmanagedRole> RemoteSessionHost;

	FHitResult LastViewportTouchResult;

	void CreateRemoteSession();
	void DestroyRemoteSession();

	void OnRemoteSessionChannelChange(IRemoteSessionRole* Role, TWeakPtr<IRemoteSessionChannel> Channel, ERemoteSessionChannelChange Change);
	void OnImageChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance);
	void OnInputChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance);
	void OnTouchEventOutsideUMG(const FVector2D& InViewportPosition);

	bool DeprojectScreenToWorld(const FVector2D& InScreenPosition, FVector& OutWorldPosition, FVector& OutWorldDirection) const;
};
