// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <atomic>
#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "Subsystems/WorldSubsystem.h"
#include "Subsystems/SubsystemCollection.h"

#include "SCommonUILayoutPanel.h"
#include "CommonUILayout.h"

#include "CommonUILayoutManager.generated.h"

class SScaleBox;
struct FStreamableHandle;

USTRUCT()
struct FCommonUILayoutContextData
{
	GENERATED_BODY()

	/** List of contexts associated with that scene has unique keys. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<const UObject>> Contexts;
};

USTRUCT()
struct FCommonUILayoutPreloadData
{
	GENERATED_BODY()

	/** List of contexts associated with that scene has unique keys. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<const UObject>> Contexts;

	TSharedPtr<FStreamableHandle> Handle;
};

/**
 * The Dynamic HUD Scene Manager is used to control the HUD module visibility
 * by adding/removing active scenes. Each time a scene is added or removed,
 * the allowed & unallowed list is refreshed to determine which HUD modules
 * can be visible.
 */
UCLASS()
class UCommonUILayoutManager : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	COMMONUILAYOUT_API static UCommonUILayoutManager* GetInstance(const UWorld* World);

	// Set the HUD scale applied to ALL children.
	COMMONUILAYOUT_API void SetHUDScale(const float InHUDScale);

	// Use to trigger a refresh of the layout in case scenes were added before the layout widget was created,
	// preventing the manager from creating the layout panel.
	COMMONUILAYOUT_API void NotifyLayoutAddedToViewport();

	// Add scenes to the active list and trigger a recalculation of the allowed & unallowed widgets.
	COMMONUILAYOUT_API void Add(const UCommonUILayout* Layout, const UObject* OptionalContext = nullptr);
	COMMONUILAYOUT_API void Add(const TArray<UCommonUILayout*> Layouts, const UObject* OptionalContext = nullptr);

	// Remove scenes from the active list.
	COMMONUILAYOUT_API void Remove(const UCommonUILayout* Layout, const UObject* OptionalContext = nullptr);
	COMMONUILAYOUT_API void Remove(const TArray<UCommonUILayout*> Layouts, const UObject* OptionalContext = nullptr);

	// Get the unique id associated to a user widget.
	COMMONUILAYOUT_API FName GetUniqueIDForWidget(UUserWidget* Widget) const;

	// Get the instantiated user widget matching class and unique id.
	COMMONUILAYOUT_API TWeakObjectPtr<UUserWidget> FindUserWidgetWithUniqueID(const TSoftClassPtr<UUserWidget>& WidgetClass, const FName& UniqueID) const;

	COMMONUILAYOUT_API void AddLayoutToPreloadQueue(const UCommonUILayout* Layout, const UObject* OptionalContext = nullptr);
	COMMONUILAYOUT_API void RemoveLayoutFromPreloadQueue(const UCommonUILayout* Layout, const UObject* OptionalContext = nullptr);
	COMMONUILAYOUT_API void ClearPreloadQueue();
	COMMONUILAYOUT_API bool IsLayoutPreloaded(const UCommonUILayout* Layout, const UObject* OptionalContext = nullptr);

protected:
	
	UFUNCTION(BlueprintCallable, Category = "CommonUI Layout")
	void AddLayout(const UCommonUILayout* Layout) { Add(Layout); }

	UFUNCTION(BlueprintCallable, Category = "CommonUI Layout")
	void RemoveLayout(const UCommonUILayout* Layout) { Remove(Layout); }

	UFUNCTION(BlueprintCallable, Category = "CommonUI Layout")
	void AddPreloadLayout(const UCommonUILayout* Layout) { AddLayoutToPreloadQueue(Layout); }

	UFUNCTION(BlueprintCallable, Category = "CommonUI Layout")
	void RemovePreloadLayout(const UCommonUILayout* Layout) { RemoveLayoutFromPreloadQueue(Layout); }

	UFUNCTION(BlueprintCallable, Category = "CommonUI Layout")
	void ClearPreloadLayouts() { ClearPreloadQueue(); }

	UFUNCTION(BlueprintCallable, Category = "CommonUI Layout")
	void IsPreloadLayoutComplete(const UCommonUILayout* Layout) { IsLayoutPreloaded(Layout); }

private:
	
	void CreateRootPanel();
	void DestroyRootLayout();

	void ApplyHUDScale();

	void Add_Internal(const UCommonUILayout* Scene, const UObject* Context);
	void Remove_Internal(const UCommonUILayout* Scene, const UObject* Context);

	void RefreshVisibility();

	struct FRootLayoutData
	{
		/** Root overlay widget to contain the layout panel and make sure it stretched across the whole screen. */
		TSharedPtr<SWidget> RootPanel;

		/** Player used to add this root layout. */
		TWeakObjectPtr<ULocalPlayer> Player;

		void Reset(const UWorld* World);
	} RootPanelData;
	
	/** Panel that is used to parent widgets that are added in layouts. */
	TSharedPtr<SCommonUILayoutPanel> LayoutPanel;

	/** Scale box used to apply the HUD scale UI settings. */
	TSharedPtr<SScaleBox> ScaleBox;

	/** Scale applied to the scale box which is parent to ALL children. */
	float HUDScale = 1.0f;
	
	/** List of active layouts. (Key = Layout pointer, Value = Context UObject)
	The context UObject is used as a unique key in the remove functions to prevent another
	callee from removing a scene added somewhere else. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<const UCommonUILayout> /*Layout*/, FCommonUILayoutContextData /*Context*/> ActiveLayouts;
	FCriticalSection ActiveLayoutsCriticalSection;

	/** List of layouts that are preloaded. Automatically removed when a layout is removed. */
	UPROPERTY(Transient)
	TMap<const UCommonUILayout*, FCommonUILayoutPreloadData> PreloadLayouts;
	FCriticalSection PreloadLayoutsCriticalSection;
};