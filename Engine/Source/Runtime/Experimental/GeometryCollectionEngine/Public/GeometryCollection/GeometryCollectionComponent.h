// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/MeshComponent.h"
#include "Chaos/ChaosSolverActor.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Defines.h"
#include "Field/FieldSystem.h"
#include "Field/FieldSystemActor.h"
#include "Field/FieldSystemNodes.h"
#include "Field/FieldSystemObjects.h"
#include "GameFramework/Actor.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "GeometryCollection/GeometryCollectionSimulationCoreTypes.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "GeometryCollection/GeometryDynamicCollection.h"
#include "GeometryCollectionObject.h"
#include "GeometryCollectionEditorSelection.h"
#include "GeometryCollection/RecordedTransformTrack.h"
#include "Templates/UniquePtr.h"
#include "PhysicalMaterials/Experimental/ChaosPhysicalMaterial.h"
#include "Chaos/ChaosGameplayEventDispatcher.h"
#include "Chaos/ChaosNotifyHandlerInterface.h"
#include "Chaos/ChaosSolverComponentTypes.h"

#include "GeometryCollectionComponent.generated.h"

struct FGeometryCollectionConstantData;
struct FGeometryCollectionDynamicData;
class FGeometryCollectionPhysicsProxy;
class UGeometryCollectionComponent;
class UBoxComponent;
class UGeometryCollectionCache;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChaosBreakEvent, const FChaosBreakEvent&, BreakEvent);

namespace GeometryCollection
{
	enum class ESelectionMode : uint8
	{
		None = 0,
		AllGeometry,
		InverseGeometry,
		Siblings,
		Neighbors,
		AllInCluster
	};
}

USTRUCT()
struct FGeomComponentCacheParameters
{
	GENERATED_BODY()

	FGeomComponentCacheParameters();

	// Cache mode, whether disabled, playing or recording
	UPROPERTY(EditAnywhere, Category = Cache)
	EGeometryCollectionCacheType CacheMode;

	// The cache to target when recording or playing
	UPROPERTY(EditAnywhere, Category = Cache)
	UGeometryCollectionCache* TargetCache;

	// Cache mode, whether disabled, playing or recording
	UPROPERTY(EditAnywhere, Category = Cache)
	float ReverseCacheBeginTime;

	// Whether to buffer collisions during recording
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Record Collision Data"))
	bool SaveCollisionData;

	// Whether to generate collisions during playback
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Generate Collision Data during Playback"))
	bool DoGenerateCollisionData;

	// Maximum size of the collision buffer
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Collision Data Size Maximum"))
	int32 CollisionDataSizeMax;

	// Spatial hash collision data
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Spatial Hash Collision Data"))
	bool DoCollisionDataSpatialHash;

	// Spatial hash radius for collision data
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Spatial Hash Radius"))
	float CollisionDataSpatialHashRadius;

	// Maximum number of collisions per cell
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Maximum Number of Collisions Per Cell"))
	int32 MaxCollisionPerCell;

	// Whether to buffer breakings during recording
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Record Breaking Data"))
	bool SaveBreakingData;

	// Whether to generate breakings during playback
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Generate Breaking Data during Playback"))
	bool DoGenerateBreakingData;

	// Maximum size of the breaking buffer
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Breaking Data Size Maximum"))
	int32 BreakingDataSizeMax;

	// Spatial hash breaking data
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Spatial Hash Breaking Data"))
	bool DoBreakingDataSpatialHash;

	// Spatial hash radius for breaking data
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Spatial Hash Radius"))
	float BreakingDataSpatialHashRadius;

	// Maximum number of breaking per cell
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Maximum Number of Breakings Per Cell"))
	int32 MaxBreakingPerCell;

	// Whether to buffer trailings during recording
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Record Trailing Data"))	
	bool SaveTrailingData;

	// Whether to generate trailings during playback
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Generate Trailing Data during Playback"))
	bool DoGenerateTrailingData;

	// Maximum size of the trailing buffer
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Trailing Data Size Maximum"))
	int32 TrailingDataSizeMax;

