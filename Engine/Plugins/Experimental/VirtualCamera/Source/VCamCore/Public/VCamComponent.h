// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VCamTypes.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "VCamOutputProviderBase.h"
#include "GameplayTagContainer.h"

#if WITH_EDITOR
#include "UnrealEdMisc.h"
#include "VCamMultiUser.h"
#endif

#include "VCamComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVCamComponent, Log, All);

class UCineCameraComponent;
class UVCamModifierContext;
class SWindow;
class FSceneViewport;

#if WITH_EDITOR
class FLevelEditorViewportClient;
#endif

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnComponentReplaced, UVCamComponent*, NewComponent);

UENUM(BlueprintType, meta=(DisplayName = "VCam Target Viewport ID"))
enum class EVCamTargetViewportID : uint8
{
	CurrentlySelected = 0,
	Viewport1 = 1,
	Viewport2 = 2,
	Viewport3 = 3,
	Viewport4 = 4
};

UCLASS(Blueprintable, ClassGroup=(VCam), meta=(BlueprintSpawnableComponent))
class VCAMCORE_API UVCamComponent : public USceneComponent
{
	GENERATED_BODY()

	friend class UVCamModifier;

public:
	UVCamComponent();

	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	virtual void OnAttachmentChanged() override;

	TSharedPtr<FSceneViewport> GetTargetSceneViewport() const;
	TWeakPtr<SWindow> GetTargetInputWindow() const;

#if WITH_EDITOR
	FLevelEditorViewportClient* GetTargetLevelViewportClient() const;
	TSharedPtr<SLevelViewport> GetTargetLevelViewport() const;

	virtual void CheckForErrors() override;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	// There are situations in the editor where the component may be replaced by another component as part of the actor being reconstructed
	// This event will notify you of that change and give you a reference to the new component. 
	// Bindings will be copied to the new component so you do not need to rebind this event
	// 
	// Note: When the component is replaced you will need to get all properties on the component again such as Modifiers and Output Providers
	UPROPERTY(BlueprintAssignable, Category = "VirtualCamera")
	FOnComponentReplaced OnComponentReplaced;

	UFUNCTION()
	void HandleObjectReplaced(const TMap<UObject*, UObject*>& ReplacementMap);

	bool CanUpdate() const;

	void Update();

	// Sets if the VCamComponent will update every frame or not
	UFUNCTION(BlueprintSetter)
	void SetEnabled(bool bNewEnabled);

	// Returns whether or not the VCamComponent will update every frame
	UFUNCTION(BlueprintGetter)
	bool IsEnabled() const { return bEnabled; };

	// Returns the Target CineCameraComponent
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	UCineCameraComponent* GetTargetCamera() const;

	// Add a modifier to the stack with a given name.
	// If that name is already in use then the modifier will not be added.
	// Returns the created modifier if the Add succeeded
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera", Meta = (DeterminesOutputType = "ModifierClass", DynamicOutputParam = "CreatedModifier", ReturnDisplayName = "Success"))
	bool AddModifier(const FName Name, UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UVCamModifier> ModifierClass, UVCamModifier*& CreatedModifier);

	// Insert a modifier to the stack with a given name and index.
	// If that name is already in use then the modifier will not be added.
	// The index must be between zero and the number of existing modifiers inclusive
	// Returns the created modifier if the Add succeeded
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera", Meta = (DeterminesOutputType = "ModifierClass", DynamicOutputParam = "CreatedModifier", ReturnDisplayName = "Success"))
	bool InsertModifier(const FName Name, int32 Index, UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UVCamModifier> ModifierClass, UVCamModifier*& CreatedModifier);

	// Moves an existing modifier in the stack from its current index to a new index
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera", Meta = (ReturnDisplayName = "Success"))
	bool SetModifierIndex(int32 OriginalIndex, int32 NewIndex);

	// Remove all Modifiers from the Stack.
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void RemoveAllModifiers();

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

	// Returns the number of Modifiers in the Component's Stack
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	int32 GetNumberOfModifiers() const;

	// Returns all the Modifiers in the Component's Stack
	// Note: It's possible not all Modifiers will be valid (such as if the user has not set a class for the modifier in the details panel)
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	void GetAllModifiers(TArray<UVCamModifier*>& Modifiers) const;

