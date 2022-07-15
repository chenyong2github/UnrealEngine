// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Declares.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/PBDConstraintColor.h"
#include "Chaos/PBDConstraintGraph.h"

// Only used for Mac/Linux forward decl at the bottom - should be removed as per the comment there (we get a warning if the include is also at the bottom)
#include "Chaos/PBDCollisionConstraints.h"


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

		/** Called once per frame. Can be used to prepare caches etc. */
		virtual void PrepareTick() {}

		/** Called once per frame. Should undo whatever is done in PrepareTick (can also free any other transient buffers created after) */
		virtual void UnprepareTick() {}


	protected:
		int32 Priority;
	};

	/**
	 * Constraint rule for evolutions that do not use Constraint Graphs or other acceleration schemes.
	 */
	class CHAOS_API FSimpleConstraintRule : public FConstraintRule
	{
	public:
		FSimpleConstraintRule(int32 InPriority) : FConstraintRule(InPriority), SolverData(nullptr) {}

		/** Bind the solver datas to the one in the evolution */
		virtual void BindToDatas(FPBDIslandSolverData& InSolverDatas, const uint32 InContainerId) {}

		virtual void UpdatePositionBasedState(const FReal Dt) {}
		virtual void GatherSolverInput(const FReal Dt) {}
		virtual void ScatterSolverOutput(const FReal Dt) {}
		virtual bool ApplyConstraints(const FReal Dt, const int32 It, const int32 NumIts) { return false; }
		virtual bool ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts) { return false; }
		virtual bool ApplyProjection(const FReal Dt, const int32 It, const int32 NumIts) { return false; }
	protected:
		/** Solver datas that are coming from the evolution */
		FPBDIslandSolverData* SolverData = nullptr;
	};

	template<typename ConstraintType>
	class CHAOS_API TSimpleConstraintRule : public FSimpleConstraintRule
	{
	public:
		using FConstraints = ConstraintType;

		TSimpleConstraintRule(int32 InPriority, FConstraints& InConstraints);

		virtual ~TSimpleConstraintRule();

		/** Bind the solver datas to the one in the evolution */
		virtual void BindToDatas(FPBDIslandSolverData& InSolverDatas, const uint32 InContainerId) override;

		virtual void PrepareTick() override;

		virtual void UnprepareTick() override;

		virtual void UpdatePositionBasedState(const FReal Dt) override;

		virtual void GatherSolverInput(const FReal Dt) override;

		virtual void ScatterSolverOutput(const FReal Dt) override;

		virtual bool ApplyConstraints(const FReal Dt, const int32 It, const int32 NumIts) override;

		virtual bool ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts) override;

		virtual bool ApplyProjection(const FReal Dt, const int32 It, const int32 NumIts) override;

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

		// Collect all the data required to solve the constraints in the specified island. This also fills the SolverBodies
		virtual void GatherSolverInput(const FReal Dt, int32 GroupIndex) {}

		// Scatter the results of the islands constraint solver(s) out to the appropriate places (e.g., impulses, break flags, etc)
		virtual void ScatterSolverOutput(const FReal Dt, int32 GroupIndex) {}

		/** Called once per tick to allow constraint containers to create/alter their constraints based on particle position */
		virtual void UpdatePositionBasedState(const FReal Dt) {}

		/** Apply all corrections for constraints in the specified island. Return true if more iterations are needed. */
		virtual bool ApplyConstraints(const FReal Dt, int32 GroupIndex, const int32 It, const int32 NumIts) { return false; }

		/** Apply push out for constraints in the specified island. Return true if more iterations are needed. */
		virtual bool ApplyPushOut(const FReal Dt, int32 GroupIndex, const int32 It, const int32 NumIts) { return false; }

		/** Apply push out for constraints in the specified island. Return true if more iterations are needed. */
		virtual bool ApplyProjection(const FReal Dt, int32 GroupIndex, const int32 It, const int32 NumIts) { return false; }

		/** Add all constraints to the connectivity graph */
		virtual void AddToGraph() {}

		/** Initialize and performance-acceleration structures from the contact graph. Called once per evolution update */
		virtual void InitializeAccelerationStructures() {}

		/** Set up the perf-acceleration structures for the specified island. May be called in parallel for islands */
		virtual void UpdateAccelerationStructures(const FReal Dt, const int32 GroupIndex) {}

		/** Sort constraints if necessary  */
		virtual void SortConstraints() {}

		/** Boolean to check if we need to sort the constraints */
		virtual bool IsSortingEnabled() const {return false;}

		virtual void SetUseContactGraph(const bool InUseContactGraph) {}

		/** Change enabled state on all constraints associated with the specified particles */
		inline void SetConstraintsEnabled(FGeometryParticleHandle* ParticleHandle, bool bInEnabled)
		{
			for (FConstraintHandle* Constraint : ParticleHandle->ParticleConstraints())
			{
				if (Constraint->IsEnabled() != bInEnabled)
				{
					Constraint->SetEnabled(bInEnabled);
				}
			}
		}

		inline void SetConstraintsEnabled(const TSet<TGeometryParticleHandle<FReal, 3>*>& ParticleHandles, bool bInEnabled)
		{ 
			for (TGeometryParticleHandle<FReal, 3>* ParticleHandle : ParticleHandles)
			{
				SetConstraintsEnabled(ParticleHandle, bInEnabled);
			}
		}

		/** Disconnect all constraints associated with the specified particles */
		virtual void DisconnectConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles) {  }

		/** Remove all constraints */
		virtual void ResetConstraints() {}

		/** The number of constraints in the collection */
		virtual int32 NumConstraints() const { return 0; }
	};


	/**
	 * ConstraintGraphRule helper base class - templatized on Constraint Container.
	 */
	template<typename ConstraintType>
	class CHAOS_API TPBDConstraintGraphRuleImpl : public FPBDConstraintGraphRule
	{
	public:
		typedef ConstraintType FConstraints;

		TPBDConstraintGraphRuleImpl(FConstraints& InConstraints, int32 InPriority);

		virtual void PrepareTick() override
		{
			Constraints.PrepareTick();
		}

		virtual void UnprepareTick() override
		{
			Constraints.UnprepareTick();
		}

		virtual void BindToGraph(FPBDConstraintGraph& InContactGraph, uint32 InContainerId) override;

		virtual void UpdatePositionBasedState(const FReal Dt) override;

		virtual void AddToGraph() override;

		/** Disconnect all constraints associated with the specified particles */
		virtual void DisconnectConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles)
		{
			Constraints.DisconnectConstraints(RemovedParticles);
		}

		virtual int32 NumConstraints() const override;

		int32 GetContainerId() const
		{
			return Constraints.GetContainerId();
		}

	protected:
		FConstraints& Constraints;
		FPBDConstraintGraph* ConstraintGraph;
	};

	/**
	 * Island-based constraint rule. All constraints in an island are updated in single-threaded a loop. Islands may be updated in parallel.
	 */
	template<typename ConstraintType>
	class CHAOS_API TPBDConstraintIslandRule : public TPBDConstraintGraphRuleImpl<ConstraintType>
	{
	protected:
		typedef TPBDConstraintGraphRuleImpl<ConstraintType> Base;

	public:
		using FConstraints = ConstraintType;
		using FConstraintContainerHandle = typename FConstraints::FConstraintContainerHandle;
		using FConstraintList = TArray<FConstraintContainerHandle*>;

		using Base::GetContainerId;

		TPBDConstraintIslandRule(FConstraints& InConstraints, int32 InPriority = 0);

		virtual ~TPBDConstraintIslandRule();

		virtual void GatherSolverInput(const FReal Dt, int32 GroupIndex) override;

		virtual void ScatterSolverOutput(const FReal Dt, int32 GroupIndex) override;

		virtual bool ApplyConstraints(const FReal Dt, int32 GroupIndex, const int32 It, const int32 NumIts) override;

		virtual bool ApplyPushOut(const FReal Dt, int32 GroupIndex, const int32 It, const int32 NumIts) override;

		virtual bool ApplyProjection(const FReal Dt, int32 GroupIndex, const int32 It, const int32 NumIts) override;

		virtual void InitializeAccelerationStructures() override;
		
		virtual void UpdateAccelerationStructures(const FReal Dt, const int32 GroupIndex) override;

	protected:
		using Base::Constraints;
		using Base::ConstraintGraph;
	};

	/**
	 * Level- and Color-based constraint rule. 
	 * Constraints of the same color are non-interacting and can therefore be processed in parallel. 
	 * The level is used to implement shock propagation: constraints of lower levels are frozen in 
	 * place as far as higher-level constraints are concerned.
	 */
	template<typename ConstraintType>
	class CHAOS_API TPBDConstraintColorRule : public TPBDConstraintIslandRule<ConstraintType>
	{
		typedef TPBDConstraintIslandRule<ConstraintType> Base;

	public:
		using FConstraints = ConstraintType;
		using FConstraintContainerHandle = typename FConstraints::FConstraintContainerHandle;

		TPBDConstraintColorRule(FConstraints& InConstraints, int32 InPriority = 0);

		virtual ~TPBDConstraintColorRule();

		virtual void UpdatePositionBasedState(const FReal Dt) override
		{
			Constraints.UpdatePositionBasedState(Dt);
		}

		virtual void GatherSolverInput(const FReal Dt, int32 GroupIndex) override;

		virtual void ScatterSolverOutput(const FReal Dt, int32 GroupIndex) override;

		virtual bool ApplyConstraints(const FReal Dt, int32 GroupIndex, const int32 It, const int32 NumIts) override;

		virtual void RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& InConstraints)
		{
			Constraints.RemoveConstraints(InConstraints);
		}

		virtual void ResetConstraints()
		{
			Constraints.Reset();
		}

		virtual bool ApplyPushOut(const FReal Dt, int32 GroupIndex, const int32 It, const int32 NumIts) override;

		virtual void InitializeAccelerationStructures() override;

		virtual void UpdateAccelerationStructures(const FReal Dt, const int32 GroupIndex) override;

		/** Sort constraints according to island/level/color*/
		virtual void SortConstraints() override;
		
		/** Boolean to check if we need to sort the constraints*/
		virtual bool IsSortingEnabled() const override;

		virtual void SetUseContactGraph(const bool bInUseContactGraph) override;

	private:
		using Base::Base::Constraints;
		using Base::Base::ConstraintGraph;

		/** Check if sorting is using levels*/
		bool IsSortingUsingColors() const;

		/** Check if sorting is using colors */
		bool IsSortingUsingLevels() const;

		/** Compute island levels if necessary */
		void ComputeLevels();

		/** Compute island colors if necessary */
		void ComputeColors();

		/** Populate the sorted constraints list based on island/level/color */
		void PopulateConstraints();
		
		/** Loop over all the edges and apply a function */
		void ForEachEdges(TFunctionRef<void(const int32, const FPBDConstraintGraph::GraphType::FGraphEdge&)> InFunction);
		
		// Each array entry contains the [begin, end) index of a set of independent constraints
		// that can be solved in parallel. The sets are ordered by level and must be solved sequentially.
		TArray<TArray<TPair<int32, int32>>> ConstraintSets;

		// Sorted constraint handles
		TArray<FConstraintContainerHandle*> SortedConstraints;

		// Constraint offsets into the sorted handles list for a given tuple island/level/color
		TArray<int32> ConstraintOffsets;

		// Island offsets into the constraint offsets 
		TArray<int32> IslandOffsets;

		// Counters to know at which position after the island/level/color constraint offset the handle will be inserted
		TArray<int32> OffsetCounters;
	};

}
