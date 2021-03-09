// Copyright Epic Games, Inc. All Rights Reserved.
#include "UI/BrowserBinding.h"
#include "UI/BridgeUIManager.h"
#include "Widgets/SToolTip.h"
#include "Framework/Application/SlateApplication.h"
#include "WebBrowserModule.h"
#include "IWebBrowserSingleton.h"
#include "IWebBrowserCookieManager.h"
#include "SMSWindow.h"


UBrowserBinding::UBrowserBinding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UBrowserBinding::DialogSuccessCallback(FWebJSFunction DialogJSCallback)
{
	DialogSuccessDelegate.BindLambda(DialogJSCallback);
}

void UBrowserBinding::DialogFailCallback(FWebJSFunction DialogJSCallback)
{
	DialogFailDelegate.BindLambda(DialogJSCallback);
}

void UBrowserBinding::OnDroppedCallback(FWebJSFunction OnDroppedJSCallback)
{
	OnDroppedDelegate.BindLambda(OnDroppedJSCallback);
}

void UBrowserBinding::OnDropDiscardedCallback(FWebJSFunction OnDropDiscardedJSCallback)
{
	OnDropDiscardedDelegate.BindLambda(OnDropDiscardedJSCallback);
}

void UBrowserBinding::OnExitCallback(FWebJSFunction OnExitJSCallback)
{
	OnExitDelegate.BindLambda(OnExitJSCallback);
}

void UBrowserBinding::ShowDialog(FString Type, FString Url)
{

	// FString ProdUrl = TEXT("https://www.quixel.com/login?return_to=https%3A%2F%2Fquixel.com%2Fmegascans%2Fhome");
	// FString StagingUrl = TEXT("https://staging2.megascans.se/login?return_to=https%3A%2F%2Fstaging2.megascans.se%2Fmegascans%2Fhome");
	FString ProdUrl = TEXT("https://www.epicgames.com/id/login?client_id=b9101103b8814baa9bb4e79e5eb107d0&response_type=code");
	FString StagingUrl = TEXT("https://www.epicgames.com/id/login?client_id=3919f71c66d24a83836f659fd22d49f1&response_type=code");
	
	if (Type == "Login")
	{
		UBrowserBinding::ShowLoginDialog(StagingUrl);
	} 
	else
	{
		TSharedPtr<SWebBrowser> MyWebBrowser;
		TSharedRef<SWebBrowser> MyWebBrowserRef = SAssignNew(MyWebBrowser, SWebBrowser)
			.InitialURL(Url)
			.ShowControls(false);

		MyWebBrowser->BindUObject(TEXT("BrowserBinding"), FBridgeUIManager::BrowserBinding, true);

		//Initialize a dialog
		DialogMainWindow = SNew(SWindow)
			.Title(FText::FromString(Type))
			.ClientSize(FVector2D(450, 700))
			.SupportsMaximize(false)
			.SupportsMinimize(false)		
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					MyWebBrowserRef
				]
			];
		
		FSlateApplication::Get().AddWindow(DialogMainWindow.ToSharedRef());
	}
}

void UBrowserBinding::ShowLoginDialog(FString Url) 
{
	TSharedRef<SWebBrowser> MyWebBrowserRef = SAssignNew(FBridgeUIManager::BrowserBinding->DialogMainBrowser, SWebBrowser)
					.InitialURL(Url)
					.ShowControls(false)
					.OnBeforePopup_Lambda([](FString NextUrl, FString Target)
					{
						FBridgeUIManager::BrowserBinding->DialogMainBrowser->LoadURL(NextUrl);
						return true;
					})
					.OnUrlChanged_Lambda([](const FText& Url) 
								{
									FString RedirectedUrl = Url.ToString();
									const TCHAR *ProdCodeUrl = TEXT("https://quixel.com/?code=");
									const TCHAR *StagingCodeUrl = TEXT("https://staging2.megascans.se/?code=");

									const TCHAR *CodeUrl = StagingCodeUrl;

									if (RedirectedUrl.StartsWith(CodeUrl))
									{
										FBridgeUIManager::BrowserBinding->DialogMainWindow->RequestDestroyWindow();

										FString LoginCode = RedirectedUrl.Replace(CodeUrl, TEXT(""));
										FBridgeUIManager::BrowserBinding->DialogSuccessDelegate.ExecuteIfBound("Login", LoginCode);
										
										FBridgeUIManager::BrowserBinding->DialogMainBrowser.Reset();
									}
								}
					);
				
	//Initialize a dialog
	DialogMainWindow = SNew(SWindow)
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
				MyWebBrowserRef
			]
		];

	FSlateApplication::Get().AddWindow(DialogMainWindow.ToSharedRef());
}

FString UBrowserBinding::GetProjectPath()
{
	return FPaths::GetProjectFilePath();
}

