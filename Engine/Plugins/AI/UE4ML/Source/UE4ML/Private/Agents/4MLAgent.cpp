// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agents/4MLAgent.h"
#include "Sensors/4MLSensor.h"
#include "4MLSession.h"
#include "4MLSpace.h"
#include "4MLLibrarian.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "Actuators/4MLActuator_InputKey.h"
#include "UObject/UObjectGlobals.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UObject/GarbageCollection.h"
#include "GameFramework/PlayerState.h"


//----------------------------------------------------------------------//
// F4MLAgentHelpers 
//----------------------------------------------------------------------//
namespace F4MLAgentHelpers
{
	bool GetAsPawnAndController(AActor* Avatar, AController*& OutController, APawn*& OutPawn)
	{
		if (Avatar == nullptr)
		{
			return false;
		}

		APawn* AsPawn = Cast<APawn>(Avatar);
		if (AsPawn)
		{
			OutPawn = AsPawn;
			OutController = AsPawn->GetController();
			return true;
		}

		AController* AsController = Cast<AController>(Avatar);
		if (AsController)
		{
			OutPawn = AsController->GetPawn();
			OutController = AsController;
			return true;
		}

		return false;
	}
}

//----------------------------------------------------------------------//
// F4MLAgentConfig
//----------------------------------------------------------------------//
F4MLParameterMap& F4MLAgentConfig::AddSensor(const FName SensorName, F4MLParameterMap&& Parameters)
{
	F4MLParameterMap& Entry = Sensors.Add(SensorName, Parameters);
	return Entry;
}

F4MLParameterMap& F4MLAgentConfig::AddActuator(const FName ActuatorName, F4MLParameterMap&& Parameters)
{
	F4MLParameterMap& Entry = Actuators.Add(ActuatorName, Parameters);
	return Entry;
}

//----------------------------------------------------------------------//
// U4MLAgent 
//----------------------------------------------------------------------//
U4MLAgent::U4MLAgent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AgentID = F4ML::InvalidAgentID;
	bEverHadAvatar = false;
	bRegisteredForPawnControllerChange = false;

	AgentConfig.AddSensor(TEXT("Camera"));
	AgentConfig.AddSensor(TEXT("Movement"));
	AgentConfig.AddSensor(TEXT("Input"));
	AgentConfig.AddActuator(U4MLActuator_InputKey::StaticClass()->GetFName());
	AgentConfig.Actuators.Add(U4MLActuator_InputKey::StaticClass()->GetFName(), F4MLParameterMap());
	AgentConfig.AvatarClass = APlayerController::StaticClass();
	AgentConfig.bAutoRequestNewAvatarUponClearingPrev = true;
}

void U4MLAgent::BeginDestroy()
{
	ShutDownSensorsAndActuators();
	// forcing unhooking from all event delegates
	SetAvatar(nullptr); 

	if (bRegisteredForPawnControllerChange)
	{
		UGameInstance* GameInstance = GetSession().GetGameInstance();
		if (GameInstance)
		{
			GameInstance->GetOnPawnControllerChanged().RemoveAll(this);
			bRegisteredForPawnControllerChange = false;
		}
	}

	Super::BeginDestroy();
}

bool U4MLAgent::RegisterSensor(U4MLSensor& Sensor)
{
	ensure(Sensor.IsConfiguredForAgent(*this));
	Sensors.Add(&Sensor);
	return true;
}

void U4MLAgent::OnAvatarDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == Avatar)
	{
		SetAvatar(nullptr);

		if (AgentConfig.bAutoRequestNewAvatarUponClearingPrev)
		{
			// note that after this call we might not have the avatar just yet 
			// since the world might not have any. The Session will make sure to 
			// assign us one as soon as one's ready.
			GetSession().RequestAvatarForAgent(*this);
		}
	}
}

void U4MLAgent::OnPawnChanged(APawn* NewPawn, AController* InController)
{
	ensure(Controller == InController);
	if (Controller != InController)
	{
		// this came from a different controller we somehow missed unbinding from. Ignore.
		return;
	}

	if (Pawn == NewPawn)
	{
		return;
	}

	// let every sense that requires a pawn know that the pawn changed 
	for (U4MLSensor* Sensor : Sensors)
	{
		Sensor->OnPawnChanged(Pawn, NewPawn);
	}

	Pawn = NewPawn;
}

void U4MLAgent::OnPawnControllerChanged(APawn* InPawn, AController* InController)
{
	if (InPawn == Pawn)
	{
		if (Pawn == Avatar)
		{
			Controller = InController;
		}
		// if controller is the main avatar we might have just lost our pawn
		else if (Controller && Controller != InController && Controller == Avatar)
		{
			OnPawnChanged(Controller->GetPawn() == InPawn ? nullptr : Controller->GetPawn(), Controller);
		}
	}
}

