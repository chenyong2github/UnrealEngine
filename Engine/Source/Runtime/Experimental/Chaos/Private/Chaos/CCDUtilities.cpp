// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/CCDUtilities.h"

#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/CollisionResolution.h"

bool bChaosCollisionCCDEnableResweep = true;
FAutoConsoleVariableRef CVarChaosCollisionCCDEnableResweep(TEXT("p.Chaos.Collision.CCD.EnableResweep"), bChaosCollisionCCDEnableResweep, TEXT("Enable resweep for CCD. Resweeping allows CCD to catch more secondary collisions but also is more costly. Default is true."));

bool bChaosCollisionCCDAllowClipping = true;
FAutoConsoleVariableRef CVarChaosCollisionCCDAllowClipping(TEXT("p.Chaos.Collision.CCD.AllowClipping"), bChaosCollisionCCDAllowClipping, TEXT("This will clip the CCD object at colliding positions when computation budgets run out. Default is true. Turning this option off might cause tunneling."));

int32 ChaosCollisionCCDConstraintMaxProcessCount = 1;
FAutoConsoleVariableRef CVarChaosCollisionCCDConstraintMaxProcessCount(TEXT("p.Chaos.Collision.CCD.ConstraintMaxProcessCount"), ChaosCollisionCCDConstraintMaxProcessCount, TEXT("The max number of times each constraint can be resolved when applying CCD constraints. Default is 2. The larger this number is, the more fully CCD constraints are resolved."));


namespace Chaos
{
	void FCCDParticle::AddOverlappingDynamicParticle(FCCDParticle* const InParticle)
	{
		OverlappingDynamicParticles.Add(InParticle);
	}

	void FCCDParticle::AddConstraint(FCCDConstraint* const Constraint)
	{
		AttachedCCDConstraints.Add(Constraint);
	}

	FReal GetParticleCCDThreshold(const FImplicitObject* Implicit)
	{
		if (Implicit)
		{
			// Trimesh/Heightfield are thin, cannot use bounds. We do not want them to contribute to CCD threshold.
			if (Implicit->IsConvex())
			{
				const FReal MinExtent = Implicit->BoundingBox().Extents().Min();
				return MinExtent * CVars::CCDEnableThresholdBoundsScale;
			}

			return 0;
		}
		return TNumericLimits<FReal>::Max();
	}

	int32 FCCDConstraint::GetFastMovingKinematicIndex(const FPBDCollisionConstraint* Constraint, const FVec3 Displacements[]) const
	{
		for (int32 i = 0; i < 2; i++)
		{
			const TPBDRigidParticleHandle<FReal, 3>* Rigid = Constraint->GetParticle(i)->CastToRigidParticle();
			if (Rigid && Rigid->ObjectState() == EObjectStateType::Kinematic)
			{
				// The same computation is carried out in UseCCDImpl function when constructing constraints. But we don't have access to FCCDConstraint at that point. This part could potentially be optimized away. 
				const FVec3 D = Displacements[i];
				const FReal DSizeSquared = D.SizeSquared();
				const FReal CCDThreshold = GetParticleCCDThreshold(Constraint->GetImplicit(i));
				if (DSizeSquared > CCDThreshold * CCDThreshold)
				{
					return i;
				}
			}
		}
		return INDEX_NONE;
	}

	void FCCDManager::ApplyConstraintsPhaseCCD(const FReal Dt, FCollisionConstraintAllocator *CollisionAllocator, const int32 NumDynamicParticles)
	{
		SweptConstraints = CollisionAllocator->GetSweptConstraints();
		if (SweptConstraints.Num() > 0)
		{
			ApplySweptConstraints(Dt, SweptConstraints, NumDynamicParticles);
			UpdateSweptConstraints(Dt, CollisionAllocator);
			OverwriteXUsingV(Dt);
		}
	}

	void FCCDManager::ApplySweptConstraints(const FReal Dt, TArrayView<FPBDCollisionConstraint* const> InSweptConstraints, const int32 NumDynamicParticles)
	{
		const bool bNeedCCDSolve = Init(Dt, NumDynamicParticles);
		if (!bNeedCCDSolve)
		{
			return;
		}

		AssignParticleIslandsAndGroupParticles();
		AssignConstraintIslandsAndRecordConstraintNum();
		GroupConstraintsWithIslands();
		PhysicsParallelFor(IslandNum, [&](const int32 Island)
		{
			ApplyIslandSweptConstraints(Island, Dt);
		});
	}

