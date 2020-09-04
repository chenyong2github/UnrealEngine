// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "4MLTypes.h"
#include "Actuators/4MLActuator.h"
#include "4MLAgent.generated.h"


class U4MLAction;
class U4MLSensor;
class AController;
class APawn;
class U4MLBrain;
class U4MLSession;
class U4MLActuator;
struct F4MLSpaceDescription;


USTRUCT()
struct F4MLParameterMap
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FName, FString> Params;
};

USTRUCT()
struct F4MLAgentConfig
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FName, F4MLParameterMap> Sensors;

	UPROPERTY()
	TMap<FName, F4MLParameterMap> Actuators;

	UPROPERTY()
	FName AvatarClassName;

	UPROPERTY()
	FName AgentClassName;

	/** if set to true won't accept child classes of AvatarClass */
	UPROPERTY()
	bool bAvatarClassExact = false;

	UPROPERTY()
	bool bAutoRequestNewAvatarUponClearingPrev = true;

	TSubclassOf<AActor> AvatarClass;

	F4MLParameterMap& AddSensor(const FName SensorName, F4MLParameterMap&& Parameters = F4MLParameterMap());
	F4MLParameterMap& AddActuator(const FName ActuatorName, F4MLParameterMap&& Parameters = F4MLParameterMap());
};

namespace F4MLAgentHelpers
{
	bool UE4ML_API GetAsPawnAndController(AActor* Avatar, AController*& OutController, APawn*& OutPawn);
}


UCLASS(Blueprintable, EditInlineNew)
class UE4ML_API U4MLAgent : public UObject
{
    GENERATED_BODY()
public:
	U4MLAgent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginDestroy() override;

	virtual bool RegisterSensor(U4MLSensor& Sensor);
	/** Updates all the senses that are configured as 'IsPolling'*/
	virtual void Sense(const float DeltaTime);
	
	// trigger all of the agent's 
	virtual void Act(const float DeltaTime);
	
	//virtual void DigestActions(const std::vector<float>& ValueStream);
	virtual void DigestActions(F4MLMemoryReader& ValueStream);

	F4ML::FAgentID GetAgentID() const { return AgentID; }
	APawn* GetPawn() { return Pawn; }
	const APawn* GetPawn() const { return Pawn; }
	AController* GetController() { return Controller; }
	const AController* GetController() const { return Controller; }
	
	TArray<U4MLSensor*>::TConstIterator GetSensorsConstIterator() const { return Sensors.CreateConstIterator(); }
	TArray<U4MLActuator*>::TConstIterator GetActuatorsConstIterator() const { return Actuators.CreateConstIterator(); }
	
	virtual float GetReward() const;
	virtual bool IsDone() const;

	U4MLActuator* GetActuator(const uint32 ActuatorID) 
	{ 
		U4MLActuator** FoundActuator = Actuators.FindByPredicate([ActuatorID](const U4MLActuator* Actuator) { return (Actuator->GetElementID() == ActuatorID); });
		return FoundActuator ? *FoundActuator : nullptr;
	}

#if WITH_GAMEPLAY_DEBUGGER
	void DescribeSelfToGameplayDebugger(class FGameplayDebuggerCategory& DebuggerCategory) const;
#endif // WITH_GAMEPLAY_DEBUGGER

protected:
	UFUNCTION()
	virtual void OnAvatarDestroyed(AActor* DestroyedActor);
	
	/** will be bound to UGameInstance.OnPawnControllerChanged if current avatar is a pawn or a controller */
	UFUNCTION()
	void OnPawnControllerChanged(APawn* InPawn, AController* InController);

	virtual void OnPawnChanged(APawn* NewPawn, AController* InController);

	friend U4MLSession;
	void SetAgentID(F4ML::FAgentID NewAgentID) { AgentID = NewAgentID; }

public:
	void GetObservations(F4MLMemoryWriter& Ar);
	const F4MLAgentConfig& GetConfig() const { return AgentConfig; }
	virtual void Configure(const F4MLAgentConfig& NewConfig);
	virtual void GetActionSpaceDescription(F4MLSpaceDescription& OutSpaceDesc) const;
	virtual void GetObservationSpaceDescription(F4MLSpaceDescription& OutSpaceDesc) const;

	U4MLSession& GetSession();
	
	virtual bool IsSuitableAvatar(AActor& InAvatar) const;
	virtual void SetAvatar(AActor* InAvatar);
	AActor* GetAvatar() const { return Avatar; }

	bool IsReady() const { return Avatar != nullptr; }

protected:
	virtual void ShutDownSensorsAndActuators();


protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = UE4ML)
	TArray<U4MLSensor*> Sensors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = UE4ML)
	TArray<U4MLActuator*> Actuators;

private:
	UPROPERTY()
	AActor* Avatar;

	UPROPERTY()
	AController* Controller;

	UPROPERTY()
	APawn* Pawn;

	F4ML::FAgentID AgentID;
	F4MLAgentConfig AgentConfig;

	uint32 bEverHadAvatar : 1;
	uint32 bRegisteredForPawnControllerChange : 1;
};
