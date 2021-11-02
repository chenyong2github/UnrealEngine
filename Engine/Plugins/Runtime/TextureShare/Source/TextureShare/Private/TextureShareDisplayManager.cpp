// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareDisplayManager.h"

#include "TextureShareDisplayExtension.h"
#include "TextureShareModule.h"
#include "TextureShareLog.h"

#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"

#include "Slate/SceneViewport.h"

#if WITH_EDITOR
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "EditorSupportDelegates.h"
#include "Editor/EditorEngine.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "SceneView.h"
#endif

static const FName RendererModuleName(TEXT("Renderer"));

namespace
{
	static const FName LevelEditorName(TEXT("LevelEditor"));

	void FindSceneViewport(TWeakPtr<FSceneViewport>& OutSceneViewport)
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Editor)
				{
					if (FModuleManager::Get().IsModuleLoaded(LevelEditorName))
					{
						FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorName);
						TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveViewport();
						if (ActiveLevelViewport.IsValid())
						{
							OutSceneViewport = ActiveLevelViewport->GetSharedActiveViewport();
						}
					}
				}
				else if (Context.WorldType == EWorldType::PIE)
				{
					FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
					if (SlatePlayInEditorSession)
					{
						if (SlatePlayInEditorSession->DestinationSlateViewport.IsValid())
						{
							TSharedPtr<IAssetViewport> DestinationLevelViewport = SlatePlayInEditorSession->DestinationSlateViewport.Pin();
							OutSceneViewport = DestinationLevelViewport->GetSharedActiveViewport();
						}
						else if (SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
						{
							OutSceneViewport = SlatePlayInEditorSession->SlatePlayInEditorWindowViewport;
						}
					}
				}
			}
		}
		else
#endif
		{
			UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
			OutSceneViewport = GameEngine->SceneViewport;
		}
	}
}

FTextureShareDisplayManager::FTextureShareDisplayManager(FTextureShareModule& InTextureShareModule)
	: TextureShareModule(InTextureShareModule)
{ }

FTextureShareDisplayManager::~FTextureShareDisplayManager()
{
	EndSceneSharing();
}

TSharedPtr<FTextureShareDisplayExtension, ESPMode::ThreadSafe> FTextureShareDisplayManager::FindOrAddDisplayConfiguration(FViewport* InViewport)
{
	const TSharedPtr<FTextureShareDisplayExtension, ESPMode::ThreadSafe>* Extension = DisplayExtensions.FindByPredicate([InViewport](const TSharedPtr<FTextureShareDisplayExtension, ESPMode::ThreadSafe>& Other) { return  Other.IsValid() && InViewport == Other->GetAssociatedViewport(); });
	if (Extension)
	{
		return *Extension;
	}
	
	// Extension not found, create it and return its config
	TSharedPtr<FTextureShareDisplayExtension, ESPMode::ThreadSafe> DisplayExtension = FSceneViewExtensions::NewExtension<FTextureShareDisplayExtension>(*this, InViewport);
	DisplayExtensions.Add(DisplayExtension);
	return DisplayExtension;
}

bool FTextureShareDisplayManager::RemoveDisplayConfiguration(const FViewport* InViewport)
{
	const int32 Index = DisplayExtensions.IndexOfByPredicate([InViewport](const TSharedPtr<FTextureShareDisplayExtension, ESPMode::ThreadSafe>& Other) { return  Other.IsValid() && InViewport == Other->GetAssociatedViewport(); });
	if (Index != INDEX_NONE)
	{
		DisplayExtensions.RemoveAtSwap(Index);
		return true;
	}

	return false;
}

bool FTextureShareDisplayManager::IsTrackingViewport(const FViewport* InViewport) const
{
	return DisplayExtensions.ContainsByPredicate([InViewport](const TSharedPtr<FTextureShareDisplayExtension, ESPMode::ThreadSafe>& Other) { return  Other.IsValid() && InViewport == Other->GetAssociatedViewport(); });
}

void FTextureShareDisplayManager::OnBeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	TextureShareModule.OnBeginRenderViewFamily(InViewFamily);
}

void FTextureShareDisplayManager::OnPreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	// Save current viewfamily (Call from FTextureShareDisplayExtension::PreRenderViewFamily_RenderThread)
	CurrentSceneViewFamily = &InViewFamily;
}

void FTextureShareDisplayManager::OnResolvedSceneColor_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneRenderTargets& SceneContext)
{
	// Forward renderer cb with saved viewfamily (Call from RendererModule->GetResolvedSceneColorCallbacks)
	if (IsSceneSharingValid() && CurrentSceneViewFamily)
	{
		TextureShareModule.OnResolvedSceneColor_RenderThread(RHICmdList, SceneContext, *CurrentSceneViewFamily);
	}
}

void FTextureShareDisplayManager::OnPostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	// Forward resolved backbuffer cb (Call from FTextureShareDisplayExtension::PostRenderViewFamily_RenderThread)
	CurrentSceneViewFamily = nullptr;
	TextureShareModule.OnPostRenderViewFamily_RenderThread(RHICmdList, InViewFamily);
}

bool FTextureShareDisplayManager::BeginSceneSharing()
{
	// Register renderer callback
	if (!bIsRenderedCallbackAssigned)
	{
		IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
		if (RendererModule)
		{
			bIsRenderedCallbackAssigned = true;
			RendererModule->GetResolvedSceneColorCallbacks().AddRaw(this, &FTextureShareDisplayManager::OnResolvedSceneColor_RenderThread);
		}

		if (!bIsRenderedCallbackAssigned)
		{
			UE_LOG(LogTextureShare, Error, TEXT("Failed Register Renderer Callback"));
		}
		else
		{
			UE_LOG(LogTextureShare, Log, TEXT("Successs initialized renderer callback"));
		}
	}

	// Register DisplayExtension for current viewport
	TWeakPtr<FSceneViewport> SceneViewport;
	FindSceneViewport(SceneViewport);
	if (SceneViewport.IsValid())
	{
		FindOrAddDisplayConfiguration(SceneViewport.Pin()->GetViewport());
	}

	return IsSceneSharingValid();
}

void FTextureShareDisplayManager::EndSceneSharing()
{
	DisplayExtensions.RemoveAll([&](TSharedPtr<FTextureShareDisplayExtension, ESPMode::ThreadSafe>& DisplayExtension){ return true; });
}

