// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonUILayoutManager.h"
#include "CommonUILayout.h"
#include "CommonUILayoutConstraints.h"
#include "CommonUILayoutLog.h"
#include "SCommonUILayoutPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StreamableManager.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScaleBox.h"

namespace UCommonUILayoutManagerCVars
{
#if !UE_BUILD_SHIPPING
	static FAutoConsoleCommand RefreshVisibility(
		TEXT("CommonUILayout.RefreshVisibility"),
		TEXT("Refresh the visibility of the widgets allowed/unallowed by reevaluating the scenes registered in the Dynamic HUD."),
		FConsoleCommandDelegate::CreateStatic(UCommonUILayoutManager::DEBUG_RefreshVisibility)
	);

	static FAutoConsoleCommand ListCurrentState(
		TEXT("CommonUILayout.ListCurrentState"),
		TEXT("List in the log the currently active scenes, root viewport layouts & state content."),
		FConsoleCommandDelegate::CreateStatic(UCommonUILayoutManager::DEBUG_ListCurrentState)
	);
#endif
}

bool UCommonUILayoutManager::ShouldCreateSubsystem(UObject* Outer) const
{
	// Don't run on dedicated server
#if !UE_SERVER
	UWorld* World = Cast<UWorld>(Outer);
	check(World);
	return (World->WorldType == EWorldType::Game || (World->WorldType == EWorldType::PIE && World->GetNetMode() == NM_Standalone)) && !IsRunningDedicatedServer();
#else
	return false;
#endif
}

void UCommonUILayoutManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	if (UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
	{
		GameInstance->OnLocalPlayerAddedEvent.AddUObject(this, &UCommonUILayoutManager::OnLocalPlayerAdded);
		GameInstance->OnLocalPlayerRemovedEvent.AddUObject(this, &UCommonUILayoutManager::OnLocalPlayerRemoved);

		for (ULocalPlayer* Player : GameInstance->GetLocalPlayers())
		{
			if (Player)
			{
				TWeakObjectPtr<ULocalPlayer> WeakPlayer(Player);
				FRootLayoutData& RootLayoutData = RootLayoutMap.Add(WeakPlayer);
				RootLayoutData.Player = WeakPlayer;
				RootLayoutData.CreateLayout();
			}
		}
	}
}