	bool FCCDManager::Init(const FReal Dt, const int32 NumDynamicParticles)
	{
		CCDParticles.Reset();
		// We store pointers to CCDParticle in CCDConstraint and GroupedCCDParticles, therefore we need to make sure to reserve enough space for TArray CCDParticles so that reallocation does not happen in the for loop. If reallocation happens, some pointers may become invalid and this could cause serious bugs. We know that the number of CCDParticles cannot exceed SweptConstraints.Num() * 2 or NumDynamicParticles. Therefore this makes sure reallocation does not happen.
		CCDParticles.Reserve(FMath::Min(SweptConstraints.Num() * 2, NumDynamicParticles));
		ParticleToCCDParticle.Reset();
		CCDConstraints.Reset();
		CCDConstraints.Reserve(SweptConstraints.Num());
		bool bNeedCCDSolve = false;
		for (FPBDCollisionConstraint* Constraint : SweptConstraints)
		{   
			// Create CCDParticle for all dynamic particles affected by swept constraints (UseCCD() could be either true or false). For static or kinematic particles, this pointer remains to be nullptr.
			FCCDParticle* CCDParticlePair[2] = {nullptr, nullptr};
			bool IsDynamic[2] = {false, false};
			FVec3 Displacements[2] = {FVec3(0.f), FVec3(0.f)};
			for (int i = 0; i < 2; i++)
			{
				TPBDRigidParticleHandle<FReal, 3>* RigidParticle = Constraint->GetParticle(i)->CastToRigidParticle();
				FCCDParticle* CCDParticle = nullptr;
				const bool IsParticleDynamic = RigidParticle && RigidParticle->ObjectState() == EObjectStateType::Dynamic;
				if (IsParticleDynamic)
				{
					FCCDParticle** FoundCCDParticle = ParticleToCCDParticle.Find(RigidParticle);
					if (!FoundCCDParticle)
					{
						CCDParticles.Add(FCCDParticle(RigidParticle));
						CCDParticle = &CCDParticles.Last();
						ParticleToCCDParticle.Add(RigidParticle, CCDParticle);
					}
					else
					{
						CCDParticle = *FoundCCDParticle;
					}
					IsDynamic[i] = IsParticleDynamic;
				}
				CCDParticlePair[i] = CCDParticle; 
				IsDynamic[i] = IsParticleDynamic;

				if (RigidParticle)
				{
					// One can also use P - X for dynamic particles. But notice that for kinematic particles, both P and X are end-frame positions and P - X won't work for kinematic particles.
					Displacements[i] = RigidParticle->V() * Dt;
				}
			}

			// Compute the relative displacement. If relative displacement is smaller than 0.5 * (Extents0.Min() + Extents1.Min()), it is impossible for an particle to tunnel through another particle, even though the absolute velocities of the particles might be large.
			const FReal CCDThreshold0 = GetParticleCCDThreshold(Constraint->GetImplicit0());
			const FReal CCDThreshold1 = GetParticleCCDThreshold(Constraint->GetImplicit1());
			const FReal CCDConstraintThreshold = CCDThreshold0 + CCDThreshold1;
			if ((Displacements[1] - Displacements[0]).SizeSquared() > CCDConstraintThreshold * CCDConstraintThreshold)
			{
				bNeedCCDSolve = true;
			}

			// make sure we ignore pairs that don't include any dynamics
			if (CCDParticlePair[0] != nullptr || CCDParticlePair[1] != nullptr)
			{
				CCDConstraints.Add(FCCDConstraint(Constraint, CCDParticlePair, Displacements));
				for (int32 i = 0; i < 2; i++)
				{
					if (CCDParticlePair[i] != nullptr)
					{
						CCDParticlePair[i]->AddConstraint(&CCDConstraints.Last());
					}
				}

				if (IsDynamic[0] && IsDynamic[1])
				{
					CCDParticlePair[0]->AddOverlappingDynamicParticle(CCDParticlePair[1]);
					CCDParticlePair[1]->AddOverlappingDynamicParticle(CCDParticlePair[0]);
				}
			}
		}
		return bNeedCCDSolve;
	}

