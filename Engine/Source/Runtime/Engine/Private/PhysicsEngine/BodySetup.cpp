// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BodySetup.cpp
=============================================================================*/ 

#include "PhysicsEngine/BodySetup.h"
#include "EngineGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Animation/AnimStats.h"
#include "DerivedDataCacheInterface.h"
#include "UObject/UObjectIterator.h"
#include "UObject/PropertyPortFlags.h"
#include "Components/SplineMeshComponent.h"
#include "UObject/FortniteReleaseBranchCustomObjectVersion.h"

#include "ChaosCheck.h"
#include "Chaos/Convex.h"

#include "PhysXCookHelper.h"

#if PHYSICS_INTERFACE_PHYSX
	#include "PhysXPublic.h"
	#include "PhysicsEngine/PhysXSupport.h"
#endif // WITH_PHYSX

#include "Modules/ModuleManager.h"

#if WITH_PHYSX
	#include "IPhysXCookingModule.h"
	#include "IPhysXCooking.h"
	#include "PhysicsEngine/PhysDerivedData.h"
#endif

#include "Physics/PhysicsInterfaceUtils.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProfilingDebugging/CookStats.h"
#include "UObject/AnimPhysObjectVersion.h"

#include "Chaos/TriangleMeshImplicitObject.h"

#if WITH_CHAOS
	#include "Experimental/ChaosDerivedData.h"
	#include "Physics/Experimental/ChaosDerivedDataReader.h"
	#include "Chaos/CollisionConvexMesh.h"
	#include "Experimental/ChaosCooking.h"
#endif

/** Enable to verify that the cooked data matches the source data as we cook it */
#define VERIFY_COOKED_PHYS_DATA 0

#define LOCTEXT_NAMESPACE "PhysicsAsset"


FCookBodySetupInfo::FCookBodySetupInfo() :
#if WITH_PHYSX
	TriMeshCookFlags(EPhysXMeshCookFlags::Default) ,
	ConvexCookFlags(EPhysXMeshCookFlags::Default) ,
#endif // WITH_PHYSX
	bCookNonMirroredConvex(false),
	bCookMirroredConvex(false),
	bConvexDeformableMesh(false),
	bCookTriMesh(false),
	bSupportUVFromHitResults(false),
	bSupportFaceRemap(false),
	bTriMeshError(false)
{
}

UBodySetup::UBodySetup(FVTableHelper& Helper)
	: Super(Helper)
{
}

UBodySetup::~UBodySetup() = default;


#if ENABLE_COOK_STATS
namespace PhysXBodySetupCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("PhysX.Usage"), TEXT("BodySetup"));
	});
}
#endif

DEFINE_STAT(STAT_PhysXCooking);

#if WITH_PHYSX

bool IsRuntimeCookingEnabled()
{
#if PHYSICS_INTERFACE_PHYSX
	return FModuleManager::LoadModulePtr<IPhysXCookingModule>("RuntimePhysXCooking") != nullptr;
#else
	return false;
#endif
}
#endif //WITH_PHYSX

#if PHYSICS_INTERFACE_PHYSX
	// Quaternion that converts Sphyls from UE space to PhysX space (negate Y, swap X & Z)
	// This is equivalent to a 180 degree rotation around the normalized (1, 0, 1) axis
	const physx::PxQuat U2PSphylBasis( PI, PxVec3( 1.0f / FMath::Sqrt( 2.0f ), 0.0f, 1.0f / FMath::Sqrt( 2.0f ) ) );
	const FQuat U2PSphylBasis_UE(FVector(1.0f / FMath::Sqrt(2.0f), 0.0f, 1.0f / FMath::Sqrt(2.0f)), PI);
#endif // WITH_PHYSX

// CVars
ENGINE_API TAutoConsoleVariable<float> CVarContactOffsetFactor(
	TEXT("p.ContactOffsetFactor"),
	-1.f,
	TEXT("Multiplied by min dimension of object to calculate how close objects get before generating contacts. < 0 implies use project settings. Default: 0.01"),
	ECVF_Default);

ENGINE_API TAutoConsoleVariable<float> CVarMaxContactOffset(
	TEXT("p.MaxContactOffset"),
	-1.f,
	TEXT("Max value of contact offset, which controls how close objects get before generating contacts. < 0 implies use project settings. Default: 1.0"),
	ECVF_Default);


void FBodySetupUVInfo::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const
{
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(IndexBuffer.GetAllocatedSize());
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(VertPositions.GetAllocatedSize());

	for (int32 ChannelIdx = 0; ChannelIdx < VertUVs.Num(); ChannelIdx++)
	{
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(VertUVs[ChannelIdx].GetAllocatedSize());
	}

	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(VertUVs.GetAllocatedSize());
}

DEFINE_LOG_CATEGORY(LogPhysics);
UBodySetup::UBodySetup(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bConsiderForBounds = true;
	bMeshCollideAll = false;
	bFailedToCreatePhysicsMeshes = false;
	bHasCookedCollisionData = true;
	bNeverNeedsCookedCollisionData = false;
	bGenerateMirroredCollision = true;
	bGenerateNonMirroredCollision = true;
	bSupportUVsAndFaceRemap = false;
	DefaultInstance.SetObjectType(ECC_PhysicsBody);
#if WITH_EDITORONLY_DATA
	BuildScale_DEPRECATED = 1.0f;
#endif
	BuildScale3D = FVector(1.0f, 1.0f, 1.0f);
	SetFlags(RF_Transactional);
	bSharedCookedData = false;
	CookedFormatDataOverride = nullptr;
#if PHYSICS_INTERFACE_PHYSX
	CurrentCookHelper = nullptr;
#endif
}

void UBodySetup::CopyBodyPropertiesFrom(const UBodySetup* FromSetup)
{
	AggGeom = FromSetup->AggGeom;

	// clear pointers copied from other BodySetup, as 
	for (int32 i = 0; i < AggGeom.ConvexElems.Num(); i++)
	{
		FKConvexElem& ConvexElem = AggGeom.ConvexElems[i];
#if PHYSICS_INTERFACE_PHYSX
		ConvexElem.SetConvexMesh(nullptr);
		ConvexElem.SetMirroredConvexMesh(nullptr);
#elif WITH_CHAOS
		ConvexElem.ResetChaosConvexMesh();
#endif
	}

	DefaultInstance.CopyBodyInstancePropertiesFrom(&FromSetup->DefaultInstance);
	PhysMaterial = FromSetup->PhysMaterial;
	PhysicsType = FromSetup->PhysicsType;
	bDoubleSidedGeometry = FromSetup->bDoubleSidedGeometry;
	CollisionTraceFlag = FromSetup->CollisionTraceFlag;
}

void UBodySetup::AddCollisionFrom(const FKAggregateGeom& FromAggGeom)
{
	// Add shapes from static mesh
	AggGeom.SphereElems.Append(FromAggGeom.SphereElems);
	AggGeom.BoxElems.Append(FromAggGeom.BoxElems);
	AggGeom.SphylElems.Append(FromAggGeom.SphylElems);

	// Remember how many convex we already have
	int32 FirstNewConvexIdx = AggGeom.ConvexElems.Num();
	// copy convex
	AggGeom.ConvexElems.Append(FromAggGeom.ConvexElems);
	// clear pointers on convex elements
	for (int32 i = FirstNewConvexIdx; i < AggGeom.ConvexElems.Num(); i++)
	{
		FKConvexElem& ConvexElem = AggGeom.ConvexElems[i];
#if PHYSICS_INTERFACE_PHYSX
		ConvexElem.SetConvexMesh(nullptr);
		ConvexElem.SetMirroredConvexMesh(nullptr);
#elif WITH_CHAOS
		ConvexElem.ResetChaosConvexMesh();
#endif
	}
}

void UBodySetup::GetCookInfo(FCookBodySetupInfo& OutCookInfo, EPhysXMeshCookFlags InCookFlags) const
{
#if WITH_PHYSX

	OutCookInfo.OuterDebugName = GetOuter()->GetPathName();
	OutCookInfo.bConvexDeformableMesh = false;

	// Cook convex meshes, but only if we are not forcing complex collision to be used as simple collision as well
	if (GetCollisionTraceFlag() != CTF_UseComplexAsSimple && AggGeom.ConvexElems.Num() > 0)
	{
		OutCookInfo.bCookNonMirroredConvex = bGenerateNonMirroredCollision;
		OutCookInfo.bCookMirroredConvex = bGenerateMirroredCollision;
		for (int32 ElementIndex = 0; ElementIndex < AggGeom.ConvexElems.Num(); ElementIndex++)
		{
			const FKConvexElem& ConvexElem = AggGeom.ConvexElems[ElementIndex];
			const int32 NumVertices = ConvexElem.VertexData.Num();

			TArray<FVector>* NonMirroredConvexVertices = nullptr;
			TArray<FVector>* MirroredConvexVertices = nullptr;

			if (bGenerateNonMirroredCollision)
			{
				OutCookInfo.NonMirroredConvexVertices.AddDefaulted();
				NonMirroredConvexVertices = &OutCookInfo.NonMirroredConvexVertices.Last();
				NonMirroredConvexVertices->AddUninitialized(NumVertices);
			}

			if (bGenerateMirroredCollision)
			{
				OutCookInfo.MirroredConvexVertices.AddDefaulted();
				MirroredConvexVertices = &OutCookInfo.MirroredConvexVertices.Last();
				MirroredConvexVertices->AddUninitialized(NumVertices);
			}

			FTransform ConvexTransform = ConvexElem.GetTransform();
			if (!ConvexTransform.IsValid())
			{
				UE_LOG(LogPhysics, Warning, TEXT("UBodySetup::GetCookInfoConvex: [%s] ConvexElem[%d] has invalid transform"), *GetPathNameSafe(GetOuter()), ElementIndex);
				ConvexTransform = FTransform::Identity;
			}

			// Transform verts from element to body space, and mirror if desired
			for (int32 VertIdx = 0; VertIdx< NumVertices; VertIdx++)
			{
				FVector BodySpaceVert = ConvexTransform.TransformPosition(ConvexElem.VertexData[VertIdx]);
				if (NonMirroredConvexVertices)
				{
					(*NonMirroredConvexVertices)[VertIdx] = BodySpaceVert;
				}

				if (MirroredConvexVertices)
				{
					(*MirroredConvexVertices)[VertIdx] = BodySpaceVert * FVector(-1, 1, 1);
				}
			}

			// Get cook flags to use
			OutCookInfo.ConvexCookFlags = InCookFlags;
			OutCookInfo.bConvexDeformableMesh = GetOuter()->IsA(USplineMeshComponent::StaticClass());
			if (OutCookInfo.bConvexDeformableMesh)
			{
				OutCookInfo.ConvexCookFlags |= EPhysXMeshCookFlags::DeformableMesh;
			}
		}
	}
	else
	{
		OutCookInfo.bCookNonMirroredConvex = false;
		OutCookInfo.bCookMirroredConvex = false;
	}

	// Cook trimesh, but only if we do not force simple collision to be used as complex collision as well
	const bool bUsingAllTriData = bMeshCollideAll;
	OutCookInfo.bCookTriMesh = false;
	OutCookInfo.bTriMeshError = false;

	UObject* CDPObj = GetOuter();
	IInterface_CollisionDataProvider* CDP = Cast<IInterface_CollisionDataProvider>(CDPObj);
	
	if (GetCollisionTraceFlag() != CTF_UseSimpleAsComplex && CDP && CDP->ContainsPhysicsTriMeshData(bUsingAllTriData))
	{
		OutCookInfo.bCookTriMesh = CDP->GetPhysicsTriMeshData(&OutCookInfo.TriangleMeshDesc, bUsingAllTriData);
		const FTriMeshCollisionData& TriangleMeshDesc = OutCookInfo.TriangleMeshDesc;

		if (OutCookInfo.bCookTriMesh)
		{
			// If any of the below checks gets hit this usually means 
			// IInterface_CollisionDataProvider::ContainsPhysicsTriMeshData did not work properly.
			const int32 NumIndices = TriangleMeshDesc.Indices.Num();
			const int32 NumVerts = TriangleMeshDesc.Vertices.Num();
			if (NumIndices == 0 || NumVerts == 0 || TriangleMeshDesc.MaterialIndices.Num() > NumIndices)
			{
				UE_LOG(LogPhysics, Warning, TEXT("UBodySetup::GetCookInfo: Triangle data from '%s' invalid (%d verts, %d indices)."), *CDPObj->GetPathName(), NumVerts, NumIndices);
				OutCookInfo.bTriMeshError = true;
			}

			// Set up cooking flags
			EPhysXMeshCookFlags CookFlags = InCookFlags;

			if (TriangleMeshDesc.bDeformableMesh)
			{
				CookFlags |= EPhysXMeshCookFlags::DeformableMesh;
			}

			if (TriangleMeshDesc.bFastCook)
			{
				CookFlags |= EPhysXMeshCookFlags::FastCook;
			}

			if (TriangleMeshDesc.bDisableActiveEdgePrecompute)
			{
				CookFlags |= EPhysXMeshCookFlags::DisableActiveEdgePrecompute;
			}

			OutCookInfo.TriMeshCookFlags = CookFlags;

			OutCookInfo.bSupportFaceRemap = bSupportUVsAndFaceRemap;
		}
		else
		{
			UE_LOG(LogPhysics, Warning, TEXT("UBodySetup::GetCookInfo: ContainsPhysicsTriMeshData returned true, but GetPhysicsTriMeshData returned false. This inconsistency should be fixed for asset '%s'"), *CDPObj->GetPathName());
		}
	}

	OutCookInfo.bSupportUVFromHitResults = UPhysicsSettings::Get()->bSupportUVFromHitResults || bSupportUVsAndFaceRemap;

#endif // WITH_PHYSX
}

