// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include "Chaos/PBDConstraintColor.h"
#include "Chaos/PBDConstraintGraph.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Utilities.h"
#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"

namespace ChaosTest {

	using namespace Chaos;

	template<int32 T_TYPEID>
	class TMockGraphConstraints;


	template<int32 T_TYPEID>
	class TMockGraphConstraintHandle : public FConstraintHandle
	{
	public:
		TMockGraphConstraintHandle(TMockGraphConstraints<T_TYPEID>* InConstraintContainer, int32 ConstraintIndex)
			: FConstraintHandle(FConstraintHandle::EType::Invalid, ConstraintIndex)
			, ConstraintContainer(InConstraintContainer)
		{
		}

		virtual void SetEnabled(bool InEnabled) {};
		virtual bool IsEnabled() const { return true; };

		TMockGraphConstraints<T_TYPEID>* ConstraintContainer;
	};

	/**
	 * Constraint Container with minimal API required to test the Graph.
	 * We can pretend we have many constraint containers of different types
	 * by using containers with different T_TYPEIDs.
	 */
	template<int32 T_TYPEID>
	class TMockGraphConstraints
	{
	public:
		using FConstraintContainerHandle = TMockGraphConstraintHandle<T_TYPEID>;
		struct FMockConstraint
		{
			TVec2<int32> ConstrainedParticles;
		};

		int32 NumConstraints() const { return Constraints.Num(); }

		TVec2<int32> ConstraintParticleIndices(const int32 ConstraintIndex) const
		{
			return Constraints[ConstraintIndex].ConstrainedParticles;
		}

		void AddConstraint(const TVec2<int32>& InConstraintedParticles)
		{
			Constraints.Emplace(FMockConstraint({ InConstraintedParticles }));
			Handles.Emplace(HandleAllocator.AllocHandle(this, Handles.Num()));
		}

		bool AreConstraintsIndependent(const TPBDRigidParticles<FReal, 3>& InParticles, const TArray<FConstraintHandle*>& InConstraintHandles)
		{
			bool bIsValid = true;
			TSet<int32> ParticleSet;
			for (FConstraintHandle* ConstraintHandle : InConstraintHandles)
			{
				FMockConstraint& Constraint = Constraints[ConstraintHandle->GetConstraintIndex()];
				if (ParticleSet.Contains(Constraint.ConstrainedParticles[0]) && !InParticles.HasInfiniteMass(Constraint.ConstrainedParticles[0]))
				{
					bIsValid = false;
				}
				if (ParticleSet.Contains(Constraint.ConstrainedParticles[1]) && !InParticles.HasInfiniteMass(Constraint.ConstrainedParticles[1]))
				{
					bIsValid = false;
				}
				ParticleSet.Add(Constraint.ConstrainedParticles[0]);
				ParticleSet.Add(Constraint.ConstrainedParticles[1]);
			}
			return bIsValid;
		}

		bool AreConstraintsIndependent(const TArray<TGeometryParticleHandle<FReal, 3>*>& Particles, const TArray<FConstraintHandle*>& InConstraintHandles)
		{
			bool bIsValid = true;
			TSet<TGeometryParticleHandle<FReal, 3>*> ParticleSet;
			for (FConstraintHandle* ConstraintHandle : InConstraintHandles)
			{
				FMockConstraint& Constraint = Constraints[ConstraintHandle->GetConstraintIndex()];
				TGeometryParticleHandle<FReal, 3>* Particle0 = Particles[Constraint.ConstrainedParticles[0]];
				if (ParticleSet.Contains(Particle0) && Particle0->CastToRigidParticle() && Particle0->ObjectState() == EObjectStateType::Dynamic)
				{
					bIsValid = false;
				}

				TGeometryParticleHandle<FReal, 3>* Particle1 = Particles[Constraint.ConstrainedParticles[1]];
				if (ParticleSet.Contains(Particle1) && Particle1->CastToRigidParticle() && Particle1->ObjectState() == EObjectStateType::Dynamic)
				{
					bIsValid = false;
				}
				ParticleSet.Add(Particle0);
				ParticleSet.Add(Particle1);
			}
			return bIsValid;
		}


		TArray<FMockConstraint> Constraints;
		TArray<FConstraintContainerHandle*> Handles;
		TConstraintHandleAllocator<TMockGraphConstraints<T_TYPEID>> HandleAllocator;

	};
	template class TMockGraphConstraints<0>;
	template class TMockGraphConstraints<1>;

	void GraphIslands()
	{
		// Create some dynamic particles - doesn't matter what position or other state they have
		TArray<TGeometryParticleHandle<FReal, 3>*> AllParticles;

		FPBDRigidsSOAs SOAs;
		TArray<TPBDRigidParticleHandle<FReal,3>*> Dynamics = SOAs.CreateDynamicParticles(17);
		for (auto Dyn : Dynamics) { AllParticles.Add(Dyn);}

		// Make a static particle. Islands should not merge across these.
		TArray<TGeometryParticleHandle<FReal, 3>*> Statics = SOAs.CreateStaticParticles(1);
		AllParticles.Append(Statics);

		Dynamics = SOAs.CreateDynamicParticles(3);
		for (auto Dyn : Dynamics) { AllParticles.Add(Dyn);}
		
		// Create some constraints between the particles
		TArray<TVec2<int32>> ConstrainedParticles0 =
		{
			//
			{0, 1},
			{0, 2},
			{0, 3},
			{3, 4},
			{3, 5},
			{6, 4},
			//
			{8, 7},
			{8, 9},
			//
			{13, 18},
			//
			{20, 17},
		};

		TArray<TVec2<int32>> ConstrainedParticles1 =
		{
			//
			{0, 1},
			{2, 1},
			//
			{9, 10},
			{11, 10},
			{11, 13},
			//
			{14, 15},
			{16, 14},
			{17, 14},
		};

		TMockGraphConstraints<0> ConstraintsOfType0;
		for (const auto& ConstrainedParticles : ConstrainedParticles0)
		{
			ConstraintsOfType0.AddConstraint(ConstrainedParticles);
		}

		TMockGraphConstraints<1> ConstraintsOfType1;
		for (const auto& ConstrainedParticles : ConstrainedParticles1)
		{
			ConstraintsOfType0.AddConstraint(ConstrainedParticles);
		}

		// Set up the particle graph
		FPBDConstraintGraph Graph;
		Graph.InitializeGraph(SOAs.GetNonDisabledView());

		Graph.ReserveConstraints(ConstraintsOfType0.NumConstraints());
		for (int32 ConstraintIndex = 0; ConstraintIndex < ConstraintsOfType0.NumConstraints(); ++ConstraintIndex)
		{
			TVec2<int32> Indices = ConstraintsOfType0.ConstraintParticleIndices(ConstraintIndex);
			Graph.AddConstraint(0, ConstraintsOfType0.Handles[ConstraintIndex], TVec2<TGeometryParticleHandle<FReal, 3>*>(AllParticles[Indices[0]], AllParticles[Indices[1]]));
		}

		Graph.ReserveConstraints(ConstraintsOfType1.NumConstraints());
		for (int32 ConstraintIndex = 0; ConstraintIndex < ConstraintsOfType1.NumConstraints(); ++ConstraintIndex)
		{
			TVec2<int32> Indices = ConstraintsOfType1.ConstraintParticleIndices(ConstraintIndex);
			Graph.AddConstraint(1, ConstraintsOfType1.Handles[ConstraintIndex], TVec2<TGeometryParticleHandle<FReal, 3>*>(AllParticles[Indices[0]], AllParticles[Indices[1]]));
		}
		
		// Generate the constraint/particle islands
		Graph.UpdateIslands(SOAs.GetNonDisabledDynamicView(), SOAs);
		
		// Islands should be end up with the following particles (note: particle 17 is infinite mass and can appear in multiple islands)
		TArray<TSet<TGeometryParticleHandle<FReal, 3>*>> ExpectedIslandParticles = {
			{AllParticles[0], AllParticles[1], AllParticles[2], AllParticles[3], AllParticles[4], AllParticles[5], AllParticles[6]},
			{AllParticles[7], AllParticles[8], AllParticles[9], AllParticles[10], AllParticles[11], AllParticles[13], AllParticles[18]},
			{AllParticles[12]},
			{AllParticles[14], AllParticles[15], AllParticles[16], AllParticles[17]},
			{AllParticles[19]},
			{AllParticles[17], AllParticles[20]},
		};

		// Get the Island indices which map to the ExpectedIslandParticles
		const TArray<int32> CalculatedIslandIndices =
		{
			AllParticles[0]->CastToRigidParticle()->Island(),
			AllParticles[7]->CastToRigidParticle()->Island(),
			AllParticles[12]->CastToRigidParticle()->Island(),
			AllParticles[14]->CastToRigidParticle()->Island(),
			AllParticles[19]->CastToRigidParticle()->Island(),
			AllParticles[20]->CastToRigidParticle()->Island(),
		};

		// All non-static partcles should still be Active
		EXPECT_EQ(SOAs.GetActiveParticlesView().Num(), 20);
		for (auto& Particle : SOAs.GetActiveParticlesView())
		{
			EXPECT_NE(Particle.Handle(), AllParticles[17]);
		}
		
		// Each calculated island should contain the particles we expected and no others
		for (int32 ExpectedIslandIndex = 0; ExpectedIslandIndex < ExpectedIslandParticles.Num(); ++ExpectedIslandIndex)
		{
			const int32 CalculatedIslandIndex = CalculatedIslandIndices[ExpectedIslandIndex];
			const TArray<TGeometryParticleHandle<FReal, 3>*>& CalculatedIslandParticles = Graph.GetIslandParticles(CalculatedIslandIndex);

			EXPECT_EQ(CalculatedIslandParticles.Num(), ExpectedIslandParticles[ExpectedIslandIndex].Num());
			for (TGeometryParticleHandle<FReal, 3>* CalculatedIslandParticleIndex : CalculatedIslandParticles)
			{
				EXPECT_TRUE(ExpectedIslandParticles[ExpectedIslandIndex].Contains(CalculatedIslandParticleIndex));
			}
		}
	}

	void CheckIslandIntegrity(TArray<TSet<TGeometryParticleHandle<FReal,3>*>>& ExpectedIslandParticles, const TArray<int32> CalculatedIslandIndices, FPBDConstraintGraph& Graph)
	{
		for (int32 ExpectedIslandIndex = 0; ExpectedIslandIndex < ExpectedIslandParticles.Num(); ++ExpectedIslandIndex)
		{
			const int32 CalculatedIslandIndex = CalculatedIslandIndices[ExpectedIslandIndex];
			const TArray<TGeometryParticleHandle<FReal, 3>*>& CalculatedIslandParticles = Graph.GetIslandParticles(CalculatedIslandIndex);

			EXPECT_EQ(CalculatedIslandParticles.Num(), ExpectedIslandParticles[ExpectedIslandIndex].Num());
			for (TGeometryParticleHandle<FReal, 3>* CalculatedIslandParticleIndex : CalculatedIslandParticles)
			{
				EXPECT_TRUE(ExpectedIslandParticles[ExpectedIslandIndex].Contains(CalculatedIslandParticleIndex));

				auto* DynParticle = CalculatedIslandParticleIndex->CastToRigidParticle();
				if (DynParticle && DynParticle->ObjectState() == EObjectStateType::Dynamic)
				{
					EXPECT_EQ(DynParticle->Island(), CalculatedIslandIndex);
				}
			}
		}
	}

	struct FIterationData 
	{
		TArray<TVec2<int32>> ConstrainedParticles;

		TArray<TSet<int32>> ExpectedIslandParticleIndices;

		TArray<int32> ExpectedIslandEdges;

		TArray<TSet<TGeometryParticleHandle<FReal, 3>*>> ExpectedIslandParticles;

		TArray<int32> MaxLevel;
		TArray<int32> MaxColor;
	};

	void GraphIslandsPersistence()
	{
		// Create some dynamic particles - doesn't matter what position or other state they have
		TArray<TGeometryParticleHandle<FReal, 3>*> AllParticles;

		FPBDRigidsSOAs SOAs;
		TArray<TPBDRigidParticleHandle<FReal, 3>*> Dynamics = SOAs.CreateDynamicParticles(2);
		for (auto Dyn : Dynamics) { AllParticles.Add(Dyn); }

		// Make a static particle. Islands should not merge across these.
		TArray<TGeometryParticleHandle<FReal, 3>*> Statics = SOAs.CreateStaticParticles(1);
		AllParticles.Append(Statics);

		Dynamics = SOAs.CreateDynamicParticles(3);
		for (auto Dyn : Dynamics) { AllParticles.Add(Dyn); }

		//////////////////////////////////////////////////////////////////////////
		// Iteration 0
		TArray<FIterationData> IterationData;

		FIterationData Data0;
		Data0.ExpectedIslandParticleIndices = {
			{0},
			{1},
			{3},
			{4},
			{5},
		};

		Data0.ExpectedIslandEdges = {
			0,
			0,
			0,
			0,
			0
		};

		Data0.MaxLevel = { -1, -1, -1, -1, -1 };
		Data0.MaxColor = { -1, -1, -1, -1, -1 };

		IterationData.Push(Data0);

		//////////////////////////////////////////////////////////////////////////
		// Iteration 1
		FIterationData Data1;
		Data1.ConstrainedParticles = {
			//
			{0, 1},
			{1, 2},
			//
			{3,4},
			{4,5}
		};

		// Islands should be end up with the following particles
		Data1.ExpectedIslandParticleIndices = {
			{0,1,2},
			{3,4,5}
		};

		Data1.ExpectedIslandEdges = {
			2,
			2
		};

		Data1.MaxLevel = { 1, 0 };
		Data1.MaxColor = { 1, 1 };

		IterationData.Push(Data1);

		//////////////////////////////////////////////////////////////////////////
		// Iteration 2
		FIterationData Data2;
		Data2.ConstrainedParticles =
		{
			//
			{0,1},
			{1,2},
			{2,3},
			{3,4},
			{4,5}
		};

		Data2.ExpectedIslandParticleIndices = {
			{0,1,2},
			{2,3,4,5},
		};

		Data2.ExpectedIslandEdges = {
			2,
			3
		};

		Data2.MaxLevel = { 1, 2 };
		Data2.MaxColor = { 1, 1 };

		IterationData.Push(Data2);
		//////////////////////////////////////////////////////////////////////////
		// Iteration 3
		FIterationData Data3;
		Data3.ConstrainedParticles =
		{
			//
			{0,1},
			//
			{2,3},
			//
			{4,5}
		};

		Data3.ExpectedIslandParticleIndices = {
			{0,1},
			{2,3},
			{4,5}
		};

		Data3.ExpectedIslandEdges = {
			1,
			1,
			1
		};

		Data3.MaxLevel = { 0, 0, 0 };
		Data3.MaxColor = { 0, 0, 0 };

		IterationData.Push(Data3);
		//////////////////////////////////////////////////////////////////////////
		// Iteration 4
		FIterationData Data4;
		Data4.ConstrainedParticles =
		{
			//
			{0,1},
			//
			{2,3},
		};

		Data4.ExpectedIslandParticleIndices = {
			{0,1},
			{2,3},
			{4},
			{5}
		};

		Data4.ExpectedIslandEdges = {
			1,
			1,
			0,
			0
		};

		Data4.MaxLevel = { 0, 0, -1, -1 };
		Data4.MaxColor = { 0, 0, -1, -1 };

		IterationData.Push(Data4);
		//////////////////////////////////////////////////////////////////////////
		// Iteration 5
		FIterationData Data5;
		Data5.ConstrainedParticles =
		{
			//
			{0,2},
			//
			{3,2},
		};

		Data5.ExpectedIslandParticleIndices = {
			{1},
			{0,2},
			{2,3},
			{4},
			{5}
		};

		Data5.ExpectedIslandEdges = {
			0,
			1,
			1,
			0,
			0
		};

		Data5.MaxLevel = { 0, -1, 0, -1, -1 };
		Data5.MaxColor = { 0, -1, 0, -1, -1 };

		IterationData.Push(Data5);
		////////////////////////////////////////////////

		for (int Iteration = 0; Iteration < IterationData.Num(); Iteration++)
		{
			FIterationData& IterData = IterationData[Iteration];

			// convert ExpectedIslandParticleIndices to ExpectedIslandParticles for ease of later comparison
			for (int32 I = 0; I < IterData.ExpectedIslandParticleIndices.Num(); I++)
			{
				const TSet<int32>& Set = IterData.ExpectedIslandParticleIndices[I];

				TSet<TGeometryParticleHandle<FReal, 3>*> HandlesSet;
				for (int32 Index : Set)
				{
					HandlesSet.Add(AllParticles[Index]);
				}
				IterData.ExpectedIslandParticles.Add(HandlesSet);
			}
			FPBDConstraintGraph Graph;
			FPBDConstraintColor GraphColor;
			const int32 ContainerId = 0;

			// Set up the particle graph
			Graph.InitializeGraph(SOAs.GetNonDisabledView());

			// add constraints
			TMockGraphConstraints<0> ConstraintsOfType0;
			for (const auto& ConstrainedParticles : IterData.ConstrainedParticles)
			{
				ConstraintsOfType0.AddConstraint(ConstrainedParticles);
			}

			Graph.ReserveConstraints(ConstraintsOfType0.NumConstraints());
			for (int32 ConstraintIndex = 0; ConstraintIndex < ConstraintsOfType0.NumConstraints(); ++ConstraintIndex)
			{
				TVec2<int32> Indices = ConstraintsOfType0.ConstraintParticleIndices(ConstraintIndex);
				Graph.AddConstraint(ContainerId, ConstraintsOfType0.Handles[ConstraintIndex], TVec2<TGeometryParticleHandle<FReal, 3>*>(AllParticles[Indices[0]], AllParticles[Indices[1]]));
			}

			// Generate the constraint/particle islands
			Graph.UpdateIslands(SOAs.GetNonDisabledDynamicView(), SOAs);

			// Assign color to constraints
			GraphColor.InitializeColor(Graph);
			for (int32 Island = 0; Island < Graph.NumIslands(); ++Island)
			{
				GraphColor.ComputeColor(Island, Graph, ContainerId);
			}

			// get the generated island indices
			TArray<int32> CalculatedIslandIndices;
			for (TSet<int32>& EIP : IterData.ExpectedIslandParticleIndices)
			{	
				auto SetAsArray = EIP.Array();
				int32 FoundIsland = INDEX_NONE;
				for (int32 ParticleIdx : SetAsArray)
				{
					auto RigidParticle = AllParticles[ParticleIdx]->CastToRigidParticle();
					if (RigidParticle && RigidParticle->ObjectState() == EObjectStateType::Dynamic)
					{
						if (FoundIsland == INDEX_NONE)
						{
							FoundIsland = RigidParticle->Island();
							CalculatedIslandIndices.Push(FoundIsland);
						}
						else
						{
							EXPECT_EQ(FoundIsland, RigidParticle->Island());
						}
					}
				}
				check(FoundIsland != INDEX_NONE);
			}

			// check the number of edges matches what we are expecting for this island
			int Index = 0;
			for (int32 Island : CalculatedIslandIndices)
			{
				const TArray<int32>& ConstraintDataIndices = Graph.GetIslandConstraintData(Island);
				EXPECT_EQ(ConstraintDataIndices.Num(), IterData.ExpectedIslandEdges[Index++]);
			}

			CheckIslandIntegrity(IterData.ExpectedIslandParticles, CalculatedIslandIndices, Graph);

			// check level/color integrity
			TSet<FConstraintHandle*> ConstraintUnionSet;
			for (int32 Island = 0; Island < Graph.NumIslands(); ++Island)
			{
				const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = GraphColor.GetIslandLevelToColorToConstraintListMap(Island);
				const int32 MaxLevel = GraphColor.GetIslandMaxLevel(Island);
				const int32 MaxColor = GraphColor.GetIslandMaxColor(Island);

				EXPECT_EQ(IterData.MaxLevel[Island], MaxLevel);
				EXPECT_EQ(IterData.MaxColor[Island], MaxColor);
			}
		}
	}

	void HelpTickConstraints(FPBDRigidsSOAs& SOAs, const TArray<TPBDRigidParticleHandle<FReal, 3>*>& Particles,
		FPBDConstraintGraph& Graph, const TArray<TVec2<int32>>& ConstrainedParticles,
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PhysicsMaterials,
		const THandleArray<FChaosPhysicsMaterial>& PhysicalMaterials)
	{
		TMockGraphConstraints<0> Constraints;
		for(const auto& ConstrainedParticleIndices : ConstrainedParticles)
		{
			Constraints.AddConstraint(ConstrainedParticleIndices);
		}

		SOAs.ClearTransientDirty();
		Graph.InitializeGraph(SOAs.GetNonDisabledView());

		Graph.ReserveConstraints(Constraints.NumConstraints());
		for(int32 ConstraintIndex = 0; ConstraintIndex < Constraints.NumConstraints(); ++ConstraintIndex)
		{
			const TVec2<int32> Indices = Constraints.ConstraintParticleIndices(ConstraintIndex);
			Graph.AddConstraint(0,Constraints.Handles[ConstraintIndex],TVec2<TGeometryParticleHandle<FReal,3>*>(Particles[Indices[0]],Particles[Indices[1]]));
		}

		Graph.UpdateIslands(SOAs.GetNonDisabledDynamicView(),SOAs);
		for(int32 IslandIndex = 0; IslandIndex < Graph.NumIslands(); ++IslandIndex)
		{
			const bool bSleeped = Graph.SleepInactive(IslandIndex,PhysicsMaterials,PhysicalMaterials);

			if(bSleeped)
			{
				for(TGeometryParticleHandle<FReal,3>* Particle : Graph.GetIslandParticles(IslandIndex))
				{
					SOAs.DeactivateParticle(Particle);
				}
			}
		}

	}

	bool ContainsHelper(const TParticleView<TPBDRigidParticles<FReal,3>>& View,const TGeometryParticleHandle<FReal,3>* InParticle)
	{
		for(auto& Particle : View)
		{
			if(Particle.Handle() == InParticle)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Create some constrained sets of particles, some of which meet the sleep criteria, and 
	 * verify that they sleep when expected while the others do not.
	 */
	
	void GraphSleep()
	{
		for(int SleepCounterThreshold = 0; SleepCounterThreshold < 5; ++SleepCounterThreshold)
		{
			TUniquePtr<FChaosPhysicsMaterial> PhysicalMaterial = MakeUnique<FChaosPhysicsMaterial>();
			PhysicalMaterial->Friction = 0;
			PhysicalMaterial->Restitution = 0;
			PhysicalMaterial->SleepingLinearThreshold = 10;
			PhysicalMaterial->SleepingAngularThreshold = 10;
			PhysicalMaterial->DisabledLinearThreshold = 0;
			PhysicalMaterial->DisabledAngularThreshold = 0;
			PhysicalMaterial->SleepCounterThreshold = SleepCounterThreshold;

			// Create some dynamic particles
			int32 NumParticles = 6;
			FPBDRigidsSOAs SOAs;
			TArray<TPBDRigidParticleHandle<FReal, 3>*> Particles = SOAs.CreateDynamicParticles(NumParticles);
			TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
			THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
 			SOAs.GetParticleHandles().AddArray(&PhysicsMaterials);

			for (int32 Idx = 0; Idx < NumParticles; ++Idx)
			{
				PhysicsMaterials[Idx] = MakeSerializable(PhysicalMaterial);
			}
			// Ensure some particles will not sleep
			Particles[0]->V() = FVec3(100);
			Particles[1]->V() = FVec3(100);
			Particles[2]->V() = FVec3(100);

			// Ensure others will sleep but only if sleep threshold is actually considered
			Particles[3]->V() = FVec3(1);
			Particles[4]->V() = FVec3(1);

			// Create some constraints between the particles
			TArray<TVec2<int32>> ConstrainedParticles =
			{
				//
				{0, 1},
				//
				{3, 4},
			};

			FPBDConstraintGraph Graph;
			for (int32 LoopIndex = 0; LoopIndex < 5 + PhysicalMaterial->SleepCounterThreshold; ++LoopIndex)
			{
				// @todo(chaos): redo this test - sleeping now used a damped velocity rather than current velocity
				// For now, this will make it work as it did before and isn't too outrageous
				for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
				{
					Particles[ParticleIndex]->ResetSmoothedVelocities();
				}

				HelpTickConstraints(SOAs,Particles,Graph,ConstrainedParticles,PhysicsMaterials,PhysicalMaterials);
			
				// Particles 0-2 are always awake
				EXPECT_FALSE(Particles[0]->Sleeping());
				EXPECT_FALSE(Particles[1]->Sleeping());
				EXPECT_FALSE(Particles[2]->Sleeping());
				EXPECT_TRUE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[0]));
				EXPECT_TRUE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[1]));
				EXPECT_TRUE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[2]));
				// Particles 3-5 should sleep when we hit the frame count threshold and then stay asleep
				bool bSomeShouldSleep = (LoopIndex >= PhysicalMaterial->SleepCounterThreshold );
				EXPECT_EQ(Particles[3]->Sleeping(), bSomeShouldSleep);
				EXPECT_EQ(Particles[4]->Sleeping(), bSomeShouldSleep);
				EXPECT_EQ(Particles[5]->Sleeping(), bSomeShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[3]), bSomeShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[4]), bSomeShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[5]), bSomeShouldSleep);
				const bool bIsDirty = LoopIndex <= PhysicalMaterial->SleepCounterThreshold;	//dirty when active and on first frame when going to sleep
				EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[3]),bIsDirty);
				EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[4]),bIsDirty);
				EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[5]),bIsDirty);
			}
		}
	}

	void GraphSleepMergeWakeup()
	{
		for(int SleepCounterThreshold = 0; SleepCounterThreshold < 5; ++SleepCounterThreshold)
		{
			TUniquePtr<FChaosPhysicsMaterial> PhysicalMaterial = MakeUnique<FChaosPhysicsMaterial>();
			PhysicalMaterial->Friction = 0;
			PhysicalMaterial->Restitution = 0;
			PhysicalMaterial->SleepingLinearThreshold = 10;
			PhysicalMaterial->SleepingAngularThreshold = 10;
			PhysicalMaterial->DisabledLinearThreshold = 0;
			PhysicalMaterial->DisabledAngularThreshold = 0;
			PhysicalMaterial->SleepCounterThreshold = SleepCounterThreshold;

			// Create some dynamic particles
			int32 NumParticles = 6;
			FPBDRigidsSOAs SOAs;
			TArray<TPBDRigidParticleHandle<FReal, 3>*> Particles = SOAs.CreateDynamicParticles(NumParticles);
			TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
			THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
 			SOAs.GetParticleHandles().AddArray(&PhysicsMaterials);

			for (int32 Idx = 0; Idx < NumParticles; ++Idx)
			{
				PhysicsMaterials[Idx] = MakeSerializable(PhysicalMaterial);
			}
			// Ensure some particles will not sleep
			Particles[0]->V() = FVec3(100);
			Particles[1]->V() = FVec3(100);
			Particles[2]->V() = FVec3(100);

			// Ensure others will sleep but only if sleep threshold is actually considered
			Particles[3]->V() = FVec3(1);
			Particles[4]->V() = FVec3(1);

			TArray<TVec2<int32>> ConstrainedParticles =
			{
				{0, 1},
				{3, 4},
			};

			TArray<TVec2<int32>> ConstrainedParticlesAfterSleep =
			{
				{0,1},
				{1,3},	//will merge islands and wake up 3,4
				{3,4}
			};

			FPBDConstraintGraph Graph;
			const int32 WakeUpFrame = 5 + PhysicalMaterial->SleepCounterThreshold;
			for (int32 LoopIndex = 0; LoopIndex < WakeUpFrame + 5; ++LoopIndex)
			{
				// @todo(chaos): redo this test - sleeping now used a damped velocity rather than current velocity
				// For now, this will make it work as it did before and isn't too outrageous
				for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
				{
					Particles[ParticleIndex]->ResetSmoothedVelocities();
				}

				if(LoopIndex < WakeUpFrame)
				{
					HelpTickConstraints(SOAs,Particles,Graph,ConstrainedParticles,PhysicsMaterials,PhysicalMaterials);
				}
				else
				{
					HelpTickConstraints(SOAs,Particles,Graph,ConstrainedParticlesAfterSleep,PhysicsMaterials,PhysicalMaterials);
				}
			
				// Particles 0-2 are always awake
				EXPECT_FALSE(Particles[0]->Sleeping());
				EXPECT_FALSE(Particles[1]->Sleeping());
				EXPECT_FALSE(Particles[2]->Sleeping());
				EXPECT_TRUE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[0]));
				EXPECT_TRUE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[1]));
				EXPECT_TRUE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[2]));
				// Particles 3,4 should sleep when we hit the frame count threshold and then stay asleep until WakeUpFrame
				// Particle 5 should stay asleep
				bool bSomeShouldSleep = (LoopIndex >= PhysicalMaterial->SleepCounterThreshold && LoopIndex < WakeUpFrame );
				const bool b5ShouldSleep = LoopIndex >= PhysicalMaterial->SleepCounterThreshold;
				EXPECT_EQ(Particles[3]->Sleeping(), bSomeShouldSleep);
				EXPECT_EQ(Particles[4]->Sleeping(), bSomeShouldSleep);
				EXPECT_EQ(Particles[5]->Sleeping(), b5ShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[3]), bSomeShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[4]), bSomeShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(), Particles[5]), b5ShouldSleep);
				const bool bIsDirty = LoopIndex <= PhysicalMaterial->SleepCounterThreshold || LoopIndex >=WakeUpFrame;	//dirty when active and on first frame when going to sleep
				const bool b5IsDirty = LoopIndex <= PhysicalMaterial->SleepCounterThreshold;
				EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[3]),bIsDirty);
				EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[4]),bIsDirty);
				EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[5]),b5IsDirty);
			}
		}
	}

	void GraphSleepMergeSlowStillWakeup()
	{
		for(int SleepCounterThreshold = 0; SleepCounterThreshold < 5; ++SleepCounterThreshold)
		{
			TUniquePtr<FChaosPhysicsMaterial> PhysicalMaterial = MakeUnique<FChaosPhysicsMaterial>();
			PhysicalMaterial->Friction = 0;
			PhysicalMaterial->Restitution = 0;
			PhysicalMaterial->SleepingLinearThreshold = 10;
			PhysicalMaterial->SleepingAngularThreshold = 10;
			PhysicalMaterial->DisabledLinearThreshold = 0;
			PhysicalMaterial->DisabledAngularThreshold = 0;
			PhysicalMaterial->SleepCounterThreshold = SleepCounterThreshold;

			// Create some dynamic particles
			int32 NumParticles = 6;
			FPBDRigidsSOAs SOAs;
			TArray<TPBDRigidParticleHandle<FReal,3>*> Particles = SOAs.CreateDynamicParticles(NumParticles);
			TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
			THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
			SOAs.GetParticleHandles().AddArray(&PhysicsMaterials);

			for(int32 Idx = 0; Idx < NumParticles; ++Idx)
			{
				PhysicsMaterials[Idx] = MakeSerializable(PhysicalMaterial);
			}
			// Ensure some particles will not sleep
			Particles[0]->V() = FVec3(100);
			Particles[1]->V() = FVec3(100);
			Particles[2]->V() = FVec3(100);

			// Ensure others will sleep but only if sleep threshold is actually considered
			Particles[3]->V() = FVec3(1);
			Particles[4]->V() = FVec3(1);

			TArray<TVec2<int32>> ConstrainedParticles =
			{
				{0,1},
			{3,4},
			};

			TArray<TVec2<int32>> ConstrainedParticlesAfterSleep =
			{
				{0,1},
			{1,3},	//will merge islands and wake up 3,4
			{3,4}
			};

			FPBDConstraintGraph Graph;
			const int32 MergeFrame = 5 + PhysicalMaterial->SleepCounterThreshold;
			const int32 SleepAfterMergeFrame = MergeFrame + PhysicalMaterial->SleepCounterThreshold + 1;	//first frame after merge must wake up
			for(int32 LoopIndex = 0; LoopIndex < SleepAfterMergeFrame + 5; ++LoopIndex)
			{
				if(LoopIndex == MergeFrame)
				{
					//slow particles down to merge islands but stay asleep
					Particles[0]->V() = FVec3(1);
					Particles[1]->V() = FVec3(1);
				}

				// @todo(chaos): redo this test - sleeping now used a damped velocity rather than current velocity
				// For now, this will make it work as it did before and isn't too outrageous
				for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
				{
					Particles[ParticleIndex]->ResetSmoothedVelocities();
				}

				if(LoopIndex < MergeFrame)
				{
					HelpTickConstraints(SOAs,Particles,Graph,ConstrainedParticles,PhysicsMaterials,PhysicalMaterials);
				}
				else
				{
					HelpTickConstraints(SOAs,Particles,Graph,ConstrainedParticlesAfterSleep,PhysicsMaterials,PhysicalMaterials);
				}

				// Particle 2 is always awake
				EXPECT_FALSE(Particles[2]->Sleeping());
				EXPECT_TRUE(ContainsHelper(SOAs.GetActiveParticlesView(),Particles[2]));
			

				// Particles 0,1 are awake until MergeFrame
				const bool b12ShouldSleep = LoopIndex >= SleepAfterMergeFrame;
				EXPECT_EQ(Particles[0]->Sleeping(), b12ShouldSleep);
				EXPECT_EQ(Particles[1]->Sleeping(), b12ShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(),Particles[0]),b12ShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(),Particles[1]),b12ShouldSleep);
				const bool b01IsDirty = LoopIndex <= SleepAfterMergeFrame;	//dirty when active and on first frame when going to sleep;
				//EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[0]),b01IsDirty);
				//EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[1]),b01IsDirty);

				// Particles 3,4 should sleep when we hit the frame count threshold and then stay asleep until WakeUpFrame
					// Particle 5 should stay asleep
				bool bSomeShouldSleep = (LoopIndex >= PhysicalMaterial->SleepCounterThreshold && LoopIndex < MergeFrame) || LoopIndex >= SleepAfterMergeFrame;
				const bool b5ShouldSleep = LoopIndex >= PhysicalMaterial->SleepCounterThreshold;
				EXPECT_EQ(Particles[3]->Sleeping(),bSomeShouldSleep);
				EXPECT_EQ(Particles[4]->Sleeping(),bSomeShouldSleep);
				EXPECT_EQ(Particles[5]->Sleeping(),b5ShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(),Particles[3]),bSomeShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(),Particles[4]),bSomeShouldSleep);
				EXPECT_NE(ContainsHelper(SOAs.GetActiveParticlesView(),Particles[5]),b5ShouldSleep);
				const bool bIsDirty = LoopIndex <= PhysicalMaterial->SleepCounterThreshold || (LoopIndex >=MergeFrame && LoopIndex <= SleepAfterMergeFrame);	//dirty when active and on first frame when going to sleep
				const bool b5IsDirty = LoopIndex <= PhysicalMaterial->SleepCounterThreshold;
				EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[3]),bIsDirty);
				EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[4]),bIsDirty);
				EXPECT_EQ(ContainsHelper(SOAs.GetDirtyParticlesView(),Particles[5]),b5IsDirty);
			}
		}
	}


	/**
	 * Arrange particles in a MxN grid with constraints connecting adjacent pairs.
	 * Outer particles are static.
	 * Support multiple constraints between each adjacent pair.
	 * Support randomization of constraint order.
	 * Arrange particles in a grid, and create a number of constraints connecting adjacent pairs.
	 * Verify that no two same-colored constraints affect the same particle.
	 * Verify that we need no more than 2 x Node-Multiplicity + 1 colors. 
	 *
	 *        X   X   X   X   X
	 *            |   |   |
	 *        X - o - o - o - X
	 *            |   |   |
	 *        X - o - o - o - X
	 *            |   |   |
	 *        X - o - o - o - X
	 *            |   |   |
	 *        X   X   X   X   X
	 *
	 */

	void GraphColorGrid(const int32 NumParticlesX, const int32 NumParticlesY, const int32 Multiplicity, const bool bRandomize)
	{
		// Create a grid of particles
		FPBDRigidsSOAs SOAs;
		int32 NumParticles = NumParticlesX * NumParticlesY;
		TArray<TGeometryParticleHandle<FReal,3>*> AllParticles;

		for (int32 ParticleIndexY = 0; ParticleIndexY < NumParticlesY; ++ParticleIndexY)
		{
			for (int32 ParticleIndexX = 0; ParticleIndexX < NumParticlesX; ++ParticleIndexX)
			{
				if ((ParticleIndexX == 0) || (ParticleIndexX == NumParticlesX - 1) || (ParticleIndexY == 0) || (ParticleIndexY == NumParticlesY - 1))
				{
					//border so static
					AllParticles.Add(SOAs.CreateStaticParticles(1)[0]);
				}
				else
				{
					AllParticles.Add(SOAs.CreateDynamicParticles(1)[0]);
				}

				TGeometryParticleHandle<FReal, 3>* Particle = AllParticles.Last();
				Particle->X() = FVec3((FReal)ParticleIndexX * 200, (FReal)ParticleIndexY * 200, (FReal)0);
			}
		}

		// Determine which particle pairs should be constrained.
		// Connect all adjacent pairs in a grid with one or more constraints for each pair
		// making sure no two non-dynamic particles are connected to each other.
		TArray<TVec2<int32>> ConstrainedParticles;
		// X-Direction constraints
		for (int32 ParticleIndexY = 0; ParticleIndexY < NumParticlesY; ++ParticleIndexY)
		{
			for (int32 ParticleIndexX = 0; ParticleIndexX < NumParticlesX - 1; ++ParticleIndexX)
			{
				int32 ParticleIndex0 = ParticleIndexX + ParticleIndexY * NumParticlesX;
				int32 ParticleIndex1 = ParticleIndexX + ParticleIndexY * NumParticlesX + 1;

				auto RigidParticle0 = AllParticles[ParticleIndex0]->CastToRigidParticle();
				auto RigidParticle1 = AllParticles[ParticleIndex1]->CastToRigidParticle();
				if (   (RigidParticle0 && RigidParticle0->ObjectState() == EObjectStateType::Dynamic)
					|| (RigidParticle1 && RigidParticle1->ObjectState() == EObjectStateType::Dynamic))
				{
					for (int32 MultiplicityIndex = 0; MultiplicityIndex < Multiplicity; ++MultiplicityIndex)
					{
						ConstrainedParticles.Add({ ParticleIndex0, ParticleIndex1 });
					}
				}
			}
		}
		// Y-Direction Constraints
		for (int32 ParticleIndexY = 0; ParticleIndexY < NumParticlesY - 1; ++ParticleIndexY)
		{
			for (int32 ParticleIndexX = 0; ParticleIndexX < NumParticlesX; ++ParticleIndexX)
			{
				int32 ParticleIndex0 = ParticleIndexX + ParticleIndexY * NumParticlesX;
				int32 ParticleIndex1 = ParticleIndexX + (ParticleIndexY + 1) * NumParticlesX;

				auto RigidParticle0 = AllParticles[ParticleIndex0]->CastToRigidParticle();
				auto RigidParticle1 = AllParticles[ParticleIndex1]->CastToRigidParticle();
				if (   (RigidParticle0 && RigidParticle0->ObjectState() == EObjectStateType::Dynamic)
					|| (RigidParticle1 && RigidParticle1->ObjectState() == EObjectStateType::Dynamic))
				{
					for (int32 MultiplicityIndex = 0; MultiplicityIndex < Multiplicity; ++MultiplicityIndex)
					{
						ConstrainedParticles.Add({ ParticleIndex0, ParticleIndex1 });
					}
				}
			}
		}

		// Randomize the constraint order
		if (bRandomize)
		{
			FMath::RandInit((int32)354786483);
			for (int32 RandIndex = 0; RandIndex < 2 * ConstrainedParticles.Num(); ++RandIndex)
			{
				int32 Rand0 = FMath::RandRange(0, ConstrainedParticles.Num() - 1);
				int32 Rand1 = FMath::RandRange(0, ConstrainedParticles.Num() - 1);
				Swap(ConstrainedParticles[Rand0], ConstrainedParticles[Rand1]);
			}
		}

		// Generate the constraints
		TMockGraphConstraints<0> Constraints;
		for (const auto& ConstrainedParticleIndices : ConstrainedParticles)
		{
			Constraints.AddConstraint(ConstrainedParticleIndices);
		}
		
		// Build the connectivity graph and islands
		FPBDConstraintGraph Graph;
		FPBDConstraintColor GraphColor;
		const int32 ContainerId = 0;
		Graph.InitializeGraph(SOAs.GetNonDisabledView());
		Graph.ReserveConstraints(Constraints.NumConstraints());
		for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.NumConstraints(); ++ConstraintIndex)
		{
			const TVec2<int32>& Indices = Constraints.ConstraintParticleIndices(ConstraintIndex);
			Graph.AddConstraint(ContainerId, Constraints.Handles[ConstraintIndex], TVec2<TGeometryParticleHandle<FReal, 3>*>(AllParticles[Indices[0]], AllParticles[Indices[1]]) );
		}
		Graph.UpdateIslands(SOAs.GetNonDisabledDynamicView(), SOAs);

		// It's a connected grid, so only one island
		EXPECT_EQ(Graph.NumIslands(), 1);
		
		// Assign color to constraints
		GraphColor.InitializeColor(Graph);
		for (int32 Island = 0; Island < Graph.NumIslands(); ++Island)
		{
			GraphColor.ComputeColor(Island, Graph, ContainerId);
		}
		// Check colors
		// No constraints should appear twice in the level-color-constraint map
		// No particles should be influenced by more than one constraint in any individual level+color
		TSet<FConstraintHandle*> ConstraintUnionSet;
		for (int32 Island = 0; Island < Graph.NumIslands(); ++Island)
		{
			const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = GraphColor.GetIslandLevelToColorToConstraintListMap(Island);
			const int32 MaxLevel = GraphColor.GetIslandMaxLevel(Island);
			const int32 MaxColor = GraphColor.GetIslandMaxColor(Island);
			for (int32 Level = 0; Level <= MaxLevel; ++Level)
			{
				for (int Color = 0; Color <= MaxColor; ++Color)
				{
					if (LevelToColorToConstraintListMap[Level].Contains(Color))
					{
						// Build the set of constraint and particle indices for this color
						TSet<FConstraintHandle*> ConstraintSet = TSet<FConstraintHandle*>(LevelToColorToConstraintListMap[Level][Color]);

						// See if we have any constraints that were also in a prior color
						TSet<FConstraintHandle*> ConstraintIntersectSet = ConstraintUnionSet.Intersect(ConstraintSet);
						EXPECT_EQ(ConstraintIntersectSet.Num(), 0);
						ConstraintUnionSet = ConstraintUnionSet.Union(ConstraintSet);

						// See if any particles are being modified by more than one constraint at this level+color
						EXPECT_TRUE(Constraints.AreConstraintsIndependent(AllParticles, LevelToColorToConstraintListMap[Level][Color]));
					}
				}
			}
		}

		// Verify that we have created a reasonable number of colors. For the greedy edge coloring algorithm this is
		//		NumColors >= MaxNodeMultiplicity
		//		NumColors <= MaxNodeMultiplicity * 2 - 1
		// Each node connects to 4 neighbors with 'Multiplicity' connections, but some of them will be to static particles and we ignore those.
		// For grid dimensions less that 4 each particle on has 2 non-static connections, otherwise there are up to 4
		const int32 MaxMultiplicity = Multiplicity * (((NumParticlesX <= 4) || (NumParticlesY <= 4)) ? 2 : 4);
		const int32 MinNumGreedyColors = MaxMultiplicity;
		const int32 MaxNumGreedyColors = 2 * MaxMultiplicity - 1;
		for (int32 Island = 0; Island < Graph.NumIslands(); ++Island)
		{
			const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = GraphColor.GetIslandLevelToColorToConstraintListMap(Island);
			const int32 MaxIslandColor = GraphColor.GetIslandMaxColor(Island);
			EXPECT_GE(MaxIslandColor, MinNumGreedyColors - 1);
			// We are consistently slightly worse that the greedy algorithm, but hopefully doesn't matter
			//EXPECT_LE(MaxIslandColor, MaxNumGreedyColors - 1);
			EXPECT_LE(MaxIslandColor, MaxNumGreedyColors);
		}
	}


	TEST(GraphTests,TestGraphIslands)
	{
		GraphIslands();
	}

	TEST(GraphTests, TestGraphIslandsPersistence)
	{
		GraphIslandsPersistence();
	}
		
	TEST(GraphTests, TestGraphSleep)
	{
		GraphSleep();
		GraphSleepMergeWakeup();
		GraphSleepMergeSlowStillWakeup();
	}

	TEST(GraphTests, TestGraphColor)
	{
		Chaos::TVec2<int32> GridDims[] = {
			{3, 3},
			{4, 4},
			{5, 5},
			{6, 6},
			{7, 7},
			{8, 8},
			{9, 9},
			{10, 10},
			{20, 3},
			{20, 10}
		};
		for (int32 Randomize = 0; Randomize < 2; ++Randomize)
		{
			bool bRandomize = (Randomize > 0);
			for (int32 Multiplicity = 1; Multiplicity < 4; ++Multiplicity)
			{
				for (int32 Grid = 0; Grid < UE_ARRAY_COUNT(GridDims); ++Grid)
				{
					GraphColorGrid(GridDims[Grid][0], GridDims[Grid][1], Multiplicity, bRandomize);
				}
			}
		}

		SUCCEED();
	}
}

