// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Deformable/ChaosDeformableSolver.h"
#include "Chaos/Deformable/ChaosDeformableSolverTypes.h"
#include "Chaos/Deformable/ChaosDeformableSolverProxy.h"

#include "Chaos/TriangleMesh.h"
#include "Chaos/PBDAltitudeSpringConstraints.h"
#include "Chaos/PBDBendingConstraints.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PBDVolumeConstraint.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PBDTriangleMeshCollisions.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDSoftsSolverParticles.h"
#include "Chaos/PBDTetConstraints.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/XPBDCorotatedConstraints.h"
#include "Chaos/XPBDVolumeConstraints.h"
#include "Chaos/XPBDCorotatedFiberConstraints.h"
#include "Chaos/Plane.h"
#include "Chaos/Utilities.h"
#include "Chaos/PBDEvolution.h"
#include "Containers/StringConv.h"
#include "CoreMinimal.h"
#include "GeometryCollection/Facades/CollectionKinematicBindingFacade.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"


DEFINE_LOG_CATEGORY_STATIC(LogChaosDeformableSolver, Log, All);
namespace Chaos::Softs
{
	FCriticalSection FDeformableSolver::PackageMutex;


	FDeformableSolver::FDeformableSolver(FDeformableSolverProperties InProp)
		: Property(InProp)
	{
		Reset(Property);
	}

	void FDeformableSolver::FPhysicsThreadAccess::Reset(const FDeformableSolverProperties& InProps)
	{
		Solver.Reset(InProps);
	}
	void FDeformableSolver::Reset(const FDeformableSolverProperties& InProps)
	{
		Property = InProps;
		MObjects = TArrayCollectionArray<const UObject*>();
		FSolverParticles LocalParticlesDummy;
		FSolverRigidParticles RigidParticles;
		Evolution.Reset(new FPBDEvolution(MoveTemp(LocalParticlesDummy), MoveTemp(RigidParticles), {}, Property.NumSolverIterations));
		Evolution->Particles().AddArray(&MObjects);
		if (Property.bDoSelfCollision)
		{
			SurfaceElements.Reset(new TArray<Chaos::TVec3<int32>>());
			SurfaceTriangleMesh.Reset(new Chaos::FTriangleMesh());
		}
		Frame = 0;
		Time = 0.f;
	}

	bool FDeformableSolver::FPhysicsThreadAccess::Advance(FSolverReal DeltaTime)
	{
		return Solver.Advance(DeltaTime);
	}
	bool FDeformableSolver::Advance(FSolverReal DeltaTime)
	{
		int32 NumIterations = FMath::Clamp<int32>(Property.NumSolverSubSteps, 0, INT_MAX);
		if (NumIterations)
		{
			FSolverReal SubDeltaTime = DeltaTime / (FSolverReal)NumIterations;
			if (!FMath::IsNearlyZero(SubDeltaTime))
			{
				for (int i = 0; i < NumIterations; ++i)
				{
					TickSimulation(SubDeltaTime);
				}
				Frame++;
				return true;
			}
		}
		return false;
	}

	void FDeformableSolver::FPhysicsThreadAccess::InitializeSimulationObjects()
	{
		Solver.InitializeSimulationObjects();
	}
	void FDeformableSolver::InitializeSimulationObjects()
	{
		for (TUniquePtr<FThreadingProxy>& Proxy : UninitializedProxys)
		{
			InitializeSimulationObject(*Proxy);
			InitializeKinematicState(*Proxy);

			FThreadingProxy::FKey Key = Proxy->GetOwner();
			Proxies.Add(Key, TUniquePtr<FThreadingProxy>(Proxy.Release()));
		}
		UninitializedProxys.SetNum(0, true);

		if (Property.bDoSelfCollision)
		{
			InitializeSelfCollisionVariables();
		}
		InitializeCollisionBodies();
	}

	void FDeformableSolver::FPhysicsThreadAccess::InitializeSimulationObject(FThreadingProxy& InProxy)
	{
		Solver.InitializeSimulationObject(InProxy);
	}
	void FDeformableSolver::InitializeSimulationObject(FThreadingProxy& InProxy)
	{
		if (FFleshThreadingProxy* Proxy = InProxy.As<FFleshThreadingProxy>())
		{
			if (const FManagedArrayCollection* Dynamic = &Proxy->GetDynamicCollection())
			{
				if (const FManagedArrayCollection* Rest = &Proxy->GetRestCollection())
				{
					const TManagedArray<FSolverReal>* MassArray = Rest->FindAttribute<FSolverReal>("Mass", FGeometryCollection::VerticesGroup);

					// @todo(chaos) : make user attributes
					FSolverReal Mass = 100.0;

					const TManagedArray<FVector3f>& Vertex = Rest->GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
					const TManagedArray<FIntVector>& Indices = Rest->GetAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);
					const TManagedArray<FVector3f>& DynamicVertex = Dynamic->GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);

					if (uint32 NumParticles = Vertex.Num())
					{
						if (uint32 NumSurfaceElements = Indices.Num())
						{
							// @todo(chaos) : reduce conversions
							auto ChaosVert = [](FVector3d V) { return Chaos::FVec3(V.X, V.Y, V.Z); };
							auto DoubleVert = [](FVector3f V) { return FVector3d(V.X, V.Y, V.Z); };
							auto ChaosM = [](FSolverReal M, const TManagedArray<float>* AM, int32 Index, int32 Num) { return FSolverReal((AM != nullptr) ? (*AM)[Index] : M / FSolverReal(Num)); };
							auto ChaosInvM = [](FSolverReal M) { return FSolverReal(FMath::IsNearlyZero(M) ? 0.0 : 1 / M); };
							auto ChaosTet = [](FIntVector4 V, int32 dp) { return Chaos::TVec4<int32>(dp + V.X, dp + V.Y, dp + V.Z, dp + V.W); };
							auto ChaosTri = [](FIntVector V, int32 dp) { return  Chaos::TVec3<int32>(dp + V.X, dp + V.Y, dp + V.Z); };
							auto ChaosIntVec2 = [](int32 A, int32 B) { return  Chaos::TVec2<int32>(A, B); };

							//  Add Simulation Particles Node
							const FTransform& InitialTransform = Proxy->GetInitialTransform();
							int32 ParticleStart = Evolution->AddParticleRange(NumParticles, 1, true);
							for (uint32 vdx = 0; vdx < NumParticles; ++vdx)
							{
								int32 SolverParticleIndex = ParticleStart + vdx;
								Evolution->Particles().X(SolverParticleIndex) = ChaosVert(InitialTransform.TransformPosition(DoubleVert(DynamicVertex[vdx])));
								Evolution->Particles().V(SolverParticleIndex) = Chaos::FVec3(0.f, 0.f, 0.f);
								Evolution->Particles().M(SolverParticleIndex) = ChaosM(Mass, MassArray, vdx, NumParticles);
								Evolution->Particles().InvM(SolverParticleIndex) = ChaosInvM(Evolution->Particles().M(SolverParticleIndex));
								Evolution->Particles().PAndInvM(SolverParticleIndex).InvM = Evolution->Particles().InvM(SolverParticleIndex);
								MObjects[SolverParticleIndex] = Proxy->GetOwner();
							}
							Proxy->SetSolverParticleRange(ParticleStart, NumParticles);

							if (Property.bEnableKinematics)
							{
								typedef Chaos::Facades::FKinematicBindingFacade Kinematics;

								// Add Kinematics Node
								for (int i = Kinematics::NumKinematicBindings(Rest) - 1; i >= 0; i--)
								{
									Kinematics::FBindingKey Key = Kinematics::GetKinematicBindingKey(Rest, i);

									int32 BoneIndex = INDEX_NONE;
									TArray<int32> BoundVerts;
									TArray<float> BoundWeights;
									Kinematics::GetBoneBindings(Rest, Key, BoneIndex, BoundVerts, BoundWeights);

									for (int32 vdx : BoundVerts)
									{
										if (BoneIndex != INDEX_NONE)
										{
											int32 ParticleIndex = ParticleStart + vdx;
											Evolution->Particles().InvM(ParticleIndex) = 0.f;
											Evolution->Particles().PAndInvM(ParticleIndex).InvM = 0.f;
										}
									}
								}
							}

							const TManagedArray<FIntVector4>& Tetrahedron = Rest->GetAttribute<FIntVector4>("Tetrahedron", "Tetrahedral");

							if (uint32 NumElements = Tetrahedron.Num())
							{
								const FIntVector2& Range = Proxy->GetSolverParticleRange();

								// Add Tetrahedral Elements Node
								TArray<Chaos::TVec4<int32>> Elements;
								Elements.SetNum(NumElements);
								for (uint32 edx = 0; edx < NumElements; ++edx)
								{
									Elements[edx] = ChaosTet(Tetrahedron[edx], Range[0]);
								}


								FXPBDCorotatedConstraints<FSolverReal, FSolverParticles>* CorotatedConstraint =
									new FXPBDCorotatedConstraints<FSolverReal, FSolverParticles>(
										Evolution->Particles(), Elements, /*bRecordMetric = */false, (FSolverReal)100000.0);

								int32 InitIndex = Evolution->AddConstraintInitRange(1, true);
								Evolution->ConstraintInits()[InitIndex] =
									[CorotatedConstraint](FSolverParticles& InParticles, const FSolverReal Dt)
								{
									CorotatedConstraint->Init();
								};


								int32 ConstraintIndex = Evolution->AddConstraintRuleRange(1, true);
								Evolution->ConstraintRules()[ConstraintIndex] =
									[CorotatedConstraint](FSolverParticles& InParticles, const FSolverReal Dt)
								{
									CorotatedConstraint->ApplyInParallel(InParticles, Dt);

								};
								CorotatedConstraints.Add(TUniquePtr<FXPBDCorotatedConstraints<FSolverReal, FSolverParticles>>(CorotatedConstraint));
							}

							if (Property.bDoSelfCollision)
							{
								// Add Surface Elements Node
								int32 SurfaceElementsOffset = SurfaceElements->Num();
								SurfaceElements->SetNum(NumSurfaceElements + SurfaceElementsOffset);
								for (uint32 edx = 0; edx < NumSurfaceElements; ++edx)
								{
									(*SurfaceElements)[edx + SurfaceElementsOffset] = ChaosTri(Indices[edx], ParticleStart);
								}

							}


							Time = 0.f;
							bSimulationInitialized = true;
						}
					}
				}
			}
		}
	}

	void FDeformableSolver::FPhysicsThreadAccess::InitializeCollisionBodies()
	{
		return Solver.InitializeCollisionBodies();
	}
	void FDeformableSolver::InitializeCollisionBodies()
	{
		if (Property.bUseFloor && Evolution->CollisionParticles().Size()==0)
		{
			Chaos::FVec3 Position(0.f);
			Chaos::FVec3 EulerRot(0.f);
			Evolution->AddCollisionParticleRange(1, 1, true);
			Evolution->CollisionParticles().X(0) = Position;
			Evolution->CollisionParticles().R(0) = Chaos::TRotation<Chaos::FReal, 3>::MakeFromEuler(EulerRot);
			Evolution->CollisionParticles().SetDynamicGeometry(0, MakeUnique<Chaos::TPlane<Chaos::FReal, 3>>(Chaos::FVec3(0.f, 0.f, 0.f), Chaos::FVec3(0.f, 0.f, 1.f)));
		}
	}

	void FDeformableSolver::FPhysicsThreadAccess::InitializeKinematicState(FThreadingProxy& InProxy)
	{
		Solver.InitializeKinematicState(InProxy);
	}
	void FDeformableSolver::InitializeKinematicState(FThreadingProxy& InProxy)
	{
		auto MKineticUpdate = [this](FSolverParticles& MParticles, const FSolverReal Dt, const FSolverReal MTime, const int32 Index)
		{
			if (0 <= Index && Index < this->MObjects.Num())
			{
				if (const UObject* Owner = this->MObjects[Index])
				{
					if (const FFleshThreadingProxy* Proxy = Proxies[Owner]->As<FFleshThreadingProxy>())
					{
						const FManagedArrayCollection& Rest = Proxy->GetRestCollection();
						const FTransform& InitialTransform = Proxy->GetInitialTransform();
						const TManagedArray<FVector3f>& Vertex = Rest.GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
						const FIntVector2& Range = Proxy->GetSolverParticleRange();

						// @todo(chaos) : reduce conversions
						auto ChaosVert = [](FVector3f V) { return Chaos::FVec3(V.X, V.Y, V.Z); };

						MParticles.X(Index) = InitialTransform.TransformPosition(ChaosVert(Vertex[Index - Range[0]]));
						MParticles.PAndInvM(Index).P = MParticles.X(Index);
					}
				}
			}
		};
		Evolution->SetKinematicUpdateFunction(MKineticUpdate);
	}

	void FDeformableSolver::FPhysicsThreadAccess::InitializeSelfCollisionVariables()
	{
		Solver.InitializeSelfCollisionVariables();
	}
	void FDeformableSolver::InitializeSelfCollisionVariables()
	{
		int32 NumParticles = Evolution->Particles().Size();
		SurfaceTriangleMesh->Init(*SurfaceElements);
		TriangleMeshCollisions.Reset(new FPBDTriangleMeshCollisions(
			0, Evolution->Particles().Size(), *SurfaceTriangleMesh, false, false));
		TSet<Chaos::TVec2<int32>>* InDisabledCollisionElements = new TSet<Chaos::TVec2<int32>>();
		for (int32 i = 0; i < (int32)NumParticles; i++)
		{
			Chaos::TVec2<int32> LocalEdge = { i, i };
			InDisabledCollisionElements->Add(LocalEdge);
		}
		CollisionSpringConstraint.Reset(new FPBDCollisionSpringConstraints(0, (int32)NumParticles, *SurfaceTriangleMesh, nullptr, MoveTemp(*InDisabledCollisionElements), 1.f, 1.f));
		int32 InitIndex1 = Evolution->AddConstraintInitRange(1, true);
		Evolution->ConstraintInits()[InitIndex1] =
			[this](FSolverParticles& InParticles, const FSolverReal Dt)
		{
			this->TriangleMeshCollisions->Init(InParticles);
			TArray<FPBDTriangleMeshCollisions::FGIAColor> EmptyGIAColors;
			this->CollisionSpringConstraint->Init(InParticles, TriangleMeshCollisions->GetSpatialHash(), static_cast<TConstArrayView<FPBDTriangleMeshCollisions::FGIAColor>>(EmptyGIAColors), EmptyGIAColors);
		};
		int32 ConstraintIndex1 = Evolution->AddConstraintRuleRange(1, true);
		Evolution->ConstraintRules()[ConstraintIndex1] =
			[this](FSolverParticles& InParticles, const FSolverReal Dt)
		{
			this->CollisionSpringConstraint->Apply(InParticles, Dt);
		};
	}


	void FDeformableSolver::FPhysicsThreadAccess::TickSimulation(FSolverReal DeltaTime)
	{
		Solver.TickSimulation(DeltaTime);
	}
	void FDeformableSolver::TickSimulation(FSolverReal DeltaTime)
	{
		if (!Property.FixTimeStep)
		{
			Evolution->AdvanceOneTimeStep(DeltaTime);
			Time += DeltaTime;
		}
		else
		{
			Evolution->AdvanceOneTimeStep(Property.TimeStepSize);
			Time += Property.TimeStepSize;
		}

		FOutputDataMap OutputBuffers;
		for (TPair< FThreadingProxy::FKey, TUniquePtr<FThreadingProxy> > & BaseProxyPair : Proxies)
		{
			UpdateOutputState(*BaseProxyPair.Value);
			if (FFleshThreadingProxy* Proxy = BaseProxyPair.Value->As<FFleshThreadingProxy>())
			{
				OutputBuffers.Add(Proxy->GetOwner(), TSharedPtr<FThreadingProxy::FOutputBuffer>(new FFleshThreadingProxy::FFleshOutputBuffer(*Proxy)));

				if (Property.CacheToFile)
				{
					// @todo
					//WriteFrame(Proxy, DeltaTime);
				}
			}

		}

		PushPackage(Frame, MoveTemp(OutputBuffers));
	}


	void FDeformableSolver::FPhysicsThreadAccess::PushPackage(int32 InFrame, FOutputDataMap&& InPackage)
	{
		Solver.PushPackage(InFrame, MoveTemp(InPackage));
	}
	void FDeformableSolver::PushPackage(int32 InFrame, FOutputDataMap&& InPackage)
	{
		FScopeLock Lock(&PackageMutex);
		OutputPackages.Push(TUniquePtr< FOutputPackage >(new FOutputPackage(InFrame, MoveTemp(InPackage))));
	}


	TUniquePtr<FOutputPackage>  FDeformableSolver::FGameThreadAccess::PullPackage()
	{
		return Solver.PullPackage();
	}
	TUniquePtr<FOutputPackage> FDeformableSolver::PullPackage()
	{
		FScopeLock Lock(&PackageMutex);
		if (OutputPackages.Num())
			return OutputPackages.Pop();
		return TUniquePtr<FOutputPackage>(nullptr);
	}

	void  FDeformableSolver::FGameThreadAccess::AddProxy(TUniquePtr<FThreadingProxy> InObject)
	{
		return Solver.AddProxy(TUniquePtr<FThreadingProxy>(InObject.Release()));
	}
	void FDeformableSolver::AddProxy(TUniquePtr<FThreadingProxy> InObject)
	{
		UninitializedProxys.Add(TUniquePtr< FThreadingProxy>(InObject.Release()));
	}

	void  FDeformableSolver::FPhysicsThreadAccess::UpdateOutputState(FThreadingProxy& InProxy)
	{
		Solver.UpdateOutputState(InProxy);
	}
	void FDeformableSolver::UpdateOutputState(FThreadingProxy& InProxy)
	{
		if (FFleshThreadingProxy* Proxy = InProxy.As<FFleshThreadingProxy>())
		{

			const FIntVector2& Range = Proxy->GetSolverParticleRange();
			if (0 <= Range[0])
			{
				// @todo(chaos) : reduce conversions
				auto UEVertd = [](Chaos::FVec3 V) { return FVector3d(V.X, V.Y, V.Z); };
				auto UEVertf = [](FVector3d V) { return FVector3f((float)V.X, (float)V.Y, (float)V.Z); };

				TManagedArray<FVector3f>& Position = Proxy->GetDynamicCollection().ModifyAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
				for (int32 vdx = 0; vdx < Position.Num(); vdx++)
				{
					Position[vdx] = UEVertf(UEVertd(Evolution->Particles().X(vdx + Range[0])));
				}
			}
		}
	}

	void  FDeformableSolver::FPhysicsThreadAccess::WriteFrame(FThreadingProxy& InProxy, const FSolverReal DeltaTime)
	{
		Solver.WriteFrame(InProxy, DeltaTime);
	}
	void FDeformableSolver::WriteFrame(FThreadingProxy& InProxy, const FSolverReal DeltaTime)
	{
		if (FFleshThreadingProxy* Proxy = InProxy.As<FFleshThreadingProxy>())
		{
			if (const FManagedArrayCollection* Rest = &Proxy->GetRestCollection())
			{
				const TManagedArray<FIntVector>& Indices = Rest->GetAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);

				WriteTrisGEO(Evolution->Particles(), Indices);
				FString file = FPaths::ProjectDir();
				file.Append(TEXT("/HoudiniOutput/DtLog.txt"));
				if (Frame == 0)
				{
					FFileHelper::SaveStringToFile(FString(TEXT("DeltaTime\r\n")), *file);
				}
				FFileHelper::SaveStringToFile((FString::SanitizeFloat(DeltaTime) + FString(TEXT("\r\n"))), *file, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
			}
		}
	}

	void  FDeformableSolver::FPhysicsThreadAccess::WriteTrisGEO(const FSolverParticles& Particles, const TManagedArray<FIntVector>& Mesh)
	{
		Solver.WriteTrisGEO(Particles, Mesh);
	}
	void FDeformableSolver::WriteTrisGEO(const FSolverParticles& Particles, const TManagedArray<FIntVector>& Mesh)
	{
		FString file = FPaths::ProjectDir();
		//file.Append(TEXT("\HoudiniOuput\Test.geo"));
		file.Append(TEXT("/HoudiniOutput/sim_frame_"));
		file.Append(FString::FromInt(Frame));
		file.Append(TEXT(".geo"));

		int32 Np = Particles.Size();
		int32 NPrims = Mesh.Num();

		// We will use this FileManager to deal with the file.
		IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
		FFileHelper::SaveStringToFile(FString(TEXT("PGEOMETRY V5\r\n")), *file);
		FString HeaderInfo = FString(TEXT("NPoints ")) + FString::FromInt(Np) + FString(TEXT(" NPrims ")) + FString::FromInt(NPrims) + FString(TEXT("\r\n"));
		FString MoreHeader = FString(TEXT("NPointGroups 0 NPrimGroups 0\r\nNPointAttrib 0 NVertexAttrib 0 NPrimAttrib 0 NAttrib 0\r\n"));

		FFileHelper::SaveStringToFile(HeaderInfo, *file, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
		FFileHelper::SaveStringToFile(MoreHeader, *file, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);

		for (int32 i = 0; i < Np; i++) {

			FString ParticleInfo = FString::SanitizeFloat(Particles.X(i)[0]) + FString(TEXT(" ")) + FString::SanitizeFloat(Particles.X(i)[1]) + FString(TEXT(" ")) + FString::SanitizeFloat(Particles.X(i)[2]) + FString(TEXT(" ")) + FString::FromInt(1) + FString(TEXT("\r\n"));
			FFileHelper::SaveStringToFile(ParticleInfo, *file, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);

		}

		for (int32 i = 0; i < Mesh.Num(); i++) {
			//outstream << "Poly 3 < ";
			FString ElementToWrite = FString(TEXT("Poly 3 < ")) + FString::FromInt(Mesh[i][0]) + FString(TEXT(" ")) + FString::FromInt(Mesh[i][1]) + FString(TEXT(" ")) + FString::FromInt(Mesh[i][2]) + FString(TEXT("\r\n"));
			FFileHelper::SaveStringToFile(ElementToWrite, *file, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
		}

		FFileHelper::SaveStringToFile(FString(TEXT("beginExtra\n")), *file, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
		FFileHelper::SaveStringToFile(FString(TEXT("endExtra\n")), *file, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);

	}
}; // Namespace Chaos::Softs