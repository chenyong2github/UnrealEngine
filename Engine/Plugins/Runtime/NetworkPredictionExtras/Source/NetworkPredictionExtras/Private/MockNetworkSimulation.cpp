// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MockNetworkSimulation.h"
#include "NetworkSimulationModelDebugger.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/CharacterMovementComponent.h" // Temp
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Engine/LocalPlayer.h"
#include "Misc/CoreDelegates.h"
#include "UObject/CoreNet.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "UObject/UObjectIterator.h"
#include "Components/CapsuleComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "Math/Color.h"
#include "DrawDebugHelpers.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Debug/ReporterGraph.h"
#include "EngineUtils.h"


DEFINE_LOG_CATEGORY_STATIC(LogMockNetworkSim, Log, All);

namespace MockNetworkSimCVars
{
static int32 UseVLogger = 1;
static FAutoConsoleVariableRef CVarUseVLogger(TEXT("mns.Debug.UseUnrealVLogger"),
	UseVLogger,	TEXT("Use Unreal Visual Logger\n"),	ECVF_Default);

static int32 UseDrawDebug = 1;
static FAutoConsoleVariableRef CVarUseDrawDebug(TEXT("mns.Debug.UseDrawDebug"),
	UseVLogger,	TEXT("Use built in DrawDebug* functions for visual logging\n"), ECVF_Default);

static float DrawDebugDefaultLifeTime = 30.f;
static FAutoConsoleVariableRef CVarDrawDebugDefaultLifeTime(TEXT("mns.Debug.UseDrawDebug.DefaultLifeTime"),
	DrawDebugDefaultLifeTime, TEXT("Use built in DrawDebug* functions for visual logging"), ECVF_Default);

// -------------------------------

static int32 DoLocalInput = 0;
static FAutoConsoleVariableRef CVarDoLocalInput(TEXT("mns.DoLocalInput"),
	DoLocalInput, TEXT("Submit non 0 imput into the mock simulation"), ECVF_Default);

static int32 RequestMispredict = 0;
static FAutoConsoleVariableRef CVarRequestMispredict(TEXT("mns.RequestMispredict"),
	RequestMispredict, TEXT("Causes a misprediction by inserting random value into stream on authority side"), ECVF_Default);

static int32 BindAutomatically = 1;
static FAutoConsoleVariableRef CVarBindAutomatically(TEXT("mns.BindAutomatically"),
	BindAutomatically, TEXT("Binds local input and mispredict commands to 5 and 6 respectively"), ECVF_Default);
}

// ============================================================================================

static bool ForceMispredict = false;

const FName FMockNetworkSimulation::GroupName(TEXT("Mock"));

void FMockNetworkSimulation::Update(IMockDriver* Driver, const float DeltaTimeSeconds, const FMockInputCmd& InputCmd, const FMockSyncState& InputState, FMockSyncState& OutputState, const FMockAuxState& AuxState)
{
	OutputState.Total = InputState.Total + (InputCmd.InputValue * AuxState.Multiplier * DeltaTimeSeconds);

	// Dev hack to force mispredict
	if (ForceMispredict)
	{
		const float Fudge = FMath::FRand() * 100.f;
		OutputState.Total += Fudge;
		UE_LOG(LogMockNetworkSim, Warning, TEXT("ForcingMispredict via CVar. Fudge=%.2f. NewTotal: %.2f"), Fudge, OutputState.Total);
		
		ForceMispredict = false;
	}
}

void FMockSyncState::VisualLog(const FVisualLoggingParameters& Parameters, IMockDriver* Driver, IMockDriver* LogDriver) const
{
	// This function is awkward because the mock example doesn't include any positional data, so we added an explicit ::GetDebugWorldTransform to the driver interface.
	// still, this doesn't give us historical location info. So we will just use Z offset based on the EvisualLoggingContext to space things out.
	const bool DrawStyleMinor = (Parameters.Context == EVisualLoggingContext::OtherMispredicted || Parameters.Context == EVisualLoggingContext::OtherPredicted);
	const FTransform WorldTransform = LogDriver->GetDebugWorldTransform(); // No sense using the Driver's world transform (this would be the server's position in PIE, not useful in mock sim)
	UWorld* DebugWorld = LogDriver->GetDriverWorld();

	FString DebugStr = FString::Printf(TEXT("[%d][%s] Total: %.4f"), Parameters.Keyframe, *LexToString(Parameters.Context), this->Total);
	if (MockNetworkSimCVars::UseDrawDebug)
	{
		const float Lifetime = Parameters.Lifetime == EVisualLoggingLifetime::Transient ? 0.0001f : MockNetworkSimCVars::DrawDebugDefaultLifeTime;
		const float YOffset = 0.f; //DrawStyleMinor ? ((100.f * (Parameters.Keyframe % 32)) - 50.f) : 0.f;
		const float ZOffset = 20.f * (int32)Parameters.Context;
		
		DrawDebugString( DebugWorld, WorldTransform.GetLocation() + WorldTransform.GetRotation().RotateVector( FVector(0.f, YOffset, ZOffset) ), *DebugStr, nullptr, Parameters.GetDebugColor(), Lifetime );
	}

	if (MockNetworkSimCVars::UseVLogger)
	{
		// I don't see a "Draw string in th world" function in VLOG so we are just doing this
		UE_VLOG(LogDriver->GetVLogOwner(), LogMockNetworkSim, Log, TEXT("%s"), *DebugStr);
	}
}