	// Minimum speed to record trailing
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Trailing Minimum Speed Threshold"))
	float TrailingMinSpeedThreshold;

	// Minimum volume to record trailing
	UPROPERTY(EditAnywhere, Category = Cache, meta = (DisplayName = "Trailing Minimum Volume Threshold"))
	float TrailingMinVolumeThreshold;
};

namespace GeometryCollection
{
	/** Type of updates used at the end of an edit operation. */
	enum class EEditUpdate : uint8
	{
		/** No update. */
		None = 0,
		/** Mark the rest collection as changed. */
		Rest = 1,
		/** Recreate the physics state (proxy). */
		Physics = 2,
		/** Reset the dynamic collection. */
		Dynamic = 4,
		/** Mark the rest collection as changed, and recreate the physics state (proxy). */
		RestPhysics = Rest | Physics,
		/** Reset dynamic collection, mark the rest collection as changed, and recreate the physics state (proxy). */
		RestPhysicsDynamic = Rest | Physics | Dynamic,
	};
	ENUM_CLASS_FLAGS(EEditUpdate);
}

/**
*	FGeometryCollectionEdit
*     Structured RestCollection access where the scope
*     of the object controls serialization back into the
*     dynamic collection
*
*	This will force any simulating geometry collection out of the
*	solver so it can be edited and afterwards will recreate the proxy
*	The update can also be specified to reset the dynamic collection
*/
class GEOMETRYCOLLECTIONENGINE_API FGeometryCollectionEdit
{
public:
	FGeometryCollectionEdit(UGeometryCollectionComponent* InComponent, GeometryCollection::EEditUpdate EditUpdate = GeometryCollection::EEditUpdate::RestPhysics);
	~FGeometryCollectionEdit();

	UGeometryCollection* GetRestCollection();

private:
	UGeometryCollectionComponent* Component;
	const GeometryCollection::EEditUpdate EditUpdate;
	bool bHadPhysicsState;
};

#if WITH_EDITOR
class GEOMETRYCOLLECTIONENGINE_API FScopedColorEdit
{
public:
	FScopedColorEdit(UGeometryCollectionComponent* InComponent, bool bForceUpdate = false);
	~FScopedColorEdit();

	void SetShowBoneColors(bool ShowBoneColorsIn);
	bool GetShowBoneColors() const;

	void SetEnableBoneSelection(bool ShowSelectedBonesIn);
	bool GetEnableBoneSelection() const;

	bool IsBoneSelected(int BoneIndex) const;
	void SetSelectedBones(const TArray<int32>& SelectedBonesIn);
	void AppendSelectedBones(const TArray<int32>& SelectedBonesIn);
	void ToggleSelectedBones(const TArray<int32>& SelectedBonesIn);
	void AddSelectedBone(int32 BoneIndex);
	void ClearSelectedBone(int32 BoneIndex);
	const TArray<int32>& GetSelectedBones() const;
	void ResetBoneSelection();
	void SelectBones(GeometryCollection::ESelectionMode SelectionMode);

	bool IsBoneHighlighted(int BoneIndex) const;
	void SetHighlightedBones(const TArray<int32>& HighlightedBonesIn);
	void AddHighlightedBone(int32 BoneIndex);
	const TArray<int32>& GetHighlightedBones() const;
	void ResetHighlightedBones();

	void SetLevelViewMode(int ViewLevel);
	int GetViewLevel();

private:
	void UpdateBoneColors();

	bool bUpdated;


	UGeometryCollectionComponent * Component;
	static TArray<FLinearColor> RandomColors;
};

#endif

