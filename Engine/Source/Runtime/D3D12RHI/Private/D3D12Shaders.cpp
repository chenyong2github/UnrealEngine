// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12Shaders.cpp: D3D shader RHI implementation.
=============================================================================*/

#include "D3D12RHIPrivate.h"

template <typename TShaderType>
static inline bool ReadShaderOptionalData(FShaderCodeReader& InShaderCode, TShaderType& OutShader, bool& bOutFoundCodeFeatures, FShaderCodeFeatures& OutCodeFeatures)
{
	const FShaderCodePackedResourceCounts* PackedResourceCounts = InShaderCode.FindOptionalData<FShaderCodePackedResourceCounts>();
	if (!PackedResourceCounts)
	{
		return false;
	}
	OutShader.ResourceCounts = *PackedResourceCounts;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	OutShader.ShaderName = InShaderCode.FindOptionalData(FShaderCodeName::Key);

	int32 UniformBufferTableSize = 0;
	const auto* UniformBufferData = InShaderCode.FindOptionalDataAndSize('u', UniformBufferTableSize);
#if 0
	//#todo-rco
	if (UniformBufferData && UniformBufferTableSize > 0)
	{
		FBufferReader UBReader((void*)UniformBufferData, UniformBufferTableSize, false);
		TArray<FString> Names;
		UBReader << Names;
		check(OutShader.UniformBuffers.Num() == 0);
		for (int32 Index = 0; Index < Names.Num(); ++Index)
		{
			OutShader.UniformBuffers.Add(FName(*Names[Index]));
		}
	}
#endif
#endif
	int32 VendorExtensionTableSize = 0;
	auto* VendorExtensionData = InShaderCode.FindOptionalDataAndSize(FShaderCodeVendorExtension::Key, VendorExtensionTableSize);
	if (VendorExtensionData && VendorExtensionTableSize > 0)
	{
		FBufferReader Ar((void*)VendorExtensionData, VendorExtensionTableSize, false);
		Ar << OutShader.VendorExtensions;
	}

	const FShaderCodeFeatures* CodeFeatures = InShaderCode.FindOptionalData<FShaderCodeFeatures>();
	if (CodeFeatures)
	{
		bOutFoundCodeFeatures = true;
		OutCodeFeatures = *CodeFeatures;

		OutShader.bUsesDiagnosticBuffer = CodeFeatures->bUsesDiagnosticBuffer;
	}
	else
	{
		bOutFoundCodeFeatures = false;
	}

	return true;
}

template <typename TShaderType>
static inline bool ReadShaderOptionalData(FShaderCodeReader& InShaderCode, TShaderType& OutShader)
{
	bool bHasCodeFeatures = false;
	FShaderCodeFeatures Features;
	return ReadShaderOptionalData(InShaderCode, OutShader, bHasCodeFeatures, Features);
}

template <typename TShaderType>
static inline void InitUniformBufferStaticSlots(TShaderType* Shader)
{
	const FBaseShaderResourceTable& SRT = Shader->ShaderResourceTable;

	Shader->StaticSlots.Reserve(SRT.ResourceTableLayoutHashes.Num());

	for (uint32 LayoutHash : SRT.ResourceTableLayoutHashes)
	{
		if (const FShaderParametersMetadata* Metadata = FindUniformBufferStructByLayoutHash(LayoutHash))
		{
			Shader->StaticSlots.Add(Metadata->GetLayout().StaticSlot);
		}
		else
		{
			Shader->StaticSlots.Add(MAX_UNIFORM_BUFFER_STATIC_SLOTS);
		}
	}
}

FVertexShaderRHIRef FD3D12DynamicRHI::RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FShaderCodeReader ShaderCode(Code);
	FD3D12VertexShader* Shader = new FD3D12VertexShader;

	FMemoryReaderView Ar(Code, true);
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const SIZE_T CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;
	bool bFoundCodeFeatures;
	FShaderCodeFeatures CodeFeatures;
	if (!ReadShaderOptionalData(ShaderCode, *Shader, bFoundCodeFeatures, CodeFeatures))
	{
		return nullptr;
	}
	if (bFoundCodeFeatures && CodeFeatures.bUsesWaveOps && !GRHISupportsWaveOperations)
	{
		return nullptr;
	}

	Shader->Code = Code;
	Shader->Offset = Offset;
	InitUniformBufferStaticSlots(Shader);

	D3D12_SHADER_BYTECODE ShaderBytecode;
	ShaderBytecode.pShaderBytecode = Shader->Code.GetData() + Offset;
	ShaderBytecode.BytecodeLength = CodeSize;
	Shader->ShaderBytecode.SetShaderBytecode(ShaderBytecode);

	return Shader;
}

FMeshShaderRHIRef FD3D12DynamicRHI::RHICreateMeshShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FShaderCodeReader ShaderCode(Code);
	FD3D12MeshShader* Shader = new FD3D12MeshShader;

	FMemoryReaderView Ar(Code, true);
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const SIZE_T CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;
	bool bFoundCodeFeatures;
	FShaderCodeFeatures CodeFeatures;
	if (!ReadShaderOptionalData(ShaderCode, *Shader, bFoundCodeFeatures, CodeFeatures))
	{
		return nullptr;
	}
	if (bFoundCodeFeatures && CodeFeatures.bUsesWaveOps && !GRHISupportsWaveOperations)
	{
		return nullptr;
	}

	Shader->Code = Code;
	InitUniformBufferStaticSlots(Shader);

	D3D12_SHADER_BYTECODE ShaderBytecode;
	ShaderBytecode.pShaderBytecode = Shader->Code.GetData() + Offset;
	ShaderBytecode.BytecodeLength = CodeSize;
	Shader->ShaderBytecode.SetShaderBytecode(ShaderBytecode);

	return Shader;
}

FAmplificationShaderRHIRef FD3D12DynamicRHI::RHICreateAmplificationShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FShaderCodeReader ShaderCode(Code);
	FD3D12AmplificationShader* Shader = new FD3D12AmplificationShader;

	FMemoryReaderView Ar(Code, true);
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const SIZE_T CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;
	bool bFoundCodeFeatures;
	FShaderCodeFeatures CodeFeatures;
	if (!ReadShaderOptionalData(ShaderCode, *Shader, bFoundCodeFeatures, CodeFeatures))
	{
		return nullptr;
	}
	if (bFoundCodeFeatures && CodeFeatures.bUsesWaveOps && !GRHISupportsWaveOperations)
	{
		return nullptr;
	}

	Shader->Code = Code;
	InitUniformBufferStaticSlots(Shader);

	D3D12_SHADER_BYTECODE ShaderBytecode;
	ShaderBytecode.pShaderBytecode = Shader->Code.GetData() + Offset;
	ShaderBytecode.BytecodeLength = CodeSize;
	Shader->ShaderBytecode.SetShaderBytecode(ShaderBytecode);

	return Shader;
}

FPixelShaderRHIRef FD3D12DynamicRHI::RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FShaderCodeReader ShaderCode(Code);

	FD3D12PixelShader* Shader = new FD3D12PixelShader;

	FMemoryReaderView Ar(Code, true);
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const SIZE_T CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;
	bool bFoundCodeFeatures;
	FShaderCodeFeatures CodeFeatures;
	if (!ReadShaderOptionalData(ShaderCode, *Shader, bFoundCodeFeatures, CodeFeatures))
	{
		return nullptr;
	}
	if (bFoundCodeFeatures && CodeFeatures.bUsesWaveOps && !GRHISupportsWaveOperations)
	{
		return nullptr;
	}

	Shader->Code = Code;
	InitUniformBufferStaticSlots(Shader);

	D3D12_SHADER_BYTECODE ShaderBytecode;
	ShaderBytecode.pShaderBytecode = Shader->Code.GetData() + Offset;
	ShaderBytecode.BytecodeLength = CodeSize;
	Shader->ShaderBytecode.SetShaderBytecode(ShaderBytecode);

	return Shader;
}

FGeometryShaderRHIRef FD3D12DynamicRHI::RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FShaderCodeReader ShaderCode(Code);

	FD3D12GeometryShader* Shader = new FD3D12GeometryShader;

	FMemoryReaderView Ar(Code, true);
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const SIZE_T CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;
	bool bFoundCodeFeatures;
	FShaderCodeFeatures CodeFeatures;
	if (!ReadShaderOptionalData(ShaderCode, *Shader, bFoundCodeFeatures, CodeFeatures))
	{
		return nullptr;
	}
	if (bFoundCodeFeatures && CodeFeatures.bUsesWaveOps && !GRHISupportsWaveOperations)
	{
		return nullptr;
	}

	Shader->Code = Code;
	InitUniformBufferStaticSlots(Shader);

	D3D12_SHADER_BYTECODE ShaderBytecode;
	ShaderBytecode.pShaderBytecode = Shader->Code.GetData() + Offset;
	ShaderBytecode.BytecodeLength = CodeSize;
	Shader->ShaderBytecode.SetShaderBytecode(ShaderBytecode);

	return Shader;
}

FComputeShaderRHIRef FD3D12DynamicRHI::RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FShaderCodeReader ShaderCode(Code);

	FD3D12ComputeShader* Shader = new FD3D12ComputeShader;

	FMemoryReaderView Ar(Code, true);
	Ar << Shader->ShaderResourceTable;
	int32 Offset = Ar.Tell();
	const uint8* CodePtr = Code.GetData() + Offset;
	const SIZE_T CodeSize = ShaderCode.GetActualShaderCodeSize() - Offset;
	bool bFoundCodeFeatures;
	FShaderCodeFeatures CodeFeatures;
	if (!ReadShaderOptionalData(ShaderCode, *Shader, bFoundCodeFeatures, CodeFeatures))
	{
		return nullptr;
	}
	if (bFoundCodeFeatures && CodeFeatures.bUsesWaveOps && !GRHISupportsWaveOperations)
	{
		return nullptr;
	}

	Shader->Code = Code;
	InitUniformBufferStaticSlots(Shader);

	D3D12_SHADER_BYTECODE ShaderBytecode;
	ShaderBytecode.pShaderBytecode = Shader->Code.GetData() + Offset;
	ShaderBytecode.BytecodeLength = CodeSize;
	Shader->ShaderBytecode.SetShaderBytecode(ShaderBytecode);

	FD3D12Adapter& Adapter = GetAdapter();

#if USE_STATIC_ROOT_SIGNATURE
	Shader->pRootSignature = Adapter.GetStaticComputeRootSignature();
#else
	const D3D12_RESOURCE_BINDING_TIER Tier = Adapter.GetResourceBindingTier();
	FD3D12QuantizedBoundShaderState QBSS;
	QuantizeBoundShaderState(Tier, Shader, QBSS);
	Shader->pRootSignature = Adapter.GetRootSignature(QBSS);
#endif

	return Shader;
}

#if D3D12_RHI_RAYTRACING

FRayTracingShaderRHIRef FD3D12DynamicRHI::RHICreateRayTracingShader(TArrayView<const uint8> Code, const FSHAHash& Hash, EShaderFrequency ShaderFrequency)
{
	checkf(GRHISupportsRayTracing, TEXT("Tried to create RayTracing shader but RHI doesn't support it!"));

	FShaderCodeReader ShaderCode(Code);
	FD3D12RayTracingShader* Shader = new FD3D12RayTracingShader(ShaderFrequency);

	FMemoryReaderView Ar(Code, true);
	Ar << Shader->ShaderResourceTable;
	Ar << Shader->EntryPoint;
	Ar << Shader->AnyHitEntryPoint;
	Ar << Shader->IntersectionEntryPoint;

	int32 Offset = Ar.Tell();

	bool bFoundCodeFeatures;
	FShaderCodeFeatures CodeFeatures;
	if (!ReadShaderOptionalData(ShaderCode, *Shader, bFoundCodeFeatures, CodeFeatures))
	{
		return nullptr;
	}

	int32 PrecompiledKey = 0;
	Ar << PrecompiledKey;
	if (PrecompiledKey == RayTracingPrecompiledPSOKey)
	{
		Offset += sizeof(PrecompiledKey); // Skip the precompiled PSO marker if it's present
		Shader->bPrecompiledPSO = true;
	}

	// Copy the native shader data only, skipping any of our own headers.
	TArrayView<const uint8> NativeShaderCode = MakeArrayView(Code.GetData() + Offset, ShaderCode.GetActualShaderCodeSize() - Offset);
	Shader->Code = TArray<uint8>(NativeShaderCode);

	D3D12_SHADER_BYTECODE ShaderBytecode;
	ShaderBytecode.pShaderBytecode = Shader->Code.GetData();
	ShaderBytecode.BytecodeLength = Shader->Code.Num();

	Shader->ShaderBytecode.SetShaderBytecode(ShaderBytecode);

	FD3D12Adapter& Adapter = GetAdapter();

#if USE_STATIC_ROOT_SIGNATURE
	switch (ShaderFrequency)
	{
	case SF_RayGen:
		Shader->pRootSignature = Adapter.GetStaticRayTracingGlobalRootSignature();
		break;
	case SF_RayHitGroup:
	case SF_RayCallable:
	case SF_RayMiss:
		Shader->pRootSignature = Adapter.GetStaticRayTracingLocalRootSignature();
		break;
	default:
		checkNoEntry(); // Unexpected shader target frequency
	}
#else // USE_STATIC_ROOT_SIGNATURE
	const D3D12_RESOURCE_BINDING_TIER Tier = Adapter.GetResourceBindingTier();
	FD3D12QuantizedBoundShaderState QBSS;
	QuantizeBoundShaderState(ShaderFrequency, Tier, Shader, QBSS);
	Shader->pRootSignature = Adapter.GetRootSignature(QBSS);
#endif // USE_STATIC_ROOT_SIGNATURE

	return Shader;
}

#endif // D3D12_RHI_RAYTRACING

void FD3D12CommandContext::RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data)
{
	// Structures are chosen to be directly mappable
	StateCache.SetViewports(Count, reinterpret_cast<const D3D12_VIEWPORT*>(Data));
}

FD3D12BoundShaderState::FD3D12BoundShaderState(
	FRHIVertexDeclaration* InVertexDeclarationRHI,
	FRHIVertexShader* InVertexShaderRHI,
	FRHIPixelShader* InPixelShaderRHI,
	FRHIGeometryShader* InGeometryShaderRHI,
	FD3D12Adapter* InAdapter
	) :
	CacheLink(InVertexDeclarationRHI, InVertexShaderRHI, InPixelShaderRHI, InGeometryShaderRHI, this)
{
	INC_DWORD_STAT(STAT_D3D12NumBoundShaderState);

#if USE_STATIC_ROOT_SIGNATURE
	pRootSignature = InAdapter->GetStaticGraphicsRootSignature();
#else
	const D3D12_RESOURCE_BINDING_TIER Tier = InAdapter->GetResourceBindingTier();
	FD3D12QuantizedBoundShaderState QuantizedBoundShaderState;
	QuantizeBoundShaderState(Tier, this, QuantizedBoundShaderState);
	pRootSignature = InAdapter->GetRootSignature(QuantizedBoundShaderState);
#endif

#if D3D12_SUPPORTS_PARALLEL_RHI_EXECUTE
	CacheLink.AddToCache();
#endif
}

#if PLATFORM_SUPPORTS_MESH_SHADERS
FD3D12BoundShaderState::FD3D12BoundShaderState(
	FRHIMeshShader* InMeshShaderRHI,
	FRHIAmplificationShader* InAmplificationShaderRHI,
	FRHIPixelShader* InPixelShaderRHI,
	FD3D12Adapter* InAdapter
) :
	CacheLink(InMeshShaderRHI, InAmplificationShaderRHI, InPixelShaderRHI, this)
{
	INC_DWORD_STAT(STAT_D3D12NumBoundShaderState);

#if USE_STATIC_ROOT_SIGNATURE
	pRootSignature = InAdapter->GetStaticMeshRootSignature();
#else
	const D3D12_RESOURCE_BINDING_TIER Tier = InAdapter->GetResourceBindingTier();
	FD3D12QuantizedBoundShaderState QuantizedBoundShaderState;
	QuantizeBoundShaderState(Tier, this, QuantizedBoundShaderState);
	pRootSignature = InAdapter->GetRootSignature(QuantizedBoundShaderState);
#endif

#if D3D12_SUPPORTS_PARALLEL_RHI_EXECUTE
	CacheLink.AddToCache();
#endif
}
#endif // PLATFORM_SUPPORTS_MESH_SHADERS

FD3D12BoundShaderState::~FD3D12BoundShaderState()
{
	DEC_DWORD_STAT(STAT_D3D12NumBoundShaderState);
#if D3D12_SUPPORTS_PARALLEL_RHI_EXECUTE
	CacheLink.RemoveFromCache();
#endif
}

FBoundShaderStateRHIRef FD3D12DynamicRHI::DX12CreateBoundShaderState(const FBoundShaderStateInput& BoundShaderStateInput)
{
	//SCOPE_CYCLE_COUNTER(STAT_D3D12CreateBoundShaderStateTime);

	checkf(GIsRHIInitialized && GetRHIDevice(0)->GetCommandListManager().IsReady(), (TEXT("Bound shader state RHI resource was created without initializing Direct3D first")));

#if D3D12_SUPPORTS_PARALLEL_RHI_EXECUTE
	// Check for an existing bound shader state which matches the parameters
	FBoundShaderStateRHIRef CachedBoundShaderState = GetCachedBoundShaderState_Threadsafe(
		BoundShaderStateInput.VertexDeclarationRHI,
		BoundShaderStateInput.VertexShaderRHI,
		BoundShaderStateInput.PixelShaderRHI,
		BoundShaderStateInput.GeometryShaderRHI,
		BoundShaderStateInput.MeshShaderRHI,
		BoundShaderStateInput.AmplificationShaderRHI
	);
	if (CachedBoundShaderState.GetReference())
	{
		// If we've already created a bound shader state with these parameters, reuse it.
		return CachedBoundShaderState;
	}
#else
	check(IsInRenderingThread() || IsInRHIThread());
	// Check for an existing bound shader state which matches the parameters
	FCachedBoundShaderStateLink* CachedBoundShaderStateLink = GetCachedBoundShaderState(
		BoundShaderStateInput.VertexDeclarationRHI,
		BoundShaderStateInput.VertexShaderRHI,
		BoundShaderStateInput.PixelShaderRHI,
		BoundShaderStateInput.GeometryShaderRHI,
		BoundShaderStateInput.MeshShaderRHI,
		BoundShaderStateInput.AmplificationShaderRHI
	);
	if (CachedBoundShaderStateLink)
	{
		// If we've already created a bound shader state with these parameters, reuse it.
		return CachedBoundShaderStateLink->BoundShaderState;
	}
#endif
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_D3D12NewBoundShaderStateTime);

#if PLATFORM_SUPPORTS_MESH_SHADERS
		if (BoundShaderStateInput.MeshShaderRHI)
		{
			return new FD3D12BoundShaderState(
				BoundShaderStateInput.MeshShaderRHI,
				BoundShaderStateInput.AmplificationShaderRHI,
				BoundShaderStateInput.PixelShaderRHI,
				&GetAdapter());
		}
		else
#endif // PLATFORM_SUPPORTS_MESH_SHADERS
		{
			return new FD3D12BoundShaderState(
				BoundShaderStateInput.VertexDeclarationRHI,
				BoundShaderStateInput.VertexShaderRHI,
				BoundShaderStateInput.PixelShaderRHI,
				BoundShaderStateInput.GeometryShaderRHI,
				&GetAdapter());
		}
	}
}

/**
* Creates a bound shader state instance which encapsulates a decl, vertex shader, and pixel shader
* @param VertexDeclaration - existing vertex decl
* @param StreamStrides - optional stream strides
* @param VertexShader - existing vertex shader
* @param PixelShader - existing pixel shader
* @param GeometryShader - existing geometry shader
*/
FBoundShaderStateRHIRef FD3D12DynamicRHI::RHICreateBoundShaderState(
	FRHIVertexDeclaration* VertexDeclarationRHI,
	FRHIVertexShader* VertexShaderRHI,
	FRHIPixelShader* PixelShaderRHI,
	FRHIGeometryShader* GeometryShaderRHI
	)
{
	FBoundShaderStateInput Inputs(VertexDeclarationRHI, VertexShaderRHI, PixelShaderRHI
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		, GeometryShaderRHI
#endif
	);
	return DX12CreateBoundShaderState(Inputs);
}
