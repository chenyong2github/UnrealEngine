// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonUILayoutManager.h"
#include "CommonUILayout.h"
#include "CommonUILayoutConstraints.h"
#include "CommonUILayoutLog.h"
#include "SCommonUILayoutPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScaleBox.h"

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
	static FAutoConsoleCommand RefreshVisibility(
		TEXT("CommonUILayout.RefreshVisibility"),
		TEXT("Refresh the visibility of the widgets allowed/unallowed by reevaluating the layouts registered in CommonUILayout."),
		FConsoleCommandDelegate::CreateUObject(this, &UCommonUILayoutManager::RefreshVisibility)
	);
}

void UCommonUILayoutManager::Deinitialize()
{
	DestroyRootLayout();
}

COMMONUILAYOUT_API UCommonUILayoutManager* UCommonUILayoutManager::GetInstance(const UWorld* World)
{
	return World ? World->GetSubsystem<UCommonUILayoutManager>() : nullptr;
}

void UCommonUILayoutManager::SetHUDScale(const float InHUDScale)
{
	if (HUDScale != InHUDScale)
	{
		HUDScale = InHUDScale;
		ApplyHUDScale();
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

FName UCommonUILayoutManager::GetUniqueIDForWidget(UUserWidget* Widget) const
{
	SCommonUILayoutPanel* LayoutPanelPtr = LayoutPanel.Get();
	return LayoutPanelPtr ? LayoutPanelPtr->FindUniqueIDForWidget(Widget) : FName();
}

TWeakObjectPtr<UUserWidget> UCommonUILayoutManager::FindUserWidgetWithUniqueID(const TSoftClassPtr<UUserWidget>& WidgetClass, const FName& UniqueID) const
{
	SCommonUILayoutPanel* LayoutPanelPtr = LayoutPanel.Get();
	return LayoutPanelPtr ? LayoutPanelPtr->FindUserWidgetWithUniqueID(WidgetClass, UniqueID) : nullptr;
}

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

void UCommonUILayoutManager::CreateRootPanel()
{
	if (!RootPanelData.RootPanel.IsValid() && !LayoutPanel.IsValid() && ActiveLayouts.Num() > 0)
	{
		UWorld* World = GetWorld();
		if (World && !World->bIsTearingDown)
		{
			if (UGameViewportClient* ViewportClient = World->GetGameViewport())
			{
				// FIXME: This doesn't work for splitscreen
				ULocalPlayer* Player = World->GetFirstLocalPlayerFromController();
				if (Player && ViewportClient->GetWindow().IsValid())
				{
					RootPanelData.Player = TWeakObjectPtr<ULocalPlayer>(Player);

					// Layout panel will be the parent of all the widgets managed by the Dynamic HUD
					// Root layout is used as a parent to the layout panel so we can have it fill the whole screen
					RootPanelData.RootPanel =
						SNew(SOverlay)
						+ SOverlay::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							SAssignNew(ScaleBox, SScaleBox)
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Fill)
							.Stretch(EStretch::UserSpecified)
							[
								SAssignNew(LayoutPanel, SCommonUILayoutPanel).AssociatedWorld(World)
							]
						];

					// Pass along the top-most widget so we can invalidate it in case layerids changes
					LayoutPanel->SetRootLayout(RootPanelData.RootPanel);

					ApplyHUDScale();

					const int32 ZOrder = 500; // 500 is chosen because the root layout & HUD layer manager are offset to 1000 to give space for plugins
					ViewportClient->AddViewportWidgetForPlayer(Player, RootPanelData.RootPanel.ToSharedRef(), ZOrder);
				}
			}
		}
	}
}

void UCommonUILayoutManager::DestroyRootLayout()
{
	if (LayoutPanel.IsValid())
	{
		LayoutPanel->ClearChildren();
		LayoutPanel.Reset();
	}

	ScaleBox.Reset();
	RootPanelData.Reset(GetWorld());
}

void UCommonUILayoutManager::ApplyHUDScale()
{
	if (ScaleBox.IsValid())
	{
		ScaleBox->SetUserSpecifiedScale(HUDScale);
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
	CreateRootPanel();

	if (SCommonUILayoutPanel* LayoutPanelPtr = LayoutPanel.Get())
	{
		TArray<TObjectPtr<const UCommonUILayout>> Layouts;
		ActiveLayouts.GenerateKeyArray(Layouts);
		LayoutPanelPtr->RefreshChildren(Layouts);
	}
}

void UCommonUILayoutManager::FRootLayoutData::Reset(const UWorld* World)
{
	if (RootPanel.IsValid())
	{
		if (World && World->IsGameWorld())
		{
			if (UGameViewportClient* ViewportClient = World->GetGameViewport())
			{
				ViewportClient->RemoveViewportWidgetForPlayer(Player.Get(), RootPanel.ToSharedRef());
			}
		}
	}

	RootPanel.Reset();
	Player.Reset();
}
