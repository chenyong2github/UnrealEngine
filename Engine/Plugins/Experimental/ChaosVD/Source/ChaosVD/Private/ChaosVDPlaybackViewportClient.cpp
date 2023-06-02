// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDPlaybackViewportClient.h"

#include "ChaosVDScene.h"
#include "EngineUtils.h"

FChaosVDPlaybackViewportClient::FChaosVDPlaybackViewportClient() : FEditorViewportClient(nullptr), CVDWorld(nullptr)
{
}

FChaosVDPlaybackViewportClient::~FChaosVDPlaybackViewportClient()
{
	if (ObjectFocusedDelegateHandle.IsValid())
	{
		if (TSharedPtr<FChaosVDScene> ScenePtr = CVDScene.Pin())
		{
			ScenePtr->OnObjectFocused().Remove(ObjectFocusedDelegateHandle);
		}
	}
}

void FChaosVDPlaybackViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	if (HitProxy == nullptr)
	{
		return;
	}

	if (TSharedPtr<FChaosVDScene> ScenePtr = CVDScene.Pin())
	{
		if (HitProxy->IsA(HActor::StaticGetType()))
		{
			HActor* ActorHitProxy = static_cast<HActor*>(HitProxy);
			if (AActor* ClickedActor = ActorHitProxy->Actor)
			{
				ScenePtr->SetSelectedObject(ClickedActor);
			}
		}
	}
}

void FChaosVDPlaybackViewportClient::SetScene(TWeakPtr<FChaosVDScene> InScene)
{
	if (TSharedPtr<FChaosVDScene> ScenePtr = InScene.Pin())
	{
		CVDWorld = ScenePtr->GetUnderlyingWorld();
		CVDScene = InScene;

		ObjectFocusedDelegateHandle = ScenePtr->OnObjectFocused().AddRaw(this, &FChaosVDPlaybackViewportClient::HandleObjectFocused);
	}
}

void FChaosVDPlaybackViewportClient::HandleObjectFocused(UObject* FocusedObject)
{
	if (AActor* FocusedActor = Cast<AActor>(FocusedObject))
	{
		FocusViewportOnBox(FocusedActor->GetComponentsBoundingBox(false));
	}
}