void FBodySetupUVInfo::FillFromTriMesh(const FTriMeshCollisionData& TriangleMeshDesc)
{
	// Store index buffer
	const int32 NumVerts = TriangleMeshDesc.Vertices.Num();
	const int32 NumTris = TriangleMeshDesc.Indices.Num();
	IndexBuffer.Empty();
	IndexBuffer.AddUninitialized(NumTris * 3);
	for (int32 TriIdx = 0; TriIdx < TriangleMeshDesc.Indices.Num(); TriIdx++)
	{
		IndexBuffer[TriIdx * 3 + 0] = TriangleMeshDesc.Indices[TriIdx].v0;
		IndexBuffer[TriIdx * 3 + 1] = TriangleMeshDesc.Indices[TriIdx].v1;
		IndexBuffer[TriIdx * 3 + 2] = TriangleMeshDesc.Indices[TriIdx].v2;
	}

	// Store vertex positions
	VertPositions.Empty();
	VertPositions.AddUninitialized(NumVerts);
	for (int32 VertIdx = 0; VertIdx < TriangleMeshDesc.Vertices.Num(); VertIdx++)
	{
		VertPositions[VertIdx] = TriangleMeshDesc.Vertices[VertIdx];
	}

	// Copy UV channels (checking they are correct size)
	for (int32 UVIndex = 0; UVIndex < TriangleMeshDesc.UVs.Num(); UVIndex++)
	{
		if (TriangleMeshDesc.UVs[UVIndex].Num() == NumVerts)
		{
			VertUVs.Add(TriangleMeshDesc.UVs[UVIndex]);
		}
		else
		{
			break;
		}
	}
}

void UBodySetup::AddCollisionFrom(class UBodySetup* FromSetup)
{
	AddCollisionFrom(FromSetup->AggGeom);
}

bool IsRuntime(const UBodySetup* BS)
{
    UObject* OwningObject = BS->GetOuter();
	UWorld* World = OwningObject ? OwningObject->GetWorld() : nullptr;
	return World && World->IsGameWorld();
}

DECLARE_CYCLE_STAT(TEXT("Create Physics Meshes"), STAT_CreatePhysicsMeshes, STATGROUP_Physics);

void UBodySetup::CreatePhysicsMeshes()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UBodySetup::CreatePhysicsMeshes);

	SCOPE_CYCLE_COUNTER(STAT_CreatePhysicsMeshes);

	// Create meshes from cooked data if not already done
	if(bCreatedPhysicsMeshes)
	{
		return;
	}

	// If we don't have any convex/trimesh data we can skip this whole function
	if (bNeverNeedsCookedCollisionData)
	{
		return;
	}
	
	bool bClearMeshes = true;

	// Find or create cooked physics data
	static FName PhysicsFormatName(FPlatformProperties::GetPhysicsFormat());

	FByteBulkData* FormatData = GetCookedData(PhysicsFormatName);

	// On dedicated servers we may be cooking generic data and sharing it
	if (FormatData == nullptr && IsRunningDedicatedServer())
	{
		FormatData = GetCookedData(FGenericPlatformProperties::GetPhysicsFormat());
	}

	if (FormatData)
	{
#if WITH_PHYSX  && PHYSICS_INTERFACE_PHYSX
		bClearMeshes = !ProcessFormatData_PhysX(FormatData);
#elif WITH_CHAOS
		bClearMeshes = !ProcessFormatData_Chaos(FormatData);
#endif
	}
	else
	{
		if (IsRuntime(this))
		{
#if WITH_PHYSX  && PHYSICS_INTERFACE_PHYSX
			bClearMeshes = !RuntimeCookPhysics_PhysX();
#elif WITH_CHAOS
			bClearMeshes = !RuntimeCookPhysics_Chaos();
#endif
		}
	}
	
	// fix up invalid transform to use identity
	// this can be here because BodySetup isn't blueprintable
	if ( GetLinkerUE4Version() < VER_UE4_FIXUP_BODYSETUP_INVALID_CONVEX_TRANSFORM )
	{
		for (int32 i=0; i<AggGeom.ConvexElems.Num(); ++i)
		{
			if ( AggGeom.ConvexElems[i].GetTransform().IsValid() == false )
			{
				AggGeom.ConvexElems[i].SetTransform(FTransform::Identity);
			}
		}
	}

#if WITH_CHAOS
	// For drawing of convex elements we require an index buffer, previously we could
	// get this from a PxConvexMesh but Chaos doesn't maintain that data. Instead now
	// it is a part of the element rather than the physics geometry, if we load in an
	// element without that data present, generate a convex hull from the convex vert
	// data and extract the index data from there.
	for(FKConvexElem& Convex : AggGeom.ConvexElems)
	{
		Convex.ComputeChaosConvexIndices();
	}
#endif


	if(bClearMeshes)
	{
		ClearPhysicsMeshes();
	}
	
	bCreatedPhysicsMeshes = true;

}

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
bool UBodySetup::RuntimeCookPhysics_PhysX()
{
	FPhysXCookHelper CookHelper(GetPhysXCookingModule());

	GetCookInfo(CookHelper.CookInfo, GetRuntimeOnlyCookOptimizationFlags());
	if(CookHelper.HasSomethingToCook(CookHelper.CookInfo))
	{
		if(!IsRuntimeCookingEnabled())
		{
			UE_LOG(LogPhysics, Error, TEXT("Attempting to build physics data for %s at runtime, but runtime cooking is disabled (see the RuntimePhysXCooking plugin)."), *GetPathName());
		}
		else
		{
			if(CookHelper.CreatePhysicsMeshes_Concurrent())
			{
				FinishCreatingPhysicsMeshes_PhysX(CookHelper.OutNonMirroredConvexMeshes, CookHelper.OutMirroredConvexMeshes, CookHelper.OutTriangleMeshes);
				bFailedToCreatePhysicsMeshes = false;
				return true;
			}
			else
			{
				bFailedToCreatePhysicsMeshes = true;
			}
		}
	}			
	return false;
}

bool UBodySetup::ProcessFormatData_PhysX(FByteBulkData* FormatData)
{
	if(FormatData->IsLocked())
	{
		// seems it's being already processed
		return false;
	}

	FPhysXCookingDataReader CookedDataReader(*FormatData, &UVInfo);

	if(GetCollisionTraceFlag() != CTF_UseComplexAsSimple)
	{
		bool bNeedsCooking = bGenerateNonMirroredCollision && CookedDataReader.ConvexMeshes.Num() != AggGeom.ConvexElems.Num();
		bNeedsCooking = bNeedsCooking || (bGenerateMirroredCollision && CookedDataReader.ConvexMeshesNegX.Num() != AggGeom.ConvexElems.Num());
		if(bNeedsCooking)	//Because of bugs it's possible to save with out of sync cooked data. In editor we want to fixup this data
		{
			InvalidatePhysicsData();
			CreatePhysicsMeshes();
			return false;
		}
	}

	FinishCreatingPhysicsMeshes_PhysX(CookedDataReader.ConvexMeshes, CookedDataReader.ConvexMeshesNegX, CookedDataReader.TriMeshes);
	return true;
}

void UBodySetup::FinishCreatingPhysicsMeshes_PhysX(const TArray<PxConvexMesh*>& ConvexMeshes, const TArray<PxConvexMesh*>& ConvexMeshesNegX, const TArray<PxTriangleMesh*>& CookedTriMeshes)
{
	ClearPhysicsMeshes();

	FPhysxSharedData::LockAccess();

	const FString FullName = GetFullName();
	if (GetCollisionTraceFlag() != CTF_UseComplexAsSimple)
	{
		ensure(!bGenerateNonMirroredCollision || ConvexMeshes.Num() == 0 || ConvexMeshes.Num() == AggGeom.ConvexElems.Num());
		ensure(!bGenerateMirroredCollision || ConvexMeshesNegX.Num() == 0 || ConvexMeshesNegX.Num() == AggGeom.ConvexElems.Num());

		//If the cooked data no longer has convex meshes, make sure to empty AggGeom.ConvexElems - otherwise we leave NULLS which cause issues, and we also read past the end of CookedDataReader.ConvexMeshes
		if ((bGenerateNonMirroredCollision && ConvexMeshes.Num() == 0) || (bGenerateMirroredCollision && ConvexMeshesNegX.Num() == 0))
		{
			AggGeom.ConvexElems.Empty();
		}

		for (int32 ElementIndex = 0; ElementIndex < AggGeom.ConvexElems.Num(); ElementIndex++)
		{
			FKConvexElem& ConvexElem = AggGeom.ConvexElems[ElementIndex];

			if (bGenerateNonMirroredCollision)
			{
				ConvexElem.SetConvexMesh(ConvexMeshes[ElementIndex]);
				FPhysxSharedData::Get().Add(ConvexElem.GetConvexMesh(), FullName);
			}

			if (bGenerateMirroredCollision)
			{
				ConvexElem.SetMirroredConvexMesh(ConvexMeshesNegX[ElementIndex]);
				FPhysxSharedData::Get().Add(ConvexElem.GetMirroredConvexMesh(), FullName);
			}
		}
	}

	for (PxTriangleMesh* TriMesh : CookedTriMeshes)
	{
		if(TriMesh)
		{
			TriMeshes.Add(TriMesh);
			FPhysxSharedData::Get().Add(TriMesh, FullName);
		}
	}

	FPhysxSharedData::UnlockAccess();

	// Clear the cooked data
	if (!GIsEditor && !bSharedCookedData)
	{
		CookedFormatData.FlushData();
	}

	bCreatedPhysicsMeshes = true;
}
#endif //WITH_PHYSX

