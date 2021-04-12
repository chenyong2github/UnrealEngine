// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sensors/MLAdapterSensor_Movement.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Agents/MLAdapterAgent.h"


UMLAdapterSensor_Movement::UMLAdapterSensor_Movement(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TickPolicy = EMLAdapterTickPolicy::EveryTick;

	bAbsoluteLocation = true;
	bAbsoluteVelocity = true;
}

bool UMLAdapterSensor_Movement::ConfigureForAgent(UMLAdapterAgent& Agent)
{
	return false;
}

void UMLAdapterSensor_Movement::Configure(const TMap<FName, FString>& Params)
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

void UMLAdapterSensor_Movement::SenseImpl(const float DeltaTime)
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

void UMLAdapterSensor_Movement::OnAvatarSet(AActor* Avatar)
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

void UMLAdapterSensor_Movement::GetObservations(FMLAdapterMemoryWriter& Ar)
{
	FScopeLock Lock(&ObservationCS);
	FVector Location = bAbsoluteLocation ? CurrentLocation : (CurrentLocation - RefLocation);
	FVector Velocity = bAbsoluteVelocity ? CurrentVelocity : (CurrentVelocity - RefVelocity);
	
	FMLAdapter::FSpaceSerializeGuard SerializeGuard(SpaceDef, Ar);	
	Ar << Location << Velocity;
	
	CurrentLocation = RefLocation;
	CurrentVelocity = RefVelocity;
}

TSharedPtr<FMLAdapter::FSpace> UMLAdapterSensor_Movement::ConstructSpaceDef() const
{
	// Location + Velocity
	return MakeShareable(new FMLAdapter::FSpace_Box({ 6 }));
}
