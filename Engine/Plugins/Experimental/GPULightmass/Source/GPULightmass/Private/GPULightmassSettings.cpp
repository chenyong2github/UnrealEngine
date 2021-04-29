// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPULightmassSettings.h"
#include "EngineUtils.h"
#include "GPULightmassModule.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Misc/ScopedSlowTask.h"
#include "LandscapeComponent.h"
#include "GPULightmass.h"
#include "Editor.h"
#include "LevelEditorViewport.h"

#define LOCTEXT_NAMESPACE "StaticLightingSystem"

int32 GGPULightmassShowProgressBars = 1;
static FAutoConsoleVariableRef CVarGPULightmassShowProgressBars(
	TEXT("r.GPULightmass.ShowProgressBars"),
	GGPULightmassShowProgressBars,
	TEXT("\n"),
	ECVF_Default
);

int32 GGPULightmassOnlyBakeWhatYouSee = 0;
static FAutoConsoleVariableRef CVarGPULightmassOnlyBakeWhatYouSee(
	TEXT("r.GPULightmass.OnlyBakeWhatYouSee"),
	GGPULightmassOnlyBakeWhatYouSee,
	TEXT("\n"),
	ECVF_Default
);

int32 GGPULightmassSamplesPerTexel = 512;
static FAutoConsoleVariableRef CVarGPULightmassSamplesPerTexel(
	TEXT("r.GPULightmass.SamplesPerTexel"),
	GGPULightmassSamplesPerTexel,
	TEXT("\n"),
	ECVF_Default
);

int32 GGPULightmassShadowSamplesPerTexel = 512; // 512 samples to reach good image plane stratification. Shadow samples are 100x faster than path samples
static FAutoConsoleVariableRef CVarGPULightmassShadowSamplesPerTexel(
	TEXT("r.GPULightmass.ShadowSamplesPerTexel"),
	GGPULightmassShadowSamplesPerTexel,
	TEXT("\n"),
	ECVF_Default
);

int32 GGPULightmassUseIrradianceCaching = 0;
static FAutoConsoleVariableRef CVarGPULightmassUseIrradianceCaching(
	TEXT("r.GPULightmass.IrradianceCaching"),
	GGPULightmassUseIrradianceCaching,
	TEXT("\n"),
	ECVF_Default
);

int32 GGPULightmassIrradianceCachingQuality = 128;
static FAutoConsoleVariableRef CVarGPULightmassIrradianceCachingQuality(
	TEXT("r.GPULightmass.IrradianceCaching.Quality"),
	GGPULightmassIrradianceCachingQuality,
	TEXT("\n"),
	ECVF_Default
);

float GGPULightmassIrradianceCachingSpacing = 32.0f;
static FAutoConsoleVariableRef CVarGPULightmassIrradianceCachingSpacing(
	TEXT("r.GPULightmass.IrradianceCaching.Spacing"),
	GGPULightmassIrradianceCachingSpacing,
	TEXT("\n"),
	ECVF_Default
);

int32 GGPULightmassVisualizeIrradianceCache = 0;
static FAutoConsoleVariableRef CVarGPULightmassVisualizeIrradianceCache(
	TEXT("r.GPULightmass.IrradianceCaching.Visualize"),
	GGPULightmassVisualizeIrradianceCache,
	TEXT("\n"),
	ECVF_Default
);

int32 GGPULightmassUseFirstBounceRayGuiding = 0;
static FAutoConsoleVariableRef CVarGPULightmassUseFirstBounceRayGuiding(
	TEXT("r.GPULightmass.FirstBounceRayGuiding"),
	GGPULightmassUseFirstBounceRayGuiding,
	TEXT("\n"),
	ECVF_Default
);

int32 GGPULightmassFirstBounceRayGuidingTrialSamples = 128;
static FAutoConsoleVariableRef CVarGPULightmassFirstBounceRayGuidingTrialSamples(
	TEXT("r.GPULightmass.FirstBounceRayGuiding.TrialSamples"),
	GGPULightmassFirstBounceRayGuidingTrialSamples,
	TEXT("\n"),
	ECVF_Default
);

