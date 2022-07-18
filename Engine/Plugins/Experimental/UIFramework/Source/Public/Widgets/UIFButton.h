// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "UIFWidget.h"

#include "UIFButton.generated.h"

class UButton;
class UTextBlock;

/**
 *
 */
UCLASS(Abstract, meta = (DisableNativeTick))
class UUIFButtonUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "UI Framework", meta = (BindWidget))
	UTextBlock* TextBlock = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "UI Framework", meta = (BindWidget))
	UButton* Button = nullptr;
};


/**
 *
 */
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_OneParam(FUIClientSideEvent, UUIFButton, OnClick, APlayerController*, PlayerControllers);

UCLASS(DisplayName = "Button UIFramework")
class UIFRAMEWORK_API UUIFButton : public UUIFWidget
{
	GENERATED_BODY()

public:
	UUIFButton();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework")
	void SetText(FText Text);

	UFUNCTION(BlueprintCallable, Category = "UI Framework")
	FText GetText() const
	{
		return Text;
	}

	virtual void OnLocalUserWidgetCreated() override;

private:
	UFUNCTION()
	void OnRep_Text();

	UFUNCTION(Server, Reliable)
	void ServerClick();

private:
	UPROPERTY(ReplicatedUsing=OnRep_Text)
	FText Text;

public:
	UPROPERTY(BlueprintAssignable)
	FUIClientSideEvent OnClick;
};
