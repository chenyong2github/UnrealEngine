// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetEditorModeManager.h"
#include "Engine/Selection.h"
#include "PreviewScene.h"
#include "TypedElementRegistry.h"

//////////////////////////////////////////////////////////////////////////
// FAssetEditorModeManager

FAssetEditorModeManager::FAssetEditorModeManager()
	: PreviewScene(nullptr)
	, SelectedElements(UTypedElementRegistry::GetInstance()->CreateElementList())
{
	ActorSet = USelection::CreateActorSelection(nullptr, GetTransientPackage(), NAME_None, RF_Transactional);
	ActorSet->SetElementList(SelectedElements.Get());
	ActorSet->AddToRoot();

	ObjectSet = USelection::CreateObjectSelection(nullptr, GetTransientPackage(), NAME_None, RF_Transactional);
	ObjectSet->AddToRoot();

	ComponentSet = USelection::CreateComponentSelection(nullptr, GetTransientPackage(), NAME_None, RF_Transactional);
	ComponentSet->SetElementList(SelectedElements.Get());
	ComponentSet->AddToRoot();
}

FAssetEditorModeManager::~FAssetEditorModeManager()
{
	SetPreviewScene(nullptr);

	ActorSet->SetElementList(nullptr);
	ActorSet->RemoveFromRoot();
	ActorSet = nullptr;

	ObjectSet->RemoveFromRoot();
	ObjectSet = nullptr;

	ComponentSet->SetElementList(nullptr);
	ComponentSet->RemoveFromRoot();
	ComponentSet = nullptr;

	SelectedElements.Reset();
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
