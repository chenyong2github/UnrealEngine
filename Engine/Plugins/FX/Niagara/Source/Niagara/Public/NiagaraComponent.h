// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraCommon.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "Particles/ParticleSystemComponent.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraComponentPool.h"
#include "Particles/ParticlePerfStats.h"

#include "NiagaraComponent.generated.h"

class FMeshElementCollector;
class FNiagaraRenderer;
class UNiagaraSystem;
class UNiagaraParameterCollection;
class UNiagaraParameterCollectionInstance;
class FNiagaraSystemSimulation;
class NiagaraEmitterInstanceBatcher;

// Called when the particle system is done
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNiagaraSystemFinished, class UNiagaraComponent*, PSystem);

#define WITH_NIAGARA_COMPONENT_PREVIEW_DATA (!UE_BUILD_SHIPPING)

/**
* UNiagaraComponent is the primitive component for a Niagara System.
* @see ANiagaraActor
* @see UNiagaraSystem
*/
UCLASS(ClassGroup = (Rendering, Common), hidecategories = Object, hidecategories = Physics, hidecategories = Collision, showcategories = Trigger, editinlinenew, meta = (BlueprintSpawnableComponent, DisplayName = "Niagara Particle System"))
class NIAGARA_API UNiagaraComponent : public UFXSystemComponent
{
	friend struct FNiagaraScalabilityManager;
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA
	DECLARE_MULTICAST_DELEGATE(FOnSystemInstanceChanged);
	DECLARE_MULTICAST_DELEGATE(FOnSynchronizedWithAssetParameters);
#endif

public:

	/********* UFXSystemComponent *********/
	void SetFloatParameter(FName ParameterName, float Param) override;
	void SetVectorParameter(FName ParameterName, FVector Param) override;
	void SetColorParameter(FName ParameterName, FLinearColor Param) override;
	void SetActorParameter(FName ParameterName, class AActor* Param) override;
	virtual UFXSystemAsset* GetFXSystemAsset() const override;
	void SetEmitterEnable(FName EmitterName, bool bNewEnableState) override;
	void ReleaseToPool() override;
	uint32 GetApproxMemoryUsage() const override;
	/********* UFXSystemComponent *********/

private:
	UPROPERTY(EditAnywhere, Category="Niagara", meta = (DisplayName = "Niagara System Asset"))
	UNiagaraSystem* Asset;

	/** Allows you to control how Niagara selects the tick group, changing this while an instance is active will result in not change as it is cached. */
	UPROPERTY(EditAnywhere, Category = "Niagara", meta = (DisplayName = "Niagara Tick Behavior"))
	ENiagaraTickBehavior TickBehavior = ENiagaraTickBehavior::UsePrereqs;

	/** Initial values for parameter overrides. 
	TODO: This should be a minimal set of explicitly override parameters similar to how parameter collection instances override their collections store. 
	Should expose anything in the "User" namespace.
	*/
	UPROPERTY(EditAnywhere, Category = Parameters)
	FNiagaraUserRedirectionParameterStore OverrideParameters;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TMap<FName, bool> EditorOverridesValue;

	FOnSystemInstanceChanged OnSystemInstanceChangedDelegate;

	FOnSynchronizedWithAssetParameters OnSynchronizedWithAssetParametersDelegate;
#endif

	/**
	When true, this component's system will be force to update via a slower "solo" path rather than the more optimal batched path with other instances of the same system.
	*/
	UPROPERTY(EditAnywhere, Category = Parameters)
	uint32 bForceSolo : 1;

	TUniquePtr<FNiagaraSystemInstance> SystemInstance;

	/** Defines the mode use when updating the System age. */
	ENiagaraAgeUpdateMode AgeUpdateMode;
	
	/** The desired age of the System instance.  This is only relevant when using the DesiredAge age update mode. */
	float DesiredAge;

	/** Whether or not the component can render while seeking to the desired age. */
	bool bCanRenderWhileSeeking;

