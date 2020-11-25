// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayDebuggerPlayerManager.h"
#include "Engine/World.h"
#include "Components/InputComponent.h"
#include "GameplayDebuggerCategoryReplicator.h"
#include "GameplayDebuggerLocalController.h"
#include "Engine/DebugCameraController.h"
#include "Engine/InputDelegateBinding.h"
#include "GameplayDebuggerConfig.h"


AGameplayDebuggerPlayerManager::AGameplayDebuggerPlayerManager(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bAllowTickOnDedicatedServer = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = 0.5f;

#if WITH_EDITOR
	SetIsTemporarilyHiddenInEditor(true);
#endif

#if WITH_EDITORONLY_DATA
	bHiddenEdLevel = true;
	bHiddenEdLayer = true;
	bHiddenEd = true;
	bEditable = false;
#endif

	bIsLocal = false;
	bInitialized = false;
}

void AGameplayDebuggerPlayerManager::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		if (InputComponent == nullptr)
		{
			// create an InputComponent object so that the level script actor can bind key events
			InputComponent = NewObject<UInputComponent>(this, TEXT("GameplayDebug_Input"));
			InputComponent->RegisterComponent();
		}

		if (UInputDelegateBinding::SupportsInputDelegate(GetClass()))
		{
			UInputDelegateBinding::BindInputDelegates(GetClass(), InputComponent);
		}
	}
}

void AGameplayDebuggerPlayerManager::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	check(World);
	const ENetMode NetMode = World->GetNetMode();
	
	bHasAuthority = FGameplayDebuggerUtils::IsAuthority(World);
	bIsLocal = (NetMode != NM_DedicatedServer);
	bInitialized = true;

	if (bHasAuthority)
	{
		UpdateAuthReplicators();
		SetActorTickEnabled(true);
	}
	
	for (int32 Idx = 0; Idx < PendingRegistrations.Num(); Idx++)
	{
		RegisterReplicator(*PendingRegistrations[Idx]);
	}

	PendingRegistrations.Empty();
}

void AGameplayDebuggerPlayerManager::EndPlay(const EEndPlayReason::Type Reason)
{
	Super::EndPlay(Reason);

	for (int32 Idx = 0; Idx < PlayerData.Num(); Idx++)
	{
		FGameplayDebuggerPlayerData& TestData = PlayerData[Idx];
		if (IsValid(TestData.Controller))
		{
			TestData.Controller->Cleanup();
			TestData.Controller = nullptr;
		}
	}
}

void AGameplayDebuggerPlayerManager::Init()
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (World != nullptr && World->WorldType == EWorldType::Editor && (GetDefault<UGameplayDebuggerUserSettings>()->bEnableGameplayDebuggerInEditor))
	{
		bHasAuthority = true;
		bIsLocal = true;
		bInitialized = true;

		AGameplayDebuggerCategoryReplicator* Replicator = World->SpawnActorDeferred<AGameplayDebuggerCategoryReplicator>(AGameplayDebuggerCategoryReplicator::StaticClass(), FTransform::Identity);
		Replicator->SetReplicatorOwner(nullptr);
		Replicator->FinishSpawning(FTransform::Identity, true);
		SetActorTickEnabled(true);

		EditorWorldData.Replicator = Replicator;

		Replicator->InitForEditor();

		EditorWorldData.Controller = NewObject<UGameplayDebuggerLocalController>(this, TEXT("GameplayDebug_Controller_Editor"));
		EditorWorldData.Controller->Initialize(*Replicator, *this);
		
	}
#endif // WITH_EDITOR
}

void AGameplayDebuggerPlayerManager::TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	Super::TickActor(DeltaTime, TickType, ThisTickFunction);
	UpdateAuthReplicators();
};

void AGameplayDebuggerPlayerManager::UpdateAuthReplicators()
{
	UWorld* World = GetWorld();
	for (int32 Idx = PlayerData.Num() - 1; Idx >= 0; Idx--)
	{
		FGameplayDebuggerPlayerData& TestData = PlayerData[Idx];

		if (!IsValid(TestData.Replicator) || !IsValid(TestData.Replicator->GetReplicationOwner()))
		{
			if (IsValid(TestData.Replicator))
			{
				World->DestroyActor(TestData.Replicator);
			}

			if (IsValid(TestData.Controller))
			{
				TestData.Controller->Cleanup();
			}

			PlayerData.RemoveAt(Idx, 1, false);
		}
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; It++)
	{
		APlayerController* TestPC = It->Get();
		if (TestPC && !TestPC->IsA<ADebugCameraController>())
		{
			const bool bNeedsReplicator = (GetReplicator(*TestPC) == nullptr);
			if (bNeedsReplicator)
			{
				AGameplayDebuggerCategoryReplicator* Replicator = World->SpawnActorDeferred<AGameplayDebuggerCategoryReplicator>(AGameplayDebuggerCategoryReplicator::StaticClass(), FTransform::Identity);
				Replicator->SetReplicatorOwner(TestPC);
				Replicator->FinishSpawning(FTransform::Identity, true);
			}
		}
	}

	PrimaryActorTick.TickInterval = PlayerData.Num() ? 5.0f : 0.5f;
}

void AGameplayDebuggerPlayerManager::RegisterReplicator(AGameplayDebuggerCategoryReplicator& Replicator)
{
	if (!bInitialized)
	{
		PendingRegistrations.Add(&Replicator);
		return;
	}

	// keep all player related objects together for easy access and GC
	FGameplayDebuggerPlayerData NewData;
	NewData.Replicator = &Replicator;
	
	if (bIsLocal)
	{
		APlayerController* OwnerPC = Replicator.GetReplicationOwner();

		NewData.InputComponent = OwnerPC ? NewObject<UInputComponent>(OwnerPC, TEXT("GameplayDebug_Input")) : InputComponent;
		check(NewData.InputComponent);
		NewData.InputComponent->Priority = -1;
		NewData.Controller = NewObject<UGameplayDebuggerLocalController>(OwnerPC ? OwnerPC : (AActor*)this, TEXT("GameplayDebug_Controller"));
		NewData.Controller->Initialize(Replicator, *this);
		NewData.Controller->BindInput(*NewData.InputComponent);

		if (OwnerPC)
		{
			OwnerPC->PushInputComponent(NewData.InputComponent);
		}
	}
	else
	{
		NewData.Controller = nullptr;
		NewData.InputComponent = nullptr;
	}

	PlayerData.Add(NewData);
}

void AGameplayDebuggerPlayerManager::RefreshInputBindings(AGameplayDebuggerCategoryReplicator& Replicator)
{
	for (int32 Idx = 0; Idx < PlayerData.Num(); Idx++)
	{
		FGameplayDebuggerPlayerData& TestData = PlayerData[Idx];
		if (TestData.Replicator == &Replicator)
		{
			TestData.InputComponent->ClearActionBindings();
			TestData.InputComponent->ClearBindingValues();
			TestData.InputComponent->KeyBindings.Empty();

			TestData.Controller->BindInput(*TestData.InputComponent);
		}
	}
}

AGameplayDebuggerCategoryReplicator* AGameplayDebuggerPlayerManager::GetReplicator(const APlayerController& OwnerPC) const
{
	const FGameplayDebuggerPlayerData* DataPtr = GetPlayerData(OwnerPC);
	return DataPtr ? DataPtr->Replicator : nullptr;
}

UInputComponent* AGameplayDebuggerPlayerManager::GetInputComponent(const APlayerController& OwnerPC) const
{
	const FGameplayDebuggerPlayerData* DataPtr = GetPlayerData(OwnerPC);
	return DataPtr ? DataPtr->InputComponent : nullptr;
}

UGameplayDebuggerLocalController* AGameplayDebuggerPlayerManager::GetLocalController(const APlayerController& OwnerPC) const
{
	const FGameplayDebuggerPlayerData* DataPtr = GetPlayerData(OwnerPC);
	return DataPtr ? DataPtr->Controller : nullptr;
}

const FGameplayDebuggerPlayerData* AGameplayDebuggerPlayerManager::GetPlayerData(const APlayerController& OwnerPC) const
{
	for (int32 Idx = 0; Idx < PlayerData.Num(); Idx++)
	{
		const FGameplayDebuggerPlayerData& TestData = PlayerData[Idx];
		if (TestData.Replicator && TestData.Replicator->GetReplicationOwner() == &OwnerPC)
		{
			return &TestData;
		}
	}

	return nullptr;
}

// FTickableGameObject begin
void AGameplayDebuggerPlayerManager::Tick(float DeltaTime)
{
#if WITH_EDITORONLY_DATA 
	if (EditorWorldData.Replicator)
	{
		FActorTickFunction DummyTickFunction;
		EditorWorldData.Replicator->TickActor(DeltaTime, ELevelTick::LEVELTICK_All, DummyTickFunction);
	}
#endif // WITH_EDITORONLY_DATA 
}

ETickableTickType AGameplayDebuggerPlayerManager::GetTickableTickType() const
{
	return
#if WITH_EDITOR
		IsTickable() ? ETickableTickType::Conditional : 
#endif // WITH_EDITOR
		ETickableTickType::Never;
}

TStatId AGameplayDebuggerPlayerManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(AGameplayDebuggerPlayerManager, STATGROUP_Tickables);
}

bool AGameplayDebuggerPlayerManager::IsTickable() const
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	return (World != nullptr) && (World->IsEditorWorld() == true) && (World->IsGameWorld() == false);
#else
	return false;
#endif // WITH_EDITOR
}
// FTickableGameObject end

