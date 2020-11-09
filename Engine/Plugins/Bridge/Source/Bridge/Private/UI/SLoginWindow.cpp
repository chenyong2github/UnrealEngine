// Copyright Epic Games, Inc. All Rights Reserved.
#include "UI/SLoginWindow.h"
#include "UI/BridgeUIManager.h"

UMegascansAuthentication::UMegascansAuthentication(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), LoginCode(TEXT(""))
{
}

void UMegascansAuthentication::LoginCallback(FWebJSFunction LoginJSCallback)
{
	LoginCompleteDelegate.BindLambda(LoginJSCallback);
}

void UMegascansAuthentication::InitiateAuthentication()
{
	//Initialize a web browser for authentication
	LoginMainWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("Login")))
		.ClientSize(FVector2D(450, 700))
		.SupportsMaximize(false)
		.SupportsMinimize(false)		
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SMegascansLoginWindow)
			]
		];
	FSlateApplication::Get().AddWindow(LoginMainWindow.ToSharedRef());
}

void SMegascansLoginWindow::HandleBrowserUrlChanged(const FText& Url)
{
	FString RedirectedUrl = Url.ToString();
	const TCHAR *ProdCodeUrl = TEXT("https://quixel.com/?code=");
	const TCHAR *StagingCodeUrl = TEXT("https://staging2.megascans.se/?code=");

	const TCHAR *CodeUrl = StagingCodeUrl;

	if (RedirectedUrl.StartsWith(CodeUrl))
	{
		FBridgeUIManager::MegascansAuthentication->LoginMainWindow->RequestDestroyWindow();
		FString LoginCode = RedirectedUrl.Replace(CodeUrl, TEXT(""));
		FBridgeUIManager::MegascansAuthentication->LoginCompleteDelegate.ExecuteIfBound(LoginCode);
		UE_LOG(LogTemp, Error, TEXT("Found code : %s"), *RedirectedUrl.Replace(CodeUrl, TEXT("")));
	}
}

void SMegascansLoginWindow::Construct(const FArguments& InArgs)
{
	FString Url = TEXT("https://www.quixel.com/login?return_to=https%3A%2F%2Fquixel.com%2Fmegascans%2Fhome");
	FString StagingUrl = TEXT("https://staging2.megascans.se/login?return_to=https%3A%2F%2Fstaging2.megascans.se%2Fmegascans%2Fhome");

	ChildSlot
		[
			SNew(SWebBrowser)
			.InitialURL(StagingUrl)
			.ShowControls(false)
			.OnUrlChanged(this, &SMegascansLoginWindow::HandleBrowserUrlChanged)
		];
}
