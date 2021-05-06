// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterEditorModule.h"
#include "Modules/ModuleManager.h"
#include "EditorModeRegistry.h"
#include "WaterUIStyle.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "WaterBodyActor.h"
#include "DetailCategoryBuilder.h"
#include "WaterLandscapeBrush.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "WaterMeshActor.h"
#include "Editor.h"
#include "ISettingsModule.h"
#include "WaterEditorSettings.h"
#include "HAL/IConsoleManager.h"
#include "Editor/UnrealEdEngine.h"
#include "WaterSplineComponentVisualizer.h"
#include "WaterSplineComponent.h"
#include "UnrealEdGlobals.h"
#include "LevelEditorViewport.h"
#include "WaterBodyActorFactory.h"
#include "WaterBodyIslandActorFactory.h"
#include "WaterBodyActorDetailCustomization.h"
#include "WaterBrushManagerFactory.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "Toolkits/IToolkit.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions_WaterWaves.h"
#include "WaterBrushCacheContainer.h"
#include "WaterBodyBrushCacheContainerThumbnailRenderer.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "WaterWavesEditorToolkit.h"

#define LOCTEXT_NAMESPACE "WaterEditor"

DEFINE_LOG_CATEGORY(LogWaterEditor);

EAssetTypeCategories::Type FWaterEditorModule::WaterAssetCategory;

void FWaterEditorModule::StartupModule()
{
	FWaterUIStyle::Initialize();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyModule.RegisterCustomClassLayout(TEXT("WaterBody"), FOnGetDetailCustomizationInstance::CreateStatic(&FWaterBodyActorDetailCustomization::MakeInstance));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	WaterAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("Water")), LOCTEXT("WaterAssetCategory", "Water"));

	// Helper lambda for registering asset type actions for automatic cleanup on shutdown
	auto RegisterAssetTypeAction = [&](TSharedRef<IAssetTypeActions> Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		CreatedAssetTypeActions.Add(Action);
	};

	// Register type actions
	RegisterAssetTypeAction(MakeShareable(new FAssetTypeActions_WaterWaves));

	GEngine->OnLevelActorAdded().AddRaw(this, &FWaterEditorModule::OnLevelActorAddedToWorld);

	RegisterComponentVisualizer(UWaterSplineComponent::StaticClass()->GetFName(), MakeShareable(new FWaterSplineComponentVisualizer));

	if (GEditor)
	{
		GEditor->ActorFactories.Add(NewObject<UWaterBodyIslandActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBodyRiverActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBodyLakeActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBodyOceanActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBodyCustomActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBrushManagerFactory>());
	}

	UThumbnailManager::Get().RegisterCustomRenderer(UWaterBodyBrushCacheContainer::StaticClass(), UWaterBodyBrushCacheContainerThumbnailRenderer::StaticClass());
}

void FWaterEditorModule::ShutdownModule()
{
	if (UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UWaterBodyBrushCacheContainer::StaticClass());
	}

	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyModule.UnregisterCustomClassLayout(TEXT("WaterBody"));
	}

	if (GEngine)
	{
		GEngine->OnLevelActorAdded().RemoveAll(this);

		// Iterate over all class names we registered for
		for (FName ClassName : RegisteredComponentClassNames)
		{
			GUnrealEd->UnregisterComponentVisualizer(ClassName);
		}
	}

	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (auto CreatedAssetTypeAction : CreatedAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(CreatedAssetTypeAction.ToSharedRef());
		}
	}
	CreatedAssetTypeActions.Empty();

	FEditorDelegates::OnMapOpened.RemoveAll(this);

	FWaterUIStyle::Shutdown();
}

TSharedRef<FWaterWavesEditorToolkit> FWaterEditorModule::CreateWaterWaveAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* WavesAsset)
{
	TSharedRef<FWaterWavesEditorToolkit> NewWaterWaveAssetEditor(new FWaterWavesEditorToolkit());
	NewWaterWaveAssetEditor->InitWaterWavesEditor(Mode, InitToolkitHost, WavesAsset);
	return NewWaterWaveAssetEditor;
}

