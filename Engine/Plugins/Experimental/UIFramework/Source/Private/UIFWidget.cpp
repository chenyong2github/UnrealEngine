// Copyright Epic Games, Inc. All Rights Reserved.

#include "UIFWidget.h"

#include "Blueprint/UserWidget.h"

#include "Engine/ActorChannel.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"


int32 UUIFWidget::GetFunctionCallspace(UFunction* Function, FFrame* Stack)
{
	if (HasAnyFlags(RF_ClassDefaultObject) || !IsSupportedForNetworking())
	{
		// This handles absorbing authority/cosmetic
		return GEngine->GetGlobalFunctionCallspace(Function, this, Stack);
	}
	return GetOuterAPlayerController()->GetFunctionCallspace(Function, Stack);
}


bool UUIFWidget::CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack)
{
	check(!HasAnyFlags(RF_ClassDefaultObject));

	AActor* Owner = GetOuterAPlayerController();

	bool bProcessed = false;
	FWorldContext* const Context = GEngine->GetWorldContextFromWorld(Owner->GetWorld());
	if (Context != nullptr)
	{
		for (FNamedNetDriver& Driver : Context->ActiveNetDrivers)
		{
			if (Driver.NetDriver != nullptr && Driver.NetDriver->ShouldReplicateFunction(Owner, Function))
			{
				Driver.NetDriver->ProcessRemoteFunction(Owner, Function, Parameters, OutParms, Stack, this);
				bProcessed = true;
			}
		}
	}

	return bProcessed;
}


UWidget* UUIFWidget::GetWidget() const
{
	return Widget;
}


void UUIFWidget::LocalCreateWidgetAsync(TFunction<void()>&& OnUserWidgetCreated)
{
	check(Widget == nullptr);
	if (Widget != nullptr)
	{
		return;
	}
	if (WidgetClassStreamableHandle && WidgetClassStreamableHandle->IsLoadingInProgress())
	{
		ensureMsgf(false, TEXT("The loading is pending. 2 LocalCreate should not be possible."));
		return;
	}

	if (WidgetClass.Get())
	{
		// the class is loaded create the widget
		LocalCreateWidget(MoveTemp(OnUserWidgetCreated));
	}
	else if (!WidgetClass.IsNull() && WidgetClass.IsPending())
	{
		// the class is not loaded
		TWeakObjectPtr<ThisClass> WeakSelf = this;
		WidgetClassStreamableHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			WidgetClass.ToSoftObjectPath()
			, [WeakSelf, MovedCallback = MoveTemp(OnUserWidgetCreated)]() mutable
			{
				if (ThisClass* StrongSelf = WeakSelf.Get())
				{
					StrongSelf->LocalCreateWidget(MoveTemp(MovedCallback));
				}
			}
			, FStreamableManager::AsyncLoadHighPriority, false, false, TEXT("UIWidget Widget Class"));
	}
	else
	{
		ensureMsgf(false, TEXT("A widget class doesn't have it's WidgetClass property set."));
	}
}


void UUIFWidget::LocalCreateWidget(TFunction<void()>&& OnUserWidgetCreated)
{
	if (UClass* Class = WidgetClass.Get())
	{
		if (Class->IsChildOf(UUserWidget::StaticClass()))
		{
			Widget = CreateWidget(GetOuterAPlayerController(), Class);
		}
		else
		{
			check(Class->IsChildOf(UWidget::StaticClass()));
			Widget = NewObject<UWidget>(this, Class, FName(), RF_Transient);
		}
		OnLocalUserWidgetCreated();
		OnUserWidgetCreated();
	}
}
