// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RayTracing.h"

#if D3D12_RHI_RAYTRACING

#include "D3D12Resources.h"
#include "D3D12Util.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "Experimental/Containers/SherwoodHashTable.h"
#include "BuiltInRayTracingShaders.h"
#include "Hash/CityHash.h"
#include "HAL/CriticalSection.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"
#include "RayTracingInstanceCopyShader.h" 

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

static int32 GD3D12RayTracingViewDescriptorHeapSize = 250'000;
static int32 GD3D12RayTracingViewDescriptorHeapOverflowReported = 0;
static FAutoConsoleVariableRef CVarD3D12RayTracingViewDescriptorHeapSize(
	TEXT("r.D3D12.RayTracing.ViewDescriptorHeapSize"),
	GD3D12RayTracingViewDescriptorHeapSize,
	TEXT("Maximum number of descriptors per ray tracing view descriptor heap. (default = 250k, ~8MB per heap)\n")
	TEXT("Typical measured descriptor heap usage in large scenes is ~50k. An error is reported when this limit is reached and shader bindings for subsequent objects are skipped.\n"),
	ECVF_ReadOnly
);

static int32 GD3D12RayTracingAllowCompaction = 1;
static FAutoConsoleVariableRef CVarD3D12RayTracingAllowCompaction(
	TEXT("r.D3D12.RayTracing.AllowCompaction"),
	GD3D12RayTracingAllowCompaction,
	TEXT("Whether to automatically perform compaction for static acceleration structures to save GPU memory. (default = 1)\n"),
	ECVF_ReadOnly
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

DECLARE_CYCLE_STAT(TEXT("SetRayTracingHitGroups"), STAT_D3D12SetRayTracingHitGroups, STATGROUP_D3D12RayTracing);
DECLARE_CYCLE_STAT(TEXT("CreateShaderTable"), STAT_D3D12CreateShaderTable, STATGROUP_D3D12RayTracing);
DECLARE_CYCLE_STAT(TEXT("BuildTopLevel"), STAT_D3D12BuildTLAS, STATGROUP_D3D12RayTracing);
DECLARE_CYCLE_STAT(TEXT("BuildBottomLevel"), STAT_D3D12BuildBLAS, STATGROUP_D3D12RayTracing);
DECLARE_CYCLE_STAT(TEXT("DispatchRays"), STAT_D3D12DispatchRays, STATGROUP_D3D12RayTracing);

// Whether to compare the full descriptor table on cache lookup or only use CityHash64 digest.
#ifndef RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE
#define RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE 1
#endif // RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE

struct FD3D12ShaderIdentifier
{
	uint64 Data[4] = {~0ull, ~0ull, ~0ull, ~0ull};

	// No shader is executed if a shader binding table record with null identifier is encountered.
	static const FD3D12ShaderIdentifier Null;

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

	void SetData(const void* InData)
	{
		FMemory::Memcpy(Data, InData, sizeof(Data));
	}
};

const FD3D12ShaderIdentifier FD3D12ShaderIdentifier::Null = { 0, 0, 0, 0 };

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
	// 4) D3D12_STATE_OBJECT_CONFIG
	// 5) Global root signature
	static constexpr uint32 NumRequiredSubobjects = 5;

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
	ShaderConfig.MaxAttributeSizeInBytes = RAY_TRACING_MAX_ALLOWED_ATTRIBUTE_SIZE; // sizeof 2 floats (barycentrics)
	ShaderConfig.MaxPayloadSizeInBytes = MaxPayloadSizeInBytes;
	check(ShaderConfig.MaxPayloadSizeInBytes <= RAY_TRACING_MAX_ALLOWED_PAYLOAD_SIZE);

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
	PipelineConfig.MaxTraceRecursionDepth = RAY_TRACING_MAX_ALLOWED_RECURSION_DEPTH;
	const uint32 PipelineConfigIndex = Index;
	Subobjects[Index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &PipelineConfig };

	// State object config

	D3D12_STATE_OBJECT_CONFIG StateObjectConfig = {};
	if (GRHISupportsRayTracingPSOAdditions)
	{
		StateObjectConfig.Flags = D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS;
	}
	Subobjects[Index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &StateObjectConfig };

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

static FD3D12ShaderIdentifier GetShaderIdentifier(ID3D12StateObjectProperties* PipelineProperties, const TCHAR* ExportName)
{
	const void* ShaderIdData = PipelineProperties->GetShaderIdentifier(ExportName);
	checkf(ShaderIdData, TEXT("Couldn't find requested export in the ray tracing shader pipeline"));

	FD3D12ShaderIdentifier Result;
	Result.SetData(ShaderIdData);

	return Result;
}

static FD3D12ShaderIdentifier GetShaderIdentifier(ID3D12StateObject* StateObject, const TCHAR* ExportName)
{
	TRefCountPtr<ID3D12StateObjectProperties> PipelineProperties;
	HRESULT QueryInterfaceResult = StateObject->QueryInterface(IID_PPV_ARGS(PipelineProperties.GetInitReference()));
	checkf(SUCCEEDED(QueryInterfaceResult), TEXT("Failed to query pipeline properties from the ray tracing pipeline state object. Result=%08x"), QueryInterfaceResult);

	return GetShaderIdentifier(PipelineProperties, ExportName);
}

// Cache for ray tracing pipeline collection objects, containing single shaders that can be linked into full pipelines.
class FD3D12RayTracingPipelineCache : FD3D12DeviceChild
{
public:

	UE_NONCOPYABLE(FD3D12RayTracingPipelineCache)

	FD3D12RayTracingPipelineCache(FD3D12Device* Device)
		: FD3D12DeviceChild(Device)
		, DefaultLocalRootSignature(Device->GetParentAdapter())
	{
		// Default empty local root signature

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC LocalRootSignatureDesc = {};
		if (Device->GetParentAdapter()->GetRootSignatureVersion() >= D3D_ROOT_SIGNATURE_VERSION_1_1)
		{
			LocalRootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
			LocalRootSignatureDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		}
		else
		{
		LocalRootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
		LocalRootSignatureDesc.Desc_1_0.Flags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		}
		
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
			check(bDeserialized || (CompileEvent.IsValid() && CompileEvent->IsComplete()));
			check(StateObject);

			D3D12_EXISTING_COLLECTION_DESC Result = {};
			Result.pExistingCollection = StateObject;

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
		bool bDeserialized = false;

		static constexpr uint32 MaxExports = 4;
		TArray<FString, TFixedAllocator<MaxExports>> ExportNames;

		FD3D12ShaderIdentifier Identifier;

		float CompileTimeMS = 0.0f;
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

			uint64 CompileTimeCycles = 0;
			CompileTimeCycles -= FPlatformTime::Cycles64();

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

			// Validate that memory reservation was correct

			check(Entry.ExportNames.Num() <= Entry.MaxExports);

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

			// Shader identifier can be queried immediately here per PSO collection, however this does not work on old NVIDIA drivers (430.00).
			// Therefore shader identifiers need to be queried from the final linked pipeline (JIRA DH-2182).
			// Entry.Identifier = GetShaderIdentifier(Entry.StateObject, Entry.GetPrimaryExportNameChars());

			CompileTimeCycles += FPlatformTime::Cycles64();

			Entry.CompileTimeMS = float(FPlatformTime::ToMilliseconds64(CompileTimeCycles));
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
		FD3D12Device* Device,
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
		if (CollectionType == ECollectionType::RayGen)
		{
			// RayGen shaders use a default empty local root signature as all their resources bound via global RS.
			LocalRootSignature = DefaultLocalRootSignature.GetRootSignature();
		}
		else
		{
			// All other shaders (hit groups, miss, callable) use custom root signatures.
			LocalRootSignature = Shader->pRootSignature->GetRootSignature();
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

			if (Shader->bPrecompiledPSO)
			{
				D3D12_SHADER_BYTECODE Bytecode = Shader->ShaderBytecode.GetShaderBytecode();
				Entry.StateObject = Device->DeserializeRayTracingStateObject(Bytecode, GlobalRootSignature);

				checkf(Entry.StateObject != nullptr, TEXT("Failed to deserialize RTPSO"));

				Entry.ExportNames.Add(Shader->EntryPoint);
				Entry.Identifier = GetShaderIdentifier(Entry.StateObject, *Shader->EntryPoint);
				Entry.bDeserialized = true;
			}
			else
			{
				// Generate primary export name, which is immediately required on the PSO creation thread.
				Entry.ExportNames.Add(GenerateShaderName(GetCollectionTypeName(CollectionType), ShaderHash));
				checkf(Entry.ExportNames.Num() == 1, TEXT("Primary export name must always be first."));

				// Defer actual compilation to another task, as there may be many shaders that may be compiled in parallel.
				// Result of the compilation (the collection PSO) is not needed until final RT PSO is linked.
				Entry.CompileEvent = TGraphTask<FShaderCompileTask>::CreateTask().ConstructAndDispatchWhenReady(
					Entry,
					CacheKey,
					Device->GetDevice5(),
					CollectionType
				);
			}
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

	ID3D12RootSignature* GetGlobalRootSignature()
	{
		FD3D12Adapter* Adapter = GetParentDevice()->GetParentAdapter();

	#if USE_STATIC_ROOT_SIGNATURE
		return Adapter->GetStaticRayTracingGlobalRootSignature()->GetRootSignature();
	#else // USE_STATIC_ROOT_SIGNATURE
		FD3D12QuantizedBoundShaderState QBSS = GetRayTracingGlobalRootSignatureDesc();
		return Adapter->GetRootSignature(QBSS)->GetRootSignature();
	#endif // USE_STATIC_ROOT_SIGNATURE
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
		Desc.NodeMask = GetParentDevice()->GetGPUMask().GetNative();

		ID3D12DescriptorHeap* D3D12Heap = nullptr;

		const TCHAR* HeapName = Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? TEXT("RT View Heap") : TEXT("RT Sampler Heap");
		UE_LOG(LogD3D12RHI, Log, TEXT("Creating %s with %d entries"), HeapName, NumDescriptors);

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

	void Init(uint32 InMaxNumDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE InType)
	{
		check(D3D12Heap == nullptr);

		Type = InType;
		HeapCacheEntry = GetParentDevice()->GetRayTracingDescriptorHeapCache()->AllocateHeap(Type, InMaxNumDescriptors);

		MaxNumDescriptors = HeapCacheEntry.NumDescriptors;
		D3D12Heap = HeapCacheEntry.Heap;

		CPUBase = D3D12Heap->GetCPUDescriptorHandleForHeapStart();
		GPUBase = D3D12Heap->GetGPUDescriptorHandleForHeapStart();

		checkf(CPUBase.ptr, TEXT("Ray tracing descriptor heap of type %d returned from descriptor heap cache is invalid."), Type);

		DescriptorSize = GetParentDevice()->GetDevice()->GetDescriptorHandleIncrementSize(Type);

	#if RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE
		Descriptors.SetNum(MaxNumDescriptors);
	#endif // RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE
	}

	// Returns descriptor heap base index or -1 if allocation is not possible.
	// Thread-safe (uses atomic linear allocation).
	int32 Allocate(uint32 InNumDescriptors)
	{
		int32 Result = FPlatformAtomics::InterlockedAdd(&NumAllocatedDescriptors, InNumDescriptors);

		if (Result + InNumDescriptors > MaxNumDescriptors)
		{
			if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
			{
				UE_LOG(LogD3D12RHI, Fatal,
					TEXT("Ray tracing sampler descriptor heap overflow. ")
					TEXT("It is not possible to recover from this error, as maximum D3D12 sampler heap size is 2048."));
			}
			else if (GD3D12RayTracingViewDescriptorHeapSize == MaxNumDescriptors
				&& FPlatformAtomics::InterlockedOr(&GD3D12RayTracingViewDescriptorHeapOverflowReported, 1) == 0)
			{
				// NOTE: GD3D12RayTracingViewDescriptorHeapOverflowReported is set atomically because multiple 
				// allocations may be happening simultaneously, but we only want to report the error once.

				UE_LOG(LogD3D12RHI, Error,
					TEXT("Ray tracing view descriptor heap overflow. Current frame will not be rendered correctly. ")
					TEXT("Increase r.D3D12.RayTracingViewDescriptorHeapSize to at least %d to fix this issue."),
					MaxNumDescriptors * 2);
			}

			Result = -1;
		}

		return Result;
	}

	void CopyDescriptors(int32 BaseIndex, const D3D12_CPU_DESCRIPTOR_HANDLE* InDescriptors, uint32 InNumDescriptors)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor = GetDescriptorCPU(BaseIndex);
		GetParentDevice()->GetDevice()->CopyDescriptors(1, &DestDescriptor, &InNumDescriptors, InNumDescriptors, InDescriptors, nullptr, Type);
	#if RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE
		for (uint32 i = 0; i < InNumDescriptors; ++i)
		{
			Descriptors[BaseIndex + i].ptr = InDescriptors[i].ptr;
		}
	#endif // RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE
	}

#if RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE
	bool CompareDescriptors(int32 BaseIndex, const D3D12_CPU_DESCRIPTOR_HANDLE* InDescriptors, uint32 InNumDescriptors)
	{
		for (uint32 i = 0; i < InNumDescriptors; ++i)
		{
			if (Descriptors[BaseIndex + i].ptr != InDescriptors[i].ptr)
			{
				return false;
			}
		}
		return true;
	}
#endif // RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE

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

	D3D12_DESCRIPTOR_HEAP_TYPE Type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	ID3D12DescriptorHeap* D3D12Heap = nullptr;
	uint32 MaxNumDescriptors = 0;

	int32 NumAllocatedDescriptors = 0;

	uint32 DescriptorSize = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE CPUBase = {};
	D3D12_GPU_DESCRIPTOR_HANDLE GPUBase = {};

	FD3D12RayTracingDescriptorHeapCache::Entry HeapCacheEntry;

#if RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE
	TArray<D3D12_CPU_DESCRIPTOR_HANDLE> Descriptors;
#endif // RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE
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
		check(IsInRHIThread() || !IsRunningRHIInSeparateThread());
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

	// Returns descriptor heap base index for this descriptor table allocation or -1 if allocation failed.
	int32 AllocateDescriptorTable(const D3D12_CPU_DESCRIPTOR_HANDLE* Descriptors, uint32 NumDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32 WorkerIndex)
	{
		checkSlow(Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

		FD3D12RayTracingDescriptorHeap& Heap = (Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) ? ViewHeap : SamplerHeap;
		TDescriptorHashMap& Map = (Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) ? ViewDescriptorTableCache[WorkerIndex]: SamplerDescriptorTableCache[WorkerIndex];

		const uint64 Key = CityHash64((const char*)Descriptors, sizeof(Descriptors[0]) * NumDescriptors);

		const int32 InvalidIndex = -1;
		int32& DescriptorTableBaseIndex = Map.FindOrAdd(Key, InvalidIndex);

		if (DescriptorTableBaseIndex != InvalidIndex)
		{
		#if RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE
			if (ensureMsgf(Heap.CompareDescriptors(DescriptorTableBaseIndex, Descriptors, NumDescriptors), 
				TEXT("Ray tracing descriptor cache hash collision detected!")))
		#endif // RAY_TRACING_DESCRIPTOR_CACHE_FULL_COMPARE
			{
				return DescriptorTableBaseIndex;
			}
		}

		DescriptorTableBaseIndex = Heap.Allocate(NumDescriptors);

		if (DescriptorTableBaseIndex == InvalidIndex)
		{
			return InvalidIndex;
		}

		Heap.CopyDescriptors(DescriptorTableBaseIndex, Descriptors, NumDescriptors);

		if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		{
			INC_DWORD_STAT_BY(STAT_D3D12RayTracingUsedViewDescriptors, NumDescriptors);
		}
		else
		{
			INC_DWORD_STAT_BY(STAT_D3D12RayTracingUsedSamplerDescriptors, NumDescriptors);
		}

		return DescriptorTableBaseIndex;
	}

	FD3D12RayTracingDescriptorHeap ViewHeap;
	FD3D12RayTracingDescriptorHeap SamplerHeap;

	template<typename KeyType>
	struct TIdentityHash
	{
		static FORCEINLINE bool Matches(KeyType A, KeyType B)
		{
			return A == B;
		}
		static FORCEINLINE uint32 GetKeyHash(KeyType Key)
		{
			return (uint32)Key;
		}
	};

	static constexpr uint32 MaxBindingWorkers = FD3D12RayTracingScene::MaxBindingWorkers;
	using TDescriptorHashMap = Experimental::TSherwoodMap<uint64, int32, TIdentityHash<uint64>>;
	TDescriptorHashMap ViewDescriptorTableCache[MaxBindingWorkers];
	TDescriptorHashMap SamplerDescriptorTableCache[MaxBindingWorkers];
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

	static constexpr uint32 MaxBindingWorkers = FD3D12RayTracingScene::MaxBindingWorkers;

	struct FInitializer
	{
		uint32 NumRayGenShaders = 0;
		uint32 NumMissShaders = 0;
		uint32 NumMissRecords = 0;
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
	#if USE_STATIC_ROOT_SIGNATURE
		for (FD3D12ConstantBufferView* CBV : TransientCBVs)
		{
			delete CBV;
		}
	#endif // USE_STATIC_ROOT_SIGNATURE
	}

	void Init(const FInitializer& Initializer, FD3D12Device* Device)
	{
		checkf(Initializer.LocalRootDataSize <= 4096, TEXT("The maximum size of a local root signature is 4KB.")); // as per section 4.22.1 of DXR spec v1.0
		checkf(Initializer.NumRayGenShaders >= 1, TEXT("All shader tables must contain at least one raygen shader."));

		LocalRecordSizeUnaligned = ShaderIdentifierSize + Initializer.LocalRootDataSize;
		LocalRecordStride = RoundUpToNextMultiple(LocalRecordSizeUnaligned, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

		// Custom descriptor cache is only required when local resources may be bound.
		// If only global resources are used, then transient descriptor cache can be used.
		const bool bNeedsDescriptorCache = (Initializer.NumHitRecords + Initializer.NumCallableRecords + Initializer.NumMissRecords) * Initializer.LocalRootDataSize != 0;

		if (bNeedsDescriptorCache)
		{
			// #dxr_todo UE-72158: Remove this when RT descriptors are sub-allocated from the global view descriptor heap.

			if (GD3D12RayTracingViewDescriptorHeapOverflowReported)
			{
				GD3D12RayTracingViewDescriptorHeapSize = GD3D12RayTracingViewDescriptorHeapSize * 2;
				GD3D12RayTracingViewDescriptorHeapOverflowReported = 0;
			}

			// D3D12 is guaranteed to support 1M (D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1) descriptors in a CBV/SRV/UAV heap, so clamp the size to this.
			// https://docs.microsoft.com/en-us/windows/desktop/direct3d12/hardware-support
			const uint32 NumViewDescriptors = FMath::Min(D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1, GD3D12RayTracingViewDescriptorHeapSize);
			const uint32 NumSamplerDescriptors = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;

			DescriptorCache = new FD3D12RayTracingDescriptorCache(Device);
			DescriptorCache->Init(NumViewDescriptors, NumSamplerDescriptors);
		}

		NumRayGenShaders = Initializer.NumRayGenShaders;
		NumMissRecords = Initializer.NumMissRecords;
		NumHitRecords = Initializer.NumHitRecords;
		NumCallableRecords = Initializer.NumCallableRecords;

		uint32 TotalDataSize = 0;

		RayGenShaderTableOffset = TotalDataSize;
		TotalDataSize += NumRayGenShaders * RayGenRecordStride;
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

		MissShaderTableOffset = TotalDataSize;
		TotalDataSize += Initializer.NumMissRecords * LocalRecordStride;
		TotalDataSize = RoundUpToNextMultiple(TotalDataSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

		// Hit groups and callable shaders are stored in a consecutive memory block and are accessed using common local record indices.
		LocalShaderTableOffset = HitGroupShaderTableOffset;
		CallableShaderRecordIndexOffset = (CallableShaderTableOffset - LocalShaderTableOffset) / LocalRecordStride;
		MissShaderRecordIndexOffset = (MissShaderTableOffset - LocalShaderTableOffset) / LocalRecordStride;
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
		const uint32 WriteOffset = MissShaderTableOffset + RecordIndex * LocalRecordStride;
		WriteData(WriteOffset, ShaderIdentifier.Data, ShaderIdentifierSize);
	}

	void SetDefaultHitGroupIdentifier(const FD3D12ShaderIdentifier& ShaderIdentifier)
	{
		const uint32 WriteOffset = DefaultHitGroupShaderTableOffset;
		WriteData(WriteOffset, ShaderIdentifier.Data, ShaderIdentifierSize);
	}

	void SetHitGroupSystemParameters(uint32 RecordIndex, const FHitGroupSystemParameters& SystemParameters)
	{
		const uint32 OffsetWithinRootSignature = 0; // System parameters are always first in the RS.
		SetLocalShaderParameters(RecordIndex, OffsetWithinRootSignature, SystemParameters);
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
		// Set all to the default
		check(Identifiers.Num());
		for (uint32 Index = 0; Index < NumMissRecords; ++Index)
		{
			SetMissIdentifier(Index, Identifiers[0]);
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
		TRACE_CPUPROFILER_EVENT_SCOPE(ShaderTableCopyToGPU);

		check(IsInRHIThread() || !IsRunningRHIInSeparateThread());

		checkf(Data.Num(), TEXT("Shader table is expected to be initialized before copying to GPU."));

		FD3D12Adapter* Adapter = Device->GetParentAdapter();

		D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(Data.GetResourceDataSize(), D3D12_RESOURCE_FLAG_NONE, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.ResourceArray = &Data;
		CreateInfo.GPUMask = FRHIGPUMask::FromIndex(Device->GetGPUIndex());

		Buffer = Adapter->CreateRHIBuffer<FD3D12MemBuffer>(
			nullptr, BufferDesc, BufferDesc.Alignment,
			0, BufferDesc.Width, BUF_Static, ED3D12ResourceStateMode::SingleState, CreateInfo);

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

		Desc.MissShaderTable.StartAddress = ShaderTableAddress + MissShaderTableOffset;
		Desc.MissShaderTable.StrideInBytes = LocalRecordStride;
		Desc.MissShaderTable.SizeInBytes = LocalRecordStride * NumMissRecords;


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
	uint32 NumMissRecords = 0;
	uint32 NumLocalRecords = 0;

	uint32 RayGenShaderTableOffset = 0;
	uint32 MissShaderTableOffset = 0;
	uint32 DefaultHitGroupShaderTableOffset = 0;
	uint32 HitGroupShaderTableOffset = 0;
	uint32 CallableShaderTableOffset = 0;
	uint32 LocalShaderTableOffset = 0;
	uint32 CallableShaderRecordIndexOffset = 0;
	uint32 MissShaderRecordIndexOffset = 0;

	uint64 LastCommandListID = 0;

	// Note: TABLE_BYTE_ALIGNMENT is used instead of RECORD_BYTE_ALIGNMENT to allow arbitrary switching 
	// between multiple RayGen and Miss shaders within the same underlying table.
	static constexpr uint32 RayGenRecordStride = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

	uint32 LocalRecordSizeUnaligned = 0; // size of the shader identifier + local root parameters, not aligned to SHADER_RECORD_BYTE_ALIGNMENT (used for out-of-bounds access checks)
	uint32 LocalRecordStride = 0; // size of shader identifier + local root parameters, aligned to SHADER_RECORD_BYTE_ALIGNMENT (same for hit groups and callable shaders)
	TResourceArray<uint8, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT> Data;

	bool bIsDirty = true;
	TRefCountPtr<FD3D12MemBuffer> Buffer;

	// SBTs have their own descriptor heaps
	FD3D12RayTracingDescriptorCache* DescriptorCache = nullptr;

	struct FShaderRecordCacheKey
	{
		static constexpr uint32 MaxUniformBuffers = 6;
		FRHIUniformBuffer* const* UniformBuffers[MaxUniformBuffers];
		uint64 Hash;
		uint32 NumUniformBuffers;
		uint32 ShaderIndex;

		FShaderRecordCacheKey() = default;
		FShaderRecordCacheKey(uint32 InNumUniformBuffers, FRHIUniformBuffer* const* InUniformBuffers, uint32 InShaderIndex)
		{
			ShaderIndex = InShaderIndex;

			check(InNumUniformBuffers <= MaxUniformBuffers);
			NumUniformBuffers = FMath::Min(MaxUniformBuffers, InNumUniformBuffers);

			const uint64 DataSizeInBytes = sizeof(FRHIUniformBuffer*) * NumUniformBuffers;
			FMemory::Memcpy(UniformBuffers, InUniformBuffers, DataSizeInBytes);
			Hash = CityHash64(reinterpret_cast<const char*>(UniformBuffers), DataSizeInBytes);
		}

		bool operator == (const FShaderRecordCacheKey& Other) const
		{
			if (Hash != Other.Hash) return false;
			if (ShaderIndex != Other.ShaderIndex) return false;
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
	TMap<FShaderRecordCacheKey, uint32> ShaderRecordCache[MaxBindingWorkers];

	// A set of all resources referenced by this shader table for the purpose of updating residency before ray tracing work dispatch.
	Experimental::TSherwoodSet<void*> ReferencedD3D12ResourceSet[MaxBindingWorkers];
	TArray<TRefCountPtr<FD3D12Resource>> ReferencedD3D12Resources[MaxBindingWorkers];
	void AddResourceReference(FD3D12Resource* D3D12Resource, uint32 WorkerIndex)
	{
		bool bIsAlreadyInSet = false;
		ReferencedD3D12ResourceSet[WorkerIndex].Add(D3D12Resource, &bIsAlreadyInSet);
		if (!bIsAlreadyInSet)
		{
			ReferencedD3D12Resources[WorkerIndex].Add(D3D12Resource);
		}
	}

	void UpdateResidency(FD3D12CommandContext& CommandContext)
	{
		// Skip redundant resource residency updates when a shader table is repeatedly used on the same command list
		if (LastCommandListID == CommandContext.CommandListHandle.GetCommandListID())
		{
			return;
		}

		// Merge all data from worker threads into the main set

		for (uint32 WorkerIndex = 1; WorkerIndex < MaxBindingWorkers; ++WorkerIndex)
		{
			for (FD3D12Resource* Resource : ReferencedD3D12Resources[WorkerIndex])
			{
				AddResourceReference(Resource, 0);
			}
			
			ReferencedD3D12Resources[WorkerIndex].Empty();
		}

		// Use the main (merged) set data to actually update resource residency

		for (FD3D12Resource* Resource : ReferencedD3D12Resources[0])
		{
			Resource->UpdateResidency(CommandContext.CommandListHandle);
		}

		Buffer->GetResource()->UpdateResidency(CommandContext.CommandListHandle);

		LastCommandListID = CommandContext.CommandListHandle.GetCommandListID();
	}

	// Some resources referenced in SBT may be dynamic (written on GPU timeline) and may require transition barriers.
	// We save such resources while we fill the SBT and issue transitions before the SBT is used.

	TSet<FD3D12ShaderResourceView*> TransitionSRVs[MaxBindingWorkers];
	TSet<FD3D12UnorderedAccessView*> TransitionUAVs[MaxBindingWorkers];

	void AddResourceTransition(FD3D12ShaderResourceView* SRV, uint32 WorkerIndex)
	{
		TransitionSRVs[WorkerIndex].Add(SRV);
	}

	void AddResourceTransition(FD3D12UnorderedAccessView* UAV, uint32 WorkerIndex)
	{
		TransitionUAVs[WorkerIndex].Add(UAV);
	}

	void TransitionResources(FD3D12CommandContext& CommandContext)
	{
		// Merge all data from worker threads into the main set

		for (uint32 WorkerIndex = 1; WorkerIndex < MaxBindingWorkers; ++WorkerIndex)
		{
			for (FD3D12ShaderResourceView* SRV : TransitionSRVs[WorkerIndex])
			{
				AddResourceTransition(SRV, 0);
			}

			for (FD3D12UnorderedAccessView* UAV : TransitionUAVs[WorkerIndex])
			{
				AddResourceTransition(UAV, 0);
			}

			TransitionSRVs[WorkerIndex].Empty();
			TransitionUAVs[WorkerIndex].Empty();
		}

		// Use the main (merged) set data to perform resource transitions

		for (FD3D12ShaderResourceView* SRV : TransitionSRVs[0])
		{
			FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, SRV, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}

		for (FD3D12UnorderedAccessView* UAV : TransitionUAVs[0])
		{
			FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, UAV, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
	}

#if USE_STATIC_ROOT_SIGNATURE
	TArray<FD3D12ConstantBufferView*> TransientCBVs;
#endif // USE_STATIC_ROOT_SIGNATURE
};


template<typename ShaderType>
static FD3D12RayTracingShader* GetBuildInRayTracingShader()
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	auto Shader = ShaderMap->GetShader<ShaderType>();
	FD3D12RayTracingShader* RayTracingShader = static_cast<FD3D12RayTracingShader*>(Shader.GetRayTracingShader());
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

		checkf(Initializer.GetRayGenTable().Num() > 0 || Initializer.bPartial, TEXT("Ray tracing pipelines must have at leat one ray generation shader."));

		uint64 TotalCreationTime = 0;
		uint64 CompileTime = 0;
		uint64 LinkTime = 0;
		uint32 NumCacheHits = 0;

		TotalCreationTime -= FPlatformTime::Cycles64();

		ID3D12Device5* RayTracingDevice = Device->GetDevice5();

		// Use hit and miss shaders from initializer or fall back to default ones if none were provided

		FRHIRayTracingShader* DefaultHitShader = GetBuildInRayTracingShader<FDefaultMainCHS>();
		FRHIRayTracingShader* DefaultHitGroupTable[] = { DefaultHitShader };

		TArrayView<FRHIRayTracingShader*> InitializerHitGroups = (Initializer.GetHitGroupTable().Num() || Initializer.bPartial)
			? Initializer.GetHitGroupTable()
			: DefaultHitGroupTable;

		FRHIRayTracingShader* DefaultMissShader = GetBuildInRayTracingShader<FDefaultPayloadMS>();
		FRHIRayTracingShader* DefaultMissTable[] = { DefaultMissShader };

		TArrayView<FRHIRayTracingShader*> InitializerMissShaders = (Initializer.GetMissTable().Num() || Initializer.bPartial)
			? Initializer.GetMissTable()
			: DefaultMissTable;

		TArrayView<FRHIRayTracingShader*> InitializerRayGenShaders = Initializer.GetRayGenTable();
		TArrayView<FRHIRayTracingShader*> InitializerCallableShaders = Initializer.GetCallableTable();

		const uint32 MaxTotalShaders = InitializerRayGenShaders.Num() + InitializerMissShaders.Num() + InitializerHitGroups.Num() + InitializerCallableShaders.Num();
		checkf(MaxTotalShaders >= 1, TEXT("Ray tracing pipelines are expected to contain at least one shader"));

		FD3D12RayTracingPipelineCache* PipelineCache = Device->GetRayTracingPipelineCache();

		// All raygen shaders must share the same global root signature (this is validated below)

		GlobalRootSignature = PipelineCache->GetGlobalRootSignature();

		const FD3D12RayTracingPipelineState* BasePipeline = GRHISupportsRayTracingPSOAdditions 
			? FD3D12DynamicRHI::ResourceCast(Initializer.BasePipeline.GetReference())
			: nullptr;

		if (BasePipeline)
		{
			PipelineShaderHashes = BasePipeline->PipelineShaderHashes;
		}
		PipelineShaderHashes.Reserve(MaxTotalShaders);

		TArray<FD3D12RayTracingPipelineCache::FEntry*> UniqueShaderCollections;
		UniqueShaderCollections.Reserve(MaxTotalShaders);

		FGraphEventArray CompileCompletionList;
		CompileCompletionList.Reserve(MaxTotalShaders);

		// Helper function to acquire a D3D12_EXISTING_COLLECTION_DESC for a compiled shader via cache

		auto AddShaderCollection = [Device, GlobalRootSignature = this->GlobalRootSignature, PipelineCache,
										&UniqueShaderHashes = this->PipelineShaderHashes, &UniqueShaderCollections, &Initializer, &NumCacheHits, &CompileTime,
										&CompileCompletionList]
			(FD3D12RayTracingShader* Shader, FD3D12RayTracingPipelineCache::ECollectionType CollectionType)
		{
			bool bIsAlreadyInSet = false;
			const uint64 ShaderHash = GetShaderHash64(Shader);
			UniqueShaderHashes.Add(ShaderHash, &bIsAlreadyInSet);

			bool bCacheHit = false;

			CompileTime -= FPlatformTime::Cycles64();

			FD3D12RayTracingPipelineCache::FEntry* ShaderCacheEntry = PipelineCache->GetOrCompileShader(
				Device, Shader, GlobalRootSignature,
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

		TArray<FD3D12RayTracingPipelineCache::FEntry*> RayGenShaderEntries;

		RayGenShaders.Reserve(InitializerRayGenShaders.Num());
		RayGenShaderEntries.Reserve(InitializerRayGenShaders.Num());

		for (FRHIRayTracingShader* ShaderRHI : InitializerRayGenShaders)
		{
			FD3D12RayTracingShader* Shader = FD3D12DynamicRHI::ResourceCast(ShaderRHI);
			checkf(Shader->pRootSignature->GetRootSignature() == GlobalRootSignature, TEXT("All raygen and miss shaders must share the same root signature"));
			checkf(!Shader->ResourceCounts.bGlobalUniformBufferUsed, TEXT("Global uniform buffers are not implemented for ray generation shaders"));

			FD3D12RayTracingPipelineCache::FEntry* ShaderCacheEntry = AddShaderCollection(Shader, FD3D12RayTracingPipelineCache::ECollectionType::RayGen);

			RayGenShaderEntries.Add(ShaderCacheEntry);
			RayGenShaders.Shaders.Add(Shader);
		}

		MaxHitGroupViewDescriptors = 0;
		MaxLocalRootSignatureSize = 0;


		// Add miss shaders

		TArray<FD3D12RayTracingPipelineCache::FEntry*> MissShaderEntries;
		MissShaders.Reserve(InitializerMissShaders.Num());
		MissShaderEntries.Reserve(InitializerMissShaders.Num());

		for (FRHIRayTracingShader* ShaderRHI : InitializerMissShaders)
		{
			FD3D12RayTracingShader* Shader = FD3D12DynamicRHI::ResourceCast(ShaderRHI);

			checkf(Shader, TEXT("A valid ray tracing shader must be provided for all elements in the FRayTracingPipelineStateInitializer miss shader table."));
			checkf(!Shader->ResourceCounts.bGlobalUniformBufferUsed, TEXT("Global uniform buffers are not implemented for ray tracing miss shaders"));

			const uint32 ShaderViewDescriptors = Shader->ResourceCounts.NumSRVs + Shader->ResourceCounts.NumUAVs;
			MaxHitGroupViewDescriptors = FMath::Max(MaxHitGroupViewDescriptors, ShaderViewDescriptors);
			MaxLocalRootSignatureSize = FMath::Max(MaxLocalRootSignatureSize, Shader->pRootSignature->GetTotalRootSignatureSizeInBytes());

			FD3D12RayTracingPipelineCache::FEntry* ShaderCacheEntry = AddShaderCollection(Shader, FD3D12RayTracingPipelineCache::ECollectionType::Miss);

			MissShaderEntries.Add(ShaderCacheEntry);
			MissShaders.Shaders.Add(Shader);
		}

		// Ensure miss shader 0 (default) requires no parameters
		if (!Initializer.bPartial)
		{
			FD3D12RayTracingShader* Shader = FD3D12DynamicRHI::ResourceCast(InitializerMissShaders[0]);

			const uint32 ShaderViewDescriptors = Shader->ResourceCounts.NumSRVs + Shader->ResourceCounts.NumUAVs;
			
			checkf(ShaderViewDescriptors == 0, TEXT("Default miss shader (miss shader 0) must take no parameters"));
		}

		// Add hit groups

		TArray<FD3D12RayTracingPipelineCache::FEntry*> HitGroupEntries;
		HitGroupShaders.Reserve(InitializerHitGroups.Num());
		HitGroupEntries.Reserve(InitializerHitGroups.Num());

		for (FRHIRayTracingShader* ShaderRHI : InitializerHitGroups)
		{
			FD3D12RayTracingShader* Shader = FD3D12DynamicRHI::ResourceCast(ShaderRHI);

			checkf(Shader, TEXT("A valid ray tracing hit group shader must be provided for all elements in the FRayTracingPipelineStateInitializer hit group table."));

			const uint32 ShaderViewDescriptors = Shader->ResourceCounts.NumSRVs + Shader->ResourceCounts.NumUAVs;
			MaxHitGroupViewDescriptors = FMath::Max(MaxHitGroupViewDescriptors, ShaderViewDescriptors);
			MaxLocalRootSignatureSize = FMath::Max(MaxLocalRootSignatureSize, Shader->pRootSignature->GetTotalRootSignatureSizeInBytes());

			FD3D12RayTracingPipelineCache::FEntry* ShaderCacheEntry = AddShaderCollection(Shader, FD3D12RayTracingPipelineCache::ECollectionType::HitGroup);

			HitGroupEntries.Add(ShaderCacheEntry);
			HitGroupShaders.Shaders.Add(Shader);
		}

		// Add callable shaders

		TArray<FD3D12RayTracingPipelineCache::FEntry*> CallableShaderEntries;
		CallableShaders.Reserve(InitializerCallableShaders.Num());
		CallableShaderEntries.Reserve(InitializerCallableShaders.Num());

		for (FRHIRayTracingShader* ShaderRHI : InitializerCallableShaders)
		{
			FD3D12RayTracingShader* Shader = FD3D12DynamicRHI::ResourceCast(ShaderRHI);

			checkf(Shader, TEXT("A valid ray tracing shader must be provided for all elements in the FRayTracingPipelineStateInitializer callable shader table."));
			checkf(!Shader->ResourceCounts.bGlobalUniformBufferUsed, TEXT("Global uniform buffers are not implemented for ray tracing callable shaders"));

			const uint32 ShaderViewDescriptors = Shader->ResourceCounts.NumSRVs + Shader->ResourceCounts.NumUAVs;
			MaxHitGroupViewDescriptors = FMath::Max(MaxHitGroupViewDescriptors, ShaderViewDescriptors);
			MaxLocalRootSignatureSize = FMath::Max(MaxLocalRootSignatureSize, Shader->pRootSignature->GetTotalRootSignatureSizeInBytes());

			FD3D12RayTracingPipelineCache::FEntry* ShaderCacheEntry = AddShaderCollection(Shader, FD3D12RayTracingPipelineCache::ECollectionType::Callable);

			CallableShaderEntries.Add(ShaderCacheEntry);
			CallableShaders.Shaders.Add(Shader);
		}

		// Wait for all compilation tasks to be complete and then gather the compiled collection descriptors

		CompileTime -= FPlatformTime::Cycles64();

		FTaskGraphInterface::Get().WaitUntilTasksComplete(CompileCompletionList);

		CompileTime += FPlatformTime::Cycles64();

		if (Initializer.bPartial)
		{
			// Partial pipelines don't have a linking phase, so exit immediately after compilation tasks are complete.
			return;
		}

		TArray<D3D12_EXISTING_COLLECTION_DESC> UniqueShaderCollectionDescs;
		UniqueShaderCollectionDescs.Reserve(MaxTotalShaders);
		for (FD3D12RayTracingPipelineCache::FEntry* Entry : UniqueShaderCollections)
		{
			UniqueShaderCollectionDescs.Add(Entry->GetCollectionDesc());
		}

		// Link final RTPSO from shader collections

		LinkTime -= FPlatformTime::Cycles64();

		if (BasePipeline)
		{
			if (UniqueShaderCollectionDescs.Num() == 0)
			{
				// New PSO does not actually have any new shaders that were not in the base
				StateObject = BasePipeline->StateObject.GetReference();
			}
			else
			{

				TArray<D3D12_STATE_SUBOBJECT> Subobjects;

				int32 SubobjectIndex = 0;
				Subobjects.Reserve(UniqueShaderCollectionDescs.Num() + 1);

				D3D12_STATE_OBJECT_CONFIG StateObjectConfig = {};
				StateObjectConfig.Flags = D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS;
				Subobjects.Add(D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &StateObjectConfig });

				for (const D3D12_EXISTING_COLLECTION_DESC& Collection : UniqueShaderCollectionDescs)
				{
					Subobjects.Add(D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION, &Collection });
				}

				D3D12_STATE_OBJECT_DESC Desc = {};
				Desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
				Desc.NumSubobjects = Subobjects.Num();
				Desc.pSubobjects = Subobjects.GetData();

				ID3D12Device7* Device7 = Device->GetDevice7();

				VERIFYD3D12RESULT(Device7->AddToStateObject(&Desc,
					BasePipeline->StateObject.GetReference(),
					IID_PPV_ARGS(StateObject.GetInitReference())));
			}
		}
		else
		{
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
		}

		LinkTime += FPlatformTime::Cycles64();

		HRESULT QueryInterfaceResult = StateObject->QueryInterface(IID_PPV_ARGS(PipelineProperties.GetInitReference()));
		checkf(SUCCEEDED(QueryInterfaceResult), TEXT("Failed to query pipeline properties from the ray tracing pipeline state object. Result=%08x"), QueryInterfaceResult);

		// Query shader identifiers from the pipeline state object

		check(HitGroupEntries.Num() == InitializerHitGroups.Num());

		auto GetEntryShaderIdentifier = [Properties = PipelineProperties.GetReference()](FD3D12RayTracingPipelineCache::FEntry* Entry) -> FD3D12ShaderIdentifier
		{
			if (Entry->Identifier.IsValid())
			{
				return Entry->Identifier;
			}
			else
			{
				return GetShaderIdentifier(Properties, Entry->GetPrimaryExportNameChars());
			}
		};

		HitGroupShaders.Identifiers.SetNumUninitialized(InitializerHitGroups.Num());
		for (int32 HitGroupIndex = 0; HitGroupIndex < HitGroupEntries.Num(); ++HitGroupIndex)
		{
			HitGroupShaders.Identifiers[HitGroupIndex] = GetEntryShaderIdentifier(HitGroupEntries[HitGroupIndex]);
		}

		RayGenShaders.Identifiers.SetNumUninitialized(RayGenShaderEntries.Num());
		for (int32 ShaderIndex = 0; ShaderIndex < RayGenShaderEntries.Num(); ++ShaderIndex)
		{
			RayGenShaders.Identifiers[ShaderIndex] = GetEntryShaderIdentifier(RayGenShaderEntries[ShaderIndex]);
		}

		MissShaders.Identifiers.SetNumUninitialized(MissShaderEntries.Num());
		for (int32 ShaderIndex = 0; ShaderIndex < MissShaderEntries.Num(); ++ShaderIndex)
		{
			MissShaders.Identifiers[ShaderIndex] = GetEntryShaderIdentifier(MissShaderEntries[ShaderIndex]);
		}

		CallableShaders.Identifiers.SetNumUninitialized(CallableShaderEntries.Num());
		for (int32 ShaderIndex = 0; ShaderIndex < CallableShaderEntries.Num(); ++ShaderIndex)
		{
			CallableShaders.Identifiers[ShaderIndex] = GetEntryShaderIdentifier(CallableShaderEntries[ShaderIndex]);
		}

		// Setup default shader binding table, which simply includes all provided RGS and MS plus a single default closest hit shader.
		// Hit record indexing and local resources access is disabled when using using this SBT.

		FD3D12RayTracingShaderTable::FInitializer SBTInitializer = {};
		SBTInitializer.NumRayGenShaders = RayGenShaders.Identifiers.Num();
		SBTInitializer.NumMissShaders = MissShaders.Identifiers.Num();
		SBTInitializer.NumCallableRecords = 0; // Default SBT does not support callable shaders
		SBTInitializer.NumMissRecords = 1;
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

		PipelineStackSize = PipelineProperties->GetPipelineStackSize();

		TotalCreationTime += FPlatformTime::Cycles64();

		// Report stats for pipelines that take a long time to create

#if !NO_LOGGING

		// Gather PSO stats
		ShaderStats.Reserve(UniqueShaderCollections.Num());
		for (FD3D12RayTracingPipelineCache::FEntry* Entry : UniqueShaderCollections)
		{
			FShaderStats Stats;
			Stats.Name = *(Entry->Shader->EntryPoint);
			Stats.ShaderSize = uint32(Entry->Shader->ShaderBytecode.GetShaderBytecode().BytecodeLength);
			Stats.CompileTimeMS = Entry->CompileTimeMS;

		#if PLATFORM_WINDOWS
			if (Entry->Shader->GetFrequency() == SF_RayGen)
			{
				Stats.StackSize = uint32(PipelineProperties->GetShaderStackSize(*(Entry->ExportNames[0])));
			}
		#endif // PLATFORM_WINDOWS

			ShaderStats.Add(Stats);
		}

		ShaderStats.Sort([](const FShaderStats& A, const FShaderStats& B) { return B.CompileTimeMS < A.CompileTimeMS; });

		const double TotalCreationTimeMS = 1000.0 * FPlatformTime::ToSeconds64(TotalCreationTime);
		const float CreationTimeWarningThresholdMS = 10.0f;
		if (TotalCreationTimeMS > CreationTimeWarningThresholdMS)
		{
			const double CompileTimeMS = 1000.0 * FPlatformTime::ToSeconds64(CompileTime);
			const double LinkTimeMS = 1000.0 * FPlatformTime::ToSeconds64(LinkTime);
			uint32 NumUniqueShaders = UniqueShaderCollections.Num();
			UE_LOG(LogD3D12RHI, Log,
				TEXT("Creating RTPSO with %d shaders (%d cached, %d new) took %.2f ms. Compile time %.2f ms, link time %.2f ms."),
				PipelineShaderHashes.Num(), NumCacheHits, NumUniqueShaders - NumCacheHits, (float)TotalCreationTimeMS, (float)CompileTimeMS, (float)LinkTimeMS);
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

	TSet<uint64> PipelineShaderHashes;

	uint32 PipelineStackSize = 0;

#if !NO_LOGGING
	struct FShaderStats
	{
		const TCHAR* Name = nullptr;
		float CompileTimeMS = 0;
		uint32 StackSize = 0;
		uint32 ShaderSize = 0;
	};
	TArray<FShaderStats> ShaderStats;
#endif // !NO_LOGGING
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

			FRHIRayTracingShader* OcclusionMSTable[] = { GetBuildInRayTracingShader<FDefaultPayloadMS>() };
			OcclusionInitializer.SetMissShaderTable(OcclusionMSTable);

			OcclusionInitializer.bAllowHitGroupIndexing = false;

			Occlusion = new FD3D12RayTracingPipelineState(Device, OcclusionInitializer);
		}

		// Intersection pipeline
		{
			FRayTracingPipelineStateInitializer IntersectionInitializer;

			FRHIRayTracingShader* IntersectionRGSTable[] = { GetBuildInRayTracingShader<FIntersectionMainRG>() };
			IntersectionInitializer.SetRayGenShaderTable(IntersectionRGSTable);

			FRHIRayTracingShader* IntersectionMSTable[] = { GetBuildInRayTracingShader<FDefaultPayloadMS>() };
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
	return new FD3D12RayTracingGeometry(Initializer);
}

FRayTracingSceneRHIRef FD3D12DynamicRHI::RHICreateRayTracingScene(const FRayTracingSceneInitializer& Initializer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateRayTracingScene);

	FD3D12Adapter& Adapter = GetAdapter();

	return new FD3D12RayTracingScene(&Adapter, Initializer);
}

FVertexBufferRHIRef FD3D12RayTracingGeometry::NullTransformBuffer;

FD3D12RayTracingGeometry::FD3D12RayTracingGeometry(const FRayTracingGeometryInitializer& Initializer)
{
	INC_DWORD_STAT(STAT_D3D12RayTracingAllocatedBLAS);

	DebugName = Initializer.DebugName;

	if(!FD3D12RayTracingGeometry::NullTransformBuffer.IsValid())
	{
		TResourceArray<float> NullTransformData;
		NullTransformData.SetNumZeroed(12);

		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.ResourceArray = &NullTransformData;

		FD3D12RayTracingGeometry::NullTransformBuffer = RHICreateVertexBuffer(NullTransformData.GetResourceDataSize(), BUF_Static, CreateInfo);
	}

	FMemory::Memzero(PostBuildInfoBufferReadbackFences);

	IndexOffsetInBytes = Initializer.IndexBufferOffset;
	TotalPrimitiveCount = Initializer.TotalPrimitiveCount;

	switch (Initializer.GeometryType)
	{
	case RTGT_Triangles:
		GeometryType = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		break;
	case RTGT_Procedural:
		GeometryType = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
		break;
	default:
		checkf(false, TEXT("Unexpected ray tracing geometry type"));
	}

	if (Initializer.bFastBuild)
	{
		BuildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
	}
	else
	{
		BuildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	}

	if (Initializer.bAllowUpdate)
	{
		BuildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	}

	if (!Initializer.bFastBuild && !Initializer.bAllowUpdate && GD3D12RayTracingAllowCompaction)
	{
		BuildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
	}

	if (GRayTracingDebugForceBuildMode == 1)
	{
		BuildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
		BuildFlags &= ~D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	}
	else if (GRayTracingDebugForceBuildMode == 2)
	{
		BuildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		BuildFlags &= ~D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
	}

	checkf(Initializer.Segments.Num() > 0, TEXT("Ray tracing geometry must be initialized with at least one segment."));
	Segments = TArray<FRayTracingGeometrySegment>(Initializer.Segments.GetData(), Initializer.Segments.Num());

#if DO_CHECK
	static constexpr uint32 IndicesPerPrimitive = 3; // Only triangle meshes are supported

	{
		uint32 ComputedPrimitiveCountForValidation = 0;

		for (const FRayTracingGeometrySegment& Segment : Segments)
		{
			checkf(Segment.VertexBuffer, TEXT("Position vertex buffer is required for ray tracing geometry"));
			checkf(Segment.VertexBufferStride, TEXT("Position vertex buffer is required for ray tracing geometry"));
			checkf(Segment.VertexBufferStride % 4 == 0, TEXT("Position vertex buffer stride must be aligned to 4 bytes for ByteAddressBuffer loads to work"));

			if (Initializer.GeometryType == RTGT_Triangles)
			{
				// #dxr_todo UE-72160: temporary constraints on vertex and index buffer formats (this will be relaxed when more flexible vertex/index fetching is implemented)
				checkf(Segment.VertexBufferElementType == VET_Float3
					|| Segment.VertexBufferElementType == VET_Float4, TEXT("Only float3/4 vertex buffers are currently implemented.")); // #dxr_todo UE-72160: support other vertex buffer formats
				checkf(Segment.VertexBufferStride >= 12, TEXT("Only deinterleaved float3 position vertex buffers are currently implemented.")); // #dxr_todo UE-72160: support interleaved vertex buffers
			}
			else if (Initializer.GeometryType == RTGT_Procedural)
			{
				checkf(Segment.VertexBufferStride >= (2 * sizeof(FVector)), TEXT("Procedural geometry vertex buffer must contain at least 2xFloat3 that defines 3D bounding boxes of primitives."));
			}

			ComputedPrimitiveCountForValidation += Segment.NumPrimitives;
			check(Segment.FirstPrimitive + Segment.NumPrimitives <= Initializer.TotalPrimitiveCount);

			if (Initializer.IndexBuffer)
			{
				check(Initializer.IndexBuffer->GetSize() >=
					(Segment.FirstPrimitive + Segment.NumPrimitives) * IndicesPerPrimitive * IndexStride + IndexOffsetInBytes);
			}
		}
		check(ComputedPrimitiveCountForValidation == Initializer.TotalPrimitiveCount);
	}
#endif // DO_CHECK

	RHIIndexBuffer = Initializer.IndexBuffer;
	SetDirty(FRHIGPUMask::All(), true);

	INC_DWORD_STAT_BY(STAT_D3D12RayTracingTrianglesBLAS, Initializer.TotalPrimitiveCount);
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
		FD3D12Buffer* IndexBuffer = CommandContext.RetrieveObject<FD3D12Buffer>(RHIIndexBuffer.GetReference());
		if (IndexBuffer->GetResource()->RequiresResourceStateTracking())
		{
			FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, IndexBuffer->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);
		}
	}

	for (const FRayTracingGeometrySegment& Segment : Segments)
	{
		const FVertexBufferRHIRef& RHIVertexBuffer = Segment.VertexBuffer;
		FD3D12Buffer* VertexBuffer = CommandContext.RetrieveObject<FD3D12Buffer>(RHIVertexBuffer.GetReference());
		if (VertexBuffer->GetResource()->RequiresResourceStateTracking())
		{
			FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, VertexBuffer->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);
		}
	}
}

static void CreateAccelerationStructureBuffers(
	TRefCountPtr<FD3D12MemBuffer>& AccelerationStructureBuffer,
	TRefCountPtr<FD3D12MemBuffer>&  ScratchBuffer,
	FD3D12Adapter* Adapter, uint32 GPUIndex,
	const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& PrebuildInfo,
	const TCHAR* DebugName)
{
	FRHIResourceCreateInfo CreateInfo;

	D3D12_RESOURCE_DESC AccelerationStructureBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
		PrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	CreateInfo.GPUMask = FRHIGPUMask::FromIndex(GPUIndex);
	CreateInfo.DebugName = DebugName;
	AccelerationStructureBuffer = Adapter->CreateRHIBuffer<FD3D12MemBuffer>(
		nullptr, AccelerationStructureBufferDesc, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT,
		0, AccelerationStructureBufferDesc.Width, BUF_AccelerationStructure, ED3D12ResourceStateMode::SingleState, CreateInfo);

	SetName(AccelerationStructureBuffer->GetResource(), DebugName);

	// #dxr_todo UE-72161: scratch buffers can be pooled and reused for different scenes and geometries
	D3D12_RESOURCE_DESC ScratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
		FMath::Max(PrebuildInfo.UpdateScratchDataSizeInBytes, PrebuildInfo.ScratchDataSizeInBytes), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	CreateInfo.GPUMask = FRHIGPUMask::FromIndex(GPUIndex);
	CreateInfo.DebugName = TEXT("ScratchBuffer");
	ScratchBuffer = Adapter->CreateRHIBuffer<FD3D12MemBuffer>(
		nullptr, ScratchBufferDesc, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT,
		0, ScratchBufferDesc.Width, BUF_UnorderedAccess, ED3D12ResourceStateMode::SingleState, CreateInfo);

	SetName(ScratchBuffer->GetResource(), TEXT("Acceleration structure scratch"));

	// Elevates the raytracing acceleration structure heap priority, which may help performance / stability in low memory conditions
	{
		ID3D12Pageable* HeapResources[] =
		{
			AccelerationStructureBuffer->GetResource()->GetPageable(),
			ScratchBuffer->GetResource()->GetPageable()
		};

		D3D12_RESIDENCY_PRIORITY HeapPriorities[] =
		{
			D3D12_RESIDENCY_PRIORITY_HIGH,
			D3D12_RESIDENCY_PRIORITY_HIGH
		};

		FD3D12Device* NodeDevice = Adapter->GetDevice(GPUIndex);
		NodeDevice->GetDevice5()->SetResidencyPriority(2, HeapResources, HeapPriorities);
	}
}

void FD3D12RayTracingGeometry::BuildAccelerationStructure(FD3D12CommandContext& CommandContext, EAccelerationStructureBuildMode BuildMode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildAccelerationStructure_BottomLevel);
	SCOPE_CYCLE_COUNTER(STAT_D3D12BuildBLAS);

	if (RHIIndexBuffer)
	{
		checkf(RHIIndexBuffer->GetStride() == 2 || RHIIndexBuffer->GetStride() == 4, TEXT("Index buffer must be 16 or 32 bit."));
	}

	IndexStride = RHIIndexBuffer ? RHIIndexBuffer->GetStride() : 0; // stride 0 means implicit triangle list for non-indexed geometry

	const bool bIsUpdate = BuildMode == EAccelerationStructureBuildMode::Update;
	static constexpr uint32 IndicesPerPrimitive = 3; // Only triangle meshes are supported

	// Array of geometry descriptions, one per segment (single-segment geometry is a common case).
	TArray<D3D12_RAYTRACING_GEOMETRY_DESC, TInlineAllocator<1>> Descs;

	Descs.Reserve(Segments.Num());

	FD3D12Buffer* IndexBuffer = CommandContext.RetrieveObject<FD3D12Buffer>(RHIIndexBuffer.GetReference());
	FD3D12Buffer* NullTransformBufferD3D12 = CommandContext.RetrieveObject<FD3D12Buffer>(NullTransformBuffer.GetReference());

	TArray<FHitGroupSystemParameters>& HitGroupSystemParametersForThisGPU = HitGroupSystemParameters[CommandContext.GetGPUIndex()];
	HitGroupSystemParametersForThisGPU.Reset(Segments.Num());

	for (const FRayTracingGeometrySegment& Segment : Segments)
	{
		D3D12_RAYTRACING_GEOMETRY_DESC Desc = {};

		Desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
		Desc.Type = GeometryType;

		if (Segment.bForceOpaque)
		{
			// Deny anyhit shader invocations when this segment is hit
			Desc.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		}

		if (!Segment.bAllowDuplicateAnyHitShaderInvocation)
		{
			// Allow only a single any-hit shader invocation per primitive
			Desc.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
		}

		FD3D12Buffer* VertexBuffer = CommandContext.RetrieveObject<FD3D12Buffer>(Segment.VertexBuffer.GetReference());
		
		FHitGroupSystemParameters SystemParameters = {};
		SystemParameters.RootConstants.SetVertexAndIndexStride(Segment.VertexBufferStride, IndexStride);
		SystemParameters.VertexBuffer = VertexBuffer->ResourceLocation.GetGPUVirtualAddress() + Segment.VertexBufferOffset;

		// Conservative estimate of the maximum number of elements in this VB.
		// Real used number will depend on the index buffer and is not available here right now.
		// #dxr_todo: Add explicit vertex count to FRayTracingGeometrySegment.
		const uint32 MaxSegmentVertices = (VertexBuffer->ResourceLocation.GetSize() - Segment.VertexBufferOffset) / Segment.VertexBufferStride;

		if (GeometryType == D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES)
		{
			switch (Segment.VertexBufferElementType)
			{
			case VET_Float4:
				// While the DXGI_FORMAT_R32G32B32A32_FLOAT format is not supported by DXR, since we manually load vertex 
				// data when we are building the BLAS, we can just rely on the vertex stride to offset the read index, 
				// and read only the 3 vertex components, and so use the DXGI_FORMAT_R32G32B32_FLOAT vertex format
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

			if (Segment.bEnabled)
			{
				Desc.Triangles.Transform3x4 = D3D12_GPU_VIRTUAL_ADDRESS(0);
			}
			else
			{
				Desc.Triangles.Transform3x4 = NullTransformBufferD3D12->ResourceLocation.GetGPUVirtualAddress();
			}

			if (IndexBuffer)
			{
				SystemParameters.IndexBuffer = IndexBuffer->ResourceLocation.GetGPUVirtualAddress();
				SystemParameters.RootConstants.IndexBufferOffsetInBytes = IndexOffsetInBytes + IndexStride * Segment.FirstPrimitive * IndicesPerPrimitive;

				Desc.Triangles.IndexFormat = IndexStride == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
				Desc.Triangles.IndexCount = Segment.NumPrimitives * IndicesPerPrimitive;
				Desc.Triangles.IndexBuffer = SystemParameters.IndexBuffer + SystemParameters.RootConstants.IndexBufferOffsetInBytes;
				Desc.Triangles.VertexCount = MaxSegmentVertices;

				IndexBuffer->GetResource()->UpdateResidency(CommandContext.CommandListHandle);
			}
			else
			{
				// Non-indexed geometry
				checkf(Segments.Num() == 1, TEXT("Non-indexed geometry with multiple segments is not implemented."));
				Desc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
				Desc.Triangles.IndexCount = 0;
				Desc.Triangles.IndexBuffer = D3D12_GPU_VIRTUAL_ADDRESS(0);
				Desc.Triangles.VertexCount = FMath::Min<uint32>(MaxSegmentVertices, TotalPrimitiveCount * 3);
			}

			Desc.Triangles.VertexBuffer.StartAddress = SystemParameters.VertexBuffer;
			Desc.Triangles.VertexBuffer.StrideInBytes = Segment.VertexBufferStride;
		}
		else if (GeometryType == D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS)
		{
			Desc.AABBs.AABBCount = Segment.NumPrimitives;
			Desc.AABBs.AABBs.StartAddress = SystemParameters.VertexBuffer;
			Desc.AABBs.AABBs.StrideInBytes = Segment.VertexBufferStride;
		}
		else
		{
			checkf(false, TEXT("Unexpected ray tracing geometry type"));
		}

		VertexBuffer->ResourceLocation.GetResource()->UpdateResidency(CommandContext.CommandListHandle);

		Descs.Add(Desc);

		if (GeometryType == D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES)
		{
			// #dxr_todo UE-72160: support various vertex buffer layouts (fetch/decode based on vertex stride and format)
			checkf(Segment.VertexBufferElementType == VET_Float3 || Segment.VertexBufferElementType == VET_Float4, TEXT("Only VET_Float3 and Float4 are currently implemented and tested. Other formats will be supported in the future."));
		}

		HitGroupSystemParametersForThisGPU.Add(SystemParameters);
	}

	const uint32 GPUIndex = CommandContext.GetGPUIndex();
	FD3D12Adapter* Adapter = CommandContext.GetParentAdapter();

	ID3D12Device5* RayTracingDevice = CommandContext.GetParentDevice()->GetDevice5();

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

		CreateAccelerationStructureBuffers(
			AccelerationStructureBuffers[GPUIndex],
			ScratchBuffers[GPUIndex],
			Adapter, GPUIndex,
			PrebuildInfo, 
			DebugName.IsValid() ? *DebugName.ToString() : TEXT("BLAS"));

		INC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, AccelerationStructureBuffers[GPUIndex]->GetSize());
		INC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, ScratchBuffers[GPUIndex]->GetSize());

		INC_MEMORY_STAT_BY(STAT_D3D12RayTracingBLASMemory, AccelerationStructureBuffers[GPUIndex]->GetSize());
		INC_MEMORY_STAT_BY(STAT_D3D12RayTracingBLASMemory, ScratchBuffers[GPUIndex]->GetSize());
	}

	// Compact BLAS if it is static
	bool bShouldCompactAfterBuild = BuildFlags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION && BuildFlags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE && !(BuildFlags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE);

	if (bShouldCompactAfterBuild)
	{
		D3D12_RESOURCE_DESC PostBuildInfoBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint64), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

		FRHIResourceCreateInfo CreateInfo;

		CreateInfo.GPUMask = FRHIGPUMask::FromIndex(GPUIndex);
		CreateInfo.DebugName = TEXT("PostBuildInfoBuffer");
		PostBuildInfoBuffers[GPUIndex] = Adapter->CreateRHIBuffer<FD3D12MemBuffer>(
			nullptr, PostBuildInfoBufferDesc, 8,
			0, PostBuildInfoBufferDesc.Width, BUF_UnorderedAccess | BUF_SourceCopy, ED3D12ResourceStateMode::MultiState, CreateInfo);

		SetName(PostBuildInfoBuffers[GPUIndex]->GetResource(), TEXT("PostBuildInfoBuffer"));

		FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, PostBuildInfoBuffers[GPUIndex].GetReference()->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0);

		PostBuildInfoStagingBuffers[GPUIndex] = RHICreateStagingBuffer();

		// Do not defer delete the staging buffer data because when readback is done we don't need
		// to keep it alive any longer - shouldn't even wait on frame fence
		PostBuildInfoBuffers[GPUIndex]->DoNoDeferDelete();
		PostBuildInfoStagingBuffers[GPUIndex]->DoNoDeferDelete();
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

		if (!bShouldCompactAfterBuild)
		{
			// Build
			RayTracingCommandList->BuildRaytracingAccelerationStructure(&BuildDesc, 0, nullptr);
		}
		else
		{
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC PostBuildInfoDesc = {};
			PostBuildInfoDesc.DestBuffer = PostBuildInfoBuffers[GPUIndex]->ResourceLocation.GetGPUVirtualAddress();
			PostBuildInfoDesc.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;

			// Build with post build info
			RayTracingCommandList->BuildRaytracingAccelerationStructure(&BuildDesc, 1, &PostBuildInfoDesc);

			CommandContext.RHICopyToStagingBuffer(PostBuildInfoBuffers[GPUIndex], PostBuildInfoStagingBuffers[GPUIndex], 0, sizeof(uint64));

			const FD3D12Fence& Fence = CommandContext.GetParentDevice()->GetCommandListManager().GetFence();
			PostBuildInfoBufferReadbackFences[GPUIndex] = FMath::Max(PostBuildInfoBufferReadbackFences[GPUIndex], Fence.GetCurrentFence());
		}

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

void FD3D12RayTracingGeometry::ConditionalCompactAccelerationStructure(FD3D12CommandContext& CommandContext)
{
	const bool bShouldCompactAfterBuild = BuildFlags& D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION&& BuildFlags& D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE && !(BuildFlags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE);

	if (!bShouldCompactAfterBuild)
	{
		return;
	}

	const uint32 GPUIndex = CommandContext.GetGPUIndex();

	// return the buffers are null (mostly because the BLAS has been compacted)
	if (PostBuildInfoBuffers[GPUIndex] == nullptr)
	{
		return;
	}

	FD3D12Adapter* Adapter = CommandContext.GetParentAdapter();

	const FD3D12Fence& Fence = CommandContext.GetParentDevice()->GetCommandListManager().GetFence();

	// Ensure that our builds & copies have finished on GPU
	if (PostBuildInfoBufferReadbackFences[GPUIndex] > Fence.GetLastCompletedFenceFast())
	{
		return;
	}

	uint64 SizeAfterCompaction = 0;

	{
		// Read size and release readback buffers.
		SizeAfterCompaction = *(uint64*)PostBuildInfoStagingBuffers[GPUIndex]->Lock(0, sizeof(uint64));

		PostBuildInfoStagingBuffers[GPUIndex]->Unlock();
		PostBuildInfoBuffers[GPUIndex] = nullptr;
		PostBuildInfoStagingBuffers[GPUIndex] = nullptr;
		PostBuildInfoBufferReadbackFences[GPUIndex] = MAX_uint64;
	}

	ensureMsgf(SizeAfterCompaction, TEXT("Compacted acceleration structure size is expected to be non-zero. This error suggests that GPU readback synchronization is broken."));

	if (SizeAfterCompaction == 0)
	{
		return;
	}

	DEC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, AccelerationStructureBuffers[GPUIndex]->GetSize());
	DEC_MEMORY_STAT_BY(STAT_D3D12RayTracingBLASMemory, AccelerationStructureBuffers[GPUIndex]->GetSize());

	// Move old AS into this temporary variable which gets released when this function returns
	TRefCountPtr<FD3D12MemBuffer> OldAccelerationStructure = MoveTemp(AccelerationStructureBuffers[GPUIndex]);

	{
		FRHIResourceCreateInfo CreateInfo;

		D3D12_RESOURCE_DESC AccelerationStructureBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(SizeAfterCompaction, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

		CreateInfo.GPUMask = FRHIGPUMask::FromIndex(GPUIndex);
		CreateInfo.DebugName = TEXT("AccelerationStructureBuffer");
		AccelerationStructureBuffers[GPUIndex] = Adapter->CreateRHIBuffer<FD3D12MemBuffer>(
			nullptr, AccelerationStructureBufferDesc, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT,
			0, AccelerationStructureBufferDesc.Width, BUF_AccelerationStructure, ED3D12ResourceStateMode::SingleState, CreateInfo);

		INC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, AccelerationStructureBuffers[GPUIndex]->GetSize());
		INC_MEMORY_STAT_BY(STAT_D3D12RayTracingBLASMemory, AccelerationStructureBuffers[GPUIndex]->GetSize());

		SetName(AccelerationStructureBuffers[GPUIndex]->GetResource(),
			DebugName.IsValid() ? *DebugName.ToString() : TEXT("BLAS"));

		OldAccelerationStructure->GetResource()->UpdateResidency(CommandContext.CommandListHandle);
		AccelerationStructureBuffers[GPUIndex]->GetResource()->UpdateResidency(CommandContext.CommandListHandle);

		ID3D12GraphicsCommandList4* RayTracingCommandList = CommandContext.CommandListHandle.RayTracingCommandList();
		RayTracingCommandList->CopyRaytracingAccelerationStructure(
			AccelerationStructureBuffers[GPUIndex]->ResourceLocation.GetGPUVirtualAddress(),
			OldAccelerationStructure->ResourceLocation.GetGPUVirtualAddress(),
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
	}
}

FD3D12RayTracingScene::FD3D12RayTracingScene(FD3D12Adapter* Adapter, const FRayTracingSceneInitializer& Initializer)
	: FD3D12AdapterChild(Adapter)
{
	INC_DWORD_STAT(STAT_D3D12RayTracingAllocatedTLAS);

	DebugName = Initializer.DebugName;

	ShaderResourceView = Adapter->CreateLinkedObject<FD3D12ShaderResourceView>(FRHIGPUMask::All(), [&](FD3D12Device* Device)
	{
		return new FD3D12ShaderResourceView(Device);
	});

	checkf(Initializer.Lifetime == RTSL_SingleFrame, TEXT("Only single-frame ray tracing scenes are currently implemented."));

	Lifetime = Initializer.Lifetime;
	CreatedFrameFenceValue = Adapter->GetFrameFence().GetCurrentFence();
	Instances = TArray<FRayTracingGeometryInstance>(Initializer.Instances.GetData(), Initializer.Instances.Num());
	ShaderSlotsPerGeometrySegment = Initializer.ShaderSlotsPerGeometrySegment;

	// Compute geometry segment count prefix sum to be later used in GetHitRecordBaseIndex()
	SegmentPrefixSum.Reserve(Instances.Num());
	NumTotalSegments = 0;

	Experimental::TSherwoodSet<FD3D12RayTracingGeometry*> UniqueGeometries;

	for (const FRayTracingGeometryInstance& Instance : Instances)
	{
		FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(Instance.GeometryRHI);
		SegmentPrefixSum.Add(NumTotalSegments);
		NumTotalSegments += Geometry->Segments.Num();

		bool bIsAlreadyInSet = false;
		UniqueGeometries.Add(Geometry, &bIsAlreadyInSet);
		if (!bIsAlreadyInSet)
		{
			Geometries.Add(Geometry);
		}
	}

	// Reserve space for all segments
	HitGroupSystemParametersCache.Reserve(NumTotalSegments);

	NumCallableShaderSlots = Initializer.NumCallableShaderSlots;
	NumMissShaderSlots = FMath::Max<uint32>(1, Initializer.NumMissShaderSlots);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildAccelerationStructure_TopLevel);
	SCOPE_CYCLE_COUNTER(STAT_D3D12BuildTLAS);

	TRefCountPtr<FD3D12StructuredBuffer> InstanceBuffer;
	TRefCountPtr<FD3D12MemBuffer> ScratchBuffer;

	const uint32 GPUIndex = CommandContext.GetGPUIndex();
	FD3D12Adapter* Adapter = CommandContext.GetParentAdapter();
	ID3D12Device5* RayTracingDevice = CommandContext.GetParentDevice()->GetDevice5();

	const uint32 NumSceneInstances = Instances.Num();

	uint32 NumDxrInstances = 0;
	BaseInstancePrefixSum.Reserve(NumSceneInstances);
	for (uint32 InstanceIndex = 0; InstanceIndex < NumSceneInstances; ++InstanceIndex)
	{
		const FRayTracingGeometryInstance& Instance = Instances[InstanceIndex];
		BaseInstancePrefixSum.Add(NumDxrInstances);
		NumDxrInstances += Instance.NumTransforms;
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS PrebuildDescInputs = {};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO PrebuildInfo = {};
	PrebuildDescInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	PrebuildDescInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	PrebuildDescInputs.NumDescs = NumDxrInstances;
	PrebuildDescInputs.Flags = BuildFlags;

	RayTracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&PrebuildDescInputs, &PrebuildInfo);

	TRefCountPtr<FD3D12MemBuffer>& AccelerationStructureBuffer = AccelerationStructureBuffers[GPUIndex];

	CreateAccelerationStructureBuffers(
		AccelerationStructureBuffer,
		ScratchBuffer,
		Adapter, GPUIndex,
		PrebuildInfo,
		DebugName.IsValid() ? *DebugName.ToString() : TEXT("TLAS"));

	INC_MEMORY_STAT_BY(STAT_D3D12RayTracingUsedVideoMemory, AccelerationStructureBuffer->GetSize());
	INC_MEMORY_STAT_BY(STAT_D3D12RayTracingTLASMemory, AccelerationStructureBuffer->GetSize());

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

	if (NumSceneInstances)
	{
		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.GPUMask = FRHIGPUMask::FromIndex(GPUIndex);
		
		D3D12_RESOURCE_DESC InstanceBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
			sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * PrebuildDescInputs.NumDescs,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

		// #dxr_todo multi state only when bShouldCopyIndirectInstances is set - will still need transition to copy_src for lock behind but this can be done on the complete pool in theory (have to check cost)
		InstanceBuffer = Adapter->CreateRHIBuffer<FD3D12StructuredBuffer>(
			nullptr, InstanceBufferDesc, D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT,
			sizeof(D3D12_RAYTRACING_INSTANCE_DESC), InstanceBufferDesc.Width, BUF_UnorderedAccess, ED3D12ResourceStateMode::MultiState, CreateInfo);

		D3D12_RAYTRACING_INSTANCE_DESC* MappedData = (D3D12_RAYTRACING_INSTANCE_DESC*)Adapter->GetOwningRHI()->LockBuffer(
			nullptr, InstanceBuffer.GetReference(), InstanceBuffer->GetSize(), InstanceBuffer->GetUsage(), 0, InstanceBufferDesc.Width, RLM_WriteOnly);

		check(MappedData);

		uint32 DxrInstanceIndex = 0;

		bool bShouldCopyIndirectInstances = false;

		{
			TArray<FD3D12ResidencyHandle*>& GeometryResidencyHandlesForThisGPU = GeometryResidencyHandles[GPUIndex];

			GeometryResidencyHandlesForThisGPU.Reset(0);

			Experimental::TSherwoodSet<FD3D12ResidencyHandle*> UniqueResidencyHandles;

			auto AddResidencyHandleForResource = [&UniqueResidencyHandles, &GeometryResidencyHandlesForThisGPU] (FD3D12Resource* Resource)
			{
			#if ENABLE_RESIDENCY_MANAGEMENT
				FD3D12ResidencyHandle* ResidencyHandle = Resource->GetResidencyHandle();
				if (D3DX12Residency::IsInitialized(ResidencyHandle))
				{
					bool bIsAlreadyInSet = false;
					UniqueResidencyHandles.Add(ResidencyHandle, &bIsAlreadyInSet);
					if (!bIsAlreadyInSet)
					{
						GeometryResidencyHandlesForThisGPU.Add(ResidencyHandle);
					}
				}
			#endif // ENABLE_RESIDENCY_MANAGEMENT
			};

			for (TRefCountPtr<FD3D12RayTracingGeometry>& Geometry : Geometries)
			{
				checkf(!Geometry->IsDirty(CommandContext.GetGPUIndex()),
					TEXT("Acceleration structures for all geometries must be built before building the top level acceleration structure for the scene."));

				Geometry->ConditionalCompactAccelerationStructure(CommandContext);

				AddResidencyHandleForResource(Geometry->AccelerationStructureBuffers[GPUIndex]->GetResource());

				if (Geometry->RHIIndexBuffer)
				{
					FD3D12Buffer* IndexBuffer = CommandContext.RetrieveObject<FD3D12Buffer>(Geometry->RHIIndexBuffer.GetReference());
					AddResidencyHandleForResource(IndexBuffer->GetResource());
				}

				for (const FRayTracingGeometrySegment& Segment : Geometry->Segments)
				{
					if (Segment.VertexBuffer)
					{
						FD3D12Buffer* VertexBuffer = CommandContext.RetrieveObject<FD3D12Buffer>(Segment.VertexBuffer.GetReference());
						AddResidencyHandleForResource(VertexBuffer->GetResource());
					}
				}
			}
		}

		for (uint32 InstanceIndex = 0; InstanceIndex < NumSceneInstances; ++InstanceIndex)
		{
			const FRayTracingGeometryInstance& Instance = Instances[InstanceIndex];
			FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(Instance.GeometryRHI);

			// make a copy of system parameters to they can optimized fetch during SBT building (only done for GPU0)
			check(Geometry->HitGroupSystemParameters[0].Num() > 0);
			HitGroupSystemParametersCache.Append(Geometry->HitGroupSystemParameters[0]);

			D3D12_RAYTRACING_INSTANCE_DESC InstanceDesc = {};

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

			bool bHasGPUTransforms = Instance.GPUTransformsSRV.IsValid();

			const uint32 NumTransforms = Instance.NumTransforms;

			checkf(Instance.UserData.Num() == 0 || Instance.UserData.Num() == 1 || Instance.UserData.Num() == NumTransforms, 
				TEXT("User data array must be either be empty (implicit 0 is assumed), contain a single entry (same value applied to all instances) or contain one entry per entry in Transforms array."));

			const bool bUseUniqueUserData = Instance.UserData.Num() > 1;
			const uint32 CommonUserData = Instance.UserData.Num() == 1 ? Instance.UserData[0] : 0;

			bShouldCopyIndirectInstances |= bHasGPUTransforms;

			for (uint32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
			{
				InstanceDesc.InstanceID = bUseUniqueUserData ? Instance.UserData[TransformIndex] : CommonUserData;

				if (!bHasGPUTransforms)
				{
					FMatrix TransformTransposed = Instance.Transforms[TransformIndex].GetTransposed();
					
					// Ensure the last row of the original Transform is <0,0,0,1>
					check((TransformTransposed.M[3][0] == 0)
						&& (TransformTransposed.M[3][1] == 0)
						&& (TransformTransposed.M[3][2] == 0)
						&& (TransformTransposed.M[3][3] == 1));

					FMemory::Memcpy(&InstanceDesc.Transform, &TransformTransposed.M[0][0], sizeof(InstanceDesc.Transform));
				}

				MappedData[DxrInstanceIndex++] = InstanceDesc;
			}

			TotalPrimitiveCount += uint64(Geometry->TotalPrimitiveCount) * NumTransforms;
		}

		Adapter->GetOwningRHI()->UnlockBuffer(nullptr, InstanceBuffer.GetReference(), InstanceBuffer->GetUsage());

		InstanceBuffer->GetResource()->UpdateResidency(CommandContext.CommandListHandle);

		if(bShouldCopyIndirectInstances)
		{
			TRHICommandList_RecursiveHazardous<FD3D12CommandContext> RHICmdList(&CommandContext);
			FUnorderedAccessViewRHIRef InstancesDescUAV = RHICreateUnorderedAccessView(InstanceBuffer, false, false);

			FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, InstanceBuffer.GetReference()->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0);

			RHICmdList.BeginUAVOverlap(InstancesDescUAV);

			uint32 DescOffset = 0;
			for (uint32 InstanceIndex = 0; InstanceIndex < NumSceneInstances; ++InstanceIndex)
			{
				const FRayTracingGeometryInstance& Instance = Instances[InstanceIndex];
				FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(Instance.GeometryRHI);

				const uint32 NumTransforms = Instance.NumTransforms;

				if (NumTransforms > 0 && Instance.GPUTransformsSRV.IsValid())
				{
					FRHIShaderResourceView* TransformsSRV = Instance.GPUTransformsSRV.GetReference();

					CopyRayTracingGPUInstances(RHICmdList, NumTransforms, DescOffset, TransformsSRV, InstancesDescUAV);
				}

				DescOffset += NumTransforms;
			}

			RHICmdList.EndUAVOverlap(InstancesDescUAV);
		}

		FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, InstanceBuffer.GetReference()->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);
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
#if ENABLE_RESIDENCY_MANAGEMENT

	// Skip redundant resource residency updates when a scene is repeatedly used on the same command list
	if (LastCommandListID == CommandContext.CommandListHandle.GetCommandListID())
	{
		return;
	}

	const uint32 GPUIndex = CommandContext.GetGPUIndex();
	AccelerationStructureBuffers[GPUIndex]->GetResource()->UpdateResidency(CommandContext.CommandListHandle);

	FD3D12ResidencySet& ResidencySet = CommandContext.CommandListHandle.GetResidencySet();

	for (FD3D12ResidencyHandle* ResidencyHandle : GeometryResidencyHandles[GPUIndex])
	{
		D3DX12Residency::Insert(ResidencySet, ResidencyHandle);
	}

	LastCommandListID = CommandContext.CommandListHandle.GetCommandListID();

#endif // ENABLE_RESIDENCY_MANAGEMENT
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

	TRACE_CPUPROFILER_EVENT_SCOPE(FindOrCreateShaderTable);
	SCOPE_CYCLE_COUNTER(STAT_D3D12CreateShaderTable);

	FD3D12RayTracingShaderTable* CreatedShaderTable = new FD3D12RayTracingShaderTable();
	ID3D12Device5* RayTracingDevice = Device->GetDevice5();
	const uint32 GPUIndex = Device->GetGPUIndex();

	const uint32 NumHitGroupSlots = Pipeline->bAllowHitGroupIndexing ? NumTotalSegments * ShaderSlotsPerGeometrySegment : 0;

	checkf(Pipeline->MaxLocalRootSignatureSize >= sizeof(FHitGroupSystemParameters), TEXT("All local root signatures are expected to contain ray tracing system root parameters (2x root buffers + 4x root DWORD)"));

	FD3D12RayTracingShaderTable::FInitializer SBTInitializer = {};
	SBTInitializer.NumRayGenShaders = Pipeline->RayGenShaders.Identifiers.Num();
	SBTInitializer.NumMissShaders = Pipeline->MissShaders.Identifiers.Num();
	SBTInitializer.NumMissRecords = NumMissShaderSlots;
	SBTInitializer.NumHitRecords = NumHitGroupSlots;
	SBTInitializer.NumCallableRecords = NumCallableShaderSlots;
	SBTInitializer.LocalRootDataSize = Pipeline->MaxLocalRootSignatureSize;
	SBTInitializer.MaxViewDescriptorsPerRecord = Pipeline->MaxHitGroupViewDescriptors;

	CreatedShaderTable->Init(SBTInitializer, Device);
	CreatedShaderTable->SetRayGenIdentifiers(Pipeline->RayGenShaders.Identifiers);
	CreatedShaderTable->SetMissIdentifiers(Pipeline->MissShaders.Identifiers);
	CreatedShaderTable->SetDefaultHitGroupIdentifier(Pipeline->HitGroupShaders.Identifiers[0]);

	ShaderTables[GPUIndex].Add(Pipeline, CreatedShaderTable);

	return CreatedShaderTable;
}

void FD3D12CommandContext::RHIBuildAccelerationStructures(const TArrayView<const FAccelerationStructureBuildParams> Params)
{
	// Update geometry vertex buffers

	for (const FAccelerationStructureBuildParams& P : Params)
	{
		FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(P.Geometry.GetReference());

		if (P.Segments.Num())
		{
			checkf(P.Segments.Num() == Geometry->Segments.Num(),
				TEXT("If updated segments are provided, they must exactly match existing geometry segments. Only vertex buffer bindings may change."));

			for (int32 i = 0; i < P.Segments.Num(); ++i)
			{
				Geometry->Segments[i].VertexBuffer            = P.Segments[i].VertexBuffer;
				Geometry->Segments[i].VertexBufferElementType = P.Segments[i].VertexBufferElementType;
				Geometry->Segments[i].VertexBufferStride      = P.Segments[i].VertexBufferStride;
				Geometry->Segments[i].VertexBufferOffset      = P.Segments[i].VertexBufferOffset;
			}
		}
	}

	// Transition all VBs and IBs to readable state

	for (const FAccelerationStructureBuildParams& P : Params)
	{
		FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(P.Geometry.GetReference());
		Geometry->TransitionBuffers(*this);
	}

	CommandListHandle.FlushResourceBarriers();

	// Then do all work

	for (const FAccelerationStructureBuildParams& P : Params)
	{
		FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(P.Geometry.GetReference());
		Geometry->SetDirty(GetGPUMask(), true);

		Geometry->BuildAccelerationStructure(*this, P.BuildMode);
	}

	// Add a UAV barrier after each acceleration structure build batch.
	// This is required because there are currently no explicit read/write barriers
	// for acceleration structures, but we need to ensure that all commands
	// are complete before BLAS is used again on the GPU.

	CommandListHandle.AddUAVBarrier();
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
	FD3D12RayTracingGlobalResourceBinder(FD3D12CommandContext& InCommandContext, FD3D12RayTracingDescriptorCache& InDescriptorCache)
		: CommandContext(InCommandContext)
		, DescriptorCache(InDescriptorCache)
	{
		check(IsInRHIThread() || !IsRunningRHIInSeparateThread());
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

#if USE_STATIC_ROOT_SIGNATURE
	D3D12_CPU_DESCRIPTOR_HANDLE CreateTransientConstantBufferView(D3D12_GPU_VIRTUAL_ADDRESS Address, uint32 DataSize)
	{
		checkf(0, TEXT("Loose parameters and transient constant buffers are not implemented for global ray tracing shaders (raygen, miss, callable)"));
		return D3D12_CPU_DESCRIPTOR_HANDLE {};
	}
#endif // USE_STATIC_ROOT_SIGNATURE

	D3D12_GPU_VIRTUAL_ADDRESS CreateTransientConstantBuffer(const void* Data, uint32 DataSize)
	{
		checkf(0, TEXT("Loose parameters and transient constant buffers are not implemented for global ray tracing shaders (raygen, miss, callable)"));
		return (D3D12_GPU_VIRTUAL_ADDRESS)0;
	}

	void AddResourceReference(FD3D12Resource* D3D12Resource, FRHIResource* RHIResource)
	{
		D3D12Resource->UpdateResidency(CommandContext.CommandListHandle);
	}

	void AddResourceTransition(FD3D12ShaderResourceView* SRV)
	{
		FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, SRV, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	void AddResourceTransition(FD3D12UnorderedAccessView* UAV)
	{
		FD3D12DynamicRHI::TransitionResource(CommandContext.CommandListHandle, UAV, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}

	FD3D12Device* GetDevice()
	{
		return CommandContext.GetParentDevice();
	}

	FD3D12CommandContext& CommandContext;
	FD3D12RayTracingDescriptorCache& DescriptorCache;
	static constexpr uint32 WorkerIndex = 0;
};

struct FD3D12RayTracingLocalResourceBinder
{
	FD3D12RayTracingLocalResourceBinder(FD3D12Device& InDevice, FD3D12RayTracingShaderTable& InShaderTable, const FD3D12RootSignature& InRootSignature, uint32 InRecordIndex, uint32 InWorkerIndex)
		: Device(InDevice)
		, ShaderTable(InShaderTable)
		, DescriptorCache(*InShaderTable.DescriptorCache)
		, RootSignature(InRootSignature)
		, RecordIndex(InRecordIndex)
		, WorkerIndex(InWorkerIndex)
	{
		check(InShaderTable.DescriptorCache != nullptr);
		check(WorkerIndex < InShaderTable.MaxBindingWorkers);
		check(WorkerIndex < DescriptorCache.MaxBindingWorkers);
		check(RecordIndex != ~0u);
	}

	void SetRootDescriptor(uint32 BaseSlotIndex, uint32 DescriptorIndex, D3D12_GPU_VIRTUAL_ADDRESS Address)
	{
		const uint32 BindOffsetBase = RootSignature.GetBindSlotOffsetInBytes(BaseSlotIndex);
		const uint32 DescriptorSize = uint32(sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
		const uint32 CurrentOffset = BindOffsetBase + DescriptorIndex * DescriptorSize;
		ShaderTable.SetLocalShaderParameters(RecordIndex, CurrentOffset, Address);
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
		const uint32 BindOffset = RootSignature.GetBindSlotOffsetInBytes(SlotIndex);
		ShaderTable.SetLocalShaderParameters(RecordIndex, BindOffset, DescriptorTable);
	}

#if USE_STATIC_ROOT_SIGNATURE
	D3D12_CPU_DESCRIPTOR_HANDLE CreateTransientConstantBufferView(D3D12_GPU_VIRTUAL_ADDRESS Address, uint32 DataSize)
	{
		FD3D12ConstantBufferView* BufferView = new FD3D12ConstantBufferView(GetDevice(), nullptr);
		BufferView->Create(Address, DataSize);
		ShaderTable.TransientCBVs.Add(BufferView);
		return BufferView->OfflineDescriptorHandle;
	}
#endif // USE_STATIC_ROOT_SIGNATURE

	D3D12_GPU_VIRTUAL_ADDRESS CreateTransientConstantBuffer(const void* Data, uint32 DataSize)
	{
		// If we see a significant number of transient allocations coming through this path, we should consider
		// caching constant buffer blocks inside ShaderTable and linearly sub-allocate from them.
		// If the amount of data is relatively small, it may also be possible to use root constants and avoid extra allocations entirely.

		FD3D12FastConstantAllocator& Allocator = Device.GetParentAdapter()->GetTransientUniformBufferAllocator();
		FD3D12ResourceLocation ResourceLocation(&Device);

	#if USE_STATIC_ROOT_SIGNATURE
		void* MappedData = Allocator.Allocate(DataSize, ResourceLocation, nullptr);
	#else // USE_STATIC_ROOT_SIGNATURE
		void* MappedData = Allocator.Allocate(DataSize, ResourceLocation);
	#endif // USE_STATIC_ROOT_SIGNATURE

		FMemory::Memcpy(MappedData, Data, DataSize);

		ShaderTable.AddResourceReference(ResourceLocation.GetResource(), WorkerIndex);

		return ResourceLocation.GetGPUVirtualAddress();
	}

	void AddResourceReference(FD3D12Resource* D3D12Resource, FRHIResource* RHIResource)
	{
		ShaderTable.AddResourceReference(D3D12Resource, WorkerIndex);
	}

	void AddResourceTransition(FD3D12ShaderResourceView* SRV)
	{
		if (SRV->RequiresResourceStateTracking())
		{
			ShaderTable.AddResourceTransition(SRV, WorkerIndex);
		}
	}

	void AddResourceTransition(FD3D12UnorderedAccessView* UAV)
	{
		if (UAV->GetResource()->RequiresResourceStateTracking())
		{
			ShaderTable.AddResourceTransition(UAV, WorkerIndex);
		}
	}

	FD3D12Device* GetDevice()
	{
		return &Device;
	}

	FD3D12Device& Device;
	FD3D12RayTracingShaderTable& ShaderTable;
	FD3D12RayTracingDescriptorCache& DescriptorCache;
	const FD3D12RootSignature& RootSignature;
	uint32 RecordIndex = ~0u;
	uint32 WorkerIndex = 0;
};

template <typename ResourceBinderType>
static bool SetRayTracingShaderResources(
	const FD3D12RayTracingShader* Shader,
	uint32 InNumTextures, FRHITexture* const* Textures,
	uint32 InNumSRVs, FRHIShaderResourceView* const* SRVs,
	uint32 InNumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
	uint32 InNumSamplers, FRHISamplerState* const* Samplers,
	uint32 InNumUAVs, FRHIUnorderedAccessView* const* UAVs,
	uint32 InLooseParameterDataSize, const void* InLooseParameterData,
	ResourceBinderType& Binder)
{
	const FD3D12RootSignature* RootSignature = Shader->pRootSignature;

#if USE_STATIC_ROOT_SIGNATURE
	D3D12_CPU_DESCRIPTOR_HANDLE LocalCBVs[MAX_CBS];
#else // USE_STATIC_ROOT_SIGNATURE
	D3D12_GPU_VIRTUAL_ADDRESS LocalCBVs[MAX_CBS];
#endif // USE_STATIC_ROOT_SIGNATURE

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

	uint32 GPUIndex = Binder.GetDevice()->GetGPUIndex();
	for (uint32 SRVIndex = 0; SRVIndex < InNumTextures; ++SRVIndex)
	{
		FRHITexture* Resource = Textures[SRVIndex];
		if (Resource)
		{
			FD3D12TextureBase* Texture = FD3D12CommandContext::RetrieveTextureBase(Resource, GPUIndex);
			FD3D12ShaderResourceView* SRV = Texture->GetShaderResourceView();
			LocalSRVs[SRVIndex] = SRV->GetView();
			BoundSRVMask |= 1ull << SRVIndex;

			ReferencedResources.Add({ Texture->GetResource(), Resource });
			Binder.AddResourceTransition(SRV);
		}
	}

	for (uint32 SRVIndex = 0; SRVIndex < InNumSRVs; ++SRVIndex)
	{
		FRHIShaderResourceView* Resource = SRVs[SRVIndex];
		if (Resource)
		{
			FD3D12ShaderResourceView* SRV = FD3D12CommandContext::RetrieveObject<FD3D12ShaderResourceView>(Resource, GPUIndex);
			LocalSRVs[SRVIndex] = SRV->GetView();
			BoundSRVMask |= 1ull << SRVIndex;

			ReferencedResources.Add({ SRV->GetResource(), Resource });
			Binder.AddResourceTransition(SRV);
		}
	}

	for (uint32 CBVIndex = 0; CBVIndex < InNumUniformBuffers; ++CBVIndex)
	{
		FRHIUniformBuffer* Resource = UniformBuffers[CBVIndex];
		if (Resource)
		{
			FD3D12UniformBuffer* CBV = FD3D12CommandContext::RetrieveObject<FD3D12UniformBuffer>(Resource, GPUIndex);
		#if USE_STATIC_ROOT_SIGNATURE
			LocalCBVs[CBVIndex] = CBV->View->OfflineDescriptorHandle;
		#else // USE_STATIC_ROOT_SIGNATURE
			LocalCBVs[CBVIndex] = CBV->ResourceLocation.GetGPUVirtualAddress();
		#endif // USE_STATIC_ROOT_SIGNATURE
			BoundCBVMask |= 1ull << CBVIndex;

			ReferencedResources.Add({ CBV->ResourceLocation.GetResource(), Resource });
		}
	}

	for (uint32 SamplerIndex = 0; SamplerIndex < InNumSamplers; ++SamplerIndex)
	{
		FRHISamplerState* Resource = Samplers[SamplerIndex];
		if (Resource)
		{
			LocalSamplers[SamplerIndex] = FD3D12CommandContext::RetrieveObject<FD3D12SamplerState>(Resource, GPUIndex)->Descriptor;
			BoundSamplerMask |= 1ull << SamplerIndex;
		}
	}

	for (uint32 UAVIndex = 0; UAVIndex < InNumUAVs; ++UAVIndex)
	{
		FRHIUnorderedAccessView* Resource = UAVs[UAVIndex];
		if (Resource)
		{
			FD3D12UnorderedAccessView* UAV = FD3D12CommandContext::RetrieveObject<FD3D12UnorderedAccessView>(Resource, GPUIndex);
			LocalUAVs[UAVIndex] = UAV->GetView();
			BoundUAVMask |= 1ull << UAVIndex;

			ReferencedResources.Add({ UAV->GetResource(), Resource });
			Binder.AddResourceTransition(UAV);
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

					FRHITexture* RHITexture = (FRHITexture*)Resources[ResourceIndex].GetReference();

					checkf(RHITexture != nullptr, TEXT("Missing required texture binding for slot %d in uniform buffer %d (UB layout name: '%s')"),
						BindIndex, BufferIndex, *(Buffer->GetLayout().GetDebugName()));

					FD3D12ShaderResourceView* SRV = FD3D12CommandContext::RetrieveTextureBase(RHITexture, GPUIndex)->GetShaderResourceView();
					if (!ensure(SRV))
					{
						SRV = FD3D12CommandContext::RetrieveTextureBase(GBlackTexture->TextureRHI, GPUIndex)->GetShaderResourceView();
					}
					check(SRV != nullptr);

					LocalSRVs[BindIndex] = SRV->GetView();
					BoundSRVMask |= 1ull << BindIndex;

					ReferencedResources.Add({ SRV->GetResource(), SRV });
					Binder.AddResourceTransition(SRV);

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

					FRHIShaderResourceView* RHISRV = (FRHIShaderResourceView*)Resources[ResourceIndex].GetReference();

					checkf(RHISRV != nullptr, TEXT("Missing required shader resource view binding for slot %d in uniform buffer %d (UB layout name: '%s')"),
						BindIndex, BufferIndex, *(Buffer->GetLayout().GetDebugName()));

					FD3D12ShaderResourceView* SRV = FD3D12CommandContext::RetrieveObject<FD3D12ShaderResourceView>(RHISRV, GPUIndex);
					check(SRV != nullptr);

					LocalSRVs[BindIndex] = SRV->GetView();
					BoundSRVMask |= 1ull << BindIndex;

					ReferencedResources.Add({ SRV->GetResource(), SRV });
					Binder.AddResourceTransition(SRV);

					ResourceInfo = *ResourceInfos++;
				} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			}
		}

		// UAVs

		{
			const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
			const TArray<uint32>& ResourceMap = ShaderResourceTable.UnorderedAccessViewMap;
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

					FRHIUnorderedAccessView* RHIUAV = (FRHIUnorderedAccessView*)Resources[ResourceIndex].GetReference();

					checkf(RHIUAV != nullptr, TEXT("Missing required unordered access view binding for slot %d in uniform buffer %d (UB layout name: '%s')"),
						BindIndex, BufferIndex, *(Buffer->GetLayout().GetDebugName()));

					FD3D12UnorderedAccessView* UAV = FD3D12CommandContext::RetrieveObject<FD3D12UnorderedAccessView>(RHIUAV, GPUIndex);
					check(UAV != nullptr);

					LocalUAVs[BindIndex] = UAV->GetView();
					BoundUAVMask |= 1ull << BindIndex;

					ReferencedResources.Add({ UAV->GetResource(), UAV });
					Binder.AddResourceTransition(UAV);

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

					FRHISamplerState* RHISampler = (FRHISamplerState*)Resources[ResourceIndex].GetReference();

					checkf(RHISampler != nullptr, TEXT("Missing required sampler binding for slot %d in uniform buffer %d (UB layout name: '%s')"),
						BindIndex, BufferIndex, *(Buffer->GetLayout().GetDebugName()));

					FD3D12SamplerState* Sampler = FD3D12CommandContext::RetrieveObject<FD3D12SamplerState>(RHISampler, GPUIndex);
					check(Sampler != nullptr);

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
		D3D12_GPU_VIRTUAL_ADDRESS BufferAddress = Binder.CreateTransientConstantBuffer(InLooseParameterData, InLooseParameterDataSize);

	#if USE_STATIC_ROOT_SIGNATURE
		LocalCBVs[CBVIndex] = Binder.CreateTransientConstantBufferView(BufferAddress, InLooseParameterDataSize);
	#else // USE_STATIC_ROOT_SIGNATURE
		LocalCBVs[CBVIndex] = BufferAddress;
	#endif // USE_STATIC_ROOT_SIGNATURE

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

	FD3D12RayTracingDescriptorCache& DescriptorCache = Binder.DescriptorCache;
	const uint32 WorkerIndex = Binder.WorkerIndex;

	const uint32 NumSRVs = Shader->ResourceCounts.NumSRVs;
	if (NumSRVs)
	{
		const int32 DescriptorTableBaseIndex = DescriptorCache.AllocateDescriptorTable(LocalSRVs, NumSRVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, WorkerIndex);
		if (DescriptorTableBaseIndex < 0)
		{
			return false;
		}

		const uint32 BindSlot = RootSignature->SRVRDTBindSlot(SF_Compute);
		check(BindSlot != 0xFF);

		const D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptorTableBaseGPU = DescriptorCache.ViewHeap.GetDescriptorGPU(DescriptorTableBaseIndex);
		Binder.SetRootDescriptorTable(BindSlot, ResourceDescriptorTableBaseGPU);
	}

	const uint32 NumUAVs = Shader->ResourceCounts.NumUAVs;
	if (NumUAVs)
	{
		const int32 DescriptorTableBaseIndex = DescriptorCache.AllocateDescriptorTable(LocalUAVs, NumUAVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, WorkerIndex);
		if (DescriptorTableBaseIndex < 0)
		{
			return false;
		}

		const uint32 BindSlot = RootSignature->UAVRDTBindSlot(SF_Compute);
		check(BindSlot != 0xFF);

		const D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptorTableBaseGPU = DescriptorCache.ViewHeap.GetDescriptorGPU(DescriptorTableBaseIndex);
		Binder.SetRootDescriptorTable(BindSlot, ResourceDescriptorTableBaseGPU);
	}

	const uint32 NumCBVs = Shader->ResourceCounts.NumCBs;
	if (Shader->ResourceCounts.NumCBs)
	{
	#if USE_STATIC_ROOT_SIGNATURE

		const uint32 DescriptorTableBaseIndex = DescriptorCache.AllocateDescriptorTable(LocalCBVs, NumCBVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, WorkerIndex);
		const uint32 BindSlot = RootSignature->CBVRDTBindSlot(SF_Compute);
		check(BindSlot != 0xFF);

		const D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptorTableBaseGPU = DescriptorCache.ViewHeap.GetDescriptorGPU(DescriptorTableBaseIndex);
		Binder.SetRootDescriptorTable(BindSlot, ResourceDescriptorTableBaseGPU);

	#else // USE_STATIC_ROOT_SIGNATURE

		checkf(RootSignature->CBVRDTBindSlot(SF_Compute) == 0xFF, TEXT("Root CBV descriptor tables are not implemented for ray tracing shaders."));

		const uint32 BindSlot = RootSignature->CBVRDBaseBindSlot(SF_Compute);
		check(BindSlot != 0xFF);

		for (uint32 i = 0; i < Shader->ResourceCounts.NumCBs; ++i)
		{
			const uint64 SlotMask = (1ull << i);
			D3D12_GPU_VIRTUAL_ADDRESS BufferAddress = (BoundCBVMask & SlotMask) ? LocalCBVs[i] : 0;
			Binder.SetRootCBV(BindSlot, i, BufferAddress);
		}

	#endif // USE_STATIC_ROOT_SIGNATURE
	}

	// Bind samplers

	const uint32 NumSamplers = Shader->ResourceCounts.NumSamplers;
	if (NumSamplers)
	{
		const int32 DescriptorTableBaseIndex = DescriptorCache.AllocateDescriptorTable(LocalSamplers, NumSamplers, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, WorkerIndex);
		if (DescriptorTableBaseIndex < 0)
		{
			return false;
		}

		const uint32 BindSlot = RootSignature->SamplerRDTBindSlot(SF_Compute);
		check(BindSlot != 0xFF);

		const D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptorTableBaseGPU = DescriptorCache.SamplerHeap.GetDescriptorGPU(DescriptorTableBaseIndex);
		Binder.SetRootDescriptorTable(BindSlot, ResourceDescriptorTableBaseGPU);
	}

	for (const FResourceEntry& Entry : ReferencedResources)
	{
		Binder.AddResourceReference(Entry.D3D12Resource, Entry.RHIResource);
	}

	return true;
}

template <typename ResourceBinderType>
static bool SetRayTracingShaderResources(
	const FD3D12RayTracingShader* Shader,
	const FRayTracingShaderBindings& ResourceBindings,
	ResourceBinderType& Binder)
{
	static_assert(
		sizeof(ResourceBindings.SRVs) / sizeof(*ResourceBindings.SRVs) == MAX_SRVS,
		"Ray Tracing Shader Bindings SRV array size must match D3D12 RHI Limit");
	static_assert(
		sizeof(ResourceBindings.UniformBuffers) / sizeof(*ResourceBindings.UniformBuffers) == MAX_CBS,
		"Ray Tracing Shader Bindings Uniform Buffer array size must match D3D12 RHI Limit");
	static_assert(
		sizeof(ResourceBindings.Samplers) / sizeof(*ResourceBindings.Samplers) == MAX_SAMPLERS,
		"Ray Tracing Shader Bindings Sampler array size must match D3D12 RHI Limit");
	static_assert(
		sizeof(ResourceBindings.UAVs) / sizeof(*ResourceBindings.UAVs) == MAX_UAVS,
		"Ray Tracing Shader Bindings UAV array size must match D3D12 RHI Limit");

	return SetRayTracingShaderResources(Shader,
		UE_ARRAY_COUNT(ResourceBindings.Textures), ResourceBindings.Textures,
		UE_ARRAY_COUNT(ResourceBindings.SRVs), ResourceBindings.SRVs,
		UE_ARRAY_COUNT(ResourceBindings.UniformBuffers), ResourceBindings.UniformBuffers,
		UE_ARRAY_COUNT(ResourceBindings.Samplers), ResourceBindings.Samplers,
		UE_ARRAY_COUNT(ResourceBindings.UAVs), ResourceBindings.UAVs,
		0, nullptr, // loose parameters
		Binder);
}

static void DispatchRays(FD3D12CommandContext& CommandContext,
	const FRayTracingShaderBindings& GlobalBindings,
	const FD3D12RayTracingPipelineState* Pipeline,
	uint32 RayGenShaderIndex,
	FD3D12RayTracingShaderTable* OptShaderTable,
	const D3D12_DISPATCH_RAYS_DESC& DispatchDesc)
{
	 SCOPE_CYCLE_COUNTER(STAT_D3D12DispatchRays);

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

	bool bResourcesBound = false;
	if (OptShaderTable && OptShaderTable->DescriptorCache)
	{
		FD3D12RayTracingDescriptorCache* DescriptorCache = OptShaderTable->DescriptorCache;
		check(DescriptorCache != nullptr);

		DescriptorCache->SetDescriptorHeaps(CommandContext);
		FD3D12RayTracingGlobalResourceBinder ResourceBinder(CommandContext, *DescriptorCache);
		bResourcesBound = SetRayTracingShaderResources(RayGenShader, GlobalBindings, ResourceBinder);

		OptShaderTable->UpdateResidency(CommandContext);
	}
	else
	{
		FD3D12RayTracingDescriptorCache TransientDescriptorCache(CommandContext.GetParentDevice());
		TransientDescriptorCache.Init(MAX_SRVS + MAX_UAVS, MAX_SAMPLERS);
		TransientDescriptorCache.SetDescriptorHeaps(CommandContext);
		FD3D12RayTracingGlobalResourceBinder ResourceBinder(CommandContext, TransientDescriptorCache);
		bResourcesBound = SetRayTracingShaderResources(RayGenShader, GlobalBindings, ResourceBinder);
	}

	if (bResourcesBound)
	{
		if (OptShaderTable)
		{
			OptShaderTable->TransitionResources(CommandContext);
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

	Scene->UpdateResidency(*this);

	FD3D12RayTracingShader* RayGenShader = FD3D12DynamicRHI::ResourceCast(RayGenShaderRHI);
	const int32 RayGenShaderIndex = Pipeline->RayGenShaders.Find(RayGenShader->GetHash());
	checkf(RayGenShaderIndex != INDEX_NONE,
		TEXT("RayGen shader '%s' is not present in the given ray tracing pipeline. ")
		TEXT("All RayGen shaders must be declared when creating RTPSO."),
			*(RayGenShader->EntryPoint));

	D3D12_DISPATCH_RAYS_DESC DispatchDesc = ShaderTable->GetDispatchRaysDesc(RayGenShaderIndex, 0, Pipeline->bAllowHitGroupIndexing);

	DispatchDesc.Width = Width;
	DispatchDesc.Height = Height;
	DispatchDesc.Depth = 1;

	DispatchRays(*this, GlobalResourceBindings, Pipeline, RayGenShaderIndex, ShaderTable, DispatchDesc);
}

static void SetRayTracingHitGroup(
	FD3D12Device* Device,
	FD3D12RayTracingShaderTable* ShaderTable,
	FD3D12RayTracingScene* Scene,
	FD3D12RayTracingPipelineState* Pipeline,
	uint32 InstanceIndex, uint32 SegmentIndex, uint32 ShaderSlot, uint32 HitGroupIndex,
	uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
	uint32 LooseParameterDataSize, const void* LooseParameterData,
	uint32 UserData,
	uint32 WorkerIndex)
{
	checkf(ShaderSlot < Scene->ShaderSlotsPerGeometrySegment, TEXT("Shader slot is invalid. Make sure that ShaderSlotsPerGeometrySegment is correct on FRayTracingSceneInitializer."));

	const uint32 RecordIndex = Scene->GetHitRecordBaseIndex(InstanceIndex, SegmentIndex) + ShaderSlot;
	const uint32 GPUIndex = Device->GetGPUIndex();

	FHitGroupSystemParameters SystemParameters;
	if (GPUIndex == 0)
	{
		uint32 PrefixedSegmentIndex = Scene->SegmentPrefixSum[InstanceIndex];
		SystemParameters = Scene->HitGroupSystemParametersCache[PrefixedSegmentIndex + SegmentIndex];
	}
	else
	{
		const FRayTracingGeometryInstance& Instance = Scene->Instances[InstanceIndex];
		const FD3D12RayTracingGeometry* Geometry = FD3D12DynamicRHI::ResourceCast(Instance.GeometryRHI);
		const TArray<FHitGroupSystemParameters>& HitGroupSystemParametersForThisGPU = Geometry->HitGroupSystemParameters[GPUIndex];

		SystemParameters = HitGroupSystemParametersForThisGPU[SegmentIndex];
	}

	SystemParameters.RootConstants.BaseInstanceIndex = Scene->BaseInstancePrefixSum[InstanceIndex];
	SystemParameters.RootConstants.UserData = UserData;
	ShaderTable->SetHitGroupSystemParameters(RecordIndex, SystemParameters);

	const FD3D12RayTracingShader* Shader = Pipeline->HitGroupShaders.Shaders[HitGroupIndex];

	FD3D12RayTracingShaderTable::FShaderRecordCacheKey CacheKey;

	const bool bCanUseRecordCache = GRayTracingCacheShaderRecords
		&& Scene->Lifetime == RTSL_SingleFrame
		&& LooseParameterDataSize == 0 // loose parameters end up in unique constant buffers, so SBT records can't be shared
		&& NumUniformBuffers > 0 // there is no benefit from cache if no resources are being bound
		&& NumUniformBuffers <= CacheKey.MaxUniformBuffers;

	if (bCanUseRecordCache)
	{
		CacheKey = FD3D12RayTracingShaderTable::FShaderRecordCacheKey(NumUniformBuffers, UniformBuffers, HitGroupIndex);

		uint32* ExistingRecordIndex = ShaderTable->ShaderRecordCache[WorkerIndex].Find(CacheKey);
		if (ExistingRecordIndex)
		{
			// Simply copy local shader parameters from existing SBT record and set the shader identifier, skipping resource binding work.
			const uint32 OffsetFromRootSignatureStart = sizeof(FHitGroupSystemParameters);
			ShaderTable->SetLocalShaderIdentifier(RecordIndex, Pipeline->HitGroupShaders.Identifiers[HitGroupIndex]);
			ShaderTable->CopyLocalShaderParameters(RecordIndex, *ExistingRecordIndex, OffsetFromRootSignatureStart);
			return;
		}
	}

	FD3D12RayTracingLocalResourceBinder ResourceBinder(*Device, *ShaderTable, *(Shader->pRootSignature), RecordIndex, WorkerIndex);
	const bool bResourcesBound = SetRayTracingShaderResources(Shader,
		0, nullptr, // Textures
		0, nullptr, // SRVs
		NumUniformBuffers, UniformBuffers,
		0, nullptr, // Samplers
		0, nullptr, // UAVs
		LooseParameterDataSize, LooseParameterData,
		ResourceBinder);

	if (bCanUseRecordCache && bResourcesBound)
	{
		ShaderTable->ShaderRecordCache[WorkerIndex].Add(CacheKey, RecordIndex);
	}

	ShaderTable->SetLocalShaderIdentifier(RecordIndex, 
		bResourcesBound
		? Pipeline->HitGroupShaders.Identifiers[HitGroupIndex]
		: FD3D12ShaderIdentifier::Null);
}

void FD3D12CommandContext::RHISetRayTracingHitGroups(
	FRHIRayTracingScene* InScene, FRHIRayTracingPipelineState* InPipeline,
	uint32 NumBindings, const FRayTracingLocalShaderBindings* Bindings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SetRayTracingHitGroups);
	SCOPE_CYCLE_COUNTER(STAT_D3D12SetRayTracingHitGroups);

	FD3D12RayTracingScene* Scene = FD3D12DynamicRHI::ResourceCast(InScene);
	FD3D12RayTracingPipelineState* Pipeline = FD3D12DynamicRHI::ResourceCast(InPipeline);
	FD3D12Device* Device = GetParentDevice();

	FD3D12RayTracingShaderTable* ShaderTable = Scene->FindOrCreateShaderTable(Pipeline, Device);
	checkf(ShaderTable->LocalShaderTableOffset == ShaderTable->HitGroupShaderTableOffset,
		TEXT("Hit shader records are assumed to be at the beginning of local shader table"));

	FGraphEventArray TaskList;

	const uint32 NumWorkerThreads = FTaskGraphInterface::Get().GetNumWorkerThreads();
	const uint32 MaxTasks = FApp::ShouldUseThreadingForPerformance() 
		? FMath::Min<uint32>(NumWorkerThreads, FD3D12RayTracingScene::MaxBindingWorkers)
		: 1;

	const uint32 MinItemsPerTask = 1024u;
	const uint32 NumTasks = FMath::Min(MaxTasks, FMath::Max(1u, FMath::DivideAndRoundUp(NumBindings, MinItemsPerTask)));

	volatile int32 SharedCounter = 0;
	const int32 IncrementCount = 128;

	auto BindingTask = [Bindings, Device, ShaderTable, Scene, Pipeline, &SharedCounter, IncrementCount, NumBindings](uint32 WorkerIndex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetRayTracingHitGroups_Task);
	 
		int32 BaseIndex = 0;
		while (BaseIndex < (int32)NumBindings)
		{
			BaseIndex = FPlatformAtomics::InterlockedAdd(&SharedCounter, IncrementCount);
			int32 EndIndex = FMath::Min(BaseIndex + IncrementCount, (int32)NumBindings);

			for (int32 CurrentIndex = BaseIndex; CurrentIndex < EndIndex; ++CurrentIndex)
			{
				const FRayTracingLocalShaderBindings& Binding = Bindings[CurrentIndex];
				SetRayTracingHitGroup(Device, ShaderTable, Scene, Pipeline,
					Binding.InstanceIndex,
					Binding.SegmentIndex,
					Binding.ShaderSlot,
					Binding.ShaderIndexInPipeline,
					Binding.NumUniformBuffers,
					Binding.UniformBuffers,
					Binding.LooseParameterDataSize,
					Binding.LooseParameterData,
					Binding.UserData,
					WorkerIndex);
			}
		}
	};

	TaskList.Reserve(NumTasks);

	// Create and kick any parallel tasks first and run task 0 on this thread
	for (uint32 TaskIndex = 1; TaskIndex < NumTasks; ++TaskIndex)
	{
		TaskList.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
			[TaskIndex, &BindingTask]()
		{
			BindingTask(TaskIndex);
		}, TStatId(), nullptr, ENamedThreads::AnyThread));
	}

	// Run task 0 on this thread, in parallel with any workers
	BindingTask(0);

	FTaskGraphInterface::Get().WaitUntilTasksComplete(TaskList, ENamedThreads::AnyThread);
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

	const uint32 WorkerIndex = 0;
	SetRayTracingHitGroup(GetParentDevice(), ShaderTable, Scene, Pipeline,
		InstanceIndex,
		SegmentIndex,
		ShaderSlot,
		HitGroupIndex,
		NumUniformBuffers,
		UniformBuffers,
		LooseParameterDataSize,
		LooseParameterData,
		UserData,
		WorkerIndex);
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

	const uint32 UserDataOffset = offsetof(FHitGroupSystemParameters, RootConstants) + offsetof(FHitGroupSystemRootConstants, UserData);
	ShaderTable->SetLocalShaderParameters(RecordIndex, UserDataOffset, UserData);

	const FD3D12RayTracingShader* Shader = Pipeline->CallableShaders.Shaders[ShaderIndexInPipeline];

	const uint32 WorkerIndex = 0;
	FD3D12RayTracingLocalResourceBinder ResourceBinder(*GetParentDevice(), *ShaderTable, *(Shader->pRootSignature), RecordIndex, WorkerIndex);
	const bool bResourcesBound = SetRayTracingShaderResources(Shader,
		0, nullptr, // Textures
		0, nullptr, // SRVs
		NumUniformBuffers, UniformBuffers,
		0, nullptr, // Samplers
		0, nullptr, // UAVs
		0, nullptr, // Loose parameters
		ResourceBinder);

	ShaderTable->SetLocalShaderIdentifier(RecordIndex, 
		bResourcesBound 
		? Pipeline->CallableShaders.Identifiers[ShaderIndexInPipeline]
		: FD3D12ShaderIdentifier::Null);
}

void FD3D12CommandContext::RHISetRayTracingMissShader(
	FRHIRayTracingScene* InScene, uint32 ShaderSlotInScene,
	FRHIRayTracingPipelineState* InPipeline, uint32 ShaderIndexInPipeline,
	uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
	uint32 UserData)
{
	FD3D12RayTracingScene* Scene = FD3D12DynamicRHI::ResourceCast(InScene);
	FD3D12RayTracingPipelineState* Pipeline = FD3D12DynamicRHI::ResourceCast(InPipeline);

	checkf(ShaderSlotInScene < Scene->NumMissShaderSlots, TEXT("Shader slot is invalid. Make sure that NumMissShaderSlots is correct on FRayTracingSceneInitializer."));
	checkf(ShaderSlotInScene > 0, TEXT("Shader slot 0 is reserved for the default MissShader."));

	FD3D12RayTracingShaderTable* ShaderTable = Scene->FindOrCreateShaderTable(Pipeline, GetParentDevice());

	const uint32 RecordIndex = ShaderTable->MissShaderRecordIndexOffset + ShaderSlotInScene;

	const uint32 UserDataOffset = offsetof(FHitGroupSystemParameters, RootConstants) + offsetof(FHitGroupSystemRootConstants, UserData);
	ShaderTable->SetLocalShaderParameters(RecordIndex, UserDataOffset, UserData);

	const FD3D12RayTracingShader* Shader = Pipeline->MissShaders.Shaders[ShaderIndexInPipeline];

	uint32 WorkerIndex = 0;
	FD3D12RayTracingLocalResourceBinder ResourceBinder(*GetParentDevice(), *ShaderTable, *(Shader->pRootSignature), RecordIndex, WorkerIndex);
	const bool bResourcesBound = SetRayTracingShaderResources(Shader,
		0, nullptr, // Textures
		0, nullptr, // SRVs
		NumUniformBuffers, UniformBuffers,
		0, nullptr, // Samplers
		0, nullptr, // UAVs
		0, nullptr, // Loose parameters
		ResourceBinder);

	ShaderTable->SetLocalShaderIdentifier(RecordIndex,
		bResourcesBound
		? Pipeline->MissShaders.Identifiers[ShaderIndexInPipeline]
		: FD3D12ShaderIdentifier::Null);
}
#endif // D3D12_RHI_RAYTRACING
