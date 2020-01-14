// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestClustering.h"

#include "HeadlessChaos.h"
#include "Modules/ModuleManager.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Box.h"
#include "Chaos/Plane.h"
#include "Chaos/Sphere.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Levelset.h"
#include "Chaos/UniformGrid.h"
#include "Chaos/Utilities.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"

namespace ChaosTest {

	using namespace Chaos;

	template<class T>
	void ImplicitCluster()
	{
		TPBDRigidsSOAs<T, 3> Particles;
		TPBDRigidsEvolutionGBF<T, 3> Evolution(Particles);
		auto& ClusteredParticles = Particles.GetClusteredParticles();
		
		uint32 FirstId = ClusteredParticles.Size();
		auto& Box1 = *AppendClusteredParticleBox<T>(Particles, TVector<T, 3>((T)100, (T)100, (T)100));
		uint32 BoxId = FirstId++;
		auto& Box2 = *AppendClusteredParticleBox<T>(Particles, TVector<T, 3>((T)100, (T)100, (T)100));
		uint32 Box2Id = FirstId++;
		
		Box2.X() = TVector<T, 3>((T)100, (T)0, (T)0);
		Box2.P() = Box2.X();

		Evolution.AdvanceOneTimeStep(0);	//hack to initialize islands
		//Evolution.InitializeAccelerationStructures();	//make sure islands are created

		Evolution.GetRigidClustering().CreateClusterParticle(0,TArray<uint32>{ BoxId, Box2Id }, TSerializablePtr<FImplicitObject>(), nullptr);
		EXPECT_EQ(ClusteredParticles.Size(), 3);

		TVector<T, 3> ClusterX = ClusteredParticles.X(2);
		TRotation<T,3> ClusterRot = ClusteredParticles.R(2);

		EXPECT_TRUE(ClusterX.Equals(TVector<T, 3> {(T)50, 0, 0}));
		EXPECT_TRUE(ClusterRot.Equals(TRotation<T, 3>::Identity));
		EXPECT_TRUE(ClusterX.Equals(ClusteredParticles.P(2)));
		EXPECT_TRUE(ClusterRot.Equals(ClusteredParticles.Q(2)));

		TRigidTransform<float, 3> ClusterTM(ClusterX, ClusterRot);
		TVector<T, 3> LocalPos = ClusterTM.InverseTransformPositionNoScale(TVector<T, 3> {(T)200, (T)0, (T)0});
		TVector<T, 3> Normal;
		T Phi = ClusteredParticles.Geometry(2)->PhiWithNormal(LocalPos, Normal);
		EXPECT_TRUE(FMath::IsNearlyEqual(Phi, (T)50));
		EXPECT_TRUE(Normal.Equals(TVector<T, 3>{(T)1, (T)0, (T)0}));

		//EXPECT_TRUE(Evolution.GetParticles().Geometry(2)->IsConvex());  we don't actually guarantee this
	}
	template void ImplicitCluster<float>();

	template<class T>
	void FractureCluster()
	{
		TPBDRigidsSOAs<T, 3> Particles;
		TPBDRigidsEvolutionGBF<T, 3> Evolution(Particles);
		auto& ClusteredParticles = Particles.GetClusteredParticles();

		//create a long row of boxes - the depth 0 cluster is the entire row, the depth 1 clusters 4 boxes each, the depth 2 clusters are 1 box each

		constexpr int32 NumBoxes = 32;
		TArray<TGeometryParticleHandle<T, 3>*> Boxes;
		TArray<uint32> BoxIDs;
		for (int i = 0; i < NumBoxes; ++i)
		{
			BoxIDs.Add(ClusteredParticles.Size());
			auto Box = AppendClusteredParticleBox<T>(Particles, TVector<T, 3>((T)100, (T)100, (T)100));
			Box->X() = TVector<T, 3>((T)i * (T)100, (T)0, (T)0);
			Box->P() = Box->X();
			Boxes.Add(Box);
		}

		Evolution.AdvanceOneTimeStep(0);	//hack to generate islands

		TArray<uint32> ClusterIDs;

		for (int i = 0; i < NumBoxes / 4; ++i)
		{
			ClusterIDs.Add(Evolution.GetRigidClustering().CreateClusterParticle(0, TArray<uint32>{ BoxIDs[i*4], BoxIDs[i*4+1], BoxIDs[i*4+2], BoxIDs[i*4+3] }, TSerializablePtr<FImplicitObject>(), nullptr));
		}

		uint32 RootClusterID = Evolution.GetRigidClustering().CreateClusterParticle(0, TArray<uint32>(ClusterIDs), TSerializablePtr<FImplicitObject>(), nullptr);
		TVector<T, 3> InitialVelocity((T)50, (T)20, (T)100);
		ClusteredParticles.V(RootClusterID) = InitialVelocity;
		
		constexpr int NumParticles = NumBoxes + NumBoxes / 4 + 1;
		EXPECT_EQ(ClusteredParticles.Size(), NumParticles);

		for (int i = 0; i < NumParticles-1; ++i)
		{
			EXPECT_TRUE(ClusteredParticles.Disabled(i));
		}

		EXPECT_TRUE(ClusteredParticles.Disabled(RootClusterID) == false);
		EXPECT_EQ(Particles.GetNonDisabledView().Num(), 1);
		EXPECT_EQ(Particles.GetNonDisabledView().Begin()->Handle(), ClusteredParticles.Handle(RootClusterID));

		const T Dt = 0;	//don't want to integrate gravity, just want to test fracture
		Evolution.AdvanceOneTimeStep(Dt);
		EXPECT_TRUE(ClusteredParticles.Disabled(RootClusterID));	//not a cluster anymore, so disabled
		for (auto& Particle : Particles.GetNonDisabledView())
		{
			EXPECT_NE(Particle.Handle(), ClusteredParticles.Handle(RootClusterID));	//view no longer contains root
		}

		EXPECT_EQ(Particles.GetNonDisabledView().Num(), NumBoxes / 4);

		//children are still in a cluster, so disabled
		for (uint32 BoxID : BoxIDs)
		{
			EXPECT_TRUE(ClusteredParticles.Disabled(BoxID));
			for (auto& Particle : Particles.GetNonDisabledView())
			{
				EXPECT_NE(Particle.Handle(), ClusteredParticles.Handle(BoxID));	//make sure boxes are not in non disabled array
			}
			
			for (int32 Island = 0; Island < Evolution.NumIslands(); ++Island)
			{
				EXPECT_TRUE(Evolution.GetIslandParticles(Island).Contains(ClusteredParticles.Handle(BoxID)) == false);
			}
		}

		for (uint32 ClusterID : ClusterIDs)
		{
			EXPECT_TRUE(ClusteredParticles.Disabled(ClusterID) == false);	//not a cluster anymore, so disabled
			bool bFoundInNonDisabled = false;
			for (auto& Particle : Particles.GetNonDisabledView())
			{
				bFoundInNonDisabled |= Particle.Handle() == ClusteredParticles.Handle(ClusterID);
			}

			EXPECT_TRUE(bFoundInNonDisabled);	//clusters are enabled and in non disabled array
			EXPECT_TRUE(ClusteredParticles.V(ClusterID).Equals(InitialVelocity));
		}

		Evolution.AdvanceOneTimeStep(Dt);
		//second fracture, all clusters are now disabled
		for (uint32 ClusterID : ClusterIDs)
		{
			EXPECT_TRUE(ClusteredParticles.Disabled(ClusterID) == true);	//not a cluster anymore, so disabled
			for (auto& Particle : Particles.GetNonDisabledView())
			{
				EXPECT_NE(Particle.Handle(), ClusteredParticles.Handle(ClusterID));	//make sure boxes are not in non disabled array
			}

			for (int32 Island = 0; Island < Evolution.NumIslands(); ++Island)
			{
				EXPECT_TRUE(Evolution.GetIslandParticles(Island).Contains(ClusteredParticles.Handle(ClusterID)) == false);
			}
		}

		EXPECT_EQ(Particles.GetNonDisabledView().Num(), NumBoxes);
		for (uint32 BoxID : BoxIDs)
		{
			EXPECT_TRUE(ClusteredParticles.Disabled(BoxID) == false);
			bool bFoundInNonDisabled = false;
			for (auto& Particle : Particles.GetNonDisabledView())
			{
				bFoundInNonDisabled |= Particle.Handle() == ClusteredParticles.Handle(BoxID);
			}
			EXPECT_TRUE(bFoundInNonDisabled);
			EXPECT_TRUE(ClusteredParticles.V(BoxID).Equals(InitialVelocity));
		}
	}
	template void FractureCluster<float>();

	template<class T>
	void PartialFractureCluster()
	{
		TPBDRigidsSOAs<T, 3> Particles;
		TPBDRigidsEvolutionGBF<T, 3> Evolution(Particles);
		auto& ClusteredParticles = Particles.GetClusteredParticles();

		//create a long row of boxes - the depth 0 cluster is the entire row, the depth 1 clusters 4 boxes each, the depth 2 clusters are 1 box each

		constexpr int32 NumBoxes = 32;
		TArray<TGeometryParticleHandle<T, 3>*> Boxes;
		TArray<uint32> BoxIDs;
		for (int i = 0; i < NumBoxes; ++i)
		{
			BoxIDs.Add(ClusteredParticles.Size());
			auto Box = AppendClusteredParticleBox<T>(Particles, TVector<T, 3>((T)100, (T)100, (T)100));
			Box->X() = TVector<T, 3>((T)i * (T)100, (T)0, (T)0);
			Box->P() = Box->X();
			Boxes.Add(Box);
		}

		Evolution.AdvanceOneTimeStep(0);	//hack to generate islands

		TArray<uint32> ClusterIDs;

		for (int i = 0; i < NumBoxes / 4; ++i)
		{
			ClusterIDs.Add(Evolution.GetRigidClustering().CreateClusterParticle(0, TArray<uint32>{ BoxIDs[i * 4], BoxIDs[i * 4 + 1], BoxIDs[i * 4 + 2], BoxIDs[i * 4 + 3] }, TSerializablePtr<FImplicitObject>(), nullptr));
		}

		TArray<uint32> ClusterIDsDup = ClusterIDs;

		uint32 RootClusterID = Evolution.GetRigidClustering().CreateClusterParticle(0, TArray<uint32>(ClusterIDs), TSerializablePtr<FImplicitObject>(), nullptr);
		TVector<T, 3> InitialVelocity((T)50, (T)20, (T)100);
		ClusteredParticles.V(RootClusterID) = InitialVelocity;

		TUniquePtr<FChaosPhysicsMaterial> PhysicalMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		Chaos::TArrayCollectionArray<float>& SolverStrainArray = Evolution.GetRigidClustering().GetStrainArray();

		for (int i = 0; i < NumBoxes + NumBoxes / 4 + 1; ++i)
		{
			SolverStrainArray[i] = (T)1;
			Evolution.SetPhysicsMaterial(ClusteredParticles.Handle(i), MakeSerializable(PhysicalMaterial));
		}

		Evolution.AdvanceOneTimeStep((T)1 / (T)60);
		EXPECT_TRUE(ClusteredParticles.Disabled(RootClusterID) == false);	//strain > 0 so no fracture yet

		SolverStrainArray[ClusterIDsDup[2]] = (T)0;	//fracture the third cluster, this should leave us with three pieces (0, 1), (2), (3,4,5,6,7)
		Evolution.AdvanceOneTimeStep((T)1 / (T)60);
		//EXPECT_TRUE(Evolution.GetParticles().Disabled(RootClusterID) == false);	//one of the connected pieces should re-use this
		EXPECT_TRUE(ClusteredParticles.Disabled(ClusterIDsDup[2]) == false);	//this cluster is on its own and should be enabled 
		
		EXPECT_EQ(Evolution.GetActiveClusteredArray().Num(), 3);	//there should only be 3 pieces
		for (uint32 BoxID : BoxIDs)
		{
			EXPECT_TRUE(ClusteredParticles.Disabled(BoxID));	//no boxes should be active yet
			EXPECT_TRUE(Evolution.GetActiveClusteredArray().Contains(ClusteredParticles.Handle(BoxID)) == false);
			for (int32 Island = 0; Island < Evolution.NumIslands(); ++Island)
			{
				EXPECT_TRUE(Evolution.GetIslandParticles(Island).Contains(ClusteredParticles.Handle(BoxID)) == false);
			}
		}

		SolverStrainArray[NumBoxes + NumBoxes / 4 + 1] = (T)1;
		Evolution.SetPhysicsMaterial(ClusteredParticles.Handle(NumBoxes + NumBoxes / 4 + 1), MakeSerializable(PhysicalMaterial));
		SolverStrainArray[NumBoxes + NumBoxes / 4 + 2] = (T)1;
		Evolution.SetPhysicsMaterial(ClusteredParticles.Handle(NumBoxes + NumBoxes / 4 + 2), MakeSerializable(PhysicalMaterial));

		Evolution.AdvanceOneTimeStep((T)1 / (T)60);	//next frame nothing should fracture
		//EXPECT_TRUE(Evolution.GetParticles().Disabled(RootClusterID) == false);	//one of the connected pieces should re-use this
		EXPECT_TRUE(ClusteredParticles.Disabled(ClusterIDsDup[2]) == false);	//this cluster is on its own and should be enabled 

		EXPECT_EQ(Evolution.GetActiveClusteredArray().Num(), 3);	//there should only be 3 pieces
		for (uint32 BoxID : BoxIDs)
		{
			EXPECT_TRUE(ClusteredParticles.Disabled(BoxID));	//no boxes should be active yet
			EXPECT_TRUE(Evolution.GetActiveClusteredArray().Contains(ClusteredParticles.Handle(BoxID)) == false);
			for (int32 Island = 0; Island < Evolution.NumIslands(); ++Island)
			{
				EXPECT_TRUE(Evolution.GetIslandParticles(Island).Contains(ClusteredParticles.Handle(BoxID)) == false);
			}
		}
	}
	template void PartialFractureCluster<float>();



}