//Provides copy on write functionality:
//GetArray (const access)
//GetArrayCopyOnWrite
//GetArrayRest (gives original rest value)
//This generates pointers to arrays marked private. Macro assumes getters are public
//todo(ocohen): may want to take in a static name
#define COPY_ON_WRITE_ATTRIBUTE(Type, Name, Group)\
FORCEINLINE const TManagedArray<Type>& Get##Name##Array() const {\
	return Indirect##Name##Array ? *Indirect##Name##Array : RestCollection->GetGeometryCollection()->Name;\
}\
FORCEINLINE TManagedArray<Type>& Get##Name##ArrayCopyOnWrite(){\
	if(!Indirect##Name##Array)\
	{\
		static FName StaticName(#Name);\
		DynamicCollection->AddAttribute<Type>(StaticName, Group);\
		DynamicCollection->CopyAttribute(*RestCollection->GetGeometryCollection(), StaticName, Group);\
		Indirect##Name##Array = &DynamicCollection->GetAttribute<Type>(StaticName, Group);\
		CopyOnWriteAttributeList.Add(reinterpret_cast<FManagedArrayBase**>(&Indirect##Name##Array));\
	}\
	return *Indirect##Name##Array;\
}\
FORCEINLINE void Reset##Name##ArrayDynamic(){\
	Indirect##Name##Array = NULL;\
}\
FORCEINLINE const TManagedArray<Type>& Get##Name##ArrayRest() const{ return RestCollection->GetGeometryCollection()->Name; }\
private:\
TManagedArray<Type>* Indirect##Name##Array;\
public:

/**
*	GeometryCollectionComponent
*/
UCLASS(meta = (BlueprintSpawnableComponent))
class GEOMETRYCOLLECTIONENGINE_API UGeometryCollectionComponent : public UMeshComponent, public IChaosNotifyHandlerInterface
{
	GENERATED_UCLASS_BODY()
	friend class FGeometryCollectionEdit;
#if WITH_EDITOR
	friend class FScopedColorEdit;
#endif
	friend class FGeometryCollectionCommands;

public:

	//~ Begin UActorComponent Interface.
	virtual void CreateRenderState_Concurrent() override;
	virtual void SendRenderDynamicData_Concurrent() override;
	FORCEINLINE void SetRenderStateDirty() { bRenderStateDirty = true; }
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type ReasonEnd) override;

	//~ Begin UActorComponent Interface. 


	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual FBoxSphereBounds CalcLocalBounds() const { return LocalBounds; }

	virtual bool HasAnySockets() const override { return false; }
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	//~ Begin USceneComponent Interface.


	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void OnRegister() override;
	virtual FBodyInstance* GetBodyInstance(FName BoneName = NAME_None, bool bGetWelded = true) const override;
	virtual void SetNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision) override;
	//~ End UPrimitiveComponent Interface.


	//~ Begin UMeshComponent Interface.	
	virtual int32 GetNumMaterials() const override;
	virtual UMaterialInterface* GetMaterial(int32 MaterialIndex) const override;
	//~ End UMeshComponent Interface.

	/** Chaos RBD Solver override. Will use the world's default solver actor if null. */
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics", meta = (DisplayName = "Chaos Solver"))
	AChaosSolverActor* ChaosSolverActor;

	/** RestCollection */
	void SetRestCollection(const UGeometryCollection * RestCollectionIn);
	FORCEINLINE const UGeometryCollection* GetRestCollection() const { return RestCollection; }
	FORCEINLINE FGeometryCollectionEdit EditRestCollection(GeometryCollection::EEditUpdate EditUpdate = GeometryCollection::EEditUpdate::RestPhysics) { return FGeometryCollectionEdit(this, EditUpdate); }
#if WITH_EDITOR
	FORCEINLINE FScopedColorEdit EditBoneSelection() { return FScopedColorEdit(this); }
#endif

	/** API for getting at geometry collection data */
	FORCEINLINE int32 GetNumElements(FName Group) const
	{
		int32 Size = RestCollection->NumElements(Group);	//assume rest collection has the group and is connected to dynamic.
		return Size > 0 ? Size : DynamicCollection->NumElements(Group);	//if not, maybe dynamic has the group
	}

	// Vertices Group
	COPY_ON_WRITE_ATTRIBUTE(FVector, Vertex, FGeometryCollection::VerticesGroup) 	//GetVertexArray, GetVertexArrayCopyOnWrite, GetVertexArrayRest
	COPY_ON_WRITE_ATTRIBUTE(FVector2D, UV, FGeometryCollection::VerticesGroup)		//GetUVArray
	COPY_ON_WRITE_ATTRIBUTE(FLinearColor, Color, FGeometryCollection::VerticesGroup)//GetColorArray
	COPY_ON_WRITE_ATTRIBUTE(FVector, TangentU, FGeometryCollection::VerticesGroup)	//GetTangentUArray
	COPY_ON_WRITE_ATTRIBUTE(FVector, TangentV, FGeometryCollection::VerticesGroup)	//...
	COPY_ON_WRITE_ATTRIBUTE(FVector, Normal, FGeometryCollection::VerticesGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, BoneMap, FGeometryCollection::VerticesGroup)

	// Faces Group
	COPY_ON_WRITE_ATTRIBUTE(FIntVector, Indices, FGeometryCollection::FacesGroup)
	COPY_ON_WRITE_ATTRIBUTE(bool, Visible, FGeometryCollection::FacesGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, MaterialIndex, FGeometryCollection::FacesGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, MaterialID, FGeometryCollection::FacesGroup)

	// Geometry Group
	COPY_ON_WRITE_ATTRIBUTE(int32, TransformIndex, FGeometryCollection::GeometryGroup)
	COPY_ON_WRITE_ATTRIBUTE(FBox, BoundingBox, FGeometryCollection::GeometryGroup)
	COPY_ON_WRITE_ATTRIBUTE(float, InnerRadius, FGeometryCollection::GeometryGroup)
	COPY_ON_WRITE_ATTRIBUTE(float, OuterRadius, FGeometryCollection::GeometryGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, VertexStart, FGeometryCollection::GeometryGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, VertexCount, FGeometryCollection::GeometryGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, FaceStart, FGeometryCollection::GeometryGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, FaceCount, FGeometryCollection::GeometryGroup)

	// Material Group
	COPY_ON_WRITE_ATTRIBUTE(FGeometryCollectionSection, Sections, FGeometryCollection::MaterialGroup)

	// Transform group
	COPY_ON_WRITE_ATTRIBUTE(FString, BoneName, FTransformCollection::TransformGroup)
	COPY_ON_WRITE_ATTRIBUTE(FLinearColor, BoneColor, FTransformCollection::TransformGroup)
	COPY_ON_WRITE_ATTRIBUTE(FTransform, Transform, FTransformCollection::TransformGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, Parent, FTransformCollection::TransformGroup)
	COPY_ON_WRITE_ATTRIBUTE(TSet<int32>, Children, FTransformCollection::TransformGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, SimulationType, FTransformCollection::TransformGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, TransformToGeometryIndex, FTransformCollection::TransformGroup)
	COPY_ON_WRITE_ATTRIBUTE(int32, StatusFlags, FTransformCollection::TransformGroup)


	UPROPERTY(EditAnywhere, NoClear, BlueprintReadOnly, Category = "ChaosPhysics")
	const UGeometryCollection* RestCollection;

	UPROPERTY(EditAnywhere, NoClear, BlueprintReadOnly, Category = "ChaosPhysics")
	TArray<const AFieldSystemActor*> InitializationFields;

	/**
	* When Simulating is enabled the Component will initialize its rigid bodies within the solver.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	bool Simulating;
	ESimulationInitializationState InitializationState;

	/*
	*  ObjectType defines how to initialize the rigid objects state, Kinematic, Sleeping, Dynamic.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	EObjectStateTypeEnum ObjectType;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	bool EnableClustering;

	/**
	* Maximum level for cluster breaks
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	int32 ClusterGroupIndex;

	/**
	* Maximum level for cluster breaks
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	int32 MaxClusterLevel;

	/**
	* Damage threshold for clusters at different levels.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	TArray<float> DamageThreshold;

	/*
	*  ObjectType defines how to initialize the rigid objects state, Kinematic, Sleeping, Dynamic.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	EClusterConnectionTypeEnum ClusterConnectionType;

	/**
	* Uniform Friction
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
	int32 CollisionGroup;

	/**
	* Uniform Friction
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
	float CollisionSampleFraction;


	/**
	* Physical Properties
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics")
	const UChaosPhysicalMaterial* PhysicalMaterial;

	/**
	*
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
	EInitialVelocityTypeEnum InitialVelocityType;

	/**
	*
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
	FVector InitialLinearVelocity;

	/**
	*
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
	FVector InitialAngularVelocity;

	UPROPERTY(EditAnywhere, Category = "ChaosPhysics|Caching", meta=(ShowOnlyInnerProperties))
	FGeomComponentCacheParameters CacheParameters;

	UFUNCTION(BlueprintCallable, Category = "Field")
	void ApplyKinematicField(float Radius, FVector Position);

	UFUNCTION(BlueprintCallable, Category = "Field")
	void ApplyPhysicsField(bool Enabled, EGeometryCollectionPhysicsTypeEnum Target, UFieldSystemMetaData* MetaData, UFieldNodeBase* Field);

	/**
	* Blueprint event
	*/

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNotifyGeometryCollectionPhysicsStateChange, UGeometryCollectionComponent*, FracturedComponent);

	UPROPERTY(BlueprintAssignable, Category = "Game|Damage")
	FNotifyGeometryCollectionPhysicsStateChange NotifyGeometryCollectionPhysicsStateChange;

	bool GetIsObjectDynamic() { return IsObjectDynamic; }

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNotifyGeometryCollectionPhysicsLoadingStateChange, UGeometryCollectionComponent*, FracturedComponent);
	UPROPERTY(BlueprintAssignable, Category = "Game|Loading")
	FNotifyGeometryCollectionPhysicsLoadingStateChange NotifyGeometryCollectionPhysicsLoadingStateChange;
	
	bool GetIsObjectLoading() { return IsObjectLoading; }



	/**
	*
	*/
	/* ---------------------------------------------------------------------------------------- */
	
	void SetShowBoneColors(bool ShowBoneColorsIn);
	bool GetShowBoneColors() const { return bShowBoneColors; }
	bool GetEnableBoneSelection() const { return bEnableBoneSelection; }
	
	FORCEINLINE const int GetBoneSelectedMaterialID() const { return RestCollection->GetBoneSelectedMaterialIndex(); }
	