void FWaterEditorModule::RegisterComponentVisualizer(FName ComponentClassName, TSharedPtr<FComponentVisualizer> Visualizer)
{
	if (GUnrealEd != NULL)
	{
		GUnrealEd->RegisterComponentVisualizer(ComponentClassName, Visualizer);
	}

	RegisteredComponentClassNames.Add(ComponentClassName);

	if (Visualizer.IsValid())
	{
		Visualizer->OnRegister();
	}
}

void FWaterEditorModule::OnLevelActorAddedToWorld(AActor* Actor)
{
	IWaterBrushActorInterface* WaterBrushActor = Cast<IWaterBrushActorInterface>(Actor);
	if (WaterBrushActor && !Actor->bIsEditorPreviewActor && !Actor->HasAnyFlags(RF_Transient) && WaterBrushActor->AffectsLandscape())
	{
		UWorld* ActorWorld = Actor->GetWorld();
		if (ActorWorld && ActorWorld->IsEditorWorld())
		{
			bool bHasWaterManager = !!TActorIterator<AWaterLandscapeBrush>(ActorWorld);
			if (!bHasWaterManager)
			{
				const UWaterEditorSettings* WaterEditorSettings = GetDefault<UWaterEditorSettings>();
				check(WaterEditorSettings != nullptr);
				TSubclassOf<AWaterLandscapeBrush> WaterBrushClass = WaterEditorSettings->GetWaterManagerClass();
				if (UClass* WaterBrushClassPtr = WaterBrushClass.Get())
				{
					UActorFactory* WaterBrushActorFactory = GEditor->FindActorFactoryForActorClass(WaterBrushClassPtr);

					// If the water manager doesn't exist, spawn it now in the same level as the landscape
					for (ALandscape* Landscape : TActorRange<ALandscape>(ActorWorld))
					{
						FString BrushActorString = FString::Format(TEXT("{0}_{1}"), { Landscape->GetActorLabel(), WaterBrushClassPtr->GetName() } );
						FName BrushActorName = MakeUniqueObjectName(Landscape->GetOuter(), WaterBrushClassPtr, FName(BrushActorString));
						FActorSpawnParameters SpawnParams;
						SpawnParams.Name = BrushActorName;
						SpawnParams.bAllowDuringConstructionScript = true; // This can be called by construction script if the actor being added to the world is part of a blueprint, for example : 
						AWaterLandscapeBrush* NewBrush = (WaterBrushActorFactory != nullptr) 
							? Cast<AWaterLandscapeBrush>(WaterBrushActorFactory->CreateActor(ActorWorld, Landscape->GetLevel(), FTransform::Identity, SpawnParams))
							: ActorWorld->SpawnActor<AWaterLandscapeBrush>(WaterBrushClassPtr, SpawnParams);
						if (NewBrush)
						{
							bHasWaterManager = true;
							NewBrush->SetActorLabel(BrushActorString);
							NewBrush->SetTargetLandscape(Landscape);
						}
						break;
					}
				}
				else
				{
					UE_LOG(LogWaterEditor, Log, TEXT("Could not find Water Manager class %s to spawn"), *WaterEditorSettings->GetWaterManagerClassPath().GetAssetPathString());
				}
			}

			const bool bHasMeshActor = !!TActorIterator<AWaterMeshActor>(ActorWorld);

			if (!bHasMeshActor && bHasWaterManager)
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.OverrideLevel = ActorWorld->PersistentLevel;
				SpawnParams.bAllowDuringConstructionScript = true; // This can be called by construction script if the actor being added to the world is part of a blueprint, for example : 
				ActorWorld->SpawnActor<AWaterMeshActor>(AWaterMeshActor::StaticClass(), SpawnParams);
			}
		}
	}
}

IMPLEMENT_MODULE(FWaterEditorModule, WaterEditor);

#undef LOCTEXT_NAMESPACE