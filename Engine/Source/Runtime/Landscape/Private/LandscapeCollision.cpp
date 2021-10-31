// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformStackWalk.h"
#include "Misc/Guid.h"
#include "Stats/Stats.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FeedbackContext.h"
#include "UObject/PropertyPortFlags.h"
#include "EngineDefines.h"
#include "Engine/EngineTypes.h"
#include "Components/SceneComponent.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Misc/SecureHash.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "PhysxUserData.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "AI/NavigationSystemBase.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapePrivate.h"
#include "PhysicsPublic.h"
#include "LandscapeDataAccess.h"
#include "PhysXPublic.h"
#include "PhysicsEngine/PhysXSupport.h"
#include "DerivedDataCacheInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeMeshCollisionComponent.h"
#include "FoliageInstanceBase.h"
#include "InstancedFoliageActor.h"
#include "InstancedFoliage.h"
#include "AI/NavigationSystemHelpers.h"
#include "Engine/CollisionProfile.h"
#include "ProfilingDebugging/CookStats.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "EngineGlobals.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "Physics/PhysicsInterfaceUtils.h"

#if WITH_EDITOR && PHYSICS_INTERFACE_PHYSX
	#include "IPhysXCooking.h"
#endif

#if WITH_CHAOS
#include "PhysXToChaosUtil.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Vector.h"
#include "Chaos/Core.h"
#include "Chaos/HeightField.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/Experimental/ChaosDerivedData.h"
#include "PhysicsEngine/Experimental/ChaosCooking.h"
#include "Chaos/ChaosArchive.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#endif

using namespace PhysicsInterfaceTypes;

// Global switch for whether to read/write to DDC for landscape cooked data
bool GLandscapeCollisionSkipDDC = false;

#if ENABLE_COOK_STATS
namespace LandscapeCollisionCookStats
{
	static FCookStats::FDDCResourceUsageStats HeightfieldUsageStats;
	static FCookStats::FDDCResourceUsageStats MeshUsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		HeightfieldUsageStats.LogStats(AddStat, TEXT("LandscapeCollision.Usage"), TEXT("Heightfield"));
		MeshUsageStats.LogStats(AddStat, TEXT("LandscapeCollision.Usage"), TEXT("Mesh"));
	});
}
#endif

TMap<FGuid, ULandscapeHeightfieldCollisionComponent::FHeightfieldGeometryRef* > GSharedHeightfieldRefs;

ULandscapeHeightfieldCollisionComponent::FHeightfieldGeometryRef::FHeightfieldGeometryRef(FGuid& InGuid)
	: Guid(InGuid)
{
}

ULandscapeHeightfieldCollisionComponent::FHeightfieldGeometryRef::~FHeightfieldGeometryRef()
{
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
	// Free the existing heightfield data.
	if (RBHeightfield)
	{
		GPhysXPendingKillHeightfield.Add(RBHeightfield);
		RBHeightfield = NULL;
	}
#if WITH_EDITOR
	if (RBHeightfieldEd)
	{
		GPhysXPendingKillHeightfield.Add(RBHeightfieldEd);
		RBHeightfieldEd = NULL;
	}
#endif// WITH_EDITOR
#endif// WITH_PHYSX

	// Remove ourselves from the shared map.
	GSharedHeightfieldRefs.Remove(Guid);
}

TMap<FGuid, ULandscapeMeshCollisionComponent::FTriMeshGeometryRef* > GSharedMeshRefs;

ULandscapeMeshCollisionComponent::FTriMeshGeometryRef::FTriMeshGeometryRef()
#if WITH_PHYSX
	:	RBTriangleMesh(nullptr)
#if WITH_EDITOR
	, RBTriangleMeshEd(nullptr)
#endif	//WITH_EDITOR
#endif	//WITH_PHYSX
{}

ULandscapeMeshCollisionComponent::FTriMeshGeometryRef::FTriMeshGeometryRef(FGuid& InGuid)
	: Guid(InGuid)
#if WITH_PHYSX
	, RBTriangleMesh(nullptr)
#if WITH_EDITOR
	, RBTriangleMeshEd(nullptr)
#endif	//WITH_EDITOR
#endif	//WITH_PHYSX
{}

ULandscapeMeshCollisionComponent::FTriMeshGeometryRef::~FTriMeshGeometryRef()
{
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
	// Free the existing heightfield data.
	if (RBTriangleMesh)
	{
		GPhysXPendingKillTriMesh.Add(RBTriangleMesh);
		RBTriangleMesh = nullptr;
	}

#if WITH_EDITOR
	if (RBTriangleMeshEd)
	{
		GPhysXPendingKillTriMesh.Add(RBTriangleMeshEd);
		RBTriangleMeshEd = nullptr;
	}
#endif// WITH_EDITOR
#endif// WITH_PHYSX

	// Remove ourselves from the shared map.
	GSharedMeshRefs.Remove(Guid);
}

// Generate a new guid to force a recache of landscape collison derived data
#define LANDSCAPE_COLLISION_DERIVEDDATA_VER	TEXT("CC58B9FA08AD47E3BF06976E60B693C3")

static FString GetHFDDCKeyString(const FName& Format, bool bDefMaterial, const FGuid& StateId, const TArray<UPhysicalMaterial*>& PhysicalMaterials)
{
	FGuid CombinedStateId;
	
	ensure(StateId.IsValid());

	if (bDefMaterial)
	{
		CombinedStateId = StateId;
	}
	else
	{
		// Build a combined state ID based on both the heightfield state and all physical materials.
		FBufferArchive CombinedStateAr;

		// Add main heightfield state
		FGuid HeightfieldState = StateId;
		CombinedStateAr << HeightfieldState;

		// Add physical materials
		for (UPhysicalMaterial* PhysicalMaterial : PhysicalMaterials)
		{
			FString PhysicalMaterialName = PhysicalMaterial->GetPathName().ToUpper();
			CombinedStateAr << PhysicalMaterialName;
		}

		uint32 Hash[5];
		FSHA1::HashBuffer(CombinedStateAr.GetData(), CombinedStateAr.Num(), (uint8*)Hash);
		CombinedStateId = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
	}

#if PHYSICS_INTERFACE_PHYSX
	const FString InterfacePrefix = TEXT("PHYSX");
#elif WITH_CHAOS
	const FString InterfacePrefix = FString::Printf(TEXT("%s_%s"), TEXT("CHAOS"), *Chaos::ChaosVersionString);
#else
	const FString InterfacePrefix = TEXT("UNDEFINED");
#endif

	const FString KeyPrefix = FString::Printf(TEXT("%s_%s_%s"), *InterfacePrefix, *Format.ToString(), (bDefMaterial ? TEXT("VIS") : TEXT("FULL")));
	return FDerivedDataCacheInterface::BuildCacheKey(*KeyPrefix, LANDSCAPE_COLLISION_DERIVEDDATA_VER, *CombinedStateId.ToString());
}

void ULandscapeHeightfieldCollisionComponent::OnRegister()
{
	Super::OnRegister();

	if (GetLandscapeProxy())
	{
		// AActor::GetWorld checks for Unreachable and BeginDestroyed
		UWorld* World = GetLandscapeProxy()->GetWorld();
		if (World)
		{
			ULandscapeInfo* Info = GetLandscapeInfo();
			if (Info)
			{
				Info->RegisterCollisionComponent(this);
			}
		}
	}
}

void ULandscapeHeightfieldCollisionComponent::OnUnregister()
{
	Super::OnUnregister();

	if (GetLandscapeProxy())
	{
		// AActor::GetWorld checks for Unreachable and BeginDestroyed
		UWorld* World = GetLandscapeProxy()->GetWorld();

		// Game worlds don't have landscape infos
		if (World)
		{
			ULandscapeInfo* Info = GetLandscapeInfo();
			if (Info)
			{
				Info->UnregisterCollisionComponent(this);
			}
		}
	}
}

ECollisionEnabled::Type ULandscapeHeightfieldCollisionComponent::GetCollisionEnabled() const
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		ALandscapeProxy* Proxy = GetLandscapeProxy();

		return Proxy->BodyInstance.GetCollisionEnabled();
	}
	return ECollisionEnabled::QueryAndPhysics;
}

ECollisionResponse ULandscapeHeightfieldCollisionComponent::GetCollisionResponseToChannel(ECollisionChannel Channel) const
{
	ALandscapeProxy* Proxy = GetLandscapeProxy();

	return Proxy->BodyInstance.GetResponseToChannel(Channel);
}

ECollisionChannel ULandscapeHeightfieldCollisionComponent::GetCollisionObjectType() const
{
	ALandscapeProxy* Proxy = GetLandscapeProxy();

	return Proxy->BodyInstance.GetObjectType();
}

const FCollisionResponseContainer& ULandscapeHeightfieldCollisionComponent::GetCollisionResponseToChannels() const
{
	ALandscapeProxy* Proxy = GetLandscapeProxy();

	return Proxy->BodyInstance.GetResponseToChannels();
}