void UBodySetup::CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished OnAsyncPhysicsCookFinished)
{
	check(IsInGameThread());

	// Don't start another cook cycle if one's already in progress
	check(CurrentCookHelper == nullptr);

	// Only perform this check for PhysX as the cooking module is optional
#if WITH_PHYSX_COOKING && PHYSICS_INTERFACE_PHYSX
	if (IsRuntime(this) && !IsRuntimeCookingEnabled())
	{
		UE_LOG(LogPhysics, Error, TEXT("Attempting to build physics data for %s at runtime, but runtime cooking is disabled (see the RuntimePhysXCooking plugin)."), *GetPathName());
		FinishCreatePhysicsMeshesAsync(nullptr, OnAsyncPhysicsCookFinished);
		return;
	}
#endif

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
	if(IPhysXCookingModule* PhysXCookingModule = GetPhysXCookingModule())
	{
		FPhysXCookHelper* AsyncPhysicsCookHelper = new FPhysXCookHelper(PhysXCookingModule);
		GetCookInfo(AsyncPhysicsCookHelper->CookInfo, GetRuntimeOnlyCookOptimizationFlags());	//TODO: pass in different flags?

		if(AsyncPhysicsCookHelper->HasSomethingToCook(AsyncPhysicsCookHelper->CookInfo))
		{
			FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(FSimpleDelegateGraphTask::FDelegate::CreateRaw(AsyncPhysicsCookHelper, &FPhysXCookHelper::CreatePhysicsMeshesAsync_Concurrent,
				/*FinishDelegate=*/FSimpleDelegateGraphTask::FDelegate::CreateUObject(this, &UBodySetup::FinishCreatePhysicsMeshesAsync, AsyncPhysicsCookHelper, OnAsyncPhysicsCookFinished)),
				GET_STATID(STAT_PhysXCooking), nullptr, ENamedThreads::AnyThread);

			CurrentCookHelper = AsyncPhysicsCookHelper;
		}
		else
		{
			delete AsyncPhysicsCookHelper;
			FinishCreatePhysicsMeshesAsync(nullptr, OnAsyncPhysicsCookFinished);
		}
	}
	else
	{
		FinishCreatePhysicsMeshesAsync(nullptr, OnAsyncPhysicsCookFinished);
	}
#else
	FAsyncCookHelper* NewCookHelper = new FAsyncCookHelper(this);
	if(NewCookHelper->HasWork())
	{
		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(FSimpleDelegateGraphTask::FDelegate::CreateRaw(NewCookHelper, &FAsyncCookHelper::CookAsync,
															 /*FinishDelegate=*/FSimpleDelegateGraphTask::FDelegate::CreateUObject(this, &UBodySetup::FinishCreatePhysicsMeshesAsync, NewCookHelper, OnAsyncPhysicsCookFinished)),
															 GET_STATID(STAT_PhysXCooking), nullptr, ENamedThreads::AnyThread);
		CurrentCookHelper = NewCookHelper;
	}
	else
	{
		delete NewCookHelper;
		FinishCreatePhysicsMeshesAsync(nullptr, OnAsyncPhysicsCookFinished);
	}
#endif // WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
}

void UBodySetup::AbortPhysicsMeshAsyncCreation()
{
#if PHYSICS_INTERFACE_PHYSX
	if (CurrentCookHelper)
	{
		CurrentCookHelper->Abort();
	}
#endif
}

void UBodySetup::FinishCreatePhysicsMeshesAsync(FAsyncCookHelper* AsyncPhysicsCookHelper, FOnAsyncPhysicsCookFinished OnAsyncPhysicsCookFinished)
{
	// Ensure we haven't gotten multiple cooks going
	// Then clear it
	check(CurrentCookHelper == AsyncPhysicsCookHelper);
	CurrentCookHelper = nullptr;

	bool bSuccess = AsyncPhysicsCookHelper != nullptr;

	if(AsyncPhysicsCookHelper)
	{
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		FinishCreatingPhysicsMeshes_PhysX(AsyncPhysicsCookHelper->OutNonMirroredConvexMeshes, AsyncPhysicsCookHelper->OutMirroredConvexMeshes, AsyncPhysicsCookHelper->OutTriangleMeshes);
		UVInfo = AsyncPhysicsCookHelper->OutUVInfo;
#elif WITH_CHAOS
		FinishCreatingPhysicsMeshes_Chaos(*AsyncPhysicsCookHelper);
		UVInfo = AsyncPhysicsCookHelper->UVInfo;
#endif // WITH_PHYSX
		delete AsyncPhysicsCookHelper;

	}
	else
	{
		ClearPhysicsMeshes();
		bCreatedPhysicsMeshes = true;
	}

	OnAsyncPhysicsCookFinished.ExecuteIfBound(bSuccess);
}

#if WITH_CHAOS
bool UBodySetup::ProcessFormatData_Chaos(FByteBulkData* FormatData)
{
	if(FormatData->IsLocked())
	{
		// seems it's being already processed
		return false;
	}

	FChaosDerivedDataReader<float, 3> Reader(FormatData);
	FinishCreatingPhysicsMeshes_Chaos(Reader);
	
	return true;
}

bool UBodySetup::RuntimeCookPhysics_Chaos()
{
	Chaos::FCookHelper Cooker(this);
	Cooker.Cook();
	FinishCreatingPhysicsMeshes_Chaos(Cooker);

	return true;
	
}

void UBodySetup::FinishCreatingPhysicsMeshes_Chaos(FChaosDerivedDataReader<float, 3>& InReader)
{
	FinishCreatingPhysicsMeshes_Chaos(InReader.ConvexImplicitObjects, InReader.TrimeshImplicitObjects, InReader.UVInfo, InReader.FaceRemap);
}

void UBodySetup::FinishCreatingPhysicsMeshes_Chaos(Chaos::FCookHelper& InHelper)
{
	TArray<TSharedPtr<Chaos::FConvex, ESPMode::ThreadSafe>> SharedSimpleImplicits;
	TArray<TSharedPtr<Chaos::FTriangleMeshImplicitObject, ESPMode::ThreadSafe>> SharedComplexImplicits;

	// The cooker will prepare unique implicits, body setup requires shared implicits, we do the conversion / promotion to shared
	// here and then the contents are moved into the body setup storage as part of FinishCreatingPhysicsMeshes
	for(TUniquePtr<Chaos::FImplicitObject>& Simple : InHelper.SimpleImplicits)
	{
		SharedSimpleImplicits.Add(MakeShared<Chaos::FConvex, ESPMode::ThreadSafe>(MoveTemp(Simple.Release()->GetObjectChecked<Chaos::FConvex>())));
	}

	for(TUniquePtr<Chaos::FTriangleMeshImplicitObject>& Complex : InHelper.ComplexImplicits)
	{
		SharedComplexImplicits.Emplace(Complex.Release());
	}

	InHelper.SimpleImplicits.Reset();
	InHelper.ComplexImplicits.Reset();

	FinishCreatingPhysicsMeshes_Chaos(SharedSimpleImplicits, SharedComplexImplicits, InHelper.UVInfo, InHelper.FaceRemap);
}

void UBodySetup::FinishCreatingPhysicsMeshes_Chaos(TArray<TSharedPtr<Chaos::FConvex, ESPMode::ThreadSafe>>& ConvexImplicits, TArray<TSharedPtr<Chaos::FTriangleMeshImplicitObject, ESPMode::ThreadSafe>>& TrimeshImplicits, FBodySetupUVInfo& InUvInfo, TArray<int32>& InFaceRemap)
{
	ClearPhysicsMeshes();

	const FString FullName = GetFullName();
	if(GetCollisionTraceFlag() != CTF_UseComplexAsSimple)
	{
		for(int32 ElementIndex = 0; ElementIndex < AggGeom.ConvexElems.Num(); ElementIndex++)
		{
			FKConvexElem& ConvexElem = AggGeom.ConvexElems[ElementIndex];

			if(CHAOS_ENSURE((ElementIndex < ConvexImplicits.Num())
			   && ConvexImplicits[ElementIndex]->IsValidGeometry()))
			{
				ConvexElem.SetChaosConvexMesh(MoveTemp(ConvexImplicits[ElementIndex]));

#if TRACK_CHAOS_GEOMETRY
				ConvexElem.GetChaosConvexMesh()->Track(Chaos::MakeSerializable(ConvexElem.GetChaosConvexMesh()), FullName);
#endif

				if(ConvexElem.GetChaosConvexMesh()->IsPerformanceWarning())
				{
					const FString& PerformanceString = ConvexElem.GetChaosConvexMesh()->PerformanceWarningAndSimplifaction();
					UE_LOG(LogPhysics, Warning, TEXT("TConvex Name:%s, Element [%d], %s"), *FullName, ElementIndex, *PerformanceString);
				}
			}
			else
			{
				if(ElementIndex >= ConvexImplicits.Num())
				{
					UE_LOG(LogPhysics, Warning, TEXT("InReader.ConvexImplicitObjects.Num() [%d], AggGeom.ConvexElems.Num() [%d]"),
						   ConvexImplicits.Num(), AggGeom.ConvexElems.Num());
				}
				CHAOS_LOG(LogPhysics, Warning, TEXT("TConvex Name:%s, Element [%d] has no Geometry"), *FullName, ElementIndex);
			}
		}
		ConvexImplicits.Reset();
	}

	ChaosTriMeshes = MoveTemp(TrimeshImplicits);
	UVInfo = MoveTemp(InUvInfo);
	FaceRemap = MoveTemp(InFaceRemap);
#if TRACK_CHAOS_GEOMETRY
	for(auto& TriMesh : ChaosTriMeshes)
	{
		TriMesh->Track(Chaos::MakeSerializable(TriMesh), FullName);
	}
#endif

#if WITH_CHAOS
	// Force trimesh collisions off
	for(auto& TriMesh : ChaosTriMeshes)
	{
		TriMesh->SetDoCollide(false);
	}
#endif

	// Clear the cooked data
	if(!GIsEditor && !bSharedCookedData)
	{
		CookedFormatData.FlushData();
	}

	bCreatedPhysicsMeshes = true;
}

#endif

void UBodySetup::ClearPhysicsMeshes()
{
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

	FPhysxSharedData::LockAccess();

	for(int32 i=0; i<AggGeom.ConvexElems.Num(); i++)
	{
		FKConvexElem* ConvexElem = &(AggGeom.ConvexElems[i]);

		if(ConvexElem->GetConvexMesh() != nullptr)
		{
			// put in list for deferred release
			GPhysXPendingKillConvex.Add(ConvexElem->GetConvexMesh());
			FPhysxSharedData::Get().Remove(ConvexElem->GetConvexMesh());
			ConvexElem->SetConvexMesh(nullptr);
		}

		if(ConvexElem->GetMirroredConvexMesh() != nullptr)
		{
			// put in list for deferred release
			GPhysXPendingKillConvex.Add(ConvexElem->GetMirroredConvexMesh());
			FPhysxSharedData::Get().Remove(ConvexElem->GetMirroredConvexMesh());
			ConvexElem->SetMirroredConvexMesh(nullptr);
		}
	}

	for(int32 ElementIndex = 0; ElementIndex < TriMeshes.Num(); ++ElementIndex)
	{
		GPhysXPendingKillTriMesh.Add(TriMeshes[ElementIndex]);
		FPhysxSharedData::Get().Remove(TriMeshes[ElementIndex]);
		TriMeshes[ElementIndex] = NULL;
	}

	FPhysxSharedData::UnlockAccess();

	TriMeshes.Empty();

#elif WITH_CHAOS
	for (int32 i = 0; i < AggGeom.ConvexElems.Num(); i++)
	{
		FKConvexElem* ConvexElem = &(AggGeom.ConvexElems[i]);
		ConvexElem->ResetChaosConvexMesh();
	}
	ChaosTriMeshes.Reset();
#endif // WITH_PHYSX

	bCreatedPhysicsMeshes = false;


	// Also clear render info
	AggGeom.FreeRenderInfo();
}

