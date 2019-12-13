// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Delegates/IDelegateInstance.h"
#include "Engine/EngineTypes.h"
#include "MediaFrameworkWorldSettingsAssetUserData.h"
#include "Misc/NotifyHook.h"
#include "PropertyEditorDelegates.h"
#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "SMediaFrameworkCapture.generated.h"

class FWorkspaceItem;
class IDetailsView;
class SMediaFrameworkCaptureCameraViewportWidget;
class SMediaFrameworkCaptureCurrentViewportWidget;
class SMediaFrameworkCaptureRenderTargetWidget;
class SSplitter;

namespace MediaFrameworkUtilities
{
	class SCaptureVerticalBox;
}

/**
 * Settings for the media capture tab.
 */
UCLASS(MinimalAPI, config = EditorPerProjectUserSettings)
class UMediaFrameworkMediaCaptureSettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(config)
	bool bIsVerticalSplitterOrientation = true;
};

/**
 * Settings for the capture that are persistent per users.
 */
UCLASS(MinimalAPI, config = Editor)
class UMediaFrameworkEditorCaptureSettings : public UMediaFrameworkWorldSettingsAssetUserData
{
	GENERATED_BODY()
public:
	/** Should the Capture setting be saved with the level or should it be saved as a project settings. */
	UPROPERTY(config)
	bool bSaveCaptureSetingsInWorld = true;
};

/*
 * SMediaFrameworkCapture
 */
class SMediaFrameworkCapture : public SCompoundWidget, public FNotifyHook
{
public:
	static void RegisterNomadTabSpawner(TSharedRef<FWorkspaceItem> InWorkspaceItem);
	static void UnregisterNomadTabSpawner();
	static TSharedPtr<SMediaFrameworkCapture> GetPanelInstance();

private:
	static TWeakPtr<SMediaFrameworkCapture> WidgetInstance;

public:
	SLATE_BEGIN_ARGS(SMediaFrameworkCapture){}
	SLATE_END_ARGS()

	~SMediaFrameworkCapture();

	void Construct(const FArguments& InArgs);

	bool IsCapturing() const { return bIsCapturing; }
	void EnabledCapture(bool bEnabled);
	UMediaFrameworkWorldSettingsAssetUserData* FindOrAddMediaFrameworkAssetUserData();

private:
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
	bool IsPropertyReadOnly(const FPropertyAndParent& PropertyAndParent) const;

	void OnMapChange(uint32 InMapFlags);
	void OnLevelActorsRemoved(AActor* InActor);
	void OnAssetsDeleted(const TArray<UClass*>& DeletedAssetClasses);
	void OnObjectPreEditChange(UObject* Object, const FEditPropertyChain& PropertyChain);

	TSharedRef<class SWidget> MakeToolBar();
	TSharedRef<SWidget> CreateSettingsMenu();

	bool CanEnableViewport() const;
	UMediaFrameworkWorldSettingsAssetUserData* FindMediaFrameworkAssetUserData() const;

	void OnPrePIE(bool bIsSimulating);
	void OnPostPIEStarted(bool bIsSimulating);
	void OnPrePIEEnded(bool bIsSimulating);

	TSharedPtr<IDetailsView> DetailView;
	TSharedPtr<SSplitter> Splitter;
	TSharedPtr<MediaFrameworkUtilities::SCaptureVerticalBox> CaptureBoxes;
	bool bIsCapturing;
	bool bIsInPIESession;

	TArray<TSharedPtr<SMediaFrameworkCaptureCameraViewportWidget>> CaptureCameraViewports;
	TArray<TSharedPtr<SMediaFrameworkCaptureRenderTargetWidget>> CaptureRenderTargets;
	TSharedPtr<SMediaFrameworkCaptureCurrentViewportWidget> CaptureCurrentViewport;

	static FDelegateHandle LevelEditorTabManagerChangedHandle;
};