	void FCCDManager::AssignParticleIslandsAndGroupParticles()
	{
		// Use DFS to find connected dynamic particles and assign islands for them.
		// In the mean time, record numbers in IslandParticleStart and IslandParticleNum
		// Group particles into GroupedCCDParticles based on islands.
		IslandNum = 0;
		IslandStack.Reset();
		GroupedCCDParticles.Reset();
		IslandParticleStart.Reset();
		IslandParticleNum.Reset();
		for (FCCDParticle &CCDParticle : CCDParticles)
		{
			if (CCDParticle.Island != INDEX_NONE || CCDParticle.Particle->ObjectState() != EObjectStateType::Dynamic)
			{
				continue;
			}
			FCCDParticle* CurrentParticle = &CCDParticle;
			CurrentParticle->Island = IslandNum;
			IslandStack.Push(CurrentParticle);
			IslandParticleStart.Push(GroupedCCDParticles.Num());
			int32 CurrentIslandParticleNum = 0;
			while (IslandStack.Num() > 0)
			{
				CurrentParticle = IslandStack.Pop();
				GroupedCCDParticles.Push(CurrentParticle);
				CurrentIslandParticleNum++;
				for (FCCDParticle* OverlappingParticle : CurrentParticle->OverlappingDynamicParticles)
				{
					if (OverlappingParticle->Island == INDEX_NONE)
					{
						OverlappingParticle->Island = IslandNum;
						IslandStack.Push(OverlappingParticle);
					}
				}
			}
			IslandParticleNum.Push(CurrentIslandParticleNum);
			IslandNum++;
		}
	}

	void FCCDManager::AssignConstraintIslandsAndRecordConstraintNum()
	{
		// Assign island to constraints based on particle islands
		// In the mean time, record IslandConstraintNum
		IslandConstraintNum.SetNum(IslandNum);
		for (int32 i = 0; i < IslandNum; i++)
		{
			IslandConstraintNum[i] = 0;
		}

		for (FCCDConstraint &CCDConstraint : CCDConstraints)
		{
			int32 Island = INDEX_NONE;
			if (CCDConstraint.Particle[0])
			{
				Island = CCDConstraint.Particle[0]->Island;
			}
			if (Island == INDEX_NONE)
			{
				// non-dynamic pairs are already ignored in Init() so if Particle 0 is null the second one should not be 
				ensure(CCDConstraint.Particle[1] != nullptr);

				if (CCDConstraint.Particle[1])
				{
					Island = CCDConstraint.Particle[1]->Island;
				}	
			}
			CCDConstraint.Island = Island;
			IslandConstraintNum[Island]++;
		}
	}

	void FCCDManager::GroupConstraintsWithIslands()
	{
		// Group constraints based on island
		// In the mean time, record IslandConstraintStart, IslandConstraintEnd
		IslandConstraintStart.SetNum(IslandNum + 1);
		IslandConstraintEnd.SetNum(IslandNum);
		IslandConstraintStart[0] = 0;
		for (int32 i = 0; i < IslandNum; i++)
		{
			IslandConstraintEnd[i] = IslandConstraintStart[i];
			IslandConstraintStart[i + 1] = IslandConstraintStart[i] + IslandConstraintNum[i];
		}

		SortedCCDConstraints.SetNum(CCDConstraints.Num());
		for (FCCDConstraint &CCDConstraint : CCDConstraints)
		{
			const int32 Island = CCDConstraint.Island;
			SortedCCDConstraints[IslandConstraintEnd[Island]] = &CCDConstraint;
			IslandConstraintEnd[Island]++;
		}
	}

	bool CCDConstraintSortPredicate(const FCCDConstraint* Constraint0, const FCCDConstraint* Constraint1)
	{
		return Constraint0->SweptConstraint->TimeOfImpact < Constraint1->SweptConstraint->TimeOfImpact;
	}