	/** The delta time used when seeking to the desired age.  This is only relevant when using the DesiredAge age update mode. */
	float SeekDelta;

	/** The maximum amount of time in seconds to spend seeking to the desired age in a single frame. */
	float MaxSimTime;

	/** Whether or not the component is currently seeking to the desired time. */
	bool bIsSeeking;

	UPROPERTY()
	uint32 bAutoDestroy : 1;

	UPROPERTY()
	uint32 bRenderingEnabled : 1;

	//~ Begin UActorComponent Interface.
protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnEndOfFrameUpdateDuringTick() override;
	virtual void CreateRenderState_Concurrent() override;
	virtual void SendRenderDynamicData_Concurrent() override;
	virtual void BeginDestroy() override;
	//virtual void OnAttachmentChanged() override;
public:
	/**
	* True if we should automatically attach to AutoAttachParent when activated, and detach from our parent when completed.
	* This overrides any current attachment that may be present at the time of activation (deferring initial attachment until activation, if AutoAttachParent is null).
	* When enabled, detachment occurs regardless of whether AutoAttachParent is assigned, and the relative transform from the time of activation is restored.
	* This also disables attachment on dedicated servers, where we don't actually activate even if bAutoActivate is true.
	* @see AutoAttachParent, AutoAttachSocketName, AutoAttachLocationType
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Attachment)
	uint32 bAutoManageAttachment : 1;

	/**
	 * Time between forced UpdateTransforms for systems that use dynamically calculated bounds,
	 * Which is effectively how often the bounds are shrunk.
	 */
	UPROPERTY()
	float MaxTimeBeforeForceUpdateTransform;

	/** How to handle pooling for this component instance. */
	ENCPoolMethod PoolingMethod;

	virtual void Activate(bool bReset = false)override;
	virtual void Deactivate()override;
	void DeactivateImmediate();

	FORCEINLINE ENiagaraExecutionState GetRequestedExecutionState()const { return SystemInstance ? SystemInstance->GetRequestedExecutionState() : ENiagaraExecutionState::Complete; }
	FORCEINLINE ENiagaraExecutionState GetExecutionState()const { return SystemInstance ? SystemInstance->GetActualExecutionState() : ENiagaraExecutionState::Complete; }

	FORCEINLINE bool IsComplete()const { return SystemInstance ? SystemInstance->IsComplete() : true; }

	FORCEINLINE float GetSafeTimeSinceRendered(float WorldTime)const;
	private:

	//Internal versions that can be called from the scalability code.
	//These will behave as expected but will keep the component registered with the scalability manager.
	void ActivateInternal(bool bReset, bool bIsScalabilityCull);
	void DeactivateInternal(bool bIsScalabilityCull);
	void DeactivateImmediateInternal(bool bIsScalabilityCull);

	bool RegisterWithScalabilityManagerOrPreCull();
	void UnregisterWithScalabilityManager();

	public:

	virtual void SetComponentTickEnabled(bool bEnabled) override;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual const UObject* AdditionalStatObject() const override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	//~ End UActorComponent Interface.

	//~ Begin UPrimitiveComponent Interface
	virtual int32 GetNumMaterials() const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;

	virtual void OnAttachmentChanged() override;
	//~ End UPrimitiveComponent Interface

	//~ Begin USceneComponent Interface
	virtual void OnChildAttached(USceneComponent* ChildComponent) override;
	virtual void OnChildDetached(USceneComponent* ChildComponent) override;
	//~ Begin USceneComponent Interface

	TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> GetSystemSimulation();

	bool InitializeSystem();
	void OnSystemComplete();
	void DestroyInstance();

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara System Asset"))
	void SetAsset(UNiagaraSystem* InAsset);

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Get Niagara System Asset"))
	UNiagaraSystem* GetAsset() const { return Asset; }

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Forced Solo Mode"))
	void SetForceSolo(bool bInForceSolo);

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Is In Forced Solo Mode"))
	bool GetForceSolo()const { return bForceSolo; }

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Get Age Update Mode"))
	ENiagaraAgeUpdateMode GetAgeUpdateMode() const;

