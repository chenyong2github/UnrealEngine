// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sensors/4MLSensor_Movement.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Agents/4MLAgent.h"


U4MLSensor_Movement::U4MLSensor_Movement(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TickPolicy = E4MLTickPolicy::EveryTick;

	bAbsoluteLocation = true;
	bAbsoluteVelocity = true;
}

bool U4MLSensor_Movement::ConfigureForAgent(U4MLAgent& Agent)
{
	return false;
}

void U4MLSensor_Movement::Configure(const TMap<FName, FString>& Params)
{
	const FName NAME_Location = TEXT("location");
	const FName NAME_Velocity = TEXT("velocity");
	
	Super::Configure(Params);

	for (auto KeyValue : Params)
	{
		if (KeyValue.Key == NAME_Location)
		{
			bAbsoluteLocation = (KeyValue.Value.Find(TEXT("absolute")) != INDEX_NONE);
		}
		else if (KeyValue.Key == NAME_Velocity)
		{
			bAbsoluteLocation = (KeyValue.Value.Find(TEXT("absolute")) != INDEX_NONE);
		}
	}

	UpdateSpaceDef();
}

void U4MLSensor_Movement::SenseImpl(const float DeltaTime)
{
	AActor* Avatar = GetAgent().GetAvatar();

	if (Avatar == nullptr)
	{
		return;
	}
	
	AController* Controller = Cast<AController>(Avatar);
	CurrentLocation = (Controller && Controller->GetPawn()) ? Controller->GetPawn()->GetActorLocation() : Avatar->GetActorLocation();
	CurrentVelocity = (Controller && Controller->GetPawn()) ? Controller->GetPawn()->GetVelocity() : Avatar->GetVelocity();
}

void U4MLSensor_Movement::OnAvatarSet(AActor* Avatar)
{
	Super::OnAvatarSet(Avatar);

	if (Avatar)
	{
		CurrentLocation = Avatar->GetActorLocation();
		CurrentVelocity = Avatar->GetVelocity();
	}
	else
	{
		CurrentLocation = CurrentVelocity = FVector::ZeroVector;
	}
}

void U4MLSensor_Movement::GetObservations(F4MLMemoryWriter& Ar)
{
	FScopeLock Lock(&ObservationCS);
	FVector Location = bAbsoluteLocation ? CurrentLocation : (CurrentLocation - RefLocation);
	FVector Velocity = bAbsoluteVelocity ? CurrentVelocity : (CurrentVelocity - RefVelocity);
	
	F4ML::FSpaceSerializeGuard SerializeGuard(SpaceDef, Ar);	
	Ar << Location << Velocity;
	
	CurrentLocation = RefLocation;
	CurrentVelocity = RefVelocity;
}

TSharedPtr<F4ML::FSpace> U4MLSensor_Movement::ConstructSpaceDef() const
{
	// Location + Velocity
	return MakeShareable(new F4ML::FSpace_Box({ 6 }));
}
