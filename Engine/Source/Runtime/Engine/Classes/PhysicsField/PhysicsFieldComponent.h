// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PrimitiveComponent.h"
#include "Field/FieldSystem.h"
#include "UObject/ObjectMacros.h"
#include "RHI.h"
#include "RenderResource.h"
#include "PrimitiveSceneProxy.h"

#include "PhysicsFieldComponent.generated.h"

/** Max targets array*/
#define MAX_TARGETS_ARRAY 16

struct FPhysicsFieldInfos
{
	/** Type of targets offsets */
	using TargetsOffsetsType = TStaticArray<int32, MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY>;

	/** Size of the datas stored on each voxels*/
	int32 TargetCount = 1;

	/** Size of the datas stored on each voxels*/
	TArray<EFieldPhysicsType> TargetTypes;

	/** Vector Targets Offsets*/
	TargetsOffsetsType VectorTargets;

	/** Scalar Targets Offsets*/
	TargetsOffsetsType ScalarTargets;

	/** Integer targets offsets */
	TargetsOffsetsType IntegerTargets;

	/** Physics targets offsets */
	TargetsOffsetsType PhysicsTargets;

	/** Clipmap  Center */
	FVector ClipmapCenter = FVector::ZeroVector;

	/** Clipmap Distance */
	float ClipmapDistance = 10000;

	/** Clipmap Count */
	int32 ClipmapCount = 4;

	/** Clipmap Exponent */
	int32 ClipmapExponent = 2;

	/** Clipmap Resolution */
	int32 ClipmapResolution = 64;

	/** Clipmap Resolution */
	FVector ViewOrigin = FVector::ZeroVector;
};

/**
 * Physics Field render resource.
 */
class FPhysicsFieldResource : public FRenderResource
{
public:

	/** Field cached clipmap texture */
	FTextureRWBuffer3D FieldClipmap;

	/** Field nodes params buffer */
	FRWBuffer NodesParams;

	/** Field nodes offsets buffer */
	FRWBuffer NodesOffsets;

	/** Field targets nodes buffer */
	FRWBuffer TargetsOffsets;

	/** Cells offsets buffer */
	FRWBuffer CellsOffsets;

	/** Cells Min buffer */
	FRWBuffer CellsMin;

	/** Cells max buffer */
	FRWBuffer CellsMax;

	/** Field infos that will be used to allocate memory and to transfer information */
	FPhysicsFieldInfos FieldInfos;

	/** Bounds Cells offsets */
	TArray<int32> DatasOffsets;

	/** Min Bounds for each target/clipmap */
	TArray<FIntVector4> DatasMin;

	/** Max Bounds for each target/clipmap */
	TArray<FIntVector4> DatasMax;

	/** Default constructor. */
	FPhysicsFieldResource(const int32 TargetCount, const TArray<EFieldPhysicsType>& TargetTypes,
		const FPhysicsFieldInfos::TargetsOffsetsType& VectorTargets, const FPhysicsFieldInfos::TargetsOffsetsType& ScalarTargets,
		const FPhysicsFieldInfos::TargetsOffsetsType& IntegerTargets, const FPhysicsFieldInfos::TargetsOffsetsType& PhysicsTargets);

	/** Release Field resources. */
	virtual void ReleaseRHI() override;

	/** Init Field resources. */
	virtual void InitRHI() override;

	/** Update RHI resources. */
	void UpdateResource(FRHICommandListImmediate& RHICmdList, const int32 NodesCount, const int32 ParamsCount,
		const TStaticArray<int32, EFieldPhysicsType::Field_PhysicsType_Max + 1>& TargetsOffsetsDatas, const TArray<int32>& NodesOffsetsDatas, const TArray<float>& NodesParamsDatas,
		const TArray<FVector>& MinBoundsDatas, const TArray<FVector>& MaxBoundsDatas, const float TimeSeconds);

