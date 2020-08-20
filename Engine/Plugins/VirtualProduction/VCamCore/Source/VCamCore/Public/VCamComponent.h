// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorInputTypes.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "VCamOutputProviderBase.h"

#if WITH_EDITOR
#include "LevelEditorViewport.h"
#include "UnrealEdMisc.h"
#endif

#include "VCamComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVCamComponent, Log, All);

class UCineCameraComponent;
class UVCamModifierContext;

UCLASS(Blueprintable, ClassGroup=(VCam), meta=(BlueprintSpawnableComponent, DisplayName = "VCam Component"))
class VCAMCORE_API UVCamComponent : public USceneComponent
{
	GENERATED_BODY()

	friend class UVCamCoreSubsystem;

public:
	UVCamComponent();

	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void PostInitProperties() override;

	virtual void OnAttachmentChanged() override;

#if WITH_EDITOR
	virtual void CheckForErrors() override;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	bool CanUpdate() const;

	void Update();

	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	UCineCameraComponent* GetTargetCamera() const;

	// Add a modifier to the stack with a given name.
	// If that name is already in use then the modifier will not be added.
	// Returns the created modifier if the Add succeeded
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera", Meta = (DeterminesOutputType = "ModifierClass", 
		DynamicOutputParam = "CreatedModifier"))
	void AddModifier(const FName Name, TSubclassOf<UVCamModifier> ModifierClass, bool& bSuccess,
                 UVCamModifier*& CreatedModifier);

	// Remove all Modifiers from the Stack.
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void ClearModifiers();

	// Remove the given Modifier from the Stack.
	// Returns true if the modifier was removed successfully
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
    bool RemoveModifier(const UVCamModifier* Modifier);

	// Remove the Modifier at a specified index from the Stack.
	// Returns true if the modifier was removed successfully
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	bool RemoveModifierByIndex(const int ModifierIndex);

	// Remove the Modifier with a specific name from the Stack.
	// Returns true if the modifier was removed successfully
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
    bool RemoveModifierByName(const FName Name);

	// Tries to find a Modifier in the Stack with the given name.
	// The returned Modifier must be checked before it is used.
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	UVCamModifier* FindModifierByName(const FName Name) const;

	UFUNCTION(BlueprintPure, Category = "VirtualCamera", Meta = (DeterminesOutputType = "ModifierClass",
		DynamicOutputParam = "FoundModifiers"))
	void FindModifiersByClass(TSubclassOf<UVCamModifier> ModifierClass, TArray<UVCamModifier*>& FoundModifiers) const;

	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
    void FindModifierByInterface(TSubclassOf<UInterface> InterfaceClass, TArray<UVCamModifier*>& FoundModifiers) const;

	/*
	Sets the Modifier Context to a new instance of the provided class
	@param ContextClass The Class to create the context from
	@param CreatedContext The created Context, can be invalid if Context Class was None
	*/
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera", Meta = (DeterminesOutputType = "ContextClass",
	DynamicOutputParam = "CreatedContext", AllowAbstract = "false"))
	void SetModifierContextClass(TSubclassOf<UVCamModifierContext> ContextClass, UVCamModifierContext*& CreatedContext);
	
	/*
	Get the current Modifier Context
	@return Current Context
	*/
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	UVCamModifierContext* GetModifierContext() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam")
	FLiveLinkSubjectName LiveLinkSubject;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam")
    bool bLockViewportToCamera = false;

	UPROPERTY(EditAnywhere, Instanced, Category="VCam")
	TArray<UVCamOutputProviderBase*> OutputProviders;

private:
	void GetInitialLiveLinkData(FLiveLinkCameraBlueprintData& InitialLiveLinkData);
	static void CopyLiveLinkDataToCamera(const FLiveLinkCameraBlueprintData& LiveLinkData, UCineCameraComponent* CameraComponent);

	float GetDeltaTime();
    void UpdateActorLock();
	void DestroyOutputProvider(UVCamOutputProviderBase* Provider);
	void ResetAllOutputProviders();
	void OnEndPlayMap();

#if WITH_EDITOR
	FLevelEditorViewportClient* GetLevelViewportClient() const;

	void OnMapChanged(UWorld* World, EMapChangeType ChangeType);

	void OnBeginPIE(const bool InIsSimulating);
	void OnPostPIEStarted(const bool InIsSimulating);
#endif

	double LastEvaluationTime;

	TWeakObjectPtr<AActor> Backup_ActorLock;
	TWeakObjectPtr<AActor> Backup_ViewTarget;

	TArray<UVCamOutputProviderBase*> SavedOutputProviders;

	UPROPERTY(EditAnywhere, Instanced, Category = "VCam")
	UVCamModifierContext* ModifierContext;

	UPROPERTY(EditAnywhere, Category = "VCam")
	TArray<FModifierStackEntry> ModifierStack;

	void ConditionallyInitializeModifiers();
};