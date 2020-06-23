// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImageProviders/RemoteSessionMediaOutput.h"
#include "IRemoteSessionRole.h"
#include "RemoteSession.h"
#include "CineCameraComponent.h"
#include "Slate/SceneViewport.h"
#include "VPFullScreenUserWidget.h"

#include "VCamOutputProvider.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVCamOutputProvider, Log, All);

class URemoteSessionMediaOutput;
class URemoteSessionMediaCapture;
class UUserWidget;
class UVPFullScreenUserWidget;

UCLASS()
class VCAMCORE_API UVCamOutputProvider : public UObject
{
	GENERATED_BODY()

public:
	UVCamOutputProvider();
	~UVCamOutputProvider();

	virtual void Initialize();
	virtual void Destroy();
	virtual void Tick(const float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Output")
	void SetActive(const bool InActive);

	UFUNCTION(BlueprintPure, Category = "VirtualCamera | Output")
	bool IsActive() const { return bIsActive; };

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Output")
	void SetTargetCamera(const UCineCameraComponent* InTargetCamera);

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Output")
    void SetUMGClass(const TSubclassOf<UUserWidget> InUMGClass);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Output")
	int32 RemoteSessionPort = IRemoteSessionModule::kDefaultPort;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Output")
    TSubclassOf<UUserWidget> UMGClass;

	UPROPERTY(Transient, EditDefaultsOnly, Category = "VirtualCamera | Output")
	URemoteSessionMediaOutput* MediaOutput = nullptr;

protected:
	UPROPERTY(Transient, EditDefaultsOnly, Category = "VirtualCamera | Output")
    UVPFullScreenUserWidget* UMGWidget;

	UPROPERTY(Transient, EditDefaultsOnly, Category = "VirtualCamera | Output")
	URemoteSessionMediaCapture* MediaCapture = nullptr;

private:
	UPROPERTY()
	bool bIsActive = true;

	TWeakObjectPtr<AActor> Backup_ActorLock;
	TWeakObjectPtr<AActor> Backup_ViewTarget;

	FHitResult LastViewportTouchResult;

	TSharedPtr<IRemoteSessionUnmanagedRole> RemoteSessionHost;

	TSoftObjectPtr<UCineCameraComponent> TargetCamera;

	void CreateUMG();

	void CreateRemoteSession();
	void DestroyRemoteSession();

	void FindSceneViewport(TWeakPtr<SWindow>& OutInputWindow, TWeakPtr<FSceneViewport>& OutSceneViewport);
		
	void OnImageChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance, const FString& Type, ERemoteSessionChannelMode Mode);
	void OnInputChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance, const FString& Type, ERemoteSessionChannelMode Mode);
	void OnTouchEventOutsideUMG(const FVector2D& InViewportPosition);

	bool DeprojectScreenToWorld(const FVector2D& InScreenPosition, FVector& OutWorldPosition, FVector& OutWorldDirection);
};