	// Returns the Modifier in the Stack with the given index if it exist.
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	UVCamModifier* GetModifierByIndex(const int32 Index) const;

	// Tries to find a Modifier in the Stack with the given name.
	// The returned Modifier must be checked before it is used.
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	UVCamModifier* GetModifierByName(const FName Name) const;

	// Given a specific Modifier class, returns a list of matching Modifiers
	UFUNCTION(BlueprintPure, Category = "VirtualCamera", Meta = (DeterminesOutputType = "ModifierClass", DynamicOutputParam = "FoundModifiers"))
	void GetModifiersByClass(UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UVCamModifier> ModifierClass, TArray<UVCamModifier*>& FoundModifiers) const;

	// Given a specific Interface class, returns a list of matching Modifiers
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	void GetModifiersByInterface(UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UInterface> InterfaceClass, TArray<UVCamModifier*>& FoundModifiers) const;

	/*
	Sets the Modifier Context to a new instance of the provided class
	@param ContextClass The Class to create the context from
	@param CreatedContext The created Context, can be invalid if Context Class was None
	*/
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera", Meta = (DeterminesOutputType = "ContextClass", DynamicOutputParam = "CreatedContext", AllowAbstract = "false"))
	void SetModifierContextClass(UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UVCamModifierContext> ContextClass, UVCamModifierContext*& CreatedContext);
	
	/*
	Get the current Modifier Context
	@return Current Context
	*/
	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	UVCamModifierContext* GetModifierContext() const;

	// Output Provider access

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera", Meta = (DeterminesOutputType = "ProviderClass", DynamicOutputParam = "CreatedProvider", ReturnDisplayName = "Success"))
	bool AddOutputProvider(UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UVCamOutputProviderBase> ProviderClass, UVCamOutputProviderBase*& CreatedProvider);

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera", Meta = (DeterminesOutputType = "ProviderClass", DynamicOutputParam = "CreatedProvider", ReturnDisplayName = "Success"))
	bool InsertOutputProvider(int32 Index, UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UVCamOutputProviderBase> ProviderClass, UVCamOutputProviderBase*& CreatedProvider);

	// Moves an existing Output Provider in the stack from its current index to a new index
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera", Meta = (ReturnDisplayName = "Success"))
	bool SetOutputProviderIndex(int32 OriginalIndex, int32 NewIndex);

	// Remove all Output Providers from the Component.
	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void RemoveAllOutputProviders();

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	bool RemoveOutputProvider(const UVCamOutputProviderBase* Provider);

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	bool RemoveOutputProviderByIndex(const int32 ProviderIndex);

	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	int32 GetNumberOfOutputProviders() const;

	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	void GetAllOutputProviders(TArray<UVCamOutputProviderBase*>& Providers) const;

	UFUNCTION(BlueprintPure, Category = "VirtualCamera")
	UVCamOutputProviderBase* GetOutputProviderByIndex(const int32 ProviderIndex) const;