int32 GGPULightmassGPUTilePoolSize = 40;
static FAutoConsoleVariableRef CVarGPULightmassGPUTilePoolSize(
	TEXT("r.GPULightmass.System.GPUTilePoolSize"),
	GGPULightmassGPUTilePoolSize,
	TEXT("\n"),
	ECVF_Default
);

int32 GGPULightmassDenoiseGIOnCompletion = WITH_INTELOIDN;
#if WITH_INTELOIDN
static FAutoConsoleVariableRef CVarGGPULightmassDenoiseOnCompletion(
	TEXT("r.GPULightmass.DenoiseGIOnCompletion"),
	GGPULightmassDenoiseGIOnCompletion,
	TEXT("\n"),
	ECVF_Default
);
#endif

int32 GGPULightmassDenoiseGIDuringInteractiveBake = 0;
#if WITH_INTELOIDN
static FAutoConsoleVariableRef CVarGGPULightmassDenoiseGIDuringInteractiveBake(
	TEXT("r.GPULightmass.DenoiseGIDuringInteractiveBake"),
	GGPULightmassDenoiseGIDuringInteractiveBake,
	TEXT("\n"),
	ECVF_Default
);
#endif

AGPULightmassSettingsActor::AGPULightmassSettingsActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.DoNotCreateDefaultSubobject(TEXT("Sprite")))
{
#if WITH_EDITORONLY_DATA
	bActorLabelEditable = false;
#endif // WITH_EDITORONLY_DATA
	bIsEditorOnlyActor = true;

	Settings = ObjectInitializer.CreateDefaultSubobject<UGPULightmassSettings>(this, TEXT("GPULightmassSettings"));
}

void UGPULightmassSettings::ApplyImmediateSettingsToRunningInstances()
{
	// Replicate value to any running instances
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		FGPULightmassModule& GPULightmassModule = FModuleManager::LoadModuleChecked<FGPULightmassModule>(TEXT("GPULightmass"));
		FGPULightmass* GPULightmass = (FGPULightmass*)GPULightmassModule.GetStaticLightingSystemForWorld(World);
		if (GPULightmass)
		{
			GPULightmass->Settings->bShowProgressBars = bShowProgressBars;
			GPULightmass->Settings->TilePassesInSlowMode = TilePassesInSlowMode;
			GPULightmass->Settings->TilePassesInFullSpeedMode = TilePassesInFullSpeedMode;
			GPULightmass->Settings->bVisualizeIrradianceCache = bVisualizeIrradianceCache;
		}
	}
}

#if WITH_EDITOR
void UGPULightmassSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if (PropertyThatChanged)
	{
		FName PropertyName = PropertyThatChanged->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UGPULightmassSettings, bUseIrradianceCaching))
		{
			if (!bUseIrradianceCaching)
			{
				bUseFirstBounceRayGuiding = false;
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UGPULightmassSettings, bShowProgressBars))
		{
			ApplyImmediateSettingsToRunningInstances();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UGPULightmassSettings, TilePassesInSlowMode))
		{
			ApplyImmediateSettingsToRunningInstances();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UGPULightmassSettings, TilePassesInFullSpeedMode))
		{
			ApplyImmediateSettingsToRunningInstances();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UGPULightmassSettings, bVisualizeIrradianceCache))
		{
			ApplyImmediateSettingsToRunningInstances();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool UGPULightmassSettings::CanEditChange(const FProperty* InProperty) const
{
	FName PropertyName = InProperty->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGPULightmassSettings, bShowProgressBars))
	{
		return true;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGPULightmassSettings, TilePassesInSlowMode))
	{
		return true;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGPULightmassSettings, TilePassesInFullSpeedMode))
	{
		return true;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGPULightmassSettings, bVisualizeIrradianceCache))
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		if (World->GetSubsystem<UGPULightmassSubsystem>()->IsRunning())
		{
			return false;
		}
	}

	return true;
}
#endif

void UGPULightmassSettings::GatherSettingsFromCVars()
{
	bShowProgressBars = GGPULightmassShowProgressBars == 1;

	if (GGPULightmassOnlyBakeWhatYouSee == 1)
	{
		Mode = EGPULightmassMode::BakeWhatYouSee;
	}

	GISamples = GGPULightmassSamplesPerTexel;
	StationaryLightShadowSamples = GGPULightmassSamplesPerTexel;
	
	bUseIrradianceCaching = GGPULightmassUseIrradianceCaching == 1;
	IrradianceCacheQuality = GGPULightmassIrradianceCachingQuality;
	IrradianceCacheSpacing = GGPULightmassIrradianceCachingSpacing;
	bVisualizeIrradianceCache = GGPULightmassVisualizeIrradianceCache == 1;

	bUseFirstBounceRayGuiding = GGPULightmassUseFirstBounceRayGuiding == 1;
	FirstBounceRayGuidingTrialSamples = GGPULightmassFirstBounceRayGuidingTrialSamples;

	if (GGPULightmassDenoiseGIOnCompletion == 1)
	{
		DenoisingOptions = EGPULightmassDenoisingOptions::OnCompletion;
	}

	if (GGPULightmassDenoiseGIDuringInteractiveBake == 1)
	{
		// Override
		DenoisingOptions = EGPULightmassDenoisingOptions::DuringInteractivePreview;
	}

	LightmapTilePoolSize = GGPULightmassGPUTilePoolSize;
}

void UGPULightmassSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UWorld* World = Cast<UWorld>(GetOuter());
	
	if (!World) return;

	AGPULightmassSettingsActor* SettingsActor = GetSettingsActor();

	if (SettingsActor == nullptr)
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.Name = AGPULightmassSettingsActor::StaticClass()->GetFName();
		SpawnInfo.bHideFromSceneOutliner = true;
		SettingsActor = World->SpawnActor<AGPULightmassSettingsActor>(AGPULightmassSettingsActor::StaticClass(), SpawnInfo);
	}

	if (SettingsActor == nullptr)
	{
		UE_LOG(LogGPULightmass, Warning, TEXT("Failed to spawn settings actor in World: $s"), *World->GetName());
	}
}

void UGPULightmassSubsystem::Launch()
{
	UWorld* World = Cast<UWorld>(GetOuter());

	if (!World) return;

	FGPULightmassModule& GPULightmassModule = FModuleManager::LoadModuleChecked<FGPULightmassModule>(TEXT("GPULightmass"));

	if (!GPULightmassModule.GetStaticLightingSystemForWorld(World))
	{
		UGPULightmassSettings* SettingsCopy = DuplicateObject(GetSettings(), GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UGPULightmassSettings::StaticClass()));

		FScopedSlowTask SlowTask(1);
		SlowTask.MakeDialog();
		SlowTask.EnterProgressFrame(1, LOCTEXT("StartingStaticLightingSystem", "Starting static lighting system"));

		{
			FGlobalComponentRecreateRenderStateContext RecreateRenderStateContext; // Implicit FlushRenderingCommands();

			FlushRenderingCommands(); // Flush again to execute commands generated by DestroyRenderState_Concurrent()

			IStaticLightingSystem* StaticLightingSystem = GPULightmassModule.AllocateStaticLightingSystemForWorldWithSettings(World, SettingsCopy);

			if (StaticLightingSystem)
			{
				UE_LOG(LogTemp, Log, TEXT("Static lighting system is created for world %s."), *World->GetPathName(World->GetOuter()));

				ULightComponent::ReassignStationaryLightChannels(World, false, NULL);
#if WITH_EDITOR
				if (!GIsEditor)
				{
					if (GEngine)
					{
						GEngine->OnPostEditorTick().AddStatic(&FStaticLightingSystemInterface::GameTick);
					}
				}
#endif // WITH_EDITOR

				int32 NumPrimitiveComponents = 0;
				int32 NumLightComponents = 0;

				for (UPrimitiveComponent* Component : TObjectRange<UPrimitiveComponent>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
				{
					if (Component->HasValidSettingsForStaticLighting(false))
					{
						NumPrimitiveComponents++;
					}
				}

				for (ULightComponentBase* Component : TObjectRange<ULightComponentBase>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
				{
					if (Component->bAffectsWorld && Component->HasStaticShadowing())
					{
						NumLightComponents++;
					}
				}

				FScopedSlowTask SubSlowTask(NumPrimitiveComponents + NumLightComponents, LOCTEXT("RegisteringComponentsWithStaticLightingSystem", "Registering components with static lighting system"));
				SubSlowTask.MakeDialog();

				for (UPrimitiveComponent* Component : TObjectRange<UPrimitiveComponent>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
				{
					if (Component->HasValidSettingsForStaticLighting(false))
					{
						FStaticLightingSystemInterface::OnPrimitiveComponentRegistered.Broadcast(Component);

						SubSlowTask.EnterProgressFrame(1, LOCTEXT("RegisteringComponentsWithStaticLightingSystem", "Registering components with static lighting system"));
					}
				}

				for (ULightComponentBase* Component : TObjectRange<ULightComponentBase>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
				{
					if (Component->bAffectsWorld && Component->HasStaticShadowing())
					{
						FStaticLightingSystemInterface::OnLightComponentRegistered.Broadcast(Component);

						SubSlowTask.EnterProgressFrame(1, LOCTEXT("RegisteringComponentsWithStaticLightingSystem", "Registering components with static lighting system"));
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Tried to create static lighting system for world %s, but failed"), *World->GetPathName(World->GetOuter()));
			}
		}

		FlushRenderingCommands(); // Flush commands generated by ~FGlobalComponentRecreateRenderStateContext();
	}
}

void UGPULightmassSubsystem::Stop()
{
	UWorld* World = Cast<UWorld>(GetOuter());

	if (!World) return;

	FGPULightmassModule& GPULightmassModule = FModuleManager::LoadModuleChecked<FGPULightmassModule>(TEXT("GPULightmass"));

	if (GPULightmassModule.GetStaticLightingSystemForWorld(World))
	{
		FScopedSlowTask SlowTask(1);
		SlowTask.MakeDialog();
		SlowTask.EnterProgressFrame(1, LOCTEXT("RemovingStaticLightingSystem", "Removing static lighting system"));

		{
			FGlobalComponentRecreateRenderStateContext RecreateRenderStateContext; // Implicit FlushRenderingCommands();

			FlushRenderingCommands(); // Flush again to execute commands generated by DestroyRenderState_Concurrent()

			int32 NumPrimitiveComponents = 0;
			int32 NumLightComponents = 0;

			for (UPrimitiveComponent* Component : TObjectRange<UPrimitiveComponent>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
			{
				NumPrimitiveComponents++;
			}

			for (ULightComponentBase* Component : TObjectRange<ULightComponentBase>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
			{
				NumLightComponents++;
			}

			FScopedSlowTask SubSlowTask(NumPrimitiveComponents + NumLightComponents, LOCTEXT("UnregisteringComponentsWithStaticLightingSystem", "Unregistering components with static lighting system"));

			// Unregister all landscapes first to prevent grass picking up landscape lightmaps
			for (ULandscapeComponent* Component : TObjectRange<ULandscapeComponent>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
			{
				FStaticLightingSystemInterface::OnPrimitiveComponentUnregistered.Broadcast(Component);
			}

			for (UPrimitiveComponent* Component : TObjectRange<UPrimitiveComponent>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
			{
				FStaticLightingSystemInterface::OnPrimitiveComponentUnregistered.Broadcast(Component);

				SubSlowTask.EnterProgressFrame(1, LOCTEXT("UnregisteringComponentsWithStaticLightingSystem", "Unregistering components with static lighting system"));
			}

			for (ULightComponentBase* Component : TObjectRange<ULightComponentBase>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
			{
				FStaticLightingSystemInterface::OnLightComponentUnregistered.Broadcast(Component);

				SubSlowTask.EnterProgressFrame(1, LOCTEXT("UnregisteringComponentsWithStaticLightingSystem", "Unregistering components with static lighting system"));
			}

			GPULightmassModule.RemoveStaticLightingSystemForWorld(World);

			UE_LOG(LogTemp, Log, TEXT("Static lighting system is removed for world %s."), *World->GetPathName(World->GetOuter()));
		}

		FlushRenderingCommands(); // Flush commands generated by ~FGlobalComponentRecreateRenderStateContext();
	}

	// Always turn Realtime on after building lighting
	SetRealtime(true);
}

bool UGPULightmassSubsystem::IsRunning()
{
	UWorld* World = Cast<UWorld>(GetOuter());

	if (!World) return false;

	FGPULightmassModule& GPULightmassModule = FModuleManager::LoadModuleChecked<FGPULightmassModule>(TEXT("GPULightmass"));

	return GPULightmassModule.GetStaticLightingSystemForWorld(World) != nullptr;
}

AGPULightmassSettingsActor* UGPULightmassSubsystem::GetSettingsActor()
{
	UWorld* World = Cast<UWorld>(GetOuter());

	if (!World) return nullptr;

	AGPULightmassSettingsActor* SettingsActor = nullptr;

	for (TActorIterator<AGPULightmassSettingsActor> It(World, AGPULightmassSettingsActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
	{
		SettingsActor = *It;
		break;
	}

	return SettingsActor;
}

UGPULightmassSettings* UGPULightmassSubsystem::GetSettings()
{
	return GetSettingsActor() ? GetSettingsActor()->Settings : nullptr;
}

void UGPULightmassSubsystem::StartRecordingVisibleTiles()
{
	// Replicate value to any running instances
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		FGPULightmassModule& GPULightmassModule = FModuleManager::LoadModuleChecked<FGPULightmassModule>(TEXT("GPULightmass"));
		FGPULightmass* GPULightmass = (FGPULightmass*)GPULightmassModule.GetStaticLightingSystemForWorld(World);
		if (GPULightmass)
		{
			GPULightmass->StartRecordingVisibleTiles();
		}
	}
}

void UGPULightmassSubsystem::EndRecordingVisibleTiles()
{
	// Replicate value to any running instances
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		FGPULightmassModule& GPULightmassModule = FModuleManager::LoadModuleChecked<FGPULightmassModule>(TEXT("GPULightmass"));
		FGPULightmass* GPULightmass = (FGPULightmass*)GPULightmassModule.GetStaticLightingSystemForWorld(World);
		if (GPULightmass)
		{
			GPULightmass->EndRecordingVisibleTiles();
		}
	}
}

int32 UGPULightmassSubsystem::GetPercentage()
{
	UWorld* World = Cast<UWorld>(GetOuter());

	if (!World) return 0;

	FGPULightmassModule& GPULightmassModule = FModuleManager::LoadModuleChecked<FGPULightmassModule>(TEXT("GPULightmass"));

	if (GPULightmassModule.StaticLightingSystems.Find(World) != nullptr)
	{
		FGPULightmass* GPULightmass = GPULightmassModule.StaticLightingSystems[World];
		return GPULightmass->LightBuildPercentage;
	}

	return 0;
}

void UGPULightmassSubsystem::SetRealtime(bool bInRealtime)
{
	if (GCurrentLevelEditingViewportClient != nullptr)
	{
		GCurrentLevelEditingViewportClient->SetRealtime(bInRealtime);
	}
	else
	{
		UE_LOG(LogGPULightmass, Warning, TEXT("CurrentLevelEditingViewportClient is NULL!"));
	}
}

void UGPULightmassSubsystem::Save()
{
	UWorld* World = Cast<UWorld>(GetOuter());

	if (!World) return;

	FGPULightmassModule& GPULightmassModule = FModuleManager::LoadModuleChecked<FGPULightmassModule>(TEXT("GPULightmass"));

	if (GPULightmassModule.StaticLightingSystems.Find(World) != nullptr)
	{
		FGPULightmass* GPULightmass = GPULightmassModule.StaticLightingSystems[World];
		GPULightmass->Scene.ApplyFinishedLightmapsToWorld();;
	}
}

#undef LOCTEXT_NAMESPACE
