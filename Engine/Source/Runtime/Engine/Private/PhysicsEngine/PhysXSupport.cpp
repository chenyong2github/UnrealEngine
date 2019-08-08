// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PhysXSupport.cpp: PhysX
=============================================================================*/

#include "PhysicsEngine/PhysXSupport.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/RigidBodyIndexPair.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "CustomPhysXPayload.h"

#if WITH_PHYSX

#include "PhysXPublic.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsPublicCore.h"

int32						GNumPhysXConvexMeshes = 0;

TArray<PxConvexMesh*>	GPhysXPendingKillConvex;
TArray<PxTriangleMesh*>	GPhysXPendingKillTriMesh;
TArray<PxHeightField*>	GPhysXPendingKillHeightfield;
TArray<PxMaterial*>		GPhysXPendingKillMaterial;
///////////////////// Utils /////////////////////


void AddRadialImpulseToPxRigidBody_AssumesLocked(PxRigidBody& PRigidBody, const FVector& Origin, float Radius, float Strength, uint8 Falloff, bool bVelChange)
{
#if WITH_PHYSX
	if (!(PRigidBody.getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
	{
		float Mass = PRigidBody.getMass();
		PxTransform PCOMTransform = PRigidBody.getGlobalPose().transform(PRigidBody.getCMassLocalPose());
		PxVec3 PCOMPos = PCOMTransform.p; // center of mass in world space
		PxVec3 POrigin = U2PVector(Origin); // origin of radial impulse, in world space
		PxVec3 PDelta = PCOMPos - POrigin; // vector from origin to COM

		float Mag = PDelta.magnitude(); // Distance from COM to origin, in Unreal scale : @todo: do we still need conversion scale?

		// If COM is outside radius, do nothing.
		if (Mag > Radius)
		{
			return;
		}

		PDelta.normalize();

		// Scale by U2PScale here, because units are velocity * mass. 
		float ImpulseMag = Strength;
		if (Falloff == RIF_Linear)
		{
			ImpulseMag *= (1.0f - (Mag / Radius));
		}

		PxVec3 PImpulse = PDelta * ImpulseMag;

		PxForceMode::Enum Mode = bVelChange ? PxForceMode::eVELOCITY_CHANGE : PxForceMode::eIMPULSE;
		PRigidBody.addForce(PImpulse, Mode);
	}
#endif // WITH_PHYSX
}

void AddRadialForceToPxRigidBody_AssumesLocked(PxRigidBody& PRigidBody, const FVector& Origin, float Radius, float Strength, uint8 Falloff, bool bAccelChange)
{
#if WITH_PHYSX
	if (!(PRigidBody.getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
	{
		float Mass = PRigidBody.getMass();
		PxTransform PCOMTransform = PRigidBody.getGlobalPose().transform(PRigidBody.getCMassLocalPose());
		PxVec3 PCOMPos = PCOMTransform.p; // center of mass in world space
		PxVec3 POrigin = U2PVector(Origin); // origin of radial impulse, in world space
		PxVec3 PDelta = PCOMPos - POrigin; // vector from

		float Mag = PDelta.magnitude(); // Distance from COM to origin, in Unreal scale : @todo: do we still need conversion scale?

		// If COM is outside radius, do nothing.
		if (Mag > Radius)
		{
			return;
		}

		PDelta.normalize();

		// If using linear falloff, scale with distance.
		float ForceMag = Strength;
		if (Falloff == RIF_Linear)
		{
			ForceMag *= (1.0f - (Mag / Radius));
		}

		// Apply force
		PxVec3 PImpulse = PDelta * ForceMag;
		PRigidBody.addForce(PImpulse, bAccelChange ? PxForceMode::eACCELERATION : PxForceMode::eFORCE);
	}
#endif // WITH_PHYSX
}

bool IsRigidBodyKinematicAndInSimulationScene_AssumesLocked(const PxRigidBody* PRigidBody)
{
	if (PRigidBody)
	{
		//For some cases we only consider an actor kinematic if it's in the simulation scene. This is in cases where we set a kinematic target
		return (PRigidBody->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC) && !(PRigidBody->getActorFlags() & PxActorFlag::eDISABLE_SIMULATION);
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// PHYSXSIMFILTERSHADER 

/** Util to return a string for the type of a query (for debugging) */
FString ObjTypeToString(PxFilterObjectAttributes PAtt)
{
	PxFilterObjectType::Enum Type = PxGetFilterObjectType(PAtt);

	if(Type == PxFilterObjectType::eRIGID_STATIC)
	{
		return TEXT("rigid static");
	}
	else if(Type == PxFilterObjectType::eRIGID_DYNAMIC)
	{
		return TEXT("rigid dynamic");
	}

	return TEXT("unknown");
}

PxFilterFlags PhysXSimFilterShader(	PxFilterObjectAttributes attributes0, PxFilterData filterData0, 
									PxFilterObjectAttributes attributes1, PxFilterData filterData1,
									PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize )
{
	//UE_LOG(LogPhysics, Log, TEXT("filterData0 (%s): %x %x %x %x"), *ObjTypeToString(attributes0), filterData0.word0, filterData0.word1, filterData0.word2, filterData0.word3);
	//UE_LOG(LogPhysics, Log, TEXT("filterData1 (%s): %x %x %x %x"), *ObjTypeToString(attributes1), filterData1.word0, filterData1.word1, filterData1.word2, filterData1.word3);

	bool k0 = PxFilterObjectIsKinematic(attributes0);
	bool k1 = PxFilterObjectIsKinematic(attributes1);

	PxU32 FilterFlags0 = (filterData0.word3 & 0xFFFFFF);
	PxU32 FilterFlags1 = (filterData1.word3 & 0xFFFFFF);

	if (k0 && k1)
	{
		//Ignore kinematic kinematic pairs unless they are explicitly requested
		if(!(FilterFlags0&EPDF_KinematicKinematicPairs) && !(FilterFlags1&EPDF_KinematicKinematicPairs))
		{
			return PxFilterFlag::eSUPPRESS;	//NOTE: Waiting on physx fix for refiltering on aggregates. For now use supress which automatically tests when changes to simulation happen
		}
	}
	
	bool s0 = PxGetFilterObjectType(attributes0) == PxFilterObjectType::eRIGID_STATIC;
	bool s1 = PxGetFilterObjectType(attributes1) == PxFilterObjectType::eRIGID_STATIC;

	//ignore static-kinematic (this assumes that statics can't be flagged as kinematics)
	// should return eSUPPRESS here instead eKILL so that kinematics vs statics will still be considered once kinematics become dynamic (dying ragdoll case)
	if((k0 || k1) && (s0 || s1))
	{
		return PxFilterFlag::eSUPPRESS;
	}
	
	// if these bodies are from the same component, use the disable table to see if we should disable collision. This case should only happen for things like skeletalmesh and destruction. The table is only created for skeletal mesh components at the moment
#if !WITH_CHAOS
	if(filterData0.word2 == filterData1.word2)
	{
		check(constantBlockSize == sizeof(FPhysSceneShaderInfo));
		const FPhysSceneShaderInfo* PhysSceneShaderInfo = (const FPhysSceneShaderInfo*) constantBlock;
		check(PhysSceneShaderInfo);
		FPhysScene * PhysScene = PhysSceneShaderInfo->PhysScene;
		check(PhysScene);

		const TMap<uint32, TMap<FRigidBodyIndexPair, bool> *> & CollisionDisableTableLookup = PhysScene->GetCollisionDisableTableLookup();
		TMap<FRigidBodyIndexPair, bool>* const * DisableTablePtrPtr = CollisionDisableTableLookup.Find(filterData1.word2);
		if (DisableTablePtrPtr)		//Since collision table is deferred during sub-stepping it's possible that we won't get the collision disable table until the next frame
		{
			TMap<FRigidBodyIndexPair, bool>* DisableTablePtr = *DisableTablePtrPtr;
			FRigidBodyIndexPair BodyPair(filterData0.word0, filterData1.word0); // body indexes are stored in word 0
			if (DisableTablePtr->Find(BodyPair))
			{
				return PxFilterFlag::eKILL;
			}

		}
	}
#endif

	// Find out which channels the objects are in
	ECollisionChannel Channel0 = GetCollisionChannel(filterData0.word3);
	ECollisionChannel Channel1 = GetCollisionChannel(filterData1.word3);
	
	// see if 0/1 would like to block the other 
	PxU32 BlockFlagTo1 = (ECC_TO_BITFIELD(Channel1) & filterData0.word1);
	PxU32 BlockFlagTo0 = (ECC_TO_BITFIELD(Channel0) & filterData1.word1);

	bool bDoesWantToBlock = (BlockFlagTo1 && BlockFlagTo0);

	// if don't want to block, suppress
	if ( !bDoesWantToBlock )
	{
		return PxFilterFlag::eSUPPRESS;
	}

	

	pairFlags = PxPairFlag::eCONTACT_DEFAULT;

	//todo enabling CCD objects against everything else for now
	if(!(k0 && k1) && ((FilterFlags0&EPDF_CCD) || (FilterFlags1&EPDF_CCD)))
	{
		pairFlags |= PxPairFlag::eDETECT_CCD_CONTACT | PxPairFlag::eSOLVE_CONTACT;
	}


	if((FilterFlags0&EPDF_ContactNotify) || (FilterFlags1&EPDF_ContactNotify))
	{
		pairFlags |= (PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_PERSISTS | PxPairFlag::eNOTIFY_CONTACT_POINTS );
	}


	if ((FilterFlags0&EPDF_ModifyContacts) || (FilterFlags1&EPDF_ModifyContacts))
	{
		pairFlags |= (PxPairFlag::eMODIFY_CONTACTS);
	}

	return PxFilterFlags();
}

#if !WITH_CHAOS

/** Figures out the new FCollisionNotifyInfo needed for pending notification. It adds it, and then returns an array that goes from pair index to notify collision index */
TArray<int32> AddCollisionNotifyInfo(const FBodyInstance* Body0, const FBodyInstance* Body1, const physx::PxContactPair * Pairs, uint32 NumPairs, TArray<FCollisionNotifyInfo> & PendingNotifyInfos)
{
	TArray<int32> PairNotifyMapping;
	PairNotifyMapping.Empty(NumPairs);

	TMap<const FBodyInstance*, TMap<const FBodyInstance*, int32> > BodyPairNotifyMap;
	for(uint32 PairIdx = 0; PairIdx < NumPairs; ++PairIdx)
	{
		const PxContactPair* Pair = Pairs + PairIdx;
		PairNotifyMapping.Add(-1);	//start as -1 because we can have collisions that we don't want to actually record collision

									// Check if either shape has been removed
		if(!Pair->events.isSet(PxPairFlag::eNOTIFY_TOUCH_LOST) &&
			!Pair->events.isSet(PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST) &&
			!Pair->flags.isSet(PxContactPairFlag::eREMOVED_SHAPE_0) &&
			!Pair->flags.isSet(PxContactPairFlag::eREMOVED_SHAPE_1))
		{
			// Get the two shapes that are involved in the collision
			const PxShape* Shape0 = Pair->shapes[0];
			check(Shape0);
			const PxShape* Shape1 = Pair->shapes[1];
			check(Shape1);

			PxU32 FilterFlags0 = Shape0->getSimulationFilterData().word3 & 0xFFFFFF;
			PxU32 FilterFlags1 = Shape1->getSimulationFilterData().word3 & 0xFFFFFF;

			const bool bBody0Notify = (FilterFlags0 & EPDF_ContactNotify) != 0;
			const bool bBody1Notify = (FilterFlags1 & EPDF_ContactNotify) != 0;

			if(bBody0Notify || bBody1Notify)
			{
#if WITH_IMMEDIATE_PHYSX
                check(false);
#else
				const FBodyInstance* SubBody0 = FPhysicsInterface_PhysX::ShapeToOriginalBodyInstance(Body0, Shape0);
				const FBodyInstance* SubBody1 = FPhysicsInterface_PhysX::ShapeToOriginalBodyInstance(Body1, Shape1);

				TMap<const FBodyInstance *, int32> & SubBodyNotifyMap = BodyPairNotifyMap.FindOrAdd(SubBody0);
				int32* NotifyInfoIndex = SubBodyNotifyMap.Find(SubBody1);

				if(NotifyInfoIndex == NULL)
				{
					FCollisionNotifyInfo * NotifyInfo = new (PendingNotifyInfos) FCollisionNotifyInfo;
					NotifyInfo->bCallEvent0 = bBody0Notify;
					NotifyInfo->Info0.SetFrom(SubBody0);
					NotifyInfo->bCallEvent1 = bBody1Notify;
					NotifyInfo->Info1.SetFrom(SubBody1);

					NotifyInfoIndex = &SubBodyNotifyMap.Add(SubBody0, PendingNotifyInfos.Num() - 1);
				}

				PairNotifyMapping[PairIdx] = *NotifyInfoIndex;
#endif
			}
		}
	}

	return PairNotifyMapping;
}

///////// FPhysXSimEventCallback //////////////////////////////////

void FPhysXSimEventCallback::onContact(const PxContactPairHeader& PairHeader, const PxContactPair* Pairs, PxU32 NumPairs)
{
	// Check actors are not destroyed
	if( PairHeader.flags & (PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | PxContactPairHeaderFlag::eREMOVED_ACTOR_1) )
	{
		UE_LOG(LogPhysics, Log, TEXT("%llu onContact(): Actors have been deleted!"), (uint64)GFrameCounter );
		return;
	}

	const PxActor* PActor0 = PairHeader.actors[0];
	const PxActor* PActor1 = PairHeader.actors[1];
	check(PActor0 && PActor1);

	const PxRigidBody* PRigidBody0 = PActor0->is<PxRigidBody>();
	const PxRigidBody* PRigidBody1 = PActor1->is<PxRigidBody>();

	const FBodyInstance* BodyInst0 = FPhysxUserData::Get<FBodyInstance>(PActor0->userData);
	const FBodyInstance* BodyInst1 = FPhysxUserData::Get<FBodyInstance>(PActor1->userData);
	
	bool bEitherCustomPayload = false;

	// check if it is a custom payload with special body instance conversion
	if (BodyInst0 == nullptr)
	{
		if (const FCustomPhysXPayload* CustomPayload = FPhysxUserData::Get<FCustomPhysXPayload>(PActor0->userData))
		{
			bEitherCustomPayload = true;
			BodyInst0 = CustomPayload->GetBodyInstance();
		}
	}

	if (BodyInst1 == nullptr)
	{
		if (const FCustomPhysXPayload* CustomPayload = FPhysxUserData::Get<FCustomPhysXPayload>(PActor1->userData))
		{
			bEitherCustomPayload = true;
			BodyInst1 = CustomPayload->GetBodyInstance();
		}
	}

	//if nothing valid just exit
	//if a custom payload (like apex destruction) generates collision between the same body instance we ignore it. This is potentially bad, but in general we have not had a need for this
	if(BodyInst0 == nullptr || BodyInst1 == nullptr || BodyInst0 == BodyInst1)
	{
		return;
	}

	//custom payloads may (hackily) rely on the onContact flag. Apex Destruction needs this for being able to apply damage as a result of collision.
	//Because of this we only want onContact events to happen if the user actually selected bNotifyRigidBodyCollision so we have to check if this is the case
	if (bEitherCustomPayload)
	{
		if (BodyInst0->bNotifyRigidBodyCollision == false && BodyInst1->bNotifyRigidBodyCollision == false)
		{
			return;
		}
	}

	TArray<FCollisionNotifyInfo>& PendingCollisionNotifies = OwningScene->GetPendingCollisionNotifies();

	uint32 PreAddingCollisionNotify = PendingCollisionNotifies.Num() - 1;
	TArray<int32> PairNotifyMapping = AddCollisionNotifyInfo(BodyInst0, BodyInst1, Pairs, NumPairs, PendingCollisionNotifies);

	// Iterate through contact points
	for(uint32 PairIdx=0; PairIdx<NumPairs; PairIdx++)
	{
		int32 NotifyIdx = PairNotifyMapping[PairIdx];
		if (NotifyIdx == -1)	//the body instance this pair belongs to is not listening for events
		{
			continue;
		}

		FCollisionNotifyInfo * NotifyInfo = &PendingCollisionNotifies[NotifyIdx];
		FCollisionImpactData* ImpactInfo = &(NotifyInfo->RigidCollisionData);

		const PxContactPair* Pair = Pairs + PairIdx;

		// Get the two shapes that are involved in the collision
		const PxShape* Shape0 = Pair->shapes[0];
		check(Shape0);
		const PxShape* Shape1 = Pair->shapes[1];
		check(Shape1);

		// Get materials
		PxMaterial* Material0 = nullptr;
		UPhysicalMaterial* PhysMat0  = nullptr;
		if(Shape0->getNbMaterials() == 1)	//If we have simple geometry or only 1 material we set it here. Otherwise do it per face
		{
			Shape0->getMaterials(&Material0, 1);		
			PhysMat0 = Material0 ? FPhysxUserData::Get<UPhysicalMaterial>(Material0->userData) : nullptr;
		}

		PxMaterial* Material1 = nullptr;
		UPhysicalMaterial* PhysMat1  = nullptr;
		if (Shape1->getNbMaterials() == 1)	//If we have simple geometry or only 1 material we set it here. Otherwise do it per face
		{
			Shape1->getMaterials(&Material1, 1);
			PhysMat1 = Material1 ? FPhysxUserData::Get<UPhysicalMaterial>(Material1->userData) : nullptr;
		}

		// Iterate over contact points
		PxContactPairPoint ContactPointBuffer[16];
		int32 NumContactPoints = Pair->extractContacts(ContactPointBuffer, 16);
		for(int32 PointIdx=0; PointIdx<NumContactPoints; PointIdx++)
		{
			const PxContactPairPoint& Point = ContactPointBuffer[PointIdx];

			const PxVec3 NormalImpulse = Point.impulse.dot(Point.normal) * Point.normal; // project impulse along normal
			ImpactInfo->TotalNormalImpulse += P2UVector(NormalImpulse);
			ImpactInfo->TotalFrictionImpulse += P2UVector(Point.impulse - NormalImpulse); // friction is component not along contact normal

			// Get per face materials
			if(!Material0)	//there is complex geometry or multiple materials so resolve the physical material here
			{
				if(PxMaterial* Material0PerFace = Shape0->getMaterialFromInternalFaceIndex(Point.internalFaceIndex0))
				{
					PhysMat0 = FPhysxUserData::Get<UPhysicalMaterial>(Material0PerFace->userData);
				}
			}

			if (!Material1)	//there is complex geometry or multiple materials so resolve the physical material here
			{
				if(PxMaterial* Material1PerFace = Shape1->getMaterialFromInternalFaceIndex(Point.internalFaceIndex1))
				{
					PhysMat1 = FPhysxUserData::Get<UPhysicalMaterial>(Material1PerFace->userData);
				}
				
			}
			
			new(ImpactInfo->ContactInfos) FRigidBodyContactInfo(
				P2UVector(Point.position), 
				P2UVector(Point.normal), 
				-1.f * Point.separation, 
				PhysMat0, 
				PhysMat1);
		}	
	}

	for (int32 NotifyIdx = PreAddingCollisionNotify + 1; NotifyIdx < PendingCollisionNotifies.Num(); NotifyIdx++)
	{
		FCollisionNotifyInfo * NotifyInfo = &PendingCollisionNotifies[NotifyIdx];
		FCollisionImpactData* ImpactInfo = &(NotifyInfo->RigidCollisionData);
		// Discard pairs that don't generate any force (eg. have been rejected through a modify contact callback).
		if (ImpactInfo->TotalNormalImpulse.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			PendingCollisionNotifies.RemoveAt(NotifyIdx);
			NotifyIdx--;
		}
	}
}

void FPhysXSimEventCallback::onConstraintBreak( PxConstraintInfo* constraints, PxU32 count )
{
	for (int32 i=0; i < (int32)count; ++i)
	{
		PxJoint* Joint = (PxJoint*)(constraints[i].externalReference);

		if (Joint && Joint->userData)
		{
			if (FConstraintInstance* Constraint = FPhysxUserData::Get<FConstraintInstance>(Joint->userData))
			{
				OwningScene->AddPendingOnConstraintBreak(Constraint);
			}
		}
	}
}

void FPhysXSimEventCallback::onWake(PxActor** Actors, PxU32 Count)
{
	for(PxU32 ActorIdx = 0; ActorIdx < Count; ++ActorIdx)
	{
		if (FBodyInstance* BodyInstance = FPhysxUserData::Get<FBodyInstance>(Actors[ActorIdx]->userData))
		{
			OwningScene->AddPendingSleepingEvent(BodyInstance, ESleepEvent::SET_Wakeup);
		}
	}
}

void FPhysXSimEventCallback::onSleep(PxActor** Actors, PxU32 Count)
{
	for (PxU32 ActorIdx = 0; ActorIdx < Count; ++ActorIdx)
	{
		if (FBodyInstance* BodyInstance = FPhysxUserData::Get<FBodyInstance>(Actors[ActorIdx]->userData))
		{
			OwningScene->AddPendingSleepingEvent(BodyInstance, ESleepEvent::SET_Sleep);
		}
	}
}
#endif

//////////////////////////////////////////////////////////////////////////
// FPhysXCookingDataReader

FPhysXCookingDataReader::FPhysXCookingDataReader( FByteBulkData& InBulkData, FBodySetupUVInfo* UVInfo )
{
	// Read cooked physics data
	uint8* DataPtr = (uint8*)InBulkData.Lock( LOCK_READ_ONLY );
	FBufferReader Ar( DataPtr, InBulkData.GetBulkDataSize(), false );
	
	uint8 bLittleEndian = true;
	int32 NumConvexElementsCooked = 0;
	int32 NumMirroredElementsCooked = 0;
	int32 NumTriMeshesCooked = 0;

	Ar << bLittleEndian;
	Ar.SetByteSwapping( PLATFORM_LITTLE_ENDIAN ? !bLittleEndian : !!bLittleEndian );
	Ar << NumConvexElementsCooked;	
	Ar << NumMirroredElementsCooked;
	Ar << NumTriMeshesCooked;
	
	ConvexMeshes.Empty(NumConvexElementsCooked);
	for( int32 ElementIndex = 0; ElementIndex < NumConvexElementsCooked; ElementIndex++ )
	{
		PxConvexMesh* ConvexMesh = ReadConvexMesh( Ar, DataPtr, InBulkData.GetBulkDataSize() );
		ConvexMeshes.Add( ConvexMesh );
	}

	ConvexMeshesNegX.Empty(NumMirroredElementsCooked);
	for( int32 ElementIndex = 0; ElementIndex < NumMirroredElementsCooked; ElementIndex++ )
	{
		PxConvexMesh* ConvexMeshNegX = ReadConvexMesh( Ar, DataPtr, InBulkData.GetBulkDataSize() );
		ConvexMeshesNegX.Add( ConvexMeshNegX );
	}

	TriMeshes.Empty(NumTriMeshesCooked);
	for(int32 ElementIndex = 0; ElementIndex < NumTriMeshesCooked; ++ElementIndex)
	{
		PxTriangleMesh* TriMesh = ReadTriMesh( Ar, DataPtr, InBulkData.GetBulkDataSize() );
		TriMeshes.Add(TriMesh);
	}

	// Init UVInfo pointer
	check(UVInfo);
	Ar << *UVInfo;

	InBulkData.Unlock();
}

PxConvexMesh* FPhysXCookingDataReader::ReadConvexMesh( FBufferReader& Ar, uint8* InBulkDataPtr, int32 InBulkDataSize )
{
	LLM_SCOPE(ELLMTag::PhysX);

	PxConvexMesh* CookedMesh = NULL;
	uint8 IsMeshCooked = false;
	Ar << IsMeshCooked;
	if( IsMeshCooked )
	{
		FPhysXInputStream Buffer( InBulkDataPtr + Ar.Tell(), InBulkDataSize - Ar.Tell() );		
		CookedMesh = GPhysXSDK->createConvexMesh( Buffer );
		check( CookedMesh != NULL );
		Ar.Seek( Ar.Tell() + Buffer.ReadPos );
	}
	return CookedMesh;
}

PxTriangleMesh* FPhysXCookingDataReader::ReadTriMesh( FBufferReader& Ar, uint8* InBulkDataPtr, int32 InBulkDataSize )
{
	LLM_SCOPE(ELLMTag::PhysX);

	FPhysXInputStream Buffer( InBulkDataPtr + Ar.Tell(), InBulkDataSize - Ar.Tell() );
	PxTriangleMesh* CookedMesh = GPhysXSDK->createTriangleMesh(Buffer);
	check(CookedMesh);
	Ar.Seek( Ar.Tell() + Buffer.ReadPos );
	return CookedMesh;
}


void AddToCollection(PxCollection* PCollection, PxBase* PBase)
{
	if (PBase)
	{
		PCollection->add(*PBase);
	}
}

PxCollection* MakePhysXCollection(const TArray<UPhysicalMaterial*>& PhysicalMaterials, const TArray<UBodySetup*>& BodySetups, uint64 BaseId)
{
#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
    ensure(false);
    return nullptr;
#else
	QUICK_SCOPE_CYCLE_COUNTER(STAT_CreateSharedData);
	PxCollection* PCollection = PxCreateCollection();
	for (UPhysicalMaterial* PhysicalMaterial : PhysicalMaterials)
	{
		if (PhysicalMaterial)
		{
			PCollection->add(*PhysicalMaterial->GetPhysicsMaterial().Material);
		}
	}

	for (UBodySetup* BodySetup : BodySetups)
	{
		for(PxTriangleMesh* TriMesh : BodySetup->TriMeshes)
		{
			AddToCollection(PCollection, TriMesh);
		}

		for (const FKConvexElem& ConvexElem : BodySetup->AggGeom.ConvexElems)
		{
			AddToCollection(PCollection, ConvexElem.GetConvexMesh());
			AddToCollection(PCollection, ConvexElem.GetMirroredConvexMesh());
		}
	}

	PxSerialization::createSerialObjectIds(*PCollection, PxSerialObjectId(BaseId));

	return PCollection;
#endif
}

void* FPhysXProfilerCallback::zoneStart(const char* eventName, bool detached, uint64_t contextId)
{
	if(GCycleStatsShouldEmitNamedEvents > 0)
	{
		FPlatformMisc::BeginNamedEvent(FColor::Red, *FString::Printf(TEXT("PHYSX: %s"), StringCast<TCHAR>(eventName).Get()));
	}

	return nullptr;
}

void FPhysXProfilerCallback::zoneEnd(void* profilerData, const char* eventName, bool detached, uint64_t contextId)
{
	if(GCycleStatsShouldEmitNamedEvents > 0)
	{
		FPlatformMisc::EndNamedEvent();
	}
}

void FPhysXMbpBroadphaseCallback::onObjectOutOfBounds(PxShape& InShape, PxActor& InActor)
{
	FBodyInstance* ActorBodyInstance = FPhysxUserData::Get<FBodyInstance>(InActor.userData);
	if(ActorBodyInstance)
	{
		UPrimitiveComponent* OwnerComponent = ActorBodyInstance->OwnerComponent.Get();

		if(OwnerComponent)
		{
			UE_LOG(LogPhysics, Warning, TEXT("Component %s at location %s has physics bodies outside of MBP bounds. Check MBP bounds are correct for this world, collisions are disabled for bodies outside of MBP bounds when MBP is enabled."), *OwnerComponent->GetName(), *OwnerComponent->GetComponentLocation().ToString());
			return;
		}
	}

	UE_LOG(LogPhysics, Warning, TEXT("Unknown component has physics bodies outside of MBP bounds. Check MBP bounds are correct for this world, collisions are disabled for bodies outside of MBP bounds when MBP is enabled."));
}

void FPhysXMbpBroadphaseCallback::onObjectOutOfBounds(PxAggregate& InAggregate)
{
	const PxU32 NumActors = InAggregate.getNbActors();
	if(NumActors > 0)
	{
		// The following code assumes that an aggregate does not span multiple components, this code will need to be updated if this changes
		PxActor* FirstActor;

		if(InAggregate.getActors(&FirstActor, 1) > 0)
		{
			FBodyInstance* ActorBodyInstance = FPhysxUserData::Get<FBodyInstance>(FirstActor->userData);
			if(ActorBodyInstance)
			{
				UPrimitiveComponent* OwnerComponent = ActorBodyInstance->OwnerComponent.Get();

				if(OwnerComponent)
				{
					UE_LOG(LogPhysics, Warning, TEXT("Component %s at location %s has physics bodies outside of MBP bounds. Check MBP bounds are correct for this world, collisions are disabled for bodies outside of MBP bounds when MBP is enabled."), *OwnerComponent->GetName(), *OwnerComponent->GetComponentLocation().ToString());
					return;
				}
			}
		}
	}

	UE_LOG(LogPhysics, Warning, TEXT("Unknown component has physics bodies outside of MBP bounds. Check MBP bounds are correct for this world, collisions are disabled for bodies outside of MBP bounds when MBP is enabled."));
}

#endif // WITH_PHYSX