	UFUNCTION(BlueprintPure, Category = "VirtualCamera", Meta = (DeterminesOutputType = "ProviderClass", DynamicOutputParam = "FoundProviders"))
	void GetOutputProvidersByClass(UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UVCamOutputProviderBase> ProviderClass, TArray<UVCamOutputProviderBase*>& FoundProviders) const;

private:
	// Enabled state of the component
	UPROPERTY(EditAnywhere, BlueprintSetter = SetEnabled, BlueprintGetter = IsEnabled, Category = "VirtualCamera")
	bool bEnabled = true;

public:
	/**
	 * The role of this virtual camera.  If this value is set and the corresponding tag set on the editor matches this value, then this
	 * camera is the sender and the authority in the case when connected to a multi-user session.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VirtualCamera")
	FGameplayTag Role;

	// LiveLink subject name for the incoming camera transform
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VirtualCamera")
	FLiveLinkSubjectName LiveLinkSubject;

	// If true, render the viewport from the point of view of the parented CineCamera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VirtualCamera")
	bool bLockViewportToCamera = false;

	// If true, the component will force bEnabled to false when it is part of a spawnable in Sequencer
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera")
	bool bDisableComponentWhenSpawnedBySequencer = true;

	/** Do we disable the output if the virtual camera is in a Multi-user session and the camera is a "receiver" from multi-user */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "VirtualCamera")
	bool bDisableOutputOnMultiUserReceiver = true;

	/** Indicates the frequency which camera updates are sent when in Multi-user mode. This has a minimum value of 30ms. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category="VirtualCamera", meta=(ForceUnits=ms, ClampMin = "30.0"), DisplayName="Update Frequencey")
	float UpdateFrequencyMs = 66.6f;

	// Which viewport to use for this VCam
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera")
	EVCamTargetViewportID TargetViewport = EVCamTargetViewportID::CurrentlySelected;

	// List of Output Providers (executed in order)
	UPROPERTY(EditAnywhere, Instanced, Category="VirtualCamera")
	TArray<UVCamOutputProviderBase*> OutputProviders;

	UFUNCTION(BlueprintCallable, Category="VirtualCamera")
	void GetLiveLinkDataForCurrentFrame(FLiveLinkCameraBlueprintData& LiveLinkData);

private:
	static void CopyLiveLinkDataToCamera(const FLiveLinkCameraBlueprintData& LiveLinkData, UCineCameraComponent* CameraComponent);

	float GetDeltaTime();
	void SetActorLock(bool bNewActorLock) { bLockViewportToCamera = bNewActorLock; UpdateActorLock(); }
	void UpdateActorLock();
	void DestroyOutputProvider(UVCamOutputProviderBase* Provider);
	void ResetAllOutputProviders();

	// Use the Saved Modifier Stack from PreEditChange to find the modified entry and then ensure the modified entry's name is unique
	// If a new modifier has been created then its name will be defaulted to BaseName
	void EnforceModifierStackNameUniqueness(const FString BaseName = "NewModifier");
	bool DoesNameExistInSavedStack(const FName InName) const;
	void FindModifiedStackEntry(int32& ModifiedStackIndex, bool& bIsNewEntry) const;

#if WITH_EDITOR
	void OnMapChanged(UWorld* World, EMapChangeType ChangeType);

	void OnBeginPIE(const bool bInIsSimulating);
	void OnEndPIE(const bool bInIsSimulating);

	// Multi-user support
	void HandleCameraComponentEventData(const FConcertSessionContext& InEventContext, const FMultiUserVCamCameraComponentEvent& InEvent);

	void SessionStartup(TSharedRef<IConcertClientSession> InSession);
	void SessionShutdown(TSharedRef<IConcertClientSession> InSession);

	FString GetNameForMultiUser() const;

	void MultiUserStartup();
	void MultiUserShutdown();

	/** Delegate handle for a the callback when a session starts up */
	FDelegateHandle OnSessionStartupHandle;

	/** Delegate handle for a the callback when a session shuts down */
	FDelegateHandle OnSessionShutdownHandle;

	/** Weak pointer to the client session with which to send events. May be null or stale. */
	TWeakPtr<IConcertClientSession> WeakSession;

	double SecondsSinceLastLocationUpdate = 0;
	double PreviousUpdateTime = 0;
#endif

	/** Is the camera currently in a role assigned to the session. */
	bool IsCameraInVPRole() const;

	/** Send the current camera state via Multi-user if connected and in a */
	void SendCameraDataViaMultiUser();

	/** Are we in a multi-user session. */
	bool IsMultiUserSession() const;

	/** Can the modifier stack be evaluated. */
	bool CanEvaluateModifierStack() const;

	// When another component replaces us, get a notification so we can clean up
	void NotifyComponentWasReplaced(UVCamComponent* ReplacementComponent);

	double LastEvaluationTime;

	TWeakObjectPtr<AActor> Backup_ActorLock;
	TWeakObjectPtr<AActor> Backup_ViewTarget;

	TArray<UVCamOutputProviderBase*> SavedOutputProviders;
	TArray<FModifierStackEntry> SavedModifierStack;

	// Modifier Context object that can be accessed by the Modifier Stack
	UPROPERTY(EditAnywhere, Instanced, Category = "VirtualCamera")
	UVCamModifierContext* ModifierContext;

	// List of Modifiers (executed in order)
	UPROPERTY(EditAnywhere, Category = "VirtualCamera")
	TArray<FModifierStackEntry> ModifierStack;

	// Variable used for pausing update on editor objects while PIE is running
	bool bIsEditorObjectButPIEIsRunning = false;

	UPROPERTY(Transient)
	bool bIsLockedToViewport = false;
};
