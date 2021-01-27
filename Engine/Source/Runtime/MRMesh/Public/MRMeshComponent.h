// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "PackedNormal.h"
#include "MRMeshBufferDefines.h"

#include "MRMeshComponent.generated.h"

class UMaterial;
class FBaseMeshReconstructorModule;
class UMeshReconstructorBase;
struct FDynamicMeshVertex;

DECLARE_STATS_GROUP(TEXT("MRMesh"), STATGROUP_MRMESH, STATCAT_Advanced);
DEFINE_LOG_CATEGORY_STATIC(LogMrMesh, Warning, All);


//@todo JoeG - remove
#ifndef PLATFORM_HOLOLENS
#define PLATFORM_HOLOLENS 0
#endif

class IMRMesh
{
public:
	struct FBrickDataReceipt
	{
		// Optionally subclass and use receipt.  For example: to release the buffers FSendBrickDataArgs has references to.
		virtual ~FBrickDataReceipt() {}
	};

	typedef uint64 FBrickId;
	struct FSendBrickDataArgs
	{
		TSharedPtr<FBrickDataReceipt, ESPMode::ThreadSafe> BrickDataReceipt;
		const FBrickId BrickId;
		const TArray<FVector>& PositionData;
		const TArray<FVector2D>& UVData;
		const TArray<FPackedNormal>& TangentXZData;
		const TArray<FColor>& ColorData;
		const TArray<MRMESH_INDEX_TYPE>& Indices;
		const FBox Bounds = FBox(EForceInit::ForceInit);
	};

	virtual void SetConnected(bool value) = 0;
	virtual bool IsConnected() const = 0;

	virtual void SendRelativeTransform(const FTransform& Transform) = 0;
	virtual void SendBrickData(FSendBrickDataArgs Args) = 0;
	virtual void Clear() = 0;
	virtual void ClearAllBrickData() = 0;
};


DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMRMeshBrickDataUpdatedDelegate, const UMRMeshComponent*, const IMRMesh::FSendBrickDataArgs&);

UCLASS(hideCategories=(Physics), meta = (BlueprintSpawnableComponent, Experimental), ClassGroup = Rendering)
class MRMESH_API UMRMeshComponent : public UPrimitiveComponent, public IMRMesh, public IInterface_CollisionDataProvider
{
public:
	friend class FMRMeshProxy;

	GENERATED_UCLASS_BODY()
	
	//~ IInterface_CollisionDataProvider
	virtual bool GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData) override;
	virtual bool ContainsPhysicsTriMeshData(bool InUseAllTriData) const override;
	//~ IInterface_CollisionDataProvider

	virtual void BeginPlay() override;
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Mesh Reconstruction")
	bool IsConnected() const override { return bConnected; }

	void SetConnected(bool value) override { bConnected = value; }
	virtual void SendRelativeTransform(const FTransform& Transform) override { SetRelativeTransform(Transform); }

	UFUNCTION(BlueprintCallable, Category = "Mesh Reconstruction")
	void ForceNavMeshUpdate();

	UFUNCTION(BlueprintCallable, Category = "Mesh Reconstruction")
	void Clear() override;

	// UPrimitiveComponent.. public BP function needs to stay public to avoid nativization errors. (RR)
	virtual void SetMaterial(int32 ElementIndex, class UMaterialInterface* InMaterial) override;
	virtual class UMaterialInterface* GetMaterial(int32 ElementIndex) const override;

	// Set the wireframe material.
	UFUNCTION(BlueprintCallable, Category = "Mesh Reconstruction")
	virtual void SetWireframeMaterial(class UMaterialInterface* InMaterial);

	/** Updates from HoloLens or iOS */
	void UpdateMesh(const FVector& InLocation, const FQuat& InRotation, const FVector& Scale, TArray<FVector>& Vertices, TArray<MRMESH_INDEX_TYPE>& Indices, TArray<FVector2D> UVData = {}, TArray<FPackedNormal> TangentXZData = {}, TArray<FColor> ColorData = {});

	UFUNCTION(BlueprintCallable, Category = "Mesh Reconstruction")
	void SetEnableMeshOcclusion(bool bEnable);
	
	UFUNCTION(BlueprintPure, Category = "Mesh Reconstruction")
	bool GetEnableMeshOcclusion() const { return bEnableOcclusion; }
	
	UFUNCTION(BlueprintCallable, Category = "Mesh Reconstruction")
	void SetUseWireframe(bool bUseWireframe);
	
	UFUNCTION(BlueprintPure, Category = "Mesh Reconstruction")
	bool GetUseWireframe() const { return bUseWireframe; }
	
	UFUNCTION(BlueprintCallable, Category = "Mesh Reconstruction")
	void SetWireframeColor(const FLinearColor& InColor);
	
	UFUNCTION(BlueprintPure, Category = "Mesh Reconstruction")
	const FLinearColor& GetWireframeColor() const { return WireframeColor; }
	
	UMaterialInterface* GetMaterialToUse() const;
	
	FOnMRMeshBrickDataUpdatedDelegate& OnBrickDataUpdated() { return OnBrickDataUpdatedDelegate; }

