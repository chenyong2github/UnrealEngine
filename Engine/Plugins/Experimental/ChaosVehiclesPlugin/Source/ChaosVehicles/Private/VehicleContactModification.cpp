// Copyright Epic Games, Inc. All Rights Reserved.

#include "VehicleContactModification.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/EngineTypes.h"
#include "Physics/PhysicsFiltering.h"
#include "Chaos/ParticleHandleFwd.h"
#include "ChaosVehicleMovementComponent.h"
#include "WheeledVehiclePawn.h"
#include "DrawDebugHelpers.h"

#if WITH_CHAOS
#include "Chaos/ParticleHandle.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"
#endif

const bool ContactModificationEnabled = true;
const bool DrawDebugLinesEnabled = true;

#if WITH_CHAOS

// #todo: issue with Clang compiler linking - just disable for now
//  Module.ChaosVehicles.cpp.obj : error LNK2019: unresolved external symbol "public: class Chaos::FPerShapeData const * __cdecl Chaos::TGeometryParticlesImp<float,3,0>::GetImplicitShape(int,class Chaos::FImplicitObject const *)"
#define CONTACT_MOD_ENABLE 0

Chaos::FCollisionModifierCallback FVehicleContactModificationFactory::Create()
{
	
	using EConstraintType = Chaos::FCollisionConstraintBase::FType;

	return [](Chaos::FPBDCollisionConstraintHandle* ConstraintHandle)
	{

#if CONTACT_MOD_ENABLE

		if (ContactModificationEnabled == 0)
		{
			return Chaos::ECollisionModifierResult::Unchanged;
		}

		Chaos::FCollisionConstraintBase* ConstraintPtr = nullptr;
		Chaos::FRigidBodyMultiPointContactConstraint* MultiPointContactConstraint = nullptr;

		if (ConstraintHandle->GetType() == EConstraintType::MultiPoint)
		{
			MultiPointContactConstraint = &ConstraintHandle->GetMultiPointContact();
			ConstraintPtr = MultiPointContactConstraint;
		}
		else if (ConstraintHandle->GetType() == EConstraintType::SinglePoint)
		{
			ConstraintPtr = &ConstraintHandle->GetPointContact();
		}
		else if (ConstraintHandle->GetType() == EConstraintType::SinglePointSwept)
		{
			ConstraintPtr = &ConstraintHandle->GetSweptPointContact();
		}
		else
		{
			return Chaos::ECollisionModifierResult::Unchanged;
		}

		Chaos::FCollisionConstraintBase& Constraint = *ConstraintPtr;

		// Find out what channels the objects are in
		const Chaos::FCollisionContact& Manifold = Constraint.GetManifold();
		Chaos::TGeometryParticleHandle<float, 3>* Particle0 = Constraint.Particle[0];
		Chaos::TGeometryParticleHandle<float, 3>* Particle1 = Constraint.Particle[1];
		const Chaos::FPerShapeData* Shape0 = Particle0->GetImplicitShape(Manifold.Implicit[0]);
		const Chaos::FPerShapeData* Shape1 = Particle1->GetImplicitShape(Manifold.Implicit[1]);
		if (!CHAOS_ENSURE(Shape0 != nullptr && Shape1 != nullptr))
		{
			return Chaos::ECollisionModifierResult::Unchanged;
		}
		ECollisionChannel Channel0 = GetCollisionChannel(Shape0->GetSimData().Word3);
		ECollisionChannel Channel1 = GetCollisionChannel(Shape1->GetSimData().Word3);

		if (Channel0 == Channel1)
		{
			// We only care about vehicles driving over static mesh. Don't bother correcting vehicle vehicle collision (Note this all assumes that only vehicles opt in for contact modification)
			return Chaos::ECollisionModifierResult::Unchanged;
		}

		// Make sure at least one of these is in the vehicle collision channel
		if (!CHAOS_ENSURE(Channel0 == ECC_Vehicle || Channel1 == ECC_Vehicle))
		{
			return Chaos::ECollisionModifierResult::Unchanged;
		}

		//If all contacts are below ledge threshold we want to just drive over it
		if (const Chaos::TGeometryParticleHandle<float, 3> * VehicleActor = Channel0 == ECC_Vehicle ? Particle0 : Particle1)
		{
			// NOTE: Accessing UObjects is very dangerous and is only safe because we are certain that GC cannot take place at the moment.
			//       Data accessed is specifically not touched while Physics is running, and the only safe data is a POD struct. Be careful.
			// NOTE: We rely on the above guarantee still being true for Chaos - that is, that GC happens after TG_DuringPhysics.

			if (FBodyInstance* BI = FPhysicsUserData::Get<FBodyInstance>(VehicleActor->UserData()))
			{
				if (UPrimitiveComponent* PrimComp = BI->OwnerComponent.Get())
				{
					if (AWheeledVehiclePawn* Actor = Cast<AWheeledVehiclePawn>(PrimComp->GetOwner()))
					{
						if (UChaosVehicleMovementComponent* Vehicle = Cast<UChaosVehicleMovementComponent>(Actor->GetComponentByClass(UChaosVehicleMovementComponent::StaticClass())))
						{
							const FSolverSafeContactData& ContactData = Vehicle->GetSolverSafeContactData();

							const Chaos::TPBDRigidParticleHandle<float, 3>* RigidVehicleActor = VehicleActor->CastToRigidParticle();
							if (CHAOS_ENSURE(RigidVehicleActor))
							{
								Chaos::FRigidTransform3 VehicleTM = Chaos::FParticleUtilitiesPQ::GetCoMWorldTransform(RigidVehicleActor);
								const Chaos::FVec3 VehicleUp = VehicleTM.GetUnitAxis(EAxis::Z);
								const auto ShapeToActor = Channel0 == ECC_Vehicle ? Constraint.ImplicitTransform[0] : Constraint.ImplicitTransform[1];
								const auto ShapeToWorld = ShapeToActor * VehicleTM;

								bool bChanges = false;

								// Number of points, use just one for single point contacts
								const int32 NumPoints = MultiPointContactConstraint ? MultiPointContactConstraint->NumManifoldPoints() : 1;

								for (int32 ContactIdx = 0; ContactIdx < NumPoints; ++ContactIdx)
								{
									Chaos::FVec3 WorldContactPt;

									if (MultiPointContactConstraint)
									{
										WorldContactPt = MultiPointContactConstraint->GetManifoldPoint(ContactIdx);
									}
									else
									{
										WorldContactPt = Constraint.GetLocation();
									}

									const auto LocalPtOnVehicle = ShapeToWorld.InverseTransformPosition(WorldContactPt);

									bool bFloorFrictionApplied = false;

									if (LocalPtOnVehicle.Z <= ContactData.ContactModificationOffset)
									{
										Constraint.Manifold.Friction = ContactData.VehicleFloorFriction;
										bFloorFrictionApplied = true;
										bChanges = true;

										FVector OldManifoldNormal = Manifold.Normal;

										///////////////////////////////////////////////////////////////////////////
										// Travelling at speed, alter terrain normal so it's not killing speed, only
										// preventing the vehicle chassis from pressing into the ground
										const Chaos::FVec3 VehicleV = RigidVehicleActor->V();
										if (VehicleV.SizeSquared() > 1.0f)
										{
											FVector VDirectionNormal = VehicleV.GetSafeNormal();
											float KillAmount = FVector::DotProduct(Manifold.Normal, VDirectionNormal);

											Constraint.Manifold.Friction = 0.0f; // slips easily
											Constraint.Manifold.Normal = FVector(0.f, 1.f, 0.f);
											bChanges = true;

											if (false && KillAmount < 0.0f)
											{
												FVector KillVector = VDirectionNormal * KillAmount;

												Constraint.Manifold.Normal -= KillVector;
												Constraint.Manifold.Normal.SafeNormalize();
												

												if (DrawDebugLinesEnabled)
												{
													FVector Start = WorldContactPt;
													FVector End = WorldContactPt + OldManifoldNormal * 100;
													DrawDebugLine(Vehicle->GetWorld(), Start, End, FColor::Red, true, 2.0f, 0, 2.f);

													FVector NewEnd = WorldContactPt + Constraint.Manifold.Normal * 100;
													DrawDebugLine(Vehicle->GetWorld(), Start, NewEnd, FColor::Green, true, 2.0f, 0, 2.f);
												}

											}
											
										}
										



										//	if (Manifold.Implicit[0]->GetType() == Chaos::ImplicitObjectType::Convex)	//TODO: this is super hacky
										//{
										//	const float Separation = Manifold.Phi;
										//	const float VehicleMaxPenetration = 5.0f; //???
										//	if (-Separation < VehicleMaxPenetration)
										//	{
										//		const auto UpNormal = ShapeToWorld.GetUnitAxis(EAxis::Z);
										//		const auto OriginalNormal = Manifold.Normal;

										//		if (Chaos::FVec3::DotProduct(OriginalNormal, UpNormal) > 0.f)	//don't try normal correction if it makes you lose the contact
										//		{
										//			Constraint.Manifold.Normal = UpNormal;
										//		}

										//		if (DrawDebugLinesEnabled)
										//		{
										//			FVector Start = WorldContactPt;
										//			FVector End = WorldContactPt + Manifold.Normal * 100;
										//			DrawDebugLine(Vehicle->GetWorld(), Start, End, FColor::Red, true, 2.0f, 0, 2.f);

										//			FVector NewEnd = WorldContactPt + UpNormal * 100;
										//			DrawDebugLine(Vehicle->GetWorld(), Start, NewEnd, FColor::Green, true, 2.0f, 0, 2.f);
										//		}
										//	}
										//}

									}

									//if (!bFloorFrictionApplied && bVehicleIsDriving)
									//{
									//	//want to reduce friction on side of vehicle unless it's sideways on the ground
									//	Constraint.Manifold.Friction = ContactData.VehicleSideScrapeFriction;
									//	bChanges = true;
									//}
								}

								if (bChanges)
								{
									return Chaos::ECollisionModifierResult::Modified;
								}
							} // if (CHAOS_ENSURE(RigidVehicleActor))
						}
					}
				}
			}
		}
#endif
		return Chaos::ECollisionModifierResult::Unchanged;
	};


}


#endif // WITH_CHAOS