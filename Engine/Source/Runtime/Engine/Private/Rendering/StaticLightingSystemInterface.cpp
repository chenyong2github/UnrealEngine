// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/StaticLightingSystemInterface.h"
#include "RenderingThread.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Engine/World.h"
#include "Components/LightComponent.h"
#include "LandscapeComponent.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "StaticLightingSystem"

FStaticLightingSystemInterface* FStaticLightingSystemInterface::Interface = nullptr;

FStaticLightingSystemInterface::FPrimitiveComponentBasedSignature FStaticLightingSystemInterface::OnPrimitiveComponentRegistered;
FStaticLightingSystemInterface::FPrimitiveComponentBasedSignature FStaticLightingSystemInterface::OnPrimitiveComponentUnregistered;
FStaticLightingSystemInterface::FLightComponentBasedSignature FStaticLightingSystemInterface::OnLightComponentRegistered;
FStaticLightingSystemInterface::FLightComponentBasedSignature FStaticLightingSystemInterface::OnLightComponentUnregistered;
FStaticLightingSystemInterface::FStationaryLightChannelReassignmentSignature FStaticLightingSystemInterface::OnStationaryLightChannelReassigned;
FStaticLightingSystemInterface::FLightmassImportanceVolumeModifiedSignature FStaticLightingSystemInterface::OnLightmassImportanceVolumeModified;
FStaticLightingSystemInterface::FMaterialInvalidationSignature FStaticLightingSystemInterface::OnMaterialInvalidated;

FStaticLightingSystemInterface* FStaticLightingSystemInterface::Get()
{
	if (!Interface)
	{
		Interface = new FStaticLightingSystemInterface();
	}

	return Interface;
}

const FMeshMapBuildData* FStaticLightingSystemInterface::GetPrimitiveMeshMapBuildData(const UPrimitiveComponent* Component, int32 LODIndex /* = 0 */)
{
	if (Get()->GetPreferredImplementation())
	{
		if (Component->GetWorld())
		{
			if (Get()->GetPreferredImplementation()->GetStaticLightingSystemForWorld(Component->GetWorld()))
			{
				return Get()->GetPreferredImplementation()->GetStaticLightingSystemForWorld(Component->GetWorld())->GetPrimitiveMeshMapBuildData(Component, LODIndex);
			}
		}
	}

	return nullptr;
}

const FLightComponentMapBuildData* FStaticLightingSystemInterface::GetLightComponentMapBuildData(const ULightComponent* Component)
{
	if (Get()->GetPreferredImplementation())
	{
		if (Component->GetWorld())
		{
			if (Get()->GetPreferredImplementation()->GetStaticLightingSystemForWorld(Component->GetWorld()))
			{
				return Get()->GetPreferredImplementation()->GetStaticLightingSystemForWorld(Component->GetWorld())->GetLightComponentMapBuildData(Component);
			}
		}
	}

	return nullptr;
}

const FPrecomputedVolumetricLightmap* FStaticLightingSystemInterface::GetPrecomputedVolumetricLightmap(UWorld* World)
{
	if (Get()->GetPreferredImplementation())
	{
		if (Get()->GetPreferredImplementation()->GetStaticLightingSystemForWorld(World))
		{
			return Get()->GetPreferredImplementation()->GetStaticLightingSystemForWorld(World)->GetPrecomputedVolumetricLightmap();
		}
	}

	return nullptr;
}

void FStaticLightingSystemInterface::EditorTick()
{
	if (Get()->GetPreferredImplementation())
	{
		Get()->GetPreferredImplementation()->EditorTick();
	}
}

// For editor -game
void FStaticLightingSystemInterface::GameTick(float DeltaSeconds)
{
	EditorTick();
}

bool FStaticLightingSystemInterface::IsStaticLightingSystemRunning()
{
	if (Get()->GetPreferredImplementation())
	{
		return Get()->GetPreferredImplementation()->IsStaticLightingSystemRunning();
	}

	return false;
}

void FStaticLightingSystemInterface::RegisterImplementation(FName Name, IStaticLightingSystemImpl* StaticLightingSystemImpl)
{
	check(IsInGameThread());
	check(Implementations.Find(Name) == nullptr);
	Implementations.Add(Name, StaticLightingSystemImpl);
}

void FStaticLightingSystemInterface::UnregisterImplementation(FName Name)
{
	check(IsInGameThread());
	check(Implementations.Find(Name) != nullptr);
	Implementations.Remove(Name);
}

IStaticLightingSystemImpl* FStaticLightingSystemInterface::GetPreferredImplementation()
{
	if (Implementations.Find(FName(TEXT("GPULightmass"))) != nullptr)
	{
		return *Implementations.Find(FName(TEXT("GPULightmass")));
	}
	
	if (Implementations.Num() > 0)
	{
		return Implementations.CreateIterator().Value();
	}

	return nullptr;
}

bool FStaticLightingSystemInterface::ShouldOperateOnWorld(UWorld* InWorld)
{
	check(IsInGameThread());

	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnGameThread() != 0);

	// IsEditorWorld() is true also for PIE and EditorPreview, which is not what we want
	return bAllowStaticLighting;
}

ENGINE_API void ToggleLightmapPreview_GameThread(UWorld* InWorld)
{
	if (FStaticLightingSystemInterface::Get()->ShouldOperateOnWorld(InWorld))
	{
		IStaticLightingSystemImpl* StaticLightingSystemImpl = FStaticLightingSystemInterface::Get()->GetPreferredImplementation();

		// Extra check to avoid the cost of FGlobalComponentRecreateRenderStateContext
		if (StaticLightingSystemImpl && StaticLightingSystemImpl->SupportsRealtimePreview())
		{
			// At this point the assumption is GT & RT are synchronized and we can access FScene from GT
			if (!StaticLightingSystemImpl->GetStaticLightingSystemForWorld(InWorld))
			{
				FScopedSlowTask SlowTask(1);
				SlowTask.MakeDialog();
				SlowTask.EnterProgressFrame(1, LOCTEXT("StartingStaticLightingSystem", "Starting static lighting system"));

				{
					FGlobalComponentRecreateRenderStateContext RecreateRenderStateContext; // Implicit FlushRenderingCommands();

					FlushRenderingCommands(); // Flush again to execute commands generated by DestroyRenderState_Concurrent()

					IStaticLightingSystem* StaticLightingSystem = StaticLightingSystemImpl->AllocateStaticLightingSystemForWorld(InWorld);

					if (StaticLightingSystem)
					{
						UE_LOG(LogTemp, Log, TEXT("Static lighting system is created for world %s."), *InWorld->GetPathName(InWorld->GetOuter()));

						ULightComponent::ReassignStationaryLightChannels(InWorld, false, NULL);
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
						UE_LOG(LogTemp, Log, TEXT("Tried to create static lighting system for world %s, but failed"), *InWorld->GetPathName(InWorld->GetOuter()));
					}
				}

				FlushRenderingCommands(); // Flush commands generated by ~FGlobalComponentRecreateRenderStateContext();
			}
			else
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

					StaticLightingSystemImpl->RemoveStaticLightingSystemForWorld(InWorld);

					UE_LOG(LogTemp, Log, TEXT("Static lighting system is removed for world %s."), *InWorld->GetPathName(InWorld->GetOuter()));
				}

				FlushRenderingCommands(); // Flush commands generated by ~FGlobalComponentRecreateRenderStateContext();
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("We should not operate on world %s."), *InWorld->GetPathName(InWorld->GetOuter()));
	}
}

FAutoConsoleCommandWithWorld GToggleLightmapPreviewCmd(
	TEXT("ToggleLightmapPreview"),
	TEXT("Toggles lightmap preview in editor"),
	FConsoleCommandWithWorldDelegate::CreateStatic(ToggleLightmapPreview_GameThread));

#undef LOCTEXT_NAMESPACE
