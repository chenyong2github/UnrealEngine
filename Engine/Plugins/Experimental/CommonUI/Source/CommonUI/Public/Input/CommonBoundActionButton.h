// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonButtonBase.h"
#include "UIActionBindingHandle.h"
#include "CommonBoundActionButton.generated.h"

class UCommonTextBlock;


UCLASS(Abstract, meta = (DisableNativeTick))
class COMMONUI_API UCommonBoundActionButton : public UCommonButtonBase
{
	GENERATED_BODY()

public:
	void SetRepresentedAction(FUIActionBindingHandle InBindingHandle);

protected:
	virtual void NativeOnClicked() override;
	virtual void NativeOnCurrentTextStyleChanged() override;

	virtual void UpdateInputActionWidget() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Common Bound Action")
	void OnUpdateInputAction();

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Text Block")
	UCommonTextBlock* Text_ActionName;

private:
	FUIActionBindingHandle BindingHandle;
};