// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CineCameraComponent.h"
#include "EVCamTargetViewportID.h"
#include "VPFullScreenUserWidget.h"
#include "VCamOutputProviderBase.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVCamOutputProvider, Log, All);

class UUserWidget;
class UVPFullScreenUserWidget;
class SWindow;
class FSceneViewport;

#if WITH_EDITOR
class FLevelEditorViewportClient;
#endif

UCLASS(BlueprintType, Abstract, EditInlineNew)
class VCAMCORE_API UVCamOutputProviderBase : public UObject
{
	GENERATED_BODY()
public:

	DECLARE_MULTICAST_DELEGATE_OneParam(FActivationDelegate, bool /*bNewIsActive*/);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActivationDelegate_Blueprint, bool, bNewIsActive);
	FActivationDelegate OnActivatedDelegate; 
	/** Called when the activation state of this output provider changes. */
	UPROPERTY(BlueprintAssignable, meta = (DisplayName = "OnActivated"))
	FActivationDelegate_Blueprint OnActivatedDelegate_Blueprint;

	/** Override the default output resolution with a custom value - NOTE you must toggle bIsActive off then back on for this to take effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (DisplayPriority = "5"))
	bool bUseOverrideResolution = false;

	/** When bUseOverrideResolution is set, use this custom resolution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (DisplayPriority = "6"), meta = (EditCondition = "bUseOverrideResolution", ClampMin = 1))
	FIntPoint OverrideResolution = { 2048, 1536 };

	UVCamOutputProviderBase();
	
	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface

	// Called when the provider is brought online such as after instantiating or loading a component containing this provider 
	// Use Initialize for any setup logic that needs to survive between Start / Stop cycles such as spawning transient objects 
	// 
	// If bForceInitialization is true then it will force a reinitialization even if the provider was already initialized
	virtual void Initialize();

	// Called when the provider is being shutdown such as before changing level or on exit
	virtual void Deinitialize();

	// Called when the provider is Activated
	virtual void Activate();

	// Called when the provider is Deactivated
	virtual void Deactivate();

	virtual void Tick(const float DeltaTime);

	// Called to turn on or off this output provider
	UFUNCTION(BlueprintCallable, Category = "Output")
	void SetActive(const bool bInActive);
	// Returns if this output provider is currently active or not
	UFUNCTION(BlueprintPure, Category = "Output")
	bool IsActive() const { return bIsActive; };

	// Returns if this output provider has been initialized or not
	UFUNCTION(BlueprintPure, Category = "Output")
	bool IsInitialized() const { return bInitialized; };

	UFUNCTION(BlueprintCallable, Category = "Output")
	void SetTargetCamera(const UCineCameraComponent* InTargetCamera);

	UFUNCTION(BlueprintPure, Category = "Output")
	EVCamTargetViewportID GetTargetViewport() const { return TargetViewport; }
	UFUNCTION(BlueprintCallable, Category = "Output")
	void SetTargetViewport(EVCamTargetViewportID Value) { TargetViewport = Value; }
	
	UFUNCTION(BlueprintPure, Category = "Output")
	TSubclassOf<UUserWidget> GetUMGClass() const { return UMGClass; }
	UFUNCTION(BlueprintCallable, Category = "Output")
	void SetUMGClass(const TSubclassOf<UUserWidget> InUMGClass) { UMGClass = InUMGClass; }

	UVPFullScreenUserWidget* GetUMGWidget() { return UMGWidget; };

	/** Temporarily disable the output.  Caller must eventually call RestoreOutput. */
	void SuspendOutput();
	/** Restore the output state from previous call to disable output. */
	void RestoreOutput();
	
	/** @return Whether this output provider should requires the viewport to be locked to the camera in order to function correctly. */
	bool NeedsForceLockToViewport() const;

	/** Calls the VCamModifierInterface on the widget if it exists and also requests any child VCam Widgets to reconnect */
	void NotifyWidgetOfComponentChange() const;

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif


	static FName GetIsActivePropertyName()			{ return GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, bIsActive); }
	static FName GetTargetViewportPropertyName()	{ return GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, TargetViewport); }
	static FName GetUMGClassPropertyName()			{ return GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, UMGClass); }
	
protected:
	
	// If set, this output provider will execute every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (DisplayPriority = "1"))
	bool bIsActive = false;

	UPROPERTY(Transient)
	bool bInitialized = false;

	UPROPERTY(Transient)
	EVPWidgetDisplayType DisplayType = EVPWidgetDisplayType::PostProcess;
	
	UPROPERTY(Transient)
	TObjectPtr<UVPFullScreenUserWidget> UMGWidget = nullptr;

	virtual void CreateUMG();

	/** Whether this subclass supports override resolutions. */
	virtual bool ShouldOverrideResolutionOnActivationEvents() const { return false; }
	/** Removes the override resolution from the given viewport. */
	void RestoreOverrideResolutionForViewport(EVCamTargetViewportID ViewportToRestore);
	/** Applies OverrideResolution to the passed in viewport - bUseOverrideResolution was already checked. */
	void ApplyOverrideResolutionForViewport(EVCamTargetViewportID Viewport);
	void ReapplyOverrideResolution(EVCamTargetViewportID Viewport);

	void DisplayUMG();
	void DestroyUMG();

	UVCamOutputProviderBase* GetOtherOutputProviderByIndex(int32 Index) const;

	/** Gets the scene viewport identified by the currently configured TargetViewport. */
	TSharedPtr<FSceneViewport> GetTargetSceneViewport() const { return GetSceneViewport(TargetViewport); }
	/** Gets the viewport identified by the passed in parameters. */
	TSharedPtr<FSceneViewport> GetSceneViewport(EVCamTargetViewportID InTargetViewport) const;
	TWeakPtr<SWindow> GetTargetInputWindow() const;

#if WITH_EDITOR
	FLevelEditorViewportClient* GetTargetLevelViewportClient() const;
	TSharedPtr<SLevelViewport> GetTargetLevelViewport() const;
#endif

private:

	/** Which viewport to use for this VCam */
	UPROPERTY(EditAnywhere, BlueprintGetter = "GetTargetViewport", BlueprintSetter = "SetTargetViewport", Category = "Output", meta = (DisplayPriority = "2"))
	EVCamTargetViewportID TargetViewport = EVCamTargetViewportID::CurrentlySelected;
	
	/** The UMG class to be rendered in this output provider */
	UPROPERTY(EditAnywhere, BlueprintGetter = "GetUMGClass", BlueprintSetter = "SetUMGClass", Category = "Output", meta = (DisplayName="UMG Overlay", DisplayPriority = "3"))
	TSubclassOf<UUserWidget> UMGClass;
	
	UPROPERTY(Transient)
	TSoftObjectPtr<UCineCameraComponent> TargetCamera;
	
	UPROPERTY(Transient)
	bool bWasActive = false;

#if WITH_EDITORONLY_DATA
	/** Used for saving widget remapping settings across enabling & disabling in the same session, see FOutputProviderLayoutCustomization. */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> SavedConnectionRemappingData = nullptr;
#endif
	
	bool IsOuterComponentEnabled() const;
};