void UBrowserBinding::SendSuccess(FString Value)
{
	
	FBridgeUIManager::BrowserBinding->DialogSuccessDelegate.ExecuteIfBound("Success", Value);
	DialogMainWindow->RequestDestroyWindow();
}

void UBrowserBinding::SendFailure(FString Message)
{
	
	FBridgeUIManager::BrowserBinding->DialogSuccessDelegate.ExecuteIfBound("Failure", Message);
	DialogMainWindow->RequestDestroyWindow();
}

void UBrowserBinding::OpenExternalUrl(FString Url)
{
	FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
}

void UBrowserBinding::DragStarted(TArray<FString> ImageUrls)
{
	TSharedPtr<SWebBrowser> PopupWebBrowser = SNew(SWebBrowser)
									.ShowControls(false);

	FString ImageUrl = ImageUrls[0];
	int32 Count = ImageUrls.Num();

	TSharedPtr<SWindow> DragDropWindow = SNew(SWindow)
		.ClientSize(FVector2D(120, 120))
		.SupportsTransparency(EWindowTransparency::PerWindow)
		.RenderOpacity(0.3)
		.CreateTitleBar(false)
		.IsTopmostWindow(true)
		.FocusWhenFirstShown(false)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			PopupWebBrowser.ToSharedRef()
		];

	FBridgeUIManager::Instance->DragDropWindows.Add(DragDropWindow);

	if (Count > 1)
	{
		PopupWebBrowser->LoadString(FString::Printf(TEXT("<!DOCTYPE html><html lang=\"en\"> <head> <meta charset=\"UTF-8\"/> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"/> <style>*{padding: 0px;}body{padding: 0px; margin: 0px;}#container{display: flex; position: relative; width: 120px; height: 120px; background: #202020; justify-content: center; align-items: center;}#full-image{max-width: 110px; max-height: 110px; display: block; font-size: 0;}#number-circle{position: absolute; border-radius: 50%; width: 18px; height: 18px; padding: 4px; background: #fff; color: #666; text-align: center; font: 16px Arial, sans-serif; box-shadow: 1px 1px 1px #888888; opacity: 0.5;}</style> </head> <body> <div id=\"container\"> <img id=\"full-image\" src=\"%s\"/> <div id=\"number-circle\">+%d</div></div></body></html>"), *ImageUrl, Count-1), TEXT(""));
	}
	else
	{
		PopupWebBrowser->LoadString(FString::Printf(TEXT("<!DOCTYPE html><html lang=\"en\"> <head> <meta charset=\"UTF-8\"/> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"/> <style>*{padding: 0px;}body{padding: 0px; margin: 0px;}#container{display: flex; position: relative; width: 120px; height: 120px; background: #202020; justify-content: center; align-items: center;}#full-image{max-width: 110px; max-height: 110px; display: block; font-size: 0;}#number-circle{position: absolute; border-radius: 50%; width: 18px; height: 18px; padding: 4px; background: #fff; color: #666; text-align: center; font: 16px Arial, sans-serif; box-shadow: 1px 1px 1px #888888; opacity: 0.5;}</style> </head> <body> <div id=\"container\"> <img id=\"full-image\" src=\"%s\"/></div></body></html>"), *ImageUrl), TEXT(""));
	}
	
	FSlateApplication::Get().AddWindow(DragDropWindow.ToSharedRef(), false);

	TSharedRef<FGenericApplicationMessageHandler> TargetHandler = FSlateApplication::Get().GetPlatformApplication().Get()->GetMessageHandler();
	UBrowserBinding::BridgeMessageHandler->SetTargetHandler(TargetHandler);
	FSlateApplication::Get().GetPlatformApplication()->SetMessageHandler(BridgeMessageHandler);
	// FBridgeUIManager::Instance->WebBrowserWidget.Get()->SetToolTipText(FText::FromString(TEXT("Dragging")));

	/*TSharedPtr<IToolTip> MyToolTip = SNew(SToolTip)
									.Text(FText::FromString(TEXT("Hmmm")));
	FSlateApplication::Get().SpawnToolTip(MyToolTip.ToSharedRef(), FVector2D(200, 300));*/

	//	FBridgeUIManager::Instance->DragDropWindow.Get()->Resize(FVector2D(100, 100));
}

void UBrowserBinding::Logout()
{
	// Delete Cookies
	IWebBrowserSingleton* WebBrowserSingleton = IWebBrowserModule::Get().GetSingleton();
	if (WebBrowserSingleton)
	{
		TSharedPtr<IWebBrowserCookieManager> CookieManager = WebBrowserSingleton->GetCookieManager();
		if (CookieManager.IsValid())
		{
			CookieManager->DeleteCookies();
		}
	}
}

void UBrowserBinding::OpenMegascansPluginSettings()
{
	MegascansSettingsWindow::OpenSettingsWindow();
}

