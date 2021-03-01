// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PrimitiveComponent.h"
#include "Field/FieldSystem.h"
#include "UObject/ObjectMacros.h"
#include "RHI.h"
#include "RenderResource.h"
#include "PrimitiveSceneProxy.h"

#include "PhysicsFieldComponent.generated.h"

enum class EFieldCommandBuffer : uint8
{
	GPUFieldBuffer = 0,
	CPUReadBuffer = 1,
	CPUWriteBuffer = 2,
	NumFieldBuffers = 3
};

struct FPhysicsFieldInfos
{
	/** Type of targets offsets */
	using BufferOffsets = TStaticArray<int32, MAX_PHYSICS_FIELD_TARGETS, 16>;

	/** Size of the datas stored on each voxels*/
	int32 TargetCount = 1;

	/** Target types to be processed */
	TArray<EFieldPhysicsType> TargetTypes;

	/** Vector Targets Offsets*/
	BufferOffsets VectorTargets;

	/** Scalar Targets Offsets*/
	BufferOffsets ScalarTargets;

	/** Integer targets offsets */
	BufferOffsets IntegerTargets;

	/** Physics targets offsets */
	BufferOffsets PhysicsTargets;

	/** Valid targets offsets */
	BufferOffsets ValidTargets;

	/** Clipmap  Center */
	FVector ClipmapCenter = FVector::ZeroVector;

	/** Clipmap Distance */
	float ClipmapDistance = 10000;

	/** Clipmap Count */
	int32 ValidCount = 0;

	/** Clipmap Count */
	int32 ClipmapCount = 4;

	/** Clipmap Exponent */
	int32 ClipmapExponent = 2;

	/** Clipmap Resolution */
	int32 ClipmapResolution = 64;

	/** Clipmap Resolution */
	FVector ViewOrigin = FVector::ZeroVector;

	/** Bounds Cells offsets */
	TArray<int32> CellsOffsets;

	/** Min Bounds for each target/clipmap */
	TArray<FIntVector4> CellsMin;

	/** Max Bounds for each target/clipmap */
	TArray<FIntVector4> CellsMax;
};

/**
 * Physics Field render resource.
 */
class FPhysicsFieldResource : public FRenderResource
{
public:

	/** Field cached clipmap buffer */
	FRWBuffer ClipmapBuffer;

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

	/** Default constructor. */
	FPhysicsFieldResource(const int32 TargetCount, const TArray<EFieldPhysicsType>& TargetTypes,
		const FPhysicsFieldInfos::BufferOffsets& VectorTargets, const FPhysicsFieldInfos::BufferOffsets& ScalarTargets,
		const FPhysicsFieldInfos::BufferOffsets& IntegerTargets, const FPhysicsFieldInfos::BufferOffsets& PhysicsTargets);

	/** Release Field resources. */
	virtual void ReleaseRHI() override;

	/** Init Field resources. */
	virtual void InitRHI() override;

	/** Update RHI resources. */
	void UpdateResource(FRHICommandListImmediate& RHICmdList,
		const TStaticArray<int32, EFieldPhysicsType::Field_PhysicsType_Max + 1>& TargetsOffsetsDatas, const TArray<int32>& NodesOffsetsDatas, const TArray<float>& NodesParamsDatas,
		const TArray<FVector>& MinBoundsDatas, const TArray<FVector>& MaxBoundsDatas, const float TimeSeconds);

	/** Update Bounds. */
	void UpdateBounds(const TArray<FVector>& MinBounds, const TArray<FVector>& MaxBounds, const TStaticArray<int32, EFieldPhysicsType::Field_PhysicsType_Max + 1>& TargetOffsets);
};


/**
 * An instance of a Physics Field.
 */
class FPhysicsFieldInstance
{
public:

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
	void BuildNodeParams(FFieldNodeBase* FieldNode, const TMap<FFieldNodeBase*, float> CommandTimes, const float PreviousTime);

	/** Update the bounds given a node */
	static void BuildNodeBounds(FFieldNodeBase* FieldNode, FVector& MinBounds, FVector& MaxBounds);

	/** The field system resource. */
	FPhysicsFieldResource* FieldResource = nullptr;

	/** Targets offsets in the nodes array*/
	TStaticArray<int32, EFieldPhysicsType::Field_PhysicsType_Max + 1> TargetsOffsets;

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
	void AddTransientCommand(const FFieldSystemCommand& FieldCommand, const bool bIsGPUField);

	/** Add the persitent field command */
	void AddPersistentCommand(const FFieldSystemCommand& FieldCommand, const bool bIsGPUField);

	/** Remove the transient field command */
	void RemoveTransientCommand(const FFieldSystemCommand& FieldCommand, const bool bIsGPUField);

	/** Remove the persitent field command */
	void RemovePersistentCommand(const FFieldSystemCommand& FieldCommand, const bool bIsGPUField);

	/** Fill the transient commands intersecting the bounding box from the physics field */
	void FillTransientCommands(const bool bIsWorldField, const FBox& BoundingBox, const float TimeSeconds, TArray<FFieldSystemCommand>& OutputCommands) const;

	/** Fill the persistent commands intersecting the bounding box from the physics field */
	void FillPersistentCommands(const bool bIsWorldField, const FBox& BoundingBox, const float TimeSeconds, TArray<FFieldSystemCommand>& OutputCommands) const;

	/** Build the command bounds */
	static void BuildCommandBounds(FFieldSystemCommand& FieldCommand);

	// These types are not static since we probably want in the future to be able to pick the vector/scalar/integer fields we are interested in

	/** List of all the field transient commands in the world */
	TArray<FFieldSystemCommand> TransientCommands[(uint8)(EFieldCommandBuffer::NumFieldBuffers)];

	/** List of all the field persitent commands in the world */
	TArray<FFieldSystemCommand> PersistentCommands[(uint8)(EFieldCommandBuffer::NumFieldBuffers)];

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