void ULandscapeHeightfieldCollisionComponent::OnCreatePhysicsState()
{
	USceneComponent::OnCreatePhysicsState(); // route OnCreatePhysicsState, skip PrimitiveComponent implementation

	if (!BodyInstance.IsValidBodyInstance())
	{
		CreateCollisionObject();

		if (IsValidRef(HeightfieldRef))
		{
			// Make transform for this landscape component PxActor
			FTransform LandscapeComponentTransform = GetComponentToWorld();
			FMatrix LandscapeComponentMatrix = LandscapeComponentTransform.ToMatrixWithScale();
			FTransform LandscapeShapeTM = FTransform::Identity;

			// Get the scale to give to PhysX
			FVector LandscapeScale = LandscapeComponentMatrix.ExtractScaling();

			bool bIsMirrored = LandscapeComponentMatrix.Determinant() < 0.f;
			if (!bIsMirrored)
			{
				// Unreal and PhysX have opposite handedness, so we need to translate the origin and rearrange the data
				LandscapeShapeTM.SetTranslation(FVector(-CollisionSizeQuads*CollisionScale*LandscapeScale.X, 0, 0));
			}


			// Reorder the axes
			FVector TerrainX = LandscapeComponentMatrix.GetScaledAxis(EAxis::X);
			FVector TerrainY = LandscapeComponentMatrix.GetScaledAxis(EAxis::Y);
			FVector TerrainZ = LandscapeComponentMatrix.GetScaledAxis(EAxis::Z);
			LandscapeComponentMatrix.SetAxis(0, TerrainX);
			LandscapeComponentMatrix.SetAxis(2, TerrainY);
			LandscapeComponentMatrix.SetAxis(1, TerrainZ);
			
			const bool bCreateSimpleCollision = SimpleCollisionSizeQuads > 0;
			const float SimpleCollisionScale = bCreateSimpleCollision ? CollisionScale * CollisionSizeQuads / SimpleCollisionSizeQuads : 0;

			// Create the geometry
			FVector FinalScale(LandscapeScale.X * CollisionScale, LandscapeScale.Y * CollisionScale, LandscapeScale.Z * LANDSCAPE_ZSCALE);

#if PHYSICS_INTERFACE_PHYSX
			PxTransform PhysXLandscapeComponentTransform = U2PTransform(FTransform(LandscapeComponentMatrix));
			PxHeightFieldGeometry LandscapeComponentGeom(HeightfieldRef->RBHeightfield, PxMeshGeometryFlag::eDOUBLE_SIDED, LandscapeScale.Z * LANDSCAPE_ZSCALE, LandscapeScale.Y * CollisionScale, LandscapeScale.X * CollisionScale);

			if (LandscapeComponentGeom.isValid())
			{
				// Creating both a sync and async actor, since this object is static

				// Create the sync scene actor
				PxRigidStatic* HeightFieldActorSync = GPhysXSDK->createRigidStatic(PhysXLandscapeComponentTransform);
				PxShape* HeightFieldShapeSync = GPhysXSDK->createShape(LandscapeComponentGeom, HeightfieldRef->UsedPhysicalMaterialArray.GetData(), HeightfieldRef->UsedPhysicalMaterialArray.Num(), true);
				HeightFieldShapeSync->setLocalPose(U2PTransform(LandscapeShapeTM));
				check(HeightFieldShapeSync);

				// Setup filtering
				FCollisionFilterData QueryFilterData, SimFilterData;
				CreateShapeFilterData(GetCollisionObjectType(), FMaskFilter(0), GetOwner()->GetUniqueID(), GetCollisionResponseToChannels(), GetUniqueID(), 0, QueryFilterData, SimFilterData, true, false, true);

				// Heightfield is used for simple and complex collision
				QueryFilterData.Word3 |= bCreateSimpleCollision ? EPDF_ComplexCollision : (EPDF_SimpleCollision | EPDF_ComplexCollision);
				SimFilterData.Word3 |= bCreateSimpleCollision ? EPDF_ComplexCollision : (EPDF_SimpleCollision | EPDF_ComplexCollision);
				HeightFieldShapeSync->setQueryFilterData(U2PFilterData(QueryFilterData));
				HeightFieldShapeSync->setSimulationFilterData(U2PFilterData(SimFilterData));
				HeightFieldShapeSync->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
				HeightFieldShapeSync->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
				HeightFieldShapeSync->setFlag(PxShapeFlag::eVISUALIZATION, true);

				HeightFieldActorSync->attachShape(*HeightFieldShapeSync);

				// attachShape holds its own ref(), so release this here.
				HeightFieldShapeSync->release();

				if (bCreateSimpleCollision)
				{
					PxHeightFieldGeometry LandscapeComponentGeomSimple(HeightfieldRef->RBHeightfieldSimple, PxMeshGeometryFlags(), LandscapeScale.Z * LANDSCAPE_ZSCALE, LandscapeScale.Y * SimpleCollisionScale, LandscapeScale.X * SimpleCollisionScale);
					check(LandscapeComponentGeomSimple.isValid());
					PxShape* HeightFieldShapeSimpleSync = GPhysXSDK->createShape(LandscapeComponentGeomSimple, HeightfieldRef->UsedPhysicalMaterialArray.GetData(), HeightfieldRef->UsedPhysicalMaterialArray.Num(), true);
					HeightFieldShapeSimpleSync->setLocalPose(U2PTransform(LandscapeShapeTM));
					check(HeightFieldShapeSimpleSync);

					// Setup filtering
					FCollisionFilterData QueryFilterDataSimple = QueryFilterData;
					FCollisionFilterData SimFilterDataSimple = SimFilterData;
					QueryFilterDataSimple.Word3 = (QueryFilterDataSimple.Word3 & ~EPDF_ComplexCollision) | EPDF_SimpleCollision;
					SimFilterDataSimple.Word3 = (SimFilterDataSimple.Word3 & ~EPDF_ComplexCollision) | EPDF_SimpleCollision;
					HeightFieldShapeSimpleSync->setQueryFilterData(U2PFilterData(QueryFilterDataSimple));
					HeightFieldShapeSimpleSync->setSimulationFilterData(U2PFilterData(SimFilterDataSimple));
					HeightFieldShapeSimpleSync->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
					HeightFieldShapeSimpleSync->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
					HeightFieldShapeSimpleSync->setFlag(PxShapeFlag::eVISUALIZATION, true);

					HeightFieldActorSync->attachShape(*HeightFieldShapeSimpleSync);

					// attachShape holds its own ref(), so release this here.
					HeightFieldShapeSimpleSync->release();
				}

#if WITH_EDITOR
				// Create a shape for a heightfield which is used only by the landscape editor
				if (!GetWorld()->IsGameWorld())
				{
					PxHeightFieldGeometry LandscapeComponentGeomEd(HeightfieldRef->RBHeightfieldEd, PxMeshGeometryFlags(), LandscapeScale.Z * LANDSCAPE_ZSCALE, LandscapeScale.Y * CollisionScale, LandscapeScale.X * CollisionScale);
					if (LandscapeComponentGeomEd.isValid())
					{
#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
						UE_LOG(LogLandscape, Warning, TEXT("Failed to create editor shapes, currently unimplemented for Chaos"));
#else
						FPhysicsMaterialHandle_PhysX MaterialHandle = GEngine->DefaultPhysMaterial->GetPhysicsMaterial();
						PxMaterial* PDefaultMat = MaterialHandle.Material;
						PxShape* HeightFieldEdShapeSync = GPhysXSDK->createShape(LandscapeComponentGeomEd, &PDefaultMat, 1, true);
						HeightFieldEdShapeSync->setLocalPose(U2PTransform(LandscapeShapeTM));
						check(HeightFieldEdShapeSync);

						FCollisionResponseContainer CollisionResponse;
						CollisionResponse.SetAllChannels(ECollisionResponse::ECR_Ignore);
						CollisionResponse.SetResponse(ECollisionChannel::ECC_Visibility, ECR_Block);
						FCollisionFilterData QueryFilterDataEd, SimFilterDataEd;
						CreateShapeFilterData(ECollisionChannel::ECC_Visibility, FMaskFilter(0), GetOwner()->GetUniqueID(), CollisionResponse, GetUniqueID(), 0, QueryFilterDataEd, SimFilterDataEd, true, false, true);

						QueryFilterDataEd.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);
						HeightFieldEdShapeSync->setQueryFilterData(U2PFilterData(QueryFilterDataEd));
						HeightFieldEdShapeSync->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);

						HeightFieldActorSync->attachShape(*HeightFieldEdShapeSync);

						// attachShape holds its own ref(), so release this here.
						HeightFieldEdShapeSync->release();
#endif
					}
				}
#endif// WITH_EDITOR

				FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();

				// Set body instance data
				BodyInstance.PhysicsUserData = FPhysicsUserData(&BodyInstance);
				BodyInstance.OwnerComponent = this;

				BodyInstance.ActorHandle.SyncActor = HeightFieldActorSync;
				HeightFieldActorSync->userData = &BodyInstance.PhysicsUserData;

				// Add to scenes
				PxScene* SyncScene = PhysScene->GetPxScene();
				SCOPED_SCENE_WRITE_LOCK(SyncScene);
				SyncScene->addActor(*HeightFieldActorSync);
			}

#elif WITH_CHAOS
			{
				FActorCreationParams Params;
				Params.InitialTM = LandscapeComponentTransform;
				Params.InitialTM.SetScale3D(FVector(0));
				Params.bQueryOnly = true;
				Params.bStatic = true;
				Params.Scene = GetWorld()->GetPhysicsScene();
				FPhysicsActorHandle PhysHandle;
				FPhysicsInterface::CreateActor(Params, PhysHandle);
				Chaos::FRigidBodyHandle_External& Body_External = PhysHandle->GetGameThreadAPI();

				Chaos::FShapesArray ShapeArray;
				TArray<TUniquePtr<Chaos::FImplicitObject>> Geoms;

				// First add complex geometry
				TUniquePtr<Chaos::FPerShapeData> NewShape = Chaos::FPerShapeData::CreatePerShapeData(ShapeArray.Num());

				HeightfieldRef->Heightfield->SetScale(FinalScale);
				TUniquePtr<Chaos::TImplicitObjectTransformed<float, 3>> ChaosHeightFieldFromCooked = MakeUnique<Chaos::TImplicitObjectTransformed<float, 3>>(MakeSerializable(HeightfieldRef->Heightfield), Chaos::TRigidTransform<float, 3>(FTransform::Identity));

				// Setup filtering
				FCollisionFilterData QueryFilterData, SimFilterData;
				CreateShapeFilterData(GetCollisionObjectType(), FMaskFilter(0), GetOwner()->GetUniqueID(), GetCollisionResponseToChannels(), GetUniqueID(), 0, QueryFilterData, SimFilterData, true, false, true);

				// Heightfield is used for simple and complex collision
				QueryFilterData.Word3 |= bCreateSimpleCollision ? EPDF_ComplexCollision : (EPDF_SimpleCollision | EPDF_ComplexCollision);
				SimFilterData.Word3 |= bCreateSimpleCollision ? EPDF_ComplexCollision : (EPDF_SimpleCollision | EPDF_ComplexCollision);

				NewShape->SetGeometry(MakeSerializable(ChaosHeightFieldFromCooked));
				NewShape->SetQueryData(QueryFilterData);
				NewShape->SetSimData(SimFilterData);
				NewShape->SetMaterials(HeightfieldRef->UsedChaosMaterials);

				Geoms.Emplace(MoveTemp(ChaosHeightFieldFromCooked));
				ShapeArray.Emplace(MoveTemp(NewShape));

				// Add simple geometry if necessary
				if(bCreateSimpleCollision)
				{
					TUniquePtr<Chaos::FPerShapeData> NewSimpleShape = Chaos::FPerShapeData::CreatePerShapeData(ShapeArray.Num());
					
					FVector FinalSimpleCollisionScale(LandscapeScale.X* SimpleCollisionScale, LandscapeScale.Y* SimpleCollisionScale, LandscapeScale.Z* LANDSCAPE_ZSCALE);
					HeightfieldRef->HeightfieldSimple->SetScale(FinalSimpleCollisionScale);
					TUniquePtr<Chaos::TImplicitObjectTransformed<float, 3>> ChaosSimpleHeightFieldFromCooked = MakeUnique<Chaos::TImplicitObjectTransformed<float, 3>>(MakeSerializable(HeightfieldRef->HeightfieldSimple), Chaos::TRigidTransform<float, 3>(FTransform::Identity));

					FCollisionFilterData QueryFilterDataSimple = QueryFilterData;
					FCollisionFilterData SimFilterDataSimple = SimFilterData;
					QueryFilterDataSimple.Word3 = (QueryFilterDataSimple.Word3 & ~EPDF_ComplexCollision) | EPDF_SimpleCollision;
					SimFilterDataSimple.Word3 = (SimFilterDataSimple.Word3 & ~EPDF_ComplexCollision) | EPDF_SimpleCollision;

					NewSimpleShape->SetGeometry(MakeSerializable(ChaosSimpleHeightFieldFromCooked));
					NewSimpleShape->SetQueryData(QueryFilterDataSimple);
					NewSimpleShape->SetSimData(SimFilterDataSimple);

					Geoms.Emplace(MoveTemp(ChaosSimpleHeightFieldFromCooked));
					ShapeArray.Emplace(MoveTemp(NewSimpleShape));
				}

#if WITH_EDITOR
				// Create a shape for a heightfield which is used only by the landscape editor
				if(!GetWorld()->IsGameWorld())
				{
					TUniquePtr<Chaos::FPerShapeData> NewEditorShape = Chaos::FPerShapeData::CreatePerShapeData(ShapeArray.Num());

					HeightfieldRef->EditorHeightfield->SetScale(FinalScale);
					TUniquePtr<Chaos::TImplicitObjectTransformed<float, 3>> ChaosEditorHeightFieldFromCooked = MakeUnique<Chaos::TImplicitObjectTransformed<float, 3>>(MakeSerializable(HeightfieldRef->EditorHeightfield), Chaos::TRigidTransform<float, 3>(FTransform::Identity));

					FCollisionResponseContainer CollisionResponse;
					CollisionResponse.SetAllChannels(ECollisionResponse::ECR_Ignore);
					CollisionResponse.SetResponse(ECollisionChannel::ECC_Visibility, ECR_Block);
					FCollisionFilterData QueryFilterDataEd, SimFilterDataEd;
					CreateShapeFilterData(ECollisionChannel::ECC_Visibility, FMaskFilter(0), GetOwner()->GetUniqueID(), CollisionResponse, GetUniqueID(), 0, QueryFilterDataEd, SimFilterDataEd, true, false, true);

					QueryFilterDataEd.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);

					NewEditorShape->SetGeometry(MakeSerializable(ChaosEditorHeightFieldFromCooked));
					NewEditorShape->SetQueryData(QueryFilterDataEd);
					NewEditorShape->SetSimData(SimFilterDataEd);

					Geoms.Emplace(MoveTemp(ChaosEditorHeightFieldFromCooked));
					ShapeArray.Emplace(MoveTemp(NewEditorShape));
				}
#endif
				// Push the shapes to the actor
				if(Geoms.Num() == 1)
				{
					Body_External.SetGeometry(MoveTemp(Geoms[0]));
				}
				else
				{
					Body_External.SetGeometry(MakeUnique<Chaos::FImplicitObjectUnion>(MoveTemp(Geoms)));
				}

				// Construct Shape Bounds
				for (auto& Shape : ShapeArray)
				{
					Chaos::FRigidTransform3 WorldTransform = Chaos::FRigidTransform3(Body_External.X(), Body_External.R());
					Shape->UpdateShapeBounds(WorldTransform);
				}



				Body_External.SetShapesArray(MoveTemp(ShapeArray));

				// Push the actor to the scene
				FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();

				// Set body instance data
				BodyInstance.PhysicsUserData = FPhysicsUserData(&BodyInstance);
				BodyInstance.OwnerComponent = this;
				BodyInstance.ActorHandle = PhysHandle;

				Body_External.SetUserData(&BodyInstance.PhysicsUserData);

				TArray<FPhysicsActorHandle> Actors;
				Actors.Add(PhysHandle);

				bool bImmediateAccelStructureInsertion = true;
				PhysScene->AddActorsToScene_AssumesLocked(Actors, bImmediateAccelStructureInsertion);

				PhysScene->AddToComponentMaps(this, PhysHandle);
				if (BodyInstance.bNotifyRigidBodyCollision)
				{
					PhysScene->RegisterForCollisionEvents(this);
				}

			}
#endif // WITH_CHAOS
		}
	}
}

void ULandscapeHeightfieldCollisionComponent::OnDestroyPhysicsState()
{
	Super::OnDestroyPhysicsState();
	
#if WITH_CHAOS
	if (FPhysScene_Chaos* PhysScene = GetWorld()->GetPhysicsScene())
	{
		FPhysicsActorHandle& ActorHandle = BodyInstance.GetPhysicsActorHandle();
		if (FPhysicsInterface::IsValid(ActorHandle))
		{
			PhysScene->RemoveFromComponentMaps(ActorHandle);
		}
		if (BodyInstance.bNotifyRigidBodyCollision)
		{
			PhysScene->UnRegisterForCollisionEvents(this);
		}
	}
#endif // WITH_CHAOS
}

void ULandscapeHeightfieldCollisionComponent::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
	Super::ApplyWorldOffset(InOffset, bWorldShift);

	if (!bWorldShift || !FPhysScene::SupportsOriginShifting())
	{
		RecreatePhysicsState();
	}
}

void ULandscapeHeightfieldCollisionComponent::CreateCollisionObject()
{
#if WITH_CHAOS
	LLM_SCOPE(ELLMTag::ChaosLandscape);
#else
	//NOTE: this currently gets ignored because of low level allocator
	LLM_SCOPE(ELLMTag::PhysXLandscape);
#endif
	// If we have not created a heightfield yet - do it now.
	if (!IsValidRef(HeightfieldRef))
	{
		UWorld* World = GetWorld();

		FHeightfieldGeometryRef* ExistingHeightfieldRef = nullptr;
		bool bCheckDDC = true;

		if (!HeightfieldGuid.IsValid())
		{
			HeightfieldGuid = FGuid::NewGuid();
			bCheckDDC = false;
		}
		else
		{
			// Look for a heightfield object with the current Guid (this occurs with PIE)
			ExistingHeightfieldRef = GSharedHeightfieldRefs.FindRef(HeightfieldGuid);
		}

		if (ExistingHeightfieldRef)
		{
			HeightfieldRef = ExistingHeightfieldRef;
		}
		else
		{
#if WITH_EDITOR
			// This should only occur if a level prior to VER_UE4_LANDSCAPE_COLLISION_DATA_COOKING 
			// was resaved using a commandlet and not saved in the editor, or if a PhysicalMaterial asset was deleted.
			if (CookedPhysicalMaterials.Num() == 0 || CookedPhysicalMaterials.Contains(nullptr))
			{
				bCheckDDC = false;
			}

			// Prepare heightfield data
			static FName PhysicsFormatName(FPlatformProperties::GetPhysicsFormat());
			CookCollisionData(PhysicsFormatName, false, bCheckDDC, CookedCollisionData, CookedPhysicalMaterials);

			// The World will clean up any speculatively-loaded data we didn't end up using.
			SpeculativeDDCRequest.Reset();
#endif //WITH_EDITOR

			if (CookedCollisionData.Num())
			{
				HeightfieldRef = GSharedHeightfieldRefs.Add(HeightfieldGuid, new FHeightfieldGeometryRef(HeightfieldGuid));

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
				// Create heightfield shape
				{
					FPhysXInputStream HeightFieldStream(CookedCollisionData.GetData(), CookedCollisionData.Num());
					HeightfieldRef->RBHeightfield = GPhysXSDK->createHeightField(HeightFieldStream);
					if (SimpleCollisionSizeQuads > 0)
					{
						HeightfieldRef->RBHeightfieldSimple = GPhysXSDK->createHeightField(HeightFieldStream);
					}
				}

				for (UPhysicalMaterial* PhysicalMaterial : CookedPhysicalMaterials)
				{
					const FPhysicsMaterialHandle_PhysX& MaterialHandle = PhysicalMaterial->GetPhysicsMaterial();
					HeightfieldRef->UsedPhysicalMaterialArray.Add(MaterialHandle.Material);
				}

				// Release cooked collison data
				// In cooked builds created collision object will never be deleted while component is alive, so we don't need this data anymore
				if (FPlatformProperties::RequiresCookedData() || World->IsGameWorld())
				{
					CookedCollisionData.Empty();
				}

#if WITH_EDITOR
				// Create heightfield for the landscape editor (no holes in it)
				if (!World->IsGameWorld())
				{
					TArray<UPhysicalMaterial*> CookedMaterialsEd;
					if (CookCollisionData(PhysicsFormatName, true, bCheckDDC, CookedCollisionDataEd, CookedMaterialsEd))
					{
						FPhysXInputStream HeightFieldStream(CookedCollisionDataEd.GetData(), CookedCollisionDataEd.Num());
						HeightfieldRef->RBHeightfieldEd = GPhysXSDK->createHeightField(HeightFieldStream);
					}
				}
#endif //WITH_EDITOR
#elif WITH_CHAOS
				// Create heightfields
				{
					FMemoryReader Reader(CookedCollisionData);
					Chaos::FChaosArchive Ar(Reader);
					bool bContainsSimple = false;
					Ar << bContainsSimple;
					Ar << HeightfieldRef->Heightfield;

					if(bContainsSimple)
					{
						Ar << HeightfieldRef->HeightfieldSimple;
					}
				}

				// Register materials
				for(UPhysicalMaterial* PhysicalMaterial : CookedPhysicalMaterials)
				{
					//todo: total hack until we get landscape fully converted to chaos
					HeightfieldRef->UsedChaosMaterials.Add(PhysicalMaterial->GetPhysicsMaterial());
				}

				// Release cooked collison data
				// In cooked builds created collision object will never be deleted while component is alive, so we don't need this data anymore
				if(FPlatformProperties::RequiresCookedData() || World->IsGameWorld())
				{
					CookedCollisionData.Empty();
				}

#if WITH_EDITOR
				// Create heightfield for the landscape editor (no holes in it)
				if(!World->IsGameWorld())
				{
					TArray<UPhysicalMaterial*> CookedMaterialsEd;
					if(CookCollisionData(PhysicsFormatName, true, bCheckDDC, CookedCollisionDataEd, CookedMaterialsEd))
					{
						FMemoryReader Reader(CookedCollisionDataEd);
						Chaos::FChaosArchive Ar(Reader);

						// Don't actually care about this but need to strip it out of the data
						bool bContainsSimple = false;
						Ar << bContainsSimple;
						Ar << HeightfieldRef->EditorHeightfield;

						CookedCollisionDataEd.Empty();
					}
				}
#endif //WITH_EDITOR
#endif
			}
		}
	}
}

#if WITH_EDITOR
void ULandscapeHeightfieldCollisionComponent::SpeculativelyLoadAsyncDDCCollsionData()
{
#if WITH_PHYSX
	if (GetLinkerUE4Version() >= VER_UE4_LANDSCAPE_SERIALIZE_PHYSICS_MATERIALS && !GLandscapeCollisionSkipDDC)
	{
		UWorld* World = GetWorld();
		if (World && HeightfieldGuid.IsValid() && CookedPhysicalMaterials.Num() > 0 && GSharedHeightfieldRefs.FindRef(HeightfieldGuid) == nullptr)
		{
			static FName PhysicsFormatName(FPlatformProperties::GetPhysicsFormat());

			FString Key = GetHFDDCKeyString(PhysicsFormatName, false, HeightfieldGuid, CookedPhysicalMaterials);
			uint32 Handle = GetDerivedDataCacheRef().GetAsynchronous(*Key, GetPathName());
			check(!SpeculativeDDCRequest.IsValid());
			SpeculativeDDCRequest = MakeShareable(new FAsyncPreRegisterDDCRequest(Key, Handle));
			World->AsyncPreRegisterDDCRequests.Add(SpeculativeDDCRequest);
		}
	}
#endif
}

