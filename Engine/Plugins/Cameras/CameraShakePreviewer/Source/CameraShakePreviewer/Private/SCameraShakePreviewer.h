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
class FLevelEditorViewportClient;
class FSceneView;
class ITableRow;
class STableViewBase;
class ULevel;
class UCameraModifier_CameraShake;
class UCameraShakeSourceComponent;
class UWorld;
struct FCameraShakeData;
struct FEditorViewportViewModifierParams;
struct FMinimalViewInfo;
struct FTogglePreviewCameraShakesParams;

/**
 * Camera shake preview panel.
 */
class SCameraShakePreviewer : public SCompoundWidget, public FEditorUndoClient, public FGCObject
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

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

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

	void OnModifyView(const FEditorViewportViewModifierParams& Params, FMinimalViewInfo& InOutPOV);

private:
	TArray<TSharedPtr<FCameraShakeData>> CameraShakes;
	UCameraModifier_CameraShake* PreviewCameraShake;

	TSharedPtr<SListView<TSharedPtr<FCameraShakeData>>> CameraShakesListView;
	TSharedPtr<SButton> PlayStopSelectedButton;

	FCameraShakePreviewerModule* CameraShakePreviewerModule;
	FLevelEditorViewportClient* ActiveViewportClient;
	int ActiveViewportIndex;

	TWeakObjectPtr<UWorld> CurrentWorld;
	bool bNeedsRefresh;
};