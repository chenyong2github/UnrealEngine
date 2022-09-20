// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "OptimusDataInterfaceJiggleSpring.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FSkeletalMeshObject;
class USkinnedMeshComponent;

/** 
* User modifyable jiggle spring attributes. These attributes appear in the Optimus 
* editor's Details panel.
*/
USTRUCT()
struct FOptimusJiggleSpringParameters
{
	GENERATED_BODY();

	/** Uniform stiffness value, multiplied against per-vertex StiffnessWeights. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	float BaselineStiffness = 100.0f;

	// Notes on stiffness and damping weights:
	// Until we can transport TArray<float> to the shader via blueprints, we're stuck
	// reading these values from standalone files.  These files contain values for all
	// surfaces, and so we need a way to associate values to surface.  So we optionally
	// match by name (which may be lost during surface export to file), and if that 
	// fails, we match by vertex to value count.  At the point when we can set per
	// skeletal mesh values via blueprints, the names arrays go away and the 
	// TArray<TArray<float>> for values turn into TArray<float>, and is exposed as a
	// UPROPERTY.

	/** Per vertex spring stiffness surface names. */
	TArray<FString> StiffnessWeightsNames;
	//UPROPERTY(EditAnywhere, Category = Deformer)
	TArray<TArray<float>> StiffnessWeights;

	/** Stiffness weights file. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	FFilePath StiffnessWeightsFile;

	/** Uniform damping value, multiplied against per-vertex DampingWeights. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	float BaselineDamping = 10.0f;

	/** Per vertex spring damping surface names. */
	TArray<FString> DampingWeightsNames;
	//UPROPERTY(EditAnywhere, Category = Deformer)
	TArray<TArray<float>> DampingWeights;

	/** Damping weights file. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	FFilePath DampingWeightsFile;

	/** Map render vertices to import indices. */
	TArray<int32> VertexMap;

	/** Multiplier on the max stretch distance per-vertex value if specified, 
	 *  or the uniform max stretch value if no per-vertex map is specified. 
	 */
	UPROPERTY(EditAnywhere, Category = Deformer)
	float MaxStretchMultiplier = 3.0f;

	/** Use the per vertex average edge length as the max stretch distance map. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	bool bUseAvgEdgeLengthForMaxStretchMap = true;

	/** Per vertex maximum stretch distance. */
	UPROPERTY(EditAnywhere, Category = Deformer)
	TArray<float> MaxStretchWeights;

	//bool ReadWeightsFile(const FFilePath& FilePath, TArray<TTuple<FString,TArray<float>>>& Values) const;
	bool ReadWeightsFile(const FFilePath& FilePath, TArray<FString>& Names, TArray<TArray<float>>& Values) const;
	bool ReadWeightsFile(const FFilePath& FilePath, TArray<FVector3f>& Positions, TArray<float>& Values) const;
};


/** 
* Compute Framework Data Interface for reading skeletal mesh. 
*
* Defines the output pins of the data interface node available in the Optimus graph 
* editor.  Inputs exposed to the user are dictated by the Parameters UPROPERTY member.
*
* This class establishes a dependency on an external HLSL resource file associated with
* this data interface, usually located in "/Plugin/Optimus/Private/.". 
*/
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusJiggleSpringDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;
	void RegisterTypes() override;
	//~ End UOptimusComputeDataInterface Interface
	
	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("JiggleSpring"); }
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	void GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const override;
	void GetShaderHash(FString& InOutKey) const override;
	void GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const override;
	UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

	UPROPERTY(EditAnywhere, Category = Deformer, meta = (ShowOnlyInnerProperties))
	FOptimusJiggleSpringParameters JiggleSpringParameters;
};

/** Compute Framework Data Provider for reading skeletal mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UOptimusJiggleSpringDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkinnedMeshComponent> SkinnedMesh = nullptr;

	UPROPERTY()
	FOptimusJiggleSpringParameters JiggleSpringParameters;

	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FOptimusJiggleSpringDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FOptimusJiggleSpringDataProviderProxy(
		USkinnedMeshComponent* SkeletalMeshComponent,
		FOptimusJiggleSpringParameters const& InJiggleSpringParameters);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData);
	//~ End FComputeDataProviderRenderProxy Interface

private:
	USkinnedMeshComponent* SkinnedMeshComponent = nullptr;
	FSkeletalMeshObject* SkeletalMeshObject = nullptr;
	FOptimusJiggleSpringParameters JiggleSpringParameters;

	// If the stiffness and damping weights came from file, then they're divided into multiple sections;
	// 1 section for each skeletal mesh.  This integer identifies which one we're currently using.
	int32 SectionIndex = INDEX_NONE;

	FRDGBuffer* VertexMapBuffer = nullptr;
	FRDGBufferSRV* VertexMapBufferSRV = nullptr;
	FRDGBuffer* StiffnessWeightsBuffer = nullptr;
	FRDGBufferSRV* StiffnessWeightsBufferSRV = nullptr;
	FRDGBuffer* DampingWeightsBuffer = nullptr;
	FRDGBufferSRV* DampingWeightsBufferSRV = nullptr;
	FRDGBuffer* MaxStretchWeightsBuffer = nullptr;
	FRDGBufferSRV* MaxStretchWeightsBufferSRV = nullptr;
	float NullFloatBuffer = 0.0f;
	int32 NullIntBuffer = 0;
};