DECLARE_CYCLE_STAT(TEXT("AddShapesToRigidActor"), STAT_AddShapesToActor, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("AddGeomToSolver"), STAT_AddGeomToSolver, STATGROUP_Physics);

void UBodySetup::AddShapesToRigidActor_AssumesLocked(
	FBodyInstance* OwningInstance, 
	FVector& Scale3D, 
	UPhysicalMaterial* SimpleMaterial,
	TArray<UPhysicalMaterial*>& ComplexMaterials,
	TArray<FPhysicalMaterialMaskParams>& ComplexMaterialMasks,
	const FBodyCollisionData& BodyCollisionData,
	const FTransform& RelativeTM, 
	TArray<FPhysicsShapeHandle>* NewShapes)
{
	SCOPE_CYCLE_COUNTER(STAT_AddShapesToActor);

	check(OwningInstance);

	// in editor, there are a lot of things relying on body setup to create physics meshes
	CreatePhysicsMeshes();

	// To AddGeometry in interface
	// if almost zero, set min scale
	// @todo fixme
	if (Scale3D.IsNearlyZero())
	{
		// set min scale
		Scale3D = FVector(0.1f);
	}

	FGeometryAddParams AddParams;
	AddParams.bDoubleSided = bDoubleSidedGeometry;
	AddParams.CollisionData = BodyCollisionData;
	AddParams.CollisionTraceType = GetCollisionTraceFlag();
	AddParams.Scale = Scale3D;
	AddParams.SimpleMaterial = SimpleMaterial;
	AddParams.ComplexMaterials = TArrayView<UPhysicalMaterial*>(ComplexMaterials);
#if WITH_CHAOS
	AddParams.ComplexMaterialMasks = TArrayView<FPhysicalMaterialMaskParams>(ComplexMaterialMasks);
#endif
	AddParams.LocalTransform = RelativeTM;
	AddParams.WorldTransform = OwningInstance->GetUnrealWorldTransform();
	AddParams.Geometry = &AggGeom;
#if PHYSICS_INTERFACE_PHYSX
	AddParams.TriMeshes = TArrayView<PxTriangleMesh*>(TriMeshes);
#endif

#if WITH_CHAOS
	AddParams.ChaosTriMeshes = MakeArrayView(ChaosTriMeshes);
#endif
	{
		SCOPE_CYCLE_COUNTER(STAT_AddGeomToSolver);
		FPhysicsInterface::AddGeometry(OwningInstance->ActorHandle, AddParams, NewShapes);
	}
}

void UBodySetup::RemoveSimpleCollision()
{
	InvalidatePhysicsData();
	AggGeom.EmptyElements();
}

void UBodySetup::RescaleSimpleCollision( FVector BuildScale )
{
	if( BuildScale3D != BuildScale )
	{					
		// Back out the old scale when applying the new scale
		const FVector ScaleMultiplier3D = (BuildScale / BuildScale3D);

		for (int32 i = 0; i < AggGeom.ConvexElems.Num(); i++)
		{
			FKConvexElem* ConvexElem = &(AggGeom.ConvexElems[i]);

			FTransform ConvexTrans = ConvexElem->GetTransform();
			FVector ConvexLoc = ConvexTrans.GetLocation();
			ConvexLoc *= ScaleMultiplier3D;
			ConvexTrans.SetLocation(ConvexLoc);
			ConvexElem->SetTransform(ConvexTrans);

			TArray<FVector>& Vertices = ConvexElem->VertexData;
			for (int32 VertIndex = 0; VertIndex < Vertices.Num(); ++VertIndex)
			{
				Vertices[VertIndex] *= ScaleMultiplier3D;
			}

			ConvexElem->UpdateElemBox();
		}

		// @todo Deal with non-vector properties by just applying the max value for the time being
		const float ScaleMultiplier = ScaleMultiplier3D.GetMax();

		for (int32 i = 0; i < AggGeom.SphereElems.Num(); i++)
		{
			FKSphereElem* SphereElem = &(AggGeom.SphereElems[i]);

			SphereElem->Center *= ScaleMultiplier3D;
			SphereElem->Radius *= ScaleMultiplier;
		}

		for (int32 i = 0; i < AggGeom.BoxElems.Num(); i++)
		{
			FKBoxElem* BoxElem = &(AggGeom.BoxElems[i]);

			BoxElem->Center *= ScaleMultiplier3D;
			BoxElem->X *= ScaleMultiplier3D.X;
			BoxElem->Y *= ScaleMultiplier3D.Y;
			BoxElem->Z *= ScaleMultiplier3D.Z;
		}

		for (int32 i = 0; i < AggGeom.SphylElems.Num(); i++)
		{
			FKSphylElem* SphylElem = &(AggGeom.SphylElems[i]);

			SphylElem->Center *= ScaleMultiplier3D;
			SphylElem->Radius *= ScaleMultiplier;
			SphylElem->Length *= ScaleMultiplier;
		}

		BuildScale3D = BuildScale;
	}
}

void UBodySetup::InvalidatePhysicsData()
{
	ClearPhysicsMeshes();
	BodySetupGuid = FGuid::NewGuid(); // change the guid
	if (!bSharedCookedData)
	{
		CookedFormatData.FlushData();
	}
#if WITH_EDITOR
	CookedFormatDataRuntimeOnlyOptimization.FlushData();
#endif
}

void UBodySetup::BeginDestroy()
{
	Super::BeginDestroy();

	AggGeom.FreeRenderInfo();
}	

void UBodySetup::FinishDestroy()
{
	ClearPhysicsMeshes();
	Super::FinishDestroy();
}




void UBodySetup::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteReleaseBranchCustomObjectVersion::GUID);

	// Load GUID (or create one for older versions)
	Ar << BodySetupGuid;

	// If we loaded a ZERO Guid, fix that
	if(Ar.IsLoading() && !BodySetupGuid.IsValid())
	{
		MarkPackageDirty();
		UE_LOG(LogPhysics, Log, TEXT("FIX GUID FOR: %s"), *GetPathName());
		BodySetupGuid = FGuid::NewGuid();
	}

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	bool bDuplicating = (Ar.GetPortFlags() & PPF_Duplicate) != 0;

	if (bCooked)
	{
#if WITH_EDITOR
		if (Ar.IsCooking())
		{
			// Make sure to reset bHasCookedCollision data to true before calling GetCookedData for cooking
			bHasCookedCollisionData = true;
			FName Format = Ar.CookingTarget()->GetPhysicsFormat(this);
			bool bUseRuntimeOnlyCookedData = !bSharedCookedData;	//For shared cook data we do not optimize for runtime only flags. This is only used by per poly skeletal mesh component at the moment. Might want to add support in future
			bHasCookedCollisionData = GetCookedData(Format, bUseRuntimeOnlyCookedData) != NULL; // Get the data from the DDC or build it

			TArray<FName> ActualFormatsToSave;
			ActualFormatsToSave.Add(Format);

			FArchive_Serialize_BitfieldBool(Ar, bHasCookedCollisionData);

			FFormatContainer* UseCookedFormatData = bUseRuntimeOnlyCookedData ? &CookedFormatDataRuntimeOnlyOptimization : &CookedFormatData;
			UseCookedFormatData->Serialize(Ar, this, &ActualFormatsToSave, !bSharedCookedData);

#if VERIFY_COOKED_PHYS_DATA
			// Verify that the cooked data matches the uncooked data
			if(GetCollisionTraceFlag() != CTF_UseComplexAsSimple)
			{
				UObject* Outer = GetOuter();

				for(TPair<FName, FByteBulkData*>& TestFormat : UseCookedFormatData->Formats)
				{
					FByteBulkData* BulkData = TestFormat.Value;
					if(BulkData && BulkData->GetBulkDataSize() > 0)
					{
						FPhysXCookingDataReader PhysDataReader(*BulkData, &UVInfo);

						if(PhysDataReader.ConvexMeshes.Num() != AggGeom.ConvexElems.Num() || PhysDataReader.TriMeshes.Num() != TriMeshes.Num())
						{
							// Cooked data doesn't match our current geo
							UE_LOG(LogPhysics, Warning, TEXT("Body setup cooked data for component %s does not match uncooked geo. Convex: %d, %d, Trimesh: %d, %d"), Outer ? *Outer->GetName() : TEXT("None"), AggGeom.ConvexElems.Num(), PhysDataReader.ConvexMeshes.Num(), TriMeshes.Num(), PhysDataReader.TriMeshes.Num());
						}
					}
				}
			}
#endif
		}
		else
#endif
		{
			if (Ar.UE4Ver() >= VER_UE4_STORE_HASCOOKEDDATA_FOR_BODYSETUP)
			{
				// CL#14327190 Removed cooked implicit collision structures from the UBodySetup.
				// UBodySetups saved with support for cooked implicit geometry store a counter for the number 
				// of implicit objects saved to the file. This count needs to be removed from the input stream. 
				// Note: We only need to extract the count, not the array. Editor operations for populating the 
				// implicit array were never added, so it's expected that the actual implicit array would 
				// always be empty.
				bool bTemp = bHasCookedCollisionData;
				Ar << bTemp;
				bHasCookedCollisionData = bTemp;
			}
			CookedFormatData.Serialize(Ar, this);
		}
	}

#if WITH_EDITOR
	AggGeom.FixupDeprecated( Ar );
#endif

#if WITH_CHAOS && WITH_EDITOR

	if (Ar.IsLoading())
	{
		const bool bForceIndexRebuild = Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::ForceRebuildBodySetupIndices;
		for (FKConvexElem& Convex : AggGeom.ConvexElems)
		{
			// Reset potentially corrupted index data to correctly rebuild below
			if (bForceIndexRebuild)
			{
				Convex.IndexData.Reset();
			}
			// Build an index buffer if we don't have one, either as a consequence of the check above or loading in a mesh that has never been
			// processed with Chaos previously
			Convex.ComputeChaosConvexIndices();
		}
	}
#endif


	// Levelset Serialization support for BodySetup.
	if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::LevelsetSerializationSupportForBodySetup
		&& Ar.CustomVer(FFortniteReleaseBranchCustomObjectVersion::GUID) < FFortniteReleaseBranchCustomObjectVersion::DisableLevelset_v14_10)
	{
		TArray<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>> ChaosImplicitObjects;
#if WITH_CHAOS
		using namespace Chaos;
		FChaosArchive ChaosAr(Ar);

		int32 NumImplicits = 0;
		
		if (Ar.IsLoading())
		{
			ChaosImplicitObjects.Reset();

			Ar << NumImplicits;

			for (int i = 0; i < NumImplicits; i++)
			{
				if (FImplicitObject* ImplicitObject = FImplicitObject::SerializationFactory(ChaosAr, nullptr))
				{
					ImplicitObject->Serialize(Ar);
					ChaosImplicitObjects.Add(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe >(ImplicitObject));
				}
			}
		}
		/*
		else if (Ar.IsSaving())
		{
			for (int i = 0; i < ChaosImplicitObjects.Num(); i++)
			{
				if (ChaosImplicitObjects[i])
				{
					NumImplicits++;
				}
			}
			Ar << NumImplicits;

			for (int i = 0; i < ChaosImplicitObjects.Num(); i++)
			{
				if (ChaosImplicitObjects[i])
				{
					FImplicitObject::SerializationFactory(ChaosAr, ChaosImplicitObjects[i].Get());
					ChaosImplicitObjects[i]->Serialize(Ar);
				}
			}
		}
		*/
#else
		if(Ar.IsLoading())
		{
			int32 DummyCount;
			Ar << DummyCount;
		}
#endif
	}


}

