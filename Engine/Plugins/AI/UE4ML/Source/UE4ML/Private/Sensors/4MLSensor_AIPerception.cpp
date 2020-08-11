// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sensors/4MLSensor_AIPerception.h"
#include "Agents/4MLAgent.h"
#include "GameFramework/Controller.h"
#include "Perception/AIPerceptionComponent.h"
#include "Debug/DebugHelpers.h"
#include "4MLManager.h"
#include "Perception/AISenseConfig_Sight.h"
#include "4MLSpace.h"

#include "GameFramework/PlayerController.h"

//----------------------------------------------------------------------//
//  U4MLSensor_AIPerception
//----------------------------------------------------------------------//
U4MLSensor_AIPerception::U4MLSensor_AIPerception(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TargetsToSenseCount = 1;
	TargetsSortType = ESortType::Distance;
	PeripheralVisionAngleDegrees = 60.f;
	MaxStimulusAge = 0.6f;
}

void U4MLSensor_AIPerception::Configure(const TMap<FName, FString>& Params)
{
	const FName NAME_Count = TEXT("count");
	const FName NAME_Sort = TEXT("sort");
	const FName NAME_Mode = TEXT("mode");
	const FName NAME_PeripheralAngle = TEXT("peripheral_angle");
	const FName NAME_MaxAge = TEXT("max_age");

	Super::Configure(Params);

	for (auto KeyValue : Params)
	{
		if (KeyValue.Key == NAME_Count)
		{
			TargetsToSenseCount = FMath::Max(FCString::Atoi(*KeyValue.Value), 1);
		}
		else if (KeyValue.Key == NAME_Sort)
		{
			TargetsSortType = (KeyValue.Value == TEXT("in_front")) ? ESortType::InFrontness : ESortType::Distance;
		}
		else if (KeyValue.Key == NAME_Mode)
		{
			bVectorMode = (KeyValue.Value.Find(TEXT("vector")) != INDEX_NONE);
		}
		else if (KeyValue.Key == NAME_PeripheralAngle)
		{
			PeripheralVisionAngleDegrees = FMath::Max(FCString::Atof(*KeyValue.Value), 1.f);
		}
		else if (KeyValue.Key == NAME_MaxAge)
		{
			MaxStimulusAge = FMath::Max(FCString::Atof(*KeyValue.Value), 0.001f);
		}
	}

	UpdateSpaceDef();
}

TSharedPtr<F4ML::FSpace> U4MLSensor_AIPerception::ConstructSpaceDef() const
{
	F4ML::FSpace* Result = nullptr;
	// FVector + Distance + ID -> 5
	// FRotator.Yaw + FRotator.Pitch + Distance + ID -> 4
	const uint32 ValuesPerEntry = bVectorMode ? 5 : 4;

	TArray<TSharedPtr<F4ML::FSpace> > Spaces;
	Spaces.AddZeroed(TargetsToSenseCount);
	for (int Index = 0; Index < TargetsToSenseCount; ++Index)
	{
		// enemy heading, enemy distance, enemy ID
		Spaces[Index] = MakeShareable(new F4ML::FSpace_Box({ ValuesPerEntry }));
	}
	Result = new F4ML::FSpace_Tuple(Spaces);
	
	return MakeShareable(Result);
}

void U4MLSensor_AIPerception::UpdateSpaceDef()	
{
	Super::UpdateSpaceDef();

	CachedTargets.Reset(TargetsToSenseCount);
	CachedTargets.AddDefaulted(TargetsToSenseCount);
}

void U4MLSensor_AIPerception::OnAvatarSet(AActor* Avatar)
{
	Super::OnAvatarSet(Avatar);
	
	PerceptionComponent = nullptr;

	AController* Controller = nullptr;
	APawn* Pawn = nullptr;
	if (F4MLAgentHelpers::GetAsPawnAndController(Avatar, Controller, Pawn))
	{
		UWorld* World = Avatar->GetWorld();
		// if at this point the World is null something is seriously wrong
		check(World);
		U4MLManager::Get().EnsureAISystemPresence(*World);

		UAISystem* AISystem = UAISystem::GetCurrent(*World);
		if (ensure(AISystem) && ensure(AISystem->GetPerceptionSystem()))
		{
			UObject* Outer = Controller ? (UObject*)Controller : (UObject*)Pawn;
			PerceptionComponent = NewObject<UAIPerceptionComponent>(Outer);
			check(PerceptionComponent);

			UAISenseConfig_Sight* SightConfig = NewObject<UAISenseConfig_Sight>(this, UAISenseConfig_Sight::StaticClass(), TEXT("UAISenseConfig_Sight"));
			check(SightConfig);
			SightConfig->SightRadius = 50000;
			SightConfig->LoseSightRadius = 53000;
			SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;
			SightConfig->AutoSuccessRangeFromLastSeenLocation = FAISystem::InvalidRange;
			SightConfig->SetMaxAge(MaxStimulusAge);
			PerceptionComponent->ConfigureSense(*SightConfig);
			PerceptionComponent->RegisterComponent();
		}
	}
}

