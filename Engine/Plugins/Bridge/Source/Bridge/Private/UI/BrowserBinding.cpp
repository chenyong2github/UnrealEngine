// Copyright Epic Games, Inc. All Rights Reserved.
#include "UI/BrowserBinding.h"
#include "UI/BridgeUIManager.h"
#include "Widgets/SToolTip.h"
#include "Framework/Application/SlateApplication.h"
#include "WebBrowserModule.h"
#include "IWebBrowserSingleton.h"
#include "IWebBrowserCookieManager.h"
#include "SMSWindow.h"
#include "TCPServer.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInstanceConstant.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"

/**
* Drag drop action
**/
class FAssetDragDropCustomOp : public FAssetDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FAssetDragDropCustomOp, FAssetDragDropOp)

		// FDragDropOperation interface
		virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
		virtual void OnDragged(const class FDragDropEvent& DragDropEvent) override;
		virtual void Construct() override;
		virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;
		// End of FDragDropOperation interface

		void SetCanDropHere(bool bCanDropHere)
		{
			MouseCursor = bCanDropHere ? EMouseCursor::TextEditBeam : EMouseCursor::SlashedCircle;
		}
		TArray<FString> ImageUrls;
		TArray<FString> IDs;
		static TSharedRef<FAssetDragDropCustomOp> New(TArray<FAssetData> AssetDataArray, UActorFactory* ActorFactory, TArray<FString> ImageUrls, TArray<FString> IDs);

	protected:
		FAssetDragDropCustomOp();
};

FAssetDragDropCustomOp::FAssetDragDropCustomOp()
{}