void UBodySetup::PostLoad()
{
	Super::PostLoad();

	// Our owner needs to be post-loaded before us else they may not have loaded
	// their data yet.
	UObject* Outer = GetOuter();
	if (Outer)
	{
		Outer->ConditionalPostLoad();
	}

#if WITH_EDITORONLY_DATA
	if ( GetLinkerUE4Version() < VER_UE4_BUILD_SCALE_VECTOR )
	{
		BuildScale3D = FVector( BuildScale_DEPRECATED );
	}
#endif

	DefaultInstance.FixupData(this);

	if ( GetLinkerUE4Version() < VER_UE4_REFACTOR_PHYSICS_BLENDING )
	{
		if ( bAlwaysFullAnimWeight_DEPRECATED )
		{
			PhysicsType = PhysType_Simulated;
		}
		else if ( DefaultInstance.bSimulatePhysics == false )
		{
			PhysicsType = PhysType_Kinematic;
		}
		else
		{
			PhysicsType = PhysType_Default;
		}
	}

	if ( GetLinkerUE4Version() < VER_UE4_BODYSETUP_COLLISION_CONVERSION )
	{
		if ( DefaultInstance.GetCollisionEnabled() == ECollisionEnabled::NoCollision )
		{
			CollisionReponse = EBodyCollisionResponse::BodyCollision_Disabled;
		}
	}

	// Compress to whatever formats the active target platforms want
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	if (TPM)
	{
		const TArray<ITargetPlatform*>& Platforms = TPM->GetActiveTargetPlatforms();

		for (int32 Index = 0; Index < Platforms.Num(); Index++)
		{
			GetCookedData(Platforms[Index]->GetPhysicsFormat(this));
		}
	}

	// make sure that we load the physX data while the linker's loader is still open
#if PHYSICS_INTERFACE_PHYSX
	CreatePhysicsMeshes();
#elif WITH_CHAOS
	// If Deferring physics creation, skip so we can call CreatePhysicsMeshes in parallel.
	if (GEnableDeferredPhysicsCreation == false)
	{
		CreatePhysicsMeshes();
	}
#endif


}

void UBodySetup::UpdateTriMeshVertices(const TArray<FVector> & NewPositions)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateTriMeshVertices);
#if PHYSICS_INTERFACE_PHYSX
	if (TriMeshes.Num())
	{
		check(TriMeshes[0] != nullptr);
		PxU32 PNumVerts = TriMeshes[0]->getNbVertices(); // Get num of verts we expect
		PxVec3 * PNewPositions = TriMeshes[0]->getVerticesForModification();	//we only update the first trimesh. We assume this per poly case is not updating welded trimeshes

		int32 NumToCopy = FMath::Min<int32>(PNumVerts, NewPositions.Num()); // Make sure we don't write off end of array provided
		for (int32 i = 0; i < NumToCopy; ++i)
		{
			PNewPositions[i] = U2PVector(NewPositions[i]);
		}

		TriMeshes[0]->refitBVH();
	}
#elif WITH_CHAOS
	ensure(false);

#endif
}

template <bool bPositionAndNormal>
float GetClosestPointAndNormalImpl(const UBodySetup* BodySetup, const FVector& WorldPosition, const FTransform& LocalToWorld, FVector* ClosestWorldPosition, FVector* FeatureNormal)
{
	float ClosestDist = FLT_MAX;
	FVector TmpPosition, TmpNormal;

	//Note that this function is optimized for BodySetup with few elements. This is more common. If we want to optimize the case with many elements we should really return the element during the distance check to avoid pointless iteration
	for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
	{
		
		if(bPositionAndNormal)
		{
			const float Dist = SphereElem.GetClosestPointAndNormal(WorldPosition, LocalToWorld, TmpPosition, TmpNormal);

			if(Dist < ClosestDist)
			{
				*ClosestWorldPosition = TmpPosition;
				*FeatureNormal = TmpNormal;
				ClosestDist = Dist;
			}
		}
		else
		{
			const float Dist = SphereElem.GetShortestDistanceToPoint(WorldPosition, LocalToWorld);
			ClosestDist = Dist < ClosestDist ? Dist : ClosestDist;
		}
	}

	for (const FKSphylElem& SphylElem : BodySetup->AggGeom.SphylElems)
	{
		if (bPositionAndNormal)
		{
			const float Dist = SphylElem.GetClosestPointAndNormal(WorldPosition, LocalToWorld, TmpPosition, TmpNormal);

			if (Dist < ClosestDist)
			{
				*ClosestWorldPosition = TmpPosition;
				*FeatureNormal = TmpNormal;
				ClosestDist = Dist;
			}
		}
		else
		{
			const float Dist = SphylElem.GetShortestDistanceToPoint(WorldPosition, LocalToWorld);
			ClosestDist = Dist < ClosestDist ? Dist : ClosestDist;
		}
	}

	for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
	{
		if (bPositionAndNormal)
		{
			const float Dist = BoxElem.GetClosestPointAndNormal(WorldPosition, LocalToWorld, TmpPosition, TmpNormal);

			if (Dist < ClosestDist)
			{
				*ClosestWorldPosition = TmpPosition;
				*FeatureNormal = TmpNormal;
				ClosestDist = Dist;
			}
		}
		else
		{
			const float Dist =  BoxElem.GetShortestDistanceToPoint(WorldPosition, LocalToWorld);
			ClosestDist = Dist < ClosestDist ? Dist : ClosestDist;
		}
	}

	if (ClosestDist == FLT_MAX)
	{
		UE_LOG(LogPhysics, Warning, TEXT("GetClosestPointAndNormalImpl ClosestDist for BodySetup %s is coming back as FLT_MAX. WorldPosition = %s, LocalToWorld = %s"), *BodySetup->GetFullName(), *WorldPosition.ToString(), *LocalToWorld.ToHumanReadableString());
	}

	return ClosestDist;
}

float UBodySetup::GetShortestDistanceToPoint(const FVector& WorldPosition, const FTransform& LocalToWorld) const
{
	return GetClosestPointAndNormalImpl<false>(this, WorldPosition, LocalToWorld, nullptr, nullptr);
}

float UBodySetup::GetClosestPointAndNormal(const FVector& WorldPosition, const FTransform& LocalToWorld, FVector& ClosestWorldPosition, FVector& FeatureNormal) const
{
	return GetClosestPointAndNormalImpl<true>(this, WorldPosition, LocalToWorld, &ClosestWorldPosition, &FeatureNormal);
}

#if WITH_EDITOR
void UBodySetup::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	GetCookedData(TargetPlatform->GetPhysicsFormat(this), true);
}

void UBodySetup::ClearCachedCookedPlatformData( const ITargetPlatform* TargetPlatform )
{
	CookedFormatDataRuntimeOnlyOptimization.FlushData();
}
#endif

#if WITH_PHYSX
EPhysXMeshCookFlags UBodySetup::GetRuntimeOnlyCookOptimizationFlags() const
{
	EPhysXMeshCookFlags RuntimeCookFlags = EPhysXMeshCookFlags::Default;
	if(UPhysicsSettings::Get()->bSuppressFaceRemapTable)
	{
		RuntimeCookFlags |= EPhysXMeshCookFlags::SuppressFaceRemapTable;
	}
	return RuntimeCookFlags;
}
#endif

bool UBodySetup::CalcUVAtLocation(const FVector& BodySpaceLocation, int32 FaceIndex, int32 UVChannel, FVector2D& UV) const
{
	bool bSuccess = false;

	if (UVInfo.VertUVs.IsValidIndex(UVChannel) && UVInfo.IndexBuffer.IsValidIndex(FaceIndex * 3 + 2))
	{
		int32 Index0 = UVInfo.IndexBuffer[FaceIndex * 3 + 0];
		int32 Index1 = UVInfo.IndexBuffer[FaceIndex * 3 + 1];
		int32 Index2 = UVInfo.IndexBuffer[FaceIndex * 3 + 2];

		FVector Pos0 = UVInfo.VertPositions[Index0];
		FVector Pos1 = UVInfo.VertPositions[Index1];
		FVector Pos2 = UVInfo.VertPositions[Index2];

		FVector2D UV0 = UVInfo.VertUVs[UVChannel][Index0];
		FVector2D UV1 = UVInfo.VertUVs[UVChannel][Index1];
		FVector2D UV2 = UVInfo.VertUVs[UVChannel][Index2];

		// Transform hit location from world to local space.
		// Find barycentric coords
		FVector BaryCoords = FMath::ComputeBaryCentric2D(BodySpaceLocation, Pos0, Pos1, Pos2);
		// Use to blend UVs
		UV = (BaryCoords.X * UV0) + (BaryCoords.Y * UV1) + (BaryCoords.Z * UV2);

		bSuccess = true;
	}

	return bSuccess;
}

template<typename DDCBuilderType>
void GetDDCBuiltData(FByteBulkData* OutResult, DDCBuilderType& InBuilder, UBodySetup* InSetup, bool bInIsRuntime)
{
	TArray<uint8> OutData;

	if(InBuilder.CanBuild())
	{
		COOK_STAT(FCookStats::FScopedStatsCounter Timer = PhysXBodySetupCookStats::UsageStats.TimeSyncWork());

		// Debugging switch, force builder to always run
		bool bSkipDDC = false;

		bool bDataWasBuilt = false;
		bool bDDCHit = false;

		if(!bSkipDDC)
		{
			bDDCHit = GetDerivedDataCacheRef().GetSynchronous(&InBuilder, OutData, &bDataWasBuilt);
		}
		else
		{
			bDataWasBuilt = true;
			InBuilder.Build(OutData);
		}

		COOK_STAT(Timer.AddHitOrMiss(!bDDCHit || bDataWasBuilt ? FCookStats::CallStats::EHitOrMiss::Miss : FCookStats::CallStats::EHitOrMiss::Hit, OutData.Num()));
	}

	if(OutData.Num())
	{
		OutResult->Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(OutResult->Realloc(OutData.Num()), OutData.GetData(), OutData.Num());
		OutResult->Unlock();
	}
	else if(!bInIsRuntime)	//only want to warn if DDC cooking failed - if it's really trying to use runtime and we can't, the runtime cooker code will catch it
	{
		UE_LOG(LogPhysics, Warning, TEXT("Attempt to build physics data for %s when we are unable to."), *InSetup->GetPathName());
	}
}

FByteBulkData* UBodySetup::GetCookedData(FName Format, bool bRuntimeOnlyOptimizedVersion)
{
	if (IsTemplate())
	{
		return NULL;
	}

	IInterface_CollisionDataProvider* CDP = Cast<IInterface_CollisionDataProvider>(GetOuter());

	// If there is nothing to cook or if we are reading data from a cooked package for an asset with no collision, 
	// we want to return here
	if ((AggGeom.ConvexElems.Num() == 0 && CDP == NULL) || !bHasCookedCollisionData)
	{
		return NULL;
	}

#if WITH_EDITOR
	//We don't support runtime cook optimization for per poly skeletal mesh. This is an edge case we may want to support (only helps memory savings)
	FFormatContainer* UseCookedData = CookedFormatDataOverride ? CookedFormatDataOverride : (bRuntimeOnlyOptimizedVersion ? &CookedFormatDataRuntimeOnlyOptimization : &CookedFormatData);
#else
	FFormatContainer* UseCookedData = CookedFormatDataOverride ? CookedFormatDataOverride : &CookedFormatData;
#endif

	bool bContainedData = UseCookedData->Contains(Format);
	FByteBulkData* Result = &UseCookedData->GetFormat(Format);
	bool bIsRuntime = IsRuntime(this);

#if /*WITH_PHYSX &&*/ WITH_EDITOR
	if (!bContainedData)
	{
		SCOPE_CYCLE_COUNTER(STAT_PhysXCooking);

		if (AggGeom.ConvexElems.Num() == 0 && (CDP == NULL || CDP->ContainsPhysicsTriMeshData(bMeshCollideAll) == false))
		{
			return nullptr;
		}

#if PHYSICS_INTERFACE_PHYSX
		const bool bEligibleForRuntimeOptimization = UseCookedData == &CookedFormatDataRuntimeOnlyOptimization;
		const EPhysXMeshCookFlags CookingFlags = bEligibleForRuntimeOptimization ? GetRuntimeOnlyCookOptimizationFlags() : EPhysXMeshCookFlags::Default;
		FDerivedDataPhysXCooker* PhysicsDerivedCooker = new FDerivedDataPhysXCooker(Format, CookingFlags, this, bIsRuntime);
#elif WITH_CHAOS 
		FChaosDerivedDataCooker* PhysicsDerivedCooker = new FChaosDerivedDataCooker(this, Format);
#else
		static_assert(false, "No cooker defined for this physics interface");
#endif
			
		GetDDCBuiltData(Result, *PhysicsDerivedCooker, this, bIsRuntime);
	}
#endif // WITH_PHYSX && WITH_EDITOR

	check(Result);
	return Result->GetBulkDataSize() > 0 ? Result : NULL; // we don't return empty bulk data...but we save it to avoid thrashing the DDC
}