	void FCCDManager::ApplyIslandSweptConstraints(const int32 Island, const FReal Dt)
	{
		const int32 ConstraintStart = IslandConstraintStart[Island];
		const int32 ConstraintNum = IslandConstraintNum[Island];
		const int32 ConstraintEnd = IslandConstraintEnd[Island];
		check(ConstraintNum > 0);

		// Sort constraints based on TOI
		std::sort(SortedCCDConstraints.GetData() + ConstraintStart, SortedCCDConstraints.GetData() + ConstraintStart + ConstraintNum, CCDConstraintSortPredicate);
		FReal IslandTOI = 0.f;
		ResetIslandParticles(Island);
		ResetIslandConstraints(Island);
		int32 ConstraintIndex = ConstraintStart;
		while (ConstraintIndex < ConstraintEnd) 
		{
			FCCDConstraint *CCDConstraint = SortedCCDConstraints[ConstraintIndex];
			IslandTOI = CCDConstraint->SweptConstraint->TimeOfImpact;

			// Constraints whose TOIs are in the range of [0, 1) are resolved for this frame. TOI = 1 means that the two particles just start touching at the end of the frame and therefore cannot have tunneling this frame. So this TOI = 1 can be left to normal collisions or CCD in next frame.
			if (IslandTOI > 1) 
			{
				break;
			}

			FCCDParticle **CCDConstraintParticles = CCDConstraint->Particle;

			// If both particles are marked Done (due to clipping), continue
			if (bChaosCollisionCCDAllowClipping && (!CCDConstraintParticles[0] || CCDConstraintParticles[0]->Done) && (!CCDConstraintParticles[1] || CCDConstraintParticles[1]->Done))
			{
				ConstraintIndex ++;
				continue;
			}

			ensure(CCDConstraint->ProcessedCount < ChaosCollisionCCDConstraintMaxProcessCount);

			// In UpdateConstraintFromGeometrySwept, InitManifoldPoint requires P, Q to be at TOI=1., but the input of UpdateConstraintFromGeometrySwept requires transforms at current TOI. So instead of rewinding P, Q, we advance X, R to current TOI and keep P, Q at TOI=1.
			if (CCDConstraintParticles[0] && !CCDConstraintParticles[0]->Done)
			{
				AdvanceParticleXToTOI(CCDConstraintParticles[0], IslandTOI, Dt);
			}
			if (CCDConstraintParticles[1] && !CCDConstraintParticles[1]->Done)
			{
				AdvanceParticleXToTOI(CCDConstraintParticles[1], IslandTOI, Dt);
			}

			ApplyImpulse(CCDConstraint);
			CCDConstraint->ProcessedCount ++;
			// After applying impulse, constraint TOI need be updated to reflect the new velocities. Usually the new velocities are separating, and therefore TOI should be infinity.
			CCDConstraint->SweptConstraint->TimeOfImpact = TNumericLimits<FReal>::Max();

			if (CCDConstraint->ProcessedCount >= ChaosCollisionCCDConstraintMaxProcessCount)
			{
				/* Here is how clipping works:
				* Assuming collision detection gives us all the possible collision pairs in the current frame.
				* Because we sort and apply constraints based on their TOIs, at current IslandTOI, the two particles cannot tunnel through other particles in the island. 
				* Now, we run out of the computational budget for this constraint, then we freeze the two particles in place. The current two particles cannot tunnel through each other this frame.
				* The two particles are then treated as static. When resweeping, we update TOIs of other constraints to make sure other particles in the island cannot tunnel through this two particles.
				* Therefore, by clipping, we can avoid tunneling but this is at the cost of reduced momentum.
				* For kinematic particles, we cannot freeze them in place. In this case, we simply offset the particle with the kinematic motion from [IslandTOI, 1] along the collision normal and freeze it there.
				* If collision detection is not perfect and does not give us all the secondary collision pairs, setting ChaosCollisionCCDConstraintMaxProcessCount to 1 will always prevent tunneling.
				*/ 
				if (bChaosCollisionCCDAllowClipping)
				{
					if (CCDConstraintParticles[0])
					{
						if (CCDConstraint->FastMovingKinematicIndex != INDEX_NONE)
						{
							const FConstGenericParticleHandle Particle1 = FGenericParticleHandle(CCDConstraint->SweptConstraint->GetParticle(CCDConstraint->FastMovingKinematicIndex));
							const FVec3 Normal = CCDConstraint->SweptConstraint->CalculateWorldContactNormal();
							const FVec3 Offset = FVec3::DotProduct(Particle1->V() * ((1.f - IslandTOI) * Dt), Normal) * Normal;
							ClipParticleP(CCDConstraintParticles[0], Offset);
						}
						else
						{
							ClipParticleP(CCDConstraintParticles[0]);
						}
						CCDConstraintParticles[0]->Done = true;
					}
					if (CCDConstraintParticles[1])
					{
						ClipParticleP(CCDConstraintParticles[1]);
						CCDConstraintParticles[1]->Done = true;
					}
				}
				// If clipping is not allowed, we update particle P (at TOI=1) based on new velocities. 
				else
				{
					if (CCDConstraintParticles[0])
					{
						UpdateParticleP(CCDConstraint->Particle[0], Dt);
					}
					if (CCDConstraintParticles[1])
					{
						UpdateParticleP(CCDConstraint->Particle[1], Dt);
					}
				}
				// Increment ConstraintIndex if we run out of computational budget for this constraint.
				ConstraintIndex ++;
			}
			// If we still have computational budget for this constraint, update particle P and don't clip.
			else
			{
				if (CCDConstraintParticles[0] && !CCDConstraintParticles[0]->Done)
				{
					UpdateParticleP(CCDConstraint->Particle[0], Dt);
				}
				if (CCDConstraintParticles[1] && !CCDConstraintParticles[1]->Done)
				{
					UpdateParticleP(CCDConstraint->Particle[1], Dt);
				}
			}

			if (bChaosCollisionCCDEnableResweep)
			{
				// For each constraint that contains the two particles on which we applied impulses, we need to update its TOI.
				const FReal RestDt = (1.f - IslandTOI) * Dt;
				bool HasResweptConstraint = false;
				for (int32 i = 0; i < 2; i++)
				{
					FCCDParticle *CCDParticle = CCDConstraintParticles[i];
					if (CCDParticle != nullptr)
					{
						for (int32 AttachedCCDConstraintIndex = 0; AttachedCCDConstraintIndex < CCDParticle->AttachedCCDConstraints.Num(); AttachedCCDConstraintIndex++)
						{
							FCCDConstraint *AttachedCCDConstraint = CCDParticle->AttachedCCDConstraints[AttachedCCDConstraintIndex];
							if (AttachedCCDConstraint == CCDConstraint || AttachedCCDConstraint->ProcessedCount >= ChaosCollisionCCDConstraintMaxProcessCount)
							{
								continue;
							}
							FRigidTransform3 RigidTransforms[2]; // Rigid transforms at TOI
							for(int32 j = 0; j < 2; j++)
							{
								FCCDParticle *AffectedCCDParticle = AttachedCCDConstraint->Particle[j];
								if (AffectedCCDParticle != nullptr)
								{
									TPBDRigidParticleHandle<FReal, 3>* AffectedParticle = AffectedCCDParticle->Particle;
									if (!AffectedCCDParticle->Done)
									{
										AdvanceParticleXToTOI(AffectedCCDParticle, IslandTOI, Dt);
									}
									RigidTransforms[j] = FRigidTransform3(AffectedParticle->X(), AffectedParticle->R());
								}
								else
								{
									FGenericParticleHandle AffectedParticle = FGenericParticleHandle(AttachedCCDConstraint->SweptConstraint->GetParticle(j));
									const bool IsKinematic = AffectedParticle->ObjectState() == EObjectStateType::Kinematic;
									if (IsKinematic)
									{
										RigidTransforms[j] = FRigidTransform3(AffectedParticle->P() - AffectedParticle->V() * ((1.f - IslandTOI) * Dt), AffectedParticle->Q());
									}
									else // Static case
									{
										RigidTransforms[j] = FRigidTransform3(AffectedParticle->X(), AffectedParticle->R());
									}
								}
							}
							/** When resweeping, we need to recompute TOI for affected constraints and therefore the work (GJKRaycast) used to compute the original TOI is wasted.
							* A potential optimization is to compute an estimate of TOI using the AABB of the particles. Sweeping AABBs to compute an estimated TOI can be very efficient, and this TOI is strictly smaller than the accurate TOI.
							* At each for-loop iteration, we only need the constraint with the smallest TOI in the island. A potential optimized algorithm could be like:
							* 	First, sort constraints based on estimated TOI.
							*	Find the constraint with the smallest accurate TOI:
							*		Walk through the constraint list, change estimated TOI to accurate TOI
							*		If accurate TOI is smaller than estimated TOI of the next constraint, we know we found the constraint.
							*	When resweeping, compute estimated TOI instead of accurate TOI since updated TOI might need to be updated again.
							*/

							const bool bUpdated = Collisions::UpdateConstraintFromGeometrySwept<ECollisionUpdateType::Deepest>(*(AttachedCCDConstraint->SweptConstraint), RigidTransforms[0], RigidTransforms[1], RestDt);
							if (bUpdated)
							{
								const FReal RestDtTOI = AttachedCCDConstraint->SweptConstraint->TimeOfImpact;
								if (RestDtTOI >= 0 && RestDtTOI < 1.f)
								{
									AttachedCCDConstraint->SweptConstraint->TimeOfImpact = IslandTOI + (1.f - IslandTOI) * RestDtTOI;
								}
							}
							// When bUpdated==true, TOI was modified. When bUpdated==false, TOI was set to be TNumericLimits<FReal>::Max(). In either case, a re-sorting on the constraints is needed.
							HasResweptConstraint = true;
						}
					}
				}
				if (HasResweptConstraint)
				{
					// This could be optimized by using bubble sort if there are only a few updated constraints.
					std::sort(SortedCCDConstraints.GetData() + ConstraintIndex, SortedCCDConstraints.GetData() + ConstraintStart + ConstraintNum, CCDConstraintSortPredicate);
				}
			}
		}

		// We need to update the world-space contact points at the final locations
		for (int32 i = ConstraintStart; i < ConstraintEnd; i++)
		{
			FPBDCollisionConstraint* Constraint = SortedCCDConstraints[i]->SweptConstraint;
			FRigidTransform3 ShapeWorldTransform0 = Constraint->GetShapeWorldTransform0();
			FRigidTransform3 ShapeWorldTransform1 = Constraint->GetShapeWorldTransform1();
			ShapeWorldTransform0.SetTranslation(FConstGenericParticleHandle(Constraint->GetParticle0())->P());
			ShapeWorldTransform1.SetTranslation(FConstGenericParticleHandle(Constraint->GetParticle1())->P());

			Constraint->SetShapeWorldTransforms(ShapeWorldTransform0, ShapeWorldTransform1);
			Constraint->UpdateManifoldContacts();
		}
	}


