// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUtilityLibrary.h"
#include "Engine/Selection.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "ContentBrowserModule.h"
#include "Modules/ModuleManager.h"
#include "IContentBrowserSingleton.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorUtilitySubsystem.h"


#define LOCTEXT_NAMESPACE "BlutilityLevelEditorExtensions"

UEditorUtilityBlueprintAsyncActionBase::UEditorUtilityBlueprintAsyncActionBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UEditorUtilityBlueprintAsyncActionBase::RegisterWithGameInstance(UObject* WorldContextObject)
{
	UEditorUtilitySubsystem* EditorUtilitySubsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	EditorUtilitySubsystem->RegisterReferencedObject(this);
}

void UEditorUtilityBlueprintAsyncActionBase::SetReadyToDestroy()
{
	if (UEditorUtilitySubsystem* EditorUtilitySubsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>())
	{
		EditorUtilitySubsystem->UnregisterReferencedObject(this);
	}
}

UAsyncEditorDelay::UAsyncEditorDelay(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

UAsyncEditorDelay* UAsyncEditorDelay::AsyncEditorDelay(float Seconds)
{
	UAsyncEditorDelay* NewTask = NewObject<UAsyncEditorDelay>();
	NewTask->Start(Seconds);

	return NewTask;
}

#endif

void UAsyncEditorDelay::Start(float Seconds)
{
	FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsyncEditorDelay::HandleComplete), Seconds);
}

bool UAsyncEditorDelay::HandleComplete(float DeltaTime)
{
	Complete.Broadcast();
	SetReadyToDestroy();
	return false;
}

UEditorUtilityLibrary::UEditorUtilityLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

TArray<AActor*> UEditorUtilityLibrary::GetSelectionSet()
{
	TArray<AActor*> Result;
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			Result.Add(Actor);
		}
	}

	return Result;
}

void UEditorUtilityLibrary::GetSelectionBounds(FVector& Origin, FVector& BoxExtent, float& SphereRadius)
{
	bool bFirstItem = true;

	FBoxSphereBounds Extents;
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			if (bFirstItem)
			{
				Extents = Actor->GetRootComponent()->Bounds;
			}
			else
			{
				Extents = Extents + Actor->GetRootComponent()->Bounds;
			}

			bFirstItem = false;
		}
	}

	Origin = Extents.Origin;
	BoxExtent = Extents.BoxExtent;
	SphereRadius = Extents.SphereRadius;
}

TArray<UObject*> UEditorUtilityLibrary::GetSelectedAssets()
{
	//@TODO: Blocking load, no slow dialog
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	TArray<UObject*> Result;
	for (FAssetData& AssetData : SelectedAssets)
	{
		Result.Add(AssetData.GetAsset());
	}

	return Result;
}

TArray<UClass*> UEditorUtilityLibrary::GetSelectedBlueprintClasses()
{
	//@TODO: Blocking load, no slow dialog
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	TArray<UClass*> Result;
	for (FAssetData& AssetData : SelectedAssets)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset()))
		{
			Result.Add(Blueprint->GeneratedClass);
		}
	}

	return Result;
}

TArray<FAssetData> UEditorUtilityLibrary::GetSelectedAssetData()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	return SelectedAssets;
}

void UEditorUtilityLibrary::RenameAsset(UObject* Asset, const FString& NewName)
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	TArray<FAssetRenameData> AssetsAndNames;
	const FString PackagePath = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());
	new (AssetsAndNames) FAssetRenameData(Asset, PackagePath, NewName);

	AssetToolsModule.Get().RenameAssetsWithDialog(AssetsAndNames);
}

AActor* UEditorUtilityLibrary::GetActorReference(FString PathToActor)
{
#if WITH_EDITOR
	return Cast<AActor>(StaticFindObject(AActor::StaticClass(), GEditor->GetEditorWorldContext().World(), *PathToActor, false));
#else
	return nullptr;
#endif //WITH_EDITOR
}

#endif

#undef LOCTEXT_NAMESPACE