#if WITH_EDITORONLY_DATA
	FORCEINLINE const TArray<int32>& GetSelectedBones() const { return SelectedBones; }
	FORCEINLINE const TArray<int32>& GetHighlightedBones() const { return HighlightedBones; }
#endif

	FPhysScene_Chaos* GetPhysicsScene() const;
	AChaosSolverActor* GetPhysicsSolverActor() const;
	const FGeometryCollectionPhysicsProxy* GetPhysicsProxy() const { return PhysicsProxy; }

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	/** Enable/disable the scene proxy per transform selection mode. When disabled the per material id default selection is used instead. */
	void EnableTransformSelectionMode(bool bEnable);
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** Force render after constant data changes (such as visibility, or hitproxy subsections). Will also work while paused. */
	void ForceRenderUpdateConstantData() { MarkRenderStateDirty(); }

	/** Force render after dynamic data changes (such as transforms). Will also work while paused. */
	void ForceRenderUpdateDynamicData() { MarkRenderDynamicDataDirty(); }
#endif  // #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	/**/
	//const TManagedArray<int32>& GetRigidBodyIdArray() const { return RigidBodyIds; }
	const TManagedArray<FGuid>& GetRigidBodyGuidArray() const { return RestCollection->GetGeometryCollection()->GetAttribute<FGuid>(FName("GUID"), FGeometryCollection::TransformGroup); }
	const TArray<bool>& GetDisabledFlags() const { return DisabledFlags; }

	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool HasValidPhysicsState() const override;

	// Mirrored from the proxy on a sync
	//TManagedArray<int32> RigidBodyIds;
	TArray<bool> DisabledFlags;
	int32 BaseRigidBodyIndex;
	int32 NumParticlesAdded;

	/** Changes whether or not this component will get future break notifications. */
	UFUNCTION(BlueprintCallable, Category = "Physics")
	void SetNotifyBreaks(bool bNewNotifyBreaks);

	/** Overrideable native notification */
	virtual void NotifyBreak(const FChaosBreakEvent& Event) {};

	UPROPERTY(BlueprintAssignable, Category = "Chaos")
	FOnChaosBreakEvent OnChaosBreakEvent;
	
	void DispatchBreakEvent(const FChaosBreakEvent& Event);

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadWrite, Interp, Category = "Chaos")
	float DesiredCacheTime;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadWrite, Category = "Chaos")
	bool CachePlayback;


