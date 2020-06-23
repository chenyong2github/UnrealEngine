// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VCamModifier.h"
#include "VCamOutputProvider.h"
#include "Subsystems/EngineSubsystem.h"
#include "EditorInputProcessor.h"
#include "EditorInputTypes.h"

#include "VCamCoreSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVCamCore, Log, All);

UCLASS()
class VCAMCORE_API UVCamCoreSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:

	UVCamCoreSubsystem();
	~UVCamCoreSubsystem();

	// Set whether the Subsystem is actively driving a camera
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void SetActive(const bool InActive);

	// Check whether the Subsystem is actively driving a camera
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	bool IsActive() const { return bIsActive; };

	// Set whether the output system is active
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void SetOutputActive(const bool InOutputActive);

	// Check whether the output system is active
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	bool IsOutputActive() const { return bIsOutputActive; };

	// Add a modifier to the stack with a given name.
	// If that name is already in use then the modifier will not be added.
	// Returns the created modifier if the Add succeeded
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void AddModifier(const FName& Name, const TSubclassOf<UVCamModifier> ModifierClass, bool& bSuccess, UVCamModifier*& CreatedModifier);

	// Tries to find a Modifier in the Stack with the given name.
	// The returned Modifier must be checked before it is used.
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	UVCamModifier* FindModifier(const FName& Name) const;

	// Remove all Modifiers from the Stack.
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void ClearModifiers();


	// Specify which Camera the system should control when running.
	// If set to nothing then a default camera actor will be spawned.
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void SetTargetCamera(UCineCameraComponent* InTargetCamera);


	// Specify the name of the Live Link subject to use as input to the Modifier Stack
	// If the Subject is not valid then the default camera blueprint data struct will be used
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void SetLiveLinkSubject(const FLiveLinkSubjectName& SubjectName);

	// Sets the UMG class for the OutputProvider
	// @todo Needs to support multiple outputs
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
    void SetUMGClassForOutput(const TSubclassOf<UUserWidget> InUMGClass);
	

public:
	// By default the editor will use gamepads to control the editor camera
	// Setting this to true will prevent this
	UFUNCTION(BlueprintCallable, Category="Input")
	void SetShouldConsumeGamepadInput(const bool bInShouldConsumeGamepadInput);

	UFUNCTION(BlueprintPure, Category="Input")
    bool GetShouldConsumeGamepadInput() const;
	
	UFUNCTION(BlueprintCallable, Category="Input")
	void BindKeyDownEvent(const FKey Key, FKeyInputDelegate Delegate);

	UFUNCTION(BlueprintCallable, Category="Input")
    void BindKeyUpEvent(const FKey Key, FKeyInputDelegate Delegate);

	UFUNCTION(BlueprintCallable, Category="Input")
    void BindAnalogEvent(const FKey Key, FAnalogInputDelegate Delegate);

	UFUNCTION(BlueprintCallable, Category="Input")
    void BindMouseMoveEvent(FPointerInputDelegate Delegate);

	UFUNCTION(BlueprintCallable, Category="Input")
    void BindMouseButtonDownEvent(const FKey Key, FPointerInputDelegate Delegate);
	
	UFUNCTION(BlueprintCallable, Category="Input")
    void BindMouseButtonUpEvent(const FKey Key, FPointerInputDelegate Delegate);
	
	UFUNCTION(BlueprintCallable, Category="Input")
    void BindMouseDoubleClickEvent(const FKey Key, FPointerInputDelegate Delegate);

	UFUNCTION(BlueprintCallable, Category="Input")
    void BindMouseWheelEvent(FPointerInputDelegate Delegate);

private:
	// Whether the input processor was successfully registered
	bool bIsRegisterd = false;

private:
	void Tick();

	void SpawnDefaultCamera();

	void GetInitialLiveLinkData(FLiveLinkCameraBlueprintData& InitialLiveLinkData);
	void CopyLiveLinkDataToCamera(FLiveLinkCameraBlueprintData& LiveLinkData, UCineCameraComponent* CameraComponent);

	UCineCameraComponent* GetActiveCamera() const;

	double LastEvaluationTime = -1.0;

	// Gets the time since GetDeltaTime was called last.
	// Calling this updates LastEvaluationTime
	float GetDeltaTime();

	UPROPERTY(Transient)
	bool bIsActive = false;

	UPROPERTY(Transient)
	bool bIsOutputActive = false;
	
	UPROPERTY()
	TArray<FModifierStackEntry> ModifierStack;

	UPROPERTY()
	FLiveLinkSubjectName LiveLinkSubjectName;
	
	UPROPERTY()
	TSoftObjectPtr<UCineCameraComponent> TargetCamera;

	UPROPERTY(Transient)
	AActor* DefaultCameraActor = nullptr;

	UPROPERTY(Transient)
	UVCamOutputProvider* OutputProvider = nullptr;

	FTransform NewCameraTransform;

	TSharedPtr<FEditorInputProcessor> InputProcessor;
};