#if PHYSICS_INTERFACE_PHYSX
TArray<PxHeightFieldSample> ConvertHeightfieldDataForPhysx(
	const ULandscapeHeightfieldCollisionComponent* const Component,
	const int32 CollisionSizeVerts,
	const bool bIsMirrored,
	const uint16* Heights,
	const bool bUseDefMaterial,
	UPhysicalMaterial* const DefMaterial,
	const uint8* DominantLayers,
	const uint8* RenderPhysicalMaterialIds,
	TArray<UPhysicalMaterial*> const& PhysicalMaterialRenderObjects,
	TArray<UPhysicalMaterial*>& InOutMaterials) 
{
	const int32 NumSamples = FMath::Square(CollisionSizeVerts);
	check(DefMaterial);
	// Might return INDEX_NONE if DefMaterial wasn't added yet
	int32 DefaultMaterialIndex = InOutMaterials.IndexOfByKey(DefMaterial);
	
	TArray<PxHeightFieldSample> Samples;
	Samples.Reserve(NumSamples);
	Samples.AddZeroed(NumSamples);

	for (int32 RowIndex = 0; RowIndex < CollisionSizeVerts; RowIndex++)
	{
		for (int32 ColIndex = 0; ColIndex < CollisionSizeVerts; ColIndex++)
		{
			int32 SrcSampleIndex = (ColIndex * CollisionSizeVerts) + (bIsMirrored ? RowIndex : (CollisionSizeVerts - RowIndex - 1));
			int32 DstSampleIndex = (RowIndex * CollisionSizeVerts) + ColIndex;

			PxHeightFieldSample& Sample = Samples[DstSampleIndex];
			Sample.height = ((int32)Heights[SrcSampleIndex] - 32768);

			// Materials are not relevant on the last row/column because they are per-triangle and the last row/column don't own any
			if (RowIndex < CollisionSizeVerts - 1 &&
				ColIndex < CollisionSizeVerts - 1)
			{
				int32 MaterialIndex = DefaultMaterialIndex; // Default physical material.
				if (!bUseDefMaterial && DominantLayers)
				{
					uint8 DominantLayerIdx = DominantLayers ? DominantLayers[SrcSampleIndex] : -1;
					ULandscapeLayerInfoObject* Layer = Component->ComponentLayerInfos.IsValidIndex(DominantLayerIdx) ? Component->ComponentLayerInfos[DominantLayerIdx] : nullptr;
					if (Layer == ALandscapeProxy::VisibilityLayer)
					{
						// If it's a hole, override with the hole flag.
						MaterialIndex = PxHeightFieldMaterial::eHOLE;
					}
					else if (RenderPhysicalMaterialIds)
					{
						uint8 RenderIdx = RenderPhysicalMaterialIds[SrcSampleIndex];
						if (RenderIdx > 0)
						{
							MaterialIndex = InOutMaterials.AddUnique(PhysicalMaterialRenderObjects[RenderIdx - 1]);
						}
					}
					else if (Layer && Layer->PhysMaterial)
					{
						MaterialIndex = InOutMaterials.AddUnique(Layer->PhysMaterial);
					}
				}

				// Default Material but Def Material wasn't added yet...
				if (MaterialIndex == INDEX_NONE)
				{
					MaterialIndex = DefaultMaterialIndex = InOutMaterials.Add(DefMaterial);
				}

				Sample.materialIndex0 = MaterialIndex;
				Sample.materialIndex1 = MaterialIndex;
			}

			// TODO: edge turning
		}
	}

	// Handle case where Component uses 100% Visibility layer
	// Add the default material because PhysX running PX_CHECKED (Debug) will return a nullptr on CreateShape if the material list is empty
	if (InOutMaterials.Num() == 0)
	{
		check(DefaultMaterialIndex == INDEX_NONE);
		InOutMaterials.Add(DefMaterial);
	}

	return Samples;
}
#endif // WITH_PHYSX

bool ULandscapeHeightfieldCollisionComponent::CookCollisionData(const FName& Format, bool bUseDefMaterial, bool bCheckDDC, TArray<uint8>& OutCookedData, TArray<UPhysicalMaterial*>& InOutMaterials) const
{
	// Use existing cooked data unless !bCheckDDC in which case the data must be rebuilt.
	if (bCheckDDC && OutCookedData.Num() > 0)
	{
		return true;
	}

	COOK_STAT(auto Timer = LandscapeCollisionCookStats::HeightfieldUsageStats.TimeSyncWork());
	 
	bool Succeeded = false;
	TArray<uint8> OutData;

	// we have 2 versions of collision objects
	const int32 CookedDataIndex = bUseDefMaterial ? 0 : 1;

	if (!GLandscapeCollisionSkipDDC && bCheckDDC && HeightfieldGuid.IsValid())
	{
		// Ensure that content was saved with physical materials before using DDC data
		if (GetLinkerUE4Version() >= VER_UE4_LANDSCAPE_SERIALIZE_PHYSICS_MATERIALS)
		{
			FString DDCKey = GetHFDDCKeyString(Format, bUseDefMaterial, HeightfieldGuid, InOutMaterials);

			// Check if the speculatively-loaded data loaded and is what we wanted
			if (SpeculativeDDCRequest.IsValid() && DDCKey == SpeculativeDDCRequest->GetKey())
			{
				// If we have a DDC request in flight, just time the synchronous cycles used.
				COOK_STAT(auto WaitTimer = LandscapeCollisionCookStats::HeightfieldUsageStats.TimeAsyncWait());
				SpeculativeDDCRequest->WaitAsynchronousCompletion();
				bool bSuccess = SpeculativeDDCRequest->GetAsynchronousResults(OutCookedData);
				// World will clean up remaining reference
				SpeculativeDDCRequest.Reset();
				if (bSuccess)
				{
					COOK_STAT(Timer.Cancel());
					COOK_STAT(WaitTimer.AddHit(OutCookedData.Num()));
					bShouldSaveCookedDataToDDC[CookedDataIndex] = false;
					return true;
				}
				else
				{
					// If the DDC request failed, then we waited for nothing and will build the resource anyway. Just ignore the wait timer and treat it all as sync time.
					COOK_STAT(WaitTimer.Cancel());
				}
			}

			if (GetDerivedDataCacheRef().GetSynchronous(*DDCKey, OutCookedData, GetPathName()))
			{
				COOK_STAT(Timer.AddHit(OutCookedData.Num()));
				bShouldSaveCookedDataToDDC[CookedDataIndex] = false;
				return true;
			}
		}
	}

	ALandscapeProxy* Proxy = GetLandscapeProxy();
	if (!Proxy || !Proxy->GetRootComponent())
	{
		// We didn't actually build anything, so just track the cycles.
		COOK_STAT(Timer.TrackCyclesOnly());
		return false;
	}

	UPhysicalMaterial* DefMaterial = Proxy->DefaultPhysMaterial ? Proxy->DefaultPhysMaterial : GEngine->DefaultPhysMaterial;

	// GetComponentTransform() might not be initialized at this point, so use landscape transform
	const FVector LandscapeScale = Proxy->GetRootComponent()->GetRelativeScale3D();
	const bool bIsMirrored = (LandscapeScale.X*LandscapeScale.Y*LandscapeScale.Z) < 0.f;

	const bool bGenerateSimpleCollision = SimpleCollisionSizeQuads > 0 && !bUseDefMaterial;

	const int32 CollisionSizeVerts = CollisionSizeQuads + 1;
	const int32 SimpleCollisionSizeVerts = SimpleCollisionSizeQuads > 0 ? SimpleCollisionSizeQuads + 1 : 0;
	const int32 NumSamples = FMath::Square(CollisionSizeVerts);
	const int32 NumSimpleSamples = FMath::Square(SimpleCollisionSizeVerts);

	const uint16* Heights = (const uint16*)CollisionHeightData.LockReadOnly();
	check(CollisionHeightData.GetElementCount() == NumSamples + NumSimpleSamples);
	const uint16* SimpleHeights = Heights + NumSamples;

	// Physical material data from layer system
	const uint8* DominantLayers = nullptr;
	const uint8* SimpleDominantLayers = nullptr;
	if (DominantLayerData.GetElementCount())
	{
		DominantLayers = (const uint8*)DominantLayerData.LockReadOnly();
		check(DominantLayerData.GetElementCount() == NumSamples + NumSimpleSamples);
		SimpleDominantLayers = DominantLayers + NumSamples;
	}

	// Physical material data from render material graph
	const uint8* RenderPhysicalMaterialIds = nullptr;
	const uint8* SimpleRenderPhysicalMaterialIds = nullptr;
	if (PhysicalMaterialRenderData.GetElementCount())
	{
		RenderPhysicalMaterialIds = (const uint8*)PhysicalMaterialRenderData.LockReadOnly();
		check(PhysicalMaterialRenderData.GetElementCount() == NumSamples + NumSimpleSamples);
		SimpleRenderPhysicalMaterialIds = RenderPhysicalMaterialIds + NumSamples;
	}

	// List of materials which is actually used by heightfield
	InOutMaterials.Empty();

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		
	TArray<PxHeightFieldSample> Samples;
	TArray<PxHeightFieldSample> SimpleSamples;
	Samples = ConvertHeightfieldDataForPhysx(this, CollisionSizeVerts, bIsMirrored, Heights, bUseDefMaterial, DefMaterial, DominantLayers, RenderPhysicalMaterialIds, PhysicalMaterialRenderObjects, InOutMaterials);

	if (bGenerateSimpleCollision)
	{
		SimpleSamples = ConvertHeightfieldDataForPhysx(this, SimpleCollisionSizeVerts, bIsMirrored, SimpleHeights, bUseDefMaterial, DefMaterial, SimpleDominantLayers, SimpleRenderPhysicalMaterialIds, PhysicalMaterialRenderObjects, InOutMaterials);
	}

	CollisionHeightData.Unlock();
	if (DominantLayers)
	{
		DominantLayerData.Unlock();
	}
	
	//
	FIntPoint HFSize = FIntPoint(CollisionSizeVerts, CollisionSizeVerts);

	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	const IPhysXCooking* Cooker = TPM->FindPhysXCooking(Format);
	Succeeded = Cooker->CookHeightField(Format, HFSize, Samples.GetData(), Samples.GetTypeSize(), OutData);

	if (Succeeded && bGenerateSimpleCollision)
	{
		FIntPoint HFSizeSimple = FIntPoint(SimpleCollisionSizeVerts, SimpleCollisionSizeVerts);
		Succeeded = Cooker->CookHeightField(Format, HFSizeSimple, SimpleSamples.GetData(), SimpleSamples.GetTypeSize(), OutData);
	}
#elif WITH_CHAOS

	// Generate material indices
	TArray<uint8> MaterialIndices;
	MaterialIndices.Reserve(NumSamples + NumSimpleSamples);
	for(int32 RowIndex = 0; RowIndex < CollisionSizeVerts; RowIndex++)
	{
		for(int32 ColIndex = 0; ColIndex < CollisionSizeVerts; ColIndex++)
		{
			const int32 SrcSampleIndex = (RowIndex * CollisionSizeVerts) + (bIsMirrored ? (CollisionSizeVerts - ColIndex - 1) : ColIndex);

			// Materials are not relevant on the last row/column because they are per-triangle and the last row/column don't own any
			if(RowIndex < CollisionSizeVerts - 1 &&
				ColIndex < CollisionSizeVerts - 1)
			{
				int32 MaterialIndex = 0; // Default physical material.
				if(!bUseDefMaterial)
				{
					uint8 DominantLayerIdx = DominantLayers ? DominantLayers[SrcSampleIndex] : -1;
					ULandscapeLayerInfoObject* Layer = ComponentLayerInfos.IsValidIndex(DominantLayerIdx) ? ComponentLayerInfos[DominantLayerIdx] : nullptr;

					if(Layer == ALandscapeProxy::VisibilityLayer)
					{
						// If it's a hole, use the final index
						MaterialIndex = TNumericLimits<uint8>::Max();
					}
					else if (RenderPhysicalMaterialIds)
					{
						uint8 RenderIdx = RenderPhysicalMaterialIds[SrcSampleIndex];
						UPhysicalMaterial* DominantMaterial = RenderIdx > 0 ? PhysicalMaterialRenderObjects[RenderIdx - 1] : DefMaterial;
						MaterialIndex = InOutMaterials.AddUnique(DominantMaterial);
					}
					else
					{
						UPhysicalMaterial* DominantMaterial = Layer && Layer->PhysMaterial ? Layer->PhysMaterial : DefMaterial;
						MaterialIndex = InOutMaterials.AddUnique(DominantMaterial);
					}
				}
				MaterialIndices.Add(MaterialIndex);
			}
		}
	}

	TUniquePtr<Chaos::FHeightField> Heightfield = nullptr;
	TUniquePtr<Chaos::FHeightField> HeightfieldSimple = nullptr;

	FMemoryWriter Writer(OutData);
	Chaos::FChaosArchive Ar(Writer);

	bool bSerializeGenerateSimpleCollision = bGenerateSimpleCollision;
	Ar << bSerializeGenerateSimpleCollision;

	TArrayView<const uint16> ComplexHeightView(Heights, NumSamples);
	Heightfield = MakeUnique<Chaos::FHeightField>(ComplexHeightView, MakeArrayView(MaterialIndices), CollisionSizeVerts, CollisionSizeVerts, Chaos::FVec3(1));
	Ar << Heightfield;
	if(bGenerateSimpleCollision)
	{
		// #BGTODO Materials for simple geometry, currently just passing in the default
		TArrayView<const uint16> SimpleHeightView(Heights + NumSamples, NumSimpleSamples);
		HeightfieldSimple = MakeUnique<Chaos::FHeightField>(SimpleHeightView, MakeArrayView(MaterialIndices.GetData(), 1), SimpleCollisionSizeVerts, SimpleCollisionSizeVerts, Chaos::FVec3(1));
		Ar << HeightfieldSimple;
	}

	Succeeded = true;
#endif

	if (CollisionHeightData.IsLocked())
	{
		CollisionHeightData.Unlock();
	}
	if (DominantLayerData.IsLocked())
	{
		DominantLayerData.Unlock();
	}
	if (PhysicalMaterialRenderData.IsLocked())
	{
		PhysicalMaterialRenderData.Unlock();
	}

	if (Succeeded)
	{
		COOK_STAT(Timer.AddMiss(OutData.Num()));
		OutCookedData.SetNumUninitialized(OutData.Num());
		FMemory::Memcpy(OutCookedData.GetData(), OutData.GetData(), OutData.Num());

		if (!GLandscapeCollisionSkipDDC && bShouldSaveCookedDataToDDC[CookedDataIndex] && HeightfieldGuid.IsValid())
		{
			GetDerivedDataCacheRef().Put(*GetHFDDCKeyString(Format, bUseDefMaterial, HeightfieldGuid, InOutMaterials), OutCookedData, GetPathName());
			bShouldSaveCookedDataToDDC[CookedDataIndex] = false;
		}
	}
	else
	{
		// if we failed to build the resource, just time the cycles we spent.
		COOK_STAT(Timer.TrackCyclesOnly());
		OutCookedData.Empty();
		InOutMaterials.Empty();
	}

	return Succeeded;
}

