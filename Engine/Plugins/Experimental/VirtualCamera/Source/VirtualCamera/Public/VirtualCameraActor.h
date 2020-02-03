// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "GameFramework/Actor.h"
#include "IRemoteSessionRole.h"
#include "IVirtualCameraController.h"
#include "LiveLinkRole.h"
#include "Templates/UniquePtr.h"
#if WITH_EDITOR
#include "UnrealEdMisc.h"
#endif

#include "VirtualCameraActor.generated.h"

struct FVirtualCameraViewportSettings;
class IRemoteSessionUnmanagedRole;
class UCineCameraComponent;
class URemoteSessionMediaCapture;
class URemoteSessionMediaOutput;
class UUserWidget;
class UVirtualCameraMovement;
class UVPFullScreenUserWidget;
class UWorld;

#if WITH_EDITOR
class UBlueprint;
struct FAssetData;
struct FCanDeleteAssetResult;
#endif

USTRUCT(BlueprintType)
struct FVirtualCameraTransform
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera")
	FTransform Transform;
};


UCLASS(Blueprintable, BlueprintType, Category="VirtualCamera", DisplayName="VirtualCameraActor")
class VIRTUALCAMERA_API AVirtualCameraActor : public AActor
{
	GENERATED_BODY()

public:

	AVirtualCameraActor(const FObjectInitializer& ObjectInitializer);
	AVirtualCameraActor(FVTableHelper& Helper);
	~AVirtualCameraActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Component")
	UCineCameraComponent* StreamedCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Component")
	UCineCameraComponent* RecordingCamera;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VirtualCamera | Movement")
	FLiveLinkSubjectRepresentation LiveLinkSubject;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Movement")
	UVirtualCameraMovement* MovementComponent;

	UPROPERTY(Transient, EditDefaultsOnly, Category = "VirtualCamera | MediaOutput")
	URemoteSessionMediaOutput* MediaOutput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera | UMG")
	TSubclassOf<UUserWidget> CameraUMGclass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Streaming")
	FVector2D ViewportResolution;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Streaming")
	int32 RemoteSessionPort;

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Streaming")
	bool StartStreaming();

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Streaming")
	bool StopStreaming();

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera | Streaming")
	bool IsStreaming() const;

	DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(FVirtualCameraTransform, FPreSetVirtualCameraTransform, FVirtualCameraTransform, CameraTransform);
	/**
	 * Delegate that will is triggered before transform is set onto Actor.
	 * @param FVirtualCameraTransform Transform data that is passed to delegate.
	 * @return FVirtualCameraTransform Manipulated transform that will be set onto Actor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Events, meta = (IsBindableEvent = "True"))
	FPreSetVirtualCameraTransform OnPreSetVirtualCameraTransform;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVirtualCameraTickDelegate, float, DeltaTime);
	/**
	 * This delegate is triggered at the end of a tick in editor/pie/game.
	 * @note The Actor is only ticked while it is being streamed.
	 * @param float Delta Time in seconds.
	 */
	UPROPERTY(BlueprintAssignable, Category = "LiveLink")
	FVirtualCameraTickDelegate OnVirtualCameraUpdated;

protected:

	UPROPERTY(EditDefaultsOnly, Category = "VirtualCamera | UMG")
	UVPFullScreenUserWidget* CameraScreenWidget;

	UPROPERTY(Transient, EditDefaultsOnly, Category = "VirtualCamera | MediaOutput")
	URemoteSessionMediaCapture* MediaCapture;

	UPROPERTY(Transient)
	UWorld* ActorWorld;

	UPROPERTY(Transient)
	USceneComponent* DefaultSceneRoot;

	UPROPERTY(Transient)
	AActor* PreviousViewTarget;

public:

	//~ Begin AActor interface
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif
	virtual void Tick(float DeltaSeconds) override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

private:

	void OnImageChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance, const FString& Type, ERemoteSessionChannelMode Mode);
	void OnInputChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance, const FString& Type, ERemoteSessionChannelMode Mode);

#if WITH_EDITOR
	void OnMapChanged(UWorld* World, EMapChangeType ChangeType);
	void OnBlueprintPreCompile(UBlueprint* Blueprint);
	void OnPrepareToCleanseEditorObject(UObject* Object);
	void OnAssetRemoved(const FAssetData& AssetData);
	void OnAssetsCanDelete(const TArray<UObject*>& InAssetsToDelete, FCanDeleteAssetResult& CanDeleteResult);
#endif

private:

	bool bIsStreaming;
	TSharedPtr<IRemoteSessionUnmanagedRole> RemoteSessionHost;
	TUniquePtr<FVirtualCameraViewportSettings> ViewportSettingsBackup;
};
