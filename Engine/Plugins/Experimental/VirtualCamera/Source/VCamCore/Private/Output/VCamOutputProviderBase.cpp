// Copyright Epic Games, Inc. All Rights Reserved.

#include "Output/VCamOutputProviderBase.h"

#include "Modifier/VCamModifierInterface.h"
#include "VCamComponent.h"
#include "UI/VCamWidget.h"
#include "Util/LevelViewportUtils.h"
#include "VCamCoreCustomVersion.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"
#include "UObject/UObjectBaseUtility.h"
#include "Util/ConnectionUtils.h"
#include "Util/WidgetSnapshotUtils.h"
#include "Util/WidgetTreeUtils.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IAssetViewport.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"
#include "SEditorViewport.h"
#include "UnrealClient.h"
#else
#include "Engine/GameEngine.h"
#endif

DEFINE_LOG_CATEGORY(LogVCamOutputProvider);

namespace UE::VCamCore::Private
{
	static const FName LevelEditorName(TEXT("LevelEditor"));
}

UVCamOutputProviderBase::UVCamOutputProviderBase()
{
	OnActivatedDelegate.AddLambda([this](bool bNewValue)
	{
		OnActivatedDelegate_Blueprint.Broadcast(bNewValue);
	});
}

void UVCamOutputProviderBase::BeginDestroy()
{
	Deinitialize();

	Super::BeginDestroy();
}

void UVCamOutputProviderBase::Initialize()
{
	bool bWasInitialized = bInitialized;
	bInitialized = true;

	// Reactivate the provider if it was previously set to active
	if (!bWasInitialized && bIsActive)
	{
#if WITH_EDITOR
		// If the editor viewports aren't fully initialized, then delay initialization for the entire Output Provider
		if (GEditor && GEditor->GetActiveViewport() && (GEditor->GetActiveViewport()->GetSizeXY().X < 1))
		{
			bInitialized = false;
		}
		else
#endif
		{
			if (IsOuterComponentEnabled())
			{
				Activate();
			}
		}
	}
}

void UVCamOutputProviderBase::Deinitialize()
{
	if (bInitialized)
	{
		Deactivate();
		bInitialized = false;
	}
}

void UVCamOutputProviderBase::Activate()
{
	CreateUMG();
	DisplayUMG();

	if (ShouldOverrideResolutionOnActivationEvents() && bUseOverrideResolution)
	{
		ApplyOverrideResolutionForViewport(TargetViewport);
	}
	
	OnActivatedDelegate.Broadcast(true);
}

void UVCamOutputProviderBase::Deactivate()
{
	if (ShouldOverrideResolutionOnActivationEvents())
	{
		RestoreOverrideResolutionForViewport(TargetViewport);
	}
	
	DestroyUMG();
	
	OnActivatedDelegate.Broadcast(false);
}

void UVCamOutputProviderBase::Tick(const float DeltaTime)
{
	if (bIsActive && UMGWidget && UMGClass)
	{
		UMGWidget->Tick(DeltaTime);
	}
}

void UVCamOutputProviderBase::SetActive(const bool bInActive)
{
	bIsActive = bInActive;

	if (IsOuterComponentEnabled())
	{
		if (bIsActive)
		{
			Activate();
		}
		else
		{
			Deactivate();
		}
	}
}

bool UVCamOutputProviderBase::IsOuterComponentEnabled() const
{
	if (UVCamComponent* OuterComponent = GetTypedOuter<UVCamComponent>())
	{
		return OuterComponent->IsEnabled();
	}

	return false;
}


void UVCamOutputProviderBase::SetTargetCamera(const UCineCameraComponent* InTargetCamera)
{
	TargetCamera = InTargetCamera;
	NotifyWidgetOfComponentChange();
}

void UVCamOutputProviderBase::CreateUMG()
{
	if (!UMGClass)
	{
		return;
	}

	if (UMGWidget)
	{
		UE_LOG(LogVCamOutputProvider, Error, TEXT("CreateUMG widget already set - failed to create"));
		return;
	}

	UMGWidget = NewObject<UVPFullScreenUserWidget>(this, UVPFullScreenUserWidget::StaticClass());
	UMGWidget->SetDisplayTypes(DisplayType, DisplayType, DisplayType);
	UMGWidget->PostProcessDisplayType.bReceiveHardwareInput = true;

#if WITH_EDITOR
	UMGWidget->SetAllTargetViewports(GetTargetLevelViewport());
#endif

	UMGWidget->WidgetClass = UMGClass;
	UE_LOG(LogVCamOutputProvider, Log, TEXT("CreateUMG widget named %s from class %s"), *UMGWidget->GetName(), *UMGWidget->WidgetClass->GetName());
}