	/** Sets the age update mode for the System instance. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Age Update Mode"))
	void SetAgeUpdateMode(ENiagaraAgeUpdateMode InAgeUpdateMode);

	/** Gets the desired age of the System instance.  This is only relevant when using the DesiredAge age update mode. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Get Desired Age"))
	float GetDesiredAge() const;

	/** Sets the desired age of the System instance.  This is only relevant when using the DesiredAge age update mode. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Desired Age"))
	void SetDesiredAge(float InDesiredAge);

	/** Sets the desired age of the System instance and designates that this change is a seek.  When seeking to a desired age the
	    The component can optionally prevent rendering. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Seek to Desired Age"))
	void SeekToDesiredAge(float InDesiredAge);

	/** Sets whether or not the system can render while seeking. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Can Render While Seeking"))
	void SetCanRenderWhileSeeking(bool bInCanRenderWhileSeeking);

	/** Gets the delta value which is used when seeking from the current age, to the desired age.  This is only relevant
	when using the DesiredAge age update mode. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Get Desired Age Seek Delta"))
	float GetSeekDelta() const;

	/** Sets the delta value which is used when seeking from the current age, to the desired age.  This is only relevant
	when using the DesiredAge age update mode. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Desired Age Seek Delta"))
	void SetSeekDelta(float InSeekDelta);

	/** Sets the maximum time that you can jump within a tick which is used when seeking from the current age, to the desired age.  This is only relevant
	when using the DesiredAge age update mode. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Get Max Desired Age Tick Delta"))
	float GetMaxSimTime() const;

	/** Sets the maximum time that you can jump within a tick which is used when seeking from the current age, to the desired age.  This is only relevant
	when using the DesiredAge age update mode. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Max Desired Age Tick Delta"))
	void SetMaxSimTime(float InMaxTime);

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Auto Destroy"))
	void SetAutoDestroy(bool bInAutoDestroy) { bAutoDestroy = bInAutoDestroy; }

	FNiagaraSystemInstance* GetSystemInstance() const;

	ENiagaraTickBehavior GetTickBehavior() const { return TickBehavior; }

	/** Returns true if this component forces it's instances to run in "Solo" mode. A sub optimal path required in some situations. */
	bool ForcesSolo()const;