void UBodySetup::GetGeometryDDCKey(FString& OutString) const
{
	// If the geometry changes in a way not controlled below, increment the key version to invalidate DDC entries
	enum { BODY_SETUP_GEOMETRY_KEY_VER = 0 };

	FString MeshIdString(TEXT("CDP_BODYSETUP"));

	IInterface_CollisionDataProvider* CDP = Cast<IInterface_CollisionDataProvider>(GetOuter());
	if(CDP)
	{
		CDP->GetMeshId(MeshIdString);
	}

	OutString = FString::Printf(TEXT("%s_%s_%s_%d_%d_%d_%d_%f_%f_%d"),
		*BodySetupGuid.ToString(),
		*MeshIdString,
		*AggGeom.MakeDDCKey().ToString(),
		(int32)bGenerateNonMirroredCollision,
		(int32)bGenerateMirroredCollision,
		(int32)UPhysicsSettings::Get()->bSupportUVFromHitResults,
		(int32)GetCollisionTraceFlag(),
		UPhysicsSettings::Get()->SolverOptions.CollisionMarginFraction,
		UPhysicsSettings::Get()->SolverOptions.CollisionMarginMax,
		(int32)BODY_SETUP_GEOMETRY_KEY_VER);

	if (bSupportUVsAndFaceRemap)
	{
		OutString += FString(TEXT("_1"));
	}
}

void UBodySetup::PostInitProperties()
{
	Super::PostInitProperties();

	if(!IsTemplate())
	{
		BodySetupGuid = FGuid::NewGuid();
	}
}

#if WITH_EDITOR

void UBodySetup::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if(PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UBodySetup, AggGeom))
	{
		UStaticMesh* StaticMesh = GetTypedOuter<UStaticMesh>();
		if(StaticMesh)
		{
			for(UStaticMeshComponent* StaticMeshComponent : TObjectRange<UStaticMeshComponent>())
			{
				if(StaticMeshComponent->GetStaticMesh() == StaticMesh)
				{
					// it needs to recreate IF it already has been created
					if(StaticMeshComponent->IsPhysicsStateCreated())
					{
						StaticMeshComponent->RecreatePhysicsState();
					}
				}
			}
		}
	}
}

void UBodySetup::PostEditUndo()
{
	Super::PostEditUndo();

	// If we have any convex elems, ensure they are recreated whenever anything is modified!
	if(AggGeom.ConvexElems.Num() > 0)
	{
		InvalidatePhysicsData();
		CreatePhysicsMeshes();
	}
}

void UBodySetup::CopyBodySetupProperty(const UBodySetup* Other)
{
	BoneName = Other->BoneName;
	PhysicsType = Other->PhysicsType;
	bConsiderForBounds = Other->bConsiderForBounds;
	bMeshCollideAll = Other->bMeshCollideAll;
	bDoubleSidedGeometry = Other->bDoubleSidedGeometry;
	bGenerateNonMirroredCollision = Other->bGenerateNonMirroredCollision;
	bSharedCookedData = Other->bSharedCookedData;
	bGenerateMirroredCollision = Other->bGenerateMirroredCollision;
	PhysMaterial = Other->PhysMaterial;
	CollisionReponse = Other->CollisionReponse;
	CollisionTraceFlag = Other->CollisionTraceFlag;
	DefaultInstance = Other->DefaultInstance;
	WalkableSlopeOverride = Other->WalkableSlopeOverride;
	BuildScale3D = Other->BuildScale3D;
}

