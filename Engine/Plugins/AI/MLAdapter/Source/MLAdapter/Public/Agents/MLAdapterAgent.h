// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "MLAdapterTypes.h"
#include "Actuators/MLAdapterActuator.h"
#include "MLAdapterAgent.generated.h"


class UMLAdapterAction;
class UMLAdapterSensor;
class AController;
class APawn;
class UMLAdapterSession;
class UMLAdapterActuator;
struct FMLAdapterSpaceDescription;


USTRUCT()
struct MLADAPTER_API FMLAdapterParameterMap
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = MLAdapter)
	TMap<FName, FString> Params;
};

USTRUCT()
struct MLADAPTER_API FMLAdapterAgentConfig
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FName, FMLAdapterParameterMap> Sensors;

	UPROPERTY()
	TMap<FName, FMLAdapterParameterMap> Actuators;

	UPROPERTY()
	FName AvatarClassName;

	UPROPERTY()
	FName AgentClassName;

	/** if set to true won't accept child classes of AvatarClass */
	UPROPERTY()
	bool bAvatarClassExact = false;

	UPROPERTY()
	bool bAutoRequestNewAvatarUponClearingPrev = true;

	FMLAdapterParameterMap& AddSensor(const FName SensorName, FMLAdapterParameterMap&& Parameters = FMLAdapterParameterMap());
	FMLAdapterParameterMap& AddActuator(const FName ActuatorName, FMLAdapterParameterMap&& Parameters = FMLAdapterParameterMap());
};

namespace FMLAdapterAgentHelpers
{
	bool MLADAPTER_API GetAsPawnAndController(AActor* Avatar, AController*& OutController, APawn*& OutPawn);
}


UCLASS(Blueprintable, EditInlineNew)
class MLADAPTER_API UMLAdapterAgent : public UObject
{
    GENERATED_BODY()
public:
	UMLAdapterAgent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void PostInitProperties() override;

	virtual void BeginDestroy() override;

	virtual bool RegisterSensor(UMLAdapterSensor& Sensor);
	
	/** Updates all the senses that are configured as 'IsPolling'*/
	virtual void Sense(const float DeltaTime);
	
	// Decide what action to take based on the current observations
	virtual void Think(const float DeltaTime);

	// trigger all of the agent's 
	virtual void Act(const float DeltaTime);
	
	//virtual void DigestActions(const std::vector<float>& ValueStream);
	virtual void DigestActions(FMLAdapterMemoryReader& ValueStream);

	FMLAdapter::FAgentID GetAgentID() const { return AgentID; }
	APawn* GetPawn() { return Pawn; }
	const APawn* GetPawn() const { return Pawn; }
	AController* GetController() { return Controller; }
	const AController* GetController() const { return Controller; }
	
	TArray<UMLAdapterSensor*>::TConstIterator GetSensorsConstIterator() const { return Sensors.CreateConstIterator(); }
	TArray<UMLAdapterActuator*>::TConstIterator GetActuatorsConstIterator() const { return Actuators.CreateConstIterator(); }
	
	virtual float GetReward() const;
	virtual bool IsDone() const;

	UMLAdapterActuator* GetActuator(const uint32 ActuatorID) 
	{ 
		UMLAdapterActuator** FoundActuator = Actuators.FindByPredicate([ActuatorID](const UMLAdapterActuator* Actuator) { return (Actuator->GetElementID() == ActuatorID); });
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

	friend UMLAdapterSession;
	void SetAgentID(FMLAdapter::FAgentID NewAgentID) { AgentID = NewAgentID; }

public:
	void GetObservations(FMLAdapterMemoryWriter& Ar);
	const FMLAdapterAgentConfig& GetConfig() const { return AgentConfig; }
	virtual void Configure(const FMLAdapterAgentConfig& NewConfig);
	virtual void GetActionSpaceDescription(FMLAdapterSpaceDescription& OutSpaceDesc) const;
	virtual void GetObservationSpaceDescription(FMLAdapterSpaceDescription& OutSpaceDesc) const;

	UMLAdapterSession& GetSession();
	
	virtual bool IsSuitableAvatar(AActor& InAvatar) const;
	virtual void SetAvatar(AActor* InAvatar);
	AActor* GetAvatar() const { return Avatar; }

	bool IsReady() const { return Avatar != nullptr; }

protected:
	virtual void ShutDownSensorsAndActuators();

protected:
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = MLAdapter)
	TArray<UMLAdapterSensor*> Sensors;

	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = MLAdapter)
	TArray<UMLAdapterActuator*> Actuators;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = MLAdapter)
	TSubclassOf<AActor> AvatarClass;

private:
	UPROPERTY()
	AActor* Avatar;

	UPROPERTY()
	AController* Controller;

	UPROPERTY()
	APawn* Pawn;

	FMLAdapter::FAgentID AgentID;
	FMLAdapterAgentConfig AgentConfig;

	uint32 bEverHadAvatar : 1;
	uint32 bRegisteredForPawnControllerChange : 1;
};
