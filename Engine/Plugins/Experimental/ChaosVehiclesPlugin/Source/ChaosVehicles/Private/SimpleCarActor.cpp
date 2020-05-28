//// Copyright Epic Games, Inc. All Rights Reserved.
//
#include "SimpleCarActor.h"
//#include "Components/SkeletalMeshComponent.h"
//#include "Engine/CollisionProfile.h"
//#include "WheeledVehicleComponent.h"
//#include "AxleVehicleMovementComponent.h"
//#include "DisplayDebugHelpers.h"
//
//PRAGMA_DISABLE_OPTIMIZATION
//
//FName ASimpleCarActor::VehicleMeshComponentName(TEXT("VehicleMesh"));
//
//ASimpleCarActor::ASimpleCarActor(const FObjectInitializer& ObjectInitializer)
//	: Super(ObjectInitializer)
//{
////	UE_LOG(LogChaos, Log, TEXT("ASimpleCarActor::ASimpleCarActor()"));
//
//	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(VehicleMeshComponentName);
////	Mesh->SetCollisionProfileName(UCollisionProfile::Vehicle_ProfileName);
//	Mesh->BodyInstance.bSimulatePhysics = true; // TEMP
//	Mesh->BodyInstance.bNotifyRigidBodyCollision = true;
//	Mesh->BodyInstance.bUseCCD = true;
//	Mesh->bBlendPhysics = true;
//	Mesh->SetGenerateOverlapEvents(true);
//	Mesh->SetCanEverAffectNavigation(false);
//	RootComponent = Mesh;
//
//	// Base classes:
//	PrimaryActorTick.bCanEverTick = true;
//	PrimaryActorTick.bAllowTickOnDedicatedServer = false;
//
//	//FieldSystemComponent = CreateDefaultSubobject<UFieldSystemComponent>(TEXT("FieldSystemComponent"));
//	//RootComponent = FieldSystemComponent;
//	const float Gravity = 9.8f;
//	float MaxSimTime = 15.0f;
//	float DeltaTime = 1.f / 30.f;
//	float SimulatedTime = 0.f;
//	float VehicleSpeedMPH = 30.f;
//	float VehicleMass = 1300.f;
//	MassPerWheel = 1300.f / 4.f;
//
//	CoreWheel.Add(FSimpleWheelConfig());
//	Wheel.Add(&CoreWheel[CoreWheel.Num()-1]);
//	//	Wheel[0].Initialize();
//
//	Wheel[0].SetWheelLoadForce(MassPerWheel * Gravity);
//
//	// Road speed
//	V.X = MPHToMS(VehicleSpeedMPH);
//
//	// wheel rolling speed matches road speed
//	Wheel[0].SetMatchingSpeed(V.X);
//
////	auto& Mesh = GetMesh();
//
//
//	//if (USkinnedMeshComponent* SkinnedMesh = GetMesh())
//	//{
//	//	if (const FBodyInstance* WheelBI = SkinnedMesh->GetBodyInstance(WheelSetup.BoneName))
//	//	{
//	//		int fred = 0; 
//	//		fred++;
//	//	}
//	//}
//}
//
//
//void ASimpleCarActor::PostInitializeComponents()
//{
//	Super::PostInitializeComponents();
//
//	int FLIndex = Mesh->GetBoneIndex("F_L_wheelJNT");
//	int FRIndex = Mesh->GetBoneIndex("F_R_wheelJNT");
//	int BLIndex = Mesh->GetBoneIndex("B_L_wheelJNT");
//	int BRIndex = Mesh->GetBoneIndex("B_R_wheelJNT");
//
//	FVector FLOffset = Mesh->GetBoneLocation("F_L_wheelJNT", EBoneSpaces::ComponentSpace); // local
//	FVector FROffset = Mesh->GetBoneLocation("F_R_wheelJNT", EBoneSpaces::ComponentSpace);
//	FVector BLOffset = Mesh->GetBoneLocation("B_L_wheelJNT", EBoneSpaces::ComponentSpace);
//	FVector BROffset = Mesh->GetBoneLocation("B_R_wheelJNT", EBoneSpaces::ComponentSpace);
//
//	FLTrans = Mesh->GetBoneTransform(FLIndex);
//	FRTrans = Mesh->GetBoneTransform(FRIndex);
//	BLTrans = Mesh->GetBoneTransform(BLIndex);
//	BRTrans = Mesh->GetBoneTransform(BRIndex);
//	//FVector FLOffset2 = Mesh->GetBoneLocation("F_L_wheelJNT", EBoneSpaces::WorldSpace); // world
//	//FVector FROffset2 = Mesh->GetBoneLocation("F_R_wheelJNT", EBoneSpaces::WorldSpace);
//	//FVector BLOffset2 = Mesh->GetBoneLocation("B_L_wheelJNT", EBoneSpaces::WorldSpace);
//	//FVector BROffset2 = Mesh->GetBoneLocation("B_R_wheelJNT", EBoneSpaces::WorldSpace);
//
//	//auto* Root = GetRootComponent();
//	//auto Matrix = Mesh->GetBoneMatrix(0);
//	//int Index = Mesh->GetBoneIndex("F_R_wheelJNT");
//	//const FBodyInstance* WheelBIT = Mesh->GetBodyInstance("F_R_wheelJNT");
//
//	//TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
//	//GetComponents<USkeletalMeshComponent>(SkeletalMeshComponents);
//
//	//for (USkeletalMeshComponent* SKMeshCmp : SkeletalMeshComponents)
//	//{
//	//	TArray<FName> Names = SKMeshCmp->GetAllSocketNames();
//	//	int fred = 0;
//	//	fred++;
//
//	//	//const FVector BonePosition = SKMeshCmp->GetBoneIndex("F_R_wheelJNT");// .GetOrigin()* Mesh->GetRelativeScale3D();
//	//	int BI = SKMeshCmp->GetBoneIndex("F_R_wheelJNT");
//
//	//	if (const FBodyInstance* WheelBI = SKMeshCmp->GetBodyInstance("F_R_wheelJNT"))
//	//	{
//	//		fred++;
//	//	}
//	//}
//}
//
////bool ASimpleCarActor::IsLevelBoundsRelevant() const 
////{ 
////	return false; 
////}
//
///**
// *	ticks the actor
// *	@param	DeltaTime			The time slice of this tick
// *	@param	TickType			The type of tick that is happening
// *	@param	ThisTickFunction	The tick function that is firing, useful for getting the completion handle
// */
//void ASimpleCarActor::TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction)
//{
//	Super::TickActor(DeltaTime, TickType, ThisTickFunction);
///*
//	float DT = FMath::Min(DeltaTime, (1.f / 30.f));
//
//	//FTransform MyTransform = GetWorldTransform();
//	FTransform Transform = this->GetActorTransform();
//	FVector P = this->GetActorLocation() * 0.01f;
//
//	//FVector& P = MyTransform.GetLocation();
//	Wheel[0].SetBrakeTorque(650);
//
//	// rolling speed matches road speed
//	Wheel[0].SetRoadSpeed(V);
//
//	Wheel[0].Simulate(DT);
//
//	// deceleration from brake
//	V.X += DT * Wheel[0].GetForceFromFriction() / MassPerWheel;
//	if (V.X < 0.f)
//	{
//		V.X = 0.f;
//	}
//
//	FMatrix Mat = FLTrans.ToMatrixNoScale();
//
//	Mat.SetAxis(0, FVector(FMath::Cos(Wheel[0].AngularPosition), 0, -FMath::Sin(Wheel[0].AngularPosition)));
//	Mat.SetAxis(2, FVector(FMath::Sin(Wheel[0].AngularPosition), 0, FMath::Cos(Wheel[0].AngularPosition)));
//
//	FLTrans.SetFromMatrix(Mat);
//	FRTrans.SetFromMatrix(Mat);
//
//	this->SetActorTransform(Transform);
//	P.X += V.X * DT;
//	SetActorLocation(P*100.0f);
//
//	*/
//}
//
//
//PRAGMA_ENABLE_OPTIMIZATION