void U4MLSensor_AIPerception::GetViewPoint(AActor& Avatar, FVector& POVLocation, FRotator& POVRotation) const
{
	APlayerController* PC = Cast<APlayerController>(&Avatar);
	if (PC && PC->PlayerCameraManager && false)
	{
		PC->PlayerCameraManager->GetCameraViewPoint(POVLocation, POVRotation);
	}
	else
	{
		Avatar.GetActorEyesViewPoint(POVLocation, POVRotation);
	}
}

void U4MLSensor_AIPerception::SenseImpl(const float DeltaTime)
{
	AActor* Avatar = GetAgent().GetAvatar();
	TArray<FTargetRecord> TmpCachedTargets;
	TmpCachedTargets.Reserve(TargetsToSenseCount);
	if (PerceptionComponent && Avatar)
	{
		TArray<AActor*> KnownActors;
		PerceptionComponent->GetKnownPerceivedActors(UAISense_Sight::StaticClass(), KnownActors);
		FVector POVLocation;
		FRotator POVRotation;
		GetViewPoint(*Avatar, POVLocation, POVRotation);

		for (AActor* Actor : KnownActors)
		{
			if (Actor)
			{
				FTargetRecord& TargetRecord = TmpCachedTargets.AddDefaulted_GetRef();

				const FVector ActorLocation = Actor->GetActorLocation();
				const FRotator ToTarget = (ActorLocation - POVLocation).ToOrientationRotator();
				
				TargetRecord.HeadingRotator = Sanify(ToTarget - POVRotation);
				TargetRecord.HeadingVector = TargetRecord.HeadingRotator.Vector();
				TargetRecord.Distance = FVector::Dist(POVLocation, ActorLocation);
				TargetRecord.ID = Actor->GetUniqueID();
				TargetRecord.HeadingDot = FVector::DotProduct(TargetRecord.HeadingVector, FVector::ForwardVector);
				TargetRecord.Target = Actor;
			}
		}
		
		if (TmpCachedTargets.Num() > 1)
		{
			TmpCachedTargets.SetNum(TargetsToSenseCount, /*bAllowShrinking=*/false);

			switch (TargetsSortType)
			{
			case ESortType::InFrontness:
				TmpCachedTargets.StableSort([](const FTargetRecord& A, const FTargetRecord& B) {
					return A.HeadingDot > B.HeadingDot || B.HeadingDot <= -1.f;
					});
				break;

			case ESortType::Distance:
			default:
				TmpCachedTargets.StableSort([](const FTargetRecord& A, const FTargetRecord& B) {
					// 0 means uninitialized so we send it of the back
					return A.Distance < B.Distance || B.Distance == 0.f; 
					});
				break;
			}
		}
	}

#if WITH_GAMEPLAY_DEBUGGER
	DebugRuntimeString = FString::Printf(TEXT("{white}%s"), (TmpCachedTargets.Num() > 0) ? *FString::Printf(TEXT("see %d"), TmpCachedTargets.Num()) : TEXT(""));
#endif // WITH_GAMEPLAY_DEBUGGER

	// fill up to TargetsToSenseCount with blanks
	for (int Index = TmpCachedTargets.Num(); Index < TargetsToSenseCount; ++Index)
	{
		TmpCachedTargets.Add(FTargetRecord());
	}

	FScopeLock Lock(&ObservationCS);
	CachedTargets = TmpCachedTargets;
}	

void U4MLSensor_AIPerception::GetObservations(F4MLMemoryWriter& Ar)
{
	FScopeLock Lock(&ObservationCS);

	F4ML::FSpaceSerializeGuard SerializeGuard(SpaceDef, Ar);
	check(CachedTargets.Num() <= TargetsToSenseCount);

	for (int Index = 0; Index < TargetsToSenseCount; ++Index)
	{
		FTargetRecord& TargetData = CachedTargets[Index];
		Ar.Serialize(&TargetData.ID, sizeof(TargetData.ID));
		Ar.Serialize(&TargetData.Distance, sizeof(float));
		if (bVectorMode)
		{
			Ar << TargetData.HeadingVector;
		}
		else
		{
			Ar << TargetData.HeadingRotator.Pitch << TargetData.HeadingRotator.Yaw;
		}
	}
}

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategory.h"
void U4MLSensor_AIPerception::DescribeSelfToGameplayDebugger(FGameplayDebuggerCategory& DebuggerCategory) const
{
	int ValidTargets = 0;
	AActor* Avatar = GetAgent().GetAvatar();
	if (Avatar)
	{
		FVector POVLocation;
		FRotator POVRotation;
		GetViewPoint(*Avatar, POVLocation, POVRotation);

		for (int Index = 0; Index < TargetsToSenseCount; ++Index)
		{
			const FTargetRecord& TargetData = CachedTargets[Index];

			if (TargetData.ID == 0)
			{
				break;
			}

			++ValidTargets;
			DebuggerCategory.AddShape(FGameplayDebuggerShape::MakeSegment(POVLocation
				, POVLocation + (POVRotation + TargetData.HeadingRotator).Vector() * TargetData.Distance
				, FColor::Purple));
		}
	}

	Super::DescribeSelfToGameplayDebugger(DebuggerCategory);
}
#endif // WITH_GAMEPLAY_DEBUGGER
