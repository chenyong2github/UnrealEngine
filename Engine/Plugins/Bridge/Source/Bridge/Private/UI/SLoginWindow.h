// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SWindow.h"
#include "Widgets/SCompoundWidget.h"
#include "WebJSFunction.h"
#include "Widgets/Input/SComboBox.h"
#include "Internationalization/Text.h"
#include "SLoginWindow.generated.h"



UCLASS()
class UMegascansAuthentication :
	public UObject
{
	GENERATED_UCLASS_BODY()
		FString LoginCode;

public:
	DECLARE_DELEGATE_OneParam(FOnLoginCompleted, FString);
	FOnLoginCompleted LoginCompleteDelegate;
	TSharedPtr<SWindow> LoginMainWindow;

	UFUNCTION()
		void LoginCallback(FWebJSFunction LoginJSCallback);

	UFUNCTION()
		void InitiateAuthentication();
};



class SMegascansLoginWindow :
	public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SMegascansLoginWindow) {}
	SLATE_END_ARGS()
public:
	void Construct(const FArguments& InArgs);
	void HandleBrowserUrlChanged(const FText& Url);
};
