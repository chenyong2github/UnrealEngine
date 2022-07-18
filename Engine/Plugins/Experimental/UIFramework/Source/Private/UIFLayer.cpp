// Copyright Epic Games, Inc. All Rights Reserved.

#include "UIFLayer.h"
#include "UIFPlayerComponent.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/AssetManager.h"
#include "Engine/NetDriver.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

void UUIFLayer::BeginDestroy()
{
	LocalRemoveLayerWidget();
	Super::BeginDestroy();
}


int32 UUIFLayer::GetFunctionCallspace(UFunction* Function, FFrame* Stack)
{
	if (HasAnyFlags(RF_ClassDefaultObject) || !IsSupportedForNetworking())
	{
		// This handles absorbing authority/cosmetic
		return GEngine->GetGlobalFunctionCallspace(Function, this, Stack);
	}
	return GetOuterAPlayerController()->GetFunctionCallspace(Function, Stack);
}


bool UUIFLayer::CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack)
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


void UUIFLayer::LocalAddLayerWidget(int32 InZOrder, EUIFLayerType InLayerType)
{
	ZOrder = InZOrder;
	LayerType = InLayerType;
	LocalCreateLayerWidgetAsync(InZOrder, InLayerType);
}


void UUIFLayer::LocalRemoveLayerWidget()
{
	if (LayerWidgetClassStreamableHandle)
	{
		LayerWidgetClassStreamableHandle->CancelHandle();
		LayerWidgetClassStreamableHandle.Reset();
	}

	if (LayerWidget)
	{
		OnLocalPreRemoveLayerWidget();
		LayerWidget->RemoveFromParent();
		LayerWidget = nullptr;
	}
}


void UUIFLayer::LocalCreateLayerWidgetAsync(int32 InZOrder, EUIFLayerType InLayerType)
{
	check(LayerWidget == nullptr);
	if (LayerWidget != nullptr)
	{
		return;
	}
	if (LayerWidgetClassStreamableHandle && LayerWidgetClassStreamableHandle->IsLoadingInProgress())
	{
		ensureMsgf(false, TEXT("The loading is pending. 2 LocalCreate should not be possible."));
		return;
	}

	if (LayerWidgetClass.Get())
	{
		// the class is loaded create the widget
		LocalCreateLayerWidget(InZOrder, InLayerType);
	}
	else if (!LayerWidgetClass.IsNull() && LayerWidgetClass.IsPending())
	{
		// the class is not loaded
		TWeakObjectPtr<ThisClass> WeakSelf = this;
		LayerWidgetClassStreamableHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			LayerWidgetClass.ToSoftObjectPath(),
			[WeakSelf, InZOrder, InLayerType]()
			{
				if (ThisClass* StrongSelf = WeakSelf.Get())
				{
					StrongSelf->LocalCreateLayerWidget(InZOrder, InLayerType);
				}
			},
			FStreamableManager::AsyncLoadHighPriority, false, false, TEXT("Layer Class"));
	}
	else
	{
		ensureMsgf(false, TEXT("The layer doesn't have it's LayerWidgetClass property set."));
	}
}


void UUIFLayer::LocalCreateLayerWidget(int32 InZOrder, EUIFLayerType InLayerType)
{
	if (UClass* Class = LayerWidgetClass.Get())
	{
		LayerWidget = CreateWidget<UUserWidget>(GetOuterAPlayerController(), Class);
		if (InLayerType == EUIFLayerType::Viewport)
		{
			LayerWidget->AddToViewport(InZOrder);
		}
		else
		{
			LayerWidget->AddToPlayerScreen(InZOrder);
		}

		OnLocalLayerWidgetAdded();
	}
	LayerWidgetClassStreamableHandle.Reset();
}