TSharedPtr<SWidget> FAssetDragDropCustomOp::GetDefaultDecorator() const
{
	TSharedPtr<SWebBrowser> PopupWebBrowser = SNew(SWebBrowser)
                                   .ShowControls(false);

    FString ImageUrl = ImageUrls[0];
    int32 Count = ImageUrls.Num();

    // FBridgeUIManager::Instance->DragDropWindow = SNew(SWindow)
    //    .ClientSize(FVector2D(120, 120))
    //    .InitialOpacity(0.5f)
    //    .SupportsTransparency(EWindowTransparency::PerWindow)
    //    .CreateTitleBar(false)
    //    .HasCloseButton(false)
    //    .IsTopmostWindow(true)
    //    .FocusWhenFirstShown(false)
    //    .SupportsMaximize(false)
    //    .SupportsMinimize(false)
    //    [
    //        PopupWebBrowser.ToSharedRef()
    //    ];

    if (Count > 1)
    {
       PopupWebBrowser->LoadString(FString::Printf(TEXT("<!DOCTYPE html><html lang=\"en\"> <head> <meta charset=\"UTF-8\"/> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"/> <style>*{padding: 0px;}body{padding: 0px; margin: 0px;}#container{display: flex; position: relative; width: 100%; height: 100%; min-width: 120px; min-height: 120px; background: #202020; justify-content: center; align-items: center;}#full-image{max-width: 110px; max-height: 110px; display: block; font-size: 0;}#number-circle{position: absolute; border-radius: 50%; width: 18px; height: 18px; padding: 4px; background: #fff; color: #666; text-align: center; font: 12px Arial, sans-serif; box-shadow: 1px 1px 1px #888888; opacity: 0.5;}</style> </head> <body> <div id=\"container\"> <img id=\"full-image\" src=\"%s\"/> <div id=\"number-circle\">+%d</div></div></body></html>"), *ImageUrl, Count-1), TEXT(""));
    }
    else
    {
       PopupWebBrowser->LoadString(FString::Printf(TEXT("<!DOCTYPE html><html lang=\"en\"> <head> <meta charset=\"UTF-8\"/> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"/> <style>*{padding: 0px;}body{padding: 0px; margin: 0px;}#container{display: flex; position: relative; width: 100%; height: 100%; min-width: 120px; min-height: 120px; background: #202020; justify-content: center; align-items: center;}#full-image{max-width: 110px; max-height: 110px; display: block; font-size: 0;}#number-circle{position: absolute; border-radius: 50%; width: 18px; height: 18px; padding: 4px; background: #fff; color: #666; text-align: center; font: 16px Arial, sans-serif; box-shadow: 1px 1px 1px #888888; opacity: 0.5;}</style> </head> <body> <div id=\"container\"> <img id=\"full-image\" src=\"%s\"/></div></body></html>"), *ImageUrl), TEXT(""));
    }

	return SNew(SBox)
	[
			SNew(SBorder)
			[
				SNew(SBox)
				.HeightOverride(120)
				.WidthOverride(120)
				[
					PopupWebBrowser.ToSharedRef()
				]
			]
	];
}

void FAssetDragDropCustomOp::OnDragged(const class FDragDropEvent& DragDropEvent)
{
	if (CursorDecoratorWindow.IsValid())
	{
		CursorDecoratorWindow->MoveWindowTo(DragDropEvent.GetScreenSpacePosition());
	}
}


void FAssetDragDropCustomOp::Construct()
{
	MouseCursor = EMouseCursor::GrabHandClosed;

	FDragDropOperation::Construct();
}

void FAssetDragDropCustomOp::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	// UE_LOG(LogTemp, Error, TEXT("On Drop Action"));
	// UE_LOG(LogTemp, Error, TEXT("bWasSwitchDragOp: %s"), FBridgeUIManager::BrowserBinding->bWasSwitchDragOperation ? TEXT("true") : TEXT("false"));

	FBridgeUIManager::BrowserBinding->bIsDragging = false;
	
	if (!bDropWasHandled)
	{
		if (FBridgeUIManager::BrowserBinding->bWasSwitchDragOperation) return;

		// UE_LOG(LogTemp, Error, TEXT("Drag discarded"));
		FBridgeUIManager::BrowserBinding->OnDropDiscardedDelegate.ExecuteIfBound(TEXT("dropped-discarded"));
		return;
	} 

	if (bDropWasHandled && FBridgeUIManager::BrowserBinding->DragOperationToAssetsMap[IDs[0]].IsEmpty())
	{
		// UE_LOG(LogTemp, Error, TEXT("Drag accepted and all assets were covered in this operation"));
	}
	else
	{
		// UE_LOG(LogTemp, Error, TEXT("Drag accepted but not all assets were covered in this operation"));
		USelection* SelectedActors = GEditor->GetSelectedActors();
		TArray<AStaticMeshActor*> Actors;
		for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
		{
			AStaticMeshActor* Actor = Cast<AStaticMeshActor>(*Iter);
			// Log Actor path name
			// UE_LOG(LogTemp, Error, TEXT("Actor Name: %s"), *Actor->GetActorLabel());
			if (Actor->GetActorLabel().Contains(TEXT("Sphere")))
			{
				Actors.Add(Actor);
			}
		}
		TArray<FString> AssetsInOperation = FBridgeUIManager::BrowserBinding->DragOperationToAssetsMap[IDs[0]];
		for (int32 i = 0; i < AssetsInOperation.Num(); i++)
		{
			if (!FBridgeUIManager::BrowserBinding->AssetToSphereMap.Contains(AssetsInOperation[i]))
			{
				if (i < Actors.Num())
				{
					FBridgeUIManager::BrowserBinding->AssetToSphereMap.Add(AssetsInOperation[i], Actors[i]);
				}
				else
				{
					break;
				}
			}
		}
	}
}

TSharedRef<FAssetDragDropCustomOp> FAssetDragDropCustomOp::New(TArray<FAssetData> AssetDataArray, UActorFactory* ActorFactory, TArray<FString> ImageUrls, TArray<FString> IDs)
{
	// Create the drag-drop op containing the key
	TSharedRef<FAssetDragDropCustomOp> Operation = MakeShareable(new FAssetDragDropCustomOp);
	Operation->Init(AssetDataArray, TArray<FString>(), ActorFactory);
	Operation->ImageUrls = ImageUrls;
	Operation->IDs = IDs;
	Operation->Construct();

	return Operation;
}

///////////////////////////////////////////////////////////////////////////////////

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

void UBrowserBinding::ShowLoginDialog(FString LoginUrl, FString ResponseCodeUrl) 
{
	// FString ProdUrl = TEXT("https://www.quixel.com/login?return_to=https%3A%2F%2Fquixel.com%2Fmegascans%2Fhome");
	// FString StagingUrl = TEXT("https://staging2.megascans.se/login?return_to=https%3A%2F%2Fstaging2.megascans.se%2Fmegascans%2Fhome");
	
	TSharedRef<SWebBrowser> MyWebBrowserRef = SAssignNew(FBridgeUIManager::BrowserBinding->DialogMainBrowser, SWebBrowser)
					.InitialURL(LoginUrl)
					.ShowControls(false)
					.OnBeforePopup_Lambda([](FString NextUrl, FString Target)
					{
						FBridgeUIManager::BrowserBinding->DialogMainBrowser->LoadURL(NextUrl);
						return true;
					})
					.OnUrlChanged_Lambda([ResponseCodeUrl](const FText& Url) 
								{
									FString RedirectedUrl = Url.ToString();
									if (RedirectedUrl.StartsWith(ResponseCodeUrl))
									{
										FBridgeUIManager::BrowserBinding->DialogMainWindow->RequestDestroyWindow();

										FString LoginCode = RedirectedUrl.Replace(*ResponseCodeUrl, TEXT(""));
										
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

void UBrowserBinding::SaveAuthToken(FString Value)
{
	FString PluginPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Bridge"));
	FString TokenPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginPath, TEXT("ThirdParty"), TEXT("megascans"), TEXT("token")));
	FFileHelper::SaveStringToFile(Value, *TokenPath);
}

FString UBrowserBinding::GetAuthToken()
{
	FString PluginPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Bridge"));
	FString TokenPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginPath, TEXT("ThirdParty"), TEXT("megascans"), TEXT("token")));
	FString Cookie;
	FFileHelper::LoadFileToString(Cookie, *TokenPath);

	return Cookie;
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

void UBrowserBinding::SwitchDragDropOp(TArray<FString> URLs, TSharedRef<FAssetDragDropOp> DragDropOperation)
{
	// UE_LOG(LogTemp, Error, TEXT("Switch Drag and Drop Op"));
	TArray<FAssetData> Assets = DragDropOperation->GetAssets();
	// Loop over assets and log their names
	for (const FAssetData& Asset : Assets)
	{
		// UE_LOG(LogTemp, Error, TEXT("Asset: %s"), *Asset.AssetName.ToString());
	}

	FBridgeUIManager::BrowserBinding->bWasSwitchDragOperation = true;
	FSlateApplication::Get().CancelDragDrop(); // calls onDrop method on the current drag drop operation
	FBridgeUIManager::BrowserBinding->bWasSwitchDragOperation = false;
	FBridgeUIManager::BrowserBinding->bIsDragging = true;

	const FVector2D CurrentCursorPosition = FSlateApplication::Get().GetCursorPos();
	const FVector2D LastCursorPosition = FSlateApplication::Get().GetLastCursorPos();

	TSet<FKey> PressedMouseButtons;
	PressedMouseButtons.Add(EKeys::LeftMouseButton);

	FModifierKeysState ModifierKeyState;

	FPointerEvent FakePointerEvent(
			FSlateApplication::Get().GetUserIndexForMouse(),
			FSlateApplicationBase::CursorPointerIndex,
			CurrentCursorPosition,
			LastCursorPosition,
			PressedMouseButtons,
			EKeys::Invalid,
			0,
			ModifierKeyState);

	// Tell slate to enter drag and drop mode.
	// Make a faux mouse event for slate, so we can initiate a drag and drop.
	FDragDropEvent DragDropEvent(FakePointerEvent, DragDropOperation);

	TSharedPtr<SWindow> OwnerWindow = FSlateApplication::Get().FindWidgetWindow(FBridgeUIManager::Instance->LocalBrowserDock.ToSharedRef());
	FSlateApplication::Get().ProcessDragEnterEvent(OwnerWindow.ToSharedRef(), DragDropEvent);
}

void UBrowserBinding::DragStarted(TArray<FString> ImageUrls, TArray<FString> IDs)
{
	FBridgeUIManager::BrowserBinding->bWasSwitchDragOperation = false;
	FBridgeUIManager::BrowserBinding->bIsDragging = true;

	if (FBridgeUIManager::BrowserBinding->DragOperationToAssetsMap.Contains(IDs[0]))
	{
		FBridgeUIManager::BrowserBinding->DragOperationToAssetsMap.Remove(IDs[0]);
	}

	FBridgeUIManager::BrowserBinding->DragOperationToAssetsMap.Add(IDs[0], IDs);
	FBridgeUIManager::BrowserBinding->InAssetData.Empty();

	// Iterate over ImageUrls
	for (int32 i = 0; i < IDs.Num(); i++)
	{
		FAssetData SphereData = FAssetData(LoadObject<UStaticMesh>(nullptr, *UActorFactoryBasicShape::BasicSphere.ToString()));
		FBridgeUIManager::BrowserBinding->InAssetData.Add(SphereData);
	}
	// UE_LOG(LogTemp, Error, TEXT("IDs we're getting from frontend: %d"), IDs.Num());

	UActorFactory* ActorFactory = GEditor->FindActorFactoryByClass(UActorFactoryBasicShape::StaticClass());
	TSharedRef<FAssetDragDropOp> DragDropOperation = FAssetDragDropCustomOp::New(FBridgeUIManager::BrowserBinding->InAssetData, ActorFactory, ImageUrls, IDs);
	SwitchDragDropOp(ImageUrls, DragDropOperation);

	// FBridgeUIManager::BrowserBinding->OnDroppedDelegate.ExecuteIfBound(TEXT("dropped"));

	FBridgeDragDrop::Instance->SetOnAddProgressiveStageData(FOnAddProgressiveStageDataCallback::CreateLambda([this, ImageUrls, IDs](FAssetData AssetData, FString AssetId, AStaticMeshActor* SpawnedActor) {
		// Find the key for this d&d operation
		FString Key = AssetId;

		if (Key.IsEmpty())
		{
			return;
		} 

		// Now we have the ID of the current drag and drop operation
		// Get all the assets in this op
		TArray<FString> AssetsInOperation = FBridgeUIManager::BrowserBinding->DragOperationToAssetsMap[IDs[0]];
		// Iterate over AssetsInOperation
		bool Found = false;
		for (int32 i = 0; i < AssetsInOperation.Num(); i++)
		{
			// If the asset is in the current drag and drop operation
			if (AssetsInOperation[i] == AssetId)
			{
				Found = true;
			}
		}

		// UE_LOG(LogTemp, Error, TEXT("%s Found: %d"), *AssetId, Found);
		if (!Found) return;

		// Filter out the asset we're currently processing
		TArray<FString> RemainingAssetsInOperation;
		// Iterate over AssetsInOperation
		for (int32 i = 0; i < AssetsInOperation.Num(); i++)
		{
			if (AssetsInOperation[i] != Key)
			{
				RemainingAssetsInOperation.Add(AssetsInOperation[i]);
			}
		}

		// Replace AssetsInOperation with RemainingAssetsInOperation
		FBridgeUIManager::BrowserBinding->DragOperationToAssetsMap[IDs[0]] = RemainingAssetsInOperation;

		// Log remaining assets in this operation
		// UE_LOG(LogTemp, Error, TEXT("Remaining Assets: %s"), *FString::Join(RemainingAssetsInOperation, TEXT(", ")));

		UWorld* CurrentWorld = GEngine->GetWorldContexts()[0].World();
		UActorFactory* ActorFactory = GEditor->FindActorFactoryByClass(UActorFactoryStaticMesh::StaticClass());

		// Remove the Sphere for this asset if it's a 3D asset
		for (int32 i = 0; i < FBridgeUIManager::BrowserBinding->InAssetData.Num(); i++)
		{
			if (FBridgeUIManager::BrowserBinding->InAssetData[i].ObjectPath.ToString().Contains("Sphere"))
			{
				FBridgeUIManager::BrowserBinding->InAssetData.RemoveAt(i);
				break;
			}
		}


		if (!FBridgeUIManager::BrowserBinding->bIsDragging)
		{
			// Return if AssetToSphereMap is empty
			if (FBridgeUIManager::BrowserBinding->AssetToSphereMap.Num() == 0)
			{
				return;
			}

			// This is where we'd want to replace the individual cubes with actual assets
			// Find the cube and replace it with the asset
			if (!FBridgeUIManager::BrowserBinding->AssetToSphereMap.Contains(Key)) return;

			FVector SpawnLocation;
			AStaticMeshActor* FoundSphereActor = FBridgeUIManager::BrowserBinding->AssetToSphereMap[Key];
			if (FoundSphereActor == nullptr)
			{
				// UE_LOG(LogTemp, Error, TEXT("FoundSphereActor is null"));
				return;
			}

			// Get the spawn location from cube
			SpawnLocation = FoundSphereActor->GetActorLocation();
			FBridgeUIManager::BrowserBinding->AssetToSphereMap.Remove(Key);
			
			UStaticMesh* SourceMesh = Cast<UStaticMesh>(AssetData.GetAsset());
			CurrentWorld->DestroyActor(FoundSphereActor);
			FViewport* ActiveViewport = GEditor->GetActiveViewport();
			FEditorViewportClient* EditorViewClient = (FEditorViewportClient*)ActiveViewport->GetClient();
			FTransform InitialTransform(SpawnLocation);	
			AStaticMeshActor* SMActor;
			if (SpawnedActor == nullptr)
			{
				// UE_LOG(LogTemp, Error, TEXT("SpawnedActor is null"));
				SMActor = Cast<AStaticMeshActor>(CurrentWorld->SpawnActor(AStaticMeshActor::StaticClass(), &InitialTransform));
				SMActor->GetStaticMeshComponent()->SetStaticMesh(SourceMesh);
				SMActor->SetActorLabel(AssetData.AssetName.ToString());
			}
			else
			{
				SMActor = SpawnedActor;
				SMActor->SetActorTransform(InitialTransform);
				SMActor->SetActorLabel(AssetId);
			}

			GEditor->EditorUpdateComponents();
			CurrentWorld->UpdateWorldComponents(true, false);
			SMActor->RerunConstructionScripts();
			GEditor->SelectActor(SMActor, true, false);
			return;
		}

		// We continue the d&d operation
		if (SpawnedActor == nullptr)
		{
			// UE_LOG(LogTemp, Error, TEXT("SpawnedActor is not null"));
			FBridgeUIManager::BrowserBinding->InAssetData.Add(AssetData);
			TSharedRef<FAssetDragDropOp> DragDropOperation = FAssetDragDropCustomOp::New(FBridgeUIManager::BrowserBinding->InAssetData, ActorFactory, ImageUrls, IDs);
			SwitchDragDropOp(ImageUrls, DragDropOperation);
		}
		else
		{
			if (!FBridgeUIManager::BrowserBinding->AssetToSphereMap.Contains(Key)) return;

			AStaticMeshActor* FoundSphereActor = FBridgeUIManager::BrowserBinding->AssetToSphereMap[Key];
			if (FoundSphereActor == nullptr)
			{
				// UE_LOG(LogTemp, Error, TEXT("FoundSphereActor is null"));
				return;
			}

			FBridgeUIManager::BrowserBinding->AssetToSphereMap.Remove(Key);
			CurrentWorld->DestroyActor(FoundSphereActor);
		}
	}));
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
	// Write file
	FString PluginPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Bridge"));
	FString TokenPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginPath, TEXT("ThirdParty"), TEXT("megascans"), TEXT("token")));
	FFileHelper::SaveStringToFile(TEXT(""), *TokenPath);
}

void UBrowserBinding::StartNodeProcess()
{
	// Start node process
	FNodeProcessManager::Get()->StartNodeProcess();
}

void UBrowserBinding::RestartNodeProcess()
{
	// Restart node process
	FNodeProcessManager::Get()->RestartNodeProcess();
}

void UBrowserBinding::OpenMegascansPluginSettings()
{
	MegascansSettingsWindow::OpenSettingsWindow();
}

void UBrowserBinding::ExportDataToMSPlugin(FString Data)
{
	FTCPServer::ImportQueue.Enqueue(Data);
}