EDataValidationResult UBodySetup::IsDataValid(TArray<FText>& ValidationErrors)
{
	EDataValidationResult Result = EDataValidationResult::Valid;

	// Check that the body has at least one shape
	int32 NumElements = AggGeom.GetElementCount();
	if (NumElements == 0)
	{
		ValidationErrors.Add(FText::Format(LOCTEXT("UBodySetupHasNoCollision", "Bone {0} requires at least one collision shape"), FText::FromName(BoneName)));
		Result = EDataValidationResult::Invalid;
	}

	// Chekc that simulated bodies have at least one shape that contributes to mass, otherwise
	// we cannot calculate the inertia, even if the mass is exlicitly set.
	// @todo(physics): should we check non-simulated bodies? The simulation type can be changed in the runtime...
	if (PhysicsType == EPhysicsType::PhysType_Simulated)
	{
		int32 NumMassContributors = 0;
		for (int32 ElementIndex = 0; ElementIndex < NumElements; ++ElementIndex)
		{
			const FKShapeElem* Shape = AggGeom.GetElement(ElementIndex);
			if (Shape->GetContributeToMass())
			{
				++NumMassContributors;
			}
		}
		
		if (NumMassContributors == 0)
		{
			ValidationErrors.Add(FText::Format(LOCTEXT("UBodySetupHasNoMass", "Bone {0} requires at least one shape with 'Contribute to Mass' set to 'true'"), FText::FromName(BoneName)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}

#endif // WITH_EDITOR

void UBodySetup::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

#if PHYSICS_INTERFACE_PHYSX
	// Count PhysX trimesh mem usage
	for(PxTriangleMesh* TriMesh : TriMeshes)
	{
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(GetPhysxObjectSize(TriMesh, NULL));
	}

	// Count PhysX convex mem usage
	for(int ConvIdx=0; ConvIdx<AggGeom.ConvexElems.Num(); ConvIdx++)
	{
		FKConvexElem& ConvexElem = AggGeom.ConvexElems[ConvIdx];

		if(ConvexElem.GetConvexMesh() != NULL)
		{
			CumulativeResourceSize.AddDedicatedSystemMemoryBytes(GetPhysxObjectSize(ConvexElem.GetConvexMesh(), NULL));
		}

		if(ConvexElem.GetMirroredConvexMesh() != NULL)
		{
			CumulativeResourceSize.AddDedicatedSystemMemoryBytes(GetPhysxObjectSize(ConvexElem.GetMirroredConvexMesh(), NULL));
		}
	}

#endif // WITH_PHYSX

	if (CookedFormatData.Contains(FPlatformProperties::GetPhysicsFormat()))
	{
		const FByteBulkData& FmtData = CookedFormatData.GetFormat(FPlatformProperties::GetPhysicsFormat());
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(FmtData.GetBulkDataSize());
	}

	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(FaceRemap.GetAllocatedSize());
	
	// Count any UV info
	UVInfo.GetResourceSizeEx(CumulativeResourceSize);
}

#if WITH_EDITORONLY_DATA
void FKAggregateGeom::FixupDeprecated(FArchive& Ar)
{
	for (auto SphereElemIt = SphereElems.CreateIterator(); SphereElemIt; ++SphereElemIt)
	{
		SphereElemIt->FixupDeprecated(Ar);
	}

	for (auto BoxElemIt = BoxElems.CreateIterator(); BoxElemIt; ++BoxElemIt)
	{
		BoxElemIt->FixupDeprecated(Ar);
	}

	for (auto SphylElemIt = SphylElems.CreateIterator(); SphylElemIt; ++SphylElemIt)
	{
		SphylElemIt->FixupDeprecated(Ar);
	}
}
#endif

float FKAggregateGeom::GetVolume(const FVector& Scale) const
{
	float Volume = 0.0f;

	for ( auto SphereElemIt = SphereElems.CreateConstIterator(); SphereElemIt; ++SphereElemIt )
	{
		Volume += SphereElemIt->GetVolume(Scale);
	}

	for ( auto BoxElemIt = BoxElems.CreateConstIterator(); BoxElemIt; ++BoxElemIt )
	{
		Volume += BoxElemIt->GetVolume(Scale);
	}

	for ( auto SphylElemIt = SphylElems.CreateConstIterator(); SphylElemIt; ++SphylElemIt )
	{
		Volume += SphylElemIt->GetVolume(Scale);
	}

	for ( auto ConvexElemIt = ConvexElems.CreateConstIterator(); ConvexElemIt; ++ConvexElemIt )
	{
		Volume += ConvexElemIt->GetVolume(Scale);
	}

	return Volume;
}

FGuid FKAggregateGeom::MakeDDCKey() const
{
	UScriptStruct* StructType = FKAggregateGeom::StaticStruct();
	TArray<uint8> Bytes;
	Bytes.Reserve(64);
	FMemoryWriter MemAr(Bytes);

	StructType->SerializeTaggedProperties(MemAr, (uint8*)this, StructType, nullptr);

	FSHA1 Sha;
	Sha.Update(Bytes.GetData(), Bytes.Num());
	Sha.Final();

	uint32 Hash[5];
	Sha.GetHash((uint8*)Hash);
	FGuid OutGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);

	return OutGuid;
}

int32 FKAggregateGeom::GetElementCount(EAggCollisionShape::Type Type) const
{
	switch (Type)
	{
	case EAggCollisionShape::Box:
		return BoxElems.Num();
	case EAggCollisionShape::Convex:
		return ConvexElems.Num();
	case EAggCollisionShape::Sphyl:
		return SphylElems.Num();
	case EAggCollisionShape::Sphere:
		return SphereElems.Num();
	case EAggCollisionShape::TaperedCapsule:
		return TaperedCapsuleElems.Num();
	default:
		return 0;
	}
}


FKConvexElem::FKConvexElem()
	: FKShapeElem(EAggCollisionShape::Convex)
	, ElemBox(ForceInit)
	, Transform(FTransform::Identity)
#if PHYSICS_INTERFACE_PHYSX
	, ConvexMesh(NULL)
	, ConvexMeshNegX(NULL)
#endif
{}

FKConvexElem::FKConvexElem(const FKConvexElem& Other)
#if PHYSICS_INTERFACE_PHYSX
	: ConvexMesh(nullptr)
	, ConvexMeshNegX(nullptr)
#endif
{
	CloneElem(Other);
}

FKConvexElem::~FKConvexElem()
{

}

const FKConvexElem& FKConvexElem::operator=(const FKConvexElem& Other)
{
#if PHYSICS_INTERFACE_PHYSX
	ensureMsgf(ConvexMesh == nullptr, TEXT("We are leaking memory. Why are we calling the assignment operator on an element that has already allocated resources?"));
	ensureMsgf(ConvexMeshNegX == nullptr, TEXT("We are leaking memory. Why are we calling the assignment operator on an element that has already allocated resources?"));
	ConvexMesh = nullptr;
	ConvexMeshNegX = nullptr;
#elif WITH_CHAOS
	ensureMsgf(!ChaosConvex, TEXT("We are leaking memory. Why are we calling the assignment operator on an element that has already allocated resources?"));
	ResetChaosConvexMesh();
#endif
	CloneElem(Other);
	return *this;
}

/** Helper function to safely copy instances of this shape*/
void FKConvexElem::CloneElem(const FKConvexElem& Other)
{
	Super::CloneElem(Other);
	VertexData = Other.VertexData;
	IndexData = Other.IndexData;
	ElemBox = Other.ElemBox;
	Transform = Other.Transform;
}

void FKConvexElem::ScaleElem(FVector DeltaSize, float MinSize)
{
	FTransform ScaledTransform = GetTransform();
	ScaledTransform.SetScale3D(ScaledTransform.GetScale3D() + DeltaSize);
	SetTransform(ScaledTransform);
}

// References: 
// http://amp.ece.cmu.edu/Publication/Cha/icip01_Cha.pdf
// http://stackoverflow.com/questions/1406029/how-to-calculate-the-volume-of-a-3d-mesh-object-the-surface-of-which-is-made-up
float SignedVolumeOfTriangle(const FVector& p1, const FVector& p2, const FVector& p3) 
{
	return FVector::DotProduct(p1, FVector::CrossProduct(p2, p3)) / 6.0f;
}
#if PHYSICS_INTERFACE_PHYSX
physx::PxConvexMesh* FKConvexElem::GetConvexMesh() const
{
	return ConvexMesh;
}

void FKConvexElem::SetConvexMesh(physx::PxConvexMesh* InMesh)
{
	ConvexMesh = InMesh;
}

physx::PxConvexMesh* FKConvexElem::GetMirroredConvexMesh() const
{
	return ConvexMeshNegX;
}

void FKConvexElem::SetMirroredConvexMesh(physx::PxConvexMesh* InMesh)
{
	ConvexMeshNegX = InMesh;
}
#endif

float FKConvexElem::GetVolume(const FVector& Scale) const
{
	float Volume = 0.0f;

#if PHYSICS_INTERFACE_PHYSX
	if (ConvexMesh != NULL)
	{
		// Preparation for convex mesh scaling implemented in another changelist
		FTransform ScaleTransform = FTransform(FQuat::Identity, FVector::ZeroVector, Scale);

		int32 NumPolys = ConvexMesh->getNbPolygons();
		PxHullPolygon PolyData;

		const PxVec3* Vertices = ConvexMesh->getVertices();
		const PxU8* Indices = ConvexMesh->getIndexBuffer();

		for (int32 PolyIdx = 0; PolyIdx < NumPolys; ++PolyIdx)
		{
			if (ConvexMesh->getPolygonData(PolyIdx, PolyData))
			{
				for (int32 VertIdx = 2; VertIdx < PolyData.mNbVerts; ++ VertIdx)
				{
					// Grab triangle indices that we hit
					int32 I0 = Indices[PolyData.mIndexBase + 0];
					int32 I1 = Indices[PolyData.mIndexBase + (VertIdx - 1)];
					int32 I2 = Indices[PolyData.mIndexBase + VertIdx];


					Volume += SignedVolumeOfTriangle(ScaleTransform.TransformPosition(P2UVector(Vertices[I0])), 
						ScaleTransform.TransformPosition(P2UVector(Vertices[I1])), 
						ScaleTransform.TransformPosition(P2UVector(Vertices[I2])));
				}
			}
		}
	}
#elif WITH_CHAOS
	//TODO Support ChaosConvex.
	CHAOS_ENSURE(false);
#endif

	return Volume;
}

#if WITH_CHAOS

void FKConvexElem::SetChaosConvexMesh(TSharedPtr<Chaos::FConvex, ESPMode::ThreadSafe>&& InChaosConvex)
{
	ChaosConvex = MoveTemp(InChaosConvex);
	
	const bool bForceCompute = true;
	ComputeChaosConvexIndices(bForceCompute);
}

void FKConvexElem::ResetChaosConvexMesh()
{
	ChaosConvex.Reset();
}

ENGINE_API void FKConvexElem::ComputeChaosConvexIndices(bool bForceCompute)
{
	if (bForceCompute || IndexData.Num() == 0)
	{
		IndexData = GetChaosConvexIndices();
	}
}

TArray<int32> FKConvexElem::GetChaosConvexIndices() const
{
	TArray<int32> ResultIndexData;
	const int32 NumVerts = VertexData.Num();
	if (NumVerts > 0)
	{
		TArray<Chaos::FVec3> ConvexVertices;
		ConvexVertices.SetNum(NumVerts);

		for (int32 VertIndex = 0; VertIndex < NumVerts; ++VertIndex)
		{
			ConvexVertices[VertIndex] = VertexData[VertIndex];
		}

		TArray<Chaos::TVec3<int32>> Triangles;
		Chaos::FConvexBuilder::Params BuildParams;
		BuildParams.HorizonEpsilon = Chaos::FConvexBuilder::SuggestEpsilon(ConvexVertices);
		Chaos::FConvexBuilder::BuildConvexHull(ConvexVertices, Triangles, BuildParams);

		ResultIndexData.Reserve(Triangles.Num() * 3);
		for (Chaos::TVec3<int32> Tri : Triangles)
		{
			ResultIndexData.Add(Tri[0]);
			ResultIndexData.Add(Tri[1]);
			ResultIndexData.Add(Tri[2]);
		}
	}

	return ResultIndexData;
}
#endif

#if WITH_EDITORONLY_DATA
void FKSphereElem::FixupDeprecated( FArchive& Ar )
{
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_REFACTOR_PHYSICS_TRANSFORMS )
	{
		Center = TM_DEPRECATED.GetOrigin();
	}
}
#endif

float FKSphereElem::GetShortestDistanceToPoint(const FVector& WorldPosition, const FTransform& LocalToWorldTM) const
{
	FKSphereElem ScaledSphere = GetFinalScaled(LocalToWorldTM.GetScale3D(), FTransform::Identity);

	const FVector Dir = LocalToWorldTM.TransformPositionNoScale(ScaledSphere.Center) - WorldPosition;
	const float DistToCenter = Dir.Size();
	const float DistToEdge = DistToCenter - ScaledSphere.Radius;
	
	return DistToEdge > SMALL_NUMBER ? DistToEdge : 0.f;
}

float FKSphereElem::GetClosestPointAndNormal(const FVector& WorldPosition, const FTransform& LocalToWorldTM, FVector& ClosestWorldPosition, FVector& Normal) const
{
	FKSphereElem ScaledSphere = GetFinalScaled(LocalToWorldTM.GetScale3D(), FTransform::Identity);

	const FVector Dir = LocalToWorldTM.TransformPositionNoScale(ScaledSphere.Center) - WorldPosition;
	const float DistToCenter = Dir.Size();
	const float DistToEdge = FMath::Max(DistToCenter - ScaledSphere.Radius, 0.f);

	if(DistToCenter > SMALL_NUMBER)
	{
		Normal = -Dir.GetUnsafeNormal();
	}
	else
	{
		Normal = FVector::ZeroVector;
	}
	
	ClosestWorldPosition = WorldPosition - Normal*DistToEdge;

	return DistToEdge;
}

void FKSphereElem::ScaleElem(FVector DeltaSize, float MinSize)
{
	// Find element with largest magnitude, btu preserve sign.
	float DeltaRadius = DeltaSize.X;
	if (FMath::Abs(DeltaSize.Y) > FMath::Abs(DeltaRadius))
		DeltaRadius = DeltaSize.Y;
	else if (FMath::Abs(DeltaSize.Z) > FMath::Abs(DeltaRadius))
		DeltaRadius = DeltaSize.Z;

	Radius = FMath::Max(Radius + DeltaRadius, MinSize);
}

FKSphereElem FKSphereElem::GetFinalScaled(const FVector& Scale3D, const FTransform& RelativeTM) const
{
	float MinScale, MinScaleAbs;
	FVector Scale3DAbs;

	SetupNonUniformHelper(Scale3D * RelativeTM.GetScale3D(), MinScale, MinScaleAbs, Scale3DAbs);

	FKSphereElem ScaledSphere = *this;
	ScaledSphere.Radius *= MinScaleAbs;

	ScaledSphere.Center = RelativeTM.TransformPosition(Center) * Scale3D;


	return ScaledSphere;
}

#if WITH_EDITORONLY_DATA
void FKBoxElem::FixupDeprecated( FArchive& Ar )
{
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_REFACTOR_PHYSICS_TRANSFORMS )
	{
		Center = TM_DEPRECATED.GetOrigin();
		Orientation_DEPRECATED = TM_DEPRECATED.ToQuat();
	}

	Ar.UsingCustomVersion(FAnimPhysObjectVersion::GUID);
	if (Ar.IsLoading() && Ar.CustomVer(FAnimPhysObjectVersion::GUID) < FAnimPhysObjectVersion::BoxSphylElemsUseRotators)
	{
		Rotation = Orientation_DEPRECATED.Rotator();
	}
}
#endif

void FKBoxElem::ScaleElem(FVector DeltaSize, float MinSize)
{
	// Sizes are lengths, so we double the delta to get similar increase in size.
	X = FMath::Max(X + 2 * DeltaSize.X, MinSize);
	Y = FMath::Max(Y + 2 * DeltaSize.Y, MinSize);
	Z = FMath::Max(Z + 2 * DeltaSize.Z, MinSize);
}


FKBoxElem FKBoxElem::GetFinalScaled(const FVector& Scale3D, const FTransform& RelativeTM) const
{
	float MinScale, MinScaleAbs;
	FVector Scale3DAbs;

	SetupNonUniformHelper(Scale3D * RelativeTM.GetScale3D(), MinScale, MinScaleAbs, Scale3DAbs);

	FKBoxElem ScaledBox = *this;
	ScaledBox.X *= Scale3DAbs.X;
	ScaledBox.Y *= Scale3DAbs.Y;
	ScaledBox.Z *= Scale3DAbs.Z;

	FTransform BoxTransform = GetTransform() * RelativeTM;
	BoxTransform.ScaleTranslation(Scale3D);
	ScaledBox.SetTransform(BoxTransform);

	return ScaledBox;
}

float FKBoxElem::GetShortestDistanceToPoint(const FVector& WorldPosition, const FTransform& BoneToWorldTM) const
{
	const FKBoxElem& ScaledBox = GetFinalScaled(BoneToWorldTM.GetScale3D(), FTransform::Identity);
	const FTransform LocalToWorldTM = GetTransform() * BoneToWorldTM;
	const FVector LocalPosition = LocalToWorldTM.InverseTransformPositionNoScale(WorldPosition);
	const FVector LocalPositionAbs = LocalPosition.GetAbs();

	const FVector HalfPoint(ScaledBox.X*0.5f, ScaledBox.Y*0.5f, ScaledBox.Z*0.5f);
	const FVector Delta = LocalPositionAbs - HalfPoint;
	const FVector Errors = FVector(FMath::Max(Delta.X, 0.f), FMath::Max(Delta.Y, 0.f), FMath::Max(Delta.Z, 0.f));
	const float Error = Errors.Size();

	return Error > SMALL_NUMBER ? Error : 0.f;
}

float FKBoxElem::GetClosestPointAndNormal(const FVector& WorldPosition, const FTransform& BoneToWorldTM, FVector& ClosestWorldPosition, FVector& Normal) const
{
	const FKBoxElem& ScaledBox = GetFinalScaled(BoneToWorldTM.GetScale3D(), FTransform::Identity);
	const FTransform LocalToWorldTM = GetTransform() * BoneToWorldTM;
	const FVector LocalPosition = LocalToWorldTM.InverseTransformPositionNoScale(WorldPosition);

	const float HalfX = ScaledBox.X * 0.5f;
	const float HalfY = ScaledBox.Y * 0.5f;
	const float HalfZ = ScaledBox.Z * 0.5f;
	
	const FVector ClosestLocalPosition(FMath::Clamp(LocalPosition.X, -HalfX, HalfX), FMath::Clamp(LocalPosition.Y, -HalfY, HalfY), FMath::Clamp(LocalPosition.Z, -HalfZ, HalfZ));
	ClosestWorldPosition = LocalToWorldTM.TransformPositionNoScale(ClosestLocalPosition);

	const FVector LocalDelta = LocalPosition - ClosestLocalPosition;
	float Error = LocalDelta.Size();
	
	bool bIsOutside = Error > SMALL_NUMBER;
	
	const FVector LocalNormal = bIsOutside ? LocalDelta.GetUnsafeNormal() : FVector::ZeroVector;

	ClosestWorldPosition = LocalToWorldTM.TransformPositionNoScale(ClosestLocalPosition);
	Normal = LocalToWorldTM.TransformVectorNoScale(LocalNormal);
	
	return bIsOutside ? Error : 0.f;
}

