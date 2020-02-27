// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"

#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

#include "Modules/ModuleManager.h"

namespace ChaosTest
{

	using namespace Chaos;

	GTEST_TEST(DirtyParticleTests,Basic)
	{
#if 0
		struct FSimpleParticleRemote
		{
			FSimpleParticleRemote() : Manager(nullptr){}

			FDirtyPropertiesManager* Manager;
			TRemoteParticleProperty<FVec3> X;
			TRemoteParticleProperty<FReal> InvM;
			TRemoteParticleProperty<TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>> Geometry;

			const FVec3& GetX(FDirtyPropertiesManager& Manager) const { return X.Read(*Manager); }
			const FReal GetInvM(FDirtyPropertiesManager& Manager) const { return InvM.Read(*Manager); }
			const TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>& GetGeometry(FDirtyPropertiesManager& Manager) const { return Geometry.Read(*Manager); }
			
			void Clear()
			{
				if(Manager)
				{
					X.Clear(*Manager);
					InvM.Clear(*Manager);
					Geometry.Clear(*Manager);
				}
			}

			~FSimpleParticleRemote()
			{
				Clear();
			}
		};

		struct FSimpleParticle
		{
			FSimpleParticle() : Manager(nullptr){}
			const FVec3& GetX() const{ return X.Read(); }
			void SetX(const FVec3& InX){ X.Write(InX,Remote.X,ToFlush,Remote.Dirty, Remote.Manager); }

			const FReal GetInvM() const { return InvM.Read(); }
			void SetInvM(FReal InInvM){ InvM.Write(InInvM,Remote.InvM, ToFlush, Remote.Dirty, Remote.Manager); }

			const TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>& GetGeometry() const { return Geometry.Read(); }
			void SetGeometry(const TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>& InGeom){ Geometry.Write(InGeom, Remote.Geometry, ToFlush, Remote.Dirty, Remote.Manager); }

			void Flush()
			{
				ensure(Remote.Manager);
				if(ToFlush.IsDirty())
				{
					if(ToFlush.IsDirty(EParticleFlags::X))
					{
						X.Flush(Remote.X,ToFlush,*Remote.Manager);
					}

					if(ToFlush.IsDirty(EParticleFlags::InvM))
					{
						InvM.Flush(Remote.InvM,ToFlush, *Remote.Manager);
					}

					if(ToFlush.IsDirty(EParticleFlags::Geometry))
					{
						Geometry.Flush(Remote.Geometry,ToFlush,*Remote.Manager);
					}
				}

				ensure(ToFlush.IsClean());
			}

			bool IsDirty() const { return Remote.Dirty.IsDirty(); }

			void StealRemote(FSimpleParticleRemote& Dst)
			{
				Flush();
				Swap(Dst,Remote);
			}

			TParticleProperty<FVec3,EParticleFlags::X> X;
			TParticleProperty<FReal,EParticleFlags::InvM> InvM;
			TParticleProperty<TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>,EParticleFlags::Geometry> Geometry;
			FParticleDirtyFlags ToFlush;

			FSimpleParticleRemote Remote;
		};

		FSimpleParticle P1;

		P1.SetX(FVec3(1,1,1));
		P1.SetInvM(3);

		EXPECT_EQ(P1.GetX(),FVec3(1,1,1));
		EXPECT_EQ(P1.GetInvM(),3);

		EXPECT_TRUE(P1.ToFlush.IsDirty(EParticleFlags::X));
		EXPECT_TRUE(P1.ToFlush.IsDirty(EParticleFlags::InvM));
		EXPECT_FALSE(P1.ToFlush.IsDirty(EParticleFlags::Geometry));

		FDirtyPropertiesManager Manager;
		P1.Remote.Manager = &Manager;

		FSimpleParticleRemote Remote;
		P1.StealRemote(Remote);

		EXPECT_TRUE(Remote.Dirty.IsDirty(EParticleFlags::X));
		EXPECT_TRUE(Remote.Dirty.IsDirty(EParticleFlags::X));
		EXPECT_FALSE(Remote.Dirty.IsDirty(EParticleFlags::Geometry));

		EXPECT_EQ(Remote.GetX(Manager),FVec3(1,1,1));
		EXPECT_EQ(Remote.GetInvM(Manager),3);

		Remote.Clear();

		/*
		FDirtyProperties DirtyParticle1;
		DirtyParticle1.WriteX(DirtyPropertiesPool, FVec3(1,1,1));
		DirtyParticle1.WriteInvM(DirtyPropertiesPool, 3);

		FDirtyProperties DirtyParticle2;
		DirtyParticle2.WriteX(DirtyPropertiesPool, FVec3(2,1,1));

		//properties are dirty
		EXPECT_TRUE(DirtyParticle1.IsDirty(EParticleFlags::X));
		EXPECT_TRUE(DirtyParticle1.IsDirty(EParticleFlags::InvM));

		EXPECT_TRUE(DirtyParticle2.IsDirty(EParticleFlags::X));

		//untouched properties are clean
		EXPECT_FALSE(DirtyParticle2.IsDirty(EParticleFlags::InvM));

		//values were saved
		EXPECT_EQ(DirtyParticle1.ReadX(DirtyPropertiesPool), FVec3(1,1,1));
		EXPECT_EQ(DirtyParticle1.ReadInvM(DirtyPropertiesPool), 3);
		EXPECT_EQ(DirtyParticle2.ReadX(DirtyPropertiesPool), FVec3(2,1,1));
		
		EXPECT_EQ(DirtyParticle1.PopX(DirtyPropertiesPool),FVec3(1,1,1));
		EXPECT_FALSE(DirtyParticle1.IsDirty(EParticleFlags::X));

		//make sure we are not leaking shared ptrs
		TSharedPtr<FImplicitObject,ESPMode::ThreadSafe> Ptr(new TSphere<FReal,3>(FVec3(0), 0));
		DirtyParticle1.WriteGeometry(DirtyPropertiesPool, Ptr);
		TWeakPtr<FImplicitObject,ESPMode::ThreadSafe> WeakPtr(Ptr);
		Ptr = nullptr;

		EXPECT_TRUE(WeakPtr.IsValid());	//still around because dirty pool is holding on to it
		{
			TSharedPtr<FImplicitObject,ESPMode::ThreadSafe> Geom = DirtyParticle1.PopGeometry(DirtyPropertiesPool);
			EXPECT_TRUE(WeakPtr.IsValid());	//Popped the geometry but still holding on to it
		}

		EXPECT_FALSE(WeakPtr.IsValid());	//Finished with popped geometry so shared ptr goes away
		

		//If we haven't popped everything we call this
		DirtyParticle1.Clean(DirtyPropertiesPool);
		DirtyParticle2.Clean(DirtyPropertiesPool);*/
#endif
	}
}

