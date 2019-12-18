// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MockNetworkSimulation.h"
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
const FName FMockNetworkModelDef::GroupName(TEXT("Mock"));

NETSIMCUE_REGISTER(FMockCue, TEXT("MockCue"));
NETSIMCUESET_REGISTER(UMockNetworkSimulationComponent, FMockCueSet);

void FMockNetworkSimulation::SimulationTick(const FNetSimTimeStep& TimeStep, const TNetSimInput<TMockNetworkSimulationBufferTypes>& Input, const TNetSimOutput<TMockNetworkSimulationBufferTypes>& Output)
{
	Output.Sync.Total = Input.Sync.Total + (Input.Cmd.InputValue * Input.Aux.Multiplier * TimeStep.StepMS.ToRealTimeSeconds());

	// Dev hack to force mispredict
	if (ForceMispredict)
	{
		const float Fudge = FMath::FRand() * 100.f;
		Output.Sync.Total += Fudge;
		UE_LOG(LogMockNetworkSim, Warning, TEXT("ForcingMispredict via CVar. Fudge=%.2f. NewTotal: %.2f"), Fudge, Output.Sync.Total);
		
		ForceMispredict = false;
	}
	
	if ((int32)(Input.Sync.Total/10.f) < (int32)(Output.Sync.Total/10.f))
	{
		// Emit a cue for crossing this threshold. Note this could mostly be accomplished with a state transition (by detecting this in FinalizeFrame)
		// But to illustrate Cues, we are adding a random number to the cue's payload. The point being cues can transmit data that is not part of the sync/aux state.
		Output.CueDispatch.Invoke<FMockCue>( FMath::Rand() % 1024 );
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

INetworkedSimulationModel* UMockNetworkSimulationComponent::InstantiateNetworkedSimulation()
{
	check(MockNetworkSimulation == nullptr);
	MockNetworkSimulation = new FMockNetworkSimulation();

	FMockSyncState InitialSyncState;
	InitialSyncState.Total = MockValue;

	auto* NewModel = new TNetworkedSimulationModel<FMockNetworkModelDef>(MockNetworkSimulation, this, InitialSyncState);
	return NewModel;
}

void UMockNetworkSimulationComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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

void UMockNetworkSimulationComponent::FinalizeFrame(const FMockSyncState& SyncState, const FMockAuxState& AuxState)
{
	MockValue = SyncState.Total;
}

FString UMockNetworkSimulationComponent::GetDebugName() const
{
	return FString::Printf(TEXT("MockSim. %s. %s"), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwnerRole()), *GetName());
}

AActor* UMockNetworkSimulationComponent::GetVLogOwner() const
{
	return GetOwner();
}

void UMockNetworkSimulationComponent::VisualLog(const FMockInputCmd* Input, const FMockSyncState* Sync, const FMockAuxState* Aux, const FVisualLoggingParameters& SystemParameters) const
{
	FVisualLoggingParameters LocalParameters = SystemParameters;
	AActor* Owner = GetOwner();
	FTransform Transform = Owner->GetActorTransform();

	LocalParameters.DebugString += FString::Printf(TEXT(" [%d] Total: %.4f"), SystemParameters.Frame, Sync->Total);

	FVisualLoggingHelpers::VisualLogActor(Owner, Transform, LocalParameters);
}

void UMockNetworkSimulationComponent::HandleCue(const FMockCue& MockCue, const FNetSimCueSystemParamemters& SystemParameters)
{
	UE_LOG(LogMockNetworkSim, Display, TEXT("MockCue Handled! Value: %d"), MockCue.RandomData);
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