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
#include "WaterZoneActor.h"
#include "WaterMeshComponent.h"
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
#include "WaterZoneActorFactory.h"
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
#include "Engine/AssetManager.h"
#include "WaterRuntimeSettings.h"

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
		GEditor->ActorFactories.Add(NewObject<UWaterZoneActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBodyIslandActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBodyRiverActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBodyLakeActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBodyOceanActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBodyCustomActorFactory>());
		GEditor->ActorFactories.Add(NewObject<UWaterBrushManagerFactory>());
	}

	UThumbnailManager::Get().RegisterCustomRenderer(UWaterBodyBrushCacheContainer::StaticClass(), UWaterBodyBrushCacheContainerThumbnailRenderer::StaticClass());

	OnLoadCollisionProfileConfigHandle = UCollisionProfile::Get()->OnLoadProfileConfig.AddLambda([this](UCollisionProfile* CollisionProfile)
		{
			check(UCollisionProfile::Get() == CollisionProfile);
			CheckForWaterCollisionProfile();
		});

	CheckForWaterCollisionProfile();
}

void FWaterEditorModule::ShutdownModule()
{
	if (UObjectInitialized())
	{
		UCollisionProfile::Get()->OnLoadProfileConfig.Remove(OnLoadCollisionProfileConfigHandle);

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
			TArray<ALandscape*> FoundLandscapes;

			// Search for all overlapping landscapes and add a water brush to them.
			// If we cannot find a suitable landscape via this method, default to using the first landscape in the world.
			
			const bool bNonColliding = true;
			const bool bIncludeChildActors = false;
			const FBox ActorBounds = Actor->GetComponentsBoundingBox(bNonColliding, bIncludeChildActors);

			for (ALandscape* Landscape : TActorRange<ALandscape>(ActorWorld))
			{
				const FBox LandscapeBounds = Landscape->GetComponentsBoundingBox(bNonColliding, bIncludeChildActors);
				if (LandscapeBounds.Intersect(ActorBounds))
				{
					FoundLandscapes.Add(Landscape);
				}
			}

			if (FoundLandscapes.Num() == 0)
			{
				UE_LOG(LogWaterEditor, Warning, TEXT("Could not find a suitable landscape to which to assign the water brush! Defaulting to the first landscape."));
				const TActorIterator<ALandscape> It(ActorWorld);
				if (ALandscape* Landscape = It ? *It : nullptr)
				{
					FoundLandscapes.Add(Landscape);
				}
			}

			// Spawn a Water brush for every landscape this actor overlaps with.
			FBox Bounds;
			for (ALandscape* FoundLandscape : FoundLandscapes)
			{
				if (!IsValid(FoundLandscape))
				{
					continue;
				}

				const FBox LandscapeBounds = FoundLandscape->GetComponentsBoundingBox(bNonColliding, bIncludeChildActors);
				Bounds += LandscapeBounds;

				bool bHasWaterManager = false;
				FoundLandscape->ForEachLayer([&bHasWaterManager](FLandscapeLayer& Layer)
				{
					for (const FLandscapeLayerBrush& Brush : Layer.Brushes)
					{
						bHasWaterManager |= Cast<AWaterLandscapeBrush>(Brush.GetBrush()) != nullptr;
					}
				});

				if (!bHasWaterManager)
				{
					const UWaterEditorSettings* WaterEditorSettings = GetDefault<UWaterEditorSettings>();
					check(WaterEditorSettings != nullptr);
					TSubclassOf<AWaterLandscapeBrush> WaterBrushClass = WaterEditorSettings->GetWaterManagerClass();
					if (UClass* WaterBrushClassPtr = WaterBrushClass.Get())
					{
						UActorFactory* WaterBrushActorFactory = GEditor->FindActorFactoryForActorClass(WaterBrushClassPtr);

						if (FoundLandscape)
						{
							FString BrushActorString = FString::Format(TEXT("{0}_{1}"), { FoundLandscape->GetActorLabel(), WaterBrushClassPtr->GetName() } );
							FName BrushActorName = MakeUniqueObjectName(FoundLandscape->GetOuter(), WaterBrushClassPtr, FName(BrushActorString));
							FActorSpawnParameters SpawnParams;
							SpawnParams.Name = BrushActorName;
							SpawnParams.bAllowDuringConstructionScript = true; // This can be called by construction script if the actor being added to the world is part of a blueprint, for example : 
							AWaterLandscapeBrush* NewBrush = (WaterBrushActorFactory != nullptr) 
								? Cast<AWaterLandscapeBrush>(WaterBrushActorFactory->CreateActor(ActorWorld, FoundLandscape->GetLevel(), FTransform(LandscapeBounds.GetCenter()), SpawnParams))
								: ActorWorld->SpawnActor<AWaterLandscapeBrush>(WaterBrushClassPtr, SpawnParams);
							if (NewBrush)
							{
								bHasWaterManager = true;
								NewBrush->SetActorLabel(BrushActorString);
								NewBrush->SetTargetLandscape(FoundLandscape);
							}
						}
					}
					else
					{
						UE_LOG(LogWaterEditor, Warning, TEXT("Could not find Water Manager class %s to spawn"), *WaterEditorSettings->GetWaterManagerClassPath().GetAssetPathString());
					}
				}
			}

			const bool bHasMeshActor = !!TActorIterator<AWaterZone>(ActorWorld);

			if (!bHasMeshActor)
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.OverrideLevel = ActorWorld->PersistentLevel;
				SpawnParams.bAllowDuringConstructionScript = true; // This can be called by construction script if the actor being added to the world is part of a blueprint, for example : 
				AWaterZone* WaterZoneActor = ActorWorld->SpawnActor<AWaterZone>(AWaterZone::StaticClass(), SpawnParams);

				// Set a more sensible default location and extent so that the zone fully encapsulates the landscape if one exists.
				if (FoundLandscapes.Num() > 0)
				{
					WaterZoneActor->SetActorLocation(Bounds.GetCenter());
					// FBox::GetExtent returns the radius, SetZoneExtent expects diameter.
					WaterZoneActor->SetZoneExtent(2 * FVector2D(Bounds.GetExtent()));
				}

				// Set the defaults here because the actor factory isn't triggered on manual SpawnActor.
				const FWaterZoneActorDefaults& WaterMeshActorDefaults = GetDefault<UWaterEditorSettings>()->WaterZoneActorDefaults;
				WaterZoneActor->GetWaterMeshComponent()->FarDistanceMaterial = WaterMeshActorDefaults.GetFarDistanceMaterial();
				WaterZoneActor->GetWaterMeshComponent()->FarDistanceMeshExtent = WaterMeshActorDefaults.FarDistanceMeshExtent;
			}
		}
	}
}

