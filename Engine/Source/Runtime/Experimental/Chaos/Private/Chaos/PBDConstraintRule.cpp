// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintRule.h"

#include "Chaos/Island/SolverIsland.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/PBDSuspensionConstraints.h"
#include "Chaos/PBDRigidDynamicSpringConstraints.h"
#include "Chaos/PBDRigidSpringConstraints.h"

namespace Chaos
{
	int32 ChaosShockPropagationVelocityPerLevelIterations = 1;
	int32 ChaosShockPropagationPositionPerLevelIterations = 1;
	FAutoConsoleVariableRef CVarChaosShockPropagationPositionPerLevelIterations(TEXT("p.Chaos.ShockPropagation.Position.PerLevelIterations"), ChaosShockPropagationPositionPerLevelIterations, TEXT(""));
	FAutoConsoleVariableRef CVarChaosShockPropagationVelocityPerLevelIterations(TEXT("p.Chaos.ShockPropagation.Velocity.PerLevelIterations"), ChaosShockPropagationVelocityPerLevelIterations, TEXT(""));

	int32 ChaosCollisionColorMinParticles = 2000;
	FAutoConsoleVariableRef CVarChaosCollisionColorMinParticles(TEXT("p.Chaos.Collision.Color.MinParticles"), ChaosCollisionColorMinParticles, TEXT(""));

	template<class ConstraintType>
	TSimpleConstraintRule<ConstraintType>::TSimpleConstraintRule(int32 InPriority, FConstraints& InConstraints)
		: FSimpleConstraintRule(InPriority)
		, Constraints(InConstraints)
	{
	}

	template<class ConstraintType>
	TSimpleConstraintRule<ConstraintType>::~TSimpleConstraintRule()
	{
	}

	template<class ConstraintType>
	void TSimpleConstraintRule<ConstraintType>::PrepareTick()
	{
		Constraints.PrepareTick();
	}

	template<class ConstraintType>
	void TSimpleConstraintRule<ConstraintType>::UnprepareTick()
	{
		Constraints.UnprepareTick();
	}

	template<class ConstraintType>
	void TSimpleConstraintRule<ConstraintType>::UpdatePositionBasedState(const FReal Dt)
	{
		return Constraints.UpdatePositionBasedState(Dt);
	}

	template<class ConstraintType>
	void TSimpleConstraintRule<ConstraintType>::BindToDatas(FPBDIslandSolverData& InSolverDatas, const uint32 InContainerId)
	{
		Constraints.SetContainerId(InContainerId);

		SolverData = &InSolverDatas;
		if (SolverData != nullptr)
		{
			SolverData->template AddConstraintDatas<ConstraintType>(Constraints.GetContainerId());
		}
	}

	template<class ConstraintType>
	void TSimpleConstraintRule<ConstraintType>::GatherSolverInput(const FReal Dt)
	{
		if(SolverData)
		{
			Constraints.SetNumIslandConstraints(Constraints.NumConstraints(), *SolverData);
			Constraints.GatherInput(Dt, *SolverData);
		}
	}

	template<class ConstraintType>
	void TSimpleConstraintRule<ConstraintType>::ScatterSolverOutput(const FReal Dt)
	{
		if(SolverData) Constraints.ScatterOutput(Dt, *SolverData);
	}

	template<class ConstraintType>
	bool TSimpleConstraintRule<ConstraintType>::ApplyConstraints(const FReal Dt, const int32 It, const int32 NumIts)
	{
		return SolverData ? Constraints.ApplyPhase1(Dt, It, NumIts, *SolverData) : false;
	}