	/** Sets a Niagara FLinearColor parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable By String (LinearColor)"))
	void SetNiagaraVariableLinearColor(const FString& InVariableName, const FLinearColor& InValue);
	
	/** Sets a Niagara FLinearColor parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (LinearColor)"))
	void SetVariableLinearColor(FName InVariableName, const FLinearColor& InValue);


	/** Sets a Niagara Vector4 parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable By String (Vector4)"))
	void SetNiagaraVariableVec4(const FString& InVariableName, const FVector4& InValue);
	
	/** Sets a Niagara Vector4 parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (Vector4)"))
	void SetVariableVec4(FName InVariableName, const FVector4& InValue);


	/** Sets a Niagara Vector3 parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable By String (Quaternion)"))
	void SetNiagaraVariableQuat(const FString& InVariableName, const FQuat& InValue);

	/** Sets a Niagara Vector3 parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (Quaternion)"))
	void SetVariableQuat(FName InVariableName, const FQuat& InValue);

	/** Sets a Niagara Vector3 parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable By String (Vector3)"))
	void SetNiagaraVariableVec3(const FString& InVariableName, FVector InValue);

	/** Sets a Niagara Vector3 parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (Vector3)"))
	void SetVariableVec3(FName InVariableName, FVector InValue);

	/** Sets a Niagara Vector3 parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable By String (Vector2)"))
	void SetNiagaraVariableVec2(const FString& InVariableName, FVector2D InValue);

	/** Sets a Niagara Vector3 parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (Vector2)"))
	void SetVariableVec2(FName InVariableName, FVector2D InValue);

	/** Sets a Niagara float parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable By String (Float)"))
	void SetNiagaraVariableFloat(const FString& InVariableName, float InValue);

	/** Sets a Niagara float parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (Float)"))
	void SetVariableFloat(FName InVariableName, float InValue);

	/** Sets a Niagara int parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable By String (Int32)"))
	void SetNiagaraVariableInt(const FString& InVariableName, int32 InValue);

	/** Sets a Niagara int parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (Int32)"))
	void SetVariableInt(FName InVariableName, int32 InValue);

	/** Sets a Niagara float parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable By String (Bool)"))
	void SetNiagaraVariableBool(const FString& InVariableName, bool InValue);

	/** Sets a Niagara float parameter by name, overriding locally if necessary.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (Bool)"))
	void SetVariableBool(FName InVariableName, bool InValue);

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable By String (Actor)"))
	void SetNiagaraVariableActor(const FString& InVariableName, AActor* Actor);
	
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (Actor)"))
	void SetVariableActor(FName InVariableName, AActor* Actor);
	
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable By String (Object)"))
	void SetNiagaraVariableObject(const FString& InVariableName, UObject* Object);

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (Object)"))
	void SetVariableObject(FName InVariableName, UObject* Object);

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Niagara Variable (Material)"))
	void SetVariableMaterial(FName InVariableName, UMaterialInterface* Object);

	/** Debug accessors for getting positions in blueprints. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Get Niagara Emitter Positions"))
	TArray<FVector> GetNiagaraParticlePositions_DebugOnly(const FString& InEmitterName);
	
	/** Debug accessors for getting a float attribute array in blueprints.  The attribute name should be without namespaces. For example for "Particles.Position", send "Position". */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Get Niagara Emitter Float Attrib"))
	TArray<float> GetNiagaraParticleValues_DebugOnly(const FString& InEmitterName, const FString& InValueName);
	
	/** Debug accessors for getting a FVector attribute array in blueprints. The attribute name should be without namespaces. For example for "Particles.Position", send "Position". */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Get Niagara Emitter Vec3 Attrib"))
	TArray<FVector> GetNiagaraParticleValueVec3_DebugOnly(const FString& InEmitterName, const FString& InValueName);

	/** Resets the System to it's initial pre-simulated state. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Reset System"))
	void ResetSystem();

	/** Called on when an external object wishes to force this System to reinitialize itself from the System data.*/
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Reinitialize System"))
	void ReinitializeSystem();

	/** Gets whether or not rendering is enabled for this component. */
	bool GetRenderingEnabled() const;

	/** Sets whether or not rendering is enabled for this component. */
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DisplayName = "Set Rendering Enabled"))
	void SetRenderingEnabled(bool bInRenderingEnabled);

	/** Advances this system's simulation by the specified number of ticks and delta time. */
	UFUNCTION(BlueprintCallable, Category = Niagara)
	void AdvanceSimulation(int32 TickCount, float TickDeltaSeconds);

	/** Advances this system's simulation by the specified time in seconds and delta time. Advancement is done in whole ticks of TickDeltaSeconds so actual simulated time will be the nearest lower multiple of TickDeltaSeconds. */
	UFUNCTION(BlueprintCallable, Category = Niagara)
	void AdvanceSimulationByTime(float SimulateTime, float TickDeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = Niagara)
	void SetPaused(bool bInPaused);

	UFUNCTION(BlueprintCallable, Category = Niagara)
	bool IsPaused()const;

	UFUNCTION(BlueprintCallable, Category = Niagara)
	UNiagaraDataInterface * GetDataInterface(const FString &Name);

	//~ Begin UObject Interface.
	virtual void PostLoad();
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void OverrideUObjectParameter(const FNiagaraVariable& InVar, UObject* InObj);

