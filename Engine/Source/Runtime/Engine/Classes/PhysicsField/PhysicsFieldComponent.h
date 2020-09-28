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
	/** Size of the datas stored on each voxels*/
	int32 TargetCount = 1;

	/** Size of the datas stored on each voxels*/
	TArray<EFieldPhysicsType> TargetTypes;

	/** Vector Targets Offsets*/
	TStaticArray<int32, MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY> VectorTargets;

	/** Scalar Targets Offsets*/
	TStaticArray<int32, MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY> ScalarTargets;

	/** Integer targets offsets */
	TStaticArray<int32, MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY> IntegerTargets;

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

	/** Field infos that will be used to allocate memory and to transfer information */
	FPhysicsFieldInfos FieldInfos;

	/** Default constructor. */
	FPhysicsFieldResource(const int32 TargetCount, const TArray<EFieldPhysicsType>& TargetTypes, const TStaticArray<int32, MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY>& VectorTargets, const TStaticArray<int32, MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY>& ScalarTargets, const TStaticArray<int32, MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY>& IntegerTargets);

	/** Release Field resources. */
	virtual void ReleaseRHI() override;

	/** Init Field resources. */
	virtual void InitRHI() override;

	/** Update RHI resources. */
	void UpdateResource(FRHICommandListImmediate& RHICmdList, const int32 NodesCount, const int32 ParamsCount,
		const int32* TargetsOffsetsDatas, const int32* NodesOffsetsDatas, const float* NodesParamsDatas);
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
	void UpdateInstance(const TArray<FFieldSystemCommand>& FieldCommands);

	/** Update the offsets and paramsgiven a node */
	void BuildNodeParams(FFieldNodeBase* FieldNode);

	/** The field system resource. */
	FPhysicsFieldResource* FieldResource = nullptr;

	/** Targets offsets in the nodes array*/
	TStaticArray<int32, TargetsCount + 1> TargetsOffsets;

	/** Nodes offsets in the paramter array */
	TArray<int32> NodesOffsets;

	/** Nodes input parameters and connection */
	TArray<float> NodesParams;
};

/**
*	PhysicsFieldComponent
*/

UCLASS(meta = (BlueprintSpawnableComponent))
class ENGINE_API UPhysicsFieldComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:

	UPhysicsFieldComponent();

	//~ Begin UPrimitiveComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void SendRenderDynamicData_Concurrent() override;
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface.

	/** Store the field command */
	void BufferCommand(const FFieldSystemCommand& InCommand);

	// These types are not static since we probably want in the future to be able to pick the vector/scalar/integer fields we are interested in

	/** List of all the field commands in the world */
	TArray<FFieldSystemCommand> FieldCommands;

	/** The instance of the field system. */
	FPhysicsFieldInstance* FieldInstance = nullptr;
};

class FPhysicsFieldSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override;

	/** Initialization constructor. */
	explicit FPhysicsFieldSceneProxy(class UPhysicsFieldComponent* PhysicsFieldComponent);

	/** Destructor. */
	~FPhysicsFieldSceneProxy();

	/**
	 *	Called when the rendering thread adds the proxy to the scene.
	 *	This function allows for generating renderer-side resources.
	 *	Called in the rendering thread.
	 */
	virtual void CreateRenderThreadResources() override;

	/**
	* Computes view relevance for this scene proxy.
	*/
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	/**
	 * Computes the memory footprint of this scene proxy.
	 */
	virtual uint32 GetMemoryFootprint() const override;

	/** The vector field resource which this proxy is visualizing. */
	FPhysicsFieldResource* FieldResource = nullptr;
};


