// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Declares.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/PBDConstraintColor.h"
#include "Chaos/PBDConstraintGraph.h"

#define USE_SHOCK_PROPOGATION 1

namespace Chaos
{
	/**
	 * Constraint Rules bind constraint collections to the evolution and provide their update algorithm.
	 */
	class CHAOS_API FConstraintRule
	{
	public:
		FConstraintRule(int32 InPriority) : Priority(InPriority) {}
		virtual ~FConstraintRule() {}

		/** Determines the order in which constraints are resolved. Higher priority constraints override lower priority ones. */
		int32 GetPriority() const { return Priority; }

		/** Set the constraint resolution priority. Higher priority constraints override lower priority ones. */
		void SetPriority(const int32 InPriority) { Priority = InPriority; }

		friend bool operator<(const FConstraintRule& L, const FConstraintRule& R)
		{
			return L.GetPriority() < R.GetPriority();
		}

		/** Called once per frame before Apply. Can be used to prepare caches etc. */
		virtual void PrepareConstraints(FReal Dt) {}

		/** Called once per frame after Apply. Should be used to release any transient stores created in PrepareConstraints. */
		virtual void UnprepareConstraints(FReal Dt) {}

	protected:
		int32 Priority;
	};

	/**
	 * Constraint rule for evolutions that do not use Constraint Graphs or other acceleration schemes.
	 */
	class CHAOS_API FSimpleConstraintRule : public FConstraintRule
	{
	public:
		FSimpleConstraintRule(int32 InPriority) : FConstraintRule(InPriority) {}