#endif
	//~ End UObject Interface

	UFUNCTION(BlueprintCallable, Category = Preview, meta = (Keywords = "preview detail level scalability"))
	void SetPreviewDetailLevel(bool bEnablePreviewDetailLevel, int32 PreviewDetailLevel);

	UFUNCTION(BlueprintCallable, Category = Preview, meta = (Keywords = "preview LOD Distance scalability"))
	void SetPreviewLODDistance(bool bEnablePreviewLODDistance, float PreviewLODDistance);

	UFUNCTION(BlueprintCallable, Category = Preview, meta = (Keywords = "preview detail level scalability"))
	FORCEINLINE bool GetPreviewDetailLevelEnabled()const;

	UFUNCTION(BlueprintCallable, Category = Preview, meta = (Keywords = "preview LOD Distance scalability"))
	FORCEINLINE bool GetPreviewLODDistanceEnabled()const;

	UFUNCTION(BlueprintCallable, Category = Preview, meta = (Keywords = "preview detail level scalability"))
	FORCEINLINE int32 GetPreviewDetailLevel()const;

	UFUNCTION(BlueprintCallable, Category = Preview, meta = (Keywords = "preview LOD Distance scalability"))
	FORCEINLINE int32 GetPreviewLODDistance()const;


	FORCEINLINE void SetLODDistance(float InLODDistance, float InMaxLODDistance) { if (SystemInstance) SystemInstance->SetLODDistance(InLODDistance, InMaxLODDistance); }

#if WITH_EDITOR
	void PostLoadNormalizeOverrideNames();
	bool IsParameterValueOverriddenLocally(const FName& InParamName);
	void SetParameterValueOverriddenLocally(const FNiagaraVariable& InParam, bool bInOverridden, bool bRequiresSystemInstanceReset);
	
	FOnSystemInstanceChanged& OnSystemInstanceChanged() { return OnSystemInstanceChangedDelegate; }

	FOnSynchronizedWithAssetParameters& OnSynchronizedWithAssetParameters() { return OnSynchronizedWithAssetParametersDelegate; }
#endif

	FNiagaraUserRedirectionParameterStore& GetOverrideParameters() { return OverrideParameters; }

	const FNiagaraParameterStore& GetOverrideParameters() const { return OverrideParameters; }

	bool IsWorldReadyToRun() const;

	//~ End UObject Interface.

	// Called when the particle system is done
	UPROPERTY(BlueprintAssignable)
	FOnNiagaraSystemFinished OnSystemFinished;

	/** Removes all local overrides and replaces them with the values from the source System - note: this also removes the editor overrides from the component as it is used by the pooling mechanism to prevent values leaking between different instances. */
	void SetUserParametersToDefaultValues();

private:
	/** Compare local overrides with the source System. Remove any that have mismatched types or no longer exist on the System.*/
	void SynchronizeWithSourceSystem();

	void AssetExposedParametersChanged();

