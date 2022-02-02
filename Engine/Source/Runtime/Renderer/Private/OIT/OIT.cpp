// Copyright Epic Games, Inc. All Rights Reserved.

#include "OIT.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "SceneTextureParameters.h"
#include "SceneRendering.h"

#include "ShaderPrintParameters.h"
#include "ShaderDebug.h"
#include "ShaderPrint.h"
#include "ScreenPass.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// Variables

static int32 GOIT_SortObjectTriangles = 1;
static FAutoConsoleVariableRef CVarOIT_PerObjectSorting(TEXT("r.OIT.SortObjectTriangles"), GOIT_SortObjectTriangles, TEXT("Enable per-instance triangle sorting to avoid invalid triangle ordering (experimental)."));

static int32 GOIT_Debug = 0;
static FAutoConsoleVariableRef CVarOIT_Debug(TEXT("r.OIT.Debug"), GOIT_Debug, TEXT("Enable per-instance triangle sorting debug rendering."));

static int32 GOIT_Pool = 0;
static int32 GOIT_PoolReleaseThreshold = 100;
static FAutoConsoleVariableRef CVarOIT_Pool(TEXT("r.OIT.Pool"), GOIT_Pool, TEXT("Enable index buffer pool allocation which reduce creation/deletion time by re-use buffers."));
static FAutoConsoleVariableRef CVarOIT_PoolReleaseThreshold(TEXT("r.OIT.Pool.ReleaseFrameThreshold"), GOIT_PoolReleaseThreshold, TEXT("Number of frame after which unused buffer are released."));

///////////////////////////////////////////////////////////////////////////////////////////////////
// OIT Debug

struct FOITDebugData
{
	static const EPixelFormat Format = PF_R32_UINT;

	FRDGBufferRef Buffer = nullptr; // First element is counter, then element are: (NumPrim/Type/Size)

	uint32 VisibleInstances = 0;
	uint32 VisiblePrimitives = 0;
	uint32 VisibleIndexSizeInBytes = 0;

	uint32 AllocatedBuffers = 0;
	uint32 AllocatedIndexSizeInBytes = 0;

	uint32 UnusedBuffers = 0;
	uint32 UnusedIndexSizeInBytes = 0;

	uint32 TotalEntries = 0;

	bool IsValid() const { return Buffer != nullptr; }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// OIT Index buffer

class FSortedIndexBuffer : public FIndexBuffer
{
public:
	static const uint32 SliceCount = 32;

	FSortedIndexBuffer(uint32 InId, const FBufferRHIRef& InSourceIndexBuffer, uint32 InNumIndices, const TCHAR* InDebugName)
	: SourceIndexBuffer(InSourceIndexBuffer)
	, NumIndices(InNumIndices)
	, Id(InId)
	, DebugName(InDebugName) { }

	virtual void InitRHI() override
	{
		check(SourceIndexBuffer);
		const uint32 BytesPerElement = SourceIndexBuffer->GetStride();
		check(BytesPerElement == 2 || BytesPerElement == 4);
		const EPixelFormat Format = BytesPerElement == 2 ? PF_R16_UINT : PF_R32_UINT;

		FRHIResourceCreateInfo CreateInfo(DebugName);
		IndexBufferRHI = RHICreateIndexBuffer(BytesPerElement /*Stride*/, NumIndices * BytesPerElement, BUF_UnorderedAccess | BUF_ShaderResource, ERHIAccess::VertexOrIndexBuffer, CreateInfo);
		SortedIndexUAV = RHICreateUnorderedAccessView(IndexBufferRHI, Format);
		SourceIndexSRV = RHICreateShaderResourceView(SourceIndexBuffer, BytesPerElement, Format);
	}

	virtual void ReleaseRHI() override
	{
		IndexBufferRHI.SafeRelease();
		SortedIndexUAV.SafeRelease();
		SourceIndexSRV.SafeRelease();
	}

	static constexpr uint32 InvalidId = ~0;

	FBufferRHIRef SourceIndexBuffer = nullptr;
	uint32 NumIndices = 0;
	uint32 Id = FSortedIndexBuffer::InvalidId;
	uint32 LastUsedFrameId = 0;
	const TCHAR* DebugName = nullptr;

	FShaderResourceViewRHIRef  SourceIndexSRV = nullptr;
	FUnorderedAccessViewRHIRef SortedIndexUAV = nullptr;
};

static bool IsOITSupported(EShaderPlatform InShaderPlatform)
{
	return RHISupportsComputeShaders(InShaderPlatform) && !IsMobilePlatform(InShaderPlatform) && !FDataDrivenShaderPlatformInfo::GetIsHlslcc(InShaderPlatform);
}

static void RemoveAllocation(FSortedIndexBuffer* InBuffer)
{
	InBuffer->ReleaseResource();
	delete InBuffer;
	InBuffer = nullptr;
}

static void TrimSortedIndexBuffers(TArray<FSortedIndexBuffer*>& FreeBuffers, uint32 FrameId)
{
	uint32 FreeCount = FreeBuffers.Num();
	for (uint32 FreeIt = 0; FreeIt < FreeCount;)
	{
		check(FreeBuffers[FreeIt]);
		const uint32 LastFrameId = FreeBuffers[FreeIt]->LastUsedFrameId;
		if (LastFrameId != 0)
		{
			const int32 ElapsedFrame = FMath::Abs(int32(FrameId) - int32(LastFrameId));
			if (ElapsedFrame > GOIT_PoolReleaseThreshold)
			{
				FSortedIndexBuffer* Buffer = FreeBuffers[FreeIt];
				RemoveAllocation(Buffer);
				FreeBuffers[FreeIt] = FreeBuffers[FreeCount - 1];
				--FreeCount;
				FreeBuffers.SetNum(FreeCount);
				continue;
			}
		}
		++FreeIt;
	}
}

FSortedTriangleData FOITSceneData::Allocate(const FIndexBuffer* InSource, EPrimitiveType PrimitiveType, uint32 InFirstIndex, uint32 InNumPrimitives)
{
	check(IsInRenderingThread());
	check(InSource && InSource->IndexBufferRHI);
	check(PrimitiveType == PT_TriangleList || PrimitiveType == PT_TriangleStrip);

	// Find a free slot, or create a new one
	FSortedTriangleData* Out = nullptr;
	uint32 FreeSlot = FSortedIndexBuffer::InvalidId;
	FreeSlots.Dequeue(FreeSlot);
	if (FreeSlot != FSortedIndexBuffer::InvalidId)
	{
		Out = &Allocations[FreeSlot];
	}
	else
	{
		FreeSlot = Allocations.Num();
		Out = &Allocations.AddDefaulted_GetRef();
	}

	// Linear scan if there are some free resource which are large enough
	const uint32 NumIndices = InNumPrimitives * 3; // Sorted index always has triangle list topology
	FSortedIndexBuffer* OITIndexBuffer = nullptr;
	if (GOIT_Pool>0)
	{
		for (uint32 FreeIt=0,FreeCount=FreeBuffers.Num(); FreeIt<FreeCount; ++FreeIt)
		{		
			FSortedIndexBuffer* FreeBuffer = FreeBuffers[FreeIt];
			if (FreeBuffer != nullptr && FreeBuffer->NumIndices >= NumIndices && FreeBuffer->Id == FSortedIndexBuffer::InvalidId)
			{			
				OITIndexBuffer = FreeBuffer;
				OITIndexBuffer->Id = FreeSlot;
				FreeBuffers[FreeIt] = FreeBuffers[FreeCount - 1];
				FreeBuffers.SetNum(FreeCount - 1);
				break;
			}
		}
	}

	// Otherwise create a new one
	if (OITIndexBuffer == nullptr)
	{
		OITIndexBuffer = new FSortedIndexBuffer(FreeSlot, InSource->IndexBufferRHI, NumIndices, TEXT("OIT::SortedIndexBuffer"));
		OITIndexBuffer->InitResource();	
	}
	Out->NumPrimitives = InNumPrimitives;
	Out->NumIndices = NumIndices;
	Out->SourceFirstIndex = InFirstIndex;
	Out->SortedFirstIndex = 0u;
	Out->SourcePrimitiveType = PrimitiveType;
	Out->SortedPrimitiveType = PT_TriangleList;
	Out->SourceIndexBuffer = InSource;
	Out->SortedIndexBuffer = OITIndexBuffer;
	Out->SortedIndexUAV = OITIndexBuffer->SortedIndexUAV;
	Out->SourceIndexSRV = OITIndexBuffer->SourceIndexSRV;

	return *Out;
}

void FOITSceneData::Deallocate(FIndexBuffer* InIndexBuffer)
{
	if (InIndexBuffer == nullptr)
	{
		return;
	}

	FSortedIndexBuffer* OITIndexBuffer = (FSortedIndexBuffer*)InIndexBuffer;
	const uint32 Slot = OITIndexBuffer->Id;
	if (Slot < uint32(Allocations.Num()))
	{
		if (GOIT_Pool > 0)
		{
			OITIndexBuffer->Id = FSortedIndexBuffer::InvalidId;
			OITIndexBuffer->LastUsedFrameId = FrameIndex;
			FreeBuffers.Add(OITIndexBuffer);
			Allocations[Slot] = FSortedTriangleData();
		}
		else
		{
			FSortedTriangleData& In = Allocations[Slot];
			In.SortedIndexUAV = nullptr;
			In.SourceIndexSRV = nullptr;
			RemoveAllocation((FSortedIndexBuffer*)In.SortedIndexBuffer);
			In = FSortedTriangleData();
		}
		FreeSlots.Enqueue(Slot);
	}
}

static uint32 GetGroupSize()
{
	if (IsRHIDeviceNVIDIA())
	{
		return 32u;
	}
	else  // IsRHIDeviceAMD() and others
	{
		return 64u;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Sort triangle indices to order them front-to-back or back-to-front

class FOITSortTriangleIndex_ScanCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOITSortTriangleIndex_ScanCS);
	SHADER_USE_PARAMETER_STRUCT(FOITSortTriangleIndex_ScanCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_GROUP_SIZE", 32, 64); 
	class FDebug : SHADER_PERMUTATION_BOOL("PERMUTATION_DEBUG");
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FDebug>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		// For Debug
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderParameters, ShaderDrawParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
		SHADER_PARAMETER(FMatrix44f, ViewToWorld)
		SHADER_PARAMETER(FVector3f, WorldBound_Min)
		SHADER_PARAMETER(FVector3f, WorldBound_Max)
		SHADER_PARAMETER(FVector3f, ViewBound_Min)
		SHADER_PARAMETER(FVector3f, ViewBound_Max)

		SHADER_PARAMETER(FMatrix44f, LocalToWorld)
		SHADER_PARAMETER(FMatrix44f, WorldToView)

		SHADER_PARAMETER(uint32, SourcePrimitiveType)
		SHADER_PARAMETER(uint32, NumPrimitives)
		SHADER_PARAMETER(uint32, NumIndices)
		SHADER_PARAMETER(uint32, SourceFirstIndex)
		SHADER_PARAMETER(uint32, SortType)
		SHADER_PARAMETER(uint32, SortedIndexBufferSizeInByte)
		
		SHADER_PARAMETER(float, ViewBoundMinZ)
		SHADER_PARAMETER(float, ViewBoundMaxZ)

		SHADER_PARAMETER_SRV(Buffer<float>, PositionBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>, IndexBuffer)
		SHADER_PARAMETER_UAV(Buffer<uint>, OutIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, OutSliceCounterBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutPrimitiveSliceBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutDebugData)


	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsOITSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_SCAN"), 1);
		OutEnvironment.SetDefine(TEXT("SORTING_SLICE_COUNT"), FSortedIndexBuffer::SliceCount);
	}
};

IMPLEMENT_GLOBAL_SHADER(FOITSortTriangleIndex_ScanCS, "/Engine/Private/OIT/OITSorting.usf", "MainCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FOITSortTriangleIndex_AllocateCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOITSortTriangleIndex_AllocateCS);
	SHADER_USE_PARAMETER_STRUCT(FOITSortTriangleIndex_AllocateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SliceCounterBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, SliceOffsetsBuffer)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsOITSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_ALLOCATE"), 1);
		OutEnvironment.SetDefine(TEXT("SORTING_SLICE_COUNT"), FSortedIndexBuffer::SliceCount);
	}
};

IMPLEMENT_GLOBAL_SHADER(FOITSortTriangleIndex_AllocateCS, "/Engine/Private/OIT/OITSorting.usf", "MainCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FOITSortTriangleIndex_WriteOutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOITSortTriangleIndex_WriteOutCS);
	SHADER_USE_PARAMETER_STRUCT(FOITSortTriangleIndex_WriteOutCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, SourcePrimitiveType)
		SHADER_PARAMETER(uint32, NumPrimitives)
		SHADER_PARAMETER(uint32, NumIndices)
		SHADER_PARAMETER(uint32, SrcFirstIndex)
		SHADER_PARAMETER(uint32, DstFirstIndex)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SliceOffsetsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PrimitiveSliceBuffer)

		SHADER_PARAMETER_SRV(Buffer<uint>, IndexBuffer)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, OutIndexBuffer)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsOITSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_WRITE"), 1);
		OutEnvironment.SetDefine(TEXT("SORTING_SLICE_COUNT"), FSortedIndexBuffer::SliceCount);
	}
};

IMPLEMENT_GLOBAL_SHADER(FOITSortTriangleIndex_WriteOutCS, "/Engine/Private/OIT/OITSorting.usf", "MainCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FOITSortTriangleIndex_Debug : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOITSortTriangleIndex_Debug);
	SHADER_USE_PARAMETER_STRUCT(FOITSortTriangleIndex_Debug, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderParameters, ShaderDrawParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
		SHADER_PARAMETER(uint32, VisibleInstances)
		SHADER_PARAMETER(uint32, VisiblePrimitives)
		SHADER_PARAMETER(uint32, VisibleIndexSizeInBytes)
		SHADER_PARAMETER(uint32, AllocatedBuffers)
		SHADER_PARAMETER(uint32, AllocatedIndexSizeInBytes)
		SHADER_PARAMETER(uint32, UnusedBuffers)
		SHADER_PARAMETER(uint32, UnusedIndexSizeInBytes)
		SHADER_PARAMETER(uint32, TotalEntries)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DebugData)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsOITSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_DEBUG"), 1);
		OutEnvironment.SetDefine(TEXT("SORTING_SLICE_COUNT"), FSortedIndexBuffer::SliceCount);
	}
};

IMPLEMENT_GLOBAL_SHADER(FOITSortTriangleIndex_Debug, "/Engine/Private/OIT/OITSorting.usf", "MainCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

static void AddOITSortTriangleIndexPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FOITSceneData& OITSceneData,
	const FSortedTrianglesMeshBatch& MeshBatch,
	FOITSortingType SortType,
	FOITDebugData& DebugData)
{
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FLocalVertexFactory"));

	// Fat format: PF_R32G32_UINT | Compact format: PF_R32_UINT
	const EPixelFormat PackedFormat = PF_R32_UINT;
	const uint32 PackedFormatInBytes = 4;

	const bool bIsValid = 
		MeshBatch.Mesh != nullptr && 
		MeshBatch.Mesh->VertexFactory != nullptr &&
		MeshBatch.Mesh->VertexFactory->GetType()->GetHashedName() == CompatibleVF->GetHashedName() &&
		MeshBatch.Mesh->Elements.Num() > 0 && 
		MeshBatch.Mesh->Elements[0].DynamicIndexBuffer.IndexBuffer != nullptr;
	if (!bIsValid)
	{
		return;
	}

	const FLocalVertexFactory* VF = (const FLocalVertexFactory*)MeshBatch.Mesh->VertexFactory;
	if (!VF) { return; }
	const FShaderResourceViewRHIRef VertexPosition = VF->GetPositionsSRV();
	if (!VertexPosition) { return; }

	const FSortedIndexBuffer* OITIndexBuffer = (const FSortedIndexBuffer*)MeshBatch.Mesh->Elements[0].DynamicIndexBuffer.IndexBuffer;
	check(OITIndexBuffer->Id < uint32(OITSceneData.Allocations.Num()));
	const FSortedTriangleData& Allocation = OITSceneData.Allocations[OITIndexBuffer->Id];
	
	check(Allocation.IsValid());
	check(Allocation.SourcePrimitiveType == PT_TriangleList || Allocation.SourcePrimitiveType == PT_TriangleStrip);
	check(Allocation.SortedPrimitiveType == PT_TriangleList);

	FRDGBufferRef PrimitiveSliceBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(PackedFormatInBytes, Allocation.NumPrimitives), TEXT("OIT.TriangleSortingSliceIndex"));
	FRDGBufferRef SliceCounterBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4, FSortedIndexBuffer::SliceCount), TEXT("OIT.SliceCounters"));
	FRDGBufferRef SliceOffsetsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4, FSortedIndexBuffer::SliceCount), TEXT("OIT.SliceOffsets"));
	FRDGBufferUAVRef SliceCounterUAV = GraphBuilder.CreateUAV(SliceCounterBuffer, PF_R32_UINT);
	AddClearUAVPass(GraphBuilder, SliceCounterUAV, 0u);

	const bool bDebugEnable = DebugData.IsValid();
	FRHIBuffer* SortedIndexBufferRHI = Allocation.SortedIndexBuffer->IndexBufferRHI;

	// 1. Scan the primitive and assign each primitive to a slice
	{
		// Compute the primitive Min/Max-Z value in view space. This domain is sliced for sorting
		const FBoxSphereBounds& Bounds = MeshBatch.Proxy->GetBounds();
		const FBoxSphereBounds& ViewBounds = Bounds.TransformBy(View.ViewMatrices.GetViewMatrix());
		const float ViewBoundMinZ = ViewBounds.GetBox().Min.Z;
		const float ViewBoundMaxZ = ViewBounds.GetBox().Max.Z;

		FOITSortTriangleIndex_ScanCS::FParameters* Parameters = GraphBuilder.AllocParameters<FOITSortTriangleIndex_ScanCS::FParameters>();
		Parameters->LocalToWorld			= FMatrix44f(MeshBatch.Proxy->GetLocalToWorld());	// LWC_TODO: Precision loss?
		Parameters->WorldToView				= FMatrix44f(View.ViewMatrices.GetViewMatrix());
		Parameters->SourcePrimitiveType		= Allocation.SourcePrimitiveType == PT_TriangleStrip ? 1u : 0u;
		Parameters->NumPrimitives			= Allocation.NumPrimitives;
		Parameters->NumIndices				= Allocation.NumIndices;
		Parameters->ViewBoundMinZ			= ViewBoundMinZ;
		Parameters->ViewBoundMaxZ			= ViewBoundMaxZ;
		Parameters->SortType				= SortType == FOITSortingType::BackToFront ? 0 : 1;
		Parameters->SortedIndexBufferSizeInByte = Allocation.SortedIndexBuffer->IndexBufferRHI->GetSize();
		Parameters->PositionBuffer			= VertexPosition;
		Parameters->SourceFirstIndex		= Allocation.SourceFirstIndex;
		Parameters->IndexBuffer				= Allocation.SourceIndexSRV;
		Parameters->OutIndexBuffer			= Allocation.SortedIndexUAV;
		Parameters->OutSliceCounterBuffer	= SliceCounterUAV;
		Parameters->OutPrimitiveSliceBuffer = GraphBuilder.CreateUAV(PrimitiveSliceBuffer, PackedFormat);

		// Debug
		if (bDebugEnable)
		{
			Parameters->ViewToWorld		= FMatrix44f(View.ViewMatrices.GetViewMatrix().Inverse());	 // LWC_TODO: Precision loss?
			Parameters->WorldBound_Min	= (FVector3f)Bounds.GetBox().Min;
			Parameters->WorldBound_Max	= (FVector3f)Bounds.GetBox().Max;
			Parameters->ViewBound_Min	= (FVector3f)ViewBounds.GetBox().Min;
			Parameters->ViewBound_Max	= (FVector3f)ViewBounds.GetBox().Max;
			if (ShaderDrawDebug::IsEnabled(View))
			{
				ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, Parameters->ShaderDrawParameters);
			}
			if (ShaderPrint::IsEnabled(View))
			{
				ShaderPrint::SetParameters(GraphBuilder, View, Parameters->ShaderPrintParameters);
			}

			check(DebugData.Buffer);

			++DebugData.VisibleInstances;
			DebugData.VisiblePrimitives += Parameters->NumPrimitives;
			DebugData.VisibleIndexSizeInBytes += Allocation.SortedIndexBuffer->IndexBufferRHI->GetSize();
			Parameters->OutDebugData = GraphBuilder.CreateUAV(DebugData.Buffer, PF_R32_UINT);
		}

		const uint32 GroupSize = GetGroupSize();

		FOITSortTriangleIndex_ScanCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FOITSortTriangleIndex_ScanCS::FGroupSize>(GroupSize);
		PermutationVector.Set<FOITSortTriangleIndex_ScanCS::FDebug>(bDebugEnable ? 1 : 0);
		TShaderMapRef<FOITSortTriangleIndex_ScanCS> ComputeShader(View.ShaderMap, PermutationVector);
		
		const FIntVector DispatchCount = FIntVector(FMath::CeilToInt(float(Parameters->NumPrimitives) / float(GroupSize)), 1u, 1u);
		check(DispatchCount.X < GRHIMaxDispatchThreadGroupsPerDimension.X);
		ClearUnusedGraphResources(ComputeShader, Parameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("OIT::SortTriangleIndices(Scan)"),
			Parameters,
			ERDGPassFlags::Compute,
			[Parameters, ComputeShader, DispatchCount, SortedIndexBufferRHI](FRHIComputeCommandList& RHICmdList)
			{
				RHICmdList.Transition(FRHITransitionInfo(SortedIndexBufferRHI, ERHIAccess::VertexOrIndexBuffer, ERHIAccess::UAVCompute));
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, DispatchCount);
			});
	}

	// 2. Pre-fix sum onto the slices count to allocate each bucket
	{
		FOITSortTriangleIndex_AllocateCS::FParameters* Parameters = GraphBuilder.AllocParameters<FOITSortTriangleIndex_AllocateCS::FParameters>();
		Parameters->SliceCounterBuffer = GraphBuilder.CreateSRV(SliceCounterBuffer, PF_R32_UINT);
		Parameters->SliceOffsetsBuffer = GraphBuilder.CreateUAV(SliceOffsetsBuffer, PF_R32_UINT);
		TShaderMapRef<FOITSortTriangleIndex_AllocateCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("OIT::SortTriangleIndices(PrefixedSum)"),
			ComputeShader,
			Parameters,
			FIntVector(1u, 1u, 1u));
	}

	// 3. Write-out the sorted indices
	{
		FOITSortTriangleIndex_WriteOutCS::FParameters* Parameters = GraphBuilder.AllocParameters<FOITSortTriangleIndex_WriteOutCS::FParameters>();
		Parameters->SourcePrimitiveType		= Allocation.SourcePrimitiveType == PT_TriangleStrip ? 1u : 0u;
		Parameters->NumPrimitives			= Allocation.NumPrimitives;
		Parameters->NumIndices				= Allocation.NumIndices;
		Parameters->SrcFirstIndex			= Allocation.SourceFirstIndex;
		Parameters->DstFirstIndex			= 0u;

		Parameters->SliceOffsetsBuffer		= GraphBuilder.CreateSRV(SliceOffsetsBuffer, PF_R32_UINT);
		Parameters->PrimitiveSliceBuffer	= GraphBuilder.CreateSRV(PrimitiveSliceBuffer, PackedFormat);

		Parameters->IndexBuffer				= Allocation.SourceIndexSRV;
		Parameters->OutIndexBuffer			= Allocation.SortedIndexUAV;

		TShaderMapRef<FOITSortTriangleIndex_WriteOutCS> ComputeShader(View.ShaderMap);

		const uint32 GroupSize = 256;
		const FIntVector DispatchCount = FIntVector(GroupSize, 1u, 1u);
		ClearUnusedGraphResources(ComputeShader, Parameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("OIT::SortTriangleIndices(Write)"),
			Parameters,
			ERDGPassFlags::Compute,
			[Parameters, ComputeShader, DispatchCount, SortedIndexBufferRHI](FRHIComputeCommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, DispatchCount);

				RHICmdList.Transition(FRHITransitionInfo(SortedIndexBufferRHI, ERHIAccess::UAVCompute, ERHIAccess::VertexOrIndexBuffer));
			});
	}

	// Next todos
	// * Merge several meshes together (not clear out to do the mapping thread->mesh info)
	// * Batch Scan/Alloc/Write of several primitive, so that we have better overlapping
}

static void AddOITDebugPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FOITDebugData& DebugData)
{
	if (!DebugData.IsValid())
	{
		return;
	}

	FOITSortTriangleIndex_Debug::FParameters* Parameters = GraphBuilder.AllocParameters<FOITSortTriangleIndex_Debug::FParameters>();
	Parameters->VisibleInstances = DebugData.VisibleInstances;
	Parameters->VisiblePrimitives = DebugData.VisiblePrimitives;
	Parameters->VisibleIndexSizeInBytes = DebugData.VisibleIndexSizeInBytes;

	Parameters->UnusedBuffers = DebugData.UnusedBuffers;
	Parameters->UnusedIndexSizeInBytes = DebugData.UnusedIndexSizeInBytes;

	Parameters->AllocatedBuffers = DebugData.AllocatedBuffers;
	Parameters->AllocatedIndexSizeInBytes = DebugData.AllocatedIndexSizeInBytes;

	Parameters->TotalEntries = DebugData.TotalEntries;

	Parameters->DebugData = GraphBuilder.CreateSRV(DebugData.Buffer, FOITDebugData::Format);
	if (ShaderDrawDebug::IsEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, Parameters->ShaderDrawParameters);
	}
	if (ShaderPrint::IsEnabled(View))
	{
		ShaderPrint::SetParameters(GraphBuilder, View, Parameters->ShaderPrintParameters);
	}

	TShaderMapRef<FOITSortTriangleIndex_Debug> ComputeShader(View.ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("OIT::Debug"),
		ComputeShader,
		Parameters,
		FIntVector(1u, 1u, 1u));
}

///////////////////////////////////////////////////////////////////////////////////////////////////

namespace OIT
{
	bool IsEnabled(const FViewInfo& View)
	{
		return GOIT_SortObjectTriangles > 0 && IsOITSupported(View.GetShaderPlatform());
	}

	bool IsEnabled(EShaderPlatform ShaderPlatform)
	{
		return GOIT_SortObjectTriangles > 0 && IsOITSupported(ShaderPlatform);
	}

	bool IsCompatible(const FMeshBatch& InMesh, ERHIFeatureLevel::Type InFeatureLevel)
	{
		// Only support local vertex factory at the moment as we need to have direct access to the position
		static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FLocalVertexFactory"));

		if (InMesh.IsTranslucent(InFeatureLevel))
		{
			return InMesh.VertexFactory->GetType()->GetHashedName() == CompatibleVF->GetHashedName();
		}
		return false;
	}

	void AddSortTrianglesPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FOITSceneData& OITSceneData, FOITSortingType SortType)
	{
		if (!IsEnabled(View))
		{
			return;
		}

		RDG_EVENT_SCOPE(GraphBuilder, "OIT::IndexSorting");

		const bool bDebugEnable = GOIT_Debug > 0;
		FOITDebugData DebugData;
		if (bDebugEnable)
		{
			const uint32 ValidAllocationCount = OITSceneData.Allocations.Num();
			DebugData.Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4u, ValidAllocationCount + 1), TEXT("OIT.DebugData"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DebugData.Buffer, FOITDebugData::Format), 0u);

			// Allocated/used
			for (const FSortedTriangleData& Allocated : OITSceneData.Allocations)
			{
				if (Allocated.IsValid())
				{
					++DebugData.AllocatedBuffers;
					DebugData.AllocatedIndexSizeInBytes += Allocated.SortedIndexBuffer->IndexBufferRHI->GetSize();
				}
			}

			// Unused
			for (const FSortedIndexBuffer* FreeBuffer : OITSceneData.FreeBuffers)
			{
				++DebugData.UnusedBuffers;
				DebugData.UnusedIndexSizeInBytes = FreeBuffer->IndexBufferRHI->GetSize();
			}

			DebugData.TotalEntries = OITSceneData.Allocations.Num();
		}

		for (const FSortedTrianglesMeshBatch& MeshBatch : View.SortedTrianglesMeshBatches)		
		{
			AddOITSortTriangleIndexPass(GraphBuilder, View, OITSceneData, MeshBatch, SortType, DebugData);
		}

		if (DebugData.IsValid())
		{
			AddOITDebugPass(GraphBuilder, View, DebugData);
		}

		// Trim unused buffers
		OITSceneData.FrameIndex = View.Family->FrameNumber;
		if (GOIT_Pool > 0 && GOIT_PoolReleaseThreshold > 0)
		{
			TrimSortedIndexBuffers(OITSceneData.FreeBuffers, OITSceneData.FrameIndex);
		}
	}

	void ConvertSortedIndexToDynamicIndex(FSortedTriangleData* In, FMeshBatchElementDynamicIndexBuffer* Out)
	{
		check(In && Out);
		Out->IndexBuffer = In->SortedIndexBuffer;
		Out->FirstIndex = In->SortedFirstIndex;
		Out->PrimitiveType = In->SortedPrimitiveType;
	}
}