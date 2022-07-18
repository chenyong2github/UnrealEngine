// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "UObject/SoftObjectPtr.h"

#include "UIFLayer.generated.h"

class UUIFWidget;
struct FStreamableHandle;


/**
 *
 */
UENUM(BlueprintType)
enum class EUIFLayerType : uint8
{
	Viewport,
	PlayerScreen,
};


/**
 * 
 */
UCLASS(Abstract, Blueprintable, Within=PlayerController)
class UIFRAMEWORK_API UUIFLayer : public UObject
{
	GENERATED_BODY()

public:
	//~ Begin UObject
	virtual bool IsSupportedForNetworking() const override { return true; }
	virtual int32 GetFunctionCallspace(UFunction* Function, FFrame* Stack) override;
	virtual bool CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack) override;
	virtual void BeginDestroy() override;
	//~ End UObject

	const UUserWidget* GetLayerWidget() const
	{
		return LayerWidget;
	}

	UUserWidget* GetLayerWidget()
	{
		return LayerWidget;
	}

	TSoftClassPtr<UUserWidget> GetLayerWidgetSoftClass() const
	{
		return LayerWidgetClass;
	}

	virtual bool ReplicateSubobjects(UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags)
	{
		return false;
	}

public:
	void LocalAddLayerWidget(int32 ZOrder, EUIFLayerType LayerType);
	void LocalRemoveLayerWidget();

private:
	void LocalCreateLayerWidgetAsync(int32 ZOrder, EUIFLayerType LayerType);
	void LocalCreateLayerWidget(int32 ZOrder, EUIFLayerType LayerType);

protected:
	virtual void OnLocalLayerWidgetAdded() { }
	virtual void OnLocalPreRemoveLayerWidget() { }

protected:
	//~ Since UObject can't have constructor, initialize LayerWidgetClass manually. That value cannot changed at runtime.
	UPROPERTY()
	TSoftClassPtr<UUserWidget> LayerWidgetClass;

private:
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> LayerWidget;

	TSharedPtr<FStreamableHandle> LayerWidgetClassStreamableHandle;
	int32 ZOrder = 0;
	EUIFLayerType LayerType = EUIFLayerType::PlayerScreen;
};