void UCommonUILayoutManager::Deinitialize()
{
	UWorld* World = GetWorld();
	if (UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
	{
		GameInstance->OnLocalPlayerAddedEvent.RemoveAll(this);
		GameInstance->OnLocalPlayerRemovedEvent.RemoveAll(this);
	}

	for (TPair<TWeakObjectPtr<const ULocalPlayer>, FRootLayoutData> Pair : RootLayoutMap)
	{
		Pair.Value.Reset();
	}
	RootLayoutMap.Empty();

	Super::Deinitialize();
}

UCommonUILayoutManager* UCommonUILayoutManager::GetInstance(const UWorld* World)
{
	return World ? World->GetSubsystem<UCommonUILayoutManager>() : nullptr;
}

void UCommonUILayoutManager::SetHUDScale(const float InHUDScale, ULocalPlayer* Player)
{
	// We use FindOrAdd here to make sure we can store the HUDScale in case it's from a local player
	// we don't know yet, as we don't have any way to grab it outside this function.
	if (Player)
	{
		TWeakObjectPtr<ULocalPlayer> WeakPlayer(Player);
		FRootLayoutData& RootLayoutData = RootLayoutMap.FindOrAdd(WeakPlayer);
		if (RootLayoutData.HUDScale != InHUDScale)
		{
			if (RootLayoutData.Player.IsExplicitlyNull())
			{
				// Data was added, so we need to set the player
				RootLayoutData.Player = WeakPlayer;
			}

			RootLayoutData.HUDScale = InHUDScale;
			RootLayoutData.ApplyHUDScale();
		}
	}
}

void UCommonUILayoutManager::NotifyLayoutAddedToViewport()
{
	RefreshVisibility();
}

void UCommonUILayoutManager::Add(const UCommonUILayout* Layout, const UObject* OptionalContext)
{
	if (Layout)
	{
		Add_Internal(Layout, OptionalContext);
		RefreshVisibility();
	}
}

void UCommonUILayoutManager::Add(const TArray<UCommonUILayout*> Layouts, const UObject* OptionalContext /*= nullptr*/)
{
	if (Layouts.Num() > 0)
	{
		for (UCommonUILayout* Layout : Layouts)
		{
			Add_Internal(Layout, OptionalContext);
		}

		RefreshVisibility();
	}
}

void UCommonUILayoutManager::Remove(const UCommonUILayout* Layout, const UObject* OptionalContext)
{
	if (Layout)
	{
		Remove_Internal(Layout, OptionalContext);
		RefreshVisibility();
	}
}

void UCommonUILayoutManager::Remove(const TArray<UCommonUILayout*> Layouts, const UObject* OptionalContext /*= nullptr*/)
{
	if (Layouts.Num() > 0)
	{
		for (UCommonUILayout* Layout : Layouts)
		{
			Remove_Internal(Layout, OptionalContext);
		}

		RefreshVisibility();
	}
}

FName UCommonUILayoutManager::GetUniqueIDForWidget(const ULocalPlayer* Player, UUserWidget* Widget) const
{
	const SCommonUILayoutPanel* LayoutPanel = GetLayoutPanel(Player);
	return LayoutPanel ? LayoutPanel->FindUniqueIDForWidget(Widget) : FName();
}

TWeakObjectPtr<UUserWidget> UCommonUILayoutManager::FindUserWidgetWithUniqueID(const ULocalPlayer* Player, const TSoftClassPtr<UUserWidget>& WidgetClass, const FName& UniqueID) const
{
	const SCommonUILayoutPanel* LayoutPanel = GetLayoutPanel(Player);
	return LayoutPanel ? LayoutPanel->FindUserWidgetWithUniqueID(WidgetClass, UniqueID) : nullptr;
}

bool UCommonUILayoutManager::TryGetLayoutPanelPaintGeometry(const ULocalPlayer* Player, FGeometry& OutPaintGeometry)
{
	if (const SCommonUILayoutPanel* LayoutPanel = GetLayoutPanel(Player))
	{
		OutPaintGeometry = LayoutPanel->GetPaintSpaceGeometry();
		return true;
	}

	return false;
}

#if !UE_BUILD_SHIPPING
void UCommonUILayoutManager::DEBUG_RefreshVisibility()
{
	UWorld* World = GEngine->GetCurrentPlayWorld();
	if (UCommonUILayoutManager* Manager = UCommonUILayoutManager::GetInstance(World))
	{
		Manager->RefreshVisibility();
	}
}

void UCommonUILayoutManager::DEBUG_ListCurrentState()
{
	UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager: ================== Current State =================="));

	UWorld* World = GEngine->GetCurrentPlayWorld();
	if (UCommonUILayoutManager* Manager = UCommonUILayoutManager::GetInstance(World))
	{
		// Active Scenes
		UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager: * Active Scenes:"));
		for (TPair<TObjectPtr<const UCommonUILayout>, FCommonUILayoutContextData> Pair : Manager->ActiveLayouts)
		{
			TObjectPtr<const UCommonUILayout>& Scene = Pair.Key;
			TArray<TWeakObjectPtr<const UObject>>& Contexts = Pair.Value.Contexts;

			TStringBuilder<2048> StringBuilder;
			for (const TWeakObjectPtr<const UObject>& Context : Contexts)
			{
				if (Context.IsValid())
				{
					if (StringBuilder.Len() > 0)
					{
						StringBuilder.Append(TEXT(", "));
					}

					StringBuilder.Append(*Context->GetName());
				}
			}
			UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager:    - %s (%s)[%d]"), *Scene->GetName(), Contexts.Num() > 0 ? StringBuilder.ToString() : TEXT("None"), Contexts.Num());
		}

		// State Content
		UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager: * State Content:"));
		for (TPair<TWeakObjectPtr<const ULocalPlayer>, FRootLayoutData> Pair : Manager->RootLayoutMap)
		{
			const FRootLayoutData& RootLayoutData = Pair.Value;

			const ULocalPlayer* LocalPlayer = RootLayoutData.Player.Get();
			UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager:    Player: %s"), LocalPlayer ? *LocalPlayer->GetNickname() : TEXT("Unknown"));

			if (SCommonUILayoutPanel* LayoutPanelPtr = RootLayoutData.LayoutPanel.Get())
			{
				const FCommonUILayoutPanelInfo& Info = LayoutPanelPtr->StateContentInfo;
				UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager:    - %s"), Info.IsValid() ? *Info.ToString() : TEXT("No active State Content"));
			}
			else
			{
				UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager:    !!! Invalid SCommonUILayoutPanel !!!"));
			}
		}
	}
	else
	{
		UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager:    Invalid World!"));
	}

	UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager: ==================================================="));
}
#endif // #if !UE_BUILD_SHIPPING

void UCommonUILayoutManager::AddLayoutToPreloadQueue(const UCommonUILayout* Layout, const UObject* OptionalContext)
{
	if (!Layout)
	{
		return;
	}

	FScopeLock Lock(&PreloadLayoutsCriticalSection);
	FCommonUILayoutPreloadData& PreloadData = PreloadLayouts.FindOrAdd(Layout);
	if (!PreloadData.Handle.IsValid())
	{
		TArray<FSoftObjectPath> VisibleWidgetPaths;
		for (const FCommonUILayoutWidget& Widget : Layout->Widgets)
		{
			VisibleWidgetPaths.AddUnique(Widget.Widget.ToSoftObjectPath());
		}

		FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
		PreloadData.Handle = StreamableManager.RequestAsyncLoad(VisibleWidgetPaths, [Layout]() {
#if WITH_EDITOR
			extern ENGINE_API FString GPlayInEditorContextString;
			UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager [%s]: Completed Preload Layout: %s"), *GPlayInEditorContextString, *Layout->GetName());
#else
			UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager: Completed Preload Layout: %s"), *Layout->GetName());
#endif
		}, FStreamableManager::DefaultAsyncLoadPriority, true);

	}
	if (PreloadData.Contexts.Contains(OptionalContext))
	{
		// Layout with provided context or nullptr context is already active
#if WITH_EDITOR
		extern ENGINE_API FString GPlayInEditorContextString;
		UE_LOG(LogCommonUILayout, Warning, TEXT("UCommonUILayoutManager [%s]: Tried to Add an already active Layout to Preload: %s (%s)[%d]"), *GPlayInEditorContextString, *Layout->GetName(), OptionalContext ? *OptionalContext->GetName() : TEXT("None"), PreloadData.Contexts.Num());
#else
		UE_LOG(LogCommonUILayout, Warning, TEXT("UCommonUILayoutManager: Tried to Add an already active Layout to Preload: %s (%s)[%d]"), *Layout->GetName(), OptionalContext ? *OptionalContext->GetName() : TEXT("None"), PreloadData.Contexts.Num());
#endif
		return;
	}
	PreloadData.Contexts.Add(OptionalContext);

#if WITH_EDITOR
	extern ENGINE_API FString GPlayInEditorContextString;
	UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager [%s]: Adding Preload Layout: %s"), *GPlayInEditorContextString, *Layout->GetName());
#else
	UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager: Adding Preload Layout: %s"), *Layout->GetName());
#endif

}

void UCommonUILayoutManager::RemoveLayoutFromPreloadQueue(const UCommonUILayout* Layout, const UObject* OptionalContext)
{
	if (Layout)
	{
		FScopeLock Lock(&PreloadLayoutsCriticalSection);
		FCommonUILayoutPreloadData* PreloadData = PreloadLayouts.Find(Layout);
		if (!PreloadData || PreloadData->Contexts.Remove(OptionalContext) == 0)
		{
#if WITH_EDITOR
			extern ENGINE_API FString GPlayInEditorContextString;
			UE_LOG(LogCommonUILayout, Warning, TEXT("UCommonUILayoutManager [%s]: Tried to Remove a Layout from Preload that was not added: %s (%s)[%d]"), *GPlayInEditorContextString, *Layout->GetName(), OptionalContext ? *OptionalContext->GetName() : TEXT("None"), PreloadData ? PreloadData->Contexts.Num() : 0);
#else
			UE_LOG(LogCommonUILayout, Warning, TEXT("UCommonUILayoutManager: Tried to Remove a Layout from Preload that was not added: %s (%s)[%d]"), *Layout->GetName(), OptionalContext ? *OptionalContext->GetName() : TEXT("None"), PreloadData ? PreloadData->Contexts.Num() : 0);
#endif
			return;
		}

		if (PreloadData->Contexts.Num() == 0)
		{
			if (PreloadData->Handle.IsValid()) 
			{
				PreloadData->Handle->ReleaseHandle();
			}
			PreloadLayouts.Remove(Layout);
		}
	}
}

void UCommonUILayoutManager::ClearPreloadQueue()
{
	FScopeLock Lock(&PreloadLayoutsCriticalSection);

	for (TPair<const UCommonUILayout*, FCommonUILayoutPreloadData>& Pair : PreloadLayouts) 
	{
		Pair.Value.Handle->ReleaseHandle();
	}

	PreloadLayouts.Empty();
}

bool UCommonUILayoutManager::IsLayoutPreloaded(const UCommonUILayout* Layout, const UObject* OptionalContext)
{
	if (Layout)
	{
		FScopeLock Lock(&PreloadLayoutsCriticalSection);
		FCommonUILayoutPreloadData* PreloadData = PreloadLayouts.Find(Layout);
		if (PreloadData && PreloadData->Handle.IsValid())
		{
			return PreloadData->Handle->HasLoadCompleted();
		}
	}

	return false;
}

void UCommonUILayoutManager::OnLocalPlayerAdded(ULocalPlayer* Player)
{
	if (Player)
	{
		TWeakObjectPtr<ULocalPlayer> WeakPlayer(Player);
		FRootLayoutData& RootLayoutData = RootLayoutMap.Add(WeakPlayer);
		RootLayoutData.Player = WeakPlayer;
		RootLayoutData.CreateLayout();
	}
}

void UCommonUILayoutManager::OnLocalPlayerRemoved(ULocalPlayer* Player)
{
	if (Player)
	{
		TWeakObjectPtr<ULocalPlayer> WeakPlayer(Player);
		FRootLayoutData RootLayoutData;
		if (RootLayoutMap.RemoveAndCopyValue(WeakPlayer, RootLayoutData))
		{
			RootLayoutData.Reset();
		}
	}
}

void UCommonUILayoutManager::Add_Internal(const UCommonUILayout* Layout, const UObject* Context)
{
	if (!Layout)
	{
		return;
	}

	FScopeLock Lock(&ActiveLayoutsCriticalSection);

	FCommonUILayoutContextData& ContextData = ActiveLayouts.FindOrAdd(Layout);
	TArray<TWeakObjectPtr<const UObject>>& Contexts = ContextData.Contexts;
	if (Contexts.Contains(Context))
	{
		// Layout with provided context or nullptr context is already active
#if WITH_EDITOR
		extern ENGINE_API FString GPlayInEditorContextString;
		UE_LOG(LogCommonUILayout, Warning, TEXT("UCommonUILayoutManager [%s]: Tried to Add an already active Layout: %s (%s)[%d]"), *GPlayInEditorContextString, *Layout->GetName(), Context ? *Context->GetName() : TEXT("None"), Contexts.Num());
#else
		UE_LOG(LogCommonUILayout, Warning, TEXT("UCommonUILayoutManager: Tried to Add an already active Layout: %s (%s)[%d]"), *Layout->GetName(), Context ? *Context->GetName() : TEXT("None"), Contexts.Num());
#endif
		return;
	}

	Contexts.Add(Context);

#if WITH_EDITOR
	extern ENGINE_API FString GPlayInEditorContextString;
	UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager [%s]: Adding Layout: %s (%s)[%d]"), *GPlayInEditorContextString, *Layout->GetName(), Context ? *Context->GetName() : TEXT("None"), Contexts.Num());
#else
	UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager: Adding Layout: %s (%s)[%d]"), *Layout->GetName(), Context ? *Context->GetName() : TEXT("None"), Contexts.Num());
#endif
}

void UCommonUILayoutManager::Remove_Internal(const UCommonUILayout* Layout, const UObject* Context)
{
	if (!Layout)
	{
		return;
	}

	FScopeLock Lock(&ActiveLayoutsCriticalSection);

	FCommonUILayoutContextData* ContextData = ActiveLayouts.Find(Layout);
	if (!ContextData || ContextData->Contexts.Remove(Context) == 0)
	{
#if WITH_EDITOR
		extern ENGINE_API FString GPlayInEditorContextString;
		UE_LOG(LogCommonUILayout, Warning, TEXT("UCommonUILayoutManager [%s]: Tried to Remove a Layout that is not active: %s (%s)[%d]"), *GPlayInEditorContextString, *Layout->GetName(), Context ? *Context->GetName() : TEXT("None"), ContextData ? ContextData->Contexts.Num() : 0);
#else
		UE_LOG(LogCommonUILayout, Warning, TEXT("UCommonUILayoutManager: Tried to Remove a Layout that is not active: %s (%s)[%d]"), *Layout->GetName(), Context ? *Context->GetName() : TEXT("None"), ContextData ? ContextData->Contexts.Num() : 0);
#endif
		return;
	}

#if WITH_EDITOR
	extern ENGINE_API FString GPlayInEditorContextString;
	UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager [%s]: Removing Layout: %s (%s)[%d]"), *GPlayInEditorContextString, *Layout->GetName(), Context ? *Context->GetName() : TEXT("None"), ContextData->Contexts.Num());
#else
	UE_LOG(LogCommonUILayout, Log, TEXT("UCommonUILayoutManager: Removing Layout: %s (%s)[%d]"), *Layout->GetName(), Context ? *Context->GetName() : TEXT("None"), ContextData->Contexts.Num());
#endif

	if (ContextData->Contexts.Num() == 0)
	{
		ActiveLayouts.Remove(Layout);
	}
}

void UCommonUILayoutManager::RefreshVisibility()
{
	for (TPair<TWeakObjectPtr<const ULocalPlayer>, FRootLayoutData> Pair : RootLayoutMap)
	{
		FRootLayoutData& RootLayoutData = Pair.Value;
		RootLayoutData.RefreshVisibility(ActiveLayouts);
	}
}

SCommonUILayoutPanel* UCommonUILayoutManager::GetLayoutPanel(const ULocalPlayer* Player) const
{
	const FRootLayoutData* RootLayoutData = Player ? RootLayoutMap.Find(TWeakObjectPtr<const ULocalPlayer>(Player)) : nullptr;
	return RootLayoutData ? RootLayoutData->LayoutPanel.Get() : nullptr;
}

SCommonUILayoutPanel& UCommonUILayoutManager::GetOrCreateLayoutPanel(ULocalPlayer* Player)
{
	FRootLayoutData& RootLayoutData = RootLayoutMap.FindOrAdd(TWeakObjectPtr<ULocalPlayer>(Player));
	if (RootLayoutData.Player.IsExplicitlyNull())
	{
		// If the player is explicitly null, it means the root layout data is newly created.
		// If so, we need to create a layout for it.
		// FIXME: Make creation lazy based on first scene added
		TWeakObjectPtr<ULocalPlayer> WeakPlayer(Player);
		RootLayoutData.Player = WeakPlayer;
		RootLayoutData.CreateLayout();
	}

	SCommonUILayoutPanel* LayoutPanel = RootLayoutData.LayoutPanel.Get();
	ensureAlwaysMsgf(LayoutPanel, TEXT("UCommonUILayoutManager: Could not create layout panel during AddRootViewportLayout"));
	return *LayoutPanel;
}

void UCommonUILayoutManager::FRootLayoutData::CreateLayout()
{
	if (!RootLayout.IsValid() && !LayoutPanel.IsValid())
	{
		ULocalPlayer* PlayerPtr = Player.Get();
		UWorld* World = PlayerPtr ? PlayerPtr->GetWorld() : nullptr;
		if (World && !World->bIsTearingDown)
		{
			if (UGameViewportClient* ViewportClient = World->GetGameViewport())
			{
				if (ViewportClient->GetWindow().IsValid())
				{
					// Layout panel will be the parent of all the widgets managed by the Dynamic HUD for this player
					// Root layout is used as a parent to the layout panel so we can have it fill the whole screen
					RootLayout =
						SNew(SOverlay)
						+ SOverlay::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							SAssignNew(ScaleBox, SScaleBox)
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Fill)
							.Visibility(EVisibility::SelfHitTestInvisible)
							.Stretch(EStretch::UserSpecified)
							[
								SAssignNew(LayoutPanel, SCommonUILayoutPanel).AssociatedWorld(World)
							]
						];

					// Pass along the top-most widget so we can invalidate it in case layerids changes
					LayoutPanel->SetRootLayout(RootLayout);

					ApplyHUDScale();

					const int32 ZOrder = 500; // 500 is chosen because the root layout & HUD layer manager are offset to 1000 to give space for plugins
					ViewportClient->AddViewportWidgetForPlayer(PlayerPtr, RootLayout.ToSharedRef(), ZOrder);
				}
			}
		}
	}
}