	void FCCDManager::ResetIslandParticles(const int32 Island)
	{
		const int32 ParticleStart = IslandParticleStart[Island];
		const int32 ParticleNum = IslandParticleNum[Island];
		for (int32 i = ParticleStart; i < ParticleStart + ParticleNum; i++)
		{
			GroupedCCDParticles[i]->TOI = 0.f;
			GroupedCCDParticles[i]->Done = false;
		}
	}

	void FCCDManager::ResetIslandConstraints(const int32 Island)
	{
		const int32 ConstraintStart = IslandConstraintStart[Island];
		const int32 ConstraintEnd = IslandConstraintEnd[Island];
		for (int32 i = ConstraintStart; i < ConstraintEnd; i++)
		{
			SortedCCDConstraints[i]->ProcessedCount = 0;
		}
	}

	void FCCDManager::AdvanceParticleXToTOI(FCCDParticle *CCDParticle, const FReal TOI, const FReal Dt) const
	{
		if (TOI > CCDParticle->TOI)
		{
			TPBDRigidParticleHandle<FReal, 3>* Particle = CCDParticle->Particle;
			const FReal RestDt = (TOI - CCDParticle->TOI) * Dt;
			Particle->X() = Particle->X() + Particle->V() * RestDt;
			CCDParticle->TOI = TOI;
		}
	}

