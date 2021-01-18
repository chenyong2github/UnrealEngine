// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/TargetDeviceId.h"
#include "Interfaces/ITargetDevice.h"
#include "Widgets/SCompoundWidget.h"
#include "IPropertyChangeListener.h"

#include "Niagara/Private/NiagaraDebugHud.h"
#include "SNiagaraDebugger.generated.h"

UCLASS(config = EditorPerProjectUserSettings, defaultconfig)
class UNiagaraDebugHUDSettings : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnChanged);
	FOnChanged OnChangedDelegate;

	/** Enables the Debug HUD */
	UPROPERTY(EditAnywhere, Category = "Debug General")
	bool bEnabled = false;

	/** Modifies the display location of the HUD */
	UPROPERTY(Config, EditAnywhere, Category = "Debug General")
	FIntPoint HUDLocation = FIntPoint(30.0f, 150.0f);

	/** Filter for the systems to show more detailed information about. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter")
	FString SystemFilter;

	/** Filter for the components to show more detailed information about. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Filter")
	FString ComponentFilter;

	/** Modifies the in world system display level. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	ENiagaraDebugHudSystemVerbosity SystemVerbosity = ENiagaraDebugHudSystemVerbosity::Basic;

	/** Should we show the system bounds. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	bool bSystemShowBounds = false;

	/** When disabled in world rendering will show systems deactivated by scalability. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	bool bSystemShowActiveOnlyInWorld = true;

	/** Should we display the system variables. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	bool bShowSystemVariables = true;

	/** List of variables to show about the system, this is an exact match unless prefixed with *, i.e. *pos will match any variable containing pos. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug System")
	TArray<FString> SystemVariables;

	/** Maximum number of particles to show information about, this is to reduce load on the system and avoid OOM if viewing 1000's of particles. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles")
	int32 MaxParticlesToDisplay = 32;

	/** When enabled will show particle data in world, otherwise it's attached to the system.. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles")
	bool bShowParticlesInWorld = true;

	/** Should we display the particle variables. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles")
	bool bShowParticleVariables = true;

	/** List of variables to show per particle, this is an exact match unless prefixed with *, i.e. *pos will match any variable containing pos. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug Particles")
	TArray<FString> ParticlesVariables;

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

	void OnPauseSimulations(bool bPause);
	void OnStepSimulations();

protected:
	TArray<FTargetDeviceEntryPtr>	TargetDeviceList;
	FTargetDeviceEntryPtr			SelectedTargetDevice;
	bool							bWasDeviceConnected = true;
};