bool ULandscapeMeshCollisionComponent::CookCollisionData(const FName& Format, bool bUseDefMaterial, bool bCheckDDC, TArray<uint8>& OutCookedData, TArray<UPhysicalMaterial*>& InOutMaterials) const
{
	// Use existing cooked data unless !bCheckDDC in which case the data must be rebuilt.
	if (bCheckDDC && OutCookedData.Num() > 0)
	{
		return true;
	}

	COOK_STAT(auto Timer = LandscapeCollisionCookStats::MeshUsageStats.TimeSyncWork());
	// we have 2 versions of collision objects
	const int32 CookedDataIndex = bUseDefMaterial ? 0 : 1;

	if (!GLandscapeCollisionSkipDDC && bCheckDDC)
	{
		// Ensure that content was saved with physical materials before using DDC data
		if (GetLinkerUE4Version() >= VER_UE4_LANDSCAPE_SERIALIZE_PHYSICS_MATERIALS && MeshGuid.IsValid())
		{
			FString DDCKey = GetHFDDCKeyString(Format, bUseDefMaterial, MeshGuid, InOutMaterials);

			// Check if the speculatively-loaded data loaded and is what we wanted
			if (SpeculativeDDCRequest.IsValid() && DDCKey == SpeculativeDDCRequest->GetKey())
			{
				// If we have a DDC request in flight, just time the synchronous cycles used.
				COOK_STAT(auto WaitTimer = LandscapeCollisionCookStats::MeshUsageStats.TimeAsyncWait());
				SpeculativeDDCRequest->WaitAsynchronousCompletion();
				bool bSuccess = SpeculativeDDCRequest->GetAsynchronousResults(OutCookedData);
				// World will clean up remaining reference
				SpeculativeDDCRequest.Reset();
				if (bSuccess)
				{
					COOK_STAT(Timer.Cancel());
					COOK_STAT(WaitTimer.AddHit(OutCookedData.Num()));
					bShouldSaveCookedDataToDDC[CookedDataIndex] = false;
					return true;
				}
				else
				{
					// If the DDC request failed, then we waited for nothing and will build the resource anyway. Just ignore the wait timer and treat it all as sync time.
					COOK_STAT(WaitTimer.Cancel());
				}
			}

			if (GetDerivedDataCacheRef().GetSynchronous(*DDCKey, OutCookedData, GetPathName()))
			{
				COOK_STAT(Timer.AddHit(OutCookedData.Num()));
				bShouldSaveCookedDataToDDC[CookedDataIndex] = false;
				return true;
			}
		}
	}
	
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	UPhysicalMaterial* DefMaterial = (Proxy && Proxy->DefaultPhysMaterial != nullptr) ? Proxy->DefaultPhysMaterial : GEngine->DefaultPhysMaterial;

	// List of materials which is actually used by trimesh
	InOutMaterials.Empty();

	TArray<FVector>			Vertices;
	TArray<FTriIndices>		Indices;
	TArray<uint16>			MaterialIndices;

	const int32 CollisionSizeVerts = CollisionSizeQuads + 1;
	const int32 SimpleCollisionSizeVerts = SimpleCollisionSizeQuads > 0 ? SimpleCollisionSizeQuads + 1 : 0;
	const int32 NumVerts = FMath::Square(CollisionSizeVerts);
	const int32 NumSimpleVerts = FMath::Square(SimpleCollisionSizeVerts);

	const uint16* Heights = (const uint16*)CollisionHeightData.LockReadOnly();
	const uint16* XYOffsets = (const uint16*)CollisionXYOffsetData.LockReadOnly();
	check(CollisionHeightData.GetElementCount() == NumVerts + NumSimpleVerts);
	check(CollisionXYOffsetData.GetElementCount() == NumVerts * 2);

	const uint8* DominantLayers = nullptr;
	if (DominantLayerData.GetElementCount() > 0)
	{
		DominantLayers = (const uint8*)DominantLayerData.LockReadOnly();
	}

	// Scale all verts into temporary vertex buffer.
	Vertices.SetNumUninitialized(NumVerts);
	for (int32 i = 0; i < NumVerts; i++)
	{
		int32 X = i % CollisionSizeVerts;
		int32 Y = i / CollisionSizeVerts;
		Vertices[i].Set(X + ((float)XYOffsets[i * 2] - 32768.f) * LANDSCAPE_XYOFFSET_SCALE, Y + ((float)XYOffsets[i * 2 + 1] - 32768.f) * LANDSCAPE_XYOFFSET_SCALE, ((float)Heights[i] - 32768.f) * LANDSCAPE_ZSCALE);
	}

	const int32 NumTris = FMath::Square(CollisionSizeQuads) * 2;
	Indices.SetNumUninitialized(NumTris);
	if (DominantLayers)
	{
		MaterialIndices.SetNumUninitialized(NumTris);
	}

	int32 TriangleIdx = 0;
	for (int32 y = 0; y < CollisionSizeQuads; y++)
	{
		for (int32 x = 0; x < CollisionSizeQuads; x++)
		{
			int32 DataIdx = x + y * CollisionSizeVerts;
			bool bHole = false;

			int32 MaterialIndex = 0; // Default physical material.
			if (!bUseDefMaterial && DominantLayers)
			{
				uint8 DominantLayerIdx = DominantLayers[DataIdx];
				if (ComponentLayerInfos.IsValidIndex(DominantLayerIdx))
				{
					ULandscapeLayerInfoObject* Layer = ComponentLayerInfos[DominantLayerIdx];
					if (Layer == ALandscapeProxy::VisibilityLayer)
					{
						// If it's a hole, override with the hole flag.
						bHole = true;
					}
					else
					{
						UPhysicalMaterial* DominantMaterial = Layer && Layer->PhysMaterial ? Layer->PhysMaterial : DefMaterial;
						MaterialIndex = InOutMaterials.AddUnique(DominantMaterial);
					}
				}
			}

			FTriIndices& TriIndex1 = Indices[TriangleIdx];
			if (bHole)
			{
				TriIndex1.v0 = (x + 0) + (y + 0) * CollisionSizeVerts;
				TriIndex1.v1 = TriIndex1.v0;
				TriIndex1.v2 = TriIndex1.v0;
			}
			else
			{
				TriIndex1.v0 = (x + 0) + (y + 0) * CollisionSizeVerts;
				TriIndex1.v1 = (x + 1) + (y + 1) * CollisionSizeVerts;
				TriIndex1.v2 = (x + 1) + (y + 0) * CollisionSizeVerts;
			}

			if (DominantLayers)
			{
				MaterialIndices[TriangleIdx] = MaterialIndex;
			}
			TriangleIdx++;

			FTriIndices& TriIndex2 = Indices[TriangleIdx];
			if (bHole)
			{
				TriIndex2.v0 = (x + 0) + (y + 0) * CollisionSizeVerts;
				TriIndex2.v1 = TriIndex2.v0;
				TriIndex2.v2 = TriIndex2.v0;
			}
			else
			{
				TriIndex2.v0 = (x + 0) + (y + 0) * CollisionSizeVerts;
				TriIndex2.v1 = (x + 0) + (y + 1) * CollisionSizeVerts;
				TriIndex2.v2 = (x + 1) + (y + 1) * CollisionSizeVerts;
			}

			if (DominantLayers)
			{
				MaterialIndices[TriangleIdx] = MaterialIndex;
			}
			TriangleIdx++;
		}
	}

	CollisionHeightData.Unlock();
	CollisionXYOffsetData.Unlock();
	if (DominantLayers)
	{
		DominantLayerData.Unlock();
	}

	// Add the default physical material to be used used when we have no dominant data.
	if (InOutMaterials.Num() == 0)
	{
		InOutMaterials.Add(DefMaterial);
	}

	TArray<uint8> OutData;
	bool Result = false;
#if PHYSICS_INTERFACE_PHYSX
	bool bFlipNormals = true;
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	const IPhysXCooking* Cooker = TPM->FindPhysXCooking(Format);
	Result = Cooker->CookTriMesh(Format, EPhysXMeshCookFlags::Default, Vertices, Indices, MaterialIndices, bFlipNormals, OutData);
#elif WITH_CHAOS
	FCookBodySetupInfo CookInfo;
	FTriMeshCollisionData& MeshDesc = CookInfo.TriangleMeshDesc;
	MeshDesc.bFlipNormals = true;
	MeshDesc.Vertices = MoveTemp(Vertices);
	MeshDesc.Indices = MoveTemp(Indices);
	MeshDesc.MaterialIndices = MoveTemp(MaterialIndices);
	CookInfo.bCookTriMesh = true;
	TArray<int32> FaceRemap;
	TArray<int32> VertexRemap;
	TUniquePtr<Chaos::FTriangleMeshImplicitObject> Trimesh = Chaos::Cooking::BuildSingleTrimesh(MeshDesc, FaceRemap, VertexRemap);

	if(Trimesh.IsValid())
	{
		FMemoryWriter Ar(OutData);
		Chaos::FChaosArchive ChaosAr(Ar);
		ChaosAr << Trimesh;
		Result = OutData.Num() > 0;
	}
#endif

	if (Result)
	{
		COOK_STAT(Timer.AddMiss(OutData.Num()));
		OutCookedData.SetNumUninitialized(OutData.Num());
		FMemory::Memcpy(OutCookedData.GetData(), OutData.GetData(), OutData.Num());

		if (!GLandscapeCollisionSkipDDC && bShouldSaveCookedDataToDDC[CookedDataIndex] && MeshGuid.IsValid())
		{
			GetDerivedDataCacheRef().Put(*GetHFDDCKeyString(Format, bUseDefMaterial, MeshGuid, InOutMaterials), OutCookedData, GetPathName());
			bShouldSaveCookedDataToDDC[CookedDataIndex] = false;
		}
	}
	else
	{
		// We didn't actually build anything, so just track the cycles.
		COOK_STAT(Timer.TrackCyclesOnly());
		OutCookedData.Empty();
		InOutMaterials.Empty();
	}

	return Result;
}
#endif //WITH_EDITOR

void ULandscapeMeshCollisionComponent::CreateCollisionObject()
{
	// If we have not created a heightfield yet - do it now.
	if (!IsValidRef(MeshRef))
	{
		FTriMeshGeometryRef* ExistingMeshRef = nullptr;
		bool bCheckDDC = true;

		if (!MeshGuid.IsValid())
		{
			MeshGuid = FGuid::NewGuid();
			bCheckDDC = false;
		}
		else
		{
			// Look for a heightfield object with the current Guid (this occurs with PIE)
			ExistingMeshRef = GSharedMeshRefs.FindRef(MeshGuid);
		}

		if (ExistingMeshRef)
		{
			MeshRef = ExistingMeshRef;
		}
		else
		{
#if WITH_EDITOR
		    // This should only occur if a level prior to VER_UE4_LANDSCAPE_COLLISION_DATA_COOKING 
		    // was resaved using a commandlet and not saved in the editor, or if a PhysicalMaterial asset was deleted.
		    if (CookedPhysicalMaterials.Num() == 0 || CookedPhysicalMaterials.Contains(nullptr))
		    {
			    bCheckDDC = false;
		    }

			// Create cooked physics data
			static FName PhysicsFormatName(FPlatformProperties::GetPhysicsFormat());
			CookCollisionData(PhysicsFormatName, false, bCheckDDC, CookedCollisionData, CookedPhysicalMaterials);
#endif //WITH_EDITOR

			if (CookedCollisionData.Num())
			{
				MeshRef = GSharedMeshRefs.Add(MeshGuid, new FTriMeshGeometryRef(MeshGuid));

				// Create physics objects
#if PHYSICS_INTERFACE_PHYSX
				FPhysXInputStream Buffer(CookedCollisionData.GetData(), CookedCollisionData.Num());
				MeshRef->RBTriangleMesh = GPhysXSDK->createTriangleMesh(Buffer);
#elif WITH_CHAOS
				FMemoryReader Reader(CookedCollisionData);
				Chaos::FChaosArchive Ar(Reader);
				Ar << MeshRef->Trimesh;
#endif

				for (UPhysicalMaterial* PhysicalMaterial : CookedPhysicalMaterials)
				{
#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
					MeshRef->UsedChaosMaterials.Add(PhysicalMaterial->GetPhysicsMaterial());
#else
					MeshRef->UsedPhysicalMaterialArray.Add(PhysicalMaterial->GetPhysicsMaterial().Material);
#endif
				}

				// Release cooked collison data
				// In cooked builds created collision object will never be deleted while component is alive, so we don't need this data anymore
				if (FPlatformProperties::RequiresCookedData() || GetWorld()->IsGameWorld())
				{
					CookedCollisionData.Empty();
				}

#if WITH_EDITOR
				// Create collision mesh for the landscape editor (no holes in it)
				if (!GetWorld()->IsGameWorld())
				{
					TArray<UPhysicalMaterial*> CookedMaterialsEd;
					if (CookCollisionData(PhysicsFormatName, true, bCheckDDC, CookedCollisionDataEd, CookedMaterialsEd))
					{
#if PHYSICS_INTERFACE_PHYSX
						FPhysXInputStream MeshStream(CookedCollisionDataEd.GetData(), CookedCollisionDataEd.Num());
						MeshRef->RBTriangleMeshEd = GPhysXSDK->createTriangleMesh(MeshStream);
#elif WITH_CHAOS
						FMemoryReader EdReader(CookedCollisionData);
						Chaos::FChaosArchive EdAr(EdReader);
						EdAr << MeshRef->EditorTrimesh;
#endif
					}
				}
#endif //WITH_EDITOR
			}
		}
	}
}


ULandscapeMeshCollisionComponent::ULandscapeMeshCollisionComponent()
{
	// make landscape always create? 
	bAlwaysCreatePhysicsState = true;
}

ULandscapeMeshCollisionComponent::~ULandscapeMeshCollisionComponent() = default;

#if PHYSICS_INTERFACE_PHYSX
struct FMeshCollisionInitHelper
{
	FMeshCollisionInitHelper() = delete;
	FMeshCollisionInitHelper(TRefCountPtr<ULandscapeMeshCollisionComponent::FTriMeshGeometryRef> InMeshRef, UWorld* InWorld, UPrimitiveComponent* InComponent, FBodyInstance* InBodyInstance)
		: ComponentToWorld(FTransform::Identity)
		, ComponentScale(FVector::OneVector)
		, CollisionScale(1.0f)
		, MeshRef(InMeshRef)
		, World(InWorld)
		, Component(InComponent)
		, TargetInstance(InBodyInstance)
	{
		PxGeom.triangleMesh = MeshRef->RBTriangleMesh;
		PxGeom.scale.scale.x = ComponentScale.X * CollisionScale;
		PxGeom.scale.scale.y = ComponentScale.Y * CollisionScale;
		PxGeom.scale.scale.z = ComponentScale.Z;

#if WITH_EDITOR
		PxGeomEd.triangleMesh = MeshRef->RBTriangleMeshEd;
		PxGeomEd.scale.scale.x = ComponentScale.X * CollisionScale;
		PxGeomEd.scale.scale.y = ComponentScale.Y * CollisionScale;
		PxGeomEd.scale.scale.z = ComponentScale.Z;
#endif

		check(World);
		PhysScene = World->GetPhysicsScene();
		check(PhysScene);
		check(Component);
		check(TargetInstance);
	}

	void SetComponentScale3D(const FVector& InScale)
	{
		ComponentScale = InScale;

		PxGeom.scale.scale.x = ComponentScale.X * CollisionScale;
		PxGeom.scale.scale.y = ComponentScale.Y * CollisionScale;
		PxGeom.scale.scale.z = ComponentScale.Z;

		PxGeomEd.scale.scale.x = ComponentScale.X * CollisionScale;
		PxGeomEd.scale.scale.y = ComponentScale.Y * CollisionScale;
		PxGeomEd.scale.scale.z = ComponentScale.Z;
	}

	void SetCollisionScale(float InScale)
	{
		CollisionScale = InScale;

		PxGeom.scale.scale.x = ComponentScale.X * CollisionScale;
		PxGeom.scale.scale.y = ComponentScale.Y * CollisionScale;
		PxGeom.scale.scale.z = ComponentScale.Z;

		PxGeomEd.scale.scale.x = ComponentScale.X * CollisionScale;
		PxGeomEd.scale.scale.y = ComponentScale.Y * CollisionScale;
		PxGeomEd.scale.scale.z = ComponentScale.Z;
	}

	void SetComponentToWorld(const FTransform& InTransform)
	{
		ComponentToWorld = InTransform;

		PxComponentTransform = U2PTransform(ComponentToWorld);
	}

	void SetFilters(const FCollisionFilterData& InQueryFilter, const FCollisionFilterData& InSimulationFilter)
	{
		QueryFilter = InQueryFilter;
		SimulationFilter = InSimulationFilter;
	}

	void SetEditorFilter(const FCollisionFilterData& InFilter)
	{
		QueryFilterEd = InFilter;
	}

	bool IsGeometryValid() const
	{
		return PxGeom.isValid();
	}

	void CreateActors()
	{
		{
			// Create the sync scene actor
			PActor = GPhysXSDK->createRigidStatic(PxComponentTransform);
			PxShape* NewShape = GPhysXSDK->createShape(PxGeom, MeshRef->UsedPhysicalMaterialArray.GetData(), MeshRef->UsedPhysicalMaterialArray.Num(), true);
			check(NewShape);

			// Heightfield is used for simple and complex collision
			NewShape->setQueryFilterData(U2PFilterData(QueryFilter));
			NewShape->setSimulationFilterData(U2PFilterData(SimulationFilter));
			NewShape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
			NewShape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
			NewShape->setFlag(PxShapeFlag::eVISUALIZATION, true);

			PActor->attachShape(*NewShape);
			NewShape->release();
		}

#if WITH_EDITOR
		if(!World->IsGameWorld()) // Need to create editor shape
		{
			PxMaterial* PDefaultMat = GEngine->DefaultPhysMaterial->GetPhysicsMaterial().Material;
			PxShape* NewShape = GPhysXSDK->createShape(PxGeom, &PDefaultMat, 1, true);
			check(NewShape);

			NewShape->setQueryFilterData(U2PFilterData(QueryFilterEd));
			NewShape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);

			PActor->attachShape(*NewShape);
			NewShape->release();
		}
#endif

		// Set body instance data
		TargetInstance->PhysicsUserData = FPhysicsUserData(TargetInstance);
		TargetInstance->OwnerComponent = Component;
		TargetInstance->ActorHandle.SyncActor = PActor;
		PActor->userData = &TargetInstance->PhysicsUserData;
	}

	void AddToScene()
	{
		check(PhysScene);

		// Add to scenes
		PxScene* SyncScene = PhysScene->GetPxScene();
		SCOPED_SCENE_WRITE_LOCK(SyncScene);
		SyncScene->addActor(*PActor);
	}