	void FCCDManager::UpdateParticleP(FCCDParticle *CCDParticle, const FReal Dt) const
	{
		TPBDRigidParticleHandle<FReal, 3>* Particle = CCDParticle->Particle;
		const FReal RestDt = (1.f - CCDParticle->TOI) * Dt;
		Particle->P() = Particle->X() + Particle->V() * RestDt;
	}

	void FCCDManager::ClipParticleP(FCCDParticle *CCDParticle) const
	{
		TPBDRigidParticleHandle<FReal, 3>* Particle = CCDParticle->Particle;
		Particle->P() = Particle->X();
	}

	void FCCDManager::ClipParticleP(FCCDParticle *CCDParticle, const FVec3 Offset) const
	{
		TPBDRigidParticleHandle<FReal, 3>* Particle = CCDParticle->Particle;
		Particle->X() += Offset;
		Particle->P() = Particle->X();
	}

	void FCCDManager::ApplyImpulse(FCCDConstraint *CCDConstraint)
	{
		FPBDCollisionConstraint *Constraint = CCDConstraint->SweptConstraint;
		TPBDRigidParticleHandle<FReal, 3> *Rigid0 = Constraint->GetParticle0()->CastToRigidParticle();
		TPBDRigidParticleHandle<FReal, 3> *Rigid1 = Constraint->GetParticle1()->CastToRigidParticle();
		check(Rigid0 != nullptr || Rigid1 != nullptr);
		const FReal Restitution = Constraint->GetRestitution();
		const FRigidTransform3& ShapeWorldTransform1 = Constraint->GetShapeWorldTransform1();
		for(const FManifoldPoint &ManifoldPoint : Constraint->GetManifoldPoints())
		{
			const FVec3 Normal = ShapeWorldTransform1.TransformVectorNoScale(ManifoldPoint.ContactPoint.ShapeContactNormal);
			const FVec3 V0 = Rigid0 != nullptr ? Rigid0->V() : FVec3(0.f);
			const FVec3 V1 = Rigid1 != nullptr ? Rigid1->V() : FVec3(0.f);
			const FReal NormalV = FVec3::DotProduct(V0 - V1, Normal);
			if (NormalV < 0.f)
			{
				const FReal TargetNormalV = -Restitution * NormalV;
				// If a particle is marked done, we treat it as static by setting InvM to 0. 
				const bool bInfMass0 = Rigid0 == nullptr || (bChaosCollisionCCDAllowClipping && CCDConstraint->Particle[0] && CCDConstraint->Particle[0]->Done);
				const bool bInfMass1 = Rigid1 == nullptr || (bChaosCollisionCCDAllowClipping && CCDConstraint->Particle[1] && CCDConstraint->Particle[1]->Done);
				const FReal InvM0 = bInfMass0 ? 0.f : Rigid0->InvM();
				const FReal InvM1 = bInfMass1 ? 0.f : Rigid1->InvM();
				const FVec3 Impulse = (TargetNormalV - NormalV) * Normal / (InvM0 + InvM1);
				if (InvM0 > 0.f)
				{
					Rigid0->V() += Impulse * InvM0;
				}
				if (InvM1 > 0.f)
				{
					Rigid1->V() -= Impulse * InvM1;
				}
			}
		}
	}