public:
	/**
	 * Component we automatically attach to when activated, if bAutoManageAttachment is true.
	 * If null during registration, we assign the existing AttachParent and defer attachment until we activate.
	 * @see bAutoManageAttachment
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Attachment, meta=(EditCondition="bAutoManageAttachment"))
	TWeakObjectPtr<USceneComponent> AutoAttachParent;

	/**
	 * Socket we automatically attach to on the AutoAttachParent, if bAutoManageAttachment is true.
	 * @see bAutoManageAttachment
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Attachment, meta=(EditCondition="bAutoManageAttachment"))
	FName AutoAttachSocketName;

	/**
	 * Options for how we handle our location when we attach to the AutoAttachParent, if bAutoManageAttachment is true.
	 * @see bAutoManageAttachment, EAttachmentRule
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attachment, meta = (EditCondition = "bAutoManageAttachment"))
	EAttachmentRule AutoAttachLocationRule;

	/**
	 * Options for how we handle our rotation when we attach to the AutoAttachParent, if bAutoManageAttachment is true.
	 * @see bAutoManageAttachment, EAttachmentRule
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attachment, meta = (EditCondition = "bAutoManageAttachment"))
	EAttachmentRule AutoAttachRotationRule;

	/**
	 * Options for how we handle our scale when we attach to the AutoAttachParent, if bAutoManageAttachment is true.
	 * @see bAutoManageAttachment, EAttachmentRule
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attachment, meta = (EditCondition = "bAutoManageAttachment"))
	EAttachmentRule AutoAttachScaleRule;


	/**
	 * Set AutoAttachParent, AutoAttachSocketName, AutoAttachLocationRule, AutoAttachRotationRule, AutoAttachScaleRule to the specified parameters. Does not change bAutoManageAttachment; that must be set separately.
	 * @param  Parent			Component to attach to. 
	 * @param  SocketName		Socket on Parent to attach to.
	 * @param  LocationRule		Option for how we handle our location when we attach to Parent.
	 * @param  RotationRule		Option for how we handle our rotation when we attach to Parent.
	 * @param  ScaleRule		Option for how we handle our scale when we attach to Parent.
	 * @see bAutoManageAttachment, AutoAttachParent, AutoAttachSocketName, AutoAttachLocationRule, AutoAttachRotationRule, AutoAttachScaleRule
	 */
	void SetAutoAttachmentParameters(USceneComponent* Parent, FName SocketName, EAttachmentRule LocationRule, EAttachmentRule RotationRule, EAttachmentRule ScaleRule) override;

	virtual void SetUseAutoManageAttachment(bool bAutoManage) override { bAutoManageAttachment = bAutoManage; }

#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
	int32 PreviewDetailLevel;
	float PreviewLODDistance;
	uint32 bEnablePreviewDetailLevel : 1;
	uint32 bEnablePreviewLODDistance : 1;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Compilation)
	uint32 bWaitForCompilationOnActivate : 1;
#endif

	virtual void SetOwnerLOD(int32 InOwnerLOD);

	UFUNCTION(BlueprintCallable, Category = Scalability, meta = (Keywords = "LOD scalability"))
	FORCEINLINE int32 GetOwnerLOD()const { return OwnerLOD; }

	/** Set whether this component is allowed to perform scalability checks and potentially be culled etc. Occasionally it is useful to disable this for specific components. E.g. Effects on the local player. */
	UFUNCTION(BlueprintCallable, Category = Scalability, meta = (Keywords = "LOD scalability"))
	void SetAllowScalability(bool bAllow);

private:
	/** Did we try and activate but fail due to the asset being not yet ready. Keep looping.*/
	uint32 bAwaitingActivationDueToNotReady : 1;
	/** Should we try and reset when ready? */
	uint32 bActivateShouldResetWhenReady : 1;

	/** Did we auto attach during activation? Used to determine if we should restore the relative transform during detachment. */
	uint32 bDidAutoAttach : 1;

	/** True if this component is allowed to perform scalability checks and potentially be culled etc. Occasionally it is useful to disable this for specific components. E.g. Effects on the local player. */
	uint32 bAllowScalability : 1;

	/** Flag to mark us as currently changing auto attachment as part of Activate/Deactivate so we don't reset in the OnAttachmentChanged() callback. */
	//uint32 bIsChangingAutoAttachment : 1;

	/** Restore relative transform from auto attachment and optionally detach from parent (regardless of whether it was an auto attachment). */
	void CancelAutoAttachment(bool bDetachFromParent);


	/** Saved relative transform before auto attachment. Used during detachment to restore the transform if we had automatically attached. */
	FVector SavedAutoAttachRelativeLocation;
	FRotator SavedAutoAttachRelativeRotation;
	FVector SavedAutoAttachRelativeScale3D;

	FDelegateHandle AssetExposedParametersChangedHandle;

	int32 ScalabilityManagerHandle;

	/**
	LOD level of our owning actor / component if it's been provided. 
	Can be useful for scalability calculations for actor based FX on actors who have some other system determining their LOD level.
	Would be nice if we just had a virtual on the actor Actor->GetLODLevel() for example that would return 0 by default and the correct level for any actors implementing a LOD level.
	*/
	int32 OwnerLOD;
};

#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
FORCEINLINE bool UNiagaraComponent::GetPreviewDetailLevelEnabled()const { return bEnablePreviewDetailLevel; }
FORCEINLINE bool UNiagaraComponent::GetPreviewLODDistanceEnabled()const { return bEnablePreviewLODDistance; }
FORCEINLINE int32 UNiagaraComponent::GetPreviewDetailLevel()const { return bEnablePreviewDetailLevel ? PreviewDetailLevel : INDEX_NONE; }
FORCEINLINE int32 UNiagaraComponent::GetPreviewLODDistance()const { return bEnablePreviewLODDistance ? PreviewLODDistance : 0.0f; }
#else
FORCEINLINE bool UNiagaraComponent::GetPreviewDetailLevelEnabled()const { return false; }
FORCEINLINE bool UNiagaraComponent::GetPreviewLODDistanceEnabled()const { return false; }
FORCEINLINE int32 UNiagaraComponent::GetPreviewDetailLevel()const { return INDEX_NONE; }
FORCEINLINE int32 UNiagaraComponent::GetPreviewLODDistance()const { return 0.0f; }
#endif






/**
* Scene proxy for drawing niagara particle simulations.
*/
class FNiagaraSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override;

	FNiagaraSceneProxy(const UNiagaraComponent* InComponent);
	~FNiagaraSceneProxy();

	/** Called on render thread to assign new dynamic data */
	const TArray<class FNiagaraRenderer*>& GetEmitterRenderers() { return EmitterRenderers; }

	void CreateRenderers(const UNiagaraComponent* InComponent);
	void ReleaseRenderers();

	/** Gets whether or not this scene proxy should be rendered. */
	bool GetRenderingEnabled() const;

	/** Sets whether or not this scene proxy should be rendered. */
	void SetRenderingEnabled(bool bInRenderingEnabled);

	NiagaraEmitterInstanceBatcher* GetBatcher() const { return Batcher; }

#if RHI_RAYTRACING
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances) override;
	virtual bool IsRayTracingRelevant() const override { return true; }
#endif

	FORCEINLINE const FMatrix& GetLocalToWorldInverse() const { return LocalToWorldInverse; }

private:
	void ReleaseRenderThreadResources();

	//~ Begin FPrimitiveSceneProxy Interface.
	virtual void CreateRenderThreadResources() override;

	//virtual void OnActorPositionChanged() override;
	virtual void OnTransformChanged() override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	/*
	virtual bool CanBeOccluded() const override
	{
	return !MaterialRelevance.bDisableDepthTest;
	}
	*/

	/** Callback from the renderer to gather simple lights that this proxy wants renderered. */
	virtual void GatherSimpleLights(const FSceneViewFamily& ViewFamily, FSimpleLightArray& OutParticleLights) const override;


	virtual uint32 GetMemoryFootprint() const override;

	uint32 GetAllocatedSize() const;

private:
	/** Emitter Renderers in the order they appear in the emitters. */
	TArray<FNiagaraRenderer*> EmitterRenderers;
	
	/** Indices of renderers in the order they should be rendered. */
	TArray<int32> RendererDrawOrder;

	bool bRenderingEnabled;
	NiagaraEmitterInstanceBatcher* Batcher = nullptr;

	FMatrix LocalToWorldInverse;

	/** Ptr to the cycle count for this systems effect type. Lifetime is guaranteed to be longer than the proxy. */
	int32* RuntimeCycleCount;

#if STATS
	TStatId SystemStatID;
#endif
#if WITH_PARTICLE_PERF_STATS
public:
	class UNiagaraSystem* PerfAsset;
#endif
};

extern float GLastRenderTimeSafetyBias;
FORCEINLINE float UNiagaraComponent::GetSafeTimeSinceRendered(float WorldTime)const
{
	return FMath::Max(0.0f, WorldTime - GetLastRenderTime() - GLastRenderTimeSafetyBias);
}