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
struct FNiagaraDataInterfaceProxy;

struct FNDITransformHandlerNoop
{
	FORCEINLINE void TransformPosition(FVector& V, const FMatrix& M) { }
	FORCEINLINE void TransformVector(FVector& V, const FMatrix& M) { }
	FORCEINLINE void TransformRotation(FQuat& Q, const FMatrix& M) { }
};

struct FNDITransformHandler
{
	FORCEINLINE void TransformPosition(FVector& P, const FMatrix& M) { P = M.TransformPosition(P); }
	FORCEINLINE void TransformVector(FVector& V, const FMatrix& M) { V = M.TransformVector(V).GetUnsafeNormal3(); }
	FORCEINLINE void TransformRotation(FQuat& Q, const FMatrix& M) { Q = M.ToQuat() * Q; }
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

	{};
	FNiagaraDataInterfaceError()
	{};
	/** Returns true if the error can be fixed automatically. */
	bool GetErrorFixable() const
	{
		return Fix.IsBound();
	};

	/** Applies the fix if a delegate is bound for it.*/
	bool TryFixError()
	{
		return Fix.IsBound() ? Fix.Execute() : false;
	};

	/** Full error description text */
	FText GetErrorText() const
	{
		return ErrorText;
	};

	/** Shortened error description text*/
	FText GetErrorSummaryText() const
	{
		return ErrorSummaryText;
	};

private:
	FText ErrorText;
	FText ErrorSummaryText;
	FNiagaraDataInterfaceFix Fix;
};
#endif

//////////////////////////////////////////////////////////////////////////

struct FNiagaraDataInterfaceProxy : TSharedFromThis<FNiagaraDataInterfaceProxy, ESPMode::ThreadSafe>
{
	virtual ~FNiagaraDataInterfaceProxy() {/*check(IsInRenderingThread());*/}

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const = 0;
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) { check(false); }

	virtual void DeferredDestroy() {}

	// #todo(dmp): move all of this stuff to the RW interface to keep it out of here?

	// a set of the shader stages that require the data interface for data output
	TSet<int> OutputShaderStages;

	// a set of the shader stages that require the data interface for setting number of output elements
	TSet<int> IterationShaderStages;
	
	// number of elements to output to
	uint32 ElementCount;

	void SetElementCount(uint32 Count) { ElementCount = Count;  }
	virtual bool IsOutputStage(uint32 CurrentStage) const { return OutputShaderStages.Contains(CurrentStage); }
	virtual bool IsIterationStage(uint32 CurrentStage) const { return IterationShaderStages.Contains(CurrentStage); }

	virtual void ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) { }

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) {}	
	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) {}
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
	// UObject Interface END

	/** Initializes the per instance data for this interface. Returns false if there was some error and the simulation should be disabled. */
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) { return true; }

	/** Destroys the per instance data for this interface. */
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) {}

	/** Ticks the per instance data for this interface, if it has any. */
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) { return false; }
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) { return false; }

	/** Allows the generic class defaults version of this class to specify any dependencies/version/etc that might invalidate the compile. It should never depend on the value of specific properties.*/
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const;

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

	virtual bool RequiresDistanceFieldData() const { return false; }
	virtual bool RequiresDepthBuffer() const { return false; }
	virtual bool RequiresEarlyViewData() const { return false; }

	virtual bool HasTickGroupPrereqs() const { return false; }
	virtual ETickingGroup CalculateTickGroup(void* PerInstanceData) const { return NiagaraFirstTickGroup; }

	/** Determines if this type definition matches to a known data interface type.*/
	static bool IsDataInterfaceType(const FNiagaraTypeDefinition& TypeDef);

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

	virtual void PostExecute() {}

#if WITH_EDITOR	
	/** Refreshes and returns the errors detected with the corresponding data, if any.*/
	virtual TArray<FNiagaraDataInterfaceError> GetErrors() { return TArray<FNiagaraDataInterfaceError>(); }

	/** Validates a function being compiled and allows interface classes to post custom compile errors when their API changes. */
	virtual void ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors);
#endif

	FNiagaraDataInterfaceProxy* GetProxy()
	{
		return Proxy.Get();
	}

protected:
	template<typename T>
	T* GetProxyAs()
	{
		T* TypedProxy = static_cast<T*>(Proxy.Get());
		check(TypedProxy != nullptr);
		return TypedProxy;
	}

	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const;

	TSharedPtr<FNiagaraDataInterfaceProxy, ESPMode::ThreadSafe> Proxy;
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

	FVectorVMContext& Context;
	FNDIParameter<FNiagaraRandInfo> RandParam;

	FNiagaraRandInfo RandInfo;
};