	template<class ConstraintType>
	bool TSimpleConstraintRule<ConstraintType>::ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts)
	{
		return SolverData ? Constraints.ApplyPhase2(Dt, It, NumIts, *SolverData) : false;
	}
	
	template<class ConstraintType>
	TPBDConstraintGraphRuleImpl<ConstraintType>::TPBDConstraintGraphRuleImpl(FConstraints& InConstraints, int32 InPriority)
		: FPBDConstraintGraphRule(InPriority)
		, Constraints(InConstraints)
		, ConstraintGraph(nullptr)
	{
	}
	
	template<class ConstraintType>
	void TPBDConstraintGraphRuleImpl<ConstraintType>::BindToGraph(FPBDConstraintGraph& InContactGraph, uint32 InContainerId)
	{
		Constraints.SetContainerId(InContainerId);
		ConstraintGraph = &InContactGraph;
	}

	template<class ConstraintType>
	void TPBDConstraintGraphRuleImpl<ConstraintType>::UpdatePositionBasedState(const FReal Dt)
	{
		Constraints.UpdatePositionBasedState(Dt);
	}
	
	template<class ConstraintType>
	void TPBDConstraintGraphRuleImpl<ConstraintType>::AddToGraph()
	{
		ConstraintGraph->ReserveConstraints(Constraints.NumConstraints());

		for (typename FConstraints::FConstraintContainerHandle * ConstraintHandle : Constraints.GetConstraintHandles())
		{
			if (ConstraintHandle->IsEnabled())
			{
				ConstraintGraph->AddConstraint(GetContainerId(), ConstraintHandle, ConstraintHandle->GetConstrainedParticles());
			}
		}
	}

	template<class ConstraintType>
	TPBDConstraintIslandRule<ConstraintType>::TPBDConstraintIslandRule(FConstraints& InConstraints, int32 InPriority)
		: TPBDConstraintGraphRuleImpl<ConstraintType>(InConstraints, InPriority)
	{
	}

	template<class ConstraintType>
	TPBDConstraintIslandRule<ConstraintType>::~TPBDConstraintIslandRule()
	{
	}

	template<class ConstraintType>
	void TPBDConstraintIslandRule<ConstraintType>::GatherSolverInput(const FReal Dt, int32 Island)
	{
		if(FPBDIslandSolver* IslandSolver = ConstraintGraph->GetSolverIsland(Island))
		{
			const TArray<FConstraintHandle*>& IslandConstraints = ConstraintGraph->GetIslandConstraints(Island);

			// This will reset the number od constraints inside the solver datas. For now we keep this function since according
			// to the constraints we can use the handles, the indices or the container. when only the container will be used
			// we can replace this call by :
			// IslandSolver->template GetConstraintContainer<typename ConstraintType::FSolverConstraintContainerType>(ContainerId)->Reset();
			Constraints.SetNumIslandConstraints(IslandConstraints.Num(), *IslandSolver);

			for (FConstraintHandle* ConstraintHandle : IslandConstraints)
			{
				if (ConstraintHandle->GetContainerId() == GetContainerId())
				{
					FConstraintContainerHandle* Constraint = ConstraintHandle->As<FConstraintContainerHandle>();

					// Note we are building the SolverBodies as we go, in the order that we visit them. Each constraint
					// references two bodies, so we won't strictly be acessing only in cache order, but it's about as good as it can be.
					if (Constraint->IsEnabled())
					{
						// @todo(chaos): we should provide Particle Levels in the island rule as well (see TPBDConstraintColorRule)
						Constraint->GatherInput(Dt, INDEX_NONE, INDEX_NONE, *IslandSolver);
					}
				}
			}
		}
	}

	template<class ConstraintType>
	void TPBDConstraintIslandRule<ConstraintType>::ScatterSolverOutput(const FReal Dt, int32 Island)
	{
		if(FPBDIslandSolver* IslandSolver = ConstraintGraph->GetSolverIsland(Island))
		{
			Constraints.ScatterOutput(Dt, *IslandSolver);
		}
	}
	
	template<class ConstraintType>
	bool TPBDConstraintIslandRule<ConstraintType>::ApplyConstraints(const FReal Dt, int32 Island, const int32 It, const int32 NumIts)
	{
		FPBDIslandSolver* IslandSolver = ConstraintGraph->GetSolverIsland(Island);
		return IslandSolver ? Constraints.ApplyPhase1Serial(Dt, It, NumIts, *IslandSolver) : false;
	}

	template<class ConstraintType>
	bool TPBDConstraintIslandRule<ConstraintType>::ApplyPushOut(const FReal Dt, int32 Island, const int32 It, const int32 NumIts)
	{
		FPBDIslandSolver* IslandSolver = ConstraintGraph->GetSolverIsland(Island);
		return IslandSolver ? Constraints.ApplyPhase2Serial(Dt, It, NumIts, *IslandSolver) : false;
	}

	template<class ConstraintType>
	void TPBDConstraintIslandRule<ConstraintType>::InitializeAccelerationStructures()
	{
		ConstraintGraph->template AddConstraintDatas<ConstraintType>(Constraints.GetContainerId());
	}

	template<class ConstraintType>
	void TPBDConstraintIslandRule<ConstraintType>::UpdateAccelerationStructures(const FReal Dt, const int32 Island)
	{
	}
	
	template<class ConstraintType>
	int32 TPBDConstraintGraphRuleImpl<ConstraintType>::NumConstraints() const
	{ 
		return Constraints.NumConstraints(); 
	}

	template<class ConstraintType>
	TPBDConstraintColorRule<ConstraintType>::TPBDConstraintColorRule(FConstraints& InConstraints, int32 InPriority)
		: TPBDConstraintGraphRuleImpl<ConstraintType>(InConstraints, InPriority)
		, ConstraintSets()
		, bUseColor(false)
	{
	}

	template<class ConstraintType>
	TPBDConstraintColorRule<ConstraintType>::~TPBDConstraintColorRule()
	{
	}

	template<class ConstraintType>
	void TPBDConstraintColorRule<ConstraintType>::GatherSolverInput(const FReal Dt, int32 Island)
	{
		if(FPBDIslandSolver* IslandSolver = ConstraintGraph->GetSolverIsland(Island))
		{
			const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = GraphColor.GetIslandLevelToColorToConstraintListMap(Island);
			int32 MaxColor = GraphColor.GetIslandMaxColor(Island);
			int32 MaxLevel = GraphColor.GetIslandMaxLevel(Island);

			TArray<TPair<int32, int32>>& IslandConstraintSets = ConstraintSets[Island];
			IslandConstraintSets.Reset(MaxLevel * MaxColor);	// Pessimistic array size - we could store the actual required size in coloring algorithm
			int32 ConstraintSetEnd = 0;

			Constraints.SetNumIslandConstraints(GraphColor.NumIslandEdges(Island), *IslandSolver);

			for (int32 Level = 0; Level <= MaxLevel; ++Level)
			{
				const int32 OldConstraintSetEnd = ConstraintSetEnd;

				for (int32 Color = 0; Color <= MaxColor; ++Color)
				{
					if (LevelToColorToConstraintListMap[Level].Contains(Color) && LevelToColorToConstraintListMap[Level][Color].Num())
					{
						// Calculate the range of indices for this color as a set of independent contacts
						TPair<int32, int32> ColorConstrainSet(ConstraintSetEnd, ConstraintSetEnd);

						const TArray<FConstraintContainerHandle*>& ConstraintHandles = GetLevelColorConstraints(LevelToColorToConstraintListMap, Level, Color);
						for (FConstraintContainerHandle* Constraint : ConstraintHandles)
						{
							if (Constraint->IsEnabled())
							{
								// Levels that should be assigned to the bodies for shock propagation
								// @todo(chaos): optimize the lookup
								const TVector<TGeometryParticleHandle<FReal, 3>*, 2> ConstrainedParticles = Constraint->GetConstrainedParticles();
								const int32 Particle0Level = GraphColor.GetParticleLevel(ConstrainedParticles[0]);
								const int32 Particle1Level = GraphColor.GetParticleLevel(ConstrainedParticles[1]);

								// Note we are building the SolverBodies as we go, in the order that we visit them. Each constraint
								// references two bodies, so we won't strictly be acessing only in cache order, but it's about as good as it can be.
								Constraint->GatherInput(Dt, Particle0Level, Particle1Level, *IslandSolver);

								// Update the current constraint set of this color
								ColorConstrainSet.Value = ++ConstraintSetEnd;
							}
						}

						// Remember the set of constraints of this color
						if (bUseColor)
						{
							IslandConstraintSets.Add(ColorConstrainSet);
						}
					}
				}

				const int32 NumAdded = ConstraintSetEnd - OldConstraintSetEnd;
				if (NumAdded > 1)
				{
					static int32 sHere = 0;
					++sHere;
				}
			}

			// If we aren't coloring, we have a single group of all constraints (they have been created in level order above)
			if (!bUseColor)
			{
				const TPair<int32, int32> ConstrainSet(0, ConstraintSetEnd);
				IslandConstraintSets.Add(ConstrainSet);
			}
		}
	}

	template<class ConstraintType>
	void TPBDConstraintColorRule<ConstraintType>::ScatterSolverOutput(const FReal Dt, int32 Island)
	{
		if(FPBDIslandSolver* IslandSolver = ConstraintGraph->GetSolverIsland(Island))
		{
			const TArray<TPair<int32, int32>>& IslandConstraintSets = ConstraintSets[Island];
			for (const TPair<int32, int32>& ConstraintSet : IslandConstraintSets)
			{
				Constraints.ScatterOutput(Dt, ConstraintSet.Key, ConstraintSet.Value, *IslandSolver);
			}
		}
	}

	template<class ConstraintType>
	void TPBDConstraintColorRule<ConstraintType>::ApplySwept(const FReal Dt, int32 Island)
	{
		if(FPBDIslandSolver* IslandSolver = ConstraintGraph->GetSolverIsland(Island))
		{
			Constraints.ApplySwept(Dt, *IslandSolver);
		}
	}

	template<class ConstraintType>
	bool TPBDConstraintColorRule<ConstraintType>::ApplyConstraints(const FReal Dt, int32 Island, const int32 It, const int32 NumIts)
	{
		bool bNeedsAnotherIteration = false;
		if(FPBDIslandSolver* IslandSolver = ConstraintGraph->GetSolverIsland(Island))
		{
			const TArray<TPair<int32, int32>>& IslandConstraintSets = ConstraintSets[Island];
			if (!bUseColor)
			{
				for (const TPair<int32, int32>& ConstraintSet : IslandConstraintSets)
				{
					bNeedsAnotherIteration |= Constraints.ApplyPhase1Serial(Dt, It, NumIts, ConstraintSet.Key, ConstraintSet.Value, *IslandSolver);
				}
			}
			else
			{
				for (const TPair<int32, int32>& ConstraintSet : IslandConstraintSets)
				{
					bNeedsAnotherIteration |= Constraints.ApplyPhase1Parallel(Dt, It, NumIts, ConstraintSet.Key, ConstraintSet.Value, *IslandSolver);
				}
			}
		}
		return bNeedsAnotherIteration;
	}

	template<class ConstraintType>
	bool TPBDConstraintColorRule<ConstraintType>::ApplyPushOut(const FReal Dt, int32 Island, const int32 It, const int32 NumIts)
	{
		bool bNeedsAnotherIteration = false;
		if(FPBDIslandSolver* IslandSolver = ConstraintGraph->GetSolverIsland(Island))
		{
			const TArray<TPair<int32, int32>>& IslandConstraintSets = ConstraintSets[Island];
			if (!bUseColor)
			{
				for (const TPair<int32, int32>& ConstraintSet : IslandConstraintSets)
				{
					bNeedsAnotherIteration |= Constraints.ApplyPhase2Serial(Dt, It, NumIts, ConstraintSet.Key, ConstraintSet.Value, *IslandSolver);
				}
			}
			else
			{
				for (const TPair<int32, int32>& ConstraintSet : IslandConstraintSets)
				{
					bNeedsAnotherIteration |= Constraints.ApplyPhase2Parallel(Dt, It, NumIts, ConstraintSet.Key, ConstraintSet.Value, *IslandSolver);
				}
			}
		}
		return bNeedsAnotherIteration;
	}

	template<class ConstraintType>
	void TPBDConstraintColorRule<ConstraintType>::InitializeAccelerationStructures()
	{
		GraphColor.InitializeColor(*ConstraintGraph);

		ConstraintSets.SetNum(GraphColor.NumIslands());
		ConstraintGraph->template AddConstraintDatas<ConstraintType>(Constraints.GetContainerId());
	}

	template<class ConstraintType>
	void TPBDConstraintColorRule<ConstraintType>::UpdateAccelerationStructures(const FReal Dt, const int32 Island)
	{
		// Decide if we want to attempt to solve the constraints in parallel - this requires coloring the graph which
		// is expensive, so we need to get a good number of contacts per color (per level) for it to be worthwhile.
		// @todo(chaos): if enabled here, it should get disabled again if the level/coloring doesn't produce enough parallelism
		bUseColor = (ConstraintGraph->GetIslandParticles(Island).Num() > ChaosCollisionColorMinParticles);

		// @todo(chaos): disable coloring, but still generate levels and ordering, when we don't need parallelism
		GraphColor.ComputeColor(Dt, Island, *ConstraintGraph, Constraints.GetContainerId());
	}

	template<class ConstraintType>
	void TPBDConstraintColorRule<ConstraintType>::SetUseContactGraph(const bool bInUseContactGraph)
	{
		GraphColor.SetUseContactGraph(bInUseContactGraph);
	}


	template class TSimpleConstraintRule<FPBDCollisionConstraints>;
	template class TSimpleConstraintRule<FPBDJointConstraints>;
	template class TSimpleConstraintRule<FPBDRigidSpringConstraints>;

	template class TPBDConstraintGraphRuleImpl<FPBDCollisionConstraints>;
	template class TPBDConstraintGraphRuleImpl<FPBDJointConstraints>;
	template class TPBDConstraintGraphRuleImpl<FPBDPositionConstraints>;
	template class TPBDConstraintGraphRuleImpl<FPBDSuspensionConstraints>;
	template class TPBDConstraintGraphRuleImpl<FPBDRigidDynamicSpringConstraints>;
	template class TPBDConstraintGraphRuleImpl<FPBDRigidSpringConstraints>;

	template class TPBDConstraintColorRule<FPBDCollisionConstraints>;
	template class TPBDConstraintIslandRule<FPBDJointConstraints>;
	template class TPBDConstraintIslandRule<FPBDPositionConstraints>;
	template class TPBDConstraintIslandRule<FPBDSuspensionConstraints>;
	template class TPBDConstraintIslandRule<FPBDRigidDynamicSpringConstraints>;
	template class TPBDConstraintIslandRule<FPBDRigidSpringConstraints>;
}