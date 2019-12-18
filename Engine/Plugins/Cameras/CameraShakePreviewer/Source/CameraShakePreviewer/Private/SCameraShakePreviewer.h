// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Containers/Array.h"
#include "EditorUndoClient.h"
#include "Templates/SharedPointer.h"
#include "UObject/GCObject.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/SCompoundWidget.h"

class AActor;
class FCameraShakePreviewerLevelEditorViewportClient;
class FCameraShakePreviewerModule;
class FCameraShakePreviewUpdater;
class FLevelEditorViewportClient;
class FSceneView;
class ITableRow;
class STableViewBase;
class ULevel;
class UCameraModifier_CameraShake;
class UCameraShakeSourceComponent;
class UWorld;
struct FCameraShakeData;
struct FMinimalViewInfo;
struct FTogglePreviewCameraShakesParams;

class FCameraShakePreviewUpdater : public FTickableEditorObject, public FGCObject
{
public:
	FCameraShakePreviewUpdater();

	// FTickableObject Interface
	virtual ETickableTickType GetTickableTickType() const { return ETickableTickType::Always; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FCameraShakePreviewUpdater, STATGROUP_Tickables); }
	virtual void Tick(float DeltaTime) override { LastDeltaTime = DeltaTime; }

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Collector.AddReferencedObject(PreviewCameraShake); }
	virtual FString GetReferencerName() const override { return TEXT("SCameraShakePreviewer"); }

	UCameraModifier_CameraShake& ShakeModifier() const { return *PreviewCameraShake; }

	void ModifyCamera(FMinimalViewInfo& InOutPOV);

private:
	UCameraModifier_CameraShake* PreviewCameraShake;

	TOptional<float> LastDeltaTime;

	FVector LastLocationModifier;
	FRotator LastRotationModifier;
	float LastFOVModifier;
};

/**
 * Camera shake preview panel.
 */
class SCameraShakePreviewer : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SCameraShakePreviewer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SCameraShakePreviewer();

	// SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	// FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }

private:
	void Populate();
	bool FindCurrentWorld();
	void Refresh();

	void OnTogglePreviewCameraShakes(const FTogglePreviewCameraShakesParams& Params);

	TSharedRef<ITableRow> OnCameraShakesListGenerateRowWidget(TSharedPtr<FCameraShakeData> Entry, const TSharedRef<STableViewBase>& OwnerTable) const;
	void OnCameraShakesListSectionChanged(TSharedPtr<FCameraShakeData> Entry, ESelectInfo::Type SelectInfo) const;
	FText GetActiveViewportName() const;
	FText GetActiveViewportWarnings() const;

	FReply OnPlayStopAllShakes();
	FReply OnPlayStopSelectedShake();
	void PlayCameraShake(TSharedPtr<FCameraShakeData> CameraShake);

	void OnLevelViewportClientListChanged();
	void OnLevelAdded(ULevel* InLevel, UWorld* InWorld);
	void OnLevelRemoved(ULevel* InLevel, UWorld* InWorld);
	void OnLevelActorsAdded(AActor* InActor);
	void OnLevelActorsRemoved(AActor* InActor);
	void OnLevelActorListChanged();
	void OnMapChange(uint32 MapFlags);
	void OnNewCurrentLevel();
	void OnMapLoaded(const FString&  Filename, bool bAsTemplate);

	void OnModifyView(FMinimalViewInfo& InOutPOV);

private:
	TArray<TSharedPtr<FCameraShakeData>> CameraShakes;
	TUniquePtr<FCameraShakePreviewUpdater> CameraShakePreviewUpdater;

	TSharedPtr<SListView<TSharedPtr<FCameraShakeData>>> CameraShakesListView;
	TSharedPtr<SButton> PlayStopSelectedButton;

	FCameraShakePreviewerModule* CameraShakePreviewerModule;
	FLevelEditorViewportClient* ActiveViewportClient;
	int ActiveViewportIndex;

	TWeakObjectPtr<UWorld> CurrentWorld;
	bool bNeedsRefresh;
};