void U4MLAgent::Sense(const float DeltaTime)
{
	for (U4MLSensor* Sensor : Sensors)
	{
		// not that due to the system's design Sensor won't be null
		Sensor->Sense(DeltaTime);
	}
}

void U4MLAgent::Act(const float DeltaTime)
{
	for (U4MLActuator* Actuator : Actuators)
	{
		Actuator->Act(DeltaTime);
	}
}

void U4MLAgent::DigestActions(F4MLMemoryReader& ValueStream)
{
	for (U4MLActuator* Actuator : Actuators)
	{
		Actuator->DigestInputData(ValueStream);
	}
}

void U4MLAgent::GetObservations(F4MLMemoryWriter& Ar)
{
	for (U4MLSensor* Sensor : Sensors)
	{
		Sensor->GetObservations(Ar);
	}
}

void U4MLAgent::ShutDownSensorsAndActuators()
{	
	for (U4MLActuator* Actuator : Actuators)
	{
		if (Actuator)
		{
			Actuator->Shutdown();
		}
	}
	Actuators.Reset();

	for (U4MLSensor* Sensor : Sensors)
	{
		if (Sensor)
		{
			Sensor->Shutdown();
		}
	}
	Sensors.Reset();
}

void U4MLAgent::Configure(const F4MLAgentConfig& NewConfig)
{
	ShutDownSensorsAndActuators();

	TSubclassOf<AActor> PreviousAvatarClass = AgentConfig.AvatarClass;
	AgentConfig = NewConfig;

	for (const TTuple<FName, F4MLParameterMap>& KeyValue : AgentConfig.Actuators)
	{
		UClass* ResultClass = F4MLLibrarian::Get().FindActuatorClass(KeyValue.Key);
		if (ResultClass)
		{
			U4MLActuator* NewActuator = F4ML::NewObject<U4MLActuator>(this, ResultClass);
			check(NewActuator);
			NewActuator->SetNickname(KeyValue.Key.ToString());
			NewActuator->Configure(KeyValue.Value.Params);
			Actuators.Add(NewActuator);
		}
	}
	Actuators.Sort(FAgentElementSort());

	for (const TTuple<FName, F4MLParameterMap>& KeyValue : AgentConfig.Sensors)
	{
		UClass* ResultClass = F4MLLibrarian::Get().FindSensorClass(KeyValue.Key);
		if (ResultClass)
		{
			U4MLSensor* NewSensor = F4ML::NewObject<U4MLSensor>(this, ResultClass);
			check(NewSensor);
			NewSensor->SetNickname(KeyValue.Key.ToString());
			NewSensor->Configure(KeyValue.Value.Params);
			Sensors.Add(NewSensor);
		}
	}
	Sensors.Sort(FAgentElementSort());

	if (NewConfig.AvatarClassName != NAME_None)
	{
		AgentConfig.AvatarClass = FindObject<UClass>(ANY_PACKAGE, *NewConfig.AvatarClassName.ToString());
	}
	if (!AgentConfig.AvatarClass)
	{
		AgentConfig.AvatarClass = AActor::StaticClass();
	}

	ensure(AgentConfig.AvatarClass || Avatar);

	if (AgentConfig.AvatarClass && (Avatar == nullptr || IsSuitableAvatar(*Avatar) == false))
	{
		SetAvatar(nullptr);

		// if avatar class changed make sure the following RequestAvatarForAgent actually tries to find an avatar
		// rather than just ignoring the request due to the agent already being in AwaitingAvatar
		const bool bForceUpdate = (AgentConfig.AvatarClass != PreviousAvatarClass);

		// note that after this call we might not have the avatar just yet 
		// since the world might not have any. The Session will make sure to 
		// assign us one as soon as one's ready.
		GetSession().RequestAvatarForAgent(*this, /*World=*/nullptr, bForceUpdate);
	}
	else if (ensure(Avatar))
	{
		// newly created actuators and sensors might need the information about 
		// the current avatar
		for (U4MLSensor* Sensor : Sensors)
		{
			Sensor->OnAvatarSet(Avatar);
		}
		for (U4MLActuator* Actuator : Actuators)
		{
			Actuator->OnAvatarSet(Avatar);
		}
	}
}

U4MLSession& U4MLAgent::GetSession()
{
	U4MLSession* Session = CastChecked<U4MLSession>(GetOuter());
	return *Session;
}

void U4MLAgent::GetActionSpaceDescription(F4MLSpaceDescription& OutSpaceDesc) const
{
	F4MLDescription ElementDesc;

	for (U4MLActuator* Actuator : Actuators)
	{
		if (Actuator)
		{
			ElementDesc.Reset();
			ElementDesc.Add(Actuator->GetSpaceDef());
			OutSpaceDesc.Add(Actuator->GetNickname(), ElementDesc);
		}
	}
}

