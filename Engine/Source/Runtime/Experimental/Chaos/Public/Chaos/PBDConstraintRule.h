// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Declares.h"
#include "Chaos/PBDCollisionTypes.h"
#include "Chaos/PBDConstraintGraph.h"
#include "Chaos/PBDConstraintColor.h"

#define USE_SHOCK_PROPOGATION 1

namespace Chaos
{
	template<typename T, int d>
	class TConstraintHandle;

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

	protected:
		int32 Priority;
	};

	/**
	 * Base class for Constraint Rules that use the Contact Graph (which will be most optimized ones).
	 * The graph is shared among many/all constraint rules and is held external to the Graph rule itself.
	 * Each edge in the graph can be mapped back to a constraint controlled by the rule. To support this,
	 * each rule is assigned an ID which is stored alongside the constraint index in the graph.
	 * @see TPBDConstraintGraphRuleImpl
	 */
	template<typename T, int d>
	class CHAOS_API TPBDConstraintGraphRule : public FConstraintRule
	{
	public:
		typedef TPBDConstraintGraph<T, d> FConstraintGraph;

		TPBDConstraintGraphRule(int32 InPriority) : FConstraintRule(InPriority) {}
		virtual ~TPBDConstraintGraphRule() {}

		virtual void BindToGraph(FConstraintGraph& InContactGraph, uint32 InContainerId) {}

		/** Called once per tick to allow constraint containers to create/alter their constraints based on particle position */
		virtual void UpdatePositionBasedState(const T Dt) {}

		/** Apply all corrections for constraints in the specified island */
		virtual void ApplyConstraints(const T Dt, int32 Island, const int32 It, const int32 NumIts) {}

		/** Apply push out for constraints in the specified island */
		virtual bool ApplyPushOut(const T Dt, int32 Island, const int32 It, const int32 NumIts) { return false; }

		/** Add all constraints to the connectivity graph */
		virtual void AddToGraph() {}

		/** Initialize and performance-acceleration structures from the contact graph. Called once per evolution update */
		virtual void InitializeAccelerationStructures() {}

		/** Set up the perf-acceleration structures for the specified island. May be called in parallel for islands */
		virtual void UpdateAccelerationStructures(const int32 Island) {}

		/** Remove all constraints associated with the specified particles */
		// @todo(ccaulfield): remove uint version
		virtual void RemoveConstraints(const TSet<TGeometryParticleHandle<T,d>*>& RemovedParticles) { }

		/** The number of constraints in the collection */
		virtual int32 NumConstraints() const { return 0; }
	};


	/**
	 * ConstraintGraphRule helper base class - templatized on Constraint Container.
	 */
	template<typename T_CONSTRAINTS, typename T, int d>
	class CHAOS_API TPBDConstraintGraphRuleImpl : public TPBDConstraintGraphRule<T, d>
	{
		typedef TPBDConstraintGraphRule<T, d> Base;
	public:
		typedef T_CONSTRAINTS FConstraints;
		typedef TPBDConstraintGraph<T, d> FConstraintGraph;

		TPBDConstraintGraphRuleImpl(FConstraints& InConstraints, int32 InPriority)
			: TPBDConstraintGraphRule<T, d>(InPriority)
			, Constraints(InConstraints)
			, ConstraintGraph(nullptr)
		{
		}

		virtual void BindToGraph(FConstraintGraph& InContactGraph, uint32 InContainerId) override
		{
			ConstraintGraph = &InContactGraph;
			ContainerId = InContainerId;
		}

		virtual void UpdatePositionBasedState(const T Dt) override
		{
			Constraints.UpdatePositionBasedState(Dt);
		}

		virtual void AddToGraph() override
		{
			ConstraintGraph->ReserveConstraints(Constraints.NumConstraints());
			for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.NumConstraints(); ++ConstraintIndex)
			{
				ConstraintGraph->AddConstraint(ContainerId, Constraints.GetConstraintHandle(ConstraintIndex), Constraints.GetConstrainedParticles(ConstraintIndex));
			}
		}

		virtual int32 NumConstraints() const override { return Constraints.NumConstraints(); }

	protected:
		FConstraints& Constraints;
		FConstraintGraph* ConstraintGraph;
		uint32 ContainerId;
	};

	/**
	 * Island-based constraint rule. All constraints in an island are updated in single-threaded a loop. Islands may be updated in parallel.
	 */
	template<typename T_CONSTRAINTS, typename T, int d>
	class CHAOS_API TPBDConstraintIslandRule : public TPBDConstraintGraphRuleImpl<T_CONSTRAINTS, T, d>
	{
		typedef TPBDConstraintGraphRuleImpl<T_CONSTRAINTS, T, d> Base;

	public:
		using FConstraints = typename Base::FConstraints;
		using FConstraintHandle = typename FConstraints::FConstraintHandle;
		using FConstraintList = TArray<TConstraintHandle<T, d>*>;
		using FConstraintGraph = typename Base::FConstraintGraph;

		TPBDConstraintIslandRule(FConstraints& InConstraints, int32 InPriority = 0)
			: TPBDConstraintGraphRuleImpl<T_CONSTRAINTS, T, d>(InConstraints, InPriority)
		{
		}

		virtual void ApplyConstraints(const T Dt, int32 Island, const int32 It, const int32 NumIts) override
		{
			if (IslandConstraintLists[Island].Num())
			{
				Constraints.Apply(Dt, GetIslandConstraints(Island), It, NumIts);
			}
		}

		virtual bool ApplyPushOut(const T Dt, int32 Island, const int32 It, const int32 NumIts) override
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
				const typename FConstraintGraph::FConstraintData& ConstraintData = ConstraintGraph->GetConstraintData(ConstraintDataIndex);
				if (ConstraintData.ContainerId == ContainerId)
				{
					IslandConstraintList.Add(ConstraintData.ConstraintHandle);
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

		const TArray<FConstraintHandle*>& GetIslandConstraints(int32 Island) const
		{
			// Constraint rules are bound to a single type, but the FConstraintGraph works with many types. We have
			// already pre-filtered the constraint lists based on type, so this case is safe.
			return reinterpret_cast<const TArray<FConstraintHandle*>&>(IslandConstraintLists[Island]);
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
	template<typename T_CONSTRAINTS, typename T, int d>
	class CHAOS_API TPBDConstraintColorRule : public TPBDConstraintGraphRuleImpl<T_CONSTRAINTS, T, d>
	{
		typedef TPBDConstraintGraphRuleImpl<T_CONSTRAINTS, T, d> Base;

	public:
		typedef typename Base::FConstraints FConstraints;
		typedef typename Base::FConstraintGraph FConstraintGraph;
		typedef TPBDConstraintColor<T, d> FConstraintColor;

		TPBDConstraintColorRule(FConstraints& InConstraints, const int32 InPushOutIterations, int32 InPriority = 0)
			: TPBDConstraintGraphRuleImpl<T_CONSTRAINTS, T, d>(InConstraints, InPriority)
			, PushOutIterations(InPushOutIterations)
		{
		}


		virtual void UpdatePositionBasedState(const T Dt) override
		{
			Constraints.UpdatePositionBasedState(Dt);
		}

		virtual void ApplyConstraints(const T Dt, int32 Island, const int32 It, const int32 NumIts) override
		{
			const typename FConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = GraphColor.GetIslandLevelToColorToConstraintListMap(Island);
			int32 MaxColor = GraphColor.GetIslandMaxColor(Island);
			int32 MaxLevel = GraphColor.GetIslandMaxLevel(Island);
			for (int32 Level = 0; Level <= MaxLevel; ++Level)
			{
				for (int32 Color = 0; Color <= MaxColor; ++Color)
				{
					if (LevelToColorToConstraintListMap[Level].Contains(Color) && LevelToColorToConstraintListMap[Level][Color].Num())
					{
						const TArray<typename FConstraints::FConstraintHandle*>& ConstraintHandles = GetLevelColorConstraints(LevelToColorToConstraintListMap, Level, Color);
						Constraints.Apply(Dt, ConstraintHandles, It, NumIts);
					}
				}
			}
		}

		virtual void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& InConstraints)
		{
			Constraints.RemoveConstraints(InConstraints);
		}

		virtual bool ApplyPushOut(const T Dt, int32 Island, const int32 It, const int32 NumIts) override
		{
			const typename FConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = GraphColor.GetIslandLevelToColorToConstraintListMap(Island);
			int32 MaxColor = GraphColor.GetIslandMaxColor(Island);
			int32 MaxLevel = GraphColor.GetIslandMaxLevel(Island);

			TSet<TGeometryParticleHandle<T, d>*> IsTemporarilyStatic;
			bool bNeedsAnotherIteration = false;
			for (int32 Level = 0; Level <= MaxLevel; ++Level)
			{
				for (int32 Color = 0; Color <= MaxColor; ++Color)
				{
					if (LevelToColorToConstraintListMap[Level].Contains(Color) && LevelToColorToConstraintListMap[Level][Color].Num())
					{
						const TArray<typename FConstraints::FConstraintHandle*>& ConstraintHandles = GetLevelColorConstraints(LevelToColorToConstraintListMap, Level, Color);
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
						for (int32 Edge = 0; Edge < LevelToColorToConstraintListMap[Level][Color].Num(); ++Edge)
						{
							const int32 ConstraintIndex = LevelToColorToConstraintListMap[Level][Color][Edge]->GetConstraintIndex();
							const TVector<TGeometryParticleHandle<T,d>*, 2> Particles = Constraints.ConstraintParticles(ConstraintIndex);
							if (It == NumIts - 1)
							{
								if (Particles[0]->AsDynamic() == nullptr || IsTemporarilyStatic.Contains(Particles[0]))
								{
									IsTemporarilyStatic.Add(Particles[1]);
								}
								else if (Particles[1]->AsDynamic() == nullptr || IsTemporarilyStatic.Contains(Particles[1]))
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
			const typename FConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = GraphColor.GetIslandLevelToColorToConstraintListMap(Island);
			int32 MaxColor = GraphColor.GetIslandMaxColor(Island);
			int32 MaxLevel = GraphColor.GetIslandMaxLevel(Island);
			for (int32 Level = 0; Level <= MaxLevel; ++Level)
			{
				for (int32 Color = 0; Color <= MaxColor; ++Color)
				{
					if (LevelToColorToConstraintListMap[Level].Contains(Color) && LevelToColorToConstraintListMap[Level][Color].Num())
					{
						const TArray<typename FConstraints::FConstraintHandle*>& ConstraintHandles = GetLevelColorConstraints(LevelToColorToConstraintListMap, Level, Color);
						Visitor(ConstraintHandles);
					}
				}
			}
		}

	private:
		using Base::Constraints;
		using Base::ConstraintGraph;
		using Base::ContainerId;

		const TArray<typename FConstraints::FConstraintHandle*>& GetLevelColorConstraints(const typename FConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap, int32 Level, int32 Color) const
		{
			// FConstraintColor works with any constraint type (in principle - currently only used with Collisions), but the rule is bound to a single type and so this cast is ok
			return reinterpret_cast<const TArray<typename FConstraints::FConstraintHandle*>&>(LevelToColorToConstraintListMap[Level][Color]);
		}

		FConstraintColor GraphColor;
		int32 PushOutIterations;
	};

	/**
	 * Simplify creation of constraint rules thanks to template parameter deduction.
	 * 
	 * E.g., it allows you to write code like this:
	 *		TPBDCollisionConstraints<float, 3> Constraints;
	 *		auto Rule = FConstraintRuleFactory::CreateIslandRule(Constraints);
	 *
	 * as opposed to 
	 *		TPBDCollisionConstraints<float, 3> Constraints;
	 *		auto Rule = TPBDConstraintIslandRule<TPBDCollisionConstraints<float, 3>, float, 3>(Constraints);
	 *
	 * @todo(ccaulfield): The evolution classes should be factories of Constraint Rules since they own the 
	 * update loop and therefore know what algorithms should be applied.
	 */
	template<typename T, int d>
	struct CHAOS_API TConstraintRuleFactory
	{
	public:
		typedef TPBDConstraintGraph<T, d> FConstraintGraph;

		template<typename T_CONSTRAINTS>
		static TPBDConstraintIslandRule<T_CONSTRAINTS, T, d> CreateIslandRule(T_CONSTRAINTS& Constraints, const int32 Priority = 0)
		{
			return TPBDConstraintIslandRule<T_CONSTRAINTS, T, d>(Constraints, Priority);
		}

		template<typename T_CONSTRAINTS>
		static TPBDConstraintColorRule<T_CONSTRAINTS, T, d> CreateColorRule(T_CONSTRAINTS& Constraints, const int32 InPushOutIterations, const int32 Priority = 0)
		{
			return TPBDConstraintColorRule<T_CONSTRAINTS, T, d>(Constraints, InPushOutIterations, Priority);
		}
	};

}