// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Elements/Framework/TypedElementHandle.h"

#include "LevelEditorMenuContext.generated.h"

class SLevelEditor;
class UActorComponent;
class SLevelViewport;
class SLevelViewportToolBar;
class FLevelEditorViewportClient;
class UTypedElementSelectionSet;

UCLASS()
class LEVELEDITOR_API ULevelEditorMenuContext : public UObject
{
	GENERATED_BODY()
public:
	TWeakPtr<SLevelEditor> LevelEditor;
};


/** Enum to describe what a level editor context menu should be built for */
enum class ELevelEditorMenuContext
{
	/** This context menu is applicable to a viewport */
	Viewport,
	/** This context menu is applicable to the Scene Outliner (disables click-position-based menu items) */
	SceneOutliner,
};

UCLASS()
class LEVELEDITOR_API ULevelEditorContextMenuContext : public UObject
{
	GENERATED_BODY()
public:

	TWeakPtr<SLevelEditor> LevelEditor;

	ELevelEditorMenuContext ContextType;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LevelEditor|Menu")
	TObjectPtr<const UTypedElementSelectionSet> CurrentSelection;
	
	/** If the ContextType is Viewport this property can be set to the HitProxy element that triggered the ContextMenu. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LevelEditor|Menu")
	FTypedElementHandle HitProxyElement;

	UPROPERTY()
	TArray<TObjectPtr<UActorComponent>> SelectedComponents;

	/** If the ContextType is Viewport this property can be set to the HitProxy actor that triggered the ContextMenu. */
	UPROPERTY()
	TObjectPtr<AActor> HitProxyActor = nullptr;
};

UCLASS()
class LEVELEDITOR_API ULevelViewportToolBarContext : public UObject
{
	GENERATED_BODY()
public:
	TWeakPtr<SLevelViewportToolBar> LevelViewportToolBarWidget;
	TWeakPtr<const SLevelViewportToolBar> LevelViewportToolBarWidgetConst;

	FLevelEditorViewportClient* GetLevelViewportClient();
};


UCLASS()
class LEVELEDITOR_API UQuickActionMenuContext : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient)
	TObjectPtr<const UTypedElementSelectionSet> CurrentSelection;
};
