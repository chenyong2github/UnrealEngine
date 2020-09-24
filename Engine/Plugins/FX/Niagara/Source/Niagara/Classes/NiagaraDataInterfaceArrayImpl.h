// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterfaceArray.h"
#include "NiagaraStats.h"

template<typename TArrayType>
struct FNDIArrayImplHelperBase
{
	typedef TArrayType TVMArrayType;

	static constexpr bool bSupportsCPU = true;
	static constexpr bool bSupportsGPU = true;

	//static constexpr TCHAR const* HLSLValueTypeName = TEXT("float4");
	//static constexpr TCHAR const* HLSLBufferTypeName = TEXT("float4");
	//static constexpr EPixelFormat PixelFormat = PF_R32_FLOAT;
	//static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetIntDef(); }
	//static const TArrayType GetDefaultValue();

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

	~FNiagaraDataInterfaceProxyArrayImpl()
	{
		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, Buffer.NumBytes);
		Buffer.Release();
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }
};

struct FNiagaraDataInterfaceArrayImplHelper
{
	static const FName Function_LengthName;
	static const FName Function_IsValidIndexName;
	static const FName Function_LastIndexName;
	static const FName Function_GetName;

	static const FName Function_ClearName;
	static const FName Function_ResizeName;
	static const FName Function_SetArrayElemName;
	static const FName Function_AddName;
	static const FName Function_RemoveLastElemName;

	static FString GetBufferName(const FString& InterfaceName);
	static FString GetBufferSizeName(const FString& InterfaceName);
#if WITH_EDITORONLY_DATA
	static bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature);
#endif
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
	static constexpr int32 kSafeMaxElements = TNumericLimits<int32>::Max();

	UNiagaraDataInterfaceArray* Owner;
	TArray<TArrayType>& Data;

	FNiagaraDataInterfaceArrayImpl(UNiagaraDataInterfaceArray* InOwner, TArray<TArrayType>& InData)
		: Owner(InOwner)
		, Data(InData)
	{
	}

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) const override
	{
		OutFunctions.Reserve(OutFunctions.Num() + 3);

		// Immutable functions
		{
			FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_LengthName;
			#if WITH_EDITORONLY_DATA
				Sig.Description = NSLOCTEXT("Niagara", "Array_LengthDesc", "Gets the number of elements in the array.");
			#endif
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
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_IsValidIndexName;
			#if WITH_EDITORONLY_DATA
				Sig.Description = NSLOCTEXT("Niagara", "Array_IsValidIndexDesc", "Tests to see if the index is valid and exists in the array.");
			#endif
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			Sig.bExperimental = true;
			Sig.bSupportsCPU = FNDIArrayImplHelper<TArrayType>::bSupportsCPU;
			Sig.bSupportsGPU = FNDIArrayImplHelper<TArrayType>::bSupportsGPU;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TObjectType::StaticClass()), TEXT("Array interface")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		}

		{
			FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_LastIndexName;
#if WITH_EDITORONLY_DATA
			Sig.Description = NSLOCTEXT("Niagara", "Array_LastIndexDesc", "Returns the last valid index in the array, will be -1 if no elements.");
#endif
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			Sig.bExperimental = true;
			Sig.bSupportsCPU = FNDIArrayImplHelper<TArrayType>::bSupportsCPU;
			Sig.bSupportsGPU = FNDIArrayImplHelper<TArrayType>::bSupportsGPU;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TObjectType::StaticClass()), TEXT("Array interface")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		}

		{
			FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_GetName;
			#if WITH_EDITORONLY_DATA
				Sig.Description = NSLOCTEXT("Niagara", "Array_GetDesc", "Gets the value from the array at the given zero based index.");
			#endif
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			Sig.bExperimental = true;
			Sig.bSupportsCPU = FNDIArrayImplHelper<TArrayType>::bSupportsCPU;
			Sig.bSupportsGPU = FNDIArrayImplHelper<TArrayType>::bSupportsGPU;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TObjectType::StaticClass()), TEXT("Array interface")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
			Sig.Outputs.Add(FNiagaraVariable(FNDIArrayImplHelper<TArrayType>::GetTypeDefinition(), TEXT("Value")));
		}

		// Mutable functions
		{
			FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_ClearName;
			#if WITH_EDITORONLY_DATA
				Sig.Description = NSLOCTEXT("Niagara", "Array_ClearDesc", "Clears the array, removing all elements");
			#endif
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			Sig.bExperimental = true;
			Sig.bSupportsCPU = FNDIArrayImplHelper<TArrayType>::bSupportsCPU;
			Sig.bSupportsGPU = false;
			Sig.bRequiresExecPin = true;
			Sig.ModuleUsageBitmask = ENiagaraScriptUsageMask::System | ENiagaraScriptUsageMask::Emitter;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TObjectType::StaticClass()), TEXT("Array interface")));
		}

		{
			FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_ResizeName;
			#if WITH_EDITORONLY_DATA
				Sig.Description = NSLOCTEXT("Niagara", "Array_ResizeDesc", "Resizes the array to the specified size, initializing new elements with the default value.");
			#endif
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			Sig.bExperimental = true;
			Sig.bSupportsCPU = FNDIArrayImplHelper<TArrayType>::bSupportsCPU;
			Sig.bSupportsGPU = false;
			Sig.bRequiresExecPin = true;
			Sig.ModuleUsageBitmask = ENiagaraScriptUsageMask::System | ENiagaraScriptUsageMask::Emitter;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TObjectType::StaticClass()), TEXT("Array interface")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num")));
		}

		{
			FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_SetArrayElemName;
			#if WITH_EDITORONLY_DATA
				Sig.Description = NSLOCTEXT("Niagara", "Array_SetArrayElemDesc", "Sets the value at the given zero based index (i.e the first element is 0).");
			#endif
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			Sig.bExperimental = true;
			Sig.bSupportsCPU = FNDIArrayImplHelper<TArrayType>::bSupportsCPU;
			Sig.bSupportsGPU = false;
			Sig.bRequiresExecPin = true;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TObjectType::StaticClass()), TEXT("Array interface")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
			Sig.Inputs.Add(FNiagaraVariable(FNDIArrayImplHelper<TArrayType>::GetTypeDefinition(), TEXT("Value")));
		}

		{
			FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_AddName;
			#if WITH_EDITORONLY_DATA
				Sig.Description = NSLOCTEXT("Niagara", "Array_AddDesc", "Optionally add a value onto the end of the array.");
			#endif
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			Sig.bExperimental = true;
			Sig.bSupportsCPU = FNDIArrayImplHelper<TArrayType>::bSupportsCPU;
			Sig.bSupportsGPU = false;
			Sig.bRequiresExecPin = true;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TObjectType::StaticClass()), TEXT("Array interface")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipAdd")));
			Sig.Inputs.Add(FNiagaraVariable(FNDIArrayImplHelper<TArrayType>::GetTypeDefinition(), TEXT("Value")));
		}

		{
			FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
			Sig.Name = FNiagaraDataInterfaceArrayImplHelper::Function_RemoveLastElemName;
#if WITH_EDITORONLY_DATA
			Sig.Description = NSLOCTEXT("Niagara", "Array_RemoveLastElemDesc", "Optionally remove the last element from the array.  Returns the default value if no elements are in the array or you skip the remove.");
#endif
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			Sig.bExperimental = true;
			Sig.bSupportsCPU = FNDIArrayImplHelper<TArrayType>::bSupportsCPU;
			Sig.bSupportsGPU = false;
			Sig.bRequiresExecPin = true;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TObjectType::StaticClass()), TEXT("Array interface")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipRemove")));
			Sig.Outputs.Add(FNiagaraVariable(FNDIArrayImplHelper<TArrayType>::GetTypeDefinition(), TEXT("Value")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid")));
		}
	}

	template<typename T = FNDIArrayImplHelper<TArrayType>>
	typename TEnableIf<T::bSupportsCPU>::Type GetVMExternalFunction_Internal(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		// Immutable functions
		if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_LengthName)
		{
			check(BindingInfo.GetNumInputs() == 0 && BindingInfo.GetNumOutputs() == 1);
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetLength(Context); });
		}
		else if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_IsValidIndexName)
		{
			check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->IsValidIndex(Context); });
		}
		else if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_LastIndexName)
		{
			check(BindingInfo.GetNumInputs() == 0 && BindingInfo.GetNumOutputs() == 1);
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetLastIndex(Context); });
		}
		else if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_GetName)
		{
			// Note: Outputs is variable based upon type
			//check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetValue(Context); });
		}
		// Mutable functions
		else if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_ClearName)
		{
			check(BindingInfo.GetNumInputs() == 0 && BindingInfo.GetNumOutputs() == 0);
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->Clear(Context); });
		}
		else if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_ResizeName)
		{
			check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 0);
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->Resize(Context); });
		}
		else if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_SetArrayElemName)
		{
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->SetValue(Context); });
		}
		else if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_AddName)
		{
			// Note: Inputs is variable based upon type
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->PushValue(Context); });
		}
		else if (BindingInfo.Name == FNiagaraDataInterfaceArrayImplHelper::Function_RemoveLastElemName)
		{
			// Note: Outputs is variable based upon type
			OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->PopValue(Context); });
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
		OutHLSL.Appendf(TEXT("int2 %s;\n"), *FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(ParamInfo.DataInterfaceHLSLSymbol));
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
		// Immutable functions
		if (FunctionInfo.DefinitionName == FNiagaraDataInterfaceArrayImplHelper::Function_LengthName)
		{
			OutHLSL.Appendf(TEXT("void %s(out int OutValue) { OutValue = %s[0]; }\n"), *FunctionInfo.InstanceName, *FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(ParamInfo.DataInterfaceHLSLSymbol));
			return true;
		}
		else if (FunctionInfo.DefinitionName == FNiagaraDataInterfaceArrayImplHelper::Function_IsValidIndexName)
		{
			OutHLSL.Appendf(TEXT("void %s(in int Index, out bool bValid) { bValid = Index >=0 && Index < %s[0]; }\n"), *FunctionInfo.InstanceName, *FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(ParamInfo.DataInterfaceHLSLSymbol));
			return true;
		}
		else if (FunctionInfo.DefinitionName == FNiagaraDataInterfaceArrayImplHelper::Function_LastIndexName)
		{
			OutHLSL.Appendf(TEXT("void %s(out int OutValue) { OutValue = %s[0] - 1; }\n"), *FunctionInfo.InstanceName, *FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(ParamInfo.DataInterfaceHLSLSymbol));
			return true;
		}
		else if (FunctionInfo.DefinitionName == FNiagaraDataInterfaceArrayImplHelper::Function_GetName)
		{
			OutHLSL.Appendf(TEXT("void %s(int Index, out %s OutValue) { int ClampedIndex = clamp(Index, 0, %s[1]); "), *FunctionInfo.InstanceName, FNDIArrayImplHelper<TArrayType>::HLSLValueTypeName, *FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(ParamInfo.DataInterfaceHLSLSymbol));
			T::GPUGetFetchHLSL(OutHLSL, *FNiagaraDataInterfaceArrayImplHelper::GetBufferName(ParamInfo.DataInterfaceHLSLSymbol));
			OutHLSL.Append(TEXT(" }\n"));
			return true;
		}
		// Mutable functions
		//-TODO: Supoprt mutable functions in some limited way
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

#if WITH_EDITORONLY_DATA
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) const override
	{
		return FNiagaraDataInterfaceArrayImplHelper::UpgradeFunctionCall(FunctionSignature);
	}
#endif

	virtual bool CopyToInternal(INiagaraDataInterfaceArrayImpl* InDestination) const override
	{
		auto Destination = static_cast<FNiagaraDataInterfaceArrayImpl<TArrayType, TObjectType>*>(InDestination);
		{
			FRWScopeLock WriteLock(Owner->ArrayRWGuard, SLT_Write);
			Destination->Data = Data;
		}
		return true;
	}

	virtual bool Equals(const INiagaraDataInterfaceArrayImpl* InOther) const override
	{
		auto Other = static_cast<const FNiagaraDataInterfaceArrayImpl<TArrayType, TObjectType>*>(InOther);
		FRWScopeLock ReadLock(Owner->ArrayRWGuard, SLT_ReadOnly);
		return Other->Data == Data;
	}

	template<typename T = FNDIArrayImplHelper<TArrayType>>
	typename TEnableIf<T::bSupportsGPU>::Type PushToRenderThread_Internal() const
	{
		FNiagaraDataInterfaceProxy* Proxy = Owner->GetProxy();

		//-TODO: Only create RT resource if we are servicing a GPU system
		ENQUEUE_RENDER_COMMAND(UpdateArray)
		(
			[RT_Proxy=static_cast<FNiagaraDataInterfaceProxyArrayImpl*>(Proxy), RT_Array=TArray<TArrayType>(Data)](FRHICommandListImmediate& RHICmdList)
			{
				DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RT_Proxy->Buffer.NumBytes);

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
				else
				{
					const int32 BufferStride = T::GPUGetTypeStride();
					const int32 BufferSize = RT_Array.GetTypeSize();
					const int32 BufferNumElements = BufferSize / BufferStride;
					check((sizeof(TArrayType) % BufferStride) == 0);

					const TArrayType DefaultValue = FNDIArrayImplHelper<TArrayType>::GetDefaultValue();

					RT_Proxy->Buffer.Initialize(BufferStride, BufferNumElements, FNDIArrayImplHelper<TArrayType>::PixelFormat, BUF_Static, TEXT("NiagaraArrayFloat"));
					void* GPUMemory = RHICmdList.LockVertexBuffer(RT_Proxy->Buffer.Buffer, 0, BufferSize, RLM_WriteOnly);
					FMemory::Memcpy(GPUMemory, &DefaultValue, BufferSize);
					RHICmdList.UnlockVertexBuffer(RT_Proxy->Buffer.Buffer);
				}

				INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RT_Proxy->Buffer.NumBytes);
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
		check(DataInterface->Buffer.NumBytes > 0);
		static_cast<const FNiagaraDataInterfaceParametersCS_ArrayImpl*>(Base)->SetBuffer(RHICmdList, ComputeShaderRHI, DataInterface->Buffer.SRV, DataInterface->NumElements);
	}

	void UnsetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(FNDIArrayImplHelper<TArrayType>::bSupportsGPU);
		// Nothing to Unset
		//static_cast<const FNiagaraDataInterfaceParametersCS_ArrayImpl*>(Base)->Unset(RHICmdList, Context);
	}

	void GetLength(FVectorVMContext& Context)
	{
		FNDIOutputParam<int32> OutValue(Context);

		Owner->ArrayRWGuard.ReadLock();
		const int32 Num = Data.Num();
		Owner->ArrayRWGuard.ReadUnlock();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutValue.SetAndAdvance(Num);
		}
	}

	void IsValidIndex(FVectorVMContext& Context)
	{
		FNDIInputParam<int32> IndexParam(Context);
		FNDIOutputParam<FNiagaraBool> OutValue(Context);

		Owner->ArrayRWGuard.ReadLock();
		const int32 Num = Data.Num();
		Owner->ArrayRWGuard.ReadUnlock();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 Index = IndexParam.GetAndAdvance();
			OutValue.SetAndAdvance((Index >= 0) && (Index < Num));
		}
	}

	void GetLastIndex(FVectorVMContext& Context)
	{
		FNDIOutputParam<int32> OutValue(Context);

		Owner->ArrayRWGuard.ReadLock();
		const int32 Num = Data.Num() - 1;
		Owner->ArrayRWGuard.ReadUnlock();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutValue.SetAndAdvance(Num);
		}
	}

	void GetValue(FVectorVMContext& Context)
	{
		FNDIInputParam<int32> IndexParam(Context);
		FNDIOutputParam<typename FNDIArrayImplHelper<TArrayType>::TVMArrayType> OutValue(Context);

		FRWScopeLock ReadLock(Owner->ArrayRWGuard, SLT_ReadOnly);
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
			const TArrayType DefaultValue = FNDIArrayImplHelper<TArrayType>::GetDefaultValue();
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				OutValue.SetAndAdvance(DefaultValue);
			}
		}
	}

	void Clear(FVectorVMContext& Context)
	{
		//-TODO: This dirties the GPU data
		ensureMsgf(Context.NumInstances == 1, TEXT("Setting the number of values in an array with more than one instance, which doesn't make sense"));

		FRWScopeLock WriteLock(Owner->ArrayRWGuard, SLT_Write);
		Data.Reset();
	}

	void Resize(FVectorVMContext& Context)
	{
		//-TODO: This dirties the GPU data
		FNDIInputParam<int32> NewNumParam(Context);

		ensureMsgf(Context.NumInstances == 1, TEXT("Setting the number of values in an array with more than one instance, which doesn't make sense"));

		FRWScopeLock WriteLock(Owner->ArrayRWGuard, SLT_Write);
		const int32 OldNum = Data.Num();
		const int32 NewNum = FMath::Min(NewNumParam.GetAndAdvance(), kSafeMaxElements);
		Data.SetNumUninitialized(NewNum);

		if (NewNum > OldNum)
		{
			const TArrayType DefaultValue = FNDIArrayImplHelper<TArrayType>::GetDefaultValue();
			for (int32 i = OldNum; i < NewNum; ++i)
			{
				Data[i] = DefaultValue;
			}
		}
	}

	void SetValue(FVectorVMContext& Context)
	{
		//-TODO: This dirties the GPU data
		FNDIInputParam<int32> IndexParam(Context);
		FNDIInputParam<typename FNDIArrayImplHelper<TArrayType>::TVMArrayType> InValue(Context);

		FRWScopeLock WriteLock(Owner->ArrayRWGuard, SLT_Write);
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 Index = IndexParam.GetAndAdvance();
			const TArrayType Value = InValue.GetAndAdvance();

			if (Data.IsValidIndex(Index))
			{
				Data[Index] = Value;
			}
		}
	}

	void PushValue(FVectorVMContext& Context)
	{
		//-TODO: This dirties the GPU data
		FNDIInputParam<FNiagaraBool> InSkipExecute(Context);
		FNDIInputParam<typename FNDIArrayImplHelper<TArrayType>::TVMArrayType> InValue(Context);

		const int32 MaxElements = Owner->MaxElements > 0 ? Owner->MaxElements : kSafeMaxElements;

		FRWScopeLock WriteLock(Owner->ArrayRWGuard, SLT_Write);
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const bool bSkipExecute = InSkipExecute.GetAndAdvance();
			const TArrayType Value = InValue.GetAndAdvance();
			if (!bSkipExecute && (Data.Num() < MaxElements))
			{
				Data.Emplace(Value);
			}
		}
	}

	void PopValue(FVectorVMContext& Context)
	{
		//-TODO: This dirties the GPU data
		FNDIInputParam<FNiagaraBool> InSkipExecute(Context);
		FNDIOutputParam<typename FNDIArrayImplHelper<TArrayType>::TVMArrayType> OutValue(Context);
		FNDIOutputParam<FNiagaraBool> OutIsValid(Context);
		const TArrayType DefaultValue = FNDIArrayImplHelper<TArrayType>::GetDefaultValue();

		FRWScopeLock WriteLock(Owner->ArrayRWGuard, SLT_Write);
		for (int32 i=0; i < Context.NumInstances; ++i)
		{
			const bool bSkipExecute = InSkipExecute.GetAndAdvance();
			if (bSkipExecute || (Data.Num() == 0))
			{
				OutValue.SetAndAdvance(DefaultValue);
				OutIsValid.SetAndAdvance(false);
			}
			else
			{
				OutValue.SetAndAdvance(Data.Pop());
				OutIsValid.SetAndAdvance(true);
			}
		}
	}
};
