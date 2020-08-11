// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamOutputProviderBase.h"

#include "VCamModifierInterface.h"
#include "VCamComponent.h"

#include "Blueprint/UserWidget.h"

DEFINE_LOG_CATEGORY(LogVCamOutputProvider);

UVCamOutputProviderBase::UVCamOutputProviderBase()
{
	bIsActive = false;
	bIsPaused = false;
	bInitialized = false;
}

UVCamOutputProviderBase::~UVCamOutputProviderBase()
{
	Destroy();
}

void UVCamOutputProviderBase::InitializeSafe()
{
	if (!bInitialized)
	{
		bInitialized = true;
	}
}

void UVCamOutputProviderBase::Destroy()
{
	if (bIsActive)
	{
		SetPause(false);
		SetActive(false);
	}

	bInitialized = false;
}

void UVCamOutputProviderBase::Tick(const float DeltaTime)
{
	if (bIsActive && UMGWidget && UMGClass)
	{
		UMGWidget->Tick(DeltaTime);
	}
}

void UVCamOutputProviderBase::SetActive(const bool InActive)
{
	bIsActive = InActive;

	if (InActive)
	{
		CreateUMG();
		DisplayUMG();
	}
	else
	{
		DestroyUMG();
	}
}

void UVCamOutputProviderBase::SetPause(const bool InPause)
{
	if (InPause && bIsActive)
	{
		SetActive(false);
		bIsPaused = true;
	}
	else if (!InPause && !bIsActive && bIsPaused)
	{
		SetActive(true);
		bIsPaused = false;
	}
}

void UVCamOutputProviderBase::SetTargetCamera(const UCineCameraComponent* InTargetCamera)
{
	TargetCamera = InTargetCamera;

	NotifyWidgetOfComponentChange();
}

void UVCamOutputProviderBase::SetUMGClass(const TSubclassOf<UUserWidget> InUMGClass)
{
	UMGClass = InUMGClass;
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

	UMGWidget = NewObject<UVPFullScreenUserWidget>(GetTransientPackage(), UVPFullScreenUserWidget::StaticClass());
	UMGWidget->SetDisplayTypes(DisplayType, DisplayType, DisplayType);
	UMGWidget->PostProcessDisplayType.bReceiveHardwareInput = true;

	UMGWidget->WidgetClass = UMGClass;
	UE_LOG(LogVCamOutputProvider, Log, TEXT("CreateUMG widget named %s from class %s"), *UMGWidget->GetName(), *UMGWidget->WidgetClass->GetName());
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
			UE_LOG(LogVCamOutputProvider, Log, TEXT("DisplayUMG widget displayed in WorldType %d"), WorldType);
		}

		NotifyWidgetOfComponentChange();
	}
}

void UVCamOutputProviderBase::DestroyUMG()
{
	if (UMGWidget)
	{
		if (UMGWidget->IsDisplayed())
		{
			UMGWidget->Hide();
		}
		UMGWidget->ConditionalBeginDestroy();
		UMGWidget = nullptr;
	}
}

void UVCamOutputProviderBase::NotifyWidgetOfComponentChange() const
{
	if (UMGWidget && UMGWidget->IsDisplayed())
	{
		UUserWidget* DisplayedWidget = UMGWidget->GetWidget();
		if (DisplayedWidget && DisplayedWidget->Implements<UVCamModifierInterface>())
		{
			if (UVCamComponent* OwningComponent = Cast<UVCamComponent>(this->GetOuter()))
			{
				UVCamComponent* CameraComponent = bIsActive ? OwningComponent : nullptr;

				IVCamModifierInterface::Execute_OnVCamComponentChanged(DisplayedWidget, CameraComponent);
			}
		}

	}
}

#if WITH_EDITOR
void UVCamOutputProviderBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;

	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		static FName NAME_IsActive = GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, bIsActive);
		static FName NAME_UMGClass = GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, UMGClass);

		if (Property->GetFName() == NAME_IsActive)
		{
			SetActive(bIsActive);
		}
		else if (Property->GetFName() == NAME_UMGClass)
		{
			if (bIsActive)
			{
				SetActive(false);
				SetActive(true);
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
