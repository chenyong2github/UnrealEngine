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
class ULocalPlayer;

/** Context data that act as a key to lock a scene removal to your system. */
USTRUCT()
struct FCommonUILayoutContextData
{
	GENERATED_BODY()

	/** List of contexts associated with a layout to prevent others from removing it. */
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
class COMMONUILAYOUT_API UCommonUILayoutManager : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	 static UCommonUILayoutManager* GetInstance(const UWorld* World);

	// Set the HUD scale applied to ALL children.
	void SetHUDScale(const float InHUDScale, ULocalPlayer* Player);

	// Use to trigger a refresh of the layout in case scenes were added before the layout widget was created,
	// preventing the manager from creating the layout panel.
	void NotifyLayoutAddedToViewport();

	// Add scenes to the active list and trigger a recalculation of the allowed & unallowed widgets.
	void Add(const UCommonUILayout* Layout, const UObject* OptionalContext = nullptr);
	void Add(const TArray<UCommonUILayout*> Layouts, const UObject* OptionalContext = nullptr);

	// Remove scenes from the active list.
	void Remove(const UCommonUILayout* Layout, const UObject* OptionalContext = nullptr);
	void Remove(const TArray<UCommonUILayout*> Layouts, const UObject* OptionalContext = nullptr);

	// Get the unique id associated to a user widget.
	FName GetUniqueIDForWidget(const ULocalPlayer* Player, UUserWidget* Widget) const;

	// Get the instantiated user widget matching class and unique id.
	TWeakObjectPtr<UUserWidget> FindUserWidgetWithUniqueID(const ULocalPlayer* Player, const TSoftClassPtr<UUserWidget>& WidgetClass, const FName& UniqueID) const;

	bool TryGetLayoutPanelPaintGeometry(const ULocalPlayer* Player, FGeometry& OutPaintGeometry);

	void AddLayoutToPreloadQueue(const UCommonUILayout* Layout, const UObject* OptionalContext = nullptr);
	void RemoveLayoutFromPreloadQueue(const UCommonUILayout* Layout, const UObject* OptionalContext = nullptr);
	void ClearPreloadQueue();
	bool IsLayoutPreloaded(const UCommonUILayout* Layout, const UObject* OptionalContext = nullptr);

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
	
	void OnLocalPlayerAdded(ULocalPlayer* Player);
	void OnLocalPlayerRemoved(ULocalPlayer* Player);

	void Add_Internal(const UCommonUILayout* Scene, const UObject* Context);
	void Remove_Internal(const UCommonUILayout* Scene, const UObject* Context);

	void RefreshVisibility();

	SCommonUILayoutPanel* GetLayoutPanel(const ULocalPlayer* Player) const;
	SCommonUILayoutPanel& GetOrCreateLayoutPanel(ULocalPlayer* Player);

private:
	/** Root layout assigned to a player */
	struct FRootLayoutData
	{
	public:
		void CreateLayout();
		void Reset();

		void RefreshVisibility(const TMap<TObjectPtr<const UCommonUILayout>, FCommonUILayoutContextData>& InActiveLayouts);

		void ApplyHUDScale();

	public:
		/** Player associated to this root layout. */
		TWeakObjectPtr<ULocalPlayer> Player;

		/** Root overlay widget to contain the layout panel and make sure it stretched across the whole screen. */
		TSharedPtr<SWidget> RootLayout;

		/** Panel that is used to parent every widgets that are allowed. */
		TSharedPtr<SCommonUILayoutPanel> LayoutPanel;

		/** Scale box used to apply the HUD scale UI settings. */
		TSharedPtr<SScaleBox> ScaleBox;

		/** Scale applied to the scale box which is parent to ALL children. */
		float HUDScale = 1.0f;
	};
	TMap<TWeakObjectPtr<const ULocalPlayer>, FRootLayoutData> RootLayoutMap;
	
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

public:
	// Debugging functions
#if !UE_BUILD_SHIPPING
	static void DEBUG_RefreshVisibility();
	static void DEBUG_ListCurrentState();
#endif

};