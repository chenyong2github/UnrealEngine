// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/Widget.h"
#include "UObject/SoftObjectPtr.h"

#include "UIFWidget.generated.h"

struct FStreamableHandle;

/**
 * 
 */
UCLASS(Abstract, BlueprintType, Within=PlayerController)
class UIFRAMEWORK_API UUIFWidget : public UObject
{
	GENERATED_BODY()

public:
	//~ Begin UObject
	virtual bool IsSupportedForNetworking() const override { return true; }
	virtual int32 GetFunctionCallspace(UFunction* Function, FFrame* Stack) override;
	virtual bool CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack) override;
	//~ End UObject

	UWidget* GetWidget() const;

	void LocalCreateWidgetAsync(TFunction<void()>&& OnUserWidgetCreated);

protected:
	virtual void OnLocalUserWidgetCreated() { }

private:
	void LocalCreateWidget(TFunction<void()>&& OnUserWidgetCreated);

protected:
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = "UI Framework")
	TSoftClassPtr<UWidget> WidgetClass; // todo: make this private and use a constructor argument

private:
	//UPROPERTY(BlueprintReadWrite)
	//EUIEnability Enablility;
	//
	//UPROPERTY(BlueprintReadWrite)
	//EUIVisibility Visibility;

	// only on client
	UPROPERTY(Transient)
	TObjectPtr<UWidget> Widget;

	TSharedPtr<FStreamableHandle> WidgetClassStreamableHandle;
};