void FWaterEditorModule::CheckForWaterCollisionProfile()
{
	// Make sure WaterCollisionProfileName is added to Engine's collision profiles
	const FName WaterCollisionProfileName = GetDefault<UWaterRuntimeSettings>()->GetDefaultWaterCollisionProfileName();
	FCollisionResponseTemplate WaterBodyCollisionProfile;
	if (!UCollisionProfile::Get()->GetProfileTemplate(WaterCollisionProfileName, WaterBodyCollisionProfile))
	{
		FMessageLog("LoadErrors").Error()
			->AddToken(FTextToken::Create(LOCTEXT("MissingWaterCollisionProfile", "Collision Profile settings do not include an entry for the Water Body Collision profile, which is required for water collision to function.")))
			->AddToken(FActionToken::Create(LOCTEXT("AddWaterCollisionProfile", "Add entry to DefaultEngine.ini?"), FText(),
				FOnActionTokenExecuted::CreateRaw(this, &FWaterEditorModule::AddWaterCollisionProfile), true));
	}
}

void FWaterEditorModule::AddWaterCollisionProfile()
{
	// Make sure WaterCollisionProfileName is added to Engine's collision profiles
	const FName WaterCollisionProfileName = GetDefault<UWaterRuntimeSettings>()->GetDefaultWaterCollisionProfileName();
	FCollisionResponseTemplate WaterBodyCollisionProfile;
	if (!UCollisionProfile::Get()->GetProfileTemplate(WaterCollisionProfileName, WaterBodyCollisionProfile))
	{
		WaterBodyCollisionProfile.Name = WaterCollisionProfileName;
		WaterBodyCollisionProfile.CollisionEnabled = ECollisionEnabled::QueryOnly;
		WaterBodyCollisionProfile.ObjectType = ECollisionChannel::ECC_WorldStatic;
		WaterBodyCollisionProfile.bCanModify = false;
		WaterBodyCollisionProfile.ResponseToChannels = FCollisionResponseContainer::GetDefaultResponseContainer();
		WaterBodyCollisionProfile.ResponseToChannels.Camera = ECR_Ignore;
		WaterBodyCollisionProfile.ResponseToChannels.Visibility = ECR_Ignore;
		WaterBodyCollisionProfile.ResponseToChannels.WorldDynamic = ECR_Overlap;
		WaterBodyCollisionProfile.ResponseToChannels.Pawn = ECR_Overlap;
		WaterBodyCollisionProfile.ResponseToChannels.PhysicsBody = ECR_Overlap;
		WaterBodyCollisionProfile.ResponseToChannels.Destructible = ECR_Overlap;
		WaterBodyCollisionProfile.ResponseToChannels.Vehicle = ECR_Overlap;
#if WITH_EDITORONLY_DATA
		WaterBodyCollisionProfile.HelpMessage = TEXT("Default Water Collision Profile (Created by Water Plugin)");
#endif
		FCollisionProfilePrivateAccessor::AddProfileTemplate(WaterBodyCollisionProfile);
	}
}

IMPLEMENT_MODULE(FWaterEditorModule, WaterEditor);

#undef LOCTEXT_NAMESPACE