// ===============================================================================================
//
// ===============================================================================================

UMockNetworkSimulationComponent::UMockNetworkSimulationComponent()
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = true;

	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);

	bWantsInitializeComponent = true;

	// Set default bindings. This is not something you would ever want in a shipping game,
	// but the optional, latent, non evasive nature of the NetworkPredictionExtras module makes this ideal approach
	if (HasAnyFlags(RF_ClassDefaultObject) == false && MockNetworkSimCVars::BindAutomatically > 0)
	{
		if (ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController())
		{
			LocalPlayer->Exec(GetWorld(), TEXT("setbind Five \"mns.DoLocalInput 1 | OnRelease mns.DoLocalInput 0\""), *GLog);
			LocalPlayer->Exec(GetWorld(), TEXT("setbind Six \"mns.RequestMispredict 1\""), *GLog);

			LocalPlayer->Exec(GetWorld(), TEXT("setbind Nine nms.Debug.LocallyControlledPawn"), *GLog);
			LocalPlayer->Exec(GetWorld(), TEXT("setbind Zero nms.Debug.ToggleContinous"), *GLog);
		}
	}
}

INetworkSimulationModel* UMockNetworkSimulationComponent::InstantiateNetworkSimulation()
{
	auto* NewSim = new FMockNetworkModel(this);
	DO_NETSIM_MODEL_DEBUG(FNetworkSimulationModelDebuggerManager::Get().RegisterNetworkSimulationModel(NewSim, GetOwner()));
	return NewSim;
}

void UMockNetworkSimulationComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	const ENetRole OwnerRole = GetOwnerRole();

	// Check if we should trip a mispredict. (Note how its not possible to do this inside the Update function!)
	if (OwnerRole == ROLE_Authority && MockNetworkSimCVars::RequestMispredict)
	{
		ForceMispredict = true;
		MockNetworkSimCVars::RequestMispredict = 0;
	}
	
	// Mock example of displaying
	DrawDebugString( GetWorld(), GetOwner()->GetActorLocation() + FVector(0.f,0.f,100.f), *LexToString(MockValue), nullptr, FColor::White, 0.00001f );
}

void UMockNetworkSimulationComponent::ProduceInput(const FNetworkSimTime SimTime, FMockInputCmd& Cmd)
{
	if (MockNetworkSimCVars::DoLocalInput)
	{
		Cmd.InputValue = FMath::FRand() * 10.f;
	}
	else
	{
		Cmd.InputValue = 0.f;
	}
}

void UMockNetworkSimulationComponent::InitSyncState(FMockSyncState& OutSyncState) const
{
	OutSyncState.Total = MockValue;
}

void UMockNetworkSimulationComponent::FinalizeFrame(const FMockSyncState& SyncState)
{
	MockValue = SyncState.Total;
}

FString UMockNetworkSimulationComponent::GetDebugName() const
{
	return FString::Printf(TEXT("MockSim. %s. %s"), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwnerRole()), *GetName());
}

FTransform UMockNetworkSimulationComponent::GetDebugWorldTransform() const
{
	return GetOwner()->GetActorTransform();
}

UObject* UMockNetworkSimulationComponent::GetVLogOwner() const
{
	return GetOwner();
}

// ------------------------------------------------------------------------------------------------------------------------
//
// ------------------------------------------------------------------------------------------------------------------------

FAutoConsoleCommandWithWorldAndArgs MockNetworkSimulationSpawnCmd(TEXT("mns.Spawn"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& InArgs, UWorld* World) 
{
	bool bFoundWorld = false;
	for (TObjectIterator<UWorld> It; It; ++It)
	{
		if (It->WorldType == EWorldType::PIE && It->GetNetMode() != NM_Client)
		{
			bFoundWorld = true;
			for (TActorIterator<APawn> ActorIt(*It); ActorIt; ++ActorIt)
			{
				if (APawn* Pawn = *ActorIt)
				{
					UMockNetworkSimulationComponent* NewComponent = NewObject<UMockNetworkSimulationComponent>(Pawn);
					NewComponent->RegisterComponent();
				}
			}
		}
	}
}));