void UCommonUILayoutManager::FRootLayoutData::Reset()
{
	if (RootLayout.IsValid())
	{
		ULocalPlayer* PlayerPtr = Player.Get();
		UWorld* World = PlayerPtr ? PlayerPtr->GetWorld() : nullptr;
		if (UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr)
		{
			ViewportClient->RemoveViewportWidgetForPlayer(PlayerPtr, RootLayout.ToSharedRef());
		}
	}

	if (LayoutPanel.IsValid())
	{
		LayoutPanel->ClearChildren();
		LayoutPanel.Reset();
	}

	ScaleBox.Reset();
	RootLayout.Reset();
	Player.Reset();
}

void UCommonUILayoutManager::FRootLayoutData::RefreshVisibility(const TMap<TObjectPtr<const UCommonUILayout>, FCommonUILayoutContextData>& InActiveLayouts)
{
	if (SCommonUILayoutPanel* LayoutPanelPtr = LayoutPanel.Get())
	{
		TArray<TObjectPtr<const UCommonUILayout>> Layouts;
		InActiveLayouts.GenerateKeyArray(Layouts);
		LayoutPanelPtr->RefreshChildren(Layouts);
	}
}

void UCommonUILayoutManager::FRootLayoutData::ApplyHUDScale()
{
	if (ScaleBox.IsValid())
	{
		ScaleBox->SetUserSpecifiedScale(HUDScale);
	}
}
