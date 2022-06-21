// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterfaceMeshCommon.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"
#include "NiagaraDataInterfaceStaticMesh.generated.h"

UENUM()
enum class ENDIStaticMesh_SourceMode : uint8
{
	/**
	Default behavior.
	- Use "Source" when specified (either set explicitly or via blueprint with Set Niagara Static Mesh Component).
	- When no source is specified, attempt to find a Static Mesh Component on an attached actor or component.
	- If no source actor/component specified and no attached component found, fall back to the "Default Mesh" specified.
	*/
	Default,

	/**
	Only use "Source" (either set explicitly or via blueprint with Set Niagara Static Mesh Component).
	*/
	Source,

	/**
	Only use the parent actor or component the system is attached to.
	*/
	AttachParent,

	/**
	Only use the "Default Mesh" specified.
	*/
	DefaultMeshOnly,
};

USTRUCT()
struct FNDIStaticMeshSectionFilter
{
	GENERATED_USTRUCT_BODY();

	/** Only allow sections these material slots. */
	UPROPERTY(EditAnywhere, Category="Section Filter")
	TArray<int32> AllowedMaterialSlots;

	//Others?
	//Banned material slots
	
	void Init(class UNiagaraDataInterfaceStaticMesh* Owner, bool bAreaWeighted);
	FORCEINLINE bool CanEverReject()const { return AllowedMaterialSlots.Num() > 0; }
};

/** Data Interface allowing sampling of static meshes. */
UCLASS(EditInlineNew, Category = "Meshes", meta = (DisplayName = "Static Mesh"))
class NIAGARA_API UNiagaraDataInterfaceStaticMesh : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	/** Controls how to retrieve the Static Mesh Component to attach to. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	ENDIStaticMesh_SourceMode SourceMode = ENDIStaticMesh_SourceMode::Default;
	
#if WITH_EDITORONLY_DATA
	/** Mesh used to sample from when not overridden by a source actor from the scene. Only available in editor for previewing. This is removed in cooked builds. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	TSoftObjectPtr<UStaticMesh> PreviewMesh;
#endif

	/** Mesh used to sample from when not overridden by a source actor from the scene. This mesh is NOT removed from cooked builds. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	TObjectPtr<UStaticMesh> DefaultMesh;

	/** The source actor from which to sample. Takes precedence over the direct mesh. Note that this can only be set when used as a user variable on a component in the world. */
	UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Source Actor"))
	TSoftObjectPtr<AActor> SoftSourceActor;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<AActor> Source_DEPRECATED;
#endif

	/** The source component from which to sample. Takes precedence over the direct mesh. Not exposed to the user, only indirectly accessible from blueprints. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SourceComponent;

	/** Array of filters the can be used to limit sampling to certain sections of the mesh. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	FNDIStaticMeshSectionFilter SectionFilter;

	/** If true then the mesh velocity is taken from the mesh component's physics data. Otherwise it will be calculated by diffing the component transforms between ticks, which is more reliable but won't work on the first frame. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Mesh")
    bool bUsePhysicsBodyVelocity = false;

	/** List of filtered sockets to use. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	TArray<FName> FilteredSockets;

    /** Changed within the editor on PostEditChangeProperty. Should be changed whenever a refresh is desired.*/
	uint32 ChangeId = 0;

	//~ UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	//~ UObject interface

	//~ UNiagaraDataInterface interface
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize() const override;

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
#if WITH_EDITORONLY_DATA
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;
#endif
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual bool RequiresDistanceFieldData() const override;
	virtual bool HasPreSimulateTick() const override { return true; }

#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void ModifyCompilationEnvironment(EShaderPlatform ShaderPlatform, struct FShaderCompilerEnvironment& OutEnvironment) const override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif
	virtual bool UseLegacyShaderBindings() const override { return false; }
	virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
	virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
public:
#if WITH_EDITOR
	virtual void GetFeedback(UNiagaraSystem* Asset, UNiagaraComponent* Component, TArray<FNiagaraDataInterfaceError>& OutErrors,
		TArray<FNiagaraDataInterfaceFeedback>& OutWarnings, TArray<FNiagaraDataInterfaceFeedback>& OutInfo) override;
#endif
	//~ UNiagaraDataInterface interface

	UStaticMesh* GetStaticMesh(USceneComponent*& OutComponent, class FNiagaraSystemInstance* SystemInstance = nullptr);
	void SetSourceComponentFromBlueprints(UStaticMeshComponent* ComponentToUse);
	void SetDefaultMeshFromBlueprints(UStaticMesh* MeshToUse);

protected:
	// VM Vertex Sampling
	void VMIsValidVertex(FVectorVMExternalFunctionContext& Context);
	void VMRandomVertex(FVectorVMExternalFunctionContext& Context);
	void VMGetVertexCount(FVectorVMExternalFunctionContext& Context);

	template<typename TTransformHandler>
	void VMGetVertex(FVectorVMExternalFunctionContext& Context);
	void VMGetVertexColor(FVectorVMExternalFunctionContext& Context);
	void VMGetVertexUV(FVectorVMExternalFunctionContext& Context);

	// VM Triangle Sampling
	void VMIsValidTriangle(FVectorVMExternalFunctionContext& Context);
	template<typename TRandomHelper>
	void VMRandomTriangle(FVectorVMExternalFunctionContext& Context);
	void VMGetTriangleCount(FVectorVMExternalFunctionContext& Context);

	void VMIsValidFilteredTriangle(FVectorVMExternalFunctionContext& Context);
	void VMRandomFilteredTriangle(FVectorVMExternalFunctionContext& Context);
	void VMGetFilteredTriangleCount(FVectorVMExternalFunctionContext& Context);
	void VMGetFilteredTriangleAt(FVectorVMExternalFunctionContext& Context);

	void VMIsValidUnfilteredTriangle(FVectorVMExternalFunctionContext& Context);
	void VMRandomUnfilteredTriangle(FVectorVMExternalFunctionContext& Context);
	void VMGetUnfilteredTriangleCount(FVectorVMExternalFunctionContext& Context);
	void VMGetUnfilteredTriangleAt(FVectorVMExternalFunctionContext& Context);

	template<typename TTransformHandler>
	void VMGetTriangle(FVectorVMExternalFunctionContext& Context);
	void VMGetTriangleColor(FVectorVMExternalFunctionContext& Context);
	void VMGetTriangleUV(FVectorVMExternalFunctionContext& Context);

	void VMGetTriangleIndices(FVectorVMExternalFunctionContext& Context);

	// Socket Functions
	void VMGetSocketCount(FVectorVMExternalFunctionContext& Context);
	void VMGetFilteredSocketCount(FVectorVMExternalFunctionContext& Context);
	void VMGetUnfilteredSocketCount(FVectorVMExternalFunctionContext& Context);
	template<typename TTransformHandler>
	void VMGetSocketTransform(FVectorVMExternalFunctionContext& Context);
	template<typename TTransformHandler>
	void VMGetFilteredSocketTransform(FVectorVMExternalFunctionContext& Context);
	template<typename TTransformHandler>
	void VMGetUnfilteredSocketTransform(FVectorVMExternalFunctionContext& Context);

	// Section functions
	void VMIsValidSection(FVectorVMExternalFunctionContext& Context);
	void VMGetSectionTriangleCount(FVectorVMExternalFunctionContext& Context);
	template<typename TRandomHandler>
	void VMRandomSectionTriangle(FVectorVMExternalFunctionContext& Context);
	void VMGetSectionTriangleAt(FVectorVMExternalFunctionContext& Context);

	void VMGetFilteredSectionAt(FVectorVMExternalFunctionContext& Context);
	void VMGetUnfilteredSectionAt(FVectorVMExternalFunctionContext& Context);

	void VMGetSectionCount(FVectorVMExternalFunctionContext& Context);
	void VMGetFilteredSectionCount(FVectorVMExternalFunctionContext& Context);
	void VMGetUnfilteredSectionCount(FVectorVMExternalFunctionContext& Context);

	template<typename TRandomHandler>
	void VMRandomSection(FVectorVMExternalFunctionContext& Context);
	template<typename TRandomHandler>
	void VMRandomFilteredSection(FVectorVMExternalFunctionContext& Context);
	void VMRandomUnfilteredSection(FVectorVMExternalFunctionContext& Context);

	// VM Misc Functions
	void VMIsValid(FVectorVMExternalFunctionContext& Context);

	void VMGetLocalToWorld(FVectorVMExternalFunctionContext& Context);
	void VMGetLocalToWorldInverseTransposed(FVectorVMExternalFunctionContext& Context);
	void VMGetWorldVelocity(FVectorVMExternalFunctionContext& Context);

	// Deprecated VM Functions
	template<typename TTransformHandler>
	void VMGetVertexPosition_Deprecated(FVectorVMExternalFunctionContext& Context);
	template<typename TTransformHandler>
	void VMGetTriPosition_Deprecated(FVectorVMExternalFunctionContext& Context);
	template<typename TTransformHandler>
	void VMGetTriPositionAndVelocity_Deprecated(FVectorVMExternalFunctionContext& Context);
	template<typename TTransformHandler>
	void VMGetTriangleTangentBasis_Deprecated(FVectorVMExternalFunctionContext& Context);
	template<typename TTransformHandler>
	void VMGetTriangleNormal_Deprecated(FVectorVMExternalFunctionContext& Context);
};