public:
	UPROPERTY(BlueprintAssignable, Category = "Collision")
	FOnChaosPhysicsCollision OnChaosPhysicsCollision;
	
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Physics Collision"), Category = "Collision")
	void ReceivePhysicsCollision(const FChaosPhysicsCollisionInfo& CollisionInfo);

	// IChaosNotifyHandlerInterface
	virtual void DispatchChaosPhysicsCollisionBlueprintEvents(const FChaosPhysicsCollisionInfo& CollisionInfo) override;

protected:
	/** Call SetNotifyBreaks to set this at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics|General")
	bool bNotifyBreaks;

	/** If true, this component will get Chaos-specific collision notification events (@see IChaosNotifyHandlerInterface) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics|General")
	bool bNotifyCollisions;

	/** Populate the static geometry structures for the render thread. */
	void InitConstantData(FGeometryCollectionConstantData* ConstantData) const;

	/** Populate the dynamic particle data for the render thread. */
	void InitDynamicData(FGeometryCollectionDynamicData* ConstantData);

	/** Reset the dynamic collection from the current rest state. */
	void ResetDynamicCollection();

	/** Combine the commands from the input field assets */
	void GetInitializationCommands(TArray<FFieldSystemCommand>& CombinedCommmands);

	/** Issue a field command for the physics thread */
	void DispatchCommand(const FFieldSystemCommand& InCommand);

	void CalculateLocalBounds();
	void CalculateGlobalMatrices();

	void RegisterForEvents();
	void UpdateRBCollisionEventRegistration();
	void UpdateBreakEventRegistration();

