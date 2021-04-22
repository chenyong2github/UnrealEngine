// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceRW.h"
#include "ClearQuad.h"
#include "NiagaraComponent.h"
#include "Niagara/Private/NiagaraStats.h"

#include "NiagaraDataInterfaceGrid3DCollection.generated.h"

class FNiagaraSystemInstance;
class UTextureRenderTarget;
class UTextureRenderTargetVolume;

class FGrid3DBuffer
{
public:
	FGrid3DBuffer(int NumX, int NumY, int NumZ, EPixelFormat PixelFormat)
	{
		GridBuffer.Initialize(GPixelFormats[PixelFormat].BlockBytes, NumX, NumY, NumZ, PixelFormat);
		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GridBuffer.NumBytes);
	}

	~FGrid3DBuffer()
	{
		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GridBuffer.NumBytes);
		GridBuffer.Release();
	}

	FTextureRWBuffer3D GridBuffer;	
};

struct FGrid3DCollectionRWInstanceData_GameThread
{
	FIntVector NumCells = FIntVector::ZeroValue;
	FIntVector NumTiles = FIntVector::ZeroValue;
	int32 TotalNumAttributes = 0;
	FVector CellSize = FVector::ZeroVector;
	FVector WorldBBoxSize = FVector::ZeroVector;
	EPixelFormat PixelFormat = EPixelFormat::PF_R32_FLOAT;
#if WITH_EDITOR
	bool bPreviewGrid = false;
	FIntVector4 PreviewAttribute = FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE);
#endif

	bool NeedsRealloc = false;

	/** A binding to the user ptr we're reading the RT from (if we are). */
	FNiagaraParameterDirectBinding<UObject*> RTUserParamBinding;

	UTextureRenderTargetVolume* TargetTexture = nullptr;
	TArray<FNiagaraVariableBase> Vars;
	TArray<uint32> Offsets;

	int32 FindAttributeIndexByName(const FName& InName, int32 NumChannels);
	bool UpdateTargetTexture(ENiagaraGpuBufferFormat BufferFormat);
};

struct FGrid3DCollectionRWInstanceData_RenderThread
{
	FIntVector NumCells = FIntVector::ZeroValue;
	FIntVector NumTiles = FIntVector::ZeroValue;
	int32 TotalNumAttributes = 0;
	FVector CellSize = FVector::ZeroVector;
	FVector WorldBBoxSize = FVector::ZeroVector;
	EPixelFormat PixelFormat = EPixelFormat::PF_R32_FLOAT;
	TArray<int32> AttributeIndices;

	TArray<FName> Vars;
	TArray<int32> VarComponents;
	TArray<uint32> Offsets;
#if WITH_EDITOR
	bool bPreviewGrid = false;
	FIntVector4 PreviewAttribute = FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE);
#endif

	TArray<TUniquePtr<FGrid3DBuffer>> Buffers;
	FGrid3DBuffer* CurrentData = nullptr;
	FGrid3DBuffer* DestinationData = nullptr;

	FReadBuffer PerAttributeData;

	FTextureRHIRef RenderTargetToCopyTo;

	void BeginSimulate(FRHICommandList& RHICmdList);
	void EndSimulate(FRHICommandList& RHICmdList);
};

struct FNiagaraDataInterfaceProxyGrid3DCollectionProxy : public FNiagaraDataInterfaceProxyRW
{
	FNiagaraDataInterfaceProxyGrid3DCollectionProxy() {}

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;
	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override;
	virtual void PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override;
	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override;

	virtual FIntVector GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const override;

	/* List of proxy data for each system instances*/
	// #todo(dmp): this should all be refactored to avoid duplicate code
	TMap<FNiagaraSystemInstanceID, FGrid3DCollectionRWInstanceData_RenderThread> SystemInstancesToProxyData_RT;
};

UCLASS(EditInlineNew, Category = "Grid", meta = (DisplayName = "Grid3D Collection", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceGrid3DCollection : public UNiagaraDataInterfaceGrid3D
{
	GENERATED_UCLASS_BODY()

public:
	DECLARE_NIAGARA_DI_PARAMETER();

	// Number of attributes stored on the grid
	UPROPERTY(EditAnywhere, Category = "Grid")
	int32 NumAttributes;

	/** Reference to a user parameter if we're reading one. */
	UPROPERTY(EditAnywhere, Category = "Grid3DCollection")
	FNiagaraUserParameterBinding RenderTargetUserParameter;

	/** When enabled overrides the format used to store data inside the grid, otherwise uses the project default setting.  Lower bit depth formats will save memory and performance at the cost of precision. */
	UPROPERTY(EditAnywhere, Category = "Grid3DCollection", meta = (EditCondition = "bOverrideFormat"))
	ENiagaraGpuBufferFormat OverrideBufferFormat;

	UPROPERTY(EditAnywhere, Category = "Grid3DCollection", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverrideFormat : 1;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Grid3DCollection", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bPreviewGrid : 1;

	UPROPERTY(EditAnywhere, Category = "Grid3DCollection", meta = (EditCondition = "bPreviewGrid", ToolTip = "When enabled allows you to preview the grid in a debug display") )
	FName PreviewAttribute = NAME_None;
#endif

	virtual void PostInitProperties() override;
	
	//~ UNiagaraDataInterface interface
	// VM functionality
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;

#if WITH_EDITOR	
	virtual void GetFeedback(UNiagaraSystem* Asset, UNiagaraComponent* Component, TArray<FNiagaraDataInterfaceError>& OutErrors,
		TArray<FNiagaraDataInterfaceFeedback>& Warnings, TArray<FNiagaraDataInterfaceFeedback>& Info) override;
#endif

#if WITH_EDITORONLY_DATA
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;
#endif

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override {}
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FGrid3DCollectionRWInstanceData_GameThread); }
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasPostSimulateTick() const override { return true; }

	virtual bool CanExposeVariables() const override { return true; }
	virtual void GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const override;
	virtual bool GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const override;
	//~ UNiagaraDataInterface interface END

private:
	static void CollectAttributesForScript(UNiagaraScript* Script, FName VariableName, TArray<FNiagaraVariableBase>& OutVariables, TArray<uint32>& OutVariableOffsets, int32& TotalAttributes, TArray<FText>* OutWarnings = nullptr);
public:
	/** Finds all attributes by locating the variable name inside the parameter stores. */
	void FindAttributesByName(FName DataInterfaceName, TArray<FNiagaraVariableBase>& OutVariables, TArray<uint32>& OutVariableOffsets, int32& OutNumAttribChannelsFound, TArray<FText>* OutWarnings = nullptr) const;
	/** Finds all attributes by locating the data interface amongst the parameter stores. */
	void FindAttributes(TArray<FNiagaraVariableBase>& OutVariables, TArray<uint32>& OutVariableOffsets, int32& OutNumAttribChannelsFound, TArray<FText>* OutWarnings = nullptr) const;
	// Fills a texture render target 2d with the current data from the simulation
	// #todo(dmp): this will eventually go away when we formalize how data makes it out of Niagara
	// #todo(dmp): reimplement for 3d

	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DeprecatedFunction, DeprecationMessage = "This function has been replaced by object user variables on the emitter to specify render targets to fill with data."))
	virtual bool FillVolumeTexture(const UNiagaraComponent *Component, UVolumeTexture *dest, int AttributeIndex);
	
	UFUNCTION(BlueprintCallable, Category = Niagara, meta = (DeprecatedFunction, DeprecationMessage = "This function has been replaced by object user variables on the emitter to specify render targets to fill with data."))
	virtual bool FillRawVolumeTexture(const UNiagaraComponent *Component, UVolumeTexture*Dest, int &TilesX, int &TilesY, int &TileZ);
	
	UFUNCTION(BlueprintCallable, Category = Niagara)
	virtual void GetRawTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY, int &SizeZ);

	UFUNCTION(BlueprintCallable, Category = Niagara)
	virtual void GetTextureSize(const UNiagaraComponent *Component, int &SizeX, int &SizeY, int &SizeZ);	

	void GetWorldBBoxSize(FVectorVMContext& Context);
	void GetCellSize(FVectorVMContext& Context);

	void GetNumCells(FVectorVMContext& Context);
	void SetNumCells(FVectorVMContext& Context);

	void GetAttributeIndex(FVectorVMContext& Context, const FName& InName, int32 NumChannels);

	static const FString NumTilesName;
	static const FString OneOverNumTilesName;
	static const FString UnitClampMinName;
	static const FString UnitClampMaxName;

	static const FString GridName;
	static const FString OutputGridName;
	static const FString SamplerName;

	static const FName ClearCellFunctionName;
	static const FName CopyPreviousToCurrentForCellFunctionName;

	static const FName SetValueFunctionName;
	static const FName GetValueFunctionName;
	static const FName SampleGridFunctionName;

	static const FName SetVector4ValueFunctionName;
	static const FName SetVector3ValueFunctionName;
	static const FName SetVector2ValueFunctionName;
	static const FName GetVector2ValueFunctionName;
	static const FName SetFloatValueFunctionName;
	static const FName GetPreviousValueAtIndexFunctionName;
	static const FName SamplePreviousGridAtIndexFunctionName;

	static const FName GetPreviousVector4ValueFunctionName;
	static const FName SamplePreviousGridVector4FunctionName;
	static const FName SetVectorValueFunctionName;
	static const FName GetPreviousVectorValueFunctionName;
	static const FName SamplePreviousGridVectorFunctionName;
	static const FName SetVector2DValueFunctionName;
	static const FName GetPreviousVector2DValueFunctionName;
	static const FName SamplePreviousGridVector2DFunctionName;
	static const FName GetPreviousFloatValueFunctionName;
	static const FName SamplePreviousGridFloatFunctionName;
	static const FString AttributeIndicesBaseName;
	static const FString PerAttributeDataName;
	static const TCHAR* VectorComponentNames[];

	static const FName SetNumCellsFunctionName;

	static const FName GetVector4AttributeIndexFunctionName;
	static const FName GetVectorAttributeIndexFunctionName;
	static const FName GetVector2DAttributeIndexFunctionName;
	static const FName GetFloatAttributeIndexFunctionName;

	static const FString AnonymousAttributeString;

#if WITH_EDITOR
	virtual bool SupportsSetupAndTeardownHLSL() const { return true; }
	virtual bool GenerateSetupHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, TConstArrayView<FNiagaraVariable> InArguments, bool bSpawnOnly, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const;
	virtual bool GenerateTeardownHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, TConstArrayView<FNiagaraVariable> InArguments, bool bSpawnOnly, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const;
	virtual bool SupportsIterationSourceNamespaceAttributesHLSL() const override { return true; }
	virtual bool GenerateIterationSourceNamespaceReadAttributesHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, const FNiagaraVariable& IterationSourceVar, TConstArrayView<FNiagaraVariable> InArguments, TConstArrayView<FNiagaraVariable> InAttributes, TConstArrayView<FString> InAttributeHLSLNames, bool bInSetToDefaults, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const override;
	virtual bool GenerateIterationSourceNamespaceWriteAttributesHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, const FNiagaraVariable& IterationSourceVar, TConstArrayView<FNiagaraVariable> InArguments, TConstArrayView<FNiagaraVariable> InAttributes, TConstArrayView<FString> InAttributeHLSLNames, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const override;
#endif

	static int32 GetComponentCountFromFuncName(const FName& FuncName);
	static FNiagaraTypeDefinition GetValueTypeFromFuncName(const FName& FuncName);
	static bool CanCreateVarFromFuncName(const FName& FuncName);
protected:
#if WITH_EDITORONLY_DATA
	void WriteSetHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, int32 InNumChannels, FString& OutHLSL);
	void WriteGetHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, int32 InNumChannels, FString& OutHLSL);
	void WriteSampleHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, int32 InNumChannels, FString& OutHLSL);
	void WriteAttributeGetIndexHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, int32 InNumChannels, FString& OutHLSL);

	const TCHAR* TypeDefinitionToHLSLTypeString(const FNiagaraTypeDefinition& InDef) const;
	FName TypeDefinitionToGetFunctionName(const FNiagaraTypeDefinition& InDef) const;
	FName TypeDefinitionToSetFunctionName(const FNiagaraTypeDefinition& InDef) const;
#endif

	//~ UNiagaraDataInterface interface
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ UNiagaraDataInterface interface END

	TMap<FNiagaraSystemInstanceID, FGrid3DCollectionRWInstanceData_GameThread*> SystemInstancesToProxyData_GT;

	static FNiagaraVariableBase ExposedRTVar;
};