#if WITH_EDITORONLY_DATA
void FKSphylElem::FixupDeprecated( FArchive& Ar )
{
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_REFACTOR_PHYSICS_TRANSFORMS )
	{
		Center = TM_DEPRECATED.GetOrigin();
		Orientation_DEPRECATED = TM_DEPRECATED.ToQuat();
	}

	Ar.UsingCustomVersion(FAnimPhysObjectVersion::GUID);
	if (Ar.IsLoading() && Ar.CustomVer(FAnimPhysObjectVersion::GUID) < FAnimPhysObjectVersion::BoxSphylElemsUseRotators)
	{
		Rotation = Orientation_DEPRECATED.Rotator();
	}
}
#endif

void FKSphylElem::ScaleElem(FVector DeltaSize, float MinSize)
{
	float DeltaRadius = DeltaSize.X;
	if (FMath::Abs(DeltaSize.Y) > FMath::Abs(DeltaRadius))
	{
		DeltaRadius = DeltaSize.Y;
	}

	float DeltaHeight = DeltaSize.Z;
	float radius = FMath::Max(Radius + DeltaRadius, MinSize);
	float length = Length + DeltaHeight;

	length += Radius - radius;
	length = FMath::Max(0.f, length);

	Radius = radius;
	Length = length;
}

FKSphylElem FKSphylElem::GetFinalScaled(const FVector& Scale3D, const FTransform& RelativeTM) const
{
	FKSphylElem ScaledSphylElem = *this;

	float MinScale, MinScaleAbs;
	FVector Scale3DAbs;

	SetupNonUniformHelper(Scale3D * RelativeTM.GetScale3D(), MinScale, MinScaleAbs, Scale3DAbs);

	ScaledSphylElem.Radius = GetScaledRadius(Scale3DAbs);
	ScaledSphylElem.Length = GetScaledCylinderLength(Scale3DAbs);

	FVector LocalOrigin = RelativeTM.TransformPosition(Center) * Scale3D;
	ScaledSphylElem.Center = LocalOrigin;
	ScaledSphylElem.Rotation = FRotator(RelativeTM.GetRotation() * FQuat(ScaledSphylElem.Rotation));

	return ScaledSphylElem;
}

float FKSphylElem::GetScaledRadius(const FVector& Scale3D) const
{
	const FVector Scale3DAbs = Scale3D.GetAbs();
	const float RadiusScale = FMath::Max(Scale3DAbs.X, Scale3DAbs.Y);
	return FMath::Clamp(Radius * RadiusScale, 0.1f, GetScaledHalfLength(Scale3DAbs));
}

float FKSphylElem::GetScaledCylinderLength(const FVector& Scale3D) const
{
	return FMath::Max(0.1f, (GetScaledHalfLength(Scale3D) - GetScaledRadius(Scale3D)) * 2.f);
}

float FKSphylElem::GetScaledHalfLength(const FVector& Scale3D) const
{
	return FMath::Max((Length + Radius * 2.0f) * FMath::Abs(Scale3D.Z) * 0.5f, 0.1f);
}

float FKSphylElem::GetShortestDistanceToPoint(const FVector& WorldPosition, const FTransform& BoneToWorldTM) const
{
	const FKSphylElem ScaledSphyl = GetFinalScaled(BoneToWorldTM.GetScale3D(), FTransform::Identity);

	const FTransform LocalToWorldTM = GetTransform() * BoneToWorldTM;
	const FVector ErrorScale = LocalToWorldTM.GetScale3D();
	const FVector LocalPosition = LocalToWorldTM.InverseTransformPositionNoScale(WorldPosition);
	const FVector LocalPositionAbs = LocalPosition.GetAbs();
	
	
	const FVector Target(LocalPositionAbs.X, LocalPositionAbs.Y, FMath::Max(LocalPositionAbs.Z - ScaledSphyl.Length * 0.5f, 0.f));	//If we are above half length find closest point to cap, otherwise to cylinder
	const float Error = FMath::Max(Target.Size() - ScaledSphyl.Radius, 0.f);

	return Error > SMALL_NUMBER ? Error : 0.f;
}

float FKSphylElem::GetClosestPointAndNormal(const FVector& WorldPosition, const FTransform& BoneToWorldTM, FVector& ClosestWorldPosition, FVector& Normal) const
{
	const FKSphylElem ScaledSphyl = GetFinalScaled(BoneToWorldTM.GetScale3D(), FTransform::Identity);

	const FTransform LocalToWorldTM = GetTransform() * BoneToWorldTM;
	const FVector ErrorScale = LocalToWorldTM.GetScale3D();
	const FVector LocalPosition = LocalToWorldTM.InverseTransformPositionNoScale(WorldPosition);
	
	const float HalfLength = 0.5f * ScaledSphyl.Length;
	const float TargetZ = FMath::Clamp(LocalPosition.Z, -HalfLength, HalfLength);	//We want to move to a sphere somewhere along the capsule axis

	const FVector WorldSphere = LocalToWorldTM.TransformPositionNoScale(FVector(0.f, 0.f, TargetZ));
	const FVector Dir = WorldSphere - WorldPosition;
	const float DistToCenter = Dir.Size();
	const float DistToEdge = FMath::Max(DistToCenter - ScaledSphyl.Radius, 0.f);

	bool bIsOutside = DistToCenter > SMALL_NUMBER;
	if (bIsOutside)
	{
		Normal = -Dir.GetUnsafeNormal();
	}
	else
	{
		Normal = FVector::ZeroVector;
	}

	ClosestWorldPosition = WorldPosition - Normal*DistToEdge;

	return bIsOutside ? DistToEdge : 0.f;
}

void FKTaperedCapsuleElem::ScaleElem(FVector DeltaSize, float MinSize)
{
	float DeltaRadius0 = DeltaSize.X;
	float DeltaRadius1 = DeltaSize.Y;

	float DeltaHeight = DeltaSize.Z;
	float radius0 = FMath::Max(Radius0 + DeltaRadius0, MinSize);
	float radius1 = FMath::Max(Radius1 + DeltaRadius1, MinSize);
	float length = Length + DeltaHeight;

	length += ((Radius1 - radius1) + (Radius0 - radius0)) * 0.5f;
	length = FMath::Max(0.f, length);

	Radius0 = radius0;
	Radius1 = radius1;
	Length = length;
}

FKTaperedCapsuleElem FKTaperedCapsuleElem::GetFinalScaled(const FVector& Scale3D, const FTransform& RelativeTM) const
{
	FKTaperedCapsuleElem ScaledTaperedCapsuleElem = *this;

	float MinScale, MinScaleAbs;
	FVector Scale3DAbs;

	SetupNonUniformHelper(Scale3D * RelativeTM.GetScale3D(), MinScale, MinScaleAbs, Scale3DAbs);

	GetScaledRadii(Scale3DAbs, ScaledTaperedCapsuleElem.Radius0, ScaledTaperedCapsuleElem.Radius1);
	ScaledTaperedCapsuleElem.Length = GetScaledCylinderLength(Scale3DAbs);

	FVector LocalOrigin = RelativeTM.TransformPosition(Center) * Scale3D;
	ScaledTaperedCapsuleElem.Center = LocalOrigin;
	ScaledTaperedCapsuleElem.Rotation = FRotator(RelativeTM.GetRotation() * FQuat(ScaledTaperedCapsuleElem.Rotation));

	return ScaledTaperedCapsuleElem;
}

void FKTaperedCapsuleElem::GetScaledRadii(const FVector& Scale3D, float& OutRadius0, float& OutRadius1) const
{
	const FVector Scale3DAbs = Scale3D.GetAbs();
	const float RadiusScale = FMath::Max(Scale3DAbs.X, Scale3DAbs.Y);
	OutRadius0 = FMath::Clamp(Radius0 * RadiusScale, 0.1f, GetScaledHalfLength(Scale3DAbs));
	OutRadius1 = FMath::Clamp(Radius1 * RadiusScale, 0.1f, GetScaledHalfLength(Scale3DAbs));
}

float FKTaperedCapsuleElem::GetScaledCylinderLength(const FVector& Scale3D) const
{
	float ScaledRadius0, ScaledRadius1;
	GetScaledRadii(Scale3D, ScaledRadius0, ScaledRadius1);
	return FMath::Max(0.1f, ((GetScaledHalfLength(Scale3D) * 2.0f) - (ScaledRadius0 + ScaledRadius1)));
}

float FKTaperedCapsuleElem::GetScaledHalfLength(const FVector& Scale3D) const
{
	return FMath::Max((Length + Radius0 + Radius1) * FMath::Abs(Scale3D.Z) * 0.5f, 0.1f);
}

class UPhysicalMaterial* UBodySetup::GetPhysMaterial() const
{
	UPhysicalMaterial* PhysMat = PhysMaterial;

	if (PhysMat == NULL && GEngine != NULL)
	{
		PhysMat = GEngine->DefaultPhysMaterial;
	}
	return PhysMat;
}

float UBodySetup::CalculateMass(const UPrimitiveComponent* Component) const
{
	FVector ComponentScale(1.0f, 1.0f, 1.0f);
	const FBodyInstance* BodyInstance = &DefaultInstance;
	float MassScale = DefaultInstance.MassScale;

	const UPrimitiveComponent* OuterComp = Component != NULL ? Component : Cast<UPrimitiveComponent>(GetOuter());
	if (OuterComp)
	{
		ComponentScale = OuterComp->GetComponentScale();

		BodyInstance = &OuterComp->BodyInstance;

		const USkinnedMeshComponent* SkinnedMeshComp = Cast<const USkinnedMeshComponent>(OuterComp);
		if (SkinnedMeshComp != NULL)
		{
			const FBodyInstance* Body = SkinnedMeshComp->GetBodyInstance(BoneName);

			if (Body != NULL)
			{
				BodyInstance = Body;
			}
		}
	}

	if(BodyInstance->bOverrideMass)
	{
		return BodyInstance->GetMassOverride();
	}

	UPhysicalMaterial* PhysMat = BodyInstance->GetSimplePhysicalMaterial();
	MassScale = BodyInstance->MassScale;

	// physical material - nothing can weigh less than hydrogen (0.09 kg/m^3)
	float DensityKGPerCubicUU = 1.0f;
	float RaiseMassToPower = 0.75f;
	if (PhysMat)
	{
		DensityKGPerCubicUU = FMath::Max(0.00009f, PhysMat->Density * 0.001f);
		RaiseMassToPower = PhysMat->RaiseMassToPower;
	}

	// Then scale mass to avoid big differences between big and small objects.
	const float BasicVolume = GetVolume(ComponentScale);
	//@TODO: Some static meshes are triggering this - disabling until content can be analyzed - ensureMsgf(BasicVolume >= 0.0f, TEXT("UBodySetup::CalculateMass(%s) - The volume of the aggregate geometry is negative"), *Component->GetReadableName());

	const float BasicMass = FMath::Max<float>(BasicVolume, 0.0f) * DensityKGPerCubicUU;

	const float UsePow = FMath::Clamp<float>(RaiseMassToPower, KINDA_SMALL_NUMBER, 1.f);
	const float RealMass = FMath::Pow(BasicMass, UsePow);

	return RealMass * MassScale;
}

float UBodySetup::GetVolume(const FVector& Scale) const
{
	return AggGeom.GetVolume(Scale);
}

#undef LOCTEXT_NAMESPACE