void UVCamOutputProviderBase::RestoreOverrideResolutionForViewport(EVCamTargetViewportID ViewportToRestore)
{
	if (const TSharedPtr<FSceneViewport> TargetSceneViewport = GetSceneViewport(ViewportToRestore))
	{
		TargetSceneViewport->SetFixedViewportSize(0, 0);
	}
}

void UVCamOutputProviderBase::ApplyOverrideResolutionForViewport(EVCamTargetViewportID Viewport)
{
	if (const TSharedPtr<FSceneViewport> TargetSceneViewport = GetSceneViewport(Viewport))
	{
		TargetSceneViewport->SetFixedViewportSize(OverrideResolution.X, OverrideResolution.Y);
	}
}

void UVCamOutputProviderBase::ReapplyOverrideResolution(EVCamTargetViewportID Viewport)
{
	if (bUseOverrideResolution)
	{
		ApplyOverrideResolutionForViewport(Viewport);
	}
	else
	{
		RestoreOverrideResolutionForViewport(Viewport);
	}
}

void UVCamOutputProviderBase::DisplayUMG()
{
	if (UMGWidget)
	{
		UWorld* ActorWorld = nullptr;
		int32 WorldType = -1;

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				// Prioritize PIE and Game modes if active
				if ((Context.WorldType == EWorldType::PIE) || (Context.WorldType == EWorldType::Game))
				{
					ActorWorld = Context.World();
					WorldType = (int32)Context.WorldType;
					break;
				}
				else if (Context.WorldType == EWorldType::Editor)
				{
					// Only grab the Editor world if PIE and Game aren't available
					ActorWorld = Context.World();
					WorldType = (int32)Context.WorldType;
				}
			}
		}

		if (ActorWorld)
		{
			UMGWidget->Display(ActorWorld);
			if (UUserWidget* Subwidget = UMGWidget->GetWidget(); ensure(Subwidget) && WidgetSnapshot.HasData())
			{
				UE::VCamCore::WidgetSnapshotUtils::Private::ApplyTreeHierarchySnapshot(WidgetSnapshot, *Subwidget);
				// Note that NotifyWidgetOfComponentChange will cause InitializeConnections to be called - this is important for the connections to get applied!
			}
			UE_LOG(LogVCamOutputProvider, Log, TEXT("DisplayUMG widget displayed in WorldType %d"), WorldType);
		}

		NotifyWidgetOfComponentChange();
#if WITH_EDITOR
		// Start registering after the initial calls to InitializeConnections to prevent unneeded snapshotting.
		StartDetectAndSnapshotWhenConnectionsChange();
#endif
	}
}

void UVCamOutputProviderBase::DestroyUMG()
{
	if (UMGWidget)
	{
		if (UMGWidget->IsDisplayed())
		{
#if WITH_EDITOR
			// The state only needs to be saved in the editor
			if (UUserWidget* Subwidget = UMGWidget->GetWidget(); ensure(Subwidget))
			{
				StopDetectAndSnapshotWhenConnectionsChange();
				Modify();
				WidgetSnapshot = UE::VCamCore::WidgetSnapshotUtils::Private::TakeTreeHierarchySnapshot(*Subwidget);
			}
#endif
			
			UMGWidget->Hide();
			UE_LOG(LogVCamOutputProvider, Log, TEXT("DestroyUMG widget %s hidden"), *UMGWidget->GetName());
		}
		UE_LOG(LogVCamOutputProvider, Log, TEXT("DestroyUMG widget %s destroyed"), *UMGWidget->GetName());

#if WITH_EDITOR
		UMGWidget->ResetAllTargetViewports();
#endif

		UMGWidget->ConditionalBeginDestroy();
		UMGWidget = nullptr;
	}
}

void UVCamOutputProviderBase::SuspendOutput()
{
	if (IsActive())
	{
		bWasActive = true;
		SetActive(false);
	}
}

void UVCamOutputProviderBase::RestoreOutput()
{
	if (bWasActive && !IsActive())
	{
		SetActive(true);
	}
	bWasActive = false;
}

bool UVCamOutputProviderBase::NeedsForceLockToViewport() const
{
	// The widget is displayed via a post process material, which is applied to the camera's post process settings, hence anything will only be visible when locked.
	return DisplayType == EVPWidgetDisplayType::PostProcess;
}

