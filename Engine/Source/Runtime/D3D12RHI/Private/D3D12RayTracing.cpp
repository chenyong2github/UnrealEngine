// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "D3D12RayTracing.h"

#if D3D12_RHI_RAYTRACING

#include "D3D12Resources.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "Containers/BitArray.h"
#include "BuiltInRayTracingShaders.h"
#include "RayTracingBuiltInResources.h"
#include "Hash/CityHash.h"
#include "HAL/CriticalSection.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"

static int32 GRayTracingDebugForceOpaque = 0;
static FAutoConsoleVariableRef CVarRayTracingDebugForceOpaque(
	TEXT("r.RayTracing.DebugForceOpaque"),
	GRayTracingDebugForceOpaque,
	TEXT("Forces all ray tracing geometry instances to be opaque, effectively disabling any-hit shaders. This is useful for debugging and profiling. (default = 0)")
);

static int32 GRayTracingDebugForceBuildMode = 0;
static FAutoConsoleVariableRef CVarRayTracingDebugForceFastTrace(
	TEXT("r.RayTracing.DebugForceBuildMode"),
	GRayTracingDebugForceBuildMode,
	TEXT("Forces specific acceleration structure build mode (not runtime-tweakable).\n")
	TEXT("0: Use build mode requested by high-level code (Default)\n")
	TEXT("1: Force fast build mode\n")
	TEXT("2: Force fast trace mode\n"),
	ECVF_ReadOnly
);

static int32 GRayTracingDebugForceFullBuild = 0;
static FAutoConsoleVariableRef CVarRayTracingDebugForceFullBuild(
	TEXT("r.RayTracing.DebugForceFullBuild"),
	GRayTracingDebugForceFullBuild,
	TEXT("Forces all acceleration structure updates to always perform a full build.\n")
	TEXT("0: Allow update (Default)\n")
	TEXT("1: Force full build\n")
);

static int32 GRayTracingDebugDisableTriangleCull = 0;
static FAutoConsoleVariableRef CVarRayTracingDebugDisableTriangleCull(
	TEXT("r.RayTracing.DebugDisableTriangleCull"),
	GRayTracingDebugDisableTriangleCull,
	TEXT("Forces all ray tracing geometry instances to be double-sided by disabling back-face culling. This is useful for debugging and profiling. (default = 0)")
);

static int32 GRayTracingCacheShaderRecords = 1;
static FAutoConsoleVariableRef CVarRayTracingShaderRecordCache(
	TEXT("r.RayTracing.CacheShaderRecords"),
	GRayTracingCacheShaderRecords,
	TEXT("Automatically cache and re-use SBT hit group records. This significantly improves CPU performance in large scenes with many identical mesh instances. (default = 1)\n")
	TEXT("This mode assumes that contents of uniform buffers does not change during ray tracing resource binding.")
);

// Ray tracing stat counters

DECLARE_STATS_GROUP(TEXT("D3D12RHI: Ray Tracing"), STATGROUP_D3D12RayTracing, STATCAT_Advanced);

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Created pipelines (total)"), STAT_D3D12RayTracingCreatedPipelines, STATGROUP_D3D12RayTracing);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Compiled shaders (total)"), STAT_D3D12RayTracingCompiledShaders, STATGROUP_D3D12RayTracing);

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Allocated bottom level acceleration structures"), STAT_D3D12RayTracingAllocatedBLAS, STATGROUP_D3D12RayTracing);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Allocated top level acceleration structures"), STAT_D3D12RayTracingAllocatedTLAS, STATGROUP_D3D12RayTracing);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Triangles in all BL acceleration structures"), STAT_D3D12RayTracingTrianglesBLAS, STATGROUP_D3D12RayTracing);

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Allocated sampler descriptor heaps"), STAT_D3D12RayTracingSamplerDescriptorHeaps, STATGROUP_D3D12RayTracing);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Allocated sampler descriptors"), STAT_D3D12RayTracingSamplerDescriptors, STATGROUP_D3D12RayTracing);

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Allocated view descriptor heaps"), STAT_D3D12RayTracingViewDescriptorHeaps, STATGROUP_D3D12RayTracing);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Allocated view descriptors"), STAT_D3D12RayTracingViewDescriptors, STATGROUP_D3D12RayTracing);

DECLARE_DWORD_COUNTER_STAT(TEXT("Used sampler descriptors (per frame)"), STAT_D3D12RayTracingUsedSamplerDescriptors, STATGROUP_D3D12RayTracing);
DECLARE_DWORD_COUNTER_STAT(TEXT("Used view descriptors (per frame)"), STAT_D3D12RayTracingUsedViewDescriptors, STATGROUP_D3D12RayTracing);

DECLARE_DWORD_COUNTER_STAT(TEXT("Built BL AS (per frame)"), STAT_D3D12RayTracingBuiltBLAS, STATGROUP_D3D12RayTracing);
DECLARE_DWORD_COUNTER_STAT(TEXT("Updated BL AS (per frame)"), STAT_D3D12RayTracingUpdatedBLAS, STATGROUP_D3D12RayTracing);
DECLARE_DWORD_COUNTER_STAT(TEXT("Built TL AS (per frame)"), STAT_D3D12RayTracingBuiltTLAS, STATGROUP_D3D12RayTracing);
DECLARE_DWORD_COUNTER_STAT(TEXT("Updated TL AS (per frame)"), STAT_D3D12RayTracingUpdatedTLAS, STATGROUP_D3D12RayTracing);

DECLARE_MEMORY_STAT(TEXT("BL AS Memory"), STAT_D3D12RayTracingBLASMemory, STATGROUP_D3D12RayTracing);
DECLARE_MEMORY_STAT(TEXT("TL AS Memory"), STAT_D3D12RayTracingTLASMemory, STATGROUP_D3D12RayTracing);
DECLARE_MEMORY_STAT(TEXT("Total Used Video Memory"), STAT_D3D12RayTracingUsedVideoMemory, STATGROUP_D3D12RayTracing);

DECLARE_CYCLE_STAT(TEXT("RTPSO Compile Shader"), STAT_RTPSO_CompileShader, STATGROUP_D3D12RayTracing);
DECLARE_CYCLE_STAT(TEXT("RTPSO Create Pipeline"), STAT_RTPSO_CreatePipeline, STATGROUP_D3D12RayTracing);

// Built-in local root parameters that are always bound to all hit shaders
struct FHitGroupSystemParameters
{
	D3D12_GPU_VIRTUAL_ADDRESS IndexBuffer;
	D3D12_GPU_VIRTUAL_ADDRESS VertexBuffer;
	FHitGroupSystemRootConstants RootConstants;
};

struct FD3D12ShaderIdentifier
{
	uint64 Data[4] = {~0ull, ~0ull, ~0ull, ~0ull};

	bool operator == (const FD3D12ShaderIdentifier& Other) const
	{
		return Data[0] == Other.Data[0]
			&& Data[1] == Other.Data[1]
			&& Data[2] == Other.Data[2]
			&& Data[3] == Other.Data[3];
	}

	bool operator != (const FD3D12ShaderIdentifier& Other) const
	{
		return !(*this == Other);
	}

	bool IsValid() const
	{
		return *this != FD3D12ShaderIdentifier();
	}

	// No shader is executed if a shader binding table record with null identifier is encountered.
	void SetNull()
	{
		Data[3] = Data[2] = Data[1] = Data[0] = 0ull;
	}

	void SetData(const void* InData)
	{
		FMemory::Memcpy(Data, InData, sizeof(Data));
	}
};

static_assert(sizeof(FD3D12ShaderIdentifier) == D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, "Unexpected shader identifier size");

struct FDXILLibrary
{
	// No copy assignment or move because FDXILLibrary points to internal struct memory
	UE_NONCOPYABLE(FDXILLibrary)

	FDXILLibrary() = default;

	void InitFromDXIL(const void* Bytecode, uint32 BytecodeLength, const LPCWSTR* InEntryNames, const LPCWSTR* InExportNames, uint32 NumEntryNames)
	{
		check(NumEntryNames != 0);
		check(InEntryNames);
		check(InExportNames);

		EntryNames.SetNum(NumEntryNames);
		ExportNames.SetNum(NumEntryNames);
		ExportDesc.SetNum(NumEntryNames);

		for (uint32 EntryIndex = 0; EntryIndex < NumEntryNames; ++EntryIndex)
		{
			EntryNames[EntryIndex] = InEntryNames[EntryIndex];
			ExportNames[EntryIndex] = InExportNames[EntryIndex];

			ExportDesc[EntryIndex].ExportToRename = *(EntryNames[EntryIndex]);
			ExportDesc[EntryIndex].Flags = D3D12_EXPORT_FLAG_NONE;
			ExportDesc[EntryIndex].Name = *(ExportNames[EntryIndex]);
		}

		Desc.DXILLibrary.pShaderBytecode = Bytecode;
		Desc.DXILLibrary.BytecodeLength = BytecodeLength;
		Desc.NumExports = ExportDesc.Num();
		Desc.pExports = ExportDesc.GetData();
	}

	void InitFromDXIL(const D3D12_SHADER_BYTECODE& ShaderBytecode, LPCWSTR* InEntryNames, LPCWSTR* InExportNames, uint32 NumEntryNames)
	{
		InitFromDXIL(ShaderBytecode.pShaderBytecode, ShaderBytecode.BytecodeLength, InEntryNames, InExportNames, NumEntryNames);
	}

	void InitFromDXIL(const FD3D12ShaderBytecode& ShaderBytecode, LPCWSTR* InEntryNames, LPCWSTR* InExportNames, uint32 NumEntryNames)
	{
		InitFromDXIL(ShaderBytecode.GetShaderBytecode(), InEntryNames, InExportNames, NumEntryNames);
	}

	D3D12_STATE_SUBOBJECT GetSubobject() const
	{
		D3D12_STATE_SUBOBJECT Subobject = {};
		Subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		Subobject.pDesc = &Desc;
		return Subobject;
	}

	// NOTE: typical DXIL library may contain up to 3 entry points (i.e. hit groups with closest hit, any hit and intersection shaders)
	// Typical case is 1 (RGS, MS or CHS only) or 2 (CHS + AHS for shaders with alpha masking)
	static constexpr uint32 ExpectedEntryPoints = 3;
	TArray<D3D12_EXPORT_DESC, TInlineAllocator<ExpectedEntryPoints>> ExportDesc;
	TArray<FString, TInlineAllocator<ExpectedEntryPoints>> EntryNames;
	TArray<FString, TInlineAllocator<ExpectedEntryPoints>> ExportNames;

	D3D12_DXIL_LIBRARY_DESC Desc = {};
};

static TRefCountPtr<ID3D12StateObject> CreateRayTracingStateObject(
	ID3D12Device5* RayTracingDevice,
	const TArrayView<const FDXILLibrary*>& ShaderLibraries,
	const TArrayView<LPCWSTR>& Exports,
	uint32 MaxPayloadSizeInBytes,
	const TArrayView<const D3D12_HIT_GROUP_DESC>& HitGroups,
	const ID3D12RootSignature* GlobalRootSignature,
	const TArrayView<ID3D12RootSignature*>& LocalRootSignatures,
	const TArrayView<uint32>& LocalRootSignatureAssociations, // indices into LocalRootSignatures, one per export (may be empty, which assumes single root signature used for everything)
	const TArrayView<D3D12_EXISTING_COLLECTION_DESC>& ExistingCollections,
	D3D12_STATE_OBJECT_TYPE StateObjectType // Full RTPSO or a Collection
)
{
	checkf((LocalRootSignatureAssociations.Num() == 0 && LocalRootSignatures.Num() == 1)
		|| (LocalRootSignatureAssociations.Num() == Exports.Num()),
		TEXT("There must be exactly one local root signature association per export."));

	TRefCountPtr<ID3D12StateObject> Result;

	// There are several pipeline sub-objects that are always required:
	// 1) D3D12_RAYTRACING_SHADER_CONFIG
	// 2) D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION
	// 3) D3D12_RAYTRACING_PIPELINE_CONFIG
	// 4) Global root signature
	static constexpr uint32 NumRequiredSubobjects = 4;

	TArray<D3D12_STATE_SUBOBJECT> Subobjects;
	Subobjects.SetNumUninitialized(NumRequiredSubobjects
		+ ShaderLibraries.Num()
		+ HitGroups.Num()
		+ LocalRootSignatures.Num()
		+ Exports.Num()
		+ ExistingCollections.Num()
	);

	TArray<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> ExportAssociations;
	ExportAssociations.SetNumUninitialized(Exports.Num());

	uint32 Index = 0;

	const uint32 NumExports = Exports.Num();

	// Shader libraries

	for (const FDXILLibrary* Library : ShaderLibraries)
	{
		Subobjects[Index++] = Library->GetSubobject();
	}

	// Shader config

	D3D12_RAYTRACING_SHADER_CONFIG ShaderConfig = {};
	ShaderConfig.MaxAttributeSizeInBytes = 8; // sizeof 2 floats (barycentrics)
	ShaderConfig.MaxPayloadSizeInBytes = MaxPayloadSizeInBytes;
	const uint32 ShaderConfigIndex = Index;
	Subobjects[Index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &ShaderConfig};

	// Shader config association

	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION ShaderConfigAssociation = {};
	ShaderConfigAssociation.NumExports = Exports.Num();
	ShaderConfigAssociation.pExports = Exports.GetData();
	ShaderConfigAssociation.pSubobjectToAssociate = &Subobjects[ShaderConfigIndex];
	Subobjects[Index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &ShaderConfigAssociation };

	// Hit groups

	for (const D3D12_HIT_GROUP_DESC& HitGroupDesc : HitGroups)
	{
		Subobjects[Index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &HitGroupDesc };
	}

	// Pipeline config

	D3D12_RAYTRACING_PIPELINE_CONFIG PipelineConfig = {};
	PipelineConfig.MaxTraceRecursionDepth = 1; // Only allow ray tracing from RayGen shader
	const uint32 PipelineConfigIndex = Index;
	Subobjects[Index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &PipelineConfig };

	// Global root signature

	Subobjects[Index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &GlobalRootSignature };

	// Local root signatures

	const uint32 LocalRootSignatureBaseIndex = Index;
	for (int32 SignatureIndex = 0; SignatureIndex < LocalRootSignatures.Num(); ++SignatureIndex)
	{
		checkf(LocalRootSignatures[SignatureIndex], TEXT("All local root signatures must be valid"));
		Subobjects[Index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &LocalRootSignatures[SignatureIndex] };
	}

	// Local root signature associations

	for (int32 ExportIndex = 0; ExportIndex < Exports.Num(); ++ExportIndex)
	{
		// If custom LocalRootSignatureAssociations data is not provided, then assume same default local RS association.
		const int32 LocalRootSignatureIndex = LocalRootSignatureAssociations.Num() != 0
			? LocalRootSignatureAssociations[ExportIndex]
			: 0;

		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION& Association = ExportAssociations[ExportIndex];
		Association = D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION{};
		Association.NumExports = 1;
		Association.pExports = &Exports[ExportIndex];

		check(LocalRootSignatureIndex < LocalRootSignatures.Num());
		Association.pSubobjectToAssociate = &Subobjects[LocalRootSignatureBaseIndex + LocalRootSignatureIndex];

		Subobjects[Index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &ExportAssociations[ExportIndex] };
	}

	// Existing collection objects

	for (int32 CollectionIndex = 0; CollectionIndex < ExistingCollections.Num(); ++CollectionIndex)
	{
		Subobjects[Index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION, &ExistingCollections[CollectionIndex] };
	}

	// Done!

	checkf(Index == Subobjects.Num(), TEXT("All pipeline subobjects must be initialized."));

	// Create ray tracing pipeline state object

	D3D12_STATE_OBJECT_DESC Desc = {};
	Desc.NumSubobjects = Index;
	Desc.pSubobjects = &Subobjects[0];
	Desc.Type = StateObjectType;

	VERIFYD3D12RESULT(RayTracingDevice->CreateStateObject(&Desc, IID_PPV_ARGS(Result.GetInitReference())));

	INC_DWORD_STAT(STAT_D3D12RayTracingCreatedPipelines);
	INC_DWORD_STAT_BY(STAT_D3D12RayTracingCompiledShaders, NumExports);

	return Result;
}

inline uint64 GetShaderHash64(FRHIRayTracingShader* ShaderRHI)
{
	uint64 ShaderHash; // 64 bits from the shader SHA1
	FMemory::Memcpy(&ShaderHash, ShaderRHI->GetHash().Hash, sizeof(ShaderHash));
	return ShaderHash;
}

// Generates a stable symbol name for a ray tracing shader, used for RT PSO creation.

inline FString GenerateShaderName(const TCHAR* Prefix, uint64 Hash)
{
	return FString::Printf(TEXT("%s_%016llx"), Prefix, Hash);
}

inline FString GenerateShaderName(FRHIRayTracingShader* ShaderRHI)
{
	const FD3D12RayTracingShader* Shader = FD3D12DynamicRHI::ResourceCast(ShaderRHI);
	uint64 ShaderHash = GetShaderHash64(ShaderRHI);
	return GenerateShaderName(*(Shader->EntryPoint), ShaderHash);
}

// Cache for ray tracing pipeline collection objects, containing single shaders that can be linked into full pipelines.
class FD3D12RayTracingPipelineCache
{
public:

	UE_NONCOPYABLE(FD3D12RayTracingPipelineCache)

	FD3D12RayTracingPipelineCache(FD3D12Device* Device)
		: DefaultLocalRootSignature(Device->GetParentAdapter())
	{
		// Default empty local root signature

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC LocalRootSignatureDesc = {};
		LocalRootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
		LocalRootSignatureDesc.Desc_1_0.Flags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		DefaultLocalRootSignature.Init(LocalRootSignatureDesc);
	}

	~FD3D12RayTracingPipelineCache()
	{
		Reset();
	}

	struct FKey
	{
		uint64 ShaderHash = 0;
		uint32 MaxPayloadSizeInBytes = 0;
		ID3D12RootSignature* GlobalRootSignature = nullptr;
		ID3D12RootSignature* LocalRootSignature = nullptr;

		bool operator == (const FKey& Other) const
		{
			return ShaderHash == Other.ShaderHash
				&& MaxPayloadSizeInBytes == Other.MaxPayloadSizeInBytes
				&& GlobalRootSignature == Other.GlobalRootSignature
				&& LocalRootSignature == Other.LocalRootSignature;
		}

		inline friend uint32 GetTypeHash(const FKey& Key)
		{
			return Key.ShaderHash;
		}
	};

	struct FEntry
	{
		// Move-only type
		FEntry() = default;
		FEntry(FEntry&& Other) = default;

		FEntry(const FEntry&) = delete;
		FEntry& operator = (const FEntry&) = delete;
		FEntry& operator = (FEntry&& Other) = delete;

		D3D12_EXISTING_COLLECTION_DESC GetCollectionDesc()
		{
			check(CompileEvent.IsValid() && CompileEvent->IsComplete());
			check(StateObject);

			D3D12_EXISTING_COLLECTION_DESC Result = {};
			Result.pExistingCollection = StateObject;
			Result.pExports = ExportDescs.GetData();
			Result.NumExports = ExportDescs.Num();

			return Result;
		}

		const TCHAR* GetPrimaryExportNameChars()
		{
			checkf(ExportNames.Num()!=0, TEXT("This ray tracing shader collection does not export any symbols."));
			return *(ExportNames[0]);
		}

		TRefCountPtr<FD3D12RayTracingShader> Shader;

		TRefCountPtr<ID3D12StateObject> StateObject;
		FGraphEventRef CompileEvent;

		static constexpr uint32 MaxExports = 4;
		TArray<FString, TFixedAllocator<MaxExports>> ExportNames;
		TArray<D3D12_EXPORT_DESC, TFixedAllocator<MaxExports>> ExportDescs;
	};

	enum class ECollectionType
	{
		RayGen,
		Miss,
		HitGroup,
		Callable,
	};

	static const TCHAR* GetCollectionTypeName(ECollectionType Type)
	{
		switch (Type)
		{
		case ECollectionType::RayGen:
			return TEXT("RayGen");
		case ECollectionType::Miss:
			return TEXT("Miss");
		case ECollectionType::HitGroup:
			return TEXT("HitGroup");
		case ECollectionType::Callable:
			return TEXT("Callable");
		default:
			return TEXT("");
		}
	}

	class FShaderCompileTask
	{
	public:

		UE_NONCOPYABLE(FShaderCompileTask)

		FShaderCompileTask(
				FEntry& InEntry,
				FKey InCacheKey,
				ID3D12Device5* InRayTracingDevice,
				ECollectionType InCollectionType)
			: Entry(InEntry)
			, CacheKey(InCacheKey)
			, RayTracingDevice(InRayTracingDevice)
			, CollectionType(InCollectionType)
		{
		}

		static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
		{
			SCOPE_CYCLE_COUNTER(STAT_RTPSO_CompileShader);

			FD3D12RayTracingShader* Shader = Entry.Shader;

			static constexpr uint32 MaxEntryPoints = 3; // CHS+AHS+IS for HitGroup or just a single entry point for other collection types
			TArray<LPCWSTR, TFixedAllocator<MaxEntryPoints>> OriginalEntryPoints;
			TArray<LPCWSTR, TFixedAllocator<MaxEntryPoints>> RenamedEntryPoints;

			const uint32 NumHitGroups = CollectionType == ECollectionType::HitGroup ? 1 : 0;
			const uint64 ShaderHash = CacheKey.ShaderHash;
			ID3D12RootSignature* GlobalRootSignature = CacheKey.GlobalRootSignature;
			ID3D12RootSignature* LocalRootSignature = CacheKey.LocalRootSignature;
			const uint32 DefaultLocalRootSignatureIndex = 0;
			uint32 MaxPayloadSizeInBytes = CacheKey.MaxPayloadSizeInBytes;

			D3D12_HIT_GROUP_DESC HitGroupDesc = {};

			if (CollectionType == ECollectionType::HitGroup)
			{
				HitGroupDesc.HitGroupExport = Entry.GetPrimaryExportNameChars();
				HitGroupDesc.Type = Shader->IntersectionEntryPoint.IsEmpty() ? D3D12_HIT_GROUP_TYPE_TRIANGLES : D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;

				{
					const FString& ExportName = Entry.ExportNames.Add_GetRef(GenerateShaderName(TEXT("CHS"), ShaderHash));

					HitGroupDesc.ClosestHitShaderImport = *ExportName;

					OriginalEntryPoints.Add(*(Shader->EntryPoint));
					RenamedEntryPoints.Add(*ExportName);
				}

				if (!Shader->AnyHitEntryPoint.IsEmpty())
				{
					const FString& ExportName = Entry.ExportNames.Add_GetRef(GenerateShaderName(TEXT("AHS"), ShaderHash));

					HitGroupDesc.AnyHitShaderImport = *ExportName;

					OriginalEntryPoints.Add(*(Shader->AnyHitEntryPoint));
					RenamedEntryPoints.Add(*ExportName);
				}

				if (!Shader->IntersectionEntryPoint.IsEmpty())
				{
					const FString& ExportName = Entry.ExportNames.Add_GetRef(GenerateShaderName(TEXT("IS"), ShaderHash));

					HitGroupDesc.IntersectionShaderImport = *ExportName;

					OriginalEntryPoints.Add(*(Shader->IntersectionEntryPoint));
					RenamedEntryPoints.Add(*ExportName);
				}
			}
			else
			{
				checkf(CollectionType == ECollectionType::Miss || CollectionType == ECollectionType::RayGen || CollectionType == ECollectionType::Callable, TEXT("Unexpected RT sahder collection type"));

				OriginalEntryPoints.Add(*(Shader->EntryPoint));
				RenamedEntryPoints.Add(Entry.GetPrimaryExportNameChars());
			}


			for (const FString& ExportName : Entry.ExportNames)
			{
				D3D12_EXPORT_DESC ExportDesc = {};
				ExportDesc.Name = *ExportName;
				ExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
				Entry.ExportDescs.Add(ExportDesc);
			}

			// Validate that memory reservation was correct

			check(Entry.ExportNames.Num() <= Entry.MaxExports);
			check(Entry.ExportDescs.Num() <= Entry.MaxExports);
			check(Entry.ExportDescs.Num() != 0);

			FDXILLibrary Library;
			Library.InitFromDXIL(Shader->ShaderBytecode, OriginalEntryPoints.GetData(), RenamedEntryPoints.GetData(), OriginalEntryPoints.Num());

			const FDXILLibrary* LibraryPtr = &Library;

			Entry.StateObject = CreateRayTracingStateObject(
				RayTracingDevice,
				MakeArrayView(&LibraryPtr, 1),
				RenamedEntryPoints,
				MaxPayloadSizeInBytes,
				MakeArrayView(&HitGroupDesc, NumHitGroups),
				GlobalRootSignature,
				MakeArrayView(&LocalRootSignature, 1),
				{}, // LocalRootSignatureAssociations (single RS will be used for all exports since this is null)
				{}, // ExistingCollections
				D3D12_STATE_OBJECT_TYPE_COLLECTION);
		}

		FORCEINLINE TStatId GetStatId() const
		{
			return GET_STATID(STAT_RTPSO_CompileShader);
		}

		ENamedThreads::Type GetDesiredThread()
		{
			return ENamedThreads::AnyHiPriThreadHiPriTask;
		}

		FEntry& Entry;
		FKey CacheKey;
		ID3D12Device5* RayTracingDevice;
		ECollectionType CollectionType;

	};

	FEntry* GetOrCompileShader(
		ID3D12Device5* RayTracingDevice,
		FD3D12RayTracingShader* Shader,
		ID3D12RootSignature* GlobalRootSignature,
		uint32 MaxPayloadSizeInBytes,
		ECollectionType CollectionType,
		FGraphEventArray& CompletionList,
		bool* bOutCacheHit = nullptr)
	{
		FScopeLock Lock(&CriticalSection);

		const uint64 ShaderHash = GetShaderHash64(Shader);

		ID3D12RootSignature* LocalRootSignature = nullptr;
		if (CollectionType == ECollectionType::HitGroup || CollectionType == ECollectionType::Callable)
		{
			// Only hit group and callable shaders have a local root signature
			LocalRootSignature = Shader->pRootSignature->GetRootSignature();
		}
		else
		{
			// ... all other shaders share a default empty local root signature
			LocalRootSignature = DefaultLocalRootSignature.GetRootSignature();
		}

		FKey CacheKey;
		CacheKey.ShaderHash = ShaderHash;
		CacheKey.MaxPayloadSizeInBytes = MaxPayloadSizeInBytes;
		CacheKey.GlobalRootSignature = GlobalRootSignature;
		CacheKey.LocalRootSignature = LocalRootSignature;

		FEntry*& FindResult = Cache.FindOrAdd(CacheKey);

		if (FindResult)
		{
			if (bOutCacheHit) *bOutCacheHit = true;
		}
		else
		{
			if (bOutCacheHit) *bOutCacheHit = false;

			if (FindResult == nullptr)
			{
				FindResult = new FEntry;
			}

			FEntry& Entry = *FindResult;

			Entry.Shader = Shader;

			// Generate primary export name, which is immediately required on the PSO creation thread.
			Entry.ExportNames.Add(GenerateShaderName(GetCollectionTypeName(CollectionType), ShaderHash));
			checkf(Entry.ExportNames.Num() == 1, TEXT("Primary export name must always be first."));

			// Defer actual compilation to another task, as there may be many shaders that may be compiled in parallel.
			// Result of the compilation (the collection PSO) is not needed until final RT PSO is linked.
			Entry.CompileEvent = TGraphTask<FShaderCompileTask>::CreateTask().ConstructAndDispatchWhenReady(
				Entry,
				CacheKey,
				RayTracingDevice,
				CollectionType
			);
		}

		if (FindResult->CompileEvent.IsValid())
		{
			if (!FindResult->CompileEvent->IsComplete())
			{
				CompletionList.Add(FindResult->CompileEvent);
			}
		}
		else
		{
			check(FindResult->StateObject != nullptr);
		}

		return FindResult;
	}

	void Reset()
	{
		FScopeLock Lock(&CriticalSection);

		for (auto It : Cache)
		{
			delete It.Value;
		}

		Cache.Reset();
	}

private:

	FCriticalSection CriticalSection;
	TMap<FKey, FEntry*> Cache;
	FD3D12RootSignature DefaultLocalRootSignature; // Default empty root signature used for default hit shaders.
};

// #dxr_todo UE-72158: FD3D12Device::GlobalViewHeap/GlobalSamplerHeap should be used instead of ad-hoc heaps here.
// Unfortunately, this requires a major refactor of how global heaps work.
// FD3D12CommandContext-s should not get static chunks of the global heap, but instead should dynamically allocate
// chunks on as-needed basis and release them when possible.
// This would allow ray tracing code to sub-allocate heap blocks from the same global heap.
class FD3D12RayTracingDescriptorHeapCache : FD3D12DeviceChild
{
public:

	UE_NONCOPYABLE(FD3D12RayTracingDescriptorHeapCache)

	struct Entry
	{
		ID3D12DescriptorHeap* Heap = nullptr;
		uint64 FenceValue = 0;
		uint32 NumDescriptors = 0;
		D3D12_DESCRIPTOR_HEAP_TYPE Type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	};

	FD3D12RayTracingDescriptorHeapCache(FD3D12Device* Device)
		: FD3D12DeviceChild(Device)
	{

	}

	~FD3D12RayTracingDescriptorHeapCache()
	{
		check(AllocatedEntries == 0);

		FScopeLock Lock(&CriticalSection);
		for (const Entry& It : Entries)
		{
			if (It.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			{
				DEC_DWORD_STAT(STAT_D3D12RayTracingViewDescriptorHeaps);
				DEC_DWORD_STAT_BY(STAT_D3D12RayTracingViewDescriptors, It.NumDescriptors);
			}
			else
			{
				DEC_DWORD_STAT(STAT_D3D12RayTracingSamplerDescriptorHeaps);
				DEC_DWORD_STAT_BY(STAT_D3D12RayTracingSamplerDescriptors, It.NumDescriptors);
			}

			It.Heap->Release();
		}
		Entries.Empty();
	}

	void ReleaseHeap(Entry& Entry)
	{
		FScopeLock Lock(&CriticalSection);

		Entries.Add(Entry);

		check(AllocatedEntries != 0);
		--AllocatedEntries;
	}

	Entry AllocateHeap(D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32 NumDescriptors)
	{
		FScopeLock Lock(&CriticalSection);

		++AllocatedEntries;

		Entry Result = {};

		const FD3D12Fence& Fence = GetParentDevice()->GetCommandListManager().GetFence();
		const uint64 CompletedFenceValue = Fence.GetLastCompletedFenceFast();

		for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
		{
			const Entry& It = Entries[EntryIndex];
			if (It.Type == Type && It.NumDescriptors >= NumDescriptors && It.FenceValue <= CompletedFenceValue)
			{
				Result = It;

				Entries[EntryIndex] = Entries.Last();
				Entries.Pop();

				return Result;
			}
		}

		// Compatible heap was not found in cache, so create a new one.

		ReleaseStaleEntries(100, CompletedFenceValue); // Release heaps that were not used for 100 frames before allocating new.

		D3D12_DESCRIPTOR_HEAP_DESC Desc = {};

		Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		Desc.Type = Type;
		Desc.NumDescriptors = NumDescriptors;
		Desc.NodeMask = (uint32)GetParentDevice()->GetGPUMask();

		ID3D12DescriptorHeap* D3D12Heap = nullptr;

		VERIFYD3D12RESULT(GetParentDevice()->GetDevice()->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&D3D12Heap)));
		SetName(D3D12Heap, Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? L"RT View Heap" : L"RT Sampler Heap");

		Result.NumDescriptors = NumDescriptors;
		Result.Type = Type;
		Result.Heap = D3D12Heap;

		if (Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		{
			INC_DWORD_STAT(STAT_D3D12RayTracingViewDescriptorHeaps);
			INC_DWORD_STAT_BY(STAT_D3D12RayTracingViewDescriptors, NumDescriptors);
		}
		else
		{
			INC_DWORD_STAT(STAT_D3D12RayTracingSamplerDescriptorHeaps);
			INC_DWORD_STAT_BY(STAT_D3D12RayTracingSamplerDescriptors, NumDescriptors);
		}

		return Result;
	}

	void ReleaseStaleEntries(uint32 MaxAge, uint64 CompletedFenceValue)
	{
		int32 EntryIndex = 0;
		while (EntryIndex < Entries.Num())
		{
			Entry& It = Entries[EntryIndex];
			if ((It.FenceValue + MaxAge) <= CompletedFenceValue)
			{
				if (It.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
				{
					DEC_DWORD_STAT(STAT_D3D12RayTracingViewDescriptorHeaps);
					DEC_DWORD_STAT_BY(STAT_D3D12RayTracingViewDescriptors, It.NumDescriptors);
				}
				else
				{
					DEC_DWORD_STAT(STAT_D3D12RayTracingSamplerDescriptorHeaps);
					DEC_DWORD_STAT_BY(STAT_D3D12RayTracingSamplerDescriptors, It.NumDescriptors);
				}

				It.Heap->Release();

				Entries[EntryIndex] = Entries.Last();
				Entries.Pop(false);
			}
			else
			{
				EntryIndex++;
			}
		}
	}

	void Flush()
	{
		FD3D12Device* Device = GetParentDevice();
		FD3D12Fence& Fence = Device->GetCommandListManager().GetFence();

		FScopeLock Lock(&CriticalSection);

		for (const Entry& It : Entries)
		{
			Device->GetParentAdapter()->GetDeferredDeletionQueue().EnqueueResource(It.Heap, &Fence);
		}
		Entries.Empty();
	}

	FCriticalSection CriticalSection;
	TArray<Entry> Entries;
	uint32 AllocatedEntries = 0;
};

struct FD3D12RayTracingDescriptorHeap : public FD3D12DeviceChild
{
	UE_NONCOPYABLE(FD3D12RayTracingDescriptorHeap)

	FD3D12RayTracingDescriptorHeap(FD3D12Device* Device)
		: FD3D12DeviceChild(Device)
	{

	}

	~FD3D12RayTracingDescriptorHeap()
	{
		if (D3D12Heap)
		{
			GetParentDevice()->GetRayTracingDescriptorHeapCache()->ReleaseHeap(HeapCacheEntry);
		}
	}

	void Init(uint32 InMaxNumDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE Type)
	{
		check(D3D12Heap == nullptr);

		HeapCacheEntry = GetParentDevice()->GetRayTracingDescriptorHeapCache()->AllocateHeap(Type, InMaxNumDescriptors);

		MaxNumDescriptors = HeapCacheEntry.NumDescriptors;
		D3D12Heap = HeapCacheEntry.Heap;

		CPUBase = D3D12Heap->GetCPUDescriptorHandleForHeapStart();
		GPUBase = D3D12Heap->GetGPUDescriptorHandleForHeapStart();

		checkf(CPUBase.ptr, TEXT("Ray tracing descriptor heap of type %d returned from descriptor heap cache is invalid."), Type);

		DescriptorSize = GetParentDevice()->GetDevice()->GetDescriptorHandleIncrementSize(Type);
	}

	bool CanAllocate(uint32 InNumDescriptors) const
	{
		return NumAllocatedDescriptors + InNumDescriptors <= MaxNumDescriptors;
	}

	uint32 Allocate(uint32 InNumDescriptors)
	{
		check(CanAllocate(InNumDescriptors));

		uint32 Result = NumAllocatedDescriptors;
		NumAllocatedDescriptors += InNumDescriptors;
		return Result;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorCPU(uint32 Index) const
	{
		checkSlow(Index < MaxNumDescriptors);
		D3D12_CPU_DESCRIPTOR_HANDLE Result = { CPUBase.ptr + Index * DescriptorSize };
		return Result;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetDescriptorGPU(uint32 Index) const
	{
		checkSlow(Index < MaxNumDescriptors);
		D3D12_GPU_DESCRIPTOR_HANDLE Result = { GPUBase.ptr + Index * DescriptorSize };
		return Result;
	}

	void UpdateSyncPoint()
	{
		const FD3D12Fence& Fence = GetParentDevice()->GetCommandListManager().GetFence();
		HeapCacheEntry.FenceValue = FMath::Max(HeapCacheEntry.FenceValue, Fence.GetCurrentFence());
	}

	ID3D12DescriptorHeap* D3D12Heap = nullptr;
	uint32 MaxNumDescriptors = 0;
	uint32 NumAllocatedDescriptors = 0;

	uint32 DescriptorSize = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE CPUBase = {};
	D3D12_GPU_DESCRIPTOR_HANDLE GPUBase = {};

	FD3D12RayTracingDescriptorHeapCache::Entry HeapCacheEntry;
};

class FD3D12RayTracingDescriptorCache : public FD3D12DeviceChild
{
public:

	UE_NONCOPYABLE(FD3D12RayTracingDescriptorCache)

	FD3D12RayTracingDescriptorCache(FD3D12Device* Device)
		: FD3D12DeviceChild(Device)
		, ViewHeap(Device)
		, SamplerHeap(Device)
	{}

	void Init(uint32 NumViewDescriptors, uint32 NumSamplerDescriptors)
	{
		ViewHeap.Init(NumViewDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		SamplerHeap.Init(NumSamplerDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
	}

	void UpdateSyncPoint()
	{
		ViewHeap.UpdateSyncPoint();
		SamplerHeap.UpdateSyncPoint();
	}

	void SetDescriptorHeaps(FD3D12CommandContext& CommandContext)
	{
		UpdateSyncPoint();

		ID3D12DescriptorHeap* Heaps[2] =
		{
			ViewHeap.D3D12Heap,
			SamplerHeap.D3D12Heap
		};

		check(ViewHeap.GetParentDevice() == CommandContext.GetParentDevice());
		check(SamplerHeap.GetParentDevice() == CommandContext.GetParentDevice());

		CommandContext.CommandListHandle.GraphicsCommandList()->SetDescriptorHeaps(2, Heaps);
	}

	uint32 GetDescriptorTableBaseIndex(const D3D12_CPU_DESCRIPTOR_HANDLE* Descriptors, uint32 NumDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE Type)
	{
		checkSlow(Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

		FD3D12RayTracingDescriptorHeap& Heap = (Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) ? ViewHeap : SamplerHeap;
		TMap<uint64, uint32>& Map = (Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) ? ViewDescriptorTableCache : SamplerDescriptorTableCache;

		const uint64 Key = CityHash64((const char*)Descriptors, sizeof(Descriptors[0]) * NumDescriptors);

		uint32 DescriptorTableBaseIndex = ~0u;
		const uint32* FoundDescriptorTableBaseIndex = Map.Find(Key);

		if (FoundDescriptorTableBaseIndex)
		{
			DescriptorTableBaseIndex = *FoundDescriptorTableBaseIndex;
		}
		else
		{
			DescriptorTableBaseIndex = Heap.Allocate(NumDescriptors);

			D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor = Heap.GetDescriptorCPU(DescriptorTableBaseIndex);
			checkf(Heap.CPUBase.ptr, TEXT("Ray tracing descriptor heap of type %d assigned to descriptor cache is invalid."), Type);
			GetParentDevice()->GetDevice()->CopyDescriptors(1, &DestDescriptor, &NumDescriptors, NumDescriptors, Descriptors, nullptr, Type);

			Map.Add(Key, DescriptorTableBaseIndex);

			if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			{
				INC_DWORD_STAT_BY(STAT_D3D12RayTracingUsedViewDescriptors, NumDescriptors);
			}
			else
			{
				INC_DWORD_STAT_BY(STAT_D3D12RayTracingUsedSamplerDescriptors, NumDescriptors);
			}
		}

		return DescriptorTableBaseIndex;
	}

	FD3D12RayTracingDescriptorHeap ViewHeap;
	FD3D12RayTracingDescriptorHeap SamplerHeap;

	TMap<uint64, uint32> ViewDescriptorTableCache;
	TMap<uint64, uint32> SamplerDescriptorTableCache;
};

class FD3D12RayTracingShaderTable
{
private:

	void WriteData(uint32 WriteOffset, const void* InData, uint32 InDataSize)
	{
#if DO_CHECK && DO_GUARD_SLOW
		Data.RangeCheck(WriteOffset);
		Data.RangeCheck(WriteOffset + InDataSize - 1);
#endif // DO_CHECK && DO_GUARD_SLOW

		FMemory::Memcpy(Data.GetData() + WriteOffset, InData, InDataSize);

		bIsDirty = true;
	}

	void WriteLocalShaderRecord(uint32 RecordIndex, uint32 OffsetWithinRecord, const void* InData, uint32 InDataSize)
	{
		checkfSlow(OffsetWithinRecord % 4 == 0, TEXT("SBT record parameters must be written on DWORD-aligned boundary"));
		checkfSlow(InDataSize % 4 == 0, TEXT("SBT record parameters must be DWORD-aligned"));
		checkfSlow(OffsetWithinRecord + InDataSize <= LocalRecordSizeUnaligned, TEXT("SBT record write request is out of bounds"));
		checkfSlow(RecordIndex < NumLocalRecords, TEXT("SBT local record write request is out of bounds"));

		const uint32 WriteOffset = LocalShaderTableOffset + LocalRecordStride * RecordIndex + OffsetWithinRecord;

		WriteData(WriteOffset, InData, InDataSize);
	}

public:

	UE_NONCOPYABLE(FD3D12RayTracingShaderTable)

	struct FInitializer
	{
		uint32 NumRayGenShaders = 0;
		uint32 NumMissShaders = 0;
		uint32 NumHitRecords = 0;
		uint32 NumCallableRecords = 0;
		uint32 LocalRootDataSize = 0;
		uint32 MaxViewDescriptorsPerRecord = 0;
	};

	FD3D12RayTracingShaderTable()
	{
	}

	~FD3D12RayTracingShaderTable()
	{
		delete DescriptorCache;
	}

	void Init(const FInitializer& Initializer, FD3D12Device* Device)
	{
		checkf(Initializer.LocalRootDataSize <= 4096, TEXT("The maximum size of a local root signature is 4KB.")); // as per section 4.22.1 of DXR spec v1.0
		checkf(Initializer.NumRayGenShaders >= 1, TEXT("All shader tables must contain at least one raygen shader."));

		LocalRecordSizeUnaligned = ShaderIdentifierSize + Initializer.LocalRootDataSize;
		LocalRecordStride = RoundUpToNextMultiple(LocalRecordSizeUnaligned, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

		// Custom descriptor cache is only required when local resources may be bound.
		// If only global resources are used, then transient descriptor cache can be used.
		const bool bNeedsDescriptorCache = (Initializer.NumHitRecords + Initializer.NumCallableRecords) * Initializer.LocalRootDataSize != 0;

		if (bNeedsDescriptorCache)
		{
			// Minimum number of descriptors required to support binding global resources (arbitrarily chosen)
			// #dxr_todo UE-72158: Remove this when RT descriptors are sub-allocated from the global view descriptor heap.
			const uint32 MinNumViewDescriptors = 1024;

			// D3D12 is guaranteed to support 1M (D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1) descriptors in a CBV/SRV/UAV heap, so clamp the size to this.
			// https://docs.microsoft.com/en-us/windows/desktop/direct3d12/hardware-support
			const uint32 NumViewDescriptors = FMath::Max(MinNumViewDescriptors, FMath::Min<uint32>(Initializer.NumHitRecords * Initializer.MaxViewDescriptorsPerRecord, D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1));
			const uint32 NumSamplerDescriptors = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;

			DescriptorCache = new FD3D12RayTracingDescriptorCache(Device);
			DescriptorCache->Init(NumViewDescriptors, NumSamplerDescriptors);
		}

		NumRayGenShaders = Initializer.NumRayGenShaders;
		NumMissShaders = Initializer.NumMissShaders;
		NumHitRecords = Initializer.NumHitRecords;
		NumCallableRecords = Initializer.NumCallableRecords;

		uint32 TotalDataSize = 0;

		RayGenShaderTableOffset = TotalDataSize;
		TotalDataSize += NumRayGenShaders * RayGenRecordStride;
		TotalDataSize = RoundUpToNextMultiple(TotalDataSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

		MissShaderTableOffset = TotalDataSize;
		TotalDataSize += NumMissShaders * MissRecordStride;
		TotalDataSize = RoundUpToNextMultiple(TotalDataSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

		DefaultHitGroupShaderTableOffset = TotalDataSize;
		TotalDataSize += ShaderIdentifierSize;
		TotalDataSize = RoundUpToNextMultiple(TotalDataSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

		HitGroupShaderTableOffset = TotalDataSize;
		TotalDataSize += Initializer.NumHitRecords * LocalRecordStride;
		TotalDataSize = RoundUpToNextMultiple(TotalDataSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

		CallableShaderTableOffset = TotalDataSize;
		TotalDataSize += Initializer.NumCallableRecords * LocalRecordStride;
		TotalDataSize = RoundUpToNextMultiple(TotalDataSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

		// Hit groups and callable shaders are stored in a consecutive memory block and are accessed using common local record indices.
		LocalShaderTableOffset = HitGroupShaderTableOffset;
		CallableShaderRecordIndexOffset = (CallableShaderTableOffset - LocalShaderTableOffset) / LocalRecordStride;
		NumLocalRecords = (TotalDataSize - LocalShaderTableOffset) / LocalRecordStride;

		Data.SetNumZeroed(TotalDataSize);

		// Keep CPU-side data after upload
		Data.SetAllowCPUAccess(true);
	}

	template <typename T>
	void SetLocalShaderParameters(uint32 RecordIndex, uint32 InOffsetWithinRootSignature, const T& Parameters)
	{
		WriteLocalShaderRecord(RecordIndex, ShaderIdentifierSize + InOffsetWithinRootSignature, &Parameters, sizeof(Parameters));
	}

	void SetLocalShaderParameters(uint32 RecordIndex, uint32 InOffsetWithinRootSignature, const void* InData, uint32 InDataSize)
	{
		WriteLocalShaderRecord(RecordIndex, ShaderIdentifierSize + InOffsetWithinRootSignature, InData, InDataSize);
	}

	void CopyLocalShaderParameters(uint32 InDestRecordIndex, uint32 InSourceRecordIndex, uint32 InOffsetWithinRootSignature)
	{
		const uint32 BaseOffset = LocalShaderTableOffset + ShaderIdentifierSize + InOffsetWithinRootSignature;
		const uint32 DestOffset   = BaseOffset + LocalRecordStride * InDestRecordIndex;
		const uint32 SourceOffset = BaseOffset + LocalRecordStride * InSourceRecordIndex;
		const uint32 CopySize = LocalRecordStride - ShaderIdentifierSize - InOffsetWithinRootSignature;
		checkSlow(CopySize <= LocalRecordStride);

		FMemory::Memcpy(
			Data.GetData() + DestOffset,
			Data.GetData() + SourceOffset,
			CopySize);

		bIsDirty = true;
	}

	void SetLocalShaderIdentifier(uint32 RecordIndex, const void* ShaderIdentifierData, uint32 InShaderIdentifierSize)
	{
		checkSlow(InShaderIdentifierSize == ShaderIdentifierSize);

		WriteLocalShaderRecord(RecordIndex, 0, ShaderIdentifierData, InShaderIdentifierSize);
	}

	void SetRayGenIdentifier(uint32 RecordIndex, const FD3D12ShaderIdentifier& ShaderIdentifier)
	{
		const uint32 WriteOffset = RayGenShaderTableOffset + RecordIndex * RayGenRecordStride;
		WriteData(WriteOffset, ShaderIdentifier.Data, ShaderIdentifierSize);
	}

	void SetMissIdentifier(uint32 RecordIndex, const FD3D12ShaderIdentifier& ShaderIdentifier)
	{
		const uint32 WriteOffset = MissShaderTableOffset + RecordIndex * MissRecordStride;
		WriteData(WriteOffset, ShaderIdentifier.Data, ShaderIdentifierSize);
	}

	void SetDefaultHitGroupIdentifier(const FD3D12ShaderIdentifier& ShaderIdentifier)
	{
		const uint32 WriteOffset = DefaultHitGroupShaderTableOffset;
		WriteData(WriteOffset, ShaderIdentifier.Data, ShaderIdentifierSize);
	}

	void SetLocalShaderIdentifier(uint32 RecordIndex, const FD3D12ShaderIdentifier& ShaderIdentifier)
	{
		checkfSlow(ShaderIdentifier.IsValid(), TEXT("Shader identifier must be initialized FD3D12RayTracingPipelineState::GetShaderIdentifier() before use."));
		checkSlow(sizeof(ShaderIdentifier.Data) >= ShaderIdentifierSize);

		SetLocalShaderIdentifier(RecordIndex, ShaderIdentifier.Data, ShaderIdentifierSize);
	}

	void SetRayGenIdentifiers(const TArrayView<const FD3D12ShaderIdentifier>& Identifiers)
	{
		check(Identifiers.Num() == NumRayGenShaders);
		for (int32 Index = 0; Index < Identifiers.Num(); ++Index)
		{
			SetRayGenIdentifier(Index, Identifiers[Index]);
		}
	}

	void SetMissIdentifiers(const TArrayView<const FD3D12ShaderIdentifier>& Identifiers)
	{
		check(Identifiers.Num() == NumMissShaders);
		for (int32 Index = 0; Index < Identifiers.Num(); ++Index)
		{
			SetMissIdentifier(Index, Identifiers[Index]);
		}
	}

	void SetLocalShaderIdentifiers(uint32 RecordIndexOffset, const TArrayView<const FD3D12ShaderIdentifier>& Identifiers)
	{
		check(Identifiers.Num() == NumCallableRecords);
		for (int32 Index = 0; Index < Identifiers.Num(); ++Index)
		{
			SetLocalShaderIdentifier(RecordIndexOffset + Index, Identifiers[Index]);
		}
	}

	void CopyToGPU(FD3D12Device* Device)
	{
		check(IsInRHIThread() || !IsRunningRHIInSeparateThread());

		checkf(Data.Num(), TEXT("Shader table is expected to be initialized before copying to GPU."));

		FD3D12Adapter* Adapter = Device->GetParentAdapter();

		D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(Data.GetResourceDataSize(), D3D12_RESOURCE_FLAG_NONE, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.ResourceArray = &Data;

		Buffer = Adapter->CreateRHIBuffer<FD3D12MemBuffer>(
			nullptr, BufferDesc, BufferDesc.Alignment,
			0, BufferDesc.Width, BUF_Static, CreateInfo,
			FRHIGPUMask::FromIndex(Device->GetGPUIndex()));

		SetName(Buffer->GetResource(), TEXT("Shader binding table"));

		bIsDirty = false;
	}

	D3D12_GPU_VIRTUAL_ADDRESS GetShaderTableAddress() const
	{
		checkf(!bIsDirty, TEXT("Shader table update is pending, therefore GPU address is not available. Use CopyToGPU() to upload data and acquire a valid GPU buffer address."));
		return Buffer->ResourceLocation.GetGPUVirtualAddress();
	}

	D3D12_DISPATCH_RAYS_DESC GetDispatchRaysDesc(uint32 RayGenShaderIndex, uint32 MissShaderBaseIndex, bool bAllowHitGroupIndexing) const
	{
		D3D12_GPU_VIRTUAL_ADDRESS ShaderTableAddress = GetShaderTableAddress();

		D3D12_DISPATCH_RAYS_DESC Desc = {};

		Desc.RayGenerationShaderRecord.StartAddress = ShaderTableAddress + RayGenShaderTableOffset + RayGenShaderIndex * RayGenRecordStride;
		Desc.RayGenerationShaderRecord.SizeInBytes = RayGenRecordStride;

		Desc.MissShaderTable.StartAddress = ShaderTableAddress + MissShaderTableOffset + MissShaderBaseIndex * MissRecordStride;
		Desc.MissShaderTable.StrideInBytes = MissRecordStride;
		Desc.MissShaderTable.SizeInBytes = MissRecordStride;

		if (NumCallableRecords)
		{
			Desc.CallableShaderTable.StartAddress = ShaderTableAddress + CallableShaderTableOffset;
			Desc.CallableShaderTable.StrideInBytes = LocalRecordStride;
			Desc.CallableShaderTable.SizeInBytes = NumCallableRecords * LocalRecordStride;
		}

		if (bAllowHitGroupIndexing)
		{
			Desc.HitGroupTable.StartAddress = ShaderTableAddress + HitGroupShaderTableOffset;
			Desc.HitGroupTable.StrideInBytes = LocalRecordStride;
			Desc.HitGroupTable.SizeInBytes = NumHitRecords * LocalRecordStride;
		}
		else
		{
			Desc.HitGroupTable.StartAddress = ShaderTableAddress + DefaultHitGroupShaderTableOffset;
			Desc.HitGroupTable.StrideInBytes = 0; // Zero stride effectively disables SBT indexing
			Desc.HitGroupTable.SizeInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT; // Minimal table with only one record
		}

		return Desc;
	}

	static constexpr uint32 ShaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	uint32 NumHitRecords = 0;
	uint32 NumRayGenShaders = 0;
	uint32 NumMissShaders = 0;
	uint32 NumCallableRecords = 0;
	uint32 NumLocalRecords = 0;

	uint32 RayGenShaderTableOffset = 0;
	uint32 MissShaderTableOffset = 0;
	uint32 DefaultHitGroupShaderTableOffset = 0;
	uint32 HitGroupShaderTableOffset = 0;
	uint32 CallableShaderTableOffset = 0;
	uint32 LocalShaderTableOffset = 0;
	uint32 CallableShaderRecordIndexOffset = 0;

	// Note: TABLE_BYTE_ALIGNMENT is used instead of RECORD_BYTE_ALIGNMENT to allow arbitrary switching 
	// between multiple RayGen and Miss shaders within the same underlying table.
	static constexpr uint32 RayGenRecordStride = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
	static constexpr uint32 MissRecordStride = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

	uint32 LocalRecordSizeUnaligned = 0; // size of the shader identifier + local root parameters, not aligned to SHADER_RECORD_BYTE_ALIGNMENT (used for out-of-bounds access checks)
	uint32 LocalRecordStride = 0; // size of shader identifier + local root parameters, aligned to SHADER_RECORD_BYTE_ALIGNMENT (same for hit groups and callable shaders)
	TResourceArray<uint8, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT> Data;

	bool bIsDirty = true;
	TRefCountPtr<FD3D12MemBuffer> Buffer;

	// SBTs have their own descriptor heaps
	FD3D12RayTracingDescriptorCache* DescriptorCache = nullptr;

	struct FShaderRecordCacheKey
	{
		static constexpr uint32 MaxUniformBuffers = 4;
		FRHIUniformBuffer* const* UniformBuffers[MaxUniformBuffers];
		uint64 Hash;
		uint32 NumUniformBuffers;

		FShaderRecordCacheKey() = default;
		FShaderRecordCacheKey(uint32 InNumUniformBuffers, FRHIUniformBuffer* const* InUniformBuffers)
		{
			check(InNumUniformBuffers <= MaxUniformBuffers);
			NumUniformBuffers = FMath::Min(MaxUniformBuffers, InNumUniformBuffers);

			const uint64 DataSizeInBytes = sizeof(FRHIUniformBuffer*) * NumUniformBuffers;
			FMemory::Memcpy(UniformBuffers, InUniformBuffers, DataSizeInBytes);
			Hash = CityHash64(reinterpret_cast<const char*>(UniformBuffers), DataSizeInBytes);
		}

		bool operator == (const FShaderRecordCacheKey& Other) const
		{
			if (Hash != Other.Hash) return false;
			if (NumUniformBuffers != Other.NumUniformBuffers) return false;

			for (uint32 BufferIndex = 0; BufferIndex < NumUniformBuffers; ++BufferIndex)
			{
				if (UniformBuffers[BufferIndex] != Other.UniformBuffers[BufferIndex]) return false;
			}

			return true;
		}

		friend uint32 GetTypeHash(const FShaderRecordCacheKey& Key)
		{
			return uint32(Key.Hash);
		}
	};
	TMap<FShaderRecordCacheKey, uint32> ShaderRecordCache;

	// A set of all resources referenced by this shader table for the purpose of updating residency before ray tracing work dispatch.
	// #dxr_todo UE-72159: remove resources from this set when SBT slot entries are replaced
	TSet<void*> ReferencedD3D12ResourceSet;
	TArray<TRefCountPtr<FD3D12Resource>> ReferencedD3D12Resources;
	TArray<TRefCountPtr<FRHIResource>> ReferencedResources;
	void AddResourceReference(FD3D12Resource* D3D12Resource, FRHIResource* Resource)
	{
		bool bIsAlreadyInSet = false;
		ReferencedD3D12ResourceSet.Add(D3D12Resource, &bIsAlreadyInSet);
		if (!bIsAlreadyInSet)
		{
			ReferencedD3D12Resources.Add(D3D12Resource);
			if (Resource)
			{
				ReferencedResources.Add(Resource);
			}
		}
	}
	void UpdateResidency(FD3D12CommandContext& CommandContext)
	{
		for (FD3D12Resource* Resource : ReferencedD3D12Resources)
		{
			Resource->UpdateResidency(CommandContext.CommandListHandle);
		}
		Buffer->GetResource()->UpdateResidency(CommandContext.CommandListHandle);
	}
};


template<typename ShaderType>
static FD3D12RayTracingShader* GetBuildInRayTracingShader()
{
	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	auto Shader = ShaderMap->GetShader<ShaderType>();
	FD3D12RayTracingShader* RayTracingShader = static_cast<FD3D12RayTracingShader*>(Shader->GetRayTracingShader());
	return RayTracingShader;
}

template<typename ShaderType>
static void GetBuildInShaderLibrary(FDXILLibrary& ShaderLibrary)
{
	FD3D12RayTracingShader* RayTracingShader = GetBuildInRayTracingShader<ShaderType>();
	LPCWSTR EntryName[1] = { *RayTracingShader->EntryPoint };
	ShaderLibrary.InitFromDXIL(RayTracingShader->ShaderBytecode.GetShaderBytecode().pShaderBytecode, RayTracingShader->ShaderBytecode.GetShaderBytecode().BytecodeLength, EntryName, EntryName, 1);
}

void FD3D12Device::DestroyRayTracingDescriptorCache()
{
	delete RayTracingDescriptorHeapCache;
	RayTracingDescriptorHeapCache = nullptr;
}

struct FD3D12RayTracingShaderLibrary
{
	void Reserve(uint32 NumShaders)
	{
		Shaders.Reserve(NumShaders);
		Identifiers.Reserve(NumShaders);
	}

	int32 Find(FSHAHash Hash) const
	{
		for (int32 Index = 0; Index < Shaders.Num(); ++Index)
		{
			if (Hash == Shaders[Index]->GetHash())
			{
				return Index;
			}
		}

		return INDEX_NONE;
	}

	TArray<TRefCountPtr<FD3D12RayTracingShader>> Shaders;
	TArray<FD3D12ShaderIdentifier> Identifiers;
};

class FD3D12RayTracingPipelineState : public FRHIRayTracingPipelineState
{
public:

	UE_NONCOPYABLE(FD3D12RayTracingPipelineState)

	FD3D12RayTracingPipelineState(FD3D12Device* Device, const FRayTracingPipelineStateInitializer& Initializer)
	{
		SCOPE_CYCLE_COUNTER(STAT_RTPSO_CreatePipeline);

		checkf(Initializer.GetRayGenTable().Num() > 0, TEXT("Ray tracing pipelines must have at leat one ray generation shader."));

		uint64 TotalCreationTime = 0;
		uint64 CompileTime = 0;
		uint64 LinkTime = 0;
		uint32 NumCacheHits = 0;

		TotalCreationTime -= FPlatformTime::Cycles64();

		ID3D12Device5* RayTracingDevice = Device->GetRayTracingDevice();

		// Use hit and miss shaders from initializer or fall back to default ones if none were provided

		FRHIRayTracingShader* DefaultHitShader = GetBuildInRayTracingShader<FDefaultMainCHS>();
		FRHIRayTracingShader* DefaultHitGroupTable[] = { DefaultHitShader };

		TArrayView<FRHIRayTracingShader*> InitializerHitGroups = Initializer.GetHitGroupTable().Num()
			? Initializer.GetHitGroupTable()
			: DefaultHitGroupTable;

		FRHIRayTracingShader* DefaultMissShader = GetBuildInRayTracingShader<FDefaultMainMS>();
		FRHIRayTracingShader* DefaultMissTable[] = { DefaultMissShader };

		TArrayView<FRHIRayTracingShader*> InitializerMissShaders = Initializer.GetMissTable().Num()
			? Initializer.GetMissTable()
			: DefaultMissTable;

		TArrayView<FRHIRayTracingShader*> InitializerRayGenShaders = Initializer.GetRayGenTable();
		TArrayView<FRHIRayTracingShader*> InitializerCallableShaders = Initializer.GetCallableTable();

		const uint32 MaxTotalShaders = InitializerRayGenShaders.Num() + InitializerMissShaders.Num() + InitializerHitGroups.Num() + InitializerCallableShaders.Num();

		// All raygen and miss shaders must share the same global root signature, so take the first one and validate the rest

		GlobalRootSignature = FD3D12DynamicRHI::ResourceCast(InitializerRayGenShaders[0])->pRootSignature->GetRootSignature();

		// Helper function to acquire a D3D12_EXISTING_COLLECTION_DESC for a compiled shader via cache

		TSet<uint64> UniqueShaderHashes;
		UniqueShaderHashes.Reserve(MaxTotalShaders);

		TArray<FD3D12RayTracingPipelineCache::FEntry*> UniqueShaderCollections;
		UniqueShaderCollections.Reserve(MaxTotalShaders);

		FGraphEventArray CompileCompletionList;
		CompileCompletionList.Reserve(MaxTotalShaders);

		FD3D12RayTracingPipelineCache* PipelineCache = Device->GetRayTracingPipelineCache();

		auto AddShaderCollection = [Device, RayTracingDevice, GlobalRootSignature = this->GlobalRootSignature, PipelineCache,
										&UniqueShaderHashes, &UniqueShaderCollections, &Initializer, &NumCacheHits, &CompileTime,
										&CompileCompletionList]
			(FD3D12RayTracingShader* Shader, FD3D12RayTracingPipelineCache::ECollectionType CollectionType)
		{
			bool bIsAlreadyInSet = false;
			const uint64 ShaderHash = GetShaderHash64(Shader);
			UniqueShaderHashes.Add(ShaderHash, &bIsAlreadyInSet);

			bool bCacheHit = false;

			CompileTime -= FPlatformTime::Cycles64();

			FD3D12RayTracingPipelineCache::FEntry* ShaderCacheEntry = PipelineCache->GetOrCompileShader(
				RayTracingDevice, Shader, GlobalRootSignature,
				Initializer.MaxPayloadSizeInBytes,
				CollectionType, CompileCompletionList,
				&bCacheHit);

			CompileTime += FPlatformTime::Cycles64();

			if (!bIsAlreadyInSet)
			{
				UniqueShaderCollections.Add(ShaderCacheEntry);

				if (bCacheHit) NumCacheHits++;
			}

			return ShaderCacheEntry;
		};

		// If no custom hit groups were provided, then disable SBT indexing and force default shader on all primitives

		bAllowHitGroupIndexing = Initializer.GetHitGroupTable().Num() ? Initializer.bAllowHitGroupIndexing : false;

		// Add ray generation shaders

		TArray<LPCWSTR> RayGenShaderNames;

		RayGenShaders.Reserve(InitializerRayGenShaders.Num());
		RayGenShaderNames.Reserve(InitializerRayGenShaders.Num());

		for (FRHIRayTracingShader* ShaderRHI : InitializerRayGenShaders)
		{
			FD3D12RayTracingShader* Shader = FD3D12DynamicRHI::ResourceCast(ShaderRHI);
			checkf(Shader->pRootSignature->GetRootSignature() == GlobalRootSignature, TEXT("All raygen and miss shaders must share the same root signature"));
			checkf(!Shader->ResourceCounts.bGlobalUniformBufferUsed, TEXT("Global uniform buffers are not implemented for ray generation shaders"));

			FD3D12RayTracingPipelineCache::FEntry* ShaderCacheEntry = AddShaderCollection(Shader, FD3D12RayTracingPipelineCache::ECollectionType::RayGen);

			RayGenShaderNames.Add(ShaderCacheEntry->GetPrimaryExportNameChars());
			RayGenShaders.Shaders.Add(Shader);
		}

		// Add miss shaders

		TArray<LPCWSTR> MissShaderNames;
		MissShaders.Reserve(InitializerMissShaders.Num());
		MissShaderNames.Reserve(InitializerMissShaders.Num());

		for (FRHIRayTracingShader* ShaderRHI : InitializerMissShaders)
		{
			FD3D12RayTracingShader* Shader = FD3D12DynamicRHI::ResourceCast(ShaderRHI);
			checkf(Shader->pRootSignature->GetRootSignature() == GlobalRootSignature, TEXT("All raygen and miss shaders must share the same root signature"));
			checkf(!Shader->ResourceCounts.bGlobalUniformBufferUsed, TEXT("Global uniform buffers are not implemented for ray tracing miss shaders"));

			FD3D12RayTracingPipelineCache::FEntry* ShaderCacheEntry = AddShaderCollection(Shader, FD3D12RayTracingPipelineCache::ECollectionType::Miss);

			MissShaderNames.Add(ShaderCacheEntry->GetPrimaryExportNameChars());
			MissShaders.Shaders.Add(Shader);
		}

		// Add hit groups

		MaxHitGroupViewDescriptors = 0;
		MaxLocalRootSignatureSize = 0;

		TArray<LPCWSTR> HitGroupNames;
		HitGroupShaders.Reserve(InitializerHitGroups.Num());
		HitGroupNames.Reserve(InitializerHitGroups.Num());

		for (FRHIRayTracingShader* ShaderRHI : InitializerHitGroups)
		{
			FD3D12RayTracingShader* Shader = FD3D12DynamicRHI::ResourceCast(ShaderRHI);

			checkf(Shader, TEXT("A valid ray tracing hit group shader must be provided for all elements in the FRayTracingPipelineStateInitializer hit group table."));

			const uint32 ShaderViewDescriptors = Shader->ResourceCounts.NumSRVs + Shader->ResourceCounts.NumUAVs;
			MaxHitGroupViewDescriptors = FMath::Max(MaxHitGroupViewDescriptors, ShaderViewDescriptors);
			MaxLocalRootSignatureSize = FMath::Max(MaxLocalRootSignatureSize, Shader->pRootSignature->GetTotalRootSignatureSizeInBytes());

			FD3D12RayTracingPipelineCache::FEntry* ShaderCacheEntry = AddShaderCollection(Shader, FD3D12RayTracingPipelineCache::ECollectionType::HitGroup);

			HitGroupNames.Add(ShaderCacheEntry->GetPrimaryExportNameChars());
			HitGroupShaders.Shaders.Add(Shader);
		}

		// Add callable shaders

		TArray<LPCWSTR> CallableShaderNames;
		CallableShaders.Reserve(InitializerCallableShaders.Num());
		CallableShaderNames.Reserve(InitializerCallableShaders.Num());

		for (FRHIRayTracingShader* ShaderRHI : InitializerCallableShaders)
		{
			FD3D12RayTracingShader* Shader = FD3D12DynamicRHI::ResourceCast(ShaderRHI);

			checkf(Shader, TEXT("A valid ray tracing shader must be provided for all elements in the FRayTracingPipelineStateInitializer callable shader table."));
			checkf(!Shader->ResourceCounts.bGlobalUniformBufferUsed, TEXT("Global uniform buffers are not implemented for ray tracing callable shaders"));

			const uint32 ShaderViewDescriptors = Shader->ResourceCounts.NumSRVs + Shader->ResourceCounts.NumUAVs;
			MaxHitGroupViewDescriptors = FMath::Max(MaxHitGroupViewDescriptors, ShaderViewDescriptors);
			MaxLocalRootSignatureSize = FMath::Max(MaxLocalRootSignatureSize, Shader->pRootSignature->GetTotalRootSignatureSizeInBytes());

			FD3D12RayTracingPipelineCache::FEntry* ShaderCacheEntry = AddShaderCollection(Shader, FD3D12RayTracingPipelineCache::ECollectionType::Callable);

			CallableShaderNames.Add(ShaderCacheEntry->GetPrimaryExportNameChars());
			CallableShaders.Shaders.Add(Shader);
		}

		// Wait for all compilation tasks to be complete and then gather the compiled collection descriptors

		CompileTime -= FPlatformTime::Cycles64();

		FGraphEventRef CompileCompletionFence = FFunctionGraphTask::CreateAndDispatchWhenReady([]() {}, TStatId(), &CompileCompletionList, ENamedThreads::AnyHiPriThreadHiPriTask);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(CompileCompletionFence);

		CompileTime += FPlatformTime::Cycles64();


		TArray<D3D12_EXISTING_COLLECTION_DESC> UniqueShaderCollectionDescs;
		UniqueShaderCollectionDescs.Reserve(MaxTotalShaders);
		for (FD3D12RayTracingPipelineCache::FEntry* Entry : UniqueShaderCollections)
		{
			UniqueShaderCollectionDescs.Add(Entry->GetCollectionDesc());
		}

		// Link final RTPSO from shader collections

		LinkTime -= FPlatformTime::Cycles64();
		StateObject = CreateRayTracingStateObject(
			RayTracingDevice,
			{}, // Libraries,
			{}, // LibraryExports,
			Initializer.MaxPayloadSizeInBytes,
			{}, // HitGroups
			GlobalRootSignature,
			{}, // LocalRootSignatures
			{}, // LocalRootSignatureAssociations,
			UniqueShaderCollectionDescs,
			D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
		LinkTime += FPlatformTime::Cycles64();

		HRESULT QueryInterfaceResult = StateObject->QueryInterface(IID_PPV_ARGS(PipelineProperties.GetInitReference()));
		checkf(SUCCEEDED(QueryInterfaceResult), TEXT("Failed to query pipeline properties from the ray tracing pipeline state object. Result=%08x"), QueryInterfaceResult);

		auto GetShaderIdentifier = [PipelineProperties = this->PipelineProperties.GetReference()](LPCWSTR ExportName)->FD3D12ShaderIdentifier
		{
			FD3D12ShaderIdentifier Result;

			const void* Data = PipelineProperties->GetShaderIdentifier(ExportName);
			checkf(Data, TEXT("Couldn't find requested export in the ray tracing shader pipeline"));

			if (Data)
			{
				Result.SetData(Data);
			}

			return Result;
		};

		// Query shader identifiers from the pipeline state object

		check(HitGroupNames.Num() == InitializerHitGroups.Num());

		HitGroupShaders.Identifiers.SetNumUninitialized(InitializerHitGroups.Num());
		for (int32 HitGroupIndex = 0; HitGroupIndex < HitGroupNames.Num(); ++HitGroupIndex)
		{
			LPCWSTR ExportNameChars = HitGroupNames[HitGroupIndex];
			HitGroupShaders.Identifiers[HitGroupIndex] = GetShaderIdentifier(ExportNameChars);
		}

		RayGenShaders.Identifiers.SetNumUninitialized(RayGenShaderNames.Num());
		for (int32 ShaderIndex = 0; ShaderIndex < RayGenShaderNames.Num(); ++ShaderIndex)
		{
			LPCWSTR ExportNameChars = RayGenShaderNames[ShaderIndex];
			RayGenShaders.Identifiers[ShaderIndex] = GetShaderIdentifier(ExportNameChars);
		}

		MissShaders.Identifiers.SetNumUninitialized(MissShaderNames.Num());
		for (int32 ShaderIndex = 0; ShaderIndex < MissShaderNames.Num(); ++ShaderIndex)
		{
			LPCWSTR ExportNameChars = MissShaderNames[ShaderIndex];
			MissShaders.Identifiers[ShaderIndex] = GetShaderIdentifier(ExportNameChars);
		}

		CallableShaders.Identifiers.SetNumUninitialized(CallableShaderNames.Num());
		for (int32 ShaderIndex = 0; ShaderIndex < CallableShaderNames.Num(); ++ShaderIndex)
		{
			LPCWSTR ExportNameChars = CallableShaderNames[ShaderIndex];
			CallableShaders.Identifiers[ShaderIndex] = GetShaderIdentifier(ExportNameChars);
		}

		// Setup default shader binding table, which simply includes all provided RGS and MS plus a single default closest hit shader.
		// Hit record indexing and local resources access is disabled when using using this SBT.

		FD3D12RayTracingShaderTable::FInitializer SBTInitializer = {};
		SBTInitializer.NumRayGenShaders = RayGenShaders.Identifiers.Num();
		SBTInitializer.NumMissShaders = MissShaders.Identifiers.Num();
		SBTInitializer.NumCallableRecords = 0; // Default SBT does not support callable shaders
		SBTInitializer.NumHitRecords = 0; // Default SBT does not support indexable hit shaders
		SBTInitializer.LocalRootDataSize = 0; // Shaders in default SBT are not allowed to access any local resources

		// Create default shader tables for every node in the LDA group
		FD3D12Adapter* Adapter = Device->GetParentAdapter();

		for (uint32 GPUIndex : FRHIGPUMask::All())
		{
			FD3D12Device* NodeDevice = Adapter->GetDevice(GPUIndex);
			DefaultShaderTables[GPUIndex].Init(SBTInitializer, NodeDevice);
			DefaultShaderTables[GPUIndex].SetRayGenIdentifiers(RayGenShaders.Identifiers);
			DefaultShaderTables[GPUIndex].SetMissIdentifiers(MissShaders.Identifiers);
			DefaultShaderTables[GPUIndex].SetDefaultHitGroupIdentifier(HitGroupShaders.Identifiers[0]);
		}

		TotalCreationTime += FPlatformTime::Cycles64();

		// Report stats for pipelines that take a long time to create

#if !NO_LOGGING
		const double TotalCreatimTimeMS = 1000.0 * FPlatformTime::ToSeconds64(TotalCreationTime);
		const float CreationTimeWarningThresholdMS = 10.0f;
		if (TotalCreatimTimeMS > CreationTimeWarningThresholdMS)
		{
			const double CompileTimeMS = 1000.0 * FPlatformTime::ToSeconds64(CompileTime);
			const double LinkTimeMS = 1000.0 * FPlatformTime::ToSeconds64(LinkTime);
			uint32 NumUniqueShaders = UniqueShaderCollections.Num();
			UE_LOG(LogD3D12RHI, Log,
				TEXT("Creating RTPSO with %d shaders (%d cached, %d new) took %.2f ms. Compile time %.2f ms, link time %.2f ms."),
				NumUniqueShaders, NumCacheHits, NumUniqueShaders - NumCacheHits, TotalCreatimTimeMS, CompileTimeMS, LinkTimeMS);
		}
#endif //!NO_LOGGING
	}

	FD3D12RayTracingShaderLibrary RayGenShaders;
	FD3D12RayTracingShaderLibrary MissShaders;
	FD3D12RayTracingShaderLibrary HitGroupShaders;
	FD3D12RayTracingShaderLibrary CallableShaders;

	// Shader table that can be used to dispatch ray tracing work that doesn't require real SBT bindings.
	// This is useful for the case where user only provides default RayGen, Miss and HitGroup shaders.
	FD3D12RayTracingShaderTable DefaultShaderTables[MAX_NUM_GPUS];

	ID3D12RootSignature* GlobalRootSignature = nullptr;

	TRefCountPtr<ID3D12StateObject> StateObject;
	TRefCountPtr<ID3D12StateObjectProperties> PipelineProperties;

	static constexpr uint32 ShaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	bool bAllowHitGroupIndexing = true;

	uint32 MaxLocalRootSignatureSize = 0;
	uint32 MaxHitGroupViewDescriptors = 0;
};

class FD3D12BasicRayTracingPipeline
{
public:

	UE_NONCOPYABLE(FD3D12BasicRayTracingPipeline)

	FD3D12BasicRayTracingPipeline(FD3D12Device* Device)
	{
		// Occlusion pipeline
		{
			FRayTracingPipelineStateInitializer OcclusionInitializer;

			FRHIRayTracingShader* OcclusionRGSTable[] = { GetBuildInRayTracingShader<FOcclusionMainRG>() };
			OcclusionInitializer.SetRayGenShaderTable(OcclusionRGSTable);

			FRHIRayTracingShader* OcclusionMSTable[] = { GetBuildInRayTracingShader<FDefaultMainMS>() };
			OcclusionInitializer.SetMissShaderTable(OcclusionMSTable);

			OcclusionInitializer.bAllowHitGroupIndexing = false;

			Occlusion = new FD3D12RayTracingPipelineState(Device, OcclusionInitializer);
		}

		// Intersection pipeline
		{
			FRayTracingPipelineStateInitializer IntersectionInitializer;

			FRHIRayTracingShader* IntersectionRGSTable[] = { GetBuildInRayTracingShader<FIntersectionMainRG>() };
			IntersectionInitializer.SetRayGenShaderTable(IntersectionRGSTable);

			FRHIRayTracingShader* IntersectionMSTable[] = { GetBuildInRayTracingShader<FDefaultMainMS>() };
			IntersectionInitializer.SetMissShaderTable(IntersectionMSTable);

			FRHIRayTracingShader* IntersectionHitTable[] = { GetBuildInRayTracingShader<FIntersectionMainCHS>() };
			IntersectionInitializer.SetHitGroupTable(IntersectionHitTable);

			IntersectionInitializer.bAllowHitGroupIndexing = false;

			Intersection = new FD3D12RayTracingPipelineState(Device, IntersectionInitializer);
		}
	}

	~FD3D12BasicRayTracingPipeline()
	{
		delete Intersection;
		delete Occlusion;
	}

	FD3D12RayTracingPipelineState* Occlusion;
	FD3D12RayTracingPipelineState* Intersection;
};

void FD3D12Device::InitRayTracing()
{
	check(RayTracingPipelineCache == nullptr);
	RayTracingPipelineCache = new FD3D12RayTracingPipelineCache(this);

	check(RayTracingDescriptorHeapCache == nullptr);
	RayTracingDescriptorHeapCache = new FD3D12RayTracingDescriptorHeapCache(this);

	check(BasicRayTracingPipeline == nullptr);
	BasicRayTracingPipeline = new FD3D12BasicRayTracingPipeline(this);
}

void FD3D12Device::CleanupRayTracing()
{
	delete BasicRayTracingPipeline;
	BasicRayTracingPipeline = nullptr;

	delete RayTracingPipelineCache;
	RayTracingPipelineCache = nullptr;

	// Note: RayTracingDescriptorHeapCache is destroyed in ~FD3D12Device, after all deferred deletion is processed
}

FRayTracingPipelineStateRHIRef FD3D12DynamicRHI::RHICreateRayTracingPipelineState(const FRayTracingPipelineStateInitializer& Initializer)
{
	FD3D12Device* Device = GetAdapter().GetDevice(0); // All pipelines are created on the first node, as they may be used on any other linked GPU.
	FD3D12RayTracingPipelineState* Result = new FD3D12RayTracingPipelineState(Device, Initializer);

	return Result;
}

FRayTracingGeometryRHIRef FD3D12DynamicRHI::RHICreateRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer)
{
	checkf(Initializer.PositionVertexBuffer, TEXT("Position vertex buffer is required for ray tracing geometry"));
	checkf(Initializer.VertexBufferStride, TEXT("Position vertex buffer is required for ray tracing geometry"));
	checkf(Initializer.VertexBufferStride % 4 == 0, TEXT("Position vertex buffer stride must be aligned to 4 bytes for ByteAddressBuffer loads to work"));

	if (Initializer.GeometryType == RTGT_Triangles)
	{
		// #dxr_todo UE-72160: VET_Half4 (DXGI_FORMAT_R16G16B16A16_FLOAT) is also supported by DXR. Should we support it?
		check(Initializer.VertexBufferElementType == VET_Float3 || Initializer.VertexBufferElementType == VET_Float2 || Initializer.VertexBufferElementType == VET_Half2);

		// #dxr_todo UE-72160: temporary constraints on vertex and index buffer formats (this will be relaxed when more flexible vertex/index fetching is implemented)
		checkf(Initializer.VertexBufferElementType == VET_Float3, TEXT("Only float3 vertex buffers are currently implemented.")); // #dxr_todo UE-72160: support other vertex buffer formats
		checkf(Initializer.VertexBufferStride == 12, TEXT("Only deinterleaved float3 position vertex buffers are currently implemented.")); // #dxr_todo UE-72160: support interleaved vertex buffers
	}

	if (Initializer.GeometryType == RTGT_Procedural)
	{
		checkf(Initializer.VertexBufferStride >= (2 * sizeof(FVector)), TEXT("Procedural geometry vertex buffer must contain at least 2xFloat3 that defines 3D bounding boxes of primitives."));
	}

	if (Initializer.IndexBuffer)
	{
		checkf(Initializer.IndexBuffer->GetStride() == 2 || Initializer.IndexBuffer->GetStride() == 4, TEXT("Index buffer must be 16 or 32 bit."));
	}

	FD3D12RayTracingGeometry* Geometry = new FD3D12RayTracingGeometry();

	Geometry->IndexStride = Initializer.IndexBuffer ? Initializer.IndexBuffer->GetStride() : 0; // stride 0 means implicit triangle list for non-indexed geometry
	Geometry->VertexOffsetInBytes = (Initializer.BaseVertexIndex * Initializer.VertexBufferStride) + Initializer.VertexBufferByteOffset;
	Geometry->VertexStrideInBytes = Initializer.VertexBufferStride;
	Geometry->BaseVertexIndex = Initializer.BaseVertexIndex;
	Geometry->TotalPrimitiveCount = Initializer.TotalPrimitiveCount;

	switch (Initializer.GeometryType)
	{
	case RTGT_Triangles:
		Geometry->GeometryType = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		break;
	case RTGT_Procedural:
		Geometry->GeometryType = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
		break;
	default:
		checkf(false, TEXT("Unexpected ray tracing geometry type"));
	}

	if (Initializer.bFastBuild)
	{
		Geometry->BuildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
	}
	else
	{
		Geometry->BuildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	}

	if (Initializer.bAllowUpdate)
	{
		Geometry->BuildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	}

	if (GRayTracingDebugForceBuildMode == 1)
	{
		Geometry->BuildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
		Geometry->BuildFlags &= ~D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	}
	else if (GRayTracingDebugForceBuildMode == 2)
	{
		Geometry->BuildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		Geometry->BuildFlags &= ~D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
	}

	if (Initializer.Segments.Num())
	{
		Geometry->Segments = TArray<FRayTracingGeometrySegment>(Initializer.Segments.GetData(), Initializer.Segments.Num());
	}
	else
	{
		FRayTracingGeometrySegment DefaultSegment;
		DefaultSegment.FirstPrimitive = 0;
		DefaultSegment.NumPrimitives = Initializer.TotalPrimitiveCount;
		Geometry->Segments.Add(DefaultSegment);
	}

#if DO_CHECK
	{
		uint32 ComputedPrimitiveCountForValidation = 0;
		for (const FRayTracingGeometrySegment& Segment : Geometry->Segments)
		{
			ComputedPrimitiveCountForValidation += Segment.NumPrimitives;
			check(Segment.FirstPrimitive + Segment.NumPrimitives <= Initializer.TotalPrimitiveCount);
		}
		check(ComputedPrimitiveCountForValidation == Initializer.TotalPrimitiveCount);
	}
#endif

	Geometry->VertexElemType = Initializer.VertexBufferElementType;

	Geometry->RHIIndexBuffer = Initializer.IndexBuffer;
	Geometry->RHIVertexBuffer = Initializer.PositionVertexBuffer;
	Geometry->SetDirty(FRHIGPUMask::All(), true);

	INC_DWORD_STAT_BY(STAT_D3D12RayTracingTrianglesBLAS, Initializer.TotalPrimitiveCount);

	return Geometry;
}

FRayTracingSceneRHIRef FD3D12DynamicRHI::RHICreateRayTracingScene(const FRayTracingSceneInitializer& Initializer)
{
	FD3D12Adapter& Adapter = GetAdapter();

	FD3D12RayTracingScene* Result = new FD3D12RayTracingScene(Adapter);

	checkf(Initializer.Lifetime == RTSL_SingleFrame, TEXT("Only single-frame ray tracing scenes are currently implemented."));

	Result->Lifetime = Initializer.Lifetime;
	Result->CreatedFrameFenceValue = Adapter.GetFrameFence().GetCurrentFence();
	Result->Instances = TArray<FRayTracingGeometryInstance>(Initializer.Instances.GetData(), Initializer.Instances.Num());
	Result->ShaderSlotsPerGeometrySegment = Initializer.ShaderSlotsPerGeometrySegment;

	// Compute geometry segment count prefix sum to be later used in GetHitRecordBaseIndex()
	Result->SegmentPrefixSum.Reserve(Result->Instances.Num());
	uint32 NumTotalSegments = 0;
	for (const FRayTracingGeometryInstance& Instance : Result->Instances)
	{
		FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(Instance.GeometryRHI);
		Result->SegmentPrefixSum.Add(NumTotalSegments);
		NumTotalSegments += Geometry->Segments.Num();
	}
	Result->NumTotalSegments = NumTotalSegments;

	Result->NumCallableShaderSlots = Initializer.NumCallableShaderSlots;

	return Result;
}

FD3D12RayTracingGeometry::FD3D12RayTracingGeometry()
{
	INC_DWORD_STAT(STAT_D3D12RayTracingAllocatedBLAS);
}

FD3D12RayTracingGeometry::~FD3D12RayTracingGeometry()
{
	for (TRefCountPtr<FD3D12MemBuffer>& Buffer : AccelerationStructureBuffers)
	{
		if (Buffer)
		{
			DEC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, Buffer->GetSize());
			DEC_MEMORY_STAT_BY(STAT_D3D12RayTracingBLASMemory, Buffer->GetSize());
		}
	}

	for (TRefCountPtr<FD3D12MemBuffer>& Buffer : ScratchBuffers)
	{
		if (Buffer)
		{
			DEC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, Buffer->GetSize());
			DEC_MEMORY_STAT_BY(STAT_D3D12RayTracingBLASMemory, Buffer->GetSize());
		}
	}

	DEC_DWORD_STAT_BY(STAT_D3D12RayTracingTrianglesBLAS, TotalPrimitiveCount);
	DEC_DWORD_STAT(STAT_D3D12RayTracingAllocatedBLAS);
}

void FD3D12RayTracingGeometry::TransitionBuffers(FD3D12CommandContext& CommandContext)
{
	// Transition vertex and index resources..
	if (RHIIndexBuffer)
	{
		FD3D12IndexBuffer* IndexBuffer = CommandContext.RetrieveObject<FD3D12IndexBuffer>(RHIIndexBuffer.GetReference());
		if (IndexBuffer->GetResource()->RequiresResourceStateTracking())
		{
			FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, IndexBuffer->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);
		}
	}

	FD3D12VertexBuffer* VertexBuffer = CommandContext.RetrieveObject<FD3D12VertexBuffer>(RHIVertexBuffer.GetReference());
	if (VertexBuffer->GetResource()->RequiresResourceStateTracking())
	{
		FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, VertexBuffer->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);
	}
}

static void CreateAccelerationStructureBuffers(TRefCountPtr<FD3D12MemBuffer>& AccelerationStructureBuffer, TRefCountPtr<FD3D12MemBuffer>&  ScratchBuffer, FD3D12Adapter* Adapter, uint32 GPUIndex, const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& PrebuildInfo)
{
	check(IsInRHIThread() || !IsRunningRHIInSeparateThread());

	FRHIResourceCreateInfo CreateInfo;

	D3D12_RESOURCE_DESC AccelerationStructureBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
		PrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	CreateInfo.DebugName = TEXT("AccelerationStructureBuffer");
	AccelerationStructureBuffer = Adapter->CreateRHIBuffer<FD3D12MemBuffer>(
		nullptr, AccelerationStructureBufferDesc, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT,
		0, AccelerationStructureBufferDesc.Width, BUF_AccelerationStructure, CreateInfo, FRHIGPUMask::FromIndex(GPUIndex));

	SetName(AccelerationStructureBuffer->GetResource(), TEXT("Acceleration structure"));

	// #dxr_todo UE-72161: scratch buffers can be pooled and reused for different scenes and geometries
	D3D12_RESOURCE_DESC ScratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
		FMath::Max(PrebuildInfo.UpdateScratchDataSizeInBytes, PrebuildInfo.ScratchDataSizeInBytes), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	CreateInfo.DebugName = TEXT("ScratchBuffer");
	ScratchBuffer = Adapter->CreateRHIBuffer<FD3D12MemBuffer>(
		nullptr, ScratchBufferDesc, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT,
		0, ScratchBufferDesc.Width, BUF_UnorderedAccess, CreateInfo, FRHIGPUMask::FromIndex(GPUIndex));

	SetName(ScratchBuffer->GetResource(), TEXT("Acceleration structure scratch"));
}

void FD3D12RayTracingGeometry::BuildAccelerationStructure(FD3D12CommandContext& CommandContext, bool bIsUpdate)
{
	static constexpr uint32 IndicesPerPrimitive = 3; // Only triangle meshes are supported

	// Array of geometry descriptions, one per segment (single-segment geometry is a common case).
	TArray<D3D12_RAYTRACING_GEOMETRY_DESC, TInlineAllocator<1>> Descs;

	Descs.Reserve(Segments.Num());

	FD3D12IndexBuffer* IndexBuffer = CommandContext.RetrieveObject<FD3D12IndexBuffer>(RHIIndexBuffer.GetReference());
	FD3D12VertexBuffer* VertexBuffer = CommandContext.RetrieveObject<FD3D12VertexBuffer>(RHIVertexBuffer.GetReference());

	for (const FRayTracingGeometrySegment& Segment : Segments)
	{
		D3D12_RAYTRACING_GEOMETRY_DESC Desc = {};

		Desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
		Desc.Type = GeometryType;

		if (!Segment.bAllowAnyHitShader)
		{
			// Deny anyhit shader invocations when this segment is hit
			Desc.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		}

		if (!Segment.bAllowDuplicateAnyHitShaderInvocation)
		{
			// Allow only a single any-hit shader invocation per primitive
			Desc.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
		}

		if (GeometryType == D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES)
		{
			switch (VertexElemType)
			{
			case VET_Float3:
				Desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
				break;
			case VET_Float2:
				Desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32_FLOAT;
				break;
			case VET_Half2:
				Desc.Triangles.VertexFormat = DXGI_FORMAT_R16G16_FLOAT;
				break;
			default:
				checkNoEntry();
				break;
			}

			Desc.Triangles.Transform3x4 = D3D12_GPU_VIRTUAL_ADDRESS(0);

			if (IndexBuffer)
			{
				Desc.Triangles.IndexFormat = IndexStride == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
				Desc.Triangles.IndexCount = Segment.NumPrimitives * IndicesPerPrimitive;
				Desc.Triangles.IndexBuffer = IndexBuffer->ResourceLocation.GetGPUVirtualAddress() + IndexStride * Segment.FirstPrimitive * IndicesPerPrimitive;

				Desc.Triangles.VertexCount = VertexBuffer->ResourceLocation.GetSize() / VertexStrideInBytes;

				IndexBuffer->GetResource()->UpdateResidency(CommandContext.CommandListHandle);
			}
			else
			{
				// Non-indexed geometry
				Desc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
				Desc.Triangles.IndexCount = 0;
				Desc.Triangles.IndexBuffer = D3D12_GPU_VIRTUAL_ADDRESS(0);

				checkf(Segments.Num() == 1, TEXT("Non-indexed geometry with multiple segments is not implemented."));

				Desc.Triangles.VertexCount = FMath::Min<uint32>(VertexBuffer->ResourceLocation.GetSize() / VertexStrideInBytes, TotalPrimitiveCount * 3);
			}

			Desc.Triangles.VertexBuffer.StartAddress = VertexBuffer->ResourceLocation.GetGPUVirtualAddress() + VertexOffsetInBytes;
			Desc.Triangles.VertexBuffer.StrideInBytes = VertexStrideInBytes;
		}
		else if (GeometryType == D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS)
		{
			Desc.AABBs.AABBCount = Segment.NumPrimitives;
			Desc.AABBs.AABBs.StartAddress = VertexBuffer->ResourceLocation.GetGPUVirtualAddress() + VertexOffsetInBytes;
			Desc.AABBs.AABBs.StrideInBytes = VertexStrideInBytes;
		}
		else
		{
			checkf(false, TEXT("Unexpected ray tracing geometry type"));
		}

		VertexBuffer->ResourceLocation.GetResource()->UpdateResidency(CommandContext.CommandListHandle);

		Descs.Add(Desc);
	}

	const uint32 GPUIndex = CommandContext.GetGPUIndex();
	FD3D12Adapter* Adapter = CommandContext.GetParentAdapter();

	ID3D12Device5* RayTracingDevice = CommandContext.GetParentDevice()->GetRayTracingDevice();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS LocalBuildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS(BuildFlags);

	if (bIsUpdate)
	{
		checkf(BuildFlags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE,
			TEXT("Acceleration structure must be created with FRayTracingGeometryInitializer::bAllowUpdate=true to perform refit / update."));

		LocalBuildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS PrebuildDescInputs = {};

	PrebuildDescInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	PrebuildDescInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	PrebuildDescInputs.NumDescs = Descs.Num();
	PrebuildDescInputs.pGeometryDescs = Descs.GetData();
	PrebuildDescInputs.Flags = LocalBuildFlags;

	if (!AccelerationStructureBuffers[GPUIndex])
	{
		check(!bIsUpdate);

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO PrebuildInfo = {};

		RayTracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&PrebuildDescInputs, &PrebuildInfo);

		CreateAccelerationStructureBuffers(AccelerationStructureBuffers[GPUIndex], ScratchBuffers[GPUIndex], Adapter, GPUIndex, PrebuildInfo);

		INC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, AccelerationStructureBuffers[GPUIndex]->GetSize());
		INC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, ScratchBuffers[GPUIndex]->GetSize());

		INC_MEMORY_STAT_BY(STAT_D3D12RayTracingBLASMemory, AccelerationStructureBuffers[GPUIndex]->GetSize());
		INC_MEMORY_STAT_BY(STAT_D3D12RayTracingBLASMemory, ScratchBuffers[GPUIndex]->GetSize());

		// #dxr_todo UE-72161: scratch buffers should be created in UAV state from the start
		FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, ScratchBuffers[GPUIndex].GetReference()->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0);
	}

	TransitionBuffers(CommandContext);
	CommandContext.CommandListHandle.FlushResourceBarriers();

	if (IsDirty(GPUIndex))
	{
		ScratchBuffers[GPUIndex]->GetResource()->UpdateResidency(CommandContext.CommandListHandle);
		AccelerationStructureBuffers[GPUIndex]->GetResource()->UpdateResidency(CommandContext.CommandListHandle);

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC BuildDesc = {};
		BuildDesc.Inputs = PrebuildDescInputs;
		BuildDesc.DestAccelerationStructureData = AccelerationStructureBuffers[GPUIndex]->ResourceLocation.GetGPUVirtualAddress();
		BuildDesc.ScratchAccelerationStructureData = ScratchBuffers[GPUIndex]->ResourceLocation.GetGPUVirtualAddress();
		BuildDesc.SourceAccelerationStructureData = bIsUpdate
			? AccelerationStructureBuffers[GPUIndex]->ResourceLocation.GetGPUVirtualAddress()
			: D3D12_GPU_VIRTUAL_ADDRESS(0);

		ID3D12GraphicsCommandList4* RayTracingCommandList = CommandContext.CommandListHandle.RayTracingCommandList();
		RayTracingCommandList->BuildRaytracingAccelerationStructure(&BuildDesc, 0, nullptr);
		SetDirty(CommandContext.GetGPUMask(), false);

		if (bIsUpdate)
		{
			INC_DWORD_STAT(STAT_D3D12RayTracingUpdatedBLAS);
		}
		else
		{
			INC_DWORD_STAT(STAT_D3D12RayTracingBuiltBLAS);
		}
	}

	// We don't need to keep a scratch buffer after initial build if acceleration structure is static.
	if (!(BuildFlags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE))
	{
		DEC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, ScratchBuffers[GPUIndex]->GetSize());
		DEC_MEMORY_STAT_BY(STAT_D3D12RayTracingBLASMemory, ScratchBuffers[GPUIndex]->GetSize());
		ScratchBuffers[GPUIndex] = nullptr;
	}
}

FD3D12RayTracingScene::FD3D12RayTracingScene(FD3D12Adapter& Adapter)
{
	ShaderResourceView = Adapter.CreateLinkedObject<FD3D12ShaderResourceView>(FRHIGPUMask::All(), [&](FD3D12Device* Device)
	{
		return new FD3D12ShaderResourceView(Device);
	});

	INC_DWORD_STAT(STAT_D3D12RayTracingAllocatedTLAS);
};

FD3D12RayTracingScene::~FD3D12RayTracingScene()
{
	for (auto& Table : ShaderTables)
	{
		for (auto Item : Table)
		{
			delete Item.Value;
		}
	}

	for (auto& AccelerationStructureBuffer : AccelerationStructureBuffers)
	{
		if (AccelerationStructureBuffer)
		{
			DEC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, AccelerationStructureBuffer->GetSize());
			DEC_MEMORY_STAT_BY(STAT_D3D12RayTracingTLASMemory, AccelerationStructureBuffer->GetSize());
		}
	}
	

	DEC_DWORD_STAT(STAT_D3D12RayTracingAllocatedTLAS);
}

void FD3D12RayTracingScene::BuildAccelerationStructure(FD3D12CommandContext& CommandContext, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlags)
{
	TRefCountPtr<FD3D12MemBuffer> InstanceBuffer;
	TRefCountPtr<FD3D12MemBuffer> ScratchBuffer;

	const uint32 GPUIndex = CommandContext.GetGPUIndex();
	FD3D12Adapter* Adapter = CommandContext.GetParentAdapter();
	ID3D12Device5* RayTracingDevice = CommandContext.GetParentDevice()->GetRayTracingDevice();

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS PrebuildDescInputs = {};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO PrebuildInfo = {};
	PrebuildDescInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	PrebuildDescInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	PrebuildDescInputs.NumDescs = Instances.Num();
	PrebuildDescInputs.Flags = BuildFlags;

	RayTracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&PrebuildDescInputs, &PrebuildInfo);

	TRefCountPtr<FD3D12MemBuffer>& AccelerationStructureBuffer = AccelerationStructureBuffers[GPUIndex];

	CreateAccelerationStructureBuffers(AccelerationStructureBuffer, ScratchBuffer, Adapter, GPUIndex, PrebuildInfo);

	INC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, AccelerationStructureBuffer->GetSize());
	INC_MEMORY_STAT_BY(STAT_D3D12RayTracingTLASMemory, AccelerationStructureBuffer->GetSize());

	// #dxr_todo UE-72161: scratch buffers should be created in UAV state from the start
	FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, ScratchBuffer.GetReference()->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0);

	FD3D12ShaderResourceView* AccelerationStructureView = CommandContext.RetrieveObject<FD3D12ShaderResourceView>(ShaderResourceView.GetReference());

	if (bAccelerationStructureViewInitialized[GPUIndex])
	{
		check(AccelerationStructureView->GetParentDevice() == AccelerationStructureBuffer->GetParentDevice());
		AccelerationStructureView->Rename(AccelerationStructureBuffer->ResourceLocation);
	}
	else
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.RaytracingAccelerationStructure.Location = AccelerationStructureBuffer->ResourceLocation.GetGPUVirtualAddress();

		check(AccelerationStructureView->GetParentDevice() == AccelerationStructureBuffer->GetParentDevice());
		AccelerationStructureView->Initialize(SRVDesc, AccelerationStructureBuffer->ResourceLocation, 4);

		bAccelerationStructureViewInitialized[GPUIndex] = true;
	}

	// Create and fill instance buffer

	TotalPrimitiveCount = 0;

	if (Instances.Num())
	{
		FRHIResourceCreateInfo CreateInfo;
		D3D12_RESOURCE_DESC InstanceBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
			sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * Instances.Num(),
			D3D12_RESOURCE_FLAG_NONE, D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT);

		// Create a temporary (volatile) buffer to hold instance data that we're about to upload.
		// The buffer does not need to persist for longer than one frame and can be discarded immediately
		// after the top level acceleration structure build is complete.
		InstanceBuffer = Adapter->CreateRHIBuffer<FD3D12MemBuffer>(
			nullptr, InstanceBufferDesc, D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT,
			0, InstanceBufferDesc.Width, BUF_Volatile, CreateInfo,
			FRHIGPUMask::FromIndex(GPUIndex));

		D3D12_RAYTRACING_INSTANCE_DESC* MappedData = (D3D12_RAYTRACING_INSTANCE_DESC*)Adapter->GetOwningRHI()->LockBuffer(
			nullptr, InstanceBuffer.GetReference(), 0, InstanceBufferDesc.Width, RLM_WriteOnly);

		check(MappedData);

		uint32 InstanceIndex = 0;

		TSet<FD3D12RayTracingGeometry*> UniqueGeometries;

		for (const FRayTracingGeometryInstance& Instance : Instances)
		{
			FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(Instance.GeometryRHI);

			checkf(!Geometry->IsDirty(CommandContext.GetGPUIndex()),
				TEXT("Acceleration structures for all geometries must be built before building the top level acceleration structure for the scene."));

			D3D12_RAYTRACING_INSTANCE_DESC InstanceDesc = {};

			FMatrix TransformTransposed = Instance.Transform.GetTransposed();

			// Ensure the last row of the original Transform is <0,0,0,1>
			check((TransformTransposed.M[3][0] == 0)
				&& (TransformTransposed.M[3][1] == 0)
				&& (TransformTransposed.M[3][2] == 0)
				&& (TransformTransposed.M[3][3] == 1));

			FMemory::Memcpy(&InstanceDesc.Transform, &TransformTransposed.M[0][0], sizeof(InstanceDesc.Transform));

			InstanceDesc.InstanceID = Instance.UserData;
			InstanceDesc.InstanceMask = Instance.Mask;
			InstanceDesc.InstanceContributionToHitGroupIndex = SegmentPrefixSum[InstanceIndex] * ShaderSlotsPerGeometrySegment;
			InstanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE; // #dxr_todo: convert cull mode based on instance mirroring or double-sidedness
			
			if (Instance.bForceOpaque || GRayTracingDebugForceOpaque)
			{
				InstanceDesc.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
			}

			if (Instance.bDoubleSided || GRayTracingDebugDisableTriangleCull)
			{
				InstanceDesc.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
			}

			InstanceDesc.AccelerationStructure = Geometry->AccelerationStructureBuffers[GPUIndex]->ResourceLocation.GetGPUVirtualAddress();
			Geometry->AccelerationStructureBuffers[GPUIndex]->ResourceLocation.GetResource()->UpdateResidency(CommandContext.CommandListHandle);

			bool bAlreadyInSet = false;
			UniqueGeometries.Add(Geometry, &bAlreadyInSet);
			if (!bAlreadyInSet)
			{
				BottomLevelAccelerationStructureBuffers[GPUIndex].Add(Geometry->AccelerationStructureBuffers[GPUIndex]);
			}

			MappedData[InstanceIndex] = InstanceDesc;
			++InstanceIndex;

			TotalPrimitiveCount += Geometry->TotalPrimitiveCount;
		}

		Adapter->GetOwningRHI()->UnlockBuffer(nullptr, InstanceBuffer.GetReference());

		InstanceBuffer->GetResource()->UpdateResidency(CommandContext.CommandListHandle);
	}

	// Build the actual acceleration structure

	const bool bIsUpdateMode = false; // Top level acceleration structure is always built from scratch

	AccelerationStructureBuffer->GetResource()->UpdateResidency(CommandContext.CommandListHandle);
	ScratchBuffer->GetResource()->UpdateResidency(CommandContext.CommandListHandle);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC BuildDesc = {};
	BuildDesc.Inputs = PrebuildDescInputs;
	BuildDesc.Inputs.InstanceDescs = InstanceBuffer ? InstanceBuffer->ResourceLocation.GetGPUVirtualAddress() : D3D12_GPU_VIRTUAL_ADDRESS(0);
	BuildDesc.DestAccelerationStructureData = AccelerationStructureBuffer->ResourceLocation.GetGPUVirtualAddress();
	BuildDesc.ScratchAccelerationStructureData = ScratchBuffer->ResourceLocation.GetGPUVirtualAddress();
	BuildDesc.SourceAccelerationStructureData = bIsUpdateMode
		? AccelerationStructureBuffer->ResourceLocation.GetGPUVirtualAddress()
		: D3D12_GPU_VIRTUAL_ADDRESS(0);

	// UAV barrier is used here to ensure that all bottom level acceleration structures are built
	CommandContext.CommandListHandle.AddUAVBarrier();
	CommandContext.CommandListHandle.FlushResourceBarriers();

	ID3D12GraphicsCommandList4* RayTracingCommandList = CommandContext.CommandListHandle.RayTracingCommandList();
	RayTracingCommandList->BuildRaytracingAccelerationStructure(&BuildDesc, 0, nullptr);

	if (bIsUpdateMode)
	{
		INC_DWORD_STAT(STAT_D3D12RayTracingUpdatedTLAS);
	}
	else
	{
		INC_DWORD_STAT(STAT_D3D12RayTracingBuiltTLAS);
	}

	// UAV barrier is used here to ensure that the acceleration structure build is complete before any rays are traced
	// #dxr_todo: these barriers should ideally be inserted by the high level code to allow more overlapped execution
	CommandContext.CommandListHandle.AddUAVBarrier();
}

void FD3D12RayTracingScene::UpdateResidency(FD3D12CommandContext& CommandContext)
{
	const uint32 GPUIndex = CommandContext.GetGPUIndex();
	AccelerationStructureBuffers[GPUIndex]->GetResource()->UpdateResidency(CommandContext.CommandListHandle);

	for (auto& BLASBuffer : BottomLevelAccelerationStructureBuffers[GPUIndex])
	{
		BLASBuffer->GetResource()->UpdateResidency(CommandContext.CommandListHandle);
	}
}

FD3D12RayTracingShaderTable* FD3D12RayTracingScene::FindExistingShaderTable(const FD3D12RayTracingPipelineState* Pipeline, FD3D12Device* Device) const
{
	const uint32 GPUIndex = Device->GetGPUIndex();
	FD3D12RayTracingShaderTable* const* FoundShaderTable = ShaderTables[GPUIndex].Find(Pipeline);
	if (FoundShaderTable)
	{
		return *FoundShaderTable;
	}
	else
	{
		return nullptr;
	}
}

FD3D12RayTracingShaderTable* FD3D12RayTracingScene::FindOrCreateShaderTable(const FD3D12RayTracingPipelineState* Pipeline, FD3D12Device* Device)
{
	FD3D12RayTracingShaderTable* FoundShaderTable = FindExistingShaderTable(Pipeline, Device);
	if (FoundShaderTable)
	{
		return FoundShaderTable;
	}

	FD3D12RayTracingShaderTable* CreatedShaderTable = new FD3D12RayTracingShaderTable();
	ID3D12Device5* RayTracingDevice = Device->GetRayTracingDevice();

	const uint32 GPUIndex = Device->GetGPUIndex();

	const uint32 NumHitGroupSlots = Pipeline->bAllowHitGroupIndexing ? NumTotalSegments * ShaderSlotsPerGeometrySegment : 0;

	checkf(Pipeline->MaxLocalRootSignatureSize >= sizeof(FHitGroupSystemParameters), TEXT("All local root signatures are expected to contain ray tracing system root parameters (2x root buffers + 4x root DWORD)"));

	FD3D12RayTracingShaderTable::FInitializer SBTInitializer = {};
	SBTInitializer.NumRayGenShaders = Pipeline->RayGenShaders.Identifiers.Num();
	SBTInitializer.NumMissShaders = Pipeline->MissShaders.Identifiers.Num();
	SBTInitializer.NumHitRecords = NumHitGroupSlots;
	SBTInitializer.NumCallableRecords = NumCallableShaderSlots;
	SBTInitializer.LocalRootDataSize = Pipeline->MaxLocalRootSignatureSize;
	SBTInitializer.MaxViewDescriptorsPerRecord = Pipeline->MaxHitGroupViewDescriptors;

	CreatedShaderTable->Init(SBTInitializer, Device);
	CreatedShaderTable->SetRayGenIdentifiers(Pipeline->RayGenShaders.Identifiers);
	CreatedShaderTable->SetMissIdentifiers(Pipeline->MissShaders.Identifiers);
	CreatedShaderTable->SetDefaultHitGroupIdentifier(Pipeline->HitGroupShaders.Identifiers[0]);

	// Bind index/vertex buffers and fetch parameters to all SBT entries (all segments of all mesh instances)
	// Resource binding is skipped for pipelines that don't use SBT indexing. Such pipelines use the same CHS for all primitives, which can't access any local resources.
	if (NumHitGroupSlots)
	{
		checkf(CreatedShaderTable->LocalShaderTableOffset == CreatedShaderTable->HitGroupShaderTableOffset,
			TEXT("Hit shader records are assumed to be at the beginning of local shader table"));

		const uint32 NumInstances = Instances.Num();
		for (uint32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
		{
			const FRayTracingGeometryInstance& Instance = Instances[InstanceIndex];

			const FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(Instance.GeometryRHI);

			static constexpr uint32 IndicesPerPrimitive = 3; // Only triangle meshes are supported

			const uint32 IndexStride = Geometry->IndexStride;

			FD3D12IndexBuffer* IndexBuffer = FD3D12DynamicRHI::ResourceCast(Geometry->RHIIndexBuffer.GetReference(), GPUIndex);
			FD3D12VertexBuffer* VertexBuffer = FD3D12DynamicRHI::ResourceCast(Geometry->RHIVertexBuffer.GetReference(), GPUIndex);

			const D3D12_GPU_VIRTUAL_ADDRESS IndexBufferAddress = IndexBuffer ? IndexBuffer->ResourceLocation.GetGPUVirtualAddress() : 0;
			const D3D12_GPU_VIRTUAL_ADDRESS VertexBufferAddress = VertexBuffer->ResourceLocation.GetGPUVirtualAddress() + Geometry->VertexOffsetInBytes;

			if (IndexBuffer)
			{
				CreatedShaderTable->AddResourceReference(IndexBuffer->ResourceLocation.GetResource(), IndexBuffer);
			}

			CreatedShaderTable->AddResourceReference(VertexBuffer->ResourceLocation.GetResource(), VertexBuffer);

			const uint32 NumSegments = Geometry->Segments.Num();
			for (uint32 SegmentIndex = 0; SegmentIndex < NumSegments; ++SegmentIndex)
			{
				const FRayTracingGeometrySegment& Segment = Geometry->Segments[SegmentIndex];

				const uint32 RecordBaseIndex = GetHitRecordBaseIndex(InstanceIndex, SegmentIndex);

				FHitGroupSystemParameters SystemParameters = {};
				SystemParameters.IndexBuffer = IndexBufferAddress;
				SystemParameters.VertexBuffer = VertexBufferAddress;

				if (Geometry->GeometryType == D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES)
				{
					// #dxr_todo UE-72160: support various vertex buffer layouts (fetch/decode based on vertex stride and format)
					checkf(Geometry->VertexElemType == VET_Float3, TEXT("Only VET_Float3 is currently implemented and tested. Other formats will be supported in the future."));
				}

				SystemParameters.RootConstants.SetVertexAndIndexStride(Geometry->VertexStrideInBytes, IndexStride);
				SystemParameters.RootConstants.IndexBufferOffsetInBytes = IndexStride * Segment.FirstPrimitive * IndicesPerPrimitive;

				for (uint32 SlotIndex = 0; SlotIndex < ShaderSlotsPerGeometrySegment; ++SlotIndex)
				{
					CreatedShaderTable->SetLocalShaderParameters(RecordBaseIndex + SlotIndex, 0, SystemParameters);
				}
			}
		}
	}

	ShaderTables[GPUIndex].Add(Pipeline, CreatedShaderTable);

	return CreatedShaderTable;
}

void FD3D12CommandContext::RHIBuildAccelerationStructure(FRHIRayTracingGeometry* InGeometry)
{
	FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(InGeometry);
	Geometry->TransitionBuffers(*this);
	CommandListHandle.FlushResourceBarriers();

	const bool bIsUpdate = false;
	Geometry->BuildAccelerationStructure(*this, bIsUpdate);
}

void FD3D12CommandContext::RHIUpdateAccelerationStructures(const TArrayView<const FAccelerationStructureUpdateParams> Params)
{
	// First batch up all barriers
	for (const FAccelerationStructureUpdateParams P : Params)
	{
		FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(P.Geometry.GetReference());
		Geometry->RHIVertexBuffer = P.VertexBuffer;
		Geometry->TransitionBuffers(*this);
	}
	CommandListHandle.FlushResourceBarriers();

	// Then do all work
	for (const FAccelerationStructureUpdateParams P : Params)
	{
		FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(P.Geometry.GetReference());
		Geometry->SetDirty(GetGPUMask(), true);

		const bool bIsUpdate = !GRayTracingDebugForceFullBuild;
		Geometry->BuildAccelerationStructure(*this, bIsUpdate);
	}
}

void FD3D12CommandContext::RHIBuildAccelerationStructures(const TArrayView<const FAccelerationStructureUpdateParams> Params)
{
	// First batch up all barriers
	for (const FAccelerationStructureUpdateParams P : Params)
	{
		FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(P.Geometry.GetReference());
		Geometry->RHIVertexBuffer = P.VertexBuffer;
		Geometry->TransitionBuffers(*this);
	}
	CommandListHandle.FlushResourceBarriers();

	// Then do all work
	for (const FAccelerationStructureUpdateParams P : Params)
	{
		FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(P.Geometry.GetReference());
		Geometry->SetDirty(GetGPUMask(), true);

		const bool bIsUpdate = false;
		Geometry->BuildAccelerationStructure(*this, bIsUpdate);
	}
}

void FD3D12CommandContext::RHIBuildAccelerationStructure(FRHIRayTracingScene* InScene)
{
	FD3D12RayTracingScene* Scene = FD3D12DynamicRHI::ResourceCast(InScene);
	Scene->BuildAccelerationStructure(*this, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE);
}

void FD3D12CommandContext::RHIClearRayTracingBindings(FRHIRayTracingScene* InScene)
{
	FD3D12RayTracingScene* Scene = FD3D12DynamicRHI::ResourceCast(InScene);

	auto& Table = Scene->ShaderTables[GetGPUIndex()];
	for (auto Item : Table)
	{
		delete Item.Value;
	}
	Table.Reset();
}

struct FD3D12RayTracingGlobalResourceBinder
{
	FD3D12RayTracingGlobalResourceBinder(FD3D12CommandContext& InCommandContext)
		: CommandContext(InCommandContext)
	{
	}

	void SetRootCBV(uint32 BaseSlotIndex, uint32 DescriptorIndex, D3D12_GPU_VIRTUAL_ADDRESS Address)
	{
		CommandContext.CommandListHandle->SetComputeRootConstantBufferView(BaseSlotIndex + DescriptorIndex, Address);
	}

	void SetRootSRV(uint32 BaseSlotIndex, uint32 DescriptorIndex, D3D12_GPU_VIRTUAL_ADDRESS Address)
	{
		CommandContext.CommandListHandle->SetComputeRootShaderResourceView(BaseSlotIndex + DescriptorIndex, Address);
	}

	void SetRootDescriptorTable(uint32 SlotIndex, D3D12_GPU_DESCRIPTOR_HANDLE DescriptorTable)
	{
		CommandContext.CommandListHandle->SetComputeRootDescriptorTable(SlotIndex, DescriptorTable);
	}

	D3D12_GPU_VIRTUAL_ADDRESS CreateTransientConstantBuffer(const void* Data, uint32 DataSize)
	{
		checkf(0, TEXT("Loose parameters and transient constant buffers are not implemented for global ray tracing shaders (raygen, miss, callable)"));
		return (D3D12_GPU_VIRTUAL_ADDRESS)0;
	}

	void AddResourceReference(FD3D12Resource* D3D12Resource, FRHIResource* RHIResource)
	{
		D3D12Resource->UpdateResidency(CommandContext.CommandListHandle);
	}

	FD3D12CommandContext& CommandContext;
};

struct FD3D12RayTracingLocalResourceBinder
{
	FD3D12RayTracingLocalResourceBinder(FD3D12CommandContext& InCommandContext, FD3D12RayTracingShaderTable* InShaderTable, const FD3D12RootSignature* InRootSignature, uint32 InRecordIndex)
		: Device(InCommandContext.GetParentDevice())
		, ShaderTable(InShaderTable)
		, RootSignature(InRootSignature)
		, RecordIndex(InRecordIndex)
	{
		check(RecordIndex != ~0u);
	}

	void SetRootDescriptor(uint32 BaseSlotIndex, uint32 DescriptorIndex, D3D12_GPU_VIRTUAL_ADDRESS Address)
	{
		const uint32 BindOffsetBase = RootSignature->GetBindSlotOffsetInBytes(BaseSlotIndex);
		const uint32 DescriptorSize = uint32(sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
		const uint32 CurrentOffset = BindOffsetBase + DescriptorIndex * DescriptorSize;
		ShaderTable->SetLocalShaderParameters(RecordIndex, CurrentOffset, Address);
	}

	void SetRootCBV(uint32 BaseSlotIndex, uint32 DescriptorIndex, D3D12_GPU_VIRTUAL_ADDRESS Address)
	{
		SetRootDescriptor(BaseSlotIndex, DescriptorIndex, Address);
	}

	void SetRootSRV(uint32 BaseSlotIndex, uint32 DescriptorIndex, D3D12_GPU_VIRTUAL_ADDRESS Address)
	{
		SetRootDescriptor(BaseSlotIndex, DescriptorIndex, Address);
	}

	void SetRootDescriptorTable(uint32 SlotIndex, D3D12_GPU_DESCRIPTOR_HANDLE DescriptorTable)
	{
		const uint32 BindOffset = RootSignature->GetBindSlotOffsetInBytes(SlotIndex);
		ShaderTable->SetLocalShaderParameters(RecordIndex, BindOffset, DescriptorTable);
	}

	D3D12_GPU_VIRTUAL_ADDRESS CreateTransientConstantBuffer(const void* Data, uint32 DataSize)
	{
		// If we see a significant number of transient allocations coming through this path, we should consider
		// caching constant buffer blocks inside ShaderTable and linearly sub-allocate from them.
		// If the amount of data is relatively small, it may also be possible to use root constants and avoid extra allocations entirely.

		FD3D12FastConstantAllocator& Allocator = Device->GetParentAdapter()->GetTransientUniformBufferAllocator();
		FD3D12ResourceLocation ResourceLocation(Device);
		void* MappedData = Allocator.Allocate(DataSize, ResourceLocation);

		FMemory::Memcpy(MappedData, Data, DataSize);

		ShaderTable->AddResourceReference(ResourceLocation.GetResource(), nullptr);

		return ResourceLocation.GetGPUVirtualAddress();
	}

	void AddResourceReference(FD3D12Resource* D3D12Resource, FRHIResource* RHIResource)
	{
		ShaderTable->AddResourceReference(D3D12Resource, RHIResource);
	}

	FD3D12Device* Device = nullptr;
	FD3D12RayTracingShaderTable* ShaderTable = nullptr;
	const FD3D12RootSignature* RootSignature = nullptr;
	uint32 RecordIndex = ~0u;
};

template <typename ResourceBinderType>
static void SetRayTracingShaderResources(
	FD3D12CommandContext& CommandContext,
	const FD3D12RayTracingShader* Shader,
	uint32 InNumTextures, FRHITexture* const* Textures,
	uint32 InNumSRVs, FRHIShaderResourceView* const* SRVs,
	uint32 InNumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
	uint32 InNumSamplers, FRHISamplerState* const* Samplers,
	uint32 InNumUAVs, FRHIUnorderedAccessView* const* UAVs,
	uint32 InLooseParameterDataSize, const void* InLooseParameterData,
	FD3D12RayTracingDescriptorCache& DescriptorCache,
	ResourceBinderType& Binder)
{
	ID3D12Device* Device = CommandContext.GetParentDevice()->GetDevice();

	const FD3D12RootSignature* RootSignature = Shader->pRootSignature;

	D3D12_GPU_VIRTUAL_ADDRESS   LocalCBVs[MAX_CBS];
	D3D12_CPU_DESCRIPTOR_HANDLE LocalSRVs[MAX_SRVS];
	D3D12_CPU_DESCRIPTOR_HANDLE LocalUAVs[MAX_UAVS];
	D3D12_CPU_DESCRIPTOR_HANDLE LocalSamplers[MAX_SAMPLERS];

	struct FResourceEntry
	{
		FD3D12Resource* D3D12Resource = nullptr;
		FRHIResource*   RHIResource   = nullptr;
	};
	TArray<FResourceEntry, TInlineAllocator<MAX_CBS + MAX_SRVS + MAX_UAVS>> ReferencedResources;

	uint64 BoundSRVMask = 0;
	uint64 BoundCBVMask = 0;
	uint64 BoundUAVMask = 0;
	uint64 BoundSamplerMask = 0;

	for (uint32 SRVIndex = 0; SRVIndex < InNumTextures; ++SRVIndex)
	{
		FRHITexture* Resource = Textures[SRVIndex];
		if (Resource)
		{
			FD3D12TextureBase* Texture = CommandContext.RetrieveTextureBase(Resource);
			LocalSRVs[SRVIndex] = Texture->GetShaderResourceView()->GetView();
			BoundSRVMask |= 1ull << SRVIndex;

			ReferencedResources.Add({ Texture->GetResource(), Resource });
		}
	}

	for (uint32 SRVIndex = 0; SRVIndex < InNumSRVs; ++SRVIndex)
	{
		FRHIShaderResourceView* Resource = SRVs[SRVIndex];
		if (Resource)
		{
			FD3D12ShaderResourceView* SRV = CommandContext.RetrieveObject<FD3D12ShaderResourceView>(Resource);
			LocalSRVs[SRVIndex] = SRV->GetView();
			BoundSRVMask |= 1ull << SRVIndex;

			ReferencedResources.Add({ SRV->GetResource(), Resource });
		}
	}

	for (uint32 CBVIndex = 0; CBVIndex < InNumUniformBuffers; ++CBVIndex)
	{
		FRHIUniformBuffer* Resource = UniformBuffers[CBVIndex];
		if (Resource)
		{
			FD3D12UniformBuffer* CBV = CommandContext.RetrieveObject<FD3D12UniformBuffer>(Resource);
			LocalCBVs[CBVIndex] = CBV->ResourceLocation.GetGPUVirtualAddress();
			BoundCBVMask |= 1ull << CBVIndex;

			ReferencedResources.Add({ CBV->ResourceLocation.GetResource(), Resource });
		}
	}

	for (uint32 SamplerIndex = 0; SamplerIndex < InNumSamplers; ++SamplerIndex)
	{
		FRHISamplerState* Resource = Samplers[SamplerIndex];
		if (Resource)
		{
			LocalSamplers[SamplerIndex] = CommandContext.RetrieveObject<FD3D12SamplerState>(Resource)->Descriptor;
			BoundSamplerMask |= 1ull << SamplerIndex;
		}
	}

	for (uint32 UAVIndex = 0; UAVIndex < InNumUAVs; ++UAVIndex)
	{
		FRHIUnorderedAccessView* Resource = UAVs[UAVIndex];
		if (Resource)
		{
			FD3D12UnorderedAccessView* UAV = CommandContext.RetrieveObject<FD3D12UnorderedAccessView>(Resource);
			LocalUAVs[UAVIndex] = UAV->GetView();
			BoundUAVMask |= 1ull << UAVIndex;

			ReferencedResources.Add({ UAV->GetResource(), Resource });
		}
	}

	const FD3D12ShaderResourceTable& ShaderResourceTable = Shader->ShaderResourceTable;

	uint32 DirtyBits = ShaderResourceTable.ResourceTableBits;

	while (DirtyBits)
	{
		// Scan for the lowest set bit, compute its index, clear it in the set of dirty bits.
		const uint32 LowestBitMask = (DirtyBits)& (-(int32)DirtyBits);
		const int32 BufferIndex = FMath::FloorLog2(LowestBitMask); // todo: This has a branch on zero, we know it could never be zero...
		DirtyBits ^= LowestBitMask;
		check(uint32(BufferIndex) < InNumUniformBuffers);
		FD3D12UniformBuffer* Buffer = FD3D12DynamicRHI::ResourceCast(UniformBuffers[BufferIndex]);
		check(Buffer);
		check(BufferIndex < ShaderResourceTable.ResourceTableLayoutHashes.Num());
		check(Buffer->GetLayout().GetHash() == ShaderResourceTable.ResourceTableLayoutHashes[BufferIndex]);

		// #dxr_todo: could implement all 3 loops using a common template function (and ideally share this with regular dx12 rhi code)

		// Textures

		{
			const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
			const TArray<uint32>& ResourceMap = ShaderResourceTable.TextureMap;
			const uint32 BufferOffset = ResourceMap[BufferIndex];
			if (BufferOffset > 0)
			{
				const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
				uint32 ResourceInfo = *ResourceInfos++;
				do
				{
					checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
					const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
					const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

					FD3D12ShaderResourceView* SRV = CommandContext.RetrieveTextureBase((FRHITexture*)Resources[ResourceIndex].GetReference())->GetShaderResourceView();
					LocalSRVs[BindIndex] = SRV->GetView();
					BoundSRVMask |= 1ull << BindIndex;

					ReferencedResources.Add({ SRV->GetResource(), SRV });

					ResourceInfo = *ResourceInfos++;
				} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			}
		}

		// SRVs

		{
			const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
			const TArray<uint32>& ResourceMap = ShaderResourceTable.ShaderResourceViewMap;
			const uint32 BufferOffset = ResourceMap[BufferIndex];
			if (BufferOffset > 0)
			{
				const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
				uint32 ResourceInfo = *ResourceInfos++;
				do
				{
					checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
					const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
					const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

					FD3D12ShaderResourceView* SRV = CommandContext.RetrieveObject<FD3D12ShaderResourceView>((FRHIShaderResourceView*)(Resources[ResourceIndex].GetReference()));
					LocalSRVs[BindIndex] = SRV->GetView();
					BoundSRVMask |= 1ull << BindIndex;

					ReferencedResources.Add({ SRV->GetResource(), SRV });

					ResourceInfo = *ResourceInfos++;
				} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			}
		}

		// Samplers

		{
			const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
			const TArray<uint32>& ResourceMap = ShaderResourceTable.SamplerMap;
			const uint32 BufferOffset = ResourceMap[BufferIndex];
			if (BufferOffset > 0)
			{
				const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
				uint32 ResourceInfo = *ResourceInfos++;
				do
				{
					checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
					const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
					const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

					FD3D12SamplerState* Sampler = CommandContext.RetrieveObject<FD3D12SamplerState>((FRHISamplerState*)(Resources[ResourceIndex].GetReference()));
					LocalSamplers[BindIndex] = Sampler->Descriptor;
					BoundSamplerMask |= 1ull << BindIndex;

					ResourceInfo = *ResourceInfos++;
				} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			}
		}
	}

	// Bind loose parameters

	if (Shader->ResourceCounts.bGlobalUniformBufferUsed)
	{
		checkf(InLooseParameterDataSize && InLooseParameterData, TEXT("Shader uses global uniform buffer, but the required loose parameter data is not provided."));
	}

	if (InLooseParameterData && Shader->ResourceCounts.bGlobalUniformBufferUsed)
	{
		const uint32 CBVIndex = 0; // Global uniform buffer is always assumed to be in slot 0
		LocalCBVs[CBVIndex] = Binder.CreateTransientConstantBuffer(InLooseParameterData, InLooseParameterDataSize);
		BoundCBVMask |= 1ull << CBVIndex;
	}

	// Validate that all resources required by the shader are set

	auto IsCompleteBinding = [](uint32 ExpectedCount, uint64 BoundMask)
	{
		if (ExpectedCount > 64) return false; // Bound resource mask can't be represented by uint64

		// All bits of the mask [0..ExpectedCount) are expected to be set
		uint64 ExpectedMask = ExpectedCount == 64 ? ~0ull : ((1ull << ExpectedCount) - 1);
		return (ExpectedMask & BoundMask) == ExpectedMask;
	};
	check(IsCompleteBinding(Shader->ResourceCounts.NumSRVs, BoundSRVMask));
	check(IsCompleteBinding(Shader->ResourceCounts.NumUAVs, BoundUAVMask));
	check(IsCompleteBinding(Shader->ResourceCounts.NumCBs, BoundCBVMask));
	check(IsCompleteBinding(Shader->ResourceCounts.NumSamplers, BoundSamplerMask));

	const uint32 NumSRVs = Shader->ResourceCounts.NumSRVs;
	if (NumSRVs)
	{
		const uint32 DescriptorTableBaseIndex = DescriptorCache.GetDescriptorTableBaseIndex(LocalSRVs, NumSRVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		const uint32 BindSlot = RootSignature->SRVRDTBindSlot(SF_Compute);
		check(BindSlot != 0xFF);

		const D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptorTableBaseGPU = DescriptorCache.ViewHeap.GetDescriptorGPU(DescriptorTableBaseIndex);
		Binder.SetRootDescriptorTable(BindSlot, ResourceDescriptorTableBaseGPU);
	}

	const uint32 NumUAVs = Shader->ResourceCounts.NumUAVs;
	if (NumUAVs)
	{
		const uint32 DescriptorTableBaseIndex = DescriptorCache.GetDescriptorTableBaseIndex(LocalUAVs, NumUAVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		const uint32 BindSlot = RootSignature->UAVRDTBindSlot(SF_Compute);
		check(BindSlot != 0xFF);

		const D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptorTableBaseGPU = DescriptorCache.ViewHeap.GetDescriptorGPU(DescriptorTableBaseIndex);
		Binder.SetRootDescriptorTable(BindSlot, ResourceDescriptorTableBaseGPU);
	}

	if (Shader->ResourceCounts.NumCBs)
	{
		checkf(RootSignature->CBVRDTBindSlot(SF_Compute) == 0xFF, TEXT("Root CBV descriptor tables are not implemented for ray tracing shaders."));

		const uint32 BindSlot = RootSignature->CBVRDBaseBindSlot(SF_Compute);
		check(BindSlot != 0xFF);

		for (uint32 i = 0; i < Shader->ResourceCounts.NumCBs; ++i)
		{
			const uint64 SlotMask = (1ull << i);
			D3D12_GPU_VIRTUAL_ADDRESS BufferAddress = (BoundCBVMask & SlotMask) ? LocalCBVs[i] : 0;
			Binder.SetRootCBV(BindSlot, i, BufferAddress);
		}
	}

	// Bind samplers

	const uint32 NumSamplers = Shader->ResourceCounts.NumSamplers;
	if (NumSamplers)
	{
		const uint32 DescriptorTableBaseIndex = DescriptorCache.GetDescriptorTableBaseIndex(LocalSamplers, NumSamplers, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

		const uint32 BindSlot = RootSignature->SamplerRDTBindSlot(SF_Compute);
		check(BindSlot != 0xFF);

		const D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptorTableBaseGPU = DescriptorCache.SamplerHeap.GetDescriptorGPU(DescriptorTableBaseIndex);
		Binder.SetRootDescriptorTable(BindSlot, ResourceDescriptorTableBaseGPU);
	}

	for (const FResourceEntry& Entry : ReferencedResources)
	{
		Binder.AddResourceReference(Entry.D3D12Resource, Entry.RHIResource);
	}
}

template <typename ResourceBinderType>
static void SetRayTracingShaderResources(
	FD3D12CommandContext& CommandContext,
	const FD3D12RayTracingShader* Shader,
	const FRayTracingShaderBindings& ResourceBindings,
	FD3D12RayTracingDescriptorCache& DescriptorCache,
	ResourceBinderType& Binder)
{
	SetRayTracingShaderResources(CommandContext, Shader,
		ARRAY_COUNT(ResourceBindings.Textures), ResourceBindings.Textures,
		ARRAY_COUNT(ResourceBindings.SRVs), ResourceBindings.SRVs,
		ARRAY_COUNT(ResourceBindings.UniformBuffers), ResourceBindings.UniformBuffers,
		ARRAY_COUNT(ResourceBindings.Samplers), ResourceBindings.Samplers,
		ARRAY_COUNT(ResourceBindings.UAVs), ResourceBindings.UAVs,
		0, nullptr, // loose parameters
		DescriptorCache, Binder);
}

static void DispatchRays(FD3D12CommandContext& CommandContext,
	const FRayTracingShaderBindings& GlobalBindings,
	const FD3D12RayTracingPipelineState* Pipeline,
	uint32 RayGenShaderIndex,
	FD3D12RayTracingShaderTable* OptShaderTable,
	const D3D12_DISPATCH_RAYS_DESC& DispatchDesc)
{
	// Setup state for RT dispatch
	
	// #dxr_todo UE-72158: RT and non-RT descriptors should use the same global heap that's dynamically sub-allocated.
	// This requires a major refactor of descriptor heap management. In the short term, RT work uses a dedicated heap
	// that's temporarily set for the duration of RT dispatch.
	ID3D12DescriptorHeap* PreviousHeaps[2] =
	{
		CommandContext.StateCache.GetDescriptorCache()->GetCurrentViewHeap()->GetHeap(),
		CommandContext.StateCache.GetDescriptorCache()->GetCurrentSamplerHeap()->GetHeap(),
	};

	// Invalidate state cache to ensure all root parameters for regular shaders are reset when non-RT work is dispatched later.
	CommandContext.StateCache.TransitionComputeState(D3D12PT_RayTracing);

	CommandContext.CommandListHandle.GraphicsCommandList()->SetComputeRootSignature(Pipeline->GlobalRootSignature);

	FD3D12RayTracingShader* RayGenShader = Pipeline->RayGenShaders.Shaders[RayGenShaderIndex];

	if (OptShaderTable && OptShaderTable->DescriptorCache)
	{
		FD3D12RayTracingDescriptorCache* DescriptorCache = OptShaderTable->DescriptorCache;

		DescriptorCache->SetDescriptorHeaps(CommandContext);
		FD3D12RayTracingGlobalResourceBinder ResourceBinder(CommandContext);
		SetRayTracingShaderResources(CommandContext, RayGenShader, GlobalBindings, *DescriptorCache, ResourceBinder);

		// #dxr_todo UE-72159: avoid updating residency if this scene was already used on the current command list (i.e. multiple ray dispatches are performed back-to-back)
		OptShaderTable->UpdateResidency(CommandContext);
	}
	else
	{
		FD3D12RayTracingDescriptorCache TransientDescriptorCache(CommandContext.GetParentDevice());
		TransientDescriptorCache.Init(MAX_SRVS + MAX_UAVS, MAX_SAMPLERS);
		TransientDescriptorCache.SetDescriptorHeaps(CommandContext);
		FD3D12RayTracingGlobalResourceBinder ResourceBinder(CommandContext);
		SetRayTracingShaderResources(CommandContext, RayGenShader, GlobalBindings, TransientDescriptorCache, ResourceBinder);
	}

	CommandContext.CommandListHandle.FlushResourceBarriers();

	ID3D12StateObject* RayTracingStateObject = Pipeline->StateObject.GetReference();

	ID3D12GraphicsCommandList4* RayTracingCommandList = CommandContext.CommandListHandle.RayTracingCommandList();
	RayTracingCommandList->SetPipelineState1(RayTracingStateObject);
	RayTracingCommandList->DispatchRays(&DispatchDesc);

	if (CommandContext.IsDefaultContext())
	{
		CommandContext.GetParentDevice()->RegisterGPUWork(1);
	}

	// Restore old global descriptor heaps
	CommandContext.CommandListHandle.GraphicsCommandList()->SetDescriptorHeaps(2, PreviousHeaps);
}


void FD3D12CommandContext::RHIRayTraceOcclusion(FRHIRayTracingScene* InScene,
	FRHIShaderResourceView* Rays,
	FRHIUnorderedAccessView* Output,
	uint32 NumRays)
{
	checkf(GetParentDevice()->GetBasicRayTracingPipeline(), TEXT("Ray tracing support is not initialized for this device. Ensure that InitRayTracing() is called before issuing any ray tracing work."));

	FD3D12RayTracingScene* Scene = FD3D12DynamicRHI::ResourceCast(InScene);

	FD3D12RayTracingPipelineState* Pipeline = GetParentDevice()->GetBasicRayTracingPipeline()->Occlusion;
	FD3D12RayTracingShaderTable& ShaderTable = Pipeline->DefaultShaderTables[GetGPUIndex()];

	if (ShaderTable.bIsDirty)
	{
		ShaderTable.CopyToGPU(GetParentDevice());
	}

	Scene->UpdateResidency(*this);

	D3D12_DISPATCH_RAYS_DESC DispatchDesc = ShaderTable.GetDispatchRaysDesc(0, 0, 0);

	DispatchDesc.Width = NumRays;
	DispatchDesc.Height = 1;
	DispatchDesc.Depth = 1;

	FRayTracingShaderBindings Bindings;
	Bindings.SRVs[0] = Scene->GetShaderResourceView();
	Bindings.SRVs[1] = Rays;
	Bindings.UAVs[0] = Output;

	ShaderTable.UpdateResidency(*this);

	DispatchRays(*this, Bindings, Pipeline, 0, nullptr, DispatchDesc);
}

void FD3D12CommandContext::RHIRayTraceIntersection(FRHIRayTracingScene* InScene,
	FRHIShaderResourceView* InRays,
	FRHIUnorderedAccessView* InOutput,
	uint32 NumRays)
{
	checkf(GetParentDevice()->GetBasicRayTracingPipeline(), TEXT("Ray tracing support is not initialized for this device. Ensure that InitRayTracing() is called before issuing any ray tracing work."));

	FD3D12RayTracingScene* Scene = FD3D12DynamicRHI::ResourceCast(InScene);
	FD3D12ShaderResourceView* Rays = FD3D12DynamicRHI::ResourceCast(InRays);
	FD3D12UnorderedAccessView* Output = FD3D12DynamicRHI::ResourceCast(InOutput);

	FD3D12RayTracingPipelineState* Pipeline = GetParentDevice()->GetBasicRayTracingPipeline()->Intersection;
	FD3D12RayTracingShaderTable& ShaderTable = Pipeline->DefaultShaderTables[GetGPUIndex()];

	if (ShaderTable.bIsDirty)
	{
		ShaderTable.CopyToGPU(GetParentDevice());
	}

	Scene->UpdateResidency(*this);

	D3D12_DISPATCH_RAYS_DESC DispatchDesc = ShaderTable.GetDispatchRaysDesc(0, 0, 0);

	DispatchDesc.Width = NumRays;
	DispatchDesc.Height = 1;
	DispatchDesc.Depth = 1;

	FRayTracingShaderBindings Bindings;
	Bindings.SRVs[0] = Scene->GetShaderResourceView();
	Bindings.SRVs[1] = Rays;
	// #dxr_todo: intersection and occlusion shaders should be split into separate files to avoid resource slot collisions.
	// Workaround for now is to bind a valid UAV to slots 0 and 1, even though only slot 1 is referenced.
	Bindings.UAVs[0] = Output;
	Bindings.UAVs[1] = Output;

	ShaderTable.UpdateResidency(*this);

	DispatchRays(*this, Bindings, Pipeline, 0, nullptr, DispatchDesc);
}

void FD3D12CommandContext::RHIRayTraceDispatch(FRHIRayTracingPipelineState* InRayTracingPipelineState, FRHIRayTracingShader* RayGenShaderRHI,
	FRHIRayTracingScene* InScene,
	const FRayTracingShaderBindings& GlobalResourceBindings,
	uint32 Width, uint32 Height)
{
	const FD3D12RayTracingPipelineState* Pipeline = FD3D12DynamicRHI::ResourceCast(InRayTracingPipelineState);

	FD3D12RayTracingScene* Scene = FD3D12DynamicRHI::ResourceCast(InScene);

	FD3D12RayTracingShaderTable* ShaderTable = Scene->FindOrCreateShaderTable(Pipeline, GetParentDevice());

	if (ShaderTable->bIsDirty)
	{
		ShaderTable->CopyToGPU(GetParentDevice());
	}

	// #dxr_todo UE-72159: avoid updating residency if this scene was already used on the current command list (i.e. multiple ray dispatches are performed back-to-back)
	Scene->UpdateResidency(*this);

	FD3D12RayTracingShader* RayGenShader = FD3D12DynamicRHI::ResourceCast(RayGenShaderRHI);
	const int32 RayGenShaderIndex = Pipeline->RayGenShaders.Find(RayGenShader->GetHash());
	checkf(RayGenShaderIndex != INDEX_NONE, TEXT("RayGen shader is not present in the given ray tracing pipeline. All RayGen shaders must be declared when creating RTPSO."));

	D3D12_DISPATCH_RAYS_DESC DispatchDesc = ShaderTable->GetDispatchRaysDesc(RayGenShaderIndex, 0, Pipeline->bAllowHitGroupIndexing);

	DispatchDesc.Width = Width;
	DispatchDesc.Height = Height;
	DispatchDesc.Depth = 1;

	DispatchRays(*this, GlobalResourceBindings, Pipeline, RayGenShaderIndex, ShaderTable, DispatchDesc);
}

void FD3D12CommandContext::RHISetRayTracingHitGroup(
	FRHIRayTracingScene* InScene, uint32 InstanceIndex, uint32 SegmentIndex, uint32 ShaderSlot,
	FRHIRayTracingPipelineState* InPipeline, uint32 HitGroupIndex,
	uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
	uint32 LooseParameterDataSize, const void* LooseParameterData,
	uint32 UserData)
{
	FD3D12RayTracingScene* Scene = FD3D12DynamicRHI::ResourceCast(InScene);
	FD3D12RayTracingPipelineState* Pipeline = FD3D12DynamicRHI::ResourceCast(InPipeline);

	checkf(ShaderSlot < Scene->ShaderSlotsPerGeometrySegment, TEXT("Shader slot is invalid. Make sure that ShaderSlotsPerGeometrySegment is correct on FRayTracingSceneInitializer."));

	FD3D12RayTracingShaderTable* ShaderTable = Scene->FindOrCreateShaderTable(Pipeline, GetParentDevice());
	checkf(ShaderTable->LocalShaderTableOffset == ShaderTable->HitGroupShaderTableOffset,
		TEXT("Hit shader records are assumed to be at the beginning of local shader table"));

	const uint32 RecordIndex = Scene->GetHitRecordBaseIndex(InstanceIndex, SegmentIndex) + ShaderSlot;
	ShaderTable->SetLocalShaderIdentifier(RecordIndex, Pipeline->HitGroupShaders.Identifiers[HitGroupIndex]);

	const uint32 UserDataOffset = offsetof(FHitGroupSystemParameters, RootConstants) + offsetof(FHitGroupSystemRootConstants, UserData);
	ShaderTable->SetLocalShaderParameters(RecordIndex, UserDataOffset, UserData);

	const FD3D12RayTracingShader* Shader = Pipeline->HitGroupShaders.Shaders[HitGroupIndex];

	FD3D12RayTracingShaderTable::FShaderRecordCacheKey CacheKey;

	const bool bCanUseRecordCache = GRayTracingCacheShaderRecords
		&& Scene->Lifetime == RTSL_SingleFrame
		&& LooseParameterDataSize == 0
		&& NumUniformBuffers > 0
		&& NumUniformBuffers <= CacheKey.MaxUniformBuffers;

	if (bCanUseRecordCache)
	{
		CacheKey = FD3D12RayTracingShaderTable::FShaderRecordCacheKey(NumUniformBuffers, UniformBuffers);

		uint32* ExistingRecordIndex = ShaderTable->ShaderRecordCache.Find(CacheKey);
		if (ExistingRecordIndex)
		{
			const uint32 OffsetFromRootSignatureStart = sizeof(FHitGroupSystemParameters);
			ShaderTable->CopyLocalShaderParameters(RecordIndex, *ExistingRecordIndex, OffsetFromRootSignatureStart);
			return;
		}
	}

	FD3D12RayTracingLocalResourceBinder ResourceBinder(*this, ShaderTable, Shader->pRootSignature, RecordIndex);
	check(ShaderTable->DescriptorCache);
	SetRayTracingShaderResources(*this, Shader,
		0, nullptr, // Textures
		0, nullptr, // SRVs
		NumUniformBuffers, UniformBuffers,
		0, nullptr, // Samplers
		0, nullptr, // UAVs
		LooseParameterDataSize, LooseParameterData,
		*(ShaderTable->DescriptorCache), ResourceBinder);

	if (bCanUseRecordCache)
	{
		ShaderTable->ShaderRecordCache.Add(CacheKey, RecordIndex);
	}
}


void FD3D12CommandContext::RHISetRayTracingCallableShader(
	FRHIRayTracingScene* InScene, uint32 ShaderSlotInScene,
	FRHIRayTracingPipelineState* InPipeline, uint32 ShaderIndexInPipeline,
	uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
	uint32 UserData)
{
	FD3D12RayTracingScene* Scene = FD3D12DynamicRHI::ResourceCast(InScene);
	FD3D12RayTracingPipelineState* Pipeline = FD3D12DynamicRHI::ResourceCast(InPipeline);

	checkf(ShaderSlotInScene < Scene->NumCallableShaderSlots, TEXT("Shader slot is invalid. Make sure that NumCallableShaderSlots is correct on FRayTracingSceneInitializer."));

	FD3D12RayTracingShaderTable* ShaderTable = Scene->FindOrCreateShaderTable(Pipeline, GetParentDevice());

	const uint32 RecordIndex = ShaderTable->CallableShaderRecordIndexOffset + ShaderSlotInScene;
	ShaderTable->SetLocalShaderIdentifier(RecordIndex, Pipeline->CallableShaders.Identifiers[ShaderIndexInPipeline]);

	const uint32 UserDataOffset = offsetof(FHitGroupSystemParameters, RootConstants) + offsetof(FHitGroupSystemRootConstants, UserData);
	ShaderTable->SetLocalShaderParameters(RecordIndex, UserDataOffset, UserData);

	const FD3D12RayTracingShader* Shader = Pipeline->CallableShaders.Shaders[ShaderIndexInPipeline];

	FD3D12RayTracingLocalResourceBinder ResourceBinder(*this, ShaderTable, Shader->pRootSignature, RecordIndex);
	check(ShaderTable->DescriptorCache);
	SetRayTracingShaderResources(*this, Shader,
		0, nullptr, // Textures
		0, nullptr, // SRVs
		NumUniformBuffers, UniformBuffers,
		0, nullptr, // Samplers
		0, nullptr, // UAVs
		0, nullptr, // Loose parameters
		*(ShaderTable->DescriptorCache), ResourceBinder);
}

#endif // D3D12_RHI_RAYTRACING
