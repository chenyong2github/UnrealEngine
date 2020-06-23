//// Copyright Epic Games, Inc. All Rights Reserved.
//
#pragma once
//
//#include "Components/PrimitiveComponent.h"
//#include "CoreMinimal.h"
//#include "GameFramework/Pawn.h"
//
//#include "WheelSystem.h"
//#include "TransmissionSystem.h"
//
//#include "SimpleCarActor.generated.h"
//
//using namespace Chaos;
//
//UENUM()
//enum class EVehicleTransmissionType : uint8
//{
//	Manual,
//	Automatic
//};
//
//USTRUCT(Blueprintable)
//struct FEngineConfig
//{
//	GENERATED_BODY()
//
//	UPROPERTY(EditAnywhere, Category = Vehicle)
//	UCurveFloat* TorqueCurve;
//
//	UPROPERTY(EditAnywhere, Category = Engine, meta = (UIMin = "0.0", UIMax = "20000.0"))
//	float MaxRPM;
//
//	UPROPERTY(EditAnywhere, Category = Engine, meta = (UIMin = "0.0", UIMax = "1.0"))
//	float EngineBrakingEffect;
//};
//
//USTRUCT(Blueprintable)
//struct FTransConfig
//{
//	GENERATED_BODY()
//
//	UPROPERTY(EditAnywhere, Category = Transmission)
//	TArray<float> ForwardRatios;	// Gear ratios for forward gears
//
//	UPROPERTY(EditAnywhere, Category = Transmission)
//	TArray<float> ReverseRatios;	// Gear ratios for reverse Gear(s)
//	
//	UPROPERTY(EditAnywhere, Category = Transmission)
//	float FinalDriveRatio;			// Final drive ratio [1.0 for arcade vehicles]
//
//	//float ClutchTorque;
//	//float TransmissionLoss;
//
//	UPROPERTY(EditAnywhere, Category = Transmission)
//	uint32 ChangeUpRPM;				// [RPM or % max RPM?]
//	
//	UPROPERTY(EditAnywhere, Category = Transmission)
//	uint32 ChangeDownRPM;			// [RPM or % max RPM?]
//
//	UPROPERTY(EditAnywhere, Category = Transmission)
//	float GearChangeTime; 			// [sec]
//
//	UPROPERTY(EditAnywhere, Category = Transmission)
//	EVehicleTransmissionType TransmissionType;	// Specify Automatic or Manual transmission
//
//	UPROPERTY(EditAnywhere, Category = Transmission)
//	bool AutoReverse;					// Arcade handling - holding Brake switches into reverse after vehicle has stopped
//
//};
//
//
//USTRUCT(Blueprintable)
//struct FWheelConfig
//{
//	GENERATED_BODY()
//
//	UPROPERTY(EditAnywhere, Category = Wheel)
//	FVector Offset;	
//
//	UPROPERTY(EditAnywhere, Category = Wheel)
//	float WheelMass;
//
//	UPROPERTY(EditAnywhere, Category = Wheel)
//	float MaxSteeringAngle;
//
//	UPROPERTY(EditAnywhere, Category = Wheel)
//	float MaxBrakeTorque;
//
//	UPROPERTY(EditAnywhere, Category = Wheel)
//	float MaxHandbrakeTorque;
//
//	UPROPERTY(EditAnywhere, Category = Wheel)
//	bool AbsEnabled;
//
//	UPROPERTY(EditAnywhere, Category = Wheel)
//	bool HandbrakeEnabled;
//
//	UPROPERTY(EditAnywhere, Category = Wheel)
//	bool SteeringEnabled;
//
//	UPROPERTY(EditAnywhere, Category = Wheel)
//	bool EngineEnabled;
//
//	UPROPERTY(EditAnywhere, Category = Wheel)
//	bool SingleWheelOnAxle;
//
//
//};
//
//
//USTRUCT(Blueprintable)
//struct FSuspensionConfig
//{
//	GENERATED_BODY()
//
//	UPROPERTY(EditAnywhere, Category = Suspension)
//	FVector MaxLength;
//
//	UPROPERTY(EditAnywhere, Category = Suspension)
//	FVector MinLength;
//
//	UPROPERTY(EditAnywhere, Category = Suspension)
//	FVector Force;
//
//	UPROPERTY(EditAnywhere, Category = Suspension)
//	FVector CompressionDamping;
//
//	UPROPERTY(EditAnywhere, Category = Suspension)
//	FVector ReboundDamping;
//
//	UPROPERTY(EditAnywhere, Category = Suspension)
//	FVector Swaybar;
//
//	UPROPERTY(EditAnywhere, Category = Suspension)
//	FVector CastorAngle;
//
//	UPROPERTY(EditAnywhere, Category = Suspension)
//	FVector CamberAngle;
//
//	UPROPERTY(EditAnywhere, Category = Suspension)
//	FVector ToeOutAngle;
//
//};
//
//USTRUCT(Blueprintable)
//struct FAxleConfig
//{
//	GENERATED_BODY()
//
//	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vehicle)
//	FWheelConfig Wheel;
//
//	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vehicle)
//	FSuspensionConfig Suspension;
//};
//
//
//
//UCLASS(meta = (BlueprintSpawnableComponent))
//class CHAOSVEHICLES_API ASimpleCarActor : public APawn
//{
//	GENERATED_BODY()
//public:
//	ASimpleCarActor(const FObjectInitializer& ObjectInitializer);
//
//	// Begin AActor interface.
//	virtual void PostInitializeComponents() override;
//	//virtual bool IsLevelBoundsRelevant() const override { return false; }
//
//	/**
//	 *	ticks the actor
//	 *	@param	DeltaTime			The time slice of this tick
//	 *	@param	TickType			The type of tick that is happening
//	 *	@param	ThisTickFunction	The tick function that is firing, useful for getting the completion handle
//	 */
//	virtual void TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;
//	// End AActor interface.
//
//	/**  The main skeletal mesh associated with this Vehicle */
//	UPROPERTY(Category = Vehicle, VisibleDefaultsOnly, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
//	class USkeletalMeshComponent* Mesh;
//
//	/** vehicle simulation component */
//	UPROPERTY(Category = Vehicle, VisibleDefaultsOnly, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
//	class UVehicleMovementComponent* VehicleMovement;
//
//	/** Name of the MeshComponent. Use this name if you want to prevent creation of the component (with ObjectInitializer.DoNotCreateDefaultSubobject). */
//	static FName VehicleMeshComponentName;
//
//	/** Name of the VehicleMovement. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
//	static FName VehicleMovementComponentName;
//
//	/** Util to get the wheeled vehicle movement component */
//	class UVehicleMovementComponent* GetVehicleMovementComponent() const;
//
//	/** Returns Mesh subobject **/
//	class USkeletalMeshComponent* GetMesh() const { return Mesh; }
//	/** Returns VehicleMovement subobject **/
//	class UVehicleMovementComponent* GetVehicleMovement() const { return VehicleMovement; }
//
//
//
//
//	TArray<FSimpleWheelSim> Wheel;
//	//FTransmissionDynamicData Transmission;
//
//	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vehicle)
//	FEngineConfig EngineSetup;
//
//	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vehicle)
//	FTransConfig TransmissionSetup;
//
//	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vehicle)
//	TArray<FAxleConfig> Axle;
//
//	// Temp
//	float MassPerWheel;
//	FVector V;
//
//	FTransform FLTrans;
//	FTransform FRTrans;
//	FTransform BLTrans;
//	FTransform BRTrans;
//
//	TArray<FSimpleWheelConfig> CoreWheel;
//
//
//};