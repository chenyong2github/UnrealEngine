// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UI/FBridgeMessageHandler.h"
#include "CoreMinimal.h"
#include "WebJSFunction.h"
#include "SWebBrowser.h"
#include "Widgets/SWindow.h"
#include "BrowserBinding.generated.h"

UCLASS()
class UBrowserBinding : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	DECLARE_DELEGATE_TwoParams(FOnDialogSuccess, FString, FString);
	FOnDialogSuccess DialogSuccessDelegate;

	DECLARE_DELEGATE_TwoParams(FOnDialogFail, FString, FString);
	FOnDialogFail DialogFailDelegate;

	DECLARE_DELEGATE_OneParam(FOnDropped, FString);
	FOnDropped OnDroppedDelegate;

	DECLARE_DELEGATE_OneParam(FOnDropDiscarded, FString);
	FOnDropDiscarded OnDropDiscardedDelegate;

	DECLARE_DELEGATE_OneParam(FOnExit, FString);
	FOnExit OnExitDelegate;

	TSharedPtr<SWindow> DialogMainWindow;
	TSharedPtr<SWebBrowser> DialogMainBrowser;

	UFUNCTION()
	void DialogSuccessCallback(FWebJSFunction DialogJSCallback);

	UFUNCTION()
	void DialogFailCallback(FWebJSFunction DialogJSCallback);

	UFUNCTION()
	void OnDroppedCallback(FWebJSFunction OnDroppedJSCallback);

	UFUNCTION()
	void OnDropDiscardedCallback(FWebJSFunction OnDropDiscardedJSCallback);

	UFUNCTION()
	void OnExitCallback(FWebJSFunction OnExitJSCallback);

	UFUNCTION()
	void SendSuccess(FString Value);

	UFUNCTION()
	void SendFailure(FString Message);

	UFUNCTION()
	void ShowDialog(FString Type, FString Url);

	UFUNCTION()
	void DragStarted(TArray<FString> ImageUrl);

	void ShowLoginDialog(FString Url);

	UFUNCTION()
	void OpenExternalUrl(FString Url);

	UFUNCTION()
	FString GetProjectPath();

	UFUNCTION()
	void Logout();

	UFUNCTION()
	void OpenMegascansPluginSettings();

	TSharedRef<FBridgeMessageHandler> BridgeMessageHandler = MakeShared<FBridgeMessageHandler>();
};