protected:
	virtual void OnActorEnableCollisionChanged() override;
	virtual void UpdatePhysicsToRBChannels() override;
public:
	virtual void SetCollisionObjectType(ECollisionChannel Channel) override;
	virtual void SetCollisionResponseToChannel(ECollisionChannel Channel, ECollisionResponse NewResponse) override;
	virtual void SetCollisionResponseToAllChannels(ECollisionResponse NewResponse) override;
	virtual void SetCollisionResponseToChannels(const FCollisionResponseContainer& NewResponses) override;
	virtual void SetCollisionEnabled(ECollisionEnabled::Type NewType) override;
	virtual void SetCollisionProfileName(FName InCollisionProfileName, bool bUpdateOverlaps=true) override;

	virtual void SetWalkableSlopeOverride(const FWalkableSlopeOverride& NewOverride) override;

	void SetNeverCreateCollisionMesh(bool bNeverCreate) { bNeverCreateCollisionMesh = bNeverCreate; }
	void SetEnableNavMesh(bool bEnable) { bUpdateNavMeshOnMeshUpdate = bEnable;  }

	/** Trackers feeding mesh data to this component may want to know when we clear our mesh data */
	DECLARE_EVENT(UMRMeshComponent, FOnClear);
	FOnClear& OnClear() { return OnClearEvent; }

private:
	//~ UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const override;
	virtual class UBodySetup* GetBodySetup();
	//~ UPrimitiveComponent
	//~ UActorComponent
	virtual bool ShouldCreatePhysicsState() const override;
	virtual void SendRenderDynamicData_Concurrent() override;
	//~ UActorComponent

	//~ IMRMesh
	virtual void SendBrickData(FSendBrickDataArgs Args) override;
	virtual void ClearAllBrickData() override;
	//~ IMRMesh

private:
	void CacheBodySetupHelper();
	class UBodySetup* CreateBodySetupHelper();
	void SendBrickData_Internal(IMRMesh::FSendBrickDataArgs Args);

	void RemoveBodyInstance(int32 BodyIndex);
	void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport) override;

	void ClearAllBrickData_Internal();

	UPROPERTY(EditAnywhere, Category = Appearance)
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, Category = Appearance)
	TObjectPtr<UMaterialInterface> WireframeMaterial;

	/** If true, MRMesh will create a renderable mesh proxy.  If false it will not, but could still provide collision. */
	UPROPERTY(EditAnywhere, Category = Appearance)
	bool bCreateMeshProxySections = true;

	/** If true, MRMesh will automatically update its navmesh whenever any Mesh section is updated. This may be expensive. If this is disabled use ForceNavMeshUpdate to trigger a navmesh update when necessary.  Moving the component will also trigger a navmesh update.*/
	UPROPERTY(EditAnywhere, Category = MRMesh)
	bool bUpdateNavMeshOnMeshUpdate = true;

	/** If true, MRMesh will not create a collidable ridgid body for each mesh section and can therefore never have collision.  Avoids the cost of generating collision.*/
	UPROPERTY(EditAnywhere, Category = MRMesh)
	bool bNeverCreateCollisionMesh = false;

	bool bConnected = false;

	UPROPERTY(Transient)
	TObjectPtr<class UBodySetup> CachedBodySetup = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBodySetup>> BodySetups;


	/** Whether this mesh should write z-depth to occlude meshes or not */
	bool bEnableOcclusion;
	/** Whether this mesh should draw using the wireframe material when no material is set or not */
	bool bUseWireframe;

	TArray<FBodyInstance*> BodyInstances;
	TArray<IMRMesh::FBrickId> BodyIds;

	FOnClear OnClearEvent;
	
	FLinearColor WireframeColor = FLinearColor::White;
	
	/** Temporarily saved data pointers used for constructing the collision mesh */
	const TArray<FVector>* TempPosition = nullptr;
	const TArray<MRMESH_INDEX_TYPE>* TempIndices = nullptr;
	
	FOnMRMeshBrickDataUpdatedDelegate OnBrickDataUpdatedDelegate;
};
