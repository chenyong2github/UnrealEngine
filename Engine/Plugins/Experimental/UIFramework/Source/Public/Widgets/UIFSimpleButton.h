// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Types/UIFEvents.h"
#include "UIFWidget.h"

#include "Localization/VerseReplicationMessage.h"

#include "UIFSimpleButton.generated.h"

struct FMVVMEventField;

/**
 *
 */
UCLASS(DisplayName = "Simple Button UIFramework")
class UIFRAMEWORK_API UUIFrameworkSimpleButton : public UUIFrameworkWidget
{
	GENERATED_BODY()

public:
	UUIFrameworkSimpleButton();

private:
	UPROPERTY(BlueprintReadOnly, Getter, FieldNotify, Category="UI Framework", meta = (AllowPrivateAccess = "true"))
	FText Text;

	UPROPERTY(ReplicatedUsing=OnRep_Message, FieldNotify)
	FVerseReplicationMessage Message;

	UPROPERTY(BlueprintReadOnly, Getter, FieldNotify, Category = "UI Framework", meta = (DisallowedViewAccess = "true", AllowPrivateAccess = "true"))
	FUIFrameworkClickEventArgument ClickEvent;

public:

	FText GetText() const
	{
		return Text;
	}

	void SetMessage(FVerseReplicationMessage&& InMessage);

	const FUIFrameworkClickEventArgument& GetClickEvent() const
	{
		return ClickEvent;
	}

	virtual void LocalOnUMGWidgetCreated() override;

	UFUNCTION(BlueprintCallable, Category = "UI Framework")
	void OnClick(FMVVMEventField Field);

private:

	UFUNCTION()
	void OnRep_Message();

	UFUNCTION(Server, Reliable)
	void ServerClick(APlayerController* PlayerController);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Types/MVVMEventField.h"
#endif