private:
	FTransform ComponentToWorld;
	FVector ComponentScale;
	float CollisionScale;
	TRefCountPtr<ULandscapeMeshCollisionComponent::FTriMeshGeometryRef> MeshRef;
	FPhysScene* PhysScene;
	FCollisionFilterData QueryFilter;
	FCollisionFilterData SimulationFilter;
	FCollisionFilterData QueryFilterEd;
	UWorld* World;
	UPrimitiveComponent* Component;
	FBodyInstance* TargetInstance;

	PxTriangleMeshGeometry PxGeom;
	PxTriangleMeshGeometry PxGeomEd;
	PxTransform PxComponentTransform;
	PxRigidStatic* PActor;
};
#elif WITH_CHAOS
struct FMeshCollisionInitHelper
{
	FMeshCollisionInitHelper() = delete;
	FMeshCollisionInitHelper(TRefCountPtr<ULandscapeMeshCollisionComponent::FTriMeshGeometryRef> InMeshRef, UWorld* InWorld, UPrimitiveComponent* InComponent, FBodyInstance* InBodyInstance)
		: ComponentToWorld(FTransform::Identity)
		, ComponentScale(FVector::OneVector)
		, CollisionScale(1.0f)
		, MeshRef(InMeshRef)
		, World(InWorld)
		, Component(InComponent)
		, TargetInstance(InBodyInstance)
	{
		check(World);
		PhysScene = World->GetPhysicsScene();
		check(PhysScene);
		check(Component);
		check(TargetInstance);
	}

	void SetComponentScale3D(const FVector& InScale)
	{
		ComponentScale = InScale;
	}

	void SetCollisionScale(float InScale)
	{
		CollisionScale = InScale;
	}

	void SetComponentToWorld(const FTransform& InTransform)
	{
		ComponentToWorld = InTransform;
	}

	void SetFilters(const FCollisionFilterData& InQueryFilter, const FCollisionFilterData& InSimulationFilter)
	{
		QueryFilter = InQueryFilter;
		SimulationFilter = InSimulationFilter;
	}

	void SetEditorFilter(const FCollisionFilterData& InFilter)
	{
		QueryFilterEd = InFilter;
	}

	bool IsGeometryValid() const
	{
		return MeshRef->Trimesh.IsValid();
	}

	void CreateActors()
	{
		Chaos::FShapesArray ShapeArray;
		TArray<TUniquePtr<Chaos::FImplicitObject>> Geometries;
		
		FActorCreationParams Params;
		Params.InitialTM = ComponentToWorld;
		Params.InitialTM.SetScale3D(FVector::OneVector);
		Params.bQueryOnly = true;
		Params.bStatic = true;
		Params.Scene = PhysScene;

		FPhysicsInterface::CreateActor(Params, ActorHandle);

		FVector Scale = FVector(ComponentScale.X * CollisionScale, ComponentScale.Y * CollisionScale, ComponentScale.Z);

		{
			TUniquePtr<Chaos::FPerShapeData> NewShape = Chaos::FPerShapeData::CreatePerShapeData(ShapeArray.Num());
			TUniquePtr<Chaos::TImplicitObjectScaled<Chaos::FTriangleMeshImplicitObject>> ScaledTrimesh = MakeUnique<Chaos::TImplicitObjectScaled<Chaos::FTriangleMeshImplicitObject>>(MakeSerializable(MeshRef->Trimesh), Scale);

			NewShape->SetGeometry(MakeSerializable(ScaledTrimesh));
			NewShape->SetQueryData(QueryFilter);
			NewShape->SetSimData(SimulationFilter);
			NewShape->SetCollisionTraceType(Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseComplexAsSimple);
			NewShape->SetMaterials(MeshRef->UsedChaosMaterials);

			Geometries.Emplace(MoveTemp(ScaledTrimesh));
			ShapeArray.Emplace(MoveTemp(NewShape));
		}

#if WITH_EDITOR
		if(!World->IsGameWorld())
		{
			TUniquePtr<Chaos::FPerShapeData> NewEdShape = Chaos::FPerShapeData::CreatePerShapeData(ShapeArray.Num());
			TUniquePtr<Chaos::TImplicitObjectScaled<Chaos::FTriangleMeshImplicitObject>> ScaledTrimeshEd = MakeUnique<Chaos::TImplicitObjectScaled<Chaos::FTriangleMeshImplicitObject>>(MakeSerializable(MeshRef->EditorTrimesh), Scale);

			NewEdShape->SetGeometry(MakeSerializable(ScaledTrimeshEd));
			NewEdShape->SetQueryData(QueryFilterEd);
			NewEdShape->SetSimEnabled(false);
			NewEdShape->SetCollisionTraceType(Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseComplexAsSimple);
			NewEdShape->SetMaterial(GEngine->DefaultPhysMaterial->GetPhysicsMaterial());

			Geometries.Emplace(MoveTemp(ScaledTrimeshEd));
			ShapeArray.Emplace(MoveTemp(NewEdShape));
		}
#endif

		if(Geometries.Num() == 1)
		{
			ActorHandle->GetGameThreadAPI().SetGeometry(MoveTemp(Geometries[0]));
		}
		else
		{
			ActorHandle->GetGameThreadAPI().SetGeometry(MakeUnique<Chaos::FImplicitObjectUnion>(MoveTemp(Geometries)));
		}

		for(TUniquePtr<Chaos::FPerShapeData>& Shape : ShapeArray)
		{
			Chaos::FRigidTransform3 WorldTransform = Chaos::FRigidTransform3(ActorHandle->GetGameThreadAPI().X(), ActorHandle->GetGameThreadAPI().R());
			Shape->UpdateShapeBounds(WorldTransform);
		}

		ActorHandle->GetGameThreadAPI().SetShapesArray(MoveTemp(ShapeArray));

		TargetInstance->PhysicsUserData = FPhysicsUserData(TargetInstance);
		TargetInstance->OwnerComponent = Component;
		TargetInstance->ActorHandle = ActorHandle;

		ActorHandle->GetGameThreadAPI().SetUserData(&TargetInstance->PhysicsUserData);
	}

	void AddToScene()
	{
		check(PhysScene);

		TArray<FPhysicsActorHandle> Actors;
		Actors.Add(ActorHandle);

		PhysScene->AddActorsToScene_AssumesLocked(Actors, true);
		PhysScene->AddToComponentMaps(Component, ActorHandle);

		if(TargetInstance->bNotifyRigidBodyCollision)
		{
			PhysScene->RegisterForCollisionEvents(Component);
		}
	}

private:
	FTransform ComponentToWorld;
	FVector ComponentScale;
	float CollisionScale;
	TRefCountPtr<ULandscapeMeshCollisionComponent::FTriMeshGeometryRef> MeshRef;
	FPhysScene* PhysScene;
	FCollisionFilterData QueryFilter;
	FCollisionFilterData SimulationFilter;
	FCollisionFilterData QueryFilterEd;
	UWorld* World;
	UPrimitiveComponent* Component;
	FBodyInstance* TargetInstance;

	FPhysicsActorHandle ActorHandle;
};
#endif

void ULandscapeMeshCollisionComponent::OnCreatePhysicsState()
{
	USceneComponent::OnCreatePhysicsState(); // route OnCreatePhysicsState, skip PrimitiveComponent implementation

	if (!BodyInstance.IsValidBodyInstance())
	{
		// This will do nothing, because we create trimesh at component PostLoad event, unless we destroyed it explicitly
		CreateCollisionObject();

		if (IsValidRef(MeshRef))
		{
			FMeshCollisionInitHelper Initializer(MeshRef, GetWorld(), this, &BodyInstance);

			// Make transform for this landscape component PxActor
			FTransform LandscapeComponentTransform = GetComponentToWorld();
			FMatrix LandscapeComponentMatrix = LandscapeComponentTransform.ToMatrixWithScale();
			bool bIsMirrored = LandscapeComponentMatrix.Determinant() < 0.f;
			if (bIsMirrored)
			{
				// Unreal and PhysX have opposite handedness, so we need to translate the origin and rearrange the data
				LandscapeComponentMatrix = FTranslationMatrix(FVector(CollisionSizeQuads, 0, 0)) * LandscapeComponentMatrix;
			}

			// Get the scale to give to PhysX
			FVector LandscapeScale = LandscapeComponentMatrix.ExtractScaling();

			Initializer.SetComponentToWorld(LandscapeComponentTransform);
			Initializer.SetComponentScale3D(LandscapeScale);
			Initializer.SetCollisionScale(CollisionScale);

			if(Initializer.IsGeometryValid())
			{
				// Setup filtering
				FCollisionFilterData QueryFilterData, SimFilterData;
				CreateShapeFilterData(GetCollisionObjectType(), FMaskFilter(0), GetOwner()->GetUniqueID(), GetCollisionResponseToChannels(), GetUniqueID(), 0, QueryFilterData, SimFilterData, false, false, true);
				QueryFilterData.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);
				SimFilterData.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);

				Initializer.SetFilters(QueryFilterData, SimFilterData);

#if WITH_EDITOR
				FCollisionResponseContainer EdResponse;
				EdResponse.SetAllChannels(ECollisionResponse::ECR_Ignore);
				EdResponse.SetResponse(ECollisionChannel::ECC_Visibility, ECR_Block);
						FCollisionFilterData QueryFilterDataEd, SimFilterDataEd;
				CreateShapeFilterData(ECollisionChannel::ECC_Visibility, FMaskFilter(0), GetOwner()->GetUniqueID(), EdResponse, GetUniqueID(), 0, QueryFilterDataEd, SimFilterDataEd, true, false, true);
						QueryFilterDataEd.Word3 |= (EPDF_SimpleCollision | EPDF_ComplexCollision);

				Initializer.SetEditorFilter(QueryFilterDataEd);
#endif

				Initializer.CreateActors();
				Initializer.AddToScene();
			}
			else
			{
				UE_LOG(LogLandscape, Log, TEXT("ULandscapeMeshCollisionComponent::OnCreatePhysicsState(): TriMesh invalid"));
			}
		}
	}
}

void ULandscapeMeshCollisionComponent::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
	Super::ApplyWorldOffset(InOffset, bWorldShift);

	if (!bWorldShift || !FPhysScene::SupportsOriginShifting())
	{
		RecreatePhysicsState();
	}
}

void ULandscapeMeshCollisionComponent::DestroyComponent(bool bPromoteChildren/*= false*/)
{
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	if (Proxy)
	{
		Proxy->CollisionComponents.Remove(this);
	}

	Super::DestroyComponent(bPromoteChildren);
}

#if WITH_EDITOR
uint32 ULandscapeHeightfieldCollisionComponent::ComputeCollisionHash() const
{
	uint32 Hash = 0;
		
	Hash = HashCombine(GetTypeHash(SimpleCollisionSizeQuads), Hash);
	Hash = HashCombine(GetTypeHash(CollisionSizeQuads), Hash);
	Hash = HashCombine(GetTypeHash(CollisionScale), Hash);

	FTransform ComponentTransform = GetComponentToWorld();
	Hash = FCrc::MemCrc32(&ComponentTransform, sizeof(ComponentTransform));

	const void* HeightBuffer = CollisionHeightData.LockReadOnly();
	Hash = FCrc::MemCrc32(HeightBuffer, CollisionHeightData.GetBulkDataSize(), Hash);
	CollisionHeightData.Unlock();

	const void* DominantBuffer = DominantLayerData.LockReadOnly();
	Hash = FCrc::MemCrc32(DominantBuffer, DominantLayerData.GetBulkDataSize(), Hash);
	DominantLayerData.Unlock();

	const void* PhysicalMaterialBuffer = PhysicalMaterialRenderData.LockReadOnly();
	Hash = FCrc::MemCrc32(PhysicalMaterialBuffer, PhysicalMaterialRenderData.GetBulkDataSize(), Hash);
	PhysicalMaterialRenderData.Unlock();

	return Hash;
}

void ULandscapeHeightfieldCollisionComponent::UpdateHeightfieldRegion(int32 ComponentX1, int32 ComponentY1, int32 ComponentX2, int32 ComponentY2)
{
#if WITH_PHYSX
	if (IsValidRef(HeightfieldRef))
	{
		// If we're currently sharing this data with a PIE session, we need to make a new heightfield.
		if (HeightfieldRef->GetRefCount() > 1)
		{
			RecreateCollision();
			return;
		}

#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
		if(!BodyInstance.ActorHandle)
		{
			return;
		}
#else
		if (BodyInstance.ActorHandle.SyncActor == NULL)
		{
			return;
		}
#endif

		// We don't lock the async scene as we only set the geometry in the sync scene's RigidActor.
		// This function is used only during painting for line traces by the painting tools.
		FPhysicsActorHandle PhysActorHandle = BodyInstance.GetPhysicsActorHandle();

		FPhysicsCommand::ExecuteWrite(PhysActorHandle, [&](const FPhysicsActorHandle& Actor)
		{
			int32 CollisionSizeVerts = CollisionSizeQuads + 1;
			int32 SimpleCollisionSizeVerts = SimpleCollisionSizeQuads > 0 ? SimpleCollisionSizeQuads + 1 : 0;
	
			bool bIsMirrored = GetComponentToWorld().GetDeterminant() < 0.f;
	
			uint16* Heights = (uint16*)CollisionHeightData.Lock(LOCK_READ_ONLY);
			check(CollisionHeightData.GetElementCount() == (FMath::Square(CollisionSizeVerts) + FMath::Square(SimpleCollisionSizeVerts)));
	
#if PHYSICS_INTERFACE_PHYSX
			// PhysX heightfield has the X and Y axis swapped, and the X component is also inverted
			int32 HeightfieldX1 = ComponentY1;
			int32 HeightfieldY1 = (bIsMirrored ? ComponentX1 : (CollisionSizeVerts - ComponentX2 - 1));
			int32 DstVertsX = ComponentY2 - ComponentY1 + 1;
			int32 DstVertsY = ComponentX2 - ComponentX1 + 1;
	
			TArray<PxHeightFieldSample> Samples;
			Samples.AddZeroed(DstVertsX*DstVertsY);
	
			// Traverse the area in destination heigthfield coordinates
			for (int32 RowIndex = 0; RowIndex < DstVertsY; RowIndex++)
			{
				for (int32 ColIndex = 0; ColIndex < DstVertsX; ColIndex++)
				{
					int32 SrcX = bIsMirrored ? (RowIndex + ComponentX1) : (ComponentX2 - RowIndex);
					int32 SrcY = ColIndex + ComponentY1;
					int32 SrcSampleIndex = (SrcY * CollisionSizeVerts) + SrcX;
					check(SrcSampleIndex < FMath::Square(CollisionSizeVerts));
					int32 DstSampleIndex = (RowIndex * DstVertsX) + ColIndex;
	
					PxHeightFieldSample& Sample = Samples[DstSampleIndex];
					Sample.height = FMath::Clamp<int32>(((int32)Heights[SrcSampleIndex] - 32768), -32768, 32767);
	
					Sample.materialIndex0 = 0;
					Sample.materialIndex1 = 0;
				}
			}
	
			CollisionHeightData.Unlock();
	
			PxHeightFieldDesc SubDesc;
			SubDesc.format = PxHeightFieldFormat::eS16_TM;
			SubDesc.nbColumns = DstVertsX;
			SubDesc.nbRows = DstVertsY;
			SubDesc.samples.data = Samples.GetData();
			SubDesc.samples.stride = sizeof(PxU32);
			SubDesc.flags = PxHeightFieldFlag::eNO_BOUNDARY_EDGES;
	
			HeightfieldRef->RBHeightfieldEd->modifySamples(HeightfieldX1, HeightfieldY1, SubDesc, true);
	
			//
			// Reset geometry of heightfield shape. Required by the modifySamples
			//
			FVector LandscapeScale = GetComponentToWorld().GetScale3D().GetAbs();
			// Create the geometry
			PxHeightFieldGeometry LandscapeComponentGeom(HeightfieldRef->RBHeightfieldEd, PxMeshGeometryFlags(), LandscapeScale.Z * LANDSCAPE_ZSCALE, LandscapeScale.Y * CollisionScale, LandscapeScale.X * CollisionScale);

			{
				FInlineShapeArray PShapes;

				const int32 NumShapes = FillInlineShapeArray_AssumesLocked(PShapes, Actor);
				if(NumShapes > 1)
				{
					FPhysicsInterface::SetGeometry(PShapes[1], LandscapeComponentGeom);
				}
			}

#elif WITH_CHAOS
			int32 HeightfieldY1 = ComponentY1;
			int32 HeightfieldX1 = (bIsMirrored ? ComponentX1 : (CollisionSizeVerts - ComponentX2 - 1));
			int32 DstVertsX = ComponentX2 - ComponentX1 + 1;
			int32 DstVertsY = ComponentY2 - ComponentY1 + 1;
			TArray<uint16> Samples;
			Samples.AddZeroed(DstVertsX*DstVertsY);

			for(int32 RowIndex = 0; RowIndex < DstVertsY; RowIndex++)
			{
				for(int32 ColIndex = 0; ColIndex < DstVertsX; ColIndex++)
				{
					int32 SrcX = bIsMirrored ? (ColIndex + ComponentX1) : (ComponentX2 - ColIndex);
					int32 SrcY = RowIndex + ComponentY1;
					int32 SrcSampleIndex = (SrcY * CollisionSizeVerts) + SrcX;
					check(SrcSampleIndex < FMath::Square(CollisionSizeVerts));
					int32 DstSampleIndex = (RowIndex * DstVertsX) + ColIndex;

					Samples[DstSampleIndex] = Heights[SrcSampleIndex];
				}
			}

			CollisionHeightData.Unlock();

			HeightfieldRef->EditorHeightfield->EditHeights(Samples, HeightfieldY1, HeightfieldX1, DstVertsY, DstVertsX);

#if WITH_CHAOS
			// Rebuild geometry to update local bounds, and update in acceleration structure.
			const Chaos::FImplicitObjectUnion& Union = PhysActorHandle->GetGameThreadAPI().Geometry()->GetObjectChecked<Chaos::FImplicitObjectUnion>();
			TArray<TUniquePtr<Chaos::FImplicitObject>> NewGeometry;
			for (const TUniquePtr<Chaos::FImplicitObject>& Object : Union.GetObjects())
			{
				const Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>& TransformedHeightField = Object->GetObjectChecked<Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>>();
				NewGeometry.Emplace(MakeUnique<Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>>(TransformedHeightField.Object(), TransformedHeightField.GetTransform()));
			}
			PhysActorHandle->GetGameThreadAPI().SetGeometry(MakeUnique<Chaos::FImplicitObjectUnion>(MoveTemp(NewGeometry)));

			FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();
			PhysScene->UpdateActorInAccelerationStructure(PhysActorHandle);
#endif
#endif
		});
	}

#endif// WITH_PHYSX
}
#endif// WITH_EDITOR