private:
	bool bRenderStateDirty;
	bool bShowBoneColors;
	bool bEnableBoneSelection;
	int ViewLevel;

	uint32 NavmeshInvalidationTimeSliceIndex;
	bool IsObjectDynamic;
	bool IsObjectLoading;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	TArray<int32> SelectedBones;

	UPROPERTY(Transient)
	TArray<int32> HighlightedBones;
#endif

	TArray<FMatrix> GlobalMatrices;
	FBox LocalBounds;
	
	FBoxSphereBounds WorldBounds;		

	float CurrentCacheTime;
	TArray<bool> EventsPlayed;

	//@todo(mlentine): Don't have one per geo collection
	TUniquePtr<Chaos::FChaosPhysicsMaterial> ChaosMaterial;

	FGeometryCollectionPhysicsProxy* PhysicsProxy;
	TUniquePtr<FGeometryDynamicCollection> DynamicCollection;
	TArray<FManagedArrayBase**> CopyOnWriteAttributeList;

	// Temporary dummies to interface with Physx expectations of the SQ syatem
#if WITH_PHYSX
	friend class FGeometryCollectionSQAccelerator;
	FBodyInstance DummyBodyInstance;
#endif

	// Temporary storage for body setup in order to initialise a dummy body instance
	UPROPERTY(Transient)
	UBodySetup* DummyBodySetup;

#if WITH_EDITORONLY_DATA
	// Tracked editor actor that owns the original component so we can write back recorded caches
	// from PIE.
	UPROPERTY(Transient)
	AActor* EditorActor;
#endif
	void SwitchRenderModels(const AActor* Actor);

	bool IsEqual(const TArray<FMatrix> &A, const TArray<FMatrix> &B, const float Tolerance = 1e-6);
	TArray<bool> TransformsAreEqual;	
	int32 TransformsAreEqualIndex;

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	bool bIsTransformSelectionModeEnabled;
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION
};