		virtual void UpdatePositionBasedState(const FReal Dt) {}
		virtual void ApplyConstraints(const FReal Dt, const int32 It, const int32 NumIts) {}
		virtual bool ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts) { return false; }
	};

	template<typename T_CONSTRAINTS>
	class CHAOS_API TSimpleConstraintRule : public FSimpleConstraintRule
	{
	public:
		using FConstraints = T_CONSTRAINTS;

		TSimpleConstraintRule(int32 InPriority, FConstraints& InConstraints)
			: FSimpleConstraintRule(InPriority)
			, Constraints(InConstraints)
		{
		}

		virtual void PrepareConstraints(FReal Dt) override
		{
			Constraints.PrepareConstraints(Dt);
		}

		virtual void UnprepareConstraints(FReal Dt) override
		{
			Constraints.UnprepareConstraints(Dt);
		}

		virtual void UpdatePositionBasedState(const FReal Dt) override
		{
			return Constraints.UpdatePositionBasedState(Dt);
		}

		virtual void ApplyConstraints(const FReal Dt, const int32 It, const int32 NumIts) override
		{
			Constraints.Apply(Dt, It, NumIts);
		}

		virtual bool ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts) override
		{ 
			return Constraints.ApplyPushOut(Dt, It, NumIts);
		}

	private:
		FConstraints& Constraints;
	};


	/**
	 * Base class for Constraint Rules that use the Contact Graph (which will be most optimized ones).
	 * The graph is shared among many/all constraint rules and is held external to the Graph rule itself.
	 * Each edge in the graph can be mapped back to a constraint controlled by the rule. To support this,
	 * each rule is assigned an ID which is stored alongside the constraint index in the graph.
	 * @see TPBDConstraintGraphRuleImpl
	 */
	class CHAOS_API FPBDConstraintGraphRule : public FConstraintRule
	{
	public:
		FPBDConstraintGraphRule(int32 InPriority) : FConstraintRule(InPriority) {}
		virtual ~FPBDConstraintGraphRule() {}

		virtual void BindToGraph(FPBDConstraintGraph& InContactGraph, uint32 InContainerId) {}

		/** Called once per tick to allow constraint containers to create/alter their constraints based on particle position */
		virtual void UpdatePositionBasedState(const FReal Dt) {}

		/** Apply all corrections for constraints in the specified island */
		virtual void ApplyConstraints(const FReal Dt, int32 Island, const int32 It, const int32 NumIts) {}

		/** Apply push out for constraints in the specified island */
		virtual bool ApplyPushOut(const FReal Dt, int32 Island, const int32 It, const int32 NumIts) { return false; }

		/** Add all constraints to the connectivity graph */
		virtual void AddToGraph() {}

		/** Initialize and performance-acceleration structures from the contact graph. Called once per evolution update */
		virtual void InitializeAccelerationStructures() {}

		/** Set up the perf-acceleration structures for the specified island. May be called in parallel for islands */
		virtual void UpdateAccelerationStructures(const int32 Island) {}

		/** Remove all constraints associated with the specified particles */
		// @todo(ccaulfield): remove uint version
		virtual void RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles) { }

		/** The number of constraints in the collection */
		virtual int32 NumConstraints() const { return 0; }
	};


	/**
	 * ConstraintGraphRule helper base class - templatized on Constraint Container.
	 */
	template<typename T_CONSTRAINTS>
	class CHAOS_API TPBDConstraintGraphRuleImpl : public FPBDConstraintGraphRule
	{
	public:
		typedef T_CONSTRAINTS FConstraints;

		TPBDConstraintGraphRuleImpl(FConstraints& InConstraints, int32 InPriority);

		virtual void PrepareConstraints(FReal Dt) override
		{
			Constraints.PrepareConstraints(Dt);
		}

		virtual void UnprepareConstraints(FReal Dt) override
		{
			Constraints.UnprepareConstraints(Dt);
		}

		virtual void BindToGraph(FPBDConstraintGraph& InContactGraph, uint32 InContainerId) override;

		virtual void UpdatePositionBasedState(const FReal Dt) override;

		virtual void AddToGraph() override;

		virtual int32 NumConstraints() const override;

	protected:
		FConstraints& Constraints;
		FPBDConstraintGraph* ConstraintGraph;
		uint32 ContainerId;
	};

	/**
	 * Island-based constraint rule. All constraints in an island are updated in single-threaded a loop. Islands may be updated in parallel.
	 */
	template<typename T_CONSTRAINTS>
	class CHAOS_API TPBDConstraintIslandRule : public TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>
	{
		typedef TPBDConstraintGraphRuleImpl<T_CONSTRAINTS> Base;

	public:
		using FConstraints = T_CONSTRAINTS;
		using FConstraintContainerHandle = typename FConstraints::FConstraintContainerHandle;
		using FConstraintList = TArray<FConstraintContainerHandle*>;

		TPBDConstraintIslandRule(FConstraints& InConstraints, int32 InPriority = 0)
			: TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>(InConstraints, InPriority)
		{
		}

		virtual void ApplyConstraints(const FReal Dt, int32 Island, const int32 It, const int32 NumIts) override
		{
			if (IslandConstraintLists[Island].Num())
			{
				Constraints.Apply(Dt, GetIslandConstraints(Island), It, NumIts);
			}
		}

		virtual bool ApplyPushOut(const FReal Dt, int32 Island, const int32 It, const int32 NumIts) override
		{
			if (IslandConstraintLists[Island].Num())
			{
				return Constraints.ApplyPushOut(Dt, GetIslandConstraints(Island), It, NumIts);
			}
			return false;
		}

		virtual void InitializeAccelerationStructures() override
		{
			IslandConstraintLists.SetNum(ConstraintGraph->NumIslands());
			for (FConstraintList& IslandConstraintList : IslandConstraintLists)
			{
				IslandConstraintList.Reset();
			}
		}
		
		virtual void UpdateAccelerationStructures(const int32 Island) override
		{
			const TArray<int32>& ConstraintDataIndices = ConstraintGraph->GetIslandConstraintData(Island);
			FConstraintList& IslandConstraintList = IslandConstraintLists[Island];
			IslandConstraintList.Reset();
			IslandConstraintList.Reserve(ConstraintDataIndices.Num());
			for (int32 ConstraintDataIndex : ConstraintDataIndices)
			{
				const typename FPBDConstraintGraph::FConstraintData& ConstraintData = ConstraintGraph->GetConstraintData(ConstraintDataIndex);
				if (ConstraintData.GetContainerId() == ContainerId)
				{
					IslandConstraintList.Add(ConstraintData.GetConstraintHandle()->As<FConstraintContainerHandle>());
				}
			}
		}

		template<typename TVisitor>
		void VisitIslandConstraints(const int32 Island, const TVisitor& Visitor) const
		{
			Visitor(GetIslandConstraints(Island));
		}

	private:
		using Base::Constraints;
		using Base::ConstraintGraph;
		using Base::ContainerId;

		const TArray<FConstraintContainerHandle*>& GetIslandConstraints(int32 Island) const
		{
			// Constraint rules are bound to a single type, but the FPBDConstraintGraph works with many types. We have
			// already pre-filtered the constraint lists based on type, so this case is safe.
			return reinterpret_cast<const TArray<FConstraintContainerHandle*>&>(IslandConstraintLists[Island]);
		}

		// @todo(ccaulfield): optimize: eliminate the need for this index list - it is a subset of EdgeData. 
		// If EdgeData were sorted by ContainerId we could use TArray<TArrayView<int32>>...
		TArray<FConstraintList> IslandConstraintLists;
	};

	/**
	 * Level- and Color-based constraint rule. 
	 * Constraints of the same color are non-interacting and can therefore be processed in parallel. 
	 * The level is used to implement shock propagation: constraints of lower levels are frozen in 
	 * place as far as higher-level constraints are concerned.
	 */
	template<typename T_CONSTRAINTS>
	class CHAOS_API TPBDConstraintColorRule : public TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>
	{
		typedef TPBDConstraintGraphRuleImpl<T_CONSTRAINTS> Base;

	public:
		using FConstraints = T_CONSTRAINTS;

		TPBDConstraintColorRule(FConstraints& InConstraints, const int32 InPushOutIterations, int32 InPriority = 0)
			: TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>(InConstraints, InPriority)
			, PushOutIterations(InPushOutIterations)
		{
		}


		virtual void UpdatePositionBasedState(const FReal Dt) override
		{
			Constraints.UpdatePositionBasedState(Dt);
		}

		virtual void ApplyConstraints(const FReal Dt, int32 Island, const int32 It, const int32 NumIts) override
		{
			const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = GraphColor.GetIslandLevelToColorToConstraintListMap(Island);
			int32 MaxColor = GraphColor.GetIslandMaxColor(Island);
			int32 MaxLevel = GraphColor.GetIslandMaxLevel(Island);
			for (int32 Level = 0; Level <= MaxLevel; ++Level)
			{
				for (int32 Color = 0; Color <= MaxColor; ++Color)
				{
					if (LevelToColorToConstraintListMap[Level].Contains(Color) && LevelToColorToConstraintListMap[Level][Color].Num())
					{
						const TArray<typename FConstraints::FConstraintContainerHandle*>& ConstraintHandles = GetLevelColorConstraints(LevelToColorToConstraintListMap, Level, Color);
						Constraints.Apply(Dt, ConstraintHandles, It, NumIts);
					}
				}
			}
		}

		virtual void RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& InConstraints)
		{
			Constraints.RemoveConstraints(InConstraints);
		}

		virtual bool ApplyPushOut(const FReal Dt, int32 Island, const int32 It, const int32 NumIts) override
		{
			const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = GraphColor.GetIslandLevelToColorToConstraintListMap(Island);
			int32 MaxColor = GraphColor.GetIslandMaxColor(Island);
			int32 MaxLevel = GraphColor.GetIslandMaxLevel(Island);

			TSet<const TGeometryParticleHandle<FReal, 3>*> IsTemporarilyStatic;
			bool bNeedsAnotherIteration = false;
			for (int32 Level = 0; Level <= MaxLevel; ++Level)
			{
				for (int32 Color = 0; Color <= MaxColor; ++Color)
				{
					if (LevelToColorToConstraintListMap[Level].Contains(Color) && LevelToColorToConstraintListMap[Level][Color].Num())
					{
						const TArray<typename FConstraints::FConstraintContainerHandle*>& ConstraintHandles = GetLevelColorConstraints(LevelToColorToConstraintListMap, Level, Color);
						if (Constraints.ApplyPushOut(Dt, ConstraintHandles, IsTemporarilyStatic, It, NumIts))
						{
							bNeedsAnotherIteration = true;
						}
					}
				}

				// @todo(ccaulfield): Move shock propagation out of color rule
#if USE_SHOCK_PROPOGATION
				for (int32 Color = 0; Color <= MaxColor; ++Color)
				{
					if (LevelToColorToConstraintListMap[Level].Contains(Color))
					{
						const TArray<typename FConstraints::FConstraintContainerHandle*>& ConstraintHandles = GetLevelColorConstraints(LevelToColorToConstraintListMap, Level, Color);

						for (int32 Edge = 0; Edge < ConstraintHandles.Num(); ++Edge)
						{
							const typename FConstraints::FConstraintContainerHandle* Handle = ConstraintHandles[Edge];
							TVector<const TGeometryParticleHandle<FReal, 3>*, 2> Particles = Handle->GetConstrainedParticles();
							if (It == NumIts - 1)
							{
								const bool bIsParticleDynamic0 = Particles[0]->CastToRigidParticle() && Particles[0]->ObjectState() == EObjectStateType::Dynamic;
								const bool bIsParticleDynamic1 = Particles[1]->CastToRigidParticle() && Particles[1]->ObjectState() == EObjectStateType::Dynamic;

								if (bIsParticleDynamic0 == false || IsTemporarilyStatic.Contains(Particles[0]))
								{
									IsTemporarilyStatic.Add(Particles[1]);
								}
								else if (bIsParticleDynamic1 == false || IsTemporarilyStatic.Contains(Particles[1]))
								{
									IsTemporarilyStatic.Add(Particles[0]);
								}
							}
						}
					}
				}
#endif
			}

			return bNeedsAnotherIteration;
		}

		virtual void InitializeAccelerationStructures() override
		{
			GraphColor.InitializeColor(*ConstraintGraph);
		}

		virtual void UpdateAccelerationStructures(const int32 Island) override
		{
			GraphColor.ComputeColor(Island, *ConstraintGraph, ContainerId);
		}

		void SetPushOutIterations(const int32 InPushOutIterations)
		{
			PushOutIterations = InPushOutIterations;
		}

		template<typename TVisitor>
		void VisitIslandConstraints(const int32 Island, const TVisitor& Visitor) const
		{
			const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = GraphColor.GetIslandLevelToColorToConstraintListMap(Island);
			int32 MaxColor = GraphColor.GetIslandMaxColor(Island);
			int32 MaxLevel = GraphColor.GetIslandMaxLevel(Island);
			for (int32 Level = 0; Level <= MaxLevel; ++Level)
			{
				for (int32 Color = 0; Color <= MaxColor; ++Color)
				{
					if (LevelToColorToConstraintListMap[Level].Contains(Color) && LevelToColorToConstraintListMap[Level][Color].Num())
					{
						const TArray<typename FConstraints::FConstraintContainerHandle*>& ConstraintHandles = GetLevelColorConstraints(LevelToColorToConstraintListMap, Level, Color);
						Visitor(ConstraintHandles);
					}
				}
			}
		}

	private:
		using Base::Constraints;
		using Base::ConstraintGraph;
		using Base::ContainerId;

		const TArray<typename FConstraints::FConstraintContainerHandle*>& GetLevelColorConstraints(const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap, int32 Level, int32 Color) const
		{
			// FPBDConstraintColor works with any constraint type (in principle - currently only used with Collisions), but the rule is bound to a single type and so this cast is ok
			return reinterpret_cast<const TArray<typename FConstraints::FConstraintContainerHandle*>&>(LevelToColorToConstraintListMap[Level][Color]);
		}

		FPBDConstraintColor GraphColor;
		int32 PushOutIterations;
	};

}