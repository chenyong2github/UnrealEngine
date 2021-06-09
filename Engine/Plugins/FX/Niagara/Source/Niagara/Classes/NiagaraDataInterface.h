// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Niagara/Public/NiagaraCommon.h"
#include "NiagaraShared.h"
#include "VectorVM.h"
#include "StaticMeshResources.h"
#include "Curves/RichCurve.h"
#include "NiagaraMergeable.h"
#include "NiagaraDataInterfaceBase.h"
#include "NiagaraDataInterface.generated.h"

class INiagaraCompiler;
class UCurveVector;
class UCurveLinearColor;
class UCurveFloat;
class FNiagaraSystemInstance;
class FNiagaraEmitterInstance;
struct FNiagaraDataInterfaceProxy;

struct FNDITransformHandlerNoop
{
	FORCEINLINE void TransformPosition(FVector& V, const FMatrix& M) { }
	FORCEINLINE void TransformVector(FVector& V, const FMatrix& M) { }
	FORCEINLINE void TransformRotation(FQuat& Q1, const FQuat& Q2) { }
};

struct FNDITransformHandler
{
	FORCEINLINE void TransformPosition(FVector& P, const FMatrix& M) { P = M.TransformPosition(P); }
	FORCEINLINE void TransformVector(FVector& V, const FMatrix& M) { V = M.TransformVector(V).GetUnsafeNormal3(); }
	FORCEINLINE void TransformRotation(FQuat& Q1, const FQuat& Q2) { Q1 = Q2 * Q1; }
};

//////////////////////////////////////////////////////////////////////////
// Some helper classes allowing neat, init time binding of templated vm external functions.

struct TNDINoopBinder {};

// Adds a known type to the parameters
template<typename DirectType, typename NextBinder>
struct TNDIExplicitBinder
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		NextBinder::template Bind<ParamTypes..., DirectType>(Interface, BindingInfo, InstanceData, OutFunc);
	}
};

// Binder that tests the location of an operand and adds the correct handler type to the Binding parameters.
template<int32 ParamIdx, typename DataType, typename NextBinder>
struct TNDIParamBinder
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		if (BindingInfo.InputParamLocations[ParamIdx])
		{
			NextBinder::template Bind<ParamTypes..., VectorVM::FExternalFuncConstHandler<DataType>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NextBinder::template Bind<ParamTypes..., VectorVM::FExternalFuncRegisterHandler<DataType>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
	}
};

template<int32 ParamIdx, typename DataType>
struct TNDIParamBinder<ParamIdx, DataType, TNDINoopBinder>
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
	}
};

#define NDI_FUNC_BINDER(ClassName, FuncName) T##ClassName##_##FuncName##Binder

#define DEFINE_NDI_FUNC_BINDER(ClassName, FuncName)\
struct NDI_FUNC_BINDER(ClassName, FuncName)\
{\
	template<typename ... ParamTypes>\
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)\
	{\
		auto Lambda = [Interface](FVectorVMContext& Context) { static_cast<ClassName*>(Interface)->FuncName<ParamTypes...>(Context); };\
		OutFunc = FVMExternalFunction::CreateLambda(Lambda);\
	}\
};

#define DEFINE_NDI_DIRECT_FUNC_BINDER(ClassName, FuncName)\
struct NDI_FUNC_BINDER(ClassName, FuncName)\
{\
	static void Bind(UNiagaraDataInterface* Interface, FVMExternalFunction &OutFunc)\
	{\
		auto Lambda = [Interface](FVectorVMContext& Context) { static_cast<ClassName*>(Interface)->FuncName(Context); };\
		OutFunc = FVMExternalFunction::CreateLambda(Lambda);\
	}\
};

#define DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(ClassName, FuncName)\
struct NDI_FUNC_BINDER(ClassName, FuncName)\
{\
	template <typename ... VarTypes>\
	static void Bind(UNiagaraDataInterface* Interface, FVMExternalFunction &OutFunc, VarTypes... Var)\
	{\
		auto Lambda = [Interface, Var...](FVectorVMContext& Context) { static_cast<ClassName*>(Interface)->FuncName(Context, Var...); };\
		OutFunc = FVMExternalFunction::CreateLambda(Lambda);\
	}\
};

#if WITH_EDITOR
// Helper class for GUI error handling
DECLARE_DELEGATE_RetVal(bool, FNiagaraDataInterfaceFix);
class FNiagaraDataInterfaceError
{
public:
	FNiagaraDataInterfaceError(FText InErrorText,
		FText InErrorSummaryText,
		FNiagaraDataInterfaceFix InFix)
		: ErrorText(InErrorText)
		, ErrorSummaryText(InErrorSummaryText)
		, Fix(InFix)

	{}

	FNiagaraDataInterfaceError()
	{}

	/** Returns true if the error can be fixed automatically. */
	bool GetErrorFixable() const
	{
		return Fix.IsBound();
	}

	/** Applies the fix if a delegate is bound for it.*/
	bool TryFixError()
	{
		return Fix.IsBound() ? Fix.Execute() : false;
	}

	/** Full error description text */
	FText GetErrorText() const
	{
		return ErrorText;
	}

	/** Shortened error description text*/
	FText GetErrorSummaryText() const
	{
		return ErrorSummaryText;
	}

	FORCEINLINE bool operator !=(const FNiagaraDataInterfaceError& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE bool operator == (const FNiagaraDataInterfaceError& Other) const
	{
		return ErrorText.EqualTo(Other.ErrorText) && ErrorSummaryText.EqualTo(Other.ErrorSummaryText);
	}

private:
	FText ErrorText;
	FText ErrorSummaryText;
	FNiagaraDataInterfaceFix Fix;
};

// Helper class for GUI feedback handling
DECLARE_DELEGATE_RetVal(bool, FNiagaraDataInterfaceFix);
class FNiagaraDataInterfaceFeedback
{
public:
	FNiagaraDataInterfaceFeedback(FText InFeedbackText,
		FText InFeedbackSummaryText,
		FNiagaraDataInterfaceFix InFix)
		: FeedbackText(InFeedbackText)
		, FeedbackSummaryText(InFeedbackSummaryText)
		, Fix(InFix)

	{}

	FNiagaraDataInterfaceFeedback()
	{}

	/** Returns true if the feedback can be fixed automatically. */
	bool GetFeedbackFixable() const
	{
		return Fix.IsBound();
	}

	/** Applies the fix if a delegate is bound for it.*/
	bool TryFixFeedback()
	{
		return Fix.IsBound() ? Fix.Execute() : false;
	}

	/** Full feedback description text */
	FText GetFeedbackText() const
	{
		return FeedbackText;
	}

	/** Shortened feedback description text*/
	FText GetFeedbackSummaryText() const
	{
		return FeedbackSummaryText;
	}

	FORCEINLINE bool operator !=(const FNiagaraDataInterfaceFeedback& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE bool operator == (const FNiagaraDataInterfaceFeedback& Other) const
	{
		return FeedbackText.EqualTo(Other.FeedbackText) && FeedbackSummaryText.EqualTo(Other.FeedbackSummaryText);
	}

private:
	FText FeedbackText;
	FText FeedbackSummaryText;
	FNiagaraDataInterfaceFix Fix;
};
#endif

//////////////////////////////////////////////////////////////////////////

struct FNiagaraDataInterfaceProxyRW;
struct FNiagaraDataInterfaceProxy : TSharedFromThis<FNiagaraDataInterfaceProxy, ESPMode::ThreadSafe>
{
	FNiagaraDataInterfaceProxy() {}
	virtual ~FNiagaraDataInterfaceProxy() {/*check(IsInRenderingThread());*/}

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const = 0;
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) { check(false); }

	// #todo(dmp): move all of this stuff to the RW interface to keep it out of here?
	FName SourceDIName;
	
	// a set of the shader stages that require the data interface for data output
	TSet<int> OutputSimulationStages_DEPRECATED;

	// a set of the shader stages that require the data interface for setting number of output elements
	TSet<int> IterationSimulationStages_DEPRECATED;
	
	virtual bool IsOutputStage_DEPRECATED(uint32 CurrentStage) const { return OutputSimulationStages_DEPRECATED.Contains(CurrentStage); }
	virtual bool IsIterationStage_DEPRECATED(uint32 CurrentStage) const { return IterationSimulationStages_DEPRECATED.Contains(CurrentStage); }

	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) { }

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) {}
	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) {}
	virtual void PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) {}

	virtual FNiagaraDataInterfaceProxyRW* AsIterationProxy() { return nullptr; }
};

//////////////////////////////////////////////////////////////////////////

/** Base class for all Niagara data interfaces. */
UCLASS(abstract, EditInlineNew)
class NIAGARA_API UNiagaraDataInterface : public UNiagaraDataInterfaceBase
{
	GENERATED_UCLASS_BODY()

public:
	virtual ~UNiagaraDataInterface();

	// UObject Interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	// UObject Interface END

	/** Does this data interface need setup and teardown for each stage when working a sim stage sim source? */
	virtual bool SupportsSetupAndTeardownHLSL() const { return false; }
	/** Generate the necessary HLSL to set up data when being added as a sim stage sim source. */
	virtual bool GenerateSetupHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, TConstArrayView<FNiagaraVariable> InArguments, bool bSpawnOnly, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const { return false;}
	/** Generate the necessary HLSL to tear down data when being added as a sim stage sim source. */
	virtual bool GenerateTeardownHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, TConstArrayView<FNiagaraVariable> InArguments, bool bSpawnOnly, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const { return false; }
	/** Can this data interface be used as a StackContext parameter map replacement when being used as a sim stage iteration source? */
	virtual bool SupportsIterationSourceNamespaceAttributesHLSL() const { return false; }
	/** Generate the necessary plumbing HLSL at the beginning of the stage where this is used as a sim stage iteration source. Note that this should inject other internal calls using the CustomHLSL node syntax. See GridCollection2D for an example.*/
	virtual bool GenerateIterationSourceNamespaceReadAttributesHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, const FNiagaraVariable& InIterationSourceVariable, TConstArrayView<FNiagaraVariable> InArguments, TConstArrayView<FNiagaraVariable> InAttributes, TConstArrayView<FString> InAttributeHLSLNames, bool bInSetToDefaults, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const { return false; };
	/** Generate the necessary plumbing HLSL at the end of the stage where this is used as a sim stage iteration source. Note that this should inject other internal calls using the CustomHLSL node syntax. See GridCollection2D for an example.*/
	virtual bool GenerateIterationSourceNamespaceWriteAttributesHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, const FNiagaraVariable& InIterationSourceVariable, TConstArrayView<FNiagaraVariable> InArguments, TConstArrayView<FNiagaraVariable> InAttributes, TConstArrayView<FString> InAttributeHLSLNames, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const { return false; };
	/** Used by the translator when dealing with signatures that turn into compiler tags to figure out the precise compiler tag. */
	virtual bool GenerateCompilerTagPrefix(const FNiagaraFunctionSignature& InSignature, FString& OutPrefix) const  { return false; }

#endif

	virtual bool NeedsGPUContextInit() const { return false; }
	virtual bool GPUContextInit(const FNiagaraScriptDataInterfaceCompileInfo& InInfo, void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) const { return false; }

	/** Initializes the per instance data for this interface. Returns false if there was some error and the simulation should be disabled. */
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) { return true; }

	/** Destroys the per instance data for this interface. */
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) {}

	/** Ticks the per instance data for this interface, if it has any. */
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) { return false; }
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) { return false; }

#if WITH_EDITORONLY_DATA
	/** Allows the generic class defaults version of this class to specify any dependencies/version/etc that might invalidate the compile. It should never depend on the value of specific properties.*/
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const;
#endif

	/** Allows data interfaces to influence the compilation of GPU shaders and is only called on the CDO object not the instance. */
	virtual void ModifyCompilationEnvironment(struct FShaderCompilerEnvironment& OutEnvironment) const {}

	/** 
		Subclasses that wish to work with GPU systems/emitters must implement this.
		Those interfaces must fill DataForRenderThread with the data needed to upload to the GPU. It will be the last thing called on this
		data interface for a specific tick.
		This will be consumed by the associated FNiagaraDataInterfaceProxy.
		Note: This class does not own the memory pointed to by DataForRenderThread. It will be recycled automatically. 
			However, if you allocate memory yourself to pass via this buffer you ARE responsible for freeing it when it is consumed by the proxy (Which is what ChaosDestruction does).
			Likewise, the class also does not own the memory in PerInstanceData. That pointer is the pointer passed to PerInstanceTick/PerInstanceTickPostSimulate.
			
		This will not be called if PerInstanceDataPassedToRenderThreadSize is 0.
	*/
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
	{
		check(false);
	}

	/**
	 * The size of the data this class will provide to ProvidePerInstanceDataForRenderThread.
	 * MUST be 16 byte aligned!
	 */
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const 
	{ 
		if (!Proxy)
		{
			return 0;
		}
		check(Proxy);
		return Proxy->PerInstanceDataPassedToRenderThreadSize();
	}

	/** 
	Returns the size of the per instance data for this interface. 0 if this interface has no per instance data. 
	Must depend solely on the class of the interface and not on any particular member data of a individual interface.
	*/
	virtual int32 PerInstanceDataSize()const { return 0; }

	/** Gets all the available functions for this data interface. */
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) {}

	/** Returns the delegate for the passed function signature. */
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) { };
	
	/** Copies the contents of this DataInterface to another.*/
	bool CopyTo(UNiagaraDataInterface* Destination) const;

	/** Determines if this DataInterface is the same as another.*/
	virtual bool Equals(const UNiagaraDataInterface* Other) const;

	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const { return false; }

	virtual bool HasPreSimulateTick() const { return false; }
	virtual bool HasPostSimulateTick() const { return false; }

	virtual bool RequiresDistanceFieldData() const { return false; }
	virtual bool RequiresDepthBuffer() const { return false; }
	virtual bool RequiresEarlyViewData() const { return false; }

	virtual bool HasTickGroupPrereqs() const { return false; }
	virtual ETickingGroup CalculateTickGroup(const void* PerInstanceData) const { return NiagaraFirstTickGroup; }

	/** Used to determine if we need to create GPU resources for the emitter. */
	bool IsUsedWithGPUEmitter(class FNiagaraSystemInstance* SystemInstance) const;

	/** Determines if this type definition matches to a known data interface type.*/
	static bool IsDataInterfaceType(const FNiagaraTypeDefinition& TypeDef);

#if WITH_EDITORONLY_DATA
	/** Allows data interfaces to provide common functionality that will be shared across interfaces on that type. */
	virtual void GetCommonHLSL(FString& OutHLSL)
	{
	}

	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
	{
	}

	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
	{
		return false;
	}

	/**
	Allows data interfaces the opportunity to rename / change the function signature and perform an upgrade.
	Return true if the signature was modified and we need to refresh the pins / name, etc.
	*/
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
	{
		return false;
	}
#endif

	virtual void PostExecute() {}

#if WITH_EDITOR	
	/** Refreshes and returns the errors detected with the corresponding data, if any.*/
	virtual TArray<FNiagaraDataInterfaceError> GetErrors() { return TArray<FNiagaraDataInterfaceError>(); }
	
	/**
		Query the data interface to give feedback to the end user. 
		Note that the default implementation, just calls GetErrors on the DataInterface, but derived classes can do much more.
		Also, InAsset or InComponent may be null values, as the UI for DataInterfaces is displayed in a variety of locations. 
		In these cases, only provide information that is relevant to that context.
	*/
	virtual void GetFeedback(UNiagaraSystem* InAsset, UNiagaraComponent* InComponent, TArray<FNiagaraDataInterfaceError>& OutErrors, TArray<FNiagaraDataInterfaceFeedback>& OutWarnings, TArray<FNiagaraDataInterfaceFeedback>& OutInfo);

	static void GetFeedback(UNiagaraDataInterface* DataInterface, TArray<FNiagaraDataInterfaceError>& Errors, TArray<FNiagaraDataInterfaceFeedback>& Warnings,
		TArray<FNiagaraDataInterfaceFeedback>& Info);

	/** Validates a function being compiled and allows interface classes to post custom compile errors when their API changes. */
	virtual void ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors);

	void RefreshErrors();

	FSimpleMulticastDelegate& OnErrorsRefreshed();

#endif

    /** Method to add asset tags that are specific to this data interface. By default we add in how many instances of this class exist in the list.*/
	virtual void GetAssetTagsForContext(const UObject* InAsset, const TArray<const UNiagaraDataInterface*>& InProperties, TMap<FName, uint32>& NumericKeys, TMap<FName, FString>& StringKeys) const;
	virtual bool CanExposeVariables() const { return false; }
	virtual void GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const {}
	virtual bool GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const { return false; }

	virtual bool CanRenderVariablesToCanvas() const { return false; }
	virtual void GetCanvasVariables(TArray<FNiagaraVariableBase>& OutVariables) const { }
	virtual bool RenderVariableToCanvas(FNiagaraSystemInstanceID SystemInstanceID, FName VariableName, class FCanvas* Canvas, const FIntRect& DrawRect) const { return false; }

	FNiagaraDataInterfaceProxy* GetProxy()
	{
		return Proxy.Get();
	}

	/**
	* Allows a DI to specify data dependencies between emitters, so the system can ensure that the emitter instances are executed in the correct order.
	* The Dependencies array may already contain items, and this method should only append to it.
	*/
	virtual void GetEmitterDependencies(UNiagaraSystem* Asset, TArray<UNiagaraEmitter*>& Dependencies) const {}

	virtual bool ReadsEmitterParticleData(const FString& EmitterName) const { return false; }

protected:
	virtual void PushToRenderThreadImpl() {}

public:
	void PushToRenderThread()
	{
		if (bUsedByGPUEmitter && bRenderDataDirty)
		{
			PushToRenderThreadImpl();
			bRenderDataDirty = false;
		}
	}

	void MarkRenderDataDirty()
	{
		bRenderDataDirty = true;
		PushToRenderThread();
	}

	void SetUsedByGPUEmitter(bool bUsed = true)
	{
		check(IsInGameThread());
		bUsedByGPUEmitter = bUsed;
		PushToRenderThread();
	}

protected:
	template<typename T>
	T* GetProxyAs()
	{
		T* TypedProxy = static_cast<T*>(Proxy.Get());
		check(TypedProxy != nullptr);
		return TypedProxy;
	}

	template<typename T>
	const T* GetProxyAs() const
	{
		const T* TypedProxy = static_cast<const T*>(Proxy.Get());
		check(TypedProxy != nullptr);
		return TypedProxy;
	}

	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const;

	TUniquePtr<FNiagaraDataInterfaceProxy> Proxy;

	uint32 bRenderDataDirty : 1;
	uint32 bUsedByGPUEmitter : 1;

private:
#if WITH_EDITOR
	FSimpleMulticastDelegate OnErrorsRefreshedDelegate;
#endif
};

/** Helper class for decoding NDI parameters into a usable struct type. */
template<typename T>
struct FNDIParameter
{
	FNDIParameter(FVectorVMContext& Context) = delete;
	FORCEINLINE void GetAndAdvance(T& OutValue) = delete;
	FORCEINLINE bool IsConstant() const = delete;
};

template<>
struct FNDIParameter<FNiagaraRandInfo>
{
	VectorVM::FExternalFuncInputHandler<int32> Seed1Param;
	VectorVM::FExternalFuncInputHandler<int32> Seed2Param;
	VectorVM::FExternalFuncInputHandler<int32> Seed3Param;
	
	FVectorVMContext& Context;

	FNDIParameter(FVectorVMContext& InContext)
		: Context(InContext)
	{
		Seed1Param.Init(Context);
		Seed2Param.Init(Context);
		Seed3Param.Init(Context);
	}

	FORCEINLINE void GetAndAdvance(FNiagaraRandInfo& OutValue)
	{
		OutValue.Seed1 = Seed1Param.GetAndAdvance();
		OutValue.Seed2 = Seed2Param.GetAndAdvance();
		OutValue.Seed3 = Seed3Param.GetAndAdvance();
	}


	FORCEINLINE bool IsConstant()const
	{
		return Seed1Param.IsConstant() && Seed2Param.IsConstant() && Seed3Param.IsConstant();
	}
};

struct FNDIRandomHelper
{
	FNDIRandomHelper(FVectorVMContext& InContext)
		: Context(InContext)
		, RandParam(Context)
	{

	}

	FORCEINLINE void GetAndAdvance()
	{
		RandParam.GetAndAdvance(RandInfo);
	}

	FORCEINLINE bool IsDeterministic()
	{
		return RandInfo.Seed3 != INDEX_NONE;
	}

	//////////////////////////////////////////////////////////////////////////
	
	FORCEINLINE_DEBUGGABLE FVector4 Rand4(int32 InstanceIndex)
	{
		if (IsDeterministic())
		{
			int32 RandomCounter = Context.RandCounters[InstanceIndex]++;

			FIntVector4 v = FIntVector4(RandomCounter, RandInfo.Seed1, RandInfo.Seed2, RandInfo.Seed3) * 1664525 + FIntVector4(1013904223);

			v.X += v.Y*v.W;
			v.Y += v.Z*v.X;
			v.Z += v.X*v.Y;
			v.W += v.Y*v.Z;
			v.X += v.Y*v.W;
			v.Y += v.Z*v.X;
			v.Z += v.X*v.Y;
			v.W += v.Y*v.Z;

			// NOTE(mv): We can use 24 bits of randomness, as all integers in [0, 2^24] 
			//           are exactly representable in single precision floats.
			//           We use the upper 24 bits as they tend to be higher quality.

			// NOTE(mv): The divide can often be folded with the range scale in the rand functions
			return FVector4((v >> 8) & 0x00ffffff) / 16777216.0; // 0x01000000 == 16777216
			// return float4((v >> 8) & 0x00ffffff) * (1.0/16777216.0); // bugged, see UE-67738
		}
		else
		{
			return FVector4(Context.RandStream.GetFraction(), Context.RandStream.GetFraction(), Context.RandStream.GetFraction(), Context.RandStream.GetFraction());
		}
	}

	FORCEINLINE_DEBUGGABLE FVector Rand3(int32 InstanceIndex)
	{
		if (IsDeterministic())
		{
			int32 RandomCounter = Context.RandCounters[InstanceIndex]++;

			FIntVector v = FIntVector(RandInfo.Seed1, RandInfo.Seed2, RandomCounter | (RandInfo.Seed3 << 16)) * 1664525 + FIntVector(1013904223);

			v.X += v.Y*v.Z;
			v.Y += v.Z*v.X;
			v.Z += v.X*v.Y;
			v.X += v.Y*v.Z;
			v.Y += v.Z*v.X;
			v.Z += v.X*v.Y;

			return FVector((v >> 8) & 0x00ffffff) / 16777216.0; // 0x01000000 == 16777216
		}
		else
		{
			return FVector(Context.RandStream.GetFraction(), Context.RandStream.GetFraction(), Context.RandStream.GetFraction());
		}
	}

	FORCEINLINE_DEBUGGABLE FVector2D Rand2(int32 InstanceIndex)
	{
		if (IsDeterministic())
		{
			FVector Rand3D = Rand3(InstanceIndex);
			return FVector2D(Rand3D.X, Rand3D.Y);
		}
		else
		{
			return FVector2D(Context.RandStream.GetFraction(), Context.RandStream.GetFraction());
		}
	}

	FORCEINLINE_DEBUGGABLE float Rand(int32 InstanceIndex)
	{
		if (IsDeterministic())
		{
			return Rand3(InstanceIndex).X;
		}
		else
		{
			return Context.RandStream.GetFraction();
		}
	}

	FORCEINLINE_DEBUGGABLE FVector4 RandRange(int32 InstanceIndex, FVector4 Min, FVector4 Max)
	{
		FVector4 Range = Max - Min;
		return Min + (Rand(InstanceIndex) * Range);
	}

	FORCEINLINE_DEBUGGABLE FVector RandRange(int32 InstanceIndex, FVector Min, FVector Max)
	{
		FVector Range = Max - Min;
		return Min + (Rand(InstanceIndex) * Range);
	}

	FORCEINLINE_DEBUGGABLE FVector2D RandRange(int32 InstanceIndex, FVector2D Min, FVector2D Max)
	{
		FVector2D Range = Max - Min;
		return Min + (Rand(InstanceIndex) * Range);
	}

	FORCEINLINE_DEBUGGABLE float RandRange(int32 InstanceIndex, float Min, float Max)
	{
		float Range = Max - Min;
		return Min + (Rand(InstanceIndex) * Range);
	}

	FORCEINLINE_DEBUGGABLE int32 RandRange(int32 InstanceIndex, int32 Min, int32 Max)
	{
		// NOTE: Scaling a uniform float range provides better distribution of 
		//       numbers than using %.
		// NOTE: Inclusive! So [0, x] instead of [0, x)
		int32 Range = Max - Min;
		return Min + (int(Rand(InstanceIndex) * (Range + 1)));
	}

	FORCEINLINE_DEBUGGABLE FVector RandomBarycentricCoord(int32 InstanceIndex)
	{
		//TODO: This is gonna be slooooow. Move to an LUT possibly or find faster method.
		//Can probably handle lower quality randoms / uniformity for a decent speed win.
		FVector2D r = Rand2(InstanceIndex);
		float sqrt0 = FMath::Sqrt(r.X);
		float sqrt1 = FMath::Sqrt(r.Y);
		return FVector(1.0f - sqrt0, sqrt0 * (1.0 - r.Y), r.Y * sqrt0);
	}

	FVectorVMContext& Context;
	FNDIParameter<FNiagaraRandInfo> RandParam;

	FNiagaraRandInfo RandInfo;
};

//Helper to deal with types with potentially several input registers.
template<typename T>
struct FNDIInputParam
{
	VectorVM::FExternalFuncInputHandler<T> Data;
	FORCEINLINE FNDIInputParam(FVectorVMContext& Context) : Data(Context) {}
	FORCEINLINE T GetAndAdvance() { return Data.GetAndAdvance(); }
};

template<>
struct FNDIInputParam<FNiagaraBool>
{
	VectorVM::FExternalFuncInputHandler<FNiagaraBool> Data;
	FORCEINLINE FNDIInputParam(FVectorVMContext& Context) : Data(Context) {}
	FORCEINLINE bool GetAndAdvance() { return Data.GetAndAdvance().GetValue(); }
};

template<>
struct FNDIInputParam<bool>
{
	VectorVM::FExternalFuncInputHandler<FNiagaraBool> Data;
	FORCEINLINE FNDIInputParam(FVectorVMContext& Context) : Data(Context) {}
	FORCEINLINE bool GetAndAdvance() { return Data.GetAndAdvance().GetValue(); }
};

template<>
struct FNDIInputParam<FVector2D>
{
	VectorVM::FExternalFuncInputHandler<float> X;
	VectorVM::FExternalFuncInputHandler<float> Y;
	FORCEINLINE FNDIInputParam(FVectorVMContext& Context) : X(Context), Y(Context) {}
	FORCEINLINE FVector2D GetAndAdvance() { return FVector2D(X.GetAndAdvance(), Y.GetAndAdvance()); }
};

template<>
struct FNDIInputParam<FVector>
{
	VectorVM::FExternalFuncInputHandler<float> X;
	VectorVM::FExternalFuncInputHandler<float> Y;
	VectorVM::FExternalFuncInputHandler<float> Z;
	FNDIInputParam(FVectorVMContext& Context) : X(Context), Y(Context), Z(Context) {}
	FORCEINLINE FVector GetAndAdvance() { return FVector(X.GetAndAdvance(), Y.GetAndAdvance(), Z.GetAndAdvance()); }
};

template<>
struct FNDIInputParam<FVector4>
{
	VectorVM::FExternalFuncInputHandler<float> X;
	VectorVM::FExternalFuncInputHandler<float> Y;
	VectorVM::FExternalFuncInputHandler<float> Z;
	VectorVM::FExternalFuncInputHandler<float> W;
	FORCEINLINE FNDIInputParam(FVectorVMContext& Context) : X(Context), Y(Context), Z(Context), W(Context) {}
	FORCEINLINE FVector4 GetAndAdvance() { return FVector4(X.GetAndAdvance(), Y.GetAndAdvance(), Z.GetAndAdvance(), W.GetAndAdvance()); }
};

template<>
struct FNDIInputParam<FQuat>
{
	VectorVM::FExternalFuncInputHandler<float> X;
	VectorVM::FExternalFuncInputHandler<float> Y;
	VectorVM::FExternalFuncInputHandler<float> Z;
	VectorVM::FExternalFuncInputHandler<float> W;
	FORCEINLINE FNDIInputParam(FVectorVMContext& Context) : X(Context), Y(Context), Z(Context), W(Context) {}
	FORCEINLINE FQuat GetAndAdvance() { return FQuat(X.GetAndAdvance(), Y.GetAndAdvance(), Z.GetAndAdvance(), W.GetAndAdvance()); }
};

template<>
struct FNDIInputParam<FLinearColor>
{
	VectorVM::FExternalFuncInputHandler<float> R;
	VectorVM::FExternalFuncInputHandler<float> G;
	VectorVM::FExternalFuncInputHandler<float> B;
	VectorVM::FExternalFuncInputHandler<float> A;
	FORCEINLINE FNDIInputParam(FVectorVMContext& Context) : R(Context), G(Context), B(Context), A(Context) {}
	FORCEINLINE FLinearColor GetAndAdvance() { return FLinearColor(R.GetAndAdvance(), G.GetAndAdvance(), B.GetAndAdvance(), A.GetAndAdvance()); }
};

template<>
struct FNDIInputParam<FNiagaraID>
{
	VectorVM::FExternalFuncInputHandler<int32> Index;
	VectorVM::FExternalFuncInputHandler<int32> AcquireTag;
	FORCEINLINE FNDIInputParam(FVectorVMContext& Context) : Index(Context), AcquireTag(Context) {}
	FORCEINLINE FNiagaraID GetAndAdvance() { return FNiagaraID(Index.GetAndAdvance(), AcquireTag.GetAndAdvance()); }
};

//Helper to deal with types with potentially several output registers.
template<typename T>
struct FNDIOutputParam
{
	VectorVM::FExternalFuncRegisterHandler<T> Data;
	FORCEINLINE FNDIOutputParam(FVectorVMContext& Context) : Data(Context) {}
	FORCEINLINE bool IsValid() const { return Data.IsValid();  }
	FORCEINLINE void SetAndAdvance(T Val) { *Data.GetDestAndAdvance() = Val; }
};

template<>
struct FNDIOutputParam<FNiagaraBool>
{
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> Data;
	FORCEINLINE FNDIOutputParam(FVectorVMContext& Context) : Data(Context) {}
	FORCEINLINE bool IsValid() const { return Data.IsValid(); }
	FORCEINLINE void SetAndAdvance(bool Val) { Data.GetDestAndAdvance()->SetValue(Val); }
};

template<>
struct FNDIOutputParam<bool>
{
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> Data;
	FORCEINLINE FNDIOutputParam(FVectorVMContext& Context) : Data(Context) {}
	FORCEINLINE bool IsValid() const { return Data.IsValid(); }
	FORCEINLINE void SetAndAdvance(bool Val) { Data.GetDestAndAdvance()->SetValue(Val); }
};

template<>
struct FNDIOutputParam<FVector2D>
{
	VectorVM::FExternalFuncRegisterHandler<float> X;
	VectorVM::FExternalFuncRegisterHandler<float> Y;
	FORCEINLINE FNDIOutputParam(FVectorVMContext& Context) : X(Context), Y(Context) {}
	FORCEINLINE bool IsValid() const { return X.IsValid() || Y.IsValid(); }
	FORCEINLINE void SetAndAdvance(FVector2D Val)
	{
		*X.GetDestAndAdvance() = Val.X;
		*Y.GetDestAndAdvance() = Val.Y;
	}
};

template<>
struct FNDIOutputParam<FVector>
{
	VectorVM::FExternalFuncRegisterHandler<float> X;
	VectorVM::FExternalFuncRegisterHandler<float> Y;
	VectorVM::FExternalFuncRegisterHandler<float> Z;
	FNDIOutputParam(FVectorVMContext& Context) : X(Context), Y(Context), Z(Context) {}
	FORCEINLINE bool IsValid() const { return X.IsValid() || Y.IsValid() || Z.IsValid(); }
	FORCEINLINE void SetAndAdvance(FVector Val)
	{
		*X.GetDestAndAdvance() = Val.X;
		*Y.GetDestAndAdvance() = Val.Y;
		*Z.GetDestAndAdvance() = Val.Z;
	}
};

template<>
struct FNDIOutputParam<FVector4>
{
	VectorVM::FExternalFuncRegisterHandler<float> X;
	VectorVM::FExternalFuncRegisterHandler<float> Y;
	VectorVM::FExternalFuncRegisterHandler<float> Z;
	VectorVM::FExternalFuncRegisterHandler<float> W;
	FORCEINLINE FNDIOutputParam(FVectorVMContext& Context) : X(Context), Y(Context), Z(Context), W(Context) {}
	FORCEINLINE bool IsValid() const { return X.IsValid() || Y.IsValid() || Z.IsValid() || W.IsValid(); }
	FORCEINLINE void SetAndAdvance(FVector4 Val)
	{
		*X.GetDestAndAdvance() = Val.X;
		*Y.GetDestAndAdvance() = Val.Y;
		*Z.GetDestAndAdvance() = Val.Z;
		*W.GetDestAndAdvance() = Val.W;
	}
};

template<>
struct FNDIOutputParam<FQuat>
{
	VectorVM::FExternalFuncRegisterHandler<float> X;
	VectorVM::FExternalFuncRegisterHandler<float> Y;
	VectorVM::FExternalFuncRegisterHandler<float> Z;
	VectorVM::FExternalFuncRegisterHandler<float> W;
	FORCEINLINE FNDIOutputParam(FVectorVMContext& Context) : X(Context), Y(Context), Z(Context), W(Context) {}
	FORCEINLINE bool IsValid() const { return X.IsValid() || Y.IsValid() || Z.IsValid() || W.IsValid(); }
	FORCEINLINE void SetAndAdvance(FQuat Val)
	{
		*X.GetDestAndAdvance() = Val.X;
		*Y.GetDestAndAdvance() = Val.Y;
		*Z.GetDestAndAdvance() = Val.Z;
		*W.GetDestAndAdvance() = Val.W;
	}
};

template<>
struct FNDIOutputParam<FMatrix>
{
	VectorVM::FExternalFuncRegisterHandler<float> Out00;
	VectorVM::FExternalFuncRegisterHandler<float> Out01;
	VectorVM::FExternalFuncRegisterHandler<float> Out02;
	VectorVM::FExternalFuncRegisterHandler<float> Out03;
	VectorVM::FExternalFuncRegisterHandler<float> Out04;
	VectorVM::FExternalFuncRegisterHandler<float> Out05;
	VectorVM::FExternalFuncRegisterHandler<float> Out06;
	VectorVM::FExternalFuncRegisterHandler<float> Out07;
	VectorVM::FExternalFuncRegisterHandler<float> Out08;
	VectorVM::FExternalFuncRegisterHandler<float> Out09;
	VectorVM::FExternalFuncRegisterHandler<float> Out10;
	VectorVM::FExternalFuncRegisterHandler<float> Out11;
	VectorVM::FExternalFuncRegisterHandler<float> Out12;
	VectorVM::FExternalFuncRegisterHandler<float> Out13;
	VectorVM::FExternalFuncRegisterHandler<float> Out14;
	VectorVM::FExternalFuncRegisterHandler<float> Out15;

	FORCEINLINE FNDIOutputParam(FVectorVMContext& Context) : Out00(Context), Out01(Context), Out02(Context), Out03(Context), Out04(Context), Out05(Context),
		Out06(Context), Out07(Context), Out08(Context), Out09(Context), Out10(Context), Out11(Context), Out12(Context), Out13(Context), Out14(Context), Out15(Context)	{}
	FORCEINLINE bool IsValid() const { return Out00.IsValid(); }
	FORCEINLINE void SetAndAdvance(const FMatrix& Val)
	{
		*Out00.GetDestAndAdvance() = Val.M[0][0];
		*Out01.GetDestAndAdvance() = Val.M[0][1];
		*Out02.GetDestAndAdvance() = Val.M[0][2];
		*Out03.GetDestAndAdvance() = Val.M[0][3];
		*Out04.GetDestAndAdvance() = Val.M[1][0];
		*Out05.GetDestAndAdvance() = Val.M[1][1];
		*Out06.GetDestAndAdvance() = Val.M[1][2];
		*Out07.GetDestAndAdvance() = Val.M[1][3];
		*Out08.GetDestAndAdvance() = Val.M[2][0];
		*Out09.GetDestAndAdvance() = Val.M[2][1];
		*Out10.GetDestAndAdvance() = Val.M[2][2];
		*Out11.GetDestAndAdvance() = Val.M[2][3];
		*Out12.GetDestAndAdvance() = Val.M[3][0];
		*Out13.GetDestAndAdvance() = Val.M[3][1];
		*Out14.GetDestAndAdvance() = Val.M[3][2];
		*Out15.GetDestAndAdvance() = Val.M[3][3];
	}
};

template<>
struct FNDIOutputParam<FLinearColor>
{
	VectorVM::FExternalFuncRegisterHandler<float> R;
	VectorVM::FExternalFuncRegisterHandler<float> G;
	VectorVM::FExternalFuncRegisterHandler<float> B;
	VectorVM::FExternalFuncRegisterHandler<float> A;
	FORCEINLINE FNDIOutputParam(FVectorVMContext& Context) : R(Context), G(Context), B(Context), A(Context) {}
	FORCEINLINE bool IsValid() const { return R.IsValid() || G.IsValid() || B.IsValid() || A.IsValid(); }
	FORCEINLINE void SetAndAdvance(FLinearColor Val)
	{
		*R.GetDestAndAdvance() = Val.R;
		*G.GetDestAndAdvance() = Val.G;
		*B.GetDestAndAdvance() = Val.B;
		*A.GetDestAndAdvance() = Val.A;
	}
};

template<>
struct FNDIOutputParam<FNiagaraID>
{
	VectorVM::FExternalFuncRegisterHandler<int32> Index;
	VectorVM::FExternalFuncRegisterHandler<int32> AcquireTag;
	FORCEINLINE FNDIOutputParam(FVectorVMContext& Context) : Index(Context), AcquireTag(Context) {}
	FORCEINLINE bool IsValid() const { return Index.IsValid() || AcquireTag.IsValid(); }
	FORCEINLINE void SetAndAdvance(FNiagaraID Val)
	{
		*Index.GetDestAndAdvance() = Val.Index;
		*AcquireTag.GetDestAndAdvance() = Val.AcquireTag;
	}
};

class FNDI_GeneratedData
{
public:
	virtual ~FNDI_GeneratedData() = default;

	typedef uint32 TypeHash;

	virtual void Tick(ETickingGroup TickGroup, float DeltaSeconds) = 0;
};

class FNDI_SharedResourceUsage
{
public:
	FNDI_SharedResourceUsage() = default;
	FNDI_SharedResourceUsage(bool InRequiresCpuAccess, bool InRequiresGpuAccess)
		: RequiresCpuAccess(InRequiresCpuAccess)
		, RequiresGpuAccess(InRequiresGpuAccess)
	{}

	bool IsValid() const { return RequiresCpuAccess || RequiresGpuAccess; }

	bool RequiresCpuAccess = false;
	bool RequiresGpuAccess = false;
};

template<typename ResourceType, typename UsageType>
class FNDI_SharedResourceHandle
{
	using HandleType = FNDI_SharedResourceHandle<ResourceType, UsageType>;

public:
	FNDI_SharedResourceHandle()
		: Resource(nullptr)
	{}

	FNDI_SharedResourceHandle(UsageType InUsage, const TSharedPtr<ResourceType>& InResource, bool bNeedsDataImmediately)
		: Usage(InUsage)
		, Resource(InResource)
	{
		if (ResourceType* ResourceData = Resource.Get())
		{
			ResourceData->RegisterUser(Usage, bNeedsDataImmediately);
		}
	}

	FNDI_SharedResourceHandle(const HandleType& Other) = delete;
	FNDI_SharedResourceHandle(HandleType&& Other)
		: Usage(Other.Usage)
		, Resource(Other.Resource)
	{
		Other.Resource = nullptr;
	}

	~FNDI_SharedResourceHandle()
	{
		if (ResourceType* ResourceData = Resource.Get())
		{
			ResourceData->UnregisterUser(Usage);
		}
	}

	FNDI_SharedResourceHandle& operator=(const HandleType& Other) = delete;
	FNDI_SharedResourceHandle& operator=(HandleType&& Other)
	{
		if (this != &Other)
		{
			if (ResourceType* ResourceData = Resource.Get())
			{
				ResourceData->UnregisterUser(Usage);
			}

			Usage = Other.Usage;
			Resource = Other.Resource;
			Other.Resource = nullptr;
		}

		return *this;
	}

	explicit operator bool() const
	{
		return Resource.IsValid();
	}

	const ResourceType& ReadResource() const
	{
		return *Resource;
	}

	UsageType Usage;

private:
	TSharedPtr<ResourceType> Resource;
};