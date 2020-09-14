// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetEditorModeManager.h"
#include "Engine/Selection.h"
#include "PreviewScene.h"
#include "TypedElementRegistry.h"

//////////////////////////////////////////////////////////////////////////
// FAssetEditorModeManager

FAssetEditorModeManager::FAssetEditorModeManager()
{
	SelectedElements = UTypedElementRegistry::GetInstance()->CreateElementList();
	SelectedElements->AddToRoot();

	ActorSet = USelection::CreateActorSelection(nullptr, GetTransientPackage(), NAME_None, RF_Transactional);
	ActorSet->SetElementList(SelectedElements);
	ActorSet->AddToRoot();

	ObjectSet = USelection::CreateObjectSelection(nullptr, GetTransientPackage(), NAME_None, RF_Transactional);
	ObjectSet->AddToRoot();

	ComponentSet = USelection::CreateComponentSelection(nullptr, GetTransientPackage(), NAME_None, RF_Transactional);
	ComponentSet->SetElementList(SelectedElements);
	ComponentSet->AddToRoot();
}

FAssetEditorModeManager::~FAssetEditorModeManager()
{
	SetPreviewScene(nullptr);

	// We may be destroyed after the UObject system has already shutdown, 
	// which would mean that these instances will be garbage
	if (UObjectInitialized())
	{
		ActorSet->SetElementList(nullptr);
		ActorSet->RemoveFromRoot();
		ActorSet = nullptr;

		ObjectSet->RemoveFromRoot();
		ObjectSet = nullptr;

		ComponentSet->SetElementList(nullptr);
		ComponentSet->RemoveFromRoot();
		ComponentSet = nullptr;

		SelectedElements->Empty();
		SelectedElements->RemoveFromRoot();
		SelectedElements = nullptr;
	}
}

USelection* FAssetEditorModeManager::GetSelectedActors() const
{
	return ActorSet;
}

USelection* FAssetEditorModeManager::GetSelectedObjects() const
{
	return ObjectSet;
}

USelection* FAssetEditorModeManager::GetSelectedComponents() const
{
	return ComponentSet;
}

UWorld* FAssetEditorModeManager::GetWorld() const
{
	return (PreviewScene != nullptr) ? PreviewScene->GetWorld() : GEditor->GetEditorWorldContext().World();
}

void FAssetEditorModeManager::SetPreviewScene(class FPreviewScene* NewPreviewScene)
{
	PreviewScene = NewPreviewScene;
}

FPreviewScene* FAssetEditorModeManager::GetPreviewScene() const
{
	return PreviewScene;
}