void U4MLAgent::GetObservationSpaceDescription(F4MLSpaceDescription& OutSpaceDesc) const
{
	F4MLDescription ElementDesc;

	for (U4MLSensor* Sensor : Sensors)
	{
		if (Sensor)
		{
			ElementDesc.Reset();
			ElementDesc.Add(Sensor->GetSpaceDef());
			OutSpaceDesc.Add(Sensor->GetNickname(), ElementDesc);
		}
	}
}

bool U4MLAgent::IsSuitableAvatar(AActor& InAvatar) const
{	
	return AgentConfig.bAvatarClassExact 
		? InAvatar.GetClass() == AgentConfig.AvatarClass.Get()
		: InAvatar.IsA(AgentConfig.AvatarClass);
}

void U4MLAgent::SetAvatar(AActor* InAvatar)
{
	if (InAvatar == Avatar)
	{
		// do nothing on purpose
		return;
	}

	if (InAvatar != nullptr && IsSuitableAvatar(*InAvatar) == false)
	{
		UE_LOG(LogUE4ML, Log, TEXT("SetAvatar was called for agent %u but %s is not a valid avatar (required avatar class %s)")
			, AgentID, *InAvatar->GetName(), *GetNameSafe(AgentConfig.AvatarClass));
		return;
	}

	AActor* PrevAvatar = Avatar;
	AController* PrevController = Controller;
	APawn* PrevPawn = Pawn;
	if (Avatar)
	{
		Avatar->OnDestroyed.RemoveAll(this);
		Avatar = nullptr;
	}

	if (InAvatar == nullptr)
	{
		Controller = nullptr;
		Pawn = nullptr;
	}
	else
	{
		bEverHadAvatar = true;
		Avatar = InAvatar;

		Pawn = nullptr;
		Controller = nullptr;
		F4MLAgentHelpers::GetAsPawnAndController(Avatar, Controller, Pawn);

		if (Avatar)
		{
			Avatar->OnDestroyed.AddDynamic(this, &U4MLAgent::OnAvatarDestroyed);
		}
	}

	// actuators and sensors might need the information that the avatar changed
	for (U4MLSensor* Sensor : Sensors)
	{
		Sensor->OnAvatarSet(Avatar);
	}
	for (U4MLActuator* Actuator : Actuators)
	{
		Actuator->OnAvatarSet(Avatar);
	}

	// unregister from unused notifies

	if (Controller != PrevController)
	{
		if (PrevController && PrevController == PrevAvatar)
		{
			PrevController->GetOnNewPawnNotifier().RemoveAll(this);
		}
		// when the controller is the main avatar
		if (Controller != nullptr && (Avatar == Controller))
		{
			Controller->GetOnNewPawnNotifier().AddUObject(this, &U4MLAgent::OnPawnChanged, Controller);
		}
	}

	if ((Controller != nullptr || Pawn != nullptr) && bRegisteredForPawnControllerChange == false)
	{
		UGameInstance* GameInstance = GetSession().GetGameInstance();
		if (GameInstance)
		{
			GameInstance->GetOnPawnControllerChanged().AddDynamic(this, &U4MLAgent::OnPawnControllerChanged);
			bRegisteredForPawnControllerChange = true;
		}
	}
}

float U4MLAgent::GetReward() const
{
	if (Avatar == nullptr)
	{
		return 0.f;
	}
	FGCScopeGuard GCScopeGuard;
	AController* AsController = F4ML::ActorToController(*Avatar);
	return AsController && AsController->PlayerState ? AsController->PlayerState->GetScore() : 0.f;
}

bool U4MLAgent::IsDone() const
{
	return AgentConfig.bAutoRequestNewAvatarUponClearingPrev == false 
		&& Avatar == nullptr
		&& bEverHadAvatar == true;
}

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategory.h"

void U4MLAgent::DescribeSelfToGameplayDebugger(FGameplayDebuggerCategory& DebuggerCategory) const
{
	DebuggerCategory.AddTextLine(FString::Printf(TEXT("{green}ID {white}%u\n{green}Avatar {white}%s")
		, AgentID, *GetNameSafe(Avatar)));

	DebuggerCategory.AddTextLine(TEXT("{green}Sensors:"));
	for (U4MLSensor* Sensor : Sensors)
	{
		if (Sensor)
		{
			Sensor->DescribeSelfToGameplayDebugger(DebuggerCategory);
		}
	}

	DebuggerCategory.AddTextLine(TEXT("{green}Actuators:"));
	for (U4MLActuator* Actuator : Actuators)
	{
		if (Actuator)
		{
			Actuator->DescribeSelfToGameplayDebugger(DebuggerCategory);
		}
	}
}
#endif // WITH_GAMEPLAY_DEBUGGER