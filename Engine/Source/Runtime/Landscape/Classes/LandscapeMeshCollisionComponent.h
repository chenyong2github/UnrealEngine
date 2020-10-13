// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Templates/RefCounting.h"
#include "EngineDefines.h"
#include "Serialization/BulkData.h"
#include "LandscapeHeightfieldCollisionComponent.h"

#include "Chaos/PhysicalMaterials.h"

#include "LandscapeMeshCollisionComponent.generated.h"

class UPhysicalMaterial;
struct FNavigableGeometryExport;

#if WITH_PHYSX
namespace physx
{
	class PxMaterial;
	class PxTriangleMesh;
}
#elif WITH_CHAOS
namespace Chaos
{
	class FTriangleMeshImplicitObject;
}
#endif



UCLASS()
class ULandscapeMeshCollisionComponent : public ULandscapeHeightfieldCollisionComponent
{
	GENERATED_BODY()

public:

	ULandscapeMeshCollisionComponent();
	virtual ~ULandscapeMeshCollisionComponent();

	// Keep the possibility to share projected height field PhysX object with editor mesh collision objects...

	/** Guid used to share PhysX heightfield objects in the editor */
	UPROPERTY()
	FGuid MeshGuid;

	struct FTriMeshGeometryRef : public FRefCountedObject
	{
		FGuid Guid;

#if WITH_PHYSX
		/** List of PxMaterials used on this landscape */
		TArray<physx::PxMaterial*>	UsedPhysicalMaterialArray;
		physx::PxTriangleMesh*		RBTriangleMesh;
#if WITH_EDITOR
		physx::PxTriangleMesh*		RBTriangleMeshEd; // Used only by landscape editor, does not have holes in it
#endif	//WITH_EDITOR
#endif	//WITH_PHYSX

#if WITH_CHAOS
		TArray<Chaos::FMaterialHandle> UsedChaosMaterials;
		TUniquePtr<Chaos::FTriangleMeshImplicitObject> Trimesh;
#if WITH_EDITOR
		TUniquePtr<Chaos::FTriangleMeshImplicitObject> EditorTrimesh;
#endif // WITH_EDITOR
#endif // WITH_CHAOS

		FTriMeshGeometryRef();
		FTriMeshGeometryRef(FGuid& InGuid);
		virtual ~FTriMeshGeometryRef();
	};

#if WITH_EDITORONLY_DATA
	/** The collision mesh values. */
	FWordBulkData								CollisionXYOffsetData; //  X, Y Offset in raw format...
#endif //WITH_EDITORONLY_DATA

	/** Physics engine version of heightfield data. */
	TRefCountPtr<FTriMeshGeometryRef>			MeshRef;

	//~ Begin UActorComponent Interface.
protected:
	virtual void OnCreatePhysicsState() override;
public:
	virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;
	//~ End UActorComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual void DestroyComponent(bool bPromoteChildren = false) override;
	//~ End USceneComponent Interface.

	//~ Begin UPrimitiveComponent Interface
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const override;
	//End UPrimitiveComponent interface

	//~ Begin UObject Interface.
	virtual void Serialize(FArchive& Ar) override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void ExportCustomProperties(FOutputDevice& Out, uint32 Indent) override;
	virtual void ImportCustomProperties(const TCHAR* SourceText, FFeedbackContext* Warn) override;

	virtual bool CookCollisionData(const FName& Format, bool bUseOnlyDefMaterial, bool bCheckDDC, TArray<uint8>& OutCookedData, TArray<UPhysicalMaterial*>& InOutMaterials) const override;
	virtual uint32 ComputeCollisionHash() const override { return 0; }
#endif
	//~ End UObject Interface.

	//~ Begin ULandscapeHeightfieldCollisionComponent Interface
	virtual void CreateCollisionObject() override;
	virtual bool RecreateCollision() override;
	//~ End ULandscapeHeightfieldCollisionComponent Interface
};