void ULandscapeHeightfieldCollisionComponent::DestroyComponent(bool bPromoteChildren/*= false*/)
{
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	if (Proxy)
	{
		Proxy->CollisionComponents.Remove(this);
	}

	Super::DestroyComponent(bPromoteChildren);
}

FBoxSphereBounds ULandscapeHeightfieldCollisionComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return CachedLocalBox.TransformBy(LocalToWorld);
}

void ULandscapeHeightfieldCollisionComponent::BeginDestroy()
{
	HeightfieldRef = NULL;
	HeightfieldGuid = FGuid();
	Super::BeginDestroy();
}

void ULandscapeMeshCollisionComponent::BeginDestroy()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		MeshRef = NULL;
		MeshGuid = FGuid();
	}

	Super::BeginDestroy();
}

bool ULandscapeHeightfieldCollisionComponent::RecreateCollision()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
#if WITH_EDITOR
		uint32 NewHash = ComputeCollisionHash();
		if (bPhysicsStateCreated && NewHash == CollisionHash && CollisionHash != 0 && bEnableCollisionHashOptim)
		{
			return false;
		}
		CollisionHash = NewHash;
#endif
		HeightfieldRef = NULL;
		HeightfieldGuid = FGuid();

		RecreatePhysicsState();
	}
	return true;
}

#if WITH_EDITORONLY_DATA
void ULandscapeHeightfieldCollisionComponent::SnapFoliageInstances()
{
	SnapFoliageInstances(FBox(FVector(-WORLD_MAX), FVector(WORLD_MAX)));
}

void ULandscapeHeightfieldCollisionComponent::SnapFoliageInstances(const FBox& InInstanceBox)
{
	UWorld* ComponentWorld = GetWorld();
	for (TActorIterator<AInstancedFoliageActor> It(ComponentWorld); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		const auto BaseId = IFA->InstanceBaseCache.GetInstanceBaseId(this);
		if (BaseId == FFoliageInstanceBaseCache::InvalidBaseId)
		{
			continue;
		}
			
		for (auto& Pair : IFA->FoliageInfos)
		{
			// Find the per-mesh info matching the mesh.
			UFoliageType* Settings = Pair.Key;
			FFoliageInfo& MeshInfo = *Pair.Value;
			
			const auto* InstanceSet = MeshInfo.ComponentHash.Find(BaseId);
			if (InstanceSet)
			{
				float TraceExtentSize = Bounds.SphereRadius * 2.f + 10.f; // extend a little
				FVector TraceVector = GetOwner()->GetRootComponent()->GetComponentTransform().GetUnitAxis(EAxis::Z) * TraceExtentSize;

				TArray<int32> InstancesToRemove;
				TSet<UHierarchicalInstancedStaticMeshComponent*> AffectedFoliageComponents;
				
				bool bIsMeshInfoDirty = false;
				for (int32 InstanceIndex : *InstanceSet)
				{
					FFoliageInstance& Instance = MeshInfo.Instances[InstanceIndex];

					// Test location should remove any Z offset
					FVector TestLocation = FMath::Abs(Instance.ZOffset) > KINDA_SMALL_NUMBER
						? (FVector)Instance.GetInstanceWorldTransform().TransformPosition(FVector(0, 0, -Instance.ZOffset))
						: Instance.Location;

					if (InInstanceBox.IsInside(TestLocation))
					{
						FVector Start = TestLocation + TraceVector;
						FVector End = TestLocation - TraceVector;

						TArray<FHitResult> Results;
						UWorld* World = GetWorld();
						check(World);
						// Editor specific landscape heightfield uses ECC_Visibility collision channel
						World->LineTraceMultiByObjectType(Results, Start, End, FCollisionObjectQueryParams(ECollisionChannel::ECC_Visibility), FCollisionQueryParams(SCENE_QUERY_STAT(FoliageSnapToLandscape), true));

						bool bFoundHit = false;
						for (const FHitResult& Hit : Results)
						{
							if (Hit.Component == this)
							{
								bFoundHit = true;
								if ((TestLocation - Hit.Location).SizeSquared() > KINDA_SMALL_NUMBER)
								{
									IFA->Modify();

									bIsMeshInfoDirty = true;

									// Remove instance location from the hash. Do not need to update ComponentHash as we re-add below.
									MeshInfo.InstanceHash->RemoveInstance(Instance.Location, InstanceIndex);

									// Update the instance editor data
									Instance.Location = Hit.Location;

									if (Instance.Flags & FOLIAGE_AlignToNormal)
									{
										// Remove previous alignment and align to new normal.
										Instance.Rotation = Instance.PreAlignRotation;
										Instance.AlignToNormal(Hit.Normal, Settings->AlignMaxAngle);
									}

									// Reapply the Z offset in local space
									if (FMath::Abs(Instance.ZOffset) > KINDA_SMALL_NUMBER)
									{
										Instance.Location = Instance.GetInstanceWorldTransform().TransformPosition(FVector(0, 0, Instance.ZOffset));
									}

									// Todo: add do validation with other parameters such as max/min height etc.

									MeshInfo.SetInstanceWorldTransform(InstanceIndex, Instance.GetInstanceWorldTransform(), false);
									// Re-add the new instance location to the hash
									MeshInfo.InstanceHash->InsertInstance(Instance.Location, InstanceIndex);
								}
								break;
							}
						}

						if (!bFoundHit)
						{
							// Couldn't find new spot - remove instance
							InstancesToRemove.Add(InstanceIndex);
							bIsMeshInfoDirty = true;
						}

						if (bIsMeshInfoDirty && (MeshInfo.GetComponent() != nullptr))
						{
							AffectedFoliageComponents.Add(MeshInfo.GetComponent());
						}
					}
				}

				// Remove any unused instances
				MeshInfo.RemoveInstances(IFA, InstancesToRemove, true);

				for (UHierarchicalInstancedStaticMeshComponent* FoliageComp : AffectedFoliageComponents)
				{
					FoliageComp->InvalidateLightingCache();
				}
			}
		}
	}
}
#endif // WITH_EDITORONLY_DATA

bool ULandscapeMeshCollisionComponent::RecreateCollision()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		MeshRef = NULL;
		MeshGuid = FGuid();
		CachedHeightFieldSamples.Heights.Empty();
		CachedHeightFieldSamples.Holes.Empty();
	}

	return Super::RecreateCollision();
}

void ULandscapeHeightfieldCollisionComponent::Serialize(FArchive& Ar)
{
#if WITH_EDITOR
	if (Ar.UE4Ver() >= VER_UE4_LANDSCAPE_COLLISION_DATA_COOKING)
	{
		// Cook data here so CookedPhysicalMaterials is always up to date
		if (Ar.IsCooking() && !HasAnyFlags(RF_ClassDefaultObject))
		{
			FName Format = Ar.CookingTarget()->GetPhysicsFormat(nullptr);
			CookCollisionData(Format, false, true, CookedCollisionData, CookedPhysicalMaterials);
		}
	}
#endif// WITH_EDITOR

	// this will also serialize CookedPhysicalMaterials
	Super::Serialize(Ar);

	if (Ar.UE4Ver() < VER_UE4_LANDSCAPE_COLLISION_DATA_COOKING)
	{
#if WITH_EDITORONLY_DATA
		CollisionHeightData.Serialize(Ar, this);
		DominantLayerData.Serialize(Ar, this);
#endif//WITH_EDITORONLY_DATA
	}
	else
	{
		bool bCooked = Ar.IsCooking() || (FPlatformProperties::RequiresCookedData() && Ar.IsSaving());
		Ar << bCooked;

		if (FPlatformProperties::RequiresCookedData() && !bCooked && Ar.IsLoading())
		{
			UE_LOG(LogPhysics, Fatal, TEXT("This platform requires cooked packages, and physX data was not cooked into %s."), *GetFullName());
		}

		if (bCooked)
		{
			CookedCollisionData.BulkSerialize(Ar);
		}
		else
		{
#if WITH_EDITORONLY_DATA
			// For PIE, we won't need the source height data if we already have a shared reference to the heightfield
			if (!(Ar.GetPortFlags() & PPF_DuplicateForPIE) || !HeightfieldGuid.IsValid() || GSharedMeshRefs.FindRef(HeightfieldGuid) == nullptr)
			{
				CollisionHeightData.Serialize(Ar, this);
				DominantLayerData.Serialize(Ar, this);

				if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::LandscapePhysicalMaterialRenderData)
				{
					PhysicalMaterialRenderData.Serialize(Ar, this);
				}
			}
#endif//WITH_EDITORONLY_DATA
		}
	}
}

void ULandscapeMeshCollisionComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.UE4Ver() < VER_UE4_LANDSCAPE_COLLISION_DATA_COOKING)
	{
#if WITH_EDITORONLY_DATA
		// conditional serialization in later versions
		CollisionXYOffsetData.Serialize(Ar, this);
#endif// WITH_EDITORONLY_DATA
	}

	// PhysX cooking mesh data
	bool bCooked = false;
	if (Ar.UE4Ver() >= VER_UE4_ADD_COOKED_TO_LANDSCAPE)
	{
		bCooked = Ar.IsCooking();
		Ar << bCooked;
	}

	if (FPlatformProperties::RequiresCookedData() && !bCooked && Ar.IsLoading())
	{
		UE_LOG(LogPhysics, Fatal, TEXT("This platform requires cooked packages, and physX data was not cooked into %s."), *GetFullName());
	}

	if (bCooked)
	{
		// triangle mesh cooked data should be serialized in ULandscapeHeightfieldCollisionComponent
	}
	else if (Ar.UE4Ver() >= VER_UE4_LANDSCAPE_COLLISION_DATA_COOKING)
	{
#if WITH_EDITORONLY_DATA		
		// we serialize raw collision data only with non-cooked content
		CollisionXYOffsetData.Serialize(Ar, this);
#endif// WITH_EDITORONLY_DATA
	}
}

#if WITH_EDITOR
void ULandscapeHeightfieldCollisionComponent::PostEditImport()
{
	Super::PostEditImport();

	if (!GetLandscapeProxy()->HasLayersContent())
	{
		// Reinitialize physics after paste
		if (CollisionSizeQuads > 0)
		{
			RecreateCollision();
		}
	}
}

void ULandscapeHeightfieldCollisionComponent::PostEditUndo()
{
	Super::PostEditUndo();

    // Landscape Layers are updates are delayed and done in  ALandscape::TickLayers
	if (!GetLandscapeProxy()->HasLayersContent())
	{
		// Reinitialize physics after undo
		if (CollisionSizeQuads > 0)
		{
			RecreateCollision();
		}

		FNavigationSystem::UpdateComponentData(*this);
	}
}

bool ULandscapeHeightfieldCollisionComponent::ComponentIsTouchingSelectionBox(const FBox& InSelBBox, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const
{
	if (ShowFlags.Landscape)
	{
		return Super::ComponentIsTouchingSelectionBox(InSelBBox, ShowFlags, bConsiderOnlyBSP, bMustEncompassEntireComponent);
	}

	return false;
}

bool ULandscapeHeightfieldCollisionComponent::ComponentIsTouchingSelectionFrustum(const FConvexVolume& InFrustum, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const
{
	if (ShowFlags.Landscape)
	{
		return Super::ComponentIsTouchingSelectionFrustum(InFrustum, ShowFlags, bConsiderOnlyBSP, bMustEncompassEntireComponent);
	}

	return false;
}
#endif

bool ULandscapeHeightfieldCollisionComponent::DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const
{
	check(IsInGameThread());
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
	if (IsValidRef(HeightfieldRef) && HeightfieldRef->RBHeightfield)
	{
		FTransform HFToW = GetComponentTransform();
		if (HeightfieldRef->RBHeightfieldSimple)
		{
			const float SimpleCollisionScale = CollisionScale * CollisionSizeQuads / SimpleCollisionSizeQuads;
			HFToW.MultiplyScale3D(FVector(SimpleCollisionScale, SimpleCollisionScale, LANDSCAPE_ZSCALE));
			GeomExport.ExportPxHeightField(HeightfieldRef->RBHeightfieldSimple, HFToW);
		}
		else
		{
			HFToW.MultiplyScale3D(FVector(CollisionScale, CollisionScale, LANDSCAPE_ZSCALE));
			GeomExport.ExportPxHeightField(HeightfieldRef->RBHeightfield, HFToW);
		}
	}
#elif WITH_CHAOS
	if(IsValidRef(HeightfieldRef) && HeightfieldRef->Heightfield)
	{
		FTransform HFToW = GetComponentTransform();
		if(HeightfieldRef->HeightfieldSimple)
		{
			const float SimpleCollisionScale = CollisionScale * CollisionSizeQuads / SimpleCollisionSizeQuads;
			HFToW.MultiplyScale3D(FVector(SimpleCollisionScale, SimpleCollisionScale, LANDSCAPE_ZSCALE));
			GeomExport.ExportChaosHeightField(HeightfieldRef->HeightfieldSimple.Get(), HFToW);
		}
		else
		{
			HFToW.MultiplyScale3D(FVector(CollisionScale, CollisionScale, LANDSCAPE_ZSCALE));
			GeomExport.ExportChaosHeightField(HeightfieldRef->Heightfield.Get(), HFToW);
		}
	}
#endif

	return false;
}

void ULandscapeHeightfieldCollisionComponent::GatherGeometrySlice(FNavigableGeometryExport& GeomExport, const FBox& SliceBox) const
{
	// note that this function can get called off game thread
	if (CachedHeightFieldSamples.IsEmpty() == false)
	{
		FTransform HFToW = GetComponentTransform();
		HFToW.MultiplyScale3D(FVector(CollisionScale, CollisionScale, LANDSCAPE_ZSCALE));

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		GeomExport.ExportPxHeightFieldSlice(CachedHeightFieldSamples, HeightfieldRowsCount, HeightfieldColumnsCount, HFToW, SliceBox);
#elif WITH_CHAOS
		GeomExport.ExportChaosHeightFieldSlice(CachedHeightFieldSamples, HeightfieldRowsCount, HeightfieldColumnsCount, HFToW, SliceBox);
#endif
	}
}

ENavDataGatheringMode ULandscapeHeightfieldCollisionComponent::GetGeometryGatheringMode() const
{ 
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	return Proxy ? Proxy->NavigationGeometryGatheringMode : ENavDataGatheringMode::Default;
}

void ULandscapeHeightfieldCollisionComponent::PrepareGeometryExportSync()
{
	//check(IsInGameThread());
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
	if (IsValidRef(HeightfieldRef) && HeightfieldRef->RBHeightfield != nullptr && CachedHeightFieldSamples.IsEmpty())
	{
		const UWorld* World = GetWorld();

		if (World != nullptr)
		{
			HeightfieldRowsCount = HeightfieldRef->RBHeightfield->getNbRows();
			HeightfieldColumnsCount = HeightfieldRef->RBHeightfield->getNbColumns();
			const int32 SamplesCount = HeightfieldRowsCount * HeightfieldColumnsCount;
				
			if (CachedHeightFieldSamples.Heights.Num() != SamplesCount)
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_NavMesh_ExportPxHeightField_saveCells);

				CachedHeightFieldSamples.Heights.SetNumUninitialized(SamplesCount);

				TArray<PxHeightFieldSample> HFSamples;
				HFSamples.SetNumUninitialized(SamplesCount);
				HeightfieldRef->RBHeightfield->saveCells(HFSamples.GetData(), HFSamples.Num()*HFSamples.GetTypeSize());

				for (int32 SampleIndex = 0; SampleIndex < HFSamples.Num(); ++SampleIndex)
				{
					const PxHeightFieldSample& Sample = HFSamples[SampleIndex];
					CachedHeightFieldSamples.Heights[SampleIndex] = Sample.height;
					CachedHeightFieldSamples.Holes.Add((Sample.materialIndex0 == PxHeightFieldMaterial::eHOLE));
				}
			}
		}
	}
#elif WITH_CHAOS
	if(IsValidRef(HeightfieldRef) && HeightfieldRef->Heightfield.Get() && CachedHeightFieldSamples.IsEmpty())
	{
		const UWorld* World = GetWorld();

		if(World != nullptr)
		{
			HeightfieldRowsCount = HeightfieldRef->Heightfield->GetNumRows();
			HeightfieldColumnsCount = HeightfieldRef->Heightfield->GetNumCols();
			const int32 HeightsCount = HeightfieldRowsCount * HeightfieldColumnsCount;

			if(CachedHeightFieldSamples.Heights.Num() != HeightsCount)
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_NavMesh_ExportChaosHeightField_saveCells);

				CachedHeightFieldSamples.Heights.SetNumUninitialized(HeightsCount);
				for(int32 Index = 0; Index < HeightsCount; ++Index)
				{
					CachedHeightFieldSamples.Heights[Index] = HeightfieldRef->Heightfield->GetHeight(Index);
				}

				const int32 HolesCount = (HeightfieldRowsCount-1) * (HeightfieldColumnsCount-1);
				CachedHeightFieldSamples.Holes.SetNumUninitialized(HolesCount);
				for(int32 Index = 0; Index < HolesCount; ++Index)
				{
					CachedHeightFieldSamples.Holes[Index] = HeightfieldRef->Heightfield->IsHole(Index);
				}
			}
		}
	}