void UVCamOutputProviderBase::NotifyWidgetOfComponentChange() const
{
	if (UMGWidget && UMGWidget->IsDisplayed())
	{
		UMGWidget->SetCustomPostProcessSettingsSource(TargetCamera.Get());
		
		UUserWidget* DisplayedWidget = UMGWidget->GetWidget();
		if (IsValid(DisplayedWidget))
		{
			if (UVCamComponent* OwningComponent = GetTypedOuter<UVCamComponent>())
			{
				UVCamComponent* VCamComponent = bIsActive ? OwningComponent : nullptr;
				
				// Find all VCam Widgets inside the displayed widget and Initialize them with the owning VCam Component
				UE::VCamCore::ForEachWidgetToConsiderForVCam(*DisplayedWidget, [VCamComponent](UWidget* Widget)
				{
					if (UVCamWidget* VCamWidget = Cast<UVCamWidget>(Widget))
					{
						VCamWidget->InitializeConnections(VCamComponent);
					}
					
					if (Widget->Implements<UVCamModifierInterface>())
					{
						IVCamModifierInterface::Execute_OnVCamComponentChanged(Widget, VCamComponent);
					}
				});
			}
		}
	}
}

void UVCamOutputProviderBase::Serialize(FArchive& Ar)
{
	using namespace UE::VCamCore;
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FVCamCoreCustomVersion::GUID);
	
	if (Ar.IsLoading() && Ar.CustomVer(FVCamCoreCustomVersion::GUID) < FVCamCoreCustomVersion::MoveTargetViewportFromComponentToOutput)
	{
		UVCamComponent* OuterComponent = GetTypedOuter<UVCamComponent>();
		TargetViewport = OuterComponent
			? OuterComponent->TargetViewport_DEPRECATED
			: TargetViewport;
	}
}

UVCamOutputProviderBase* UVCamOutputProviderBase::GetOtherOutputProviderByIndex(int32 Index) const
{
	if (Index > INDEX_NONE)
	{
		if (UVCamComponent* OuterComponent = GetTypedOuter<UVCamComponent>())
		{
			if (UVCamOutputProviderBase* Provider = OuterComponent->GetOutputProviderByIndex(Index))
			{
				return Provider;
			}
			else
			{
				UE_LOG(LogVCamOutputProvider, Warning, TEXT("GetOtherOutputProviderByIndex - specified index is out of range"));
			}
		}
	}

	return nullptr;
}

TSharedPtr<FSceneViewport> UVCamOutputProviderBase::GetSceneViewport(EVCamTargetViewportID InTargetViewport) const
{
	TSharedPtr<FSceneViewport> SceneViewport;

#if WITH_EDITOR
	if (GIsEditor)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE)
			{
				FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
				if (SlatePlayInEditorSession)
				{
					if (SlatePlayInEditorSession->DestinationSlateViewport.IsValid())
					{
						TSharedPtr<IAssetViewport> DestinationLevelViewport = SlatePlayInEditorSession->DestinationSlateViewport.Pin();
						SceneViewport = DestinationLevelViewport->GetSharedActiveViewport();
					}
					else if (SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
					{
						SceneViewport = SlatePlayInEditorSession->SlatePlayInEditorWindowViewport;
					}

					// If PIE is active always choose it
					break;
				}
			}
			else if (Context.WorldType == EWorldType::Editor)
			{
				TSharedPtr<SLevelViewport> Viewport = UE::VCamCore::LevelViewportUtils::Private::GetLevelViewport(InTargetViewport);
				FLevelEditorViewportClient* LevelViewportClient = Viewport
					? &Viewport->GetLevelViewportClient()
					: nullptr;
				if (LevelViewportClient)
				{
					TSharedPtr<SEditorViewport> ViewportWidget = LevelViewportClient->GetEditorViewportWidget();
					if (ViewportWidget.IsValid())
					{
						SceneViewport = ViewportWidget->GetSceneViewport();
					}
				}
			}
		}
	}
#else
	if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
	{
		SceneViewport = GameEngine->SceneViewport;
	}
#endif

	return SceneViewport;
}

TWeakPtr<SWindow> UVCamOutputProviderBase::GetTargetInputWindow() const
{
	TWeakPtr<SWindow> InputWindow;

#if WITH_EDITOR
	if (GIsEditor)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE)
			{
				FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
				if (SlatePlayInEditorSession)
				{
					if (SlatePlayInEditorSession->DestinationSlateViewport.IsValid())
					{
						TSharedPtr<IAssetViewport> DestinationLevelViewport = SlatePlayInEditorSession->DestinationSlateViewport.Pin();
						InputWindow = FSlateApplication::Get().FindWidgetWindow(DestinationLevelViewport->AsWidget());
					}
					else if (SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
					{
						InputWindow = SlatePlayInEditorSession->SlatePlayInEditorWindow;
					}

					// If PIE is active always choose it
					break;
				}
			}
			else if (Context.WorldType == EWorldType::Editor)
			{
				if (FLevelEditorViewportClient* LevelViewportClient = GetTargetLevelViewportClient())
				{
					TSharedPtr<SEditorViewport> ViewportWidget = LevelViewportClient->GetEditorViewportWidget();
					if (ViewportWidget.IsValid())
					{
						InputWindow = FSlateApplication::Get().FindWidgetWindow(ViewportWidget.ToSharedRef());
					}
				}
			}
		}
	}
#else
	if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
	{
		InputWindow = GameEngine->GameViewportWindow;
	}
#endif

	return InputWindow;
}

#if WITH_EDITOR

FLevelEditorViewportClient* UVCamOutputProviderBase::GetTargetLevelViewportClient() const
{
	const TSharedPtr<SLevelViewport> LevelViewport = GetTargetLevelViewport();
	return LevelViewport
		? &LevelViewport->GetLevelViewportClient()
		: nullptr;
}

TSharedPtr<SLevelViewport> UVCamOutputProviderBase::GetTargetLevelViewport() const
{
	return UE::VCamCore::LevelViewportUtils::Private::GetLevelViewport(TargetViewport);
}

void UVCamOutputProviderBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FProperty* Property = PropertyChangedEvent.MemberProperty;
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
		&& Property
		&& PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		static FName NAME_IsActive = GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, bIsActive);
		static FName NAME_UMGClass = GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, UMGClass);
		static FName NAME_TargetViewport = GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, TargetViewport);
		static FName NAME_OverrideResolution = GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, OverrideResolution);
		static FName NAME_bUseOverrideResolution = GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, bUseOverrideResolution);

		const FName PropertyName = Property->GetFName();
		if (PropertyName == NAME_IsActive)
		{
			SetActive(bIsActive);
		}
		else if (PropertyName == NAME_UMGClass)
		{
			WidgetSnapshot.Reset();
			if (bIsActive)
			{
				SetActive(false);
				SetActive(true);
			}
		}
		else if (PropertyName == NAME_TargetViewport)
		{
			for (int32 i = 0; i < static_cast<int32>(EVCamTargetViewportID::Count); ++i)
			{
				RestoreOverrideResolutionForViewport(static_cast<EVCamTargetViewportID>(i));
			}
			ApplyOverrideResolutionForViewport(TargetViewport);

			if (bIsActive)
			{
				SetActive(false);
				SetActive(true);
			}
		}
		else if (PropertyName == NAME_OverrideResolution || PropertyName == NAME_bUseOverrideResolution)
		{
			ReapplyOverrideResolution(TargetViewport);
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UVCamOutputProviderBase::StartDetectAndSnapshotWhenConnectionsChange()
{
	check(UMGWidget);
	UUserWidget* Widget = UMGWidget->GetWidget();
	check(Widget);
	
	UE::VCamCore::ForEachWidgetToConsiderForVCam(*Widget, [this](UWidget* Widget)
	{
		if (UVCamWidget* VCamWidget = Cast<UVCamWidget>(Widget))
		{
			const TWeakObjectPtr<UVCamWidget> WeakWidget = VCamWidget;
			VCamWidget->OnPostConnectionsReinitializedDelegate.AddUObject(this, &UVCamOutputProviderBase::OnConnectionReinitialized, WeakWidget);
		}
	});
}

void UVCamOutputProviderBase::StopDetectAndSnapshotWhenConnectionsChange()
{
	check(UMGWidget);
	UUserWidget* Widget = UMGWidget->GetWidget();
	check(Widget);
	
	UE::VCamCore::ForEachWidgetToConsiderForVCam(*Widget, [this](UWidget* Widget)
	{
		if (UVCamWidget* VCamWidget = Cast<UVCamWidget>(Widget))
		{
			VCamWidget->OnPostConnectionsReinitializedDelegate.RemoveAll(this);
		}
	});
}

void UVCamOutputProviderBase::OnConnectionReinitialized(TWeakObjectPtr<UVCamWidget> Widget)
{
	if (Widget.IsValid())
	{
		if (WidgetSnapshot.HasData())
		{
			Modify();
			UE::VCamCore::WidgetSnapshotUtils::Private::RetakeSnapshotForWidgetInHierarchy(WidgetSnapshot, *Widget.Get());
		}
		else if (UMGWidget && ensure(UMGWidget->GetWidget()))
		{
			Modify();
			WidgetSnapshot = UE::VCamCore::WidgetSnapshotUtils::Private::TakeTreeHierarchySnapshot(*UMGWidget->GetWidget());
		}
	}
}

#endif