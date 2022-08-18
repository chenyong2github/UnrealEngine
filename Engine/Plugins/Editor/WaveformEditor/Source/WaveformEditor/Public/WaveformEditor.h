// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/NotifyHook.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "WaveformEditorTransportController.h"
#include "WaveformEditorTransportCoordinator.h"
#include "WaveformEditorZoomController.h"

class IToolkitHost;
class SDockTab;
class USoundWave;
class UAudioComponent;

class WAVEFORMEDITOR_API FWaveformEditor : public FAssetEditorToolkit, public FGCObject, public FNotifyHook
{
public:
	bool Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, USoundWave* SoundWaveToEdit);

	/** FAssetEditorToolkit interface */
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual FName GetEditorName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	void NotifyPreChange(class FEditPropertyChain* PropertyAboutToChange) override {};
	void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, class FEditPropertyChain* PropertyThatChanged) override;

private:
	bool SetupAudioComponent();
	bool SetUpTransportController();
	bool SetUpZoom();

	bool BindDelegates();

	/**	Sets the wave editor layout */
	const TSharedRef<FTabManager::FLayout> SetupStandaloneLayout();

	/**	Toolbar Setup */
	bool RegisterToolbar();
	bool BindCommands();

	/**	Properties tab set up */
	TSharedRef<SDockTab> SpawnTab_Properties(const FSpawnTabArgs& Args);
	bool SetUpPropertiesView();

	/**	Waveform view tab setup */
	TSharedRef<SDockTab> SpawnTab_WaveformDisplay(const FSpawnTabArgs& Args);
	bool SetUpWaveformPanel();

	/** Playback delegates handlers */
	void HandlePlaybackPercentageChange(const UAudioComponent* InComponent, const USoundWave* InSoundWave, const float InPlaybackPercentage);
	void HandleAudioComponentPlayStateChanged(const UAudioComponent* InAudioComponent, EAudioComponentPlayState NewPlayState);
	void HandlePlayheadScrub(const uint32 SelectedSample, const uint32 TotalSampleLength, const bool bIsMoving);

	/** FGCObject overrides */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

	bool CanPressPlayButton() const;

	bool SetUpWaveWriter();
	void ExportWaveform();

	/** Waveform Preview widget */
	TSharedPtr<class SWaveformPanel> WaveformPanel;

	/** Manages render information for waveform transforms */
	TSharedPtr<class FWaveformTransformationsRenderManager> TransformationsRenderManager = nullptr;

	/** Exports the edited waveform to a new asset */
	TSharedPtr<class FWaveformEditorWaveWriter> WaveWriter = nullptr;

	/** Manages Transport info in waveform panel */
	TSharedPtr<FWaveformEditorTransportCoordinator> TransportCoordinator = nullptr;

	/** Controls Transport of the audio component  */
	TSharedPtr<FWaveformEditorTransportController> TransportController = nullptr;

	/** Controls and propagates zoom level */
	TSharedPtr<FWaveformEditorZoomController> ZoomManager = nullptr;

	/** Properties tab */
	TSharedPtr<IDetailsView> PropertiesView;

	/** Settings Editor App Identifier */
	static const FName AppIdentifier;

	/** Tab Ids */
	static const FName PropertiesTabId;
	static const FName WaveformDisplayTabId;

	static const FName EditorName;
	static const FName ToolkitFName;

	USoundWave* SoundWave = nullptr;
	UAudioComponent* AudioComponent = nullptr;

	bool bWasPlayingBeforeScrubbing = false;
};
