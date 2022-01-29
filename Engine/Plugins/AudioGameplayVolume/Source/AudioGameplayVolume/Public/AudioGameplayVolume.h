// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Volume.h"
#include "AudioGameplayVolume.generated.h"

// Forward Declarations 
class UAudioGameplayVolumeProxyComponent;
class UAudioGameplayVolumeSubsystem;

/**
 * AudioGameplayVolume
 */
UCLASS(Blueprintable, hidecategories = (Advanced, Attachment, Collision, Volume))
class AUDIOGAMEPLAYVOLUME_API AAudioGameplayVolume : public AVolume
{
	GENERATED_UCLASS_BODY()

private:

	// A representation of this volume for the audio thread
	UPROPERTY()
	UAudioGameplayVolumeProxyComponent* AGVComponent = nullptr;

	// Whether this volume is currently enabled.  Disabled volumes will not have a volume proxy, 
	// and therefore will not be considered for intersection checks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_bEnabled, Category = "AudioGameplay", Meta = (AllowPrivateAccess = "true"))
	bool bEnabled = true;

public:

	bool GetEnabled() const { return bEnabled; }
	
	UFUNCTION(BlueprintCallable, Category = "AudioGameplay")
	void SetEnabled(bool bEnable);

	/** Blueprint event for listener enter */
	UFUNCTION(BlueprintImplementableEvent, Category = Events)
	void OnListenerEnter();

	/** Blueprint event for listener exit */
	UFUNCTION(BlueprintImplementableEvent, Category = Events)
	void OnListenerExit();

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool ShouldCheckCollisionComponentForErrors() const override { return false; }
#endif // WITH_EDITOR
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UObject Interface

	//~ Begin AActor Interface
	virtual void PostInitializeComponents() override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	//~ End AActor Interface

	/** Called by a child component to notify our proxy may need updating */
	void OnComponentDataChanged();

	bool CanSupportProxy() const;

protected:

	UFUNCTION()
	virtual void OnRep_bEnabled();

	void TransformUpdated(USceneComponent* InRootComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	void AddProxy() const;
	void RemoveProxy() const;
	void UpdateProxy() const;
};
