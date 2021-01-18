// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/TargetDeviceId.h"
#include "Interfaces/ITargetDevice.h"
#include "Widgets/SCompoundWidget.h"
#include "IPropertyChangeListener.h"

#include "Niagara/Private/NiagaraDebugHud.h"
#include "SNiagaraDebugger.generated.h"

USTRUCT()
struct FNiagaraDebugHUDVariable
{
	GENERATED_BODY()
		
	UPROPERTY(EditAnywhere, Category = "Variable")
	bool bEnabled = true;

	/** Name of variables to match, uses wildcard matching. */
	UPROPERTY(EditAnywhere, Category = "Variable")
	FString Name;
};

UCLASS(config = EditorPerProjectUserSettings, defaultconfig)
class UNiagaraDebugHUDSettings : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnChanged);
	FOnChanged OnChangedDelegate;

	/** Enables the Debug HUD display. */
	UPROPERTY(EditAnywhere, Category = "Debug General")
	bool bEnabled = false;

	/**
	When enabled GPU particles will be debuggable by copying the data to CPU
	accessible memory, this has both a performance & memory cost.  The data
	is also latent so expect a frame or two of lag.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug General")
	bool bEnableGpuReadback = false;

	/** Modifies the display location of the HUD overview. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug General")
	FIntPoint HUDLocation = FIntPoint(30.0f, 150.0f);

	/**
	Wildcard filter for the systems to show more detailed information about.
	For example,. "NS_*" would match all systems starting with NS_.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter")
	FString SystemFilter;

	/**
	Wildcard filter for the components to show more detailed information about.
	For example, "*MyComp*" would match all components that contain MyComp.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter")
	FString ComponentFilter;

	/** Modifies the in world system display information level. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	ENiagaraDebugHudSystemVerbosity SystemVerbosity = ENiagaraDebugHudSystemVerbosity::Basic;

	/** When enabled will show the system bounds for all filtered systems. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	bool bSystemShowBounds = false;

	/** When disabled in world rendering will show systems deactivated by scalability. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	bool bSystemShowActiveOnlyInWorld = true;

	/** Should we display the system variables. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	bool bShowSystemVariables = true;

	/**
	List of variables to show about the system, each entry uses wildcard matching.
	For example, "System.*" would match all system variables.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	TArray<FNiagaraDebugHUDVariable> SystemVariables;

	/**
	Maximum number of particles to show information about.
	Set to 0 to show all variables, but be warned that displaying information about 1000's of particles
	will result in poor editor performance & potentially OOM on some platforms.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles")
	int32 MaxParticlesToDisplay = 32;

	/** When enabled will show particle data in world, otherwise it's attached to the system display. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles")
	bool bShowParticlesInWorld = true;

	/** When enabled will show particle variables from the list. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles")
	bool bShowParticleVariables = true;

	/**
	List of variables to show per particle, each entry uses wildcard matching.
	For example, "*Position" would match all variables that end in Position.
	*/
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles")
	TArray<FNiagaraDebugHUDVariable> ParticlesVariables;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

class SNiagaraDebugger : public SCompoundWidget, public FGCObject
{
	struct FTargetDeviceEntry
	{
		FTargetDeviceId			DeviceId;
		ITargetDeviceWeakPtr	DeviceWeakPtr;
		const FSlateBrush*		DeviceIconBrush = nullptr;
	};

	typedef TSharedPtr<FTargetDeviceEntry> FTargetDeviceEntryPtr;

public:
	SLATE_BEGIN_ARGS(SNiagaraDebugger) {}
	SLATE_END_ARGS();

	SNiagaraDebugger();
	virtual ~SNiagaraDebugger();

	void Construct(const FArguments& InArgs);

	// SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	TSharedRef<SWidget> MakeDeviceComboButtonMenu();

	void InitDeviceList();
	void DestroyDeviceList();

	void AddTargetDevice(ITargetDeviceRef TargetDevice);
	void RemoveTargetDevice(ITargetDeviceRef TargetDevice);
	
	void SelectDevice(FTargetDeviceEntryPtr DeviceEntry);

	const FSlateBrush* GetTargetDeviceBrush(FTargetDeviceEntryPtr DeviceEntry) const;
	FText GetTargetDeviceText(FTargetDeviceEntryPtr DeviceEntry) const;

	const FSlateBrush* GetSelectedTargetDeviceBrush() const;
	FText GetSelectedTargetDeviceText() const;

	void ExecConsoleCommand(const TCHAR* Cmd, bool bRequiresWorld);
	void ExecHUDConsoleCommand();

	float GetPlaybackRate() const;
	void SetPlaybackRate(float Rate);

protected:
	TArray<FTargetDeviceEntryPtr>	TargetDeviceList;
	FTargetDeviceEntryPtr			SelectedTargetDevice;
	bool							bWasDeviceConnected = true;
	float							PlaybackRate = 1.0f;
};