	/** Update Bounds. */
	void UpdateBounds(const TArray<FVector>& MinBounds, const TArray<FVector>& MaxBounds);
};


/**
 * An instance of a Physics Field.
 */
class FPhysicsFieldInstance
{
public:

	/** Number of field node types. */
	static const uint32 FieldsCount = FFieldNodeBase::ESerializationType::FieldNode_FReturnResultsTerminal + 1;

	/** Max number of targets. */
	static const uint32 TargetsCount = EFieldPhysicsType::Field_PhysicsType_Max;

	/** Default constructor. */
	FPhysicsFieldInstance()
		: FieldResource(nullptr)
	{}

	/** Destructor. */
	~FPhysicsFieldInstance() {}

	/**
	 * Initializes the instance for the given resource.
	 * @param TextureSize - The resource texture size to be used.
	 */
	void InitInstance(const TArray<EFieldPhysicsType>& TargetTypes);

	/**
	 * Release the resource of the instance.
	 */
	void ReleaseInstance();

	/**
	 * Update the datas based on the new bounds and commands
	 * @param FieldCommands - Field commands to be sampled
	 */
	void UpdateInstance(const float TimeSeconds);

	/** Update the offsets and params given a node */
	void BuildNodeParams(FFieldNodeBase* FieldNode);

	/** Update the bounds given a node */
	void BuildNodeBounds(FFieldNodeBase* FieldNode, FVector& MinBounds, FVector& MaxBounds);

	/** The field system resource. */
	FPhysicsFieldResource* FieldResource = nullptr;

	/** Targets offsets in the nodes array*/
	TStaticArray<int32, TargetsCount + 1> TargetsOffsets;

	/** Nodes offsets in the paramter array */
	TArray<int32> NodesOffsets;

	/** Nodes input parameters and connection */
	TArray<float> NodesParams;

	/** List of all the field commands in the world */
	TArray<FFieldSystemCommand> FieldCommands;

	/** Min Bounds for each target/clipmap */
	TArray<FVector> BoundsMin;

	/** Max Bounds for each target/clipmap */
	TArray<FVector> BoundsMax;
};

/**
*	PhysicsFieldComponent
*/

UCLASS(meta = (BlueprintSpawnableComponent))
class ENGINE_API UPhysicsFieldComponent : public USceneComponent
{
	GENERATED_BODY()

public:

	UPhysicsFieldComponent();

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void SendRenderDynamicData_Concurrent() override;
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface.

	/** Add the transient field command */
	void AddTransientCommand(const FFieldSystemCommand& InCommand);

	/** Add the persitent field command */
	void AddPersistentCommand(const FFieldSystemCommand& FieldCommand);

	/** Remove the transient field command */
	void RemoveTransientCommand(const FFieldSystemCommand& InCommand);

	/** Remove the persitent field command */
	void RemovePersistentCommand(const FFieldSystemCommand& FieldCommand);

	// These types are not static since we probably want in the future to be able to pick the vector/scalar/integer fields we are interested in

	/** List of all the field transient commands in the world */
	TArray<FFieldSystemCommand> TransientCommands;

	/** List of all the field persitent commands in the world */
	TArray<FFieldSystemCommand> PersistentCommands;

	/** The instance of the field system. */
	FPhysicsFieldInstance* FieldInstance = nullptr;

	/** Scene proxy to be sent to the render thread. */
	class FPhysicsFieldSceneProxy* FieldProxy = nullptr;
};

//class FPhysicsFieldSceneProxy final : public FPrimitiveSceneProxy
class FPhysicsFieldSceneProxy 
{
public:
	//SIZE_T GetTypeHash() const override;

	/** Initialization constructor. */
	explicit FPhysicsFieldSceneProxy(class UPhysicsFieldComponent* PhysicsFieldComponent);

	/** Destructor. */
	~FPhysicsFieldSceneProxy();

	/** The vector field resource which this proxy is visualizing. */
	FPhysicsFieldResource* FieldResource = nullptr;
};