	void FCCDManager::UpdateSweptConstraints(const FReal Dt, FCollisionConstraintAllocator *CollisionAllocator)
	{
		for (FPBDCollisionConstraint* SweptConstraint : SweptConstraints)
		{
			FRigidTransform3 RigidTransforms[2];
			for (int32 i = 0; i < 2; i++)
			{
				FGenericParticleHandle Particle = FGenericParticleHandle(SweptConstraint->GetParticle(i));
				const bool IsStatic = Particle->ObjectState() == EObjectStateType::Static;
				if (IsStatic)
				{
					RigidTransforms[i] = FRigidTransform3(Particle->X(), Particle->R());
				}
				else
				{
					RigidTransforms[i] = FRigidTransform3(Particle->P(), Particle->Q());
				}
			}
			SweptConstraint->ResetManifold();
			Collisions::UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest>(*(SweptConstraint), RigidTransforms[0], RigidTransforms[1], Dt);

			// @todo(zhenglin): Removing constraints that has Phi larger than CullDistance could reduce the island sizes in the normal solve. But I could not get this to work...
			// if (SweptConstraint->GetPhi() > SweptConstraint->GetCullDistance())
			// {
			//     CollisionAllocator->RemoveConstraintSwap(SweptConstraint);
			// }
		}
	}

	void FCCDManager::OverwriteXUsingV(const FReal Dt)
	{
		// Overwriting X = P - V * Dt so that the implicit velocity step will give our velocity back.
		for (FCCDParticle& CCDParticle : CCDParticles)
		{
			TPBDRigidParticleHandle<FReal, 3>* Particle = CCDParticle.Particle;
			Particle->X() = Particle->P() - Particle->V() * Dt;
		}
	}
}