#endif// WITH_PHYSX
}

bool ULandscapeMeshCollisionComponent::DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const
{
	check(IsInGameThread());
#if PHYSICS_INTERFACE_PHYSX
	if (IsValidRef(MeshRef) && MeshRef->RBTriangleMesh != nullptr)
	{
		FTransform MeshToW = GetComponentTransform();
		MeshToW.MultiplyScale3D(FVector(CollisionScale, CollisionScale, 1.f));

		if (MeshRef->RBTriangleMesh->getTriangleMeshFlags() & PxTriangleMeshFlag::e16_BIT_INDICES)
		{
			GeomExport.ExportPxTriMesh16Bit(MeshRef->RBTriangleMesh, MeshToW);
		}
		else
		{
			GeomExport.ExportPxTriMesh32Bit(MeshRef->RBTriangleMesh, MeshToW);
		}
	}
#elif WITH_CHAOS
	CHAOS_ENSURE(false);
#endif
	return false;
}

void ULandscapeHeightfieldCollisionComponent::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	// PostLoad of the landscape can decide to recreate collision, in which case this components checks are irrelevant
	if (!HasAnyFlags(RF_ClassDefaultObject) && !IsPendingKill())
	{
		bShouldSaveCookedDataToDDC[0] = true;
		bShouldSaveCookedDataToDDC[1] = true;

		ALandscapeProxy* LandscapeProxy = GetLandscapeProxy();
		if (ensure(LandscapeProxy) && GIsEditor)
		{
			// This is to ensure that component relative location is exact section base offset value
			FVector LocalRelativeLocation = GetRelativeLocation();
			float CheckRelativeLocationX = float(SectionBaseX - LandscapeProxy->LandscapeSectionOffset.X);
			float CheckRelativeLocationY = float(SectionBaseY - LandscapeProxy->LandscapeSectionOffset.Y);
			if (CheckRelativeLocationX != LocalRelativeLocation.X ||
				CheckRelativeLocationY != LocalRelativeLocation.Y)
			{
				UE_LOG(LogLandscape, Warning, TEXT("ULandscapeHeightfieldCollisionComponent RelativeLocation disagrees with its section base, attempted automated fix: '%s', %f,%f vs %f,%f."),
					*GetFullName(), LocalRelativeLocation.X, LocalRelativeLocation.Y, CheckRelativeLocationX, CheckRelativeLocationY);
				LocalRelativeLocation.X = CheckRelativeLocationX;
				LocalRelativeLocation.Y = CheckRelativeLocationY;
				SetRelativeLocation_Direct(LocalRelativeLocation);
			}
		}

		UWorld* World = GetWorld();
		if (World && World->IsGameWorld())
		{
			SpeculativelyLoadAsyncDDCCollsionData();
		}
	}
#endif//WITH_EDITOR
}

void ULandscapeHeightfieldCollisionComponent::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	if (!IsRunningCommandlet())
	{
#if WITH_EDITOR
		ALandscapeProxy* Proxy = GetLandscapeProxy();
		if (Proxy && Proxy->bBakeMaterialPositionOffsetIntoCollision)
		{
			if (!RenderComponent->GrassData->HasData() || RenderComponent->IsGrassMapOutdated())
			{
				if (!RenderComponent->CanRenderGrassMap())
				{
					RenderComponent->GetMaterialInstance(0, false)->GetMaterialResource(GetWorld()->FeatureLevel)->FinishCompilation();
				}
				RenderComponent->RenderGrassMap();
			}
		}
#endif// WITH_EDITOR
	}
}

#if WITH_EDITOR
void ULandscapeInfo::UpdateAllAddCollisions()
{
	XYtoAddCollisionMap.Reset();

	// Don't recreate add collisions if the landscape is not registered. This can happen during Undo.
	if (GetLandscapeProxy())
	{
		for (auto It = XYtoComponentMap.CreateIterator(); It; ++It)
		{
			const ULandscapeComponent* const Component = It.Value();
			if (ensure(Component))
			{
				const FIntPoint ComponentBase = Component->GetSectionBase() / ComponentSizeQuads;

				const FIntPoint NeighborsKeys[8] =
				{
					ComponentBase + FIntPoint(-1, -1),
					ComponentBase + FIntPoint(+0, -1),
					ComponentBase + FIntPoint(+1, -1),
					ComponentBase + FIntPoint(-1, +0),
					ComponentBase + FIntPoint(+1, +0),
					ComponentBase + FIntPoint(-1, +1),
					ComponentBase + FIntPoint(+0, +1),
					ComponentBase + FIntPoint(+1, +1)
				};

				// Search for Neighbors...
				for (int32 i = 0; i < 8; ++i)
				{
					ULandscapeComponent* NeighborComponent = XYtoComponentMap.FindRef(NeighborsKeys[i]);

					// UpdateAddCollision() treats a null CollisionComponent as an empty hole
					if (!NeighborComponent || !NeighborComponent->CollisionComponent.IsValid())
					{
						UpdateAddCollision(NeighborsKeys[i]);
					}
				}
			}
		}
	}
}

void ULandscapeInfo::UpdateAddCollision(FIntPoint LandscapeKey)
{
	FLandscapeAddCollision& AddCollision = XYtoAddCollisionMap.FindOrAdd(LandscapeKey);

	// 8 Neighbors...
	// 0 1 2
	// 3   4
	// 5 6 7
	FIntPoint NeighborsKeys[8] =
	{
		LandscapeKey + FIntPoint(-1, -1),
		LandscapeKey + FIntPoint(+0, -1),
		LandscapeKey + FIntPoint(+1, -1),
		LandscapeKey + FIntPoint(-1, +0),
		LandscapeKey + FIntPoint(+1, +0),
		LandscapeKey + FIntPoint(-1, +1),
		LandscapeKey + FIntPoint(+0, +1),
		LandscapeKey + FIntPoint(+1, +1)
	};

	// Todo: Use data accessor not collision

	ULandscapeHeightfieldCollisionComponent* NeighborCollisions[8];
	// Search for Neighbors...
	for (int32 i = 0; i < 8; ++i)
	{
		ULandscapeComponent* Comp = XYtoComponentMap.FindRef(NeighborsKeys[i]);
		if (Comp)
		{
			NeighborCollisions[i] = Comp->CollisionComponent.Get();
		}
		else
		{
			NeighborCollisions[i] = NULL;
		}
	}

	uint8 CornerSet = 0;
	uint16 HeightCorner[4];

	// Corner Cases...
	if (NeighborCollisions[0])
	{
		uint16* Heights = (uint16*)NeighborCollisions[0]->CollisionHeightData.Lock(LOCK_READ_ONLY);
		int32 CollisionSizeVerts = NeighborCollisions[0]->CollisionSizeQuads + 1;
		HeightCorner[0] = Heights[CollisionSizeVerts - 1 + (CollisionSizeVerts - 1)*CollisionSizeVerts];
		CornerSet |= 1;
		NeighborCollisions[0]->CollisionHeightData.Unlock();
	}
	if (NeighborCollisions[2])
	{
		uint16* Heights = (uint16*)NeighborCollisions[2]->CollisionHeightData.Lock(LOCK_READ_ONLY);
		int32 CollisionSizeVerts = NeighborCollisions[2]->CollisionSizeQuads + 1;
		HeightCorner[1] = Heights[(CollisionSizeVerts - 1)*CollisionSizeVerts];
		CornerSet |= 1 << 1;
		NeighborCollisions[2]->CollisionHeightData.Unlock();
	}
	if (NeighborCollisions[5])
	{
		uint16* Heights = (uint16*)NeighborCollisions[5]->CollisionHeightData.Lock(LOCK_READ_ONLY);
		int32 CollisionSizeVerts = NeighborCollisions[5]->CollisionSizeQuads + 1;
		HeightCorner[2] = Heights[(CollisionSizeVerts - 1)];
		CornerSet |= 1 << 2;
		NeighborCollisions[5]->CollisionHeightData.Unlock();
	}
	if (NeighborCollisions[7])
	{
		uint16* Heights = (uint16*)NeighborCollisions[7]->CollisionHeightData.Lock(LOCK_READ_ONLY);
		int32 CollisionSizeVerts = NeighborCollisions[7]->CollisionSizeQuads + 1;
		HeightCorner[3] = Heights[0];
		CornerSet |= 1 << 3;
		NeighborCollisions[7]->CollisionHeightData.Unlock();
	}

	// Other cases...
	if (NeighborCollisions[1])
	{
		uint16* Heights = (uint16*)NeighborCollisions[1]->CollisionHeightData.Lock(LOCK_READ_ONLY);
		int32 CollisionSizeVerts = NeighborCollisions[1]->CollisionSizeQuads + 1;
		HeightCorner[0] = Heights[(CollisionSizeVerts - 1)*CollisionSizeVerts];
		CornerSet |= 1;
		HeightCorner[1] = Heights[CollisionSizeVerts - 1 + (CollisionSizeVerts - 1)*CollisionSizeVerts];
		CornerSet |= 1 << 1;
		NeighborCollisions[1]->CollisionHeightData.Unlock();
	}
	if (NeighborCollisions[3])
	{
		uint16* Heights = (uint16*)NeighborCollisions[3]->CollisionHeightData.Lock(LOCK_READ_ONLY);
		int32 CollisionSizeVerts = NeighborCollisions[3]->CollisionSizeQuads + 1;
		HeightCorner[0] = Heights[(CollisionSizeVerts - 1)];
		CornerSet |= 1;
		HeightCorner[2] = Heights[CollisionSizeVerts - 1 + (CollisionSizeVerts - 1)*CollisionSizeVerts];
		CornerSet |= 1 << 2;
		NeighborCollisions[3]->CollisionHeightData.Unlock();
	}
	if (NeighborCollisions[4])
	{
		uint16* Heights = (uint16*)NeighborCollisions[4]->CollisionHeightData.Lock(LOCK_READ_ONLY);
		int32 CollisionSizeVerts = NeighborCollisions[4]->CollisionSizeQuads + 1;
		HeightCorner[1] = Heights[0];
		CornerSet |= 1 << 1;
		HeightCorner[3] = Heights[(CollisionSizeVerts - 1)*CollisionSizeVerts];
		CornerSet |= 1 << 3;
		NeighborCollisions[4]->CollisionHeightData.Unlock();
	}
	if (NeighborCollisions[6])
	{
		uint16* Heights = (uint16*)NeighborCollisions[6]->CollisionHeightData.Lock(LOCK_READ_ONLY);
		int32 CollisionSizeVerts = NeighborCollisions[6]->CollisionSizeQuads + 1;
		HeightCorner[2] = Heights[0];
		CornerSet |= 1 << 2;
		HeightCorner[3] = Heights[(CollisionSizeVerts - 1)];
		CornerSet |= 1 << 3;
		NeighborCollisions[6]->CollisionHeightData.Unlock();
	}

	// Fill unset values
	// First iteration only for valid values distance 1 propagation
	// Second iteration fills left ones...
	FillCornerValues(CornerSet, HeightCorner);
	//check(CornerSet == 15);

	FIntPoint SectionBase = LandscapeKey * ComponentSizeQuads;

	// Transform Height to Vectors...
	FTransform LtoW = GetLandscapeProxy()->LandscapeActorToWorld();
	AddCollision.Corners[0] = LtoW.TransformPosition(FVector(SectionBase.X                     , SectionBase.Y                     , LandscapeDataAccess::GetLocalHeight(HeightCorner[0])));
	AddCollision.Corners[1] = LtoW.TransformPosition(FVector(SectionBase.X + ComponentSizeQuads, SectionBase.Y                     , LandscapeDataAccess::GetLocalHeight(HeightCorner[1])));
	AddCollision.Corners[2] = LtoW.TransformPosition(FVector(SectionBase.X                     , SectionBase.Y + ComponentSizeQuads, LandscapeDataAccess::GetLocalHeight(HeightCorner[2])));
	AddCollision.Corners[3] = LtoW.TransformPosition(FVector(SectionBase.X + ComponentSizeQuads, SectionBase.Y + ComponentSizeQuads, LandscapeDataAccess::GetLocalHeight(HeightCorner[3])));
}

void ULandscapeHeightfieldCollisionComponent::ExportCustomProperties(FOutputDevice& Out, uint32 Indent)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	int32 CollisionSizeVerts = CollisionSizeQuads + 1;
	int32 SimpleCollisionSizeVerts = SimpleCollisionSizeQuads > 0 ? SimpleCollisionSizeQuads + 1 : 0;
	int32 NumHeights = FMath::Square(CollisionSizeVerts) + FMath::Square(SimpleCollisionSizeVerts);
	check(CollisionHeightData.GetElementCount() == NumHeights);

	uint16* Heights = (uint16*)CollisionHeightData.Lock(LOCK_READ_ONLY);

	Out.Logf(TEXT("%sCustomProperties CollisionHeightData "), FCString::Spc(Indent));
	for (int32 i = 0; i < NumHeights; i++)
	{
		Out.Logf(TEXT("%d "), Heights[i]);
	}

	CollisionHeightData.Unlock();
	Out.Logf(TEXT("\r\n"));

	int32 NumDominantLayerSamples = DominantLayerData.GetElementCount();
	check(NumDominantLayerSamples == 0 || NumDominantLayerSamples == NumHeights);

	if (NumDominantLayerSamples > 0)
	{
		uint8* DominantLayerSamples = (uint8*)DominantLayerData.Lock(LOCK_READ_ONLY);

		Out.Logf(TEXT("%sCustomProperties DominantLayerData "), FCString::Spc(Indent));
		for (int32 i = 0; i < NumDominantLayerSamples; i++)
		{
			Out.Logf(TEXT("%02x"), DominantLayerSamples[i]);
		}

		DominantLayerData.Unlock();
		Out.Logf(TEXT("\r\n"));
	}
}

void ULandscapeHeightfieldCollisionComponent::ImportCustomProperties(const TCHAR* SourceText, FFeedbackContext* Warn)
{
	if (FParse::Command(&SourceText, TEXT("CollisionHeightData")))
	{
		int32 CollisionSizeVerts = CollisionSizeQuads + 1;
		int32 SimpleCollisionSizeVerts = SimpleCollisionSizeQuads > 0 ? SimpleCollisionSizeQuads + 1 : 0;
		int32 NumHeights = FMath::Square(CollisionSizeVerts) + FMath::Square(SimpleCollisionSizeVerts);

		CollisionHeightData.Lock(LOCK_READ_WRITE);
		uint16* Heights = (uint16*)CollisionHeightData.Realloc(NumHeights);
		FMemory::Memzero(Heights, sizeof(uint16)*NumHeights);

		FParse::Next(&SourceText);
		int32 i = 0;
		while (FChar::IsDigit(*SourceText))
		{
			if (i < NumHeights)
			{
				Heights[i++] = FCString::Atoi(SourceText);
				while (FChar::IsDigit(*SourceText))
				{
					SourceText++;
				}
			}

			FParse::Next(&SourceText);
		}

		CollisionHeightData.Unlock();

		if (i != NumHeights)
		{
			Warn->Log(*NSLOCTEXT("Core", "SyntaxError", "Syntax Error").ToString());
		}
	}
	else if (FParse::Command(&SourceText, TEXT("DominantLayerData")))
	{
		int32 NumDominantLayerSamples = FMath::Square(CollisionSizeQuads + 1);

		DominantLayerData.Lock(LOCK_READ_WRITE);
		uint8* DominantLayerSamples = (uint8*)DominantLayerData.Realloc(NumDominantLayerSamples);
		FMemory::Memzero(DominantLayerSamples, NumDominantLayerSamples);

		FParse::Next(&SourceText);
		int32 i = 0;
		while (SourceText[0] && SourceText[1])
		{
			if (i < NumDominantLayerSamples)
			{
				DominantLayerSamples[i++] = FParse::HexDigit(SourceText[0]) * 16 + FParse::HexDigit(SourceText[1]);
			}
			SourceText += 2;
		}

		DominantLayerData.Unlock();

		if (i != NumDominantLayerSamples)
		{
			Warn->Log(*NSLOCTEXT("Core", "SyntaxError", "Syntax Error").ToString());
		}
	}
}

void ULandscapeMeshCollisionComponent::ExportCustomProperties(FOutputDevice& Out, uint32 Indent)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	Super::ExportCustomProperties(Out, Indent);

	uint16* XYOffsets = (uint16*)CollisionXYOffsetData.Lock(LOCK_READ_ONLY);
	int32 NumOffsets = FMath::Square(CollisionSizeQuads + 1) * 2;
	check(CollisionXYOffsetData.GetElementCount() == NumOffsets);

	Out.Logf(TEXT("%sCustomProperties CollisionXYOffsetData "), FCString::Spc(Indent));
	for (int32 i = 0; i < NumOffsets; i++)
	{
		Out.Logf(TEXT("%d "), XYOffsets[i]);
	}

	CollisionXYOffsetData.Unlock();
	Out.Logf(TEXT("\r\n"));
}

void ULandscapeMeshCollisionComponent::ImportCustomProperties(const TCHAR* SourceText, FFeedbackContext* Warn)
{
	if (FParse::Command(&SourceText, TEXT("CollisionHeightData")))
	{
		int32 CollisionSizeVerts = CollisionSizeQuads + 1;
		int32 SimpleCollisionSizeVerts = SimpleCollisionSizeQuads > 0 ? SimpleCollisionSizeQuads + 1 : 0;
		int32 NumHeights = FMath::Square(CollisionSizeVerts) + FMath::Square(SimpleCollisionSizeVerts);

		CollisionHeightData.Lock(LOCK_READ_WRITE);
		uint16* Heights = (uint16*)CollisionHeightData.Realloc(NumHeights);
		FMemory::Memzero(Heights, sizeof(uint16)*NumHeights);

		FParse::Next(&SourceText);
		int32 i = 0;
		while (FChar::IsDigit(*SourceText))
		{
			if (i < NumHeights)
			{
				Heights[i++] = FCString::Atoi(SourceText);
				while (FChar::IsDigit(*SourceText))
				{
					SourceText++;
				}
			}

			FParse::Next(&SourceText);
		}

		CollisionHeightData.Unlock();

		if (i != NumHeights)
		{
			Warn->Log(*NSLOCTEXT("Core", "SyntaxError", "Syntax Error").ToString());
		}
	}
	else if (FParse::Command(&SourceText, TEXT("DominantLayerData")))
	{
		int32 NumDominantLayerSamples = FMath::Square(CollisionSizeQuads + 1);

		DominantLayerData.Lock(LOCK_READ_WRITE);
		uint8* DominantLayerSamples = (uint8*)DominantLayerData.Realloc(NumDominantLayerSamples);
		FMemory::Memzero(DominantLayerSamples, NumDominantLayerSamples);

		FParse::Next(&SourceText);
		int32 i = 0;
		while (SourceText[0] && SourceText[1])
		{
			if (i < NumDominantLayerSamples)
			{
				DominantLayerSamples[i++] = FParse::HexDigit(SourceText[0]) * 16 + FParse::HexDigit(SourceText[1]);
			}
			SourceText += 2;
		}

		DominantLayerData.Unlock();

		if (i != NumDominantLayerSamples)
		{
			Warn->Log(*NSLOCTEXT("Core", "SyntaxError", "Syntax Error").ToString());
		}
	}
	else if (FParse::Command(&SourceText, TEXT("CollisionXYOffsetData")))
	{
		int32 NumOffsets = FMath::Square(CollisionSizeQuads + 1) * 2;

		CollisionXYOffsetData.Lock(LOCK_READ_WRITE);
		uint16* Offsets = (uint16*)CollisionXYOffsetData.Realloc(NumOffsets);
		FMemory::Memzero(Offsets, sizeof(uint16)*NumOffsets);

		FParse::Next(&SourceText);
		int32 i = 0;
		while (FChar::IsDigit(*SourceText))
		{
			if (i < NumOffsets)
			{
				Offsets[i++] = FCString::Atoi(SourceText);
				while (FChar::IsDigit(*SourceText))
				{
					SourceText++;
				}
			}

			FParse::Next(&SourceText);
		}

		CollisionXYOffsetData.Unlock();

		if (i != NumOffsets)
		{
			Warn->Log(*NSLOCTEXT("Core", "SyntaxError", "Syntax Error").ToString());
		}
	}
}

#endif // WITH_EDITOR

ULandscapeInfo* ULandscapeHeightfieldCollisionComponent::GetLandscapeInfo() const
{
	return GetLandscapeProxy()->GetLandscapeInfo();
}

ALandscapeProxy* ULandscapeHeightfieldCollisionComponent::GetLandscapeProxy() const
{
	return CastChecked<ALandscapeProxy>(GetOuter());
}

FIntPoint ULandscapeHeightfieldCollisionComponent::GetSectionBase() const
{
	return FIntPoint(SectionBaseX, SectionBaseY);
}

void ULandscapeHeightfieldCollisionComponent::SetSectionBase(FIntPoint InSectionBase)
{
	SectionBaseX = InSectionBase.X;
	SectionBaseY = InSectionBase.Y;
}

ULandscapeHeightfieldCollisionComponent::ULandscapeHeightfieldCollisionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bUseAsOccluder = true;
	bAllowCullDistanceVolume = false;
	Mobility = EComponentMobility::Static;
	bCanEverAffectNavigation = true;
	bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::Yes;

	HeightfieldRowsCount = -1;
	HeightfieldColumnsCount = -1;

	// landscape collision components should be deterministically created and therefor are addressable over the network
	SetNetAddressable();
}

ULandscapeHeightfieldCollisionComponent::ULandscapeHeightfieldCollisionComponent(FVTableHelper& Helper)
	: Super(Helper)
{

}

ULandscapeHeightfieldCollisionComponent::~ULandscapeHeightfieldCollisionComponent() = default;

ULandscapeComponent* ULandscapeHeightfieldCollisionComponent::GetRenderComponent() const
{
	return RenderComponent.Get();
}

TOptional<float> ULandscapeHeightfieldCollisionComponent::GetHeight(float X, float Y)
{
	TOptional<float> Height;
	const float ZScale = GetComponentTransform().GetScale3D().Z * LANDSCAPE_ZSCALE;
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
	if (IsValidRef(HeightfieldRef) && HeightfieldRef->RBHeightfield != nullptr)
	{
		Height = HeightfieldRef->RBHeightfield->getHeight(HeightfieldRef->RBHeightfield->getNbRows() - 1 - X, Y) * ZScale;
	}
#elif WITH_CHAOS
	if (IsValidRef(HeightfieldRef) && HeightfieldRef->Heightfield.Get())
	{
		Height = HeightfieldRef->Heightfield->GetHeightAt({ X, Y });
	}
#endif
	return Height;
}

struct FHeightFieldAccessor
{
	FHeightFieldAccessor(const ULandscapeHeightfieldCollisionComponent::FHeightfieldGeometryRef& InGeometryRef)
	: GeometryRef(InGeometryRef)
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
	, NumX(InGeometryRef.RBHeightfield ? InGeometryRef.RBHeightfield->getNbColumns() : 0)
	, NumY(InGeometryRef.RBHeightfield ? InGeometryRef.RBHeightfield->getNbRows() : 0)
#elif WITH_CHAOS
	, NumX(InGeometryRef.Heightfield.IsValid() ? InGeometryRef.Heightfield->GetNumCols() : 0)
	, NumY(InGeometryRef.Heightfield.IsValid() ? InGeometryRef.Heightfield->GetNumRows() : 0)
#endif
	{
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		const int32 CellCount = NumX * NumY;
		if (CellCount > 0)
		{
			HFSamples.SetNumUninitialized(CellCount);
			GeometryRef.RBHeightfield->saveCells(HFSamples.GetData(), CellCount * HFSamples.GetTypeSize());
		}
#endif
	}

	float GetUnscaledHeight(int32 X, int32 Y) const
	{
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		return float(HFSamples[NumX * (NumY - 1 - X) + Y].height);
#elif WITH_CHAOS
		return GeometryRef.Heightfield->GetHeight(X, Y);
#else
		return 0.0f;
#endif
	}

	uint8 GetMaterialIndex(int32 X, int32 Y) const
	{
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		// we'll just use the sample from the first triangle
		return HFSamples[NumX * (NumY - 1 - X) + Y].materialIndex0;
#elif WITH_CHAOS
		return GeometryRef.Heightfield->GetMaterialIndex(X, Y);
#else
		return 0;
#endif
	}

	const ULandscapeHeightfieldCollisionComponent::FHeightfieldGeometryRef& GeometryRef;
	const int32 NumX = 0;
	const int32 NumY = 0;

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
private:
	TArray<PxHeightFieldSample> HFSamples;
#endif
};

bool ULandscapeHeightfieldCollisionComponent::FillHeightTile(TArrayView<float> Heights, int32 Offset, int32 Stride) const
{
	if (!IsValidRef(HeightfieldRef))
	{
		return false;
	}

	FHeightFieldAccessor Accessor(*HeightfieldRef.GetReference());

	const int32 LastTiledIndex = Offset + FMath::Max(0, Accessor.NumX - 1) + Stride * FMath::Max(0, Accessor.NumY - 1);
	if (!Heights.IsValidIndex(LastTiledIndex))
	{
		return false;
	}

	const FTransform& WorldTransform = GetComponentToWorld();
	const float ZScale = WorldTransform.GetScale3D().Z * LANDSCAPE_ZSCALE;

	// Write all values to output array
	for (int32 y = 0; y < Accessor.NumY; ++y)
	{
		for (int32 x = 0; x < Accessor.NumX; ++x)
		{
			const float CurrHeight = Accessor.GetUnscaledHeight(x, y);
			const float WorldHeight = WorldTransform.TransformPositionNoScale(FVector(0, 0, CurrHeight * ZScale)).Z;

			// write output
			const int32 WriteIndex = Offset + y * Stride + x;
			Heights[WriteIndex] = WorldHeight;
		}
	}

	return true;
}

bool ULandscapeHeightfieldCollisionComponent::FillMaterialIndexTile(TArrayView<uint8> Materials, int32 Offset, int32 Stride) const
{
	if (!IsValidRef(HeightfieldRef))
	{
		return false;
	}

	FHeightFieldAccessor Accessor(*HeightfieldRef.GetReference());

	const int32 LastTiledIndex = Offset + FMath::Max(0, Accessor.NumX - 1) + Stride * FMath::Max(0, Accessor.NumY - 1);
	if (!Materials.IsValidIndex(LastTiledIndex))
	{
		return false;
	}

	// Write all values to output array
	for (int32 y = 0; y < Accessor.NumY; ++y)
	{
		for (int32 x = 0; x < Accessor.NumX; ++x)
		{
			// write output
			const int32 WriteIndex = Offset + y * Stride + x;
			Materials[WriteIndex] = Accessor.GetMaterialIndex(x, y);
		}
	}

	return true;
}

TOptional<float> ALandscapeProxy::GetHeightAtLocation(FVector Location) const
{
	TOptional<float> Height;
	if (ULandscapeInfo* Info = GetLandscapeInfo())
	{
		const FVector ActorSpaceLocation = LandscapeActorToWorld().InverseTransformPosition(Location);
		const FIntPoint Key = FIntPoint(FMath::FloorToInt(ActorSpaceLocation.X / ComponentSizeQuads), FMath::FloorToInt(ActorSpaceLocation.Y / ComponentSizeQuads));
		ULandscapeHeightfieldCollisionComponent* Component = Info->XYtoCollisionComponentMap.FindRef(Key);
		if (Component)
		{
			const FVector ComponentSpaceLocation = Component->GetComponentToWorld().InverseTransformPosition(Location);
			const TOptional<float> LocalHeight = Component->GetHeight(ComponentSpaceLocation.X, ComponentSpaceLocation.Y);
			if (LocalHeight.IsSet())
			{
				Height = Component->GetComponentToWorld().TransformPositionNoScale(FVector(0, 0, LocalHeight.GetValue())).Z;
			}
		}
	}
	return Height;
}

void ALandscapeProxy::GetHeightValues(int32& SizeX, int32& SizeY, TArray<float> &ArrayValues) const
{			
	SizeX = 0;
	SizeY = 0;
	ArrayValues.SetNum(0);
	
#if WITH_CHAOS
	// Exit if we have no landscape data
	if (LandscapeComponents.Num() == 0 || CollisionComponents.Num() == 0)
	{
		return;
	}

	// find index coordinate range for landscape
	int32 MinX = MAX_int32;
	int32 MinY = MAX_int32;
	int32 MaxX = -MAX_int32;
	int32 MaxY = -MAX_int32;

	for (ULandscapeComponent* LandscapeComponent : LandscapeComponents)
	{
		// expecting a valid pointer to a landscape component
		if (!LandscapeComponent)
		{
			return;
		}

		// #todo(dmp): should we be using ULandscapeHeightfieldCollisionComponent.CollisionSizeQuads (or HeightFieldData->GetNumCols)
		MinX = FMath::Min(LandscapeComponent->SectionBaseX, MinX);
		MinY = FMath::Min(LandscapeComponent->SectionBaseY, MinY);
		MaxX = FMath::Max(LandscapeComponent->SectionBaseX + LandscapeComponent->ComponentSizeQuads, MaxX);
		MaxY = FMath::Max(LandscapeComponent->SectionBaseY + LandscapeComponent->ComponentSizeQuads, MaxY);		
	}

	if (MinX == MAX_int32)
	{
		return;
	}			
		
	SizeX = (MaxX - MinX + 1);
	SizeY = (MaxY - MinY + 1);
	ArrayValues.SetNumUninitialized(SizeX * SizeY);
	
	for (ULandscapeHeightfieldCollisionComponent *CollisionComponent : CollisionComponents)
	{
		// Make sure we have a valid collision component and a heightfield
		if (!CollisionComponent || !IsValidRef(CollisionComponent->HeightfieldRef))
		{
			SizeX = 0;
			SizeY = 0;
			ArrayValues.SetNum(0);
			return;
		}

		TUniquePtr<Chaos::FHeightField> &HeightFieldData = CollisionComponent->HeightfieldRef->Heightfield;

		// If we are expecting height data, but it isn't there, clear the return array, and exit
		if (!HeightFieldData.IsValid())
		{
			SizeX = 0;
			SizeY = 0;
			ArrayValues.SetNum(0);
			return;
		}

		const int32 BaseX = CollisionComponent->SectionBaseX - MinX;
		const int32 BaseY = CollisionComponent->SectionBaseY - MinY;

		const int32 NumX = HeightFieldData->GetNumCols();
		const int32 NumY = HeightFieldData->GetNumRows();

		const FTransform& ComponentToWorld = CollisionComponent->GetComponentToWorld();
		const float ZScale = ComponentToWorld.GetScale3D().Z * LANDSCAPE_ZSCALE;

		// Write all values to output array
		for (int32 x = 0; x < NumX; ++x)
		{
			for (int32 y = 0; y < NumY; ++y)
			{
				const float CurrHeight = HeightFieldData->GetHeight(x, y) * ZScale;				
				const float WorldHeight = ComponentToWorld.TransformPositionNoScale(FVector(0, 0, CurrHeight)).Z;
				
				// write output
				const int32 WriteX = BaseX + x;
				const int32 WriteY = BaseY + y;
				const int32 Idx = WriteY * SizeX + WriteX;
				ArrayValues[Idx] = WorldHeight;
			}
		}
	}
#endif
}