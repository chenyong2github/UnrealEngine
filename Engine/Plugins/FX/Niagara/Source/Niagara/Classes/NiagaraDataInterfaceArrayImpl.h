// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterfaceArray.h"

template<typename TArrayType>
struct FNDIArrayImplHelperBase
{
	static constexpr bool bSupportsCPU = true;
	static constexpr bool bSupportsGPU = true;

	//static constexpr TCHAR const* HLSLValueTypeName = TEXT("float4");
	//static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float4");
	//static constexpr EPixelFormat PixelFormat = PF_R32_FLOAT;
	//static FRHIShaderResourceView* GetDummyBuffer() { return FNiagaraRenderer::GetDummyFloat4Buffer(); }
	//static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetIntDef(); }

	static void GPUGetFetchHLSL(FString& OutHLSL, const TCHAR* BufferName) { OutHLSL.Appendf(TEXT("OutValue = %s[ClampedIndex];"), BufferName); }
	static int32 GPUGetTypeStride() { return sizeof(TArrayType); }
};

template<typename TArrayType>
struct FNDIArrayImplHelper : public FNDIArrayImplHelperBase<TArrayType>
{
};

struct FNiagaraDataInterfaceProxyArrayImpl : public FNiagaraDataInterfaceProxy
{
	FReadBuffer		Buffer;
	int32			NumElements = 0;

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }
};

struct FNiagaraDataInterfaceArrayImplHelper
{
	static const FName Function_GetNumName;
	static const FName Function_GetValueName;

	static FString GetBufferName(const FString& InterfaceName);
	static FString GetBufferSizeName(const FString& InterfaceName);
};

struct FNiagaraDataInterfaceParametersCS_ArrayImpl : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_ArrayImpl, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		BufferParam.Bind(ParameterMap, *FNiagaraDataInterfaceArrayImplHelper::GetBufferName(ParameterInfo.DataInterfaceHLSLSymbol));
		BufferSizeParam.Bind(ParameterMap, *FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void SetBuffer(FRHICommandList& RHICmdList, FRHIComputeShader* ComputeShaderRHI, FRHIShaderResourceView* BufferSRV, int32 NumElements) const
	{
		SetSRVParameter(RHICmdList, ComputeShaderRHI, BufferParam, BufferSRV);

		// Set Real BufferSize & BufferSize-1, we are guaranteed at least one element on the GPU so never go below 0
		int32 BuferSizeData[] = { NumElements, FMath::Max(0, NumElements - 1) };
		SetShaderValue(RHICmdList, ComputeShaderRHI, BufferSizeParam, BuferSizeData);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, BufferParam);
	LAYOUT_FIELD(FShaderParameter, BufferSizeParam);
};

template<typename TArrayType, typename TObjectType>
struct FNiagaraDataInterfaceArrayImpl : public INiagaraDataInterfaceArrayImpl
{
	TUniquePtr<FNiagaraDataInterfaceProxy>& Proxy;
	TArray<TArrayType>& Data;

	FNiagaraDataInterfaceArrayImpl(TUniquePtr<FNiagaraDataInterfaceProxy>& InProxy, TArray<TArrayType>& InData)
		: Proxy(InProxy)
		, Data(InData)
	{
	}

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) const override
	{
		OutFunctions.Reserve(OutFunctions.Num() + 2);

		{
			FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_GetNumName;
			//#if WITH_EDITORONLY_DATA
			//		Sig.Description = NSLOCTEXT("Niagara", "GetNumDescription", "This function returns the properties of the current view. Only valid for gpu particles.");
			//#endif
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			Sig.bExperimental = true;
			Sig.bSupportsCPU = FNDIArrayImplHelper<TArrayType>::bSupportsCPU;
			Sig.bSupportsGPU = FNDIArrayImplHelper<TArrayType>::bSupportsGPU;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TObjectType::StaticClass()), TEXT("Array interface")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num")));
		}

		{
			FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_GetValueName;
			//#if WITH_EDITORONLY_DATA
			//		Sig.Description = NSLOCTEXT("Niagara", "GetValueDescription", "This function returns the properties of the current view. Only valid for gpu particles.");
			//#endif
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			Sig.bExperimental = true;
			Sig.bSupportsCPU = FNDIArrayImplHelper<TArrayType>::bSupportsCPU;
			Sig.bSupportsGPU = FNDIArrayImplHelper<TArrayType>::bSupportsGPU;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TObjectType::StaticClass()), TEXT("Array interface")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
			Sig.Outputs.Add(FNiagaraVariable(FNDIArrayImplHelper<TArrayType>::GetTypeDefinition(), TEXT("Value")));
		}
	}

	template<typename T = FNDIArrayImplHelper<TArrayType>>
	typename TEnableIf<T::bSupportsCPU>::Type GetVMExternalFunction_Internal(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_GetNumName)
		{
			check(BindingInfo.GetNumInputs() == 0 && BindingInfo.GetNumOutputs() == 1);
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetNum(Context); });
		}
		else if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_GetValueName)
		{
			// Note: Outputs is variable based upon type
			//check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetValue(Context); });
		}
	}

	template<typename T = FNDIArrayImplHelper<TArrayType>>
	typename TEnableIf<!T::bSupportsCPU>::Type GetVMExternalFunction_Internal(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
	}

	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override
	{
		GetVMExternalFunction_Internal(BindingInfo, InstanceData, OutFunc);
	}

	template<typename T = FNDIArrayImplHelper<TArrayType>>
	typename TEnableIf<T::bSupportsGPU>::Type GetParameterDefinitionHLSL_Internal(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) const
	{
		OutHLSL.Appendf(TEXT("Buffer<%s> %s;\n"), FNDIArrayImplHelper<TArrayType>::HLSLBufferTypeName, *FNiagaraDataInterfaceArrayImplHelper::GetBufferName(ParamInfo.DataInterfaceHLSLSymbol));
		OutHLSL.Appendf(TEXT("int %s[2];\n"), *FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(ParamInfo.DataInterfaceHLSLSymbol));
	}

	template<typename T = FNDIArrayImplHelper<TArrayType>>
	typename TEnableIf<!T::bSupportsGPU>::Type GetParameterDefinitionHLSL_Internal(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) const
	{
	}

	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) const override
	{
		GetParameterDefinitionHLSL_Internal(ParamInfo, OutHLSL);
	}

	template<typename T = FNDIArrayImplHelper<TArrayType>>
	typename TEnableIf<T::bSupportsGPU, bool>::Type GetFunctionHLSL_Internal(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) const
	{
		if (FunctionInfo.DefinitionName == FNiagaraDataInterfaceArrayImplHelper::Function_GetNumName)
		{
			OutHLSL.Appendf(TEXT("void %s(out int OutValue) { OutValue = %s[0]; }\n"), *FunctionInfo.InstanceName, *FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(ParamInfo.DataInterfaceHLSLSymbol));
			return true;
		}
		else if (FunctionInfo.DefinitionName == FNiagaraDataInterfaceArrayImplHelper::Function_GetValueName)
		{
			OutHLSL.Appendf(TEXT("void %s(int Index, out %s OutValue) { int ClampedIndex = clamp(Index, 0, %s[1]); "), *FunctionInfo.InstanceName, FNDIArrayImplHelper<TArrayType>::HLSLValueTypeName, *FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(ParamInfo.DataInterfaceHLSLSymbol));
			T::GPUGetFetchHLSL(OutHLSL, *FNiagaraDataInterfaceArrayImplHelper::GetBufferName(ParamInfo.DataInterfaceHLSLSymbol));
			OutHLSL.Append(TEXT(" }\n"));
			return true;
		}
		return false;
	}

	template<typename T = FNDIArrayImplHelper<TArrayType>>
	typename TEnableIf<!T::bSupportsGPU, bool>::Type GetFunctionHLSL_Internal(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) const
	{
		return false;
	}

	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) const override
	{
		return GetFunctionHLSL_Internal(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL);
	}

	virtual bool CopyToInternal(INiagaraDataInterfaceArrayImpl* InDestination) const override
	{
		auto Destination = static_cast<FNiagaraDataInterfaceArrayImpl<TArrayType, TObjectType>*>(InDestination);
		Destination->Data = Data;
		Destination->PushToRenderThread();
		return true;
	}

	virtual bool Equals(const INiagaraDataInterfaceArrayImpl* InOther) const override
	{
		auto Other = static_cast<const FNiagaraDataInterfaceArrayImpl<TArrayType, TObjectType>*>(InOther);
		return Other->Data == Data;
	}

	template<typename T = FNDIArrayImplHelper<TArrayType>>
	typename TEnableIf<T::bSupportsGPU>::Type PushToRenderThread_Internal() const
	{
		//-TODO: Only create RT resource if we are servicing a GPU system
		ENQUEUE_RENDER_COMMAND(UpdateArray)
		(
			[RT_Proxy=static_cast<FNiagaraDataInterfaceProxyArrayImpl*>(Proxy.Get()), RT_Array=TArray<TArrayType>(Data)](FRHICommandListImmediate& RHICmdList)
			{
				RT_Proxy->Buffer.Release();
				RT_Proxy->NumElements = RT_Array.Num();
				if (RT_Proxy->NumElements > 0)
				{
					const int32 BufferStride = T::GPUGetTypeStride();
					const int32 BufferSize = RT_Array.GetTypeSize() * RT_Array.Num();
					const int32 BufferNumElements = BufferSize / BufferStride;
					check((sizeof(TArrayType) % BufferStride) == 0);
					check(BufferSize == BufferNumElements * BufferStride);

					RT_Proxy->Buffer.Initialize(BufferStride, BufferNumElements, FNDIArrayImplHelper<TArrayType>::PixelFormat, BUF_Static, TEXT("NiagaraArrayFloat"));
					void* GPUMemory = RHICmdList.LockVertexBuffer(RT_Proxy->Buffer.Buffer, 0, BufferSize, RLM_WriteOnly);
					FMemory::Memcpy(GPUMemory, RT_Array.GetData(), BufferSize);
					RHICmdList.UnlockVertexBuffer(RT_Proxy->Buffer.Buffer);
				}
			}
		);
	}

	template<typename T = FNDIArrayImplHelper<TArrayType>>
	typename TEnableIf<!T::bSupportsGPU>::Type PushToRenderThread_Internal() const
	{
	}

	virtual void PushToRenderThread() const override
	{
		PushToRenderThread_Internal();
	}

	FNiagaraDataInterfaceParametersCS* CreateComputeParameters() const
	{
		if (FNDIArrayImplHelper<TArrayType>::bSupportsGPU)
		{
			return new FNiagaraDataInterfaceParametersCS_ArrayImpl();
		}
		return nullptr;
	}

	const FTypeLayoutDesc* GetComputeParametersTypeDesc() const
	{
		if (FNDIArrayImplHelper<TArrayType>::bSupportsGPU)
		{
			return &StaticGetTypeLayoutDesc<FNiagaraDataInterfaceParametersCS_ArrayImpl>();
		}
		return nullptr;
	}

	void BindParameters(FNiagaraDataInterfaceParametersCS* Base, const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		check(FNDIArrayImplHelper<TArrayType>::bSupportsGPU);
		static_cast<FNiagaraDataInterfaceParametersCS_ArrayImpl*>(Base)->Bind(ParameterInfo, ParameterMap);
	}

	void SetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(FNDIArrayImplHelper<TArrayType>::bSupportsGPU);

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyArrayImpl* DataInterface = static_cast<FNiagaraDataInterfaceProxyArrayImpl*>(Context.DataInterface);
		if (DataInterface->Buffer.NumBytes > 0)
		{
			static_cast<const FNiagaraDataInterfaceParametersCS_ArrayImpl*>(Base)->SetBuffer(RHICmdList, ComputeShaderRHI, DataInterface->Buffer.SRV, DataInterface->NumElements);
		}
		else
		{
			static_cast<const FNiagaraDataInterfaceParametersCS_ArrayImpl*>(Base)->SetBuffer(RHICmdList, ComputeShaderRHI, FNDIArrayImplHelper<TArrayType>::GetDummyBuffer(), 0);
		}
	}

	void UnsetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(FNDIArrayImplHelper<TArrayType>::bSupportsGPU);
		// Nothing to Unset
		//static_cast<const FNiagaraDataInterfaceParametersCS_ArrayImpl*>(Base)->Unset(RHICmdList, Context);
	}

	void GetNum(FVectorVMContext& Context)
	{
		FNDIOutputParam<int32> OutValue(Context);

		const int32 Num = Data.Num();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutValue.SetAndAdvance(Num);
		}
	}

	void GetValue(FVectorVMContext& Context)
	{
		VectorVM::FExternalFuncInputHandler<int32> IndexParam(Context);
		FNDIOutputParam<TArrayType> OutValue(Context);

		const int32 Num = Data.Num() - 1;
		if (Num >= 0)
		{
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 Index = FMath::Clamp(IndexParam.GetAndAdvance(), 0, Num);
				OutValue.SetAndAdvance(Data[Index]);
			}
		}
		else
		{
			const TArrayType DefaultValue = TArrayType();
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				OutValue.SetAndAdvance(DefaultValue);
			}
		}
	}
};
