// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioGameplayComponent.h"
#include "AudioGameplayVolumeComponent.generated.h"

// Forward Declarations 
class FProxyVolumeMutator;
class UAudioGameplayVolumeProxy;
class UAudioGameplayVolumeSubsystem;

/**
 *  UAudioGameplayVolumeComponentBase - Base component for use with audio gameplay volumes
 */
UCLASS(Abstract, HideDropdown)
class AUDIOGAMEPLAYVOLUME_API UAudioGameplayVolumeComponentBase : public UAudioGameplayComponent
{
	GENERATED_UCLASS_BODY()

public:

	virtual ~UAudioGameplayVolumeComponentBase() = default;

	UFUNCTION(BlueprintCallable, Category = "AudioGameplay")
	void SetPriority(int32 InPriority);

	int32 GetPriority() const { return Priority; }

	/** Create and fill the appropriate proxy mutator for this component */
	virtual TSharedPtr<FProxyVolumeMutator> CreateMutator() const final;

protected:

	/** Create this component's type of mutator */
	virtual TSharedPtr<FProxyVolumeMutator> FactoryMutator() const;

	/** Fill the mutator with data from our component */
	virtual void FillMutator(TSharedPtr<FProxyVolumeMutator> Mutator) const;

	/** Notify our parent volume our proxy may need updating */
	void NotifyDataChanged() const;

	// The priority of this component.  In the case of overlapping volumes or multiple affecting components, the highest priority is chosen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioGameplay", Meta = (AllowPrivateAccess = "true"))
	int32 Priority = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAudioGameplayVolumeProxyStateChange);

/**
 *  UAudioGameplayVolumeProxyComponent - Component used to drive interaction with AudioGameplayVolumeSubsystem.
 *   NOTE: Do not inherit from this class, use UAudioGameplayVolumeComponentBase to create extendable functionality
 */
UCLASS(Config = Game, ClassGroup = ("AudioGameplay"), meta = (BlueprintSpawnableComponent, IsBlueprintBase = false, DisplayName = "Volume Proxy"))
class AUDIOGAMEPLAYVOLUME_API UAudioGameplayVolumeProxyComponent final : public UAudioGameplayComponent
{
	GENERATED_UCLASS_BODY()

public:

	virtual ~UAudioGameplayVolumeProxyComponent() = default;

	void SetProxy(UAudioGameplayVolumeProxy* NewProxy);
	UAudioGameplayVolumeProxy* GetProxy() const { return Proxy; }

	/** Called by a component on same actor to notify our proxy may need updating */
	void OnComponentDataChanged();

	/** Called when the proxy is 'entered' - This is when the proxy goes from zero listeners to at lesat one. */
	void EnterProxy() const;

	/** Called when the proxy is 'exited' - This is when the proxy goes from at least one listeners to zero. */
	void ExitProxy() const;

	/** Blueprint event for proxy enter */
	UPROPERTY(BlueprintAssignable, Category = Events)
	FOnAudioGameplayVolumeProxyStateChange OnProxyEnter;

	/** Blueprint event for proxy exit */
	UPROPERTY(BlueprintAssignable, Category = Events)
	FOnAudioGameplayVolumeProxyStateChange OnProxyExit;

protected:

	// A representation of this volume for the audio thread
	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite, Category = "AudioGameplay", Meta = (ShowOnlyInnerProperties, AllowPrivateAccess = "true"))
	UAudioGameplayVolumeProxy* Proxy = nullptr;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	//~ Begin UActorComponent Interface
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	//~ Begin UAudioGameplayComponent Interface
	virtual void Enable() override;
	virtual void Disable() override;
	//~ End UAudioGameplayComponent Interface

	void AddProxy() const;
	void RemoveProxy() const;
	void UpdateProxy() const;

	UAudioGameplayVolumeSubsystem* GetSubsystem() const;
};