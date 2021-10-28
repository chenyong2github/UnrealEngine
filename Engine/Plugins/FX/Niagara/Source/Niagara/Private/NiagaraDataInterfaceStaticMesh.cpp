// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceStaticMesh.h"
#include "NiagaraEmitterInstance.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMeshSocket.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraRenderer.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraScript.h"
#include "ShaderParameterUtils.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraStats.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceStaticMesh"

// These are to help readability in template specializations
using TSampleModeInvalid = TIntegralConstant<UNiagaraDataInterfaceStaticMesh::ESampleMode, UNiagaraDataInterfaceStaticMesh::ESampleMode::Invalid>;
using TSampleModeDefault = TIntegralConstant<UNiagaraDataInterfaceStaticMesh::ESampleMode, UNiagaraDataInterfaceStaticMesh::ESampleMode::Default>;
using TSampleModeAreaWeighted = TIntegralConstant<UNiagaraDataInterfaceStaticMesh::ESampleMode, UNiagaraDataInterfaceStaticMesh::ESampleMode::AreaWeighted>;

const FString UNiagaraDataInterfaceStaticMesh::MeshIndexBufferName(TEXT("IndexBuffer_"));
const FString UNiagaraDataInterfaceStaticMesh::MeshVertexBufferName(TEXT("VertexBuffer_"));
const FString UNiagaraDataInterfaceStaticMesh::MeshTangentBufferName(TEXT("TangentBuffer_"));
const FString UNiagaraDataInterfaceStaticMesh::MeshTexCoordBufferName(TEXT("TexCoordBuffer_"));
const FString UNiagaraDataInterfaceStaticMesh::MeshColorBufferName(TEXT("ColorBuffer_"));
const FString UNiagaraDataInterfaceStaticMesh::MeshSectionBufferName(TEXT("SectionBuffer_"));
const FString UNiagaraDataInterfaceStaticMesh::MeshTriangleBufferName(TEXT("TriangleBuffer_"));
const FString UNiagaraDataInterfaceStaticMesh::SectionCountName(TEXT("SectionCount_"));
const FString UNiagaraDataInterfaceStaticMesh::InstanceTransformName(TEXT("InstanceTransform_"));
const FString UNiagaraDataInterfaceStaticMesh::InstanceTransformInverseTransposedName(TEXT("InstanceTransformInverseTransposed_"));
const FString UNiagaraDataInterfaceStaticMesh::InstancePrevTransformName(TEXT("InstancePrevTransform_"));
const FString UNiagaraDataInterfaceStaticMesh::InstanceRotationName(TEXT("InstanceRotation_"));
const FString UNiagaraDataInterfaceStaticMesh::InstancePrevRotationName(TEXT("InstancePrevRotation_"));
const FString UNiagaraDataInterfaceStaticMesh::InstanceInvDeltaTimeName(TEXT("InstanceInvDeltaTime_"));
const FString UNiagaraDataInterfaceStaticMesh::InstanceWorldVelocityName(TEXT("InstanceWorldVelocity_"));
const FString UNiagaraDataInterfaceStaticMesh::AreaWeightedSamplingName(TEXT("AreaWeightedSamplingName_"));
const FString UNiagaraDataInterfaceStaticMesh::NumTexCoordName(TEXT("NumTexCoordName_"));
const FString UNiagaraDataInterfaceStaticMesh::UseColorBufferName(TEXT("UseColorBuffer_"));
const FString UNiagaraDataInterfaceStaticMesh::SocketTransformsName(TEXT("SocketTransforms_"));
const FString UNiagaraDataInterfaceStaticMesh::FilteredAndUnfilteredSocketsName(TEXT("FilteredAndUnfilteredSockets_"));
const FString UNiagaraDataInterfaceStaticMesh::NumSocketsAndFilteredName(TEXT("NumSocketsAndFiltered_"));

static int32 GNiagaraFailStaticMeshDataInterface = 0;
static FAutoConsoleVariableRef CVarNiagaraFailStaticMeshDataInterface(
	TEXT("fx.Niagara.FailStaticMeshDataInterface"),
	GNiagaraFailStaticMeshDataInterface,
	TEXT("When enabled we will fail out using static mesh data interfaces."),
	ECVF_Default
);

static int32 GNDIStaticMesh_UseInlineLODsOnly = 1;
static FAutoConsoleVariableRef CVarNDIStaticMesh_UseInlineLODsOnly(
	TEXT("fx.Niagara.NDIStaticMesh.UseInlineLODsOnly"),
	GNDIStaticMesh_UseInlineLODsOnly,
	TEXT("When enabled Niagara will never use streaming LOD levels, only inline LODs."),
	ECVF_Default
);

FStaticMeshFilteredAreaWeightedSectionSampler::FStaticMeshFilteredAreaWeightedSectionSampler()
	: Res(nullptr)
	, Owner(nullptr)
{
}

void FStaticMeshFilteredAreaWeightedSectionSampler::Init(const FStaticMeshLODResources* InRes, FNDIStaticMesh_InstanceData* InOwner)
{
	Res = InRes;
	Owner = InOwner;

	Initialize();
}

float FStaticMeshFilteredAreaWeightedSectionSampler::GetWeights(TArray<float>& OutWeights)
{
	float Total = 0.0f;
	if (Owner && Owner->bMeshValid)
	{
		OutWeights.Empty(Owner->GetValidSections().Num());
		if (Owner->StaticMesh->bSupportUniformlyDistributedSampling && Res->AreaWeightedSectionSamplers.Num() > 0)
		{
			for (int32 i = 0; i < Owner->GetValidSections().Num(); ++i)
			{
				int32 SecIdx = Owner->GetValidSections()[i];
				float T = Res->AreaWeightedSectionSamplers[SecIdx].GetTotalWeight();
				OutWeights.Add(T);
				Total += T;
			}
		}
		else
		{
			for (int32 i = 0; i < Owner->GetValidSections().Num(); ++i)
			{
				int32 SecIdx = Owner->GetValidSections()[i];
				float T = 1.0f;
				OutWeights.Add(T);
				Total += T;
			}
		}

		// Release the reference to the LODresource to avoid blocking stream out operations.
		Res.SafeRelease();
	}
	return Total;
}

//////////////////////////////////////////////////////////////////////////
// FStaticMeshGpuSpawnBuffer
FStaticMeshGpuSpawnBuffer::~FStaticMeshGpuSpawnBuffer()
{
	//ValidSections.Empty();
}

void FStaticMeshGpuSpawnBuffer::Initialise(const FStaticMeshLODResources* Res, const UNiagaraDataInterfaceStaticMesh& Interface, struct FNDIStaticMesh_InstanceData* InstanceData)
{
	// In this function we prepare some data to be uploaded on GPU from the available mesh data. This is a thread safe place to create this data.
	// The section buffer needs to be specific to the current UI being built (section/material culling).
	SectionRenderData = Res;

	const uint32 ValidSectionCount = InstanceData->ValidSections.Num();
	TArrayView<const float> Prob = InstanceData->Sampler.GetProb();
	TArrayView<const int32> Alias = InstanceData->Sampler.GetAlias();
	check(ValidSectionCount == Prob.Num());
	// Build data that will be uploaded to GPU later from the render thread.
	// The array contains data used to select regions for uniform particle spawning on them, as well as section triangle ranges.
	ValidSections.Reserve(ValidSectionCount);
	for (uint32 i = 0; i < ValidSectionCount; ++i)
	{
		uint32 ValidSectionId = InstanceData->ValidSections[i];
		const FStaticMeshSection& Section = Res->Sections[ValidSectionId];
		SectionInfo NewSectionInfo = { Section.FirstIndex / 3, Section.NumTriangles, Prob[i], (uint32)Alias[i] };
		ValidSections.Add(NewSectionInfo);

		check(!InstanceData->bIsGpuUniformlyDistributedSampling || InstanceData->bIsGpuUniformlyDistributedSampling && Res->AreaWeightedSectionSamplers[ValidSectionId].GetProb().Num() == Section.NumTriangles);
	}

	if (InstanceData->bIsGpuUniformlyDistributedSampling)
	{
		BufferUniformTriangleSamplingSRV = Res->AreaWeightedSectionSamplersBuffer.GetBufferSRV(); // Cache that SRV for later
	}

	// Init socket data
	NumSockets = InstanceData->CachedSockets.Num();
	NumFilteredSockets = 0;
	if (NumSockets > 0)
	{
		SocketTransformsResourceArray.Reserve(NumFilteredSockets * 3);
		for (const FTransform& SocketTransform : InstanceData->CachedSockets)
		{
			const FQuat SocketRotation = SocketTransform.GetRotation();
			SocketTransformsResourceArray.Add(FVector4(SocketTransform.GetTranslation(), 0.0f));
			SocketTransformsResourceArray.Add(FVector4(SocketRotation.X, SocketRotation.Y, SocketRotation.Z, SocketRotation.W));
			SocketTransformsResourceArray.Add(FVector4(SocketTransform.GetScale3D(), 0.0f));
		}

		NumFilteredSockets = InstanceData->NumFilteredSockets;
		if (NumFilteredSockets > 0)
		{
			FilteredAndUnfilteredSocketsResourceArray.Reserve(InstanceData->FilteredAndUnfilteredSockets.Num());
			for (uint16 v : InstanceData->FilteredAndUnfilteredSockets)
			{
				FilteredAndUnfilteredSocketsResourceArray.Add(v);
			}
		}
	}
}

void FStaticMeshGpuSpawnBuffer::InitRHI()
{
#if STATS
	ensure(GPUMemoryUsage == 0);
#endif

	MeshIndexBufferSrv = RHICreateShaderResourceView(SectionRenderData->IndexBuffer.IndexBufferRHI);
	MeshVertexBufferSrv = SectionRenderData->VertexBuffers.PositionVertexBuffer.GetSRV();
	MeshTangentBufferSrv = SectionRenderData->VertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
	MeshTexCoordBufferSrv = SectionRenderData->VertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
	NumTexCoord = SectionRenderData->VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
	MeshColorBufferSRV = SectionRenderData->VertexBuffers.ColorVertexBuffer.GetColorComponentsSRV();

	uint32 SizeByte = ValidSections.Num() * sizeof(SectionInfo);
	if (SizeByte > 0)
	{
		FRHIResourceCreateInfo CreateInfo;
		void* BufferData = nullptr;
		BufferSectionRHI = RHICreateAndLockVertexBuffer(SizeByte, BUF_Static | BUF_ShaderResource, CreateInfo, BufferData);
		SectionInfo* SectionInfoBuffer = (SectionInfo*)BufferData;
		FMemory::Memcpy(SectionInfoBuffer, ValidSections.GetData(), SizeByte);
		RHIUnlockVertexBuffer(BufferSectionRHI);
		BufferSectionSRV = RHICreateShaderResourceView(BufferSectionRHI, sizeof(SectionInfo), PF_R32G32B32A32_UINT);
#if STATS
		GPUMemoryUsage += SizeByte;
#endif
	}

	if (SocketTransformsResourceArray.Num() > 0)
	{
		FRHIResourceCreateInfo RHICreateInfo;
		RHICreateInfo.ResourceArray = &SocketTransformsResourceArray;
		SocketTransformsBuffer = RHICreateVertexBuffer(SocketTransformsResourceArray.GetTypeSize() * SocketTransformsResourceArray.Num(), BUF_ShaderResource | BUF_Static, RHICreateInfo);
		SocketTransformsSRV = RHICreateShaderResourceView(SocketTransformsBuffer, sizeof(float) * 4, PF_A32B32G32R32F);
		SocketTransformsResourceArray.Empty();
#if STATS
		GPUMemoryUsage += SocketTransformsResourceArray.GetTypeSize() * SocketTransformsResourceArray.Num();
#endif
	}

	if (FilteredAndUnfilteredSocketsResourceArray.Num() > 0)
	{
		FRHIResourceCreateInfo RHICreateInfo;
		RHICreateInfo.ResourceArray = &FilteredAndUnfilteredSocketsResourceArray;
		FilteredAndUnfilteredSocketsBuffer = RHICreateVertexBuffer(FilteredAndUnfilteredSocketsResourceArray.GetTypeSize() * FilteredAndUnfilteredSocketsResourceArray.Num(), BUF_ShaderResource | BUF_Static, RHICreateInfo);
		FilteredAndUnfilteredSocketsSRV = RHICreateShaderResourceView(FilteredAndUnfilteredSocketsBuffer, sizeof(uint16), PF_R16_UINT);
		FilteredAndUnfilteredSocketsResourceArray.Empty();
#if STATS
		GPUMemoryUsage += FilteredAndUnfilteredSocketsResourceArray.GetTypeSize() * FilteredAndUnfilteredSocketsResourceArray.Num();
#endif
	}
#if STATS
	GPUMemoryUsage += FilteredAndUnfilteredSocketsResourceArray.GetTypeSize() * FilteredAndUnfilteredSocketsResourceArray.Num();
	INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GPUMemoryUsage);
#endif
}

void FStaticMeshGpuSpawnBuffer::ReleaseRHI()
{
#if STATS
	DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GPUMemoryUsage);
	GPUMemoryUsage = 0;
#endif

	MeshIndexBufferSrv.SafeRelease();
	BufferSectionSRV.SafeRelease();
	BufferSectionRHI.SafeRelease();

	MeshIndexBufferSrv.SafeRelease();
	MeshVertexBufferSrv.SafeRelease();
	MeshTangentBufferSrv.SafeRelease();
	MeshTexCoordBufferSrv.SafeRelease();
	MeshColorBufferSRV.SafeRelease();
	BufferSectionSRV.SafeRelease();

	SocketTransformsResourceArray.Empty();
	SocketTransformsBuffer.SafeRelease();
	SocketTransformsSRV.SafeRelease();

	FilteredAndUnfilteredSocketsResourceArray.Empty();
	FilteredAndUnfilteredSocketsBuffer.SafeRelease();
	FilteredAndUnfilteredSocketsSRV.SafeRelease();
}


//////////////////////////////////////////////////////////////////////////
//FNDIStaticMesh_InstanceData

void FNDIStaticMesh_InstanceData::InitVertexColorFiltering()
{
	DynamicVertexColorSampler = FNDI_StaticMesh_GeneratedData::GetDynamicColorFilterData(this);
}

bool FNDIStaticMesh_InstanceData::Init(UNiagaraDataInterfaceStaticMesh* Interface, FNiagaraSystemInstance* SystemInstance)
{
	check(SystemInstance);
	SceneComponent = nullptr;
	StaticMesh = nullptr;

	Transform = FMatrix::Identity;
	TransformInverseTransposed = FMatrix::Identity;
	PrevTransform = FMatrix::Identity;
	Rotation = FQuat::Identity;
	PrevRotation = FQuat::Identity;
	DeltaSeconds = 0.0f;
	ChangeId = Interface->ChangeId;
	bUsePhysicsVelocity = Interface->bUsePhysicsBodyVelocity;
	PhysicsVelocity = FVector::ZeroVector;
	bMeshValid = false;
	bComponentValid = false;
	bMeshAllowsCpuAccess = false;
	bIsCpuUniformlyDistributedSampling = false;
	bIsGpuUniformlyDistributedSampling = false;
	ValidSections.Empty();

	UStaticMesh* Mesh = Interface->GetStaticMesh(SceneComponent, SystemInstance);	
	bComponentValid = SceneComponent.IsValid();

	const FTransform& ComponentTransform = bComponentValid ? SceneComponent->GetComponentToWorld() : SystemInstance->GetWorldTransform();

	Transform = ComponentTransform.ToMatrixWithScale();
	PrevTransform = Transform;
	TransformInverseTransposed = ComponentTransform.Inverse().ToMatrixWithScale().GetTransposed();

	Rotation = ComponentTransform.GetRotation();
	PrevRotation = Rotation;

	if (bUsePhysicsVelocity)
	{
		if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
		{
			PhysicsVelocity = MeshComponent->GetPhysicsLinearVelocity();
		}
	}

	if (GNiagaraFailStaticMeshDataInterface != 0)
	{
		UE_LOG(LogNiagara, Log, TEXT("StaticMesh data interface aborting because \"fx.Niagara.FailStaticMeshDataInterface 1\". Failed InitPerInstanceData - %s"), *Interface->GetFullName());
		return false;
	}

	// Report missing or inaccessible meshes to the log
	if (Mesh == nullptr)
	{
		UE_LOG(LogNiagara, Log, TEXT("StaticMesh data interface has no valid mesh - %s"), *Interface->GetFullName());
	}
	else if (!Mesh->bAllowCPUAccess)
	{
		UE_LOG(LogNiagara, Log, TEXT("StaticMesh data interface using a mesh that does not allow CPU access. Interface: %s, Mesh: %s"),
			*Interface->GetFullName(), *Mesh->GetFullName());
		Mesh = nullptr; // Disallow usage of this mesh to prevent issues on cooked builds
	}

	TRefCountPtr<const FStaticMeshLODResources> LODData;
	if (Mesh != nullptr)
	{
		// Check if any valid LODs are found. If not, we won't use this mesh
		MinLOD = Mesh->GetMinLOD().GetValue();
		if ( GNDIStaticMesh_UseInlineLODsOnly )
		{
			MinLOD = Mesh->GetNumLODs() - Mesh->GetRenderData()->NumInlinedLODs;
		}

		FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		if (RenderData)
		{
			CachedLODIdx = RenderData->GetCurrentFirstLODIdx(MinLOD);
			if (RenderData->LODResources.IsValidIndex(CachedLODIdx))
			{
				LODData = &RenderData->LODResources[CachedLODIdx];
			}
		}

		if (!LODData.IsValid())
		{
			Mesh = nullptr;
		}
	}

	StaticMesh = Mesh;
	bMeshValid = Mesh != nullptr;
	
	if (Mesh != nullptr)
	{
#if WITH_EDITOR
		// HACK! This only works on systems created by a Niagara component...should maybe move somewhere else to cover non-component systems
		if (UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(SystemInstance->GetAttachComponent()))
		{
			Mesh->GetOnMeshChanged().AddUObject(NiagaraComponent, &UNiagaraComponent::ReinitializeSystem);
		}
#endif

		bMeshAllowsCpuAccess = Mesh->bAllowCPUAccess;
		bIsCpuUniformlyDistributedSampling = Mesh->bSupportUniformlyDistributedSampling;
		bIsGpuUniformlyDistributedSampling = bIsCpuUniformlyDistributedSampling && Mesh->bSupportGpuUniformlyDistributedSampling;

		//Init the instance filter
		for (int32 i = 0; i < LODData->Sections.Num(); ++i)
		{
			if (Interface->SectionFilter.AllowedMaterialSlots.Num() == 0 || Interface->SectionFilter.AllowedMaterialSlots.Contains(LODData->Sections[i].MaterialIndex))
			{
				ValidSections.Add(i);
			}
		}

		if (GetValidSections().Num() == 0)
		{
			UE_LOG(LogNiagara, Log, TEXT("StaticMesh data interface either has no current LODs or has a section filter preventing any spawning - %s"), *Interface->GetFullName());
		}

		Sampler.Init(LODData, this);

		// Init socket information
		const int32 NumMeshSockets = Mesh->Sockets.Num();
		if (NumMeshSockets > 0)
		{
			if (NumMeshSockets > TNumericLimits<uint16>::Max())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Static Mesh Data Interface '%s' requires more sockets '%d' than we currently support '%d' Mesh '%s' Component '%s'"), *GetFullNameSafe(Interface), NumMeshSockets, TNumericLimits<uint16>::Max(), *GetFullNameSafe(Mesh), *GetFullNameSafe(SceneComponent.Get()));
				return false;
			}

			CachedSockets.AddDefaulted(NumMeshSockets);
			for (int32 i=0; i < NumMeshSockets; ++i)
			{
				CachedSockets[i].SetTranslation(Mesh->Sockets[i]->RelativeLocation);
				CachedSockets[i].SetRotation(FQuat(Mesh->Sockets[i]->RelativeRotation));
				CachedSockets[i].SetScale3D(Mesh->Sockets[i]->RelativeScale);
			}

			NumFilteredSockets = 0;
			if (Interface->FilteredSockets.Num() > 0)
			{
				FilteredAndUnfilteredSockets.Reserve(NumMeshSockets);
				for (int32 i = 0; i < Interface->FilteredSockets.Num(); ++i)
				{
					const FName FilteredSocketName = Interface->FilteredSockets[i];
					const int32 SocketIndex = Mesh->Sockets.IndexOfByPredicate([&](const UStaticMeshSocket* Socket) { return Socket->SocketName == FilteredSocketName; });
					if (SocketIndex == INDEX_NONE)
					{
						UE_LOG(LogNiagara, Warning, TEXT("Static Mesh Data Interface '%s' could not find socket '%s' Mesh '%s' Component '%s'"), *GetFullNameSafe(Interface), *FilteredSocketName.ToString(), *GetFullNameSafe(Mesh), *GetFullNameSafe(SceneComponent.Get()));
						continue;
					}
					++NumFilteredSockets;
					FilteredAndUnfilteredSockets.Add(uint16(SocketIndex));
				}

				for (int32 i = 0; i < NumMeshSockets; ++i)
				{
					FilteredAndUnfilteredSockets.AddUnique(uint16(i));
				}
			}
		}
	}

	return true;
}

bool FNDIStaticMesh_InstanceData::ResetRequired(UNiagaraDataInterfaceStaticMesh* Interface) const
{
	USceneComponent* Component = SceneComponent.Get();
	if (bComponentValid && !Component)
	{
		// The component we were bound to is no longer valid so we have to trigger a reset.
		return true;
	}

	UStaticMesh* Mesh = StaticMesh.Get();
	if (bMeshValid)
	{
		if (!Mesh)
		{
			// The static mesh we were bound to is no longer valid so we have to trigger a reset.
			return true;
		}
		else if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Component))
		{
			if (Mesh != StaticMeshComp->GetStaticMesh())
			{
				// The mesh changed on the component we're attached to so we have to reset
				return true;
			}
		}
	}

	if (Interface != nullptr && ChangeId != Interface->ChangeId)
	{
		return true;
	}

	if (Mesh != nullptr)
	{
		// Currently we only reset if the cached LOD was streamed out, to avoid performance hits. To revisit.
		// We could probably just recache the data derived from the LOD instead of resetting everything.
		if (Mesh->GetRenderData()->GetCurrentFirstLODIdx(MinLOD) > CachedLODIdx)
		{
			return true;
		}

		// The following conditions look like they could only be triggered in Editor...
		//bool bPrevVCSampling = bSupportingVertexColorSampling;//TODO: Vertex color filtering needs more work.
		const bool bNewMeshAllowsCpuAccess = Mesh->bAllowCPUAccess;
		const bool bNewIsCpuAreaWeightedSampling = Mesh->bSupportUniformlyDistributedSampling;
		const bool bNewIsGpuAreaWeightedSampling = bIsCpuUniformlyDistributedSampling && Mesh->bSupportGpuUniformlyDistributedSampling;

		//bSupportingVertexColorSampling = bEnableVertexColorRangeSorting && MeshHasColors();
		return bNewMeshAllowsCpuAccess != bMeshAllowsCpuAccess || bNewIsCpuAreaWeightedSampling != bIsCpuUniformlyDistributedSampling || bNewIsGpuAreaWeightedSampling != bIsGpuUniformlyDistributedSampling /* || bSupportingVertexColorSampling != bPrevVCSampling*/;
	}
	
	return false;
}

bool FNDIStaticMesh_InstanceData::Tick(UNiagaraDataInterfaceStaticMesh* Interface, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	if (ResetRequired(Interface))
	{
		return true;
	}
	else
	{
		DeltaSeconds = InDeltaSeconds;
		
		const FTransform& ComponentTransform = SceneComponent.IsValid() ? SceneComponent->GetComponentToWorld() : SystemInstance->GetWorldTransform();

		PrevTransform = Transform;
		Transform = ComponentTransform.ToMatrixWithScale();
		TransformInverseTransposed = ComponentTransform.Inverse().ToMatrixWithScale().GetTransposed();

		PrevRotation = Rotation;
		Rotation = ComponentTransform.GetRotation();

		if (bUsePhysicsVelocity)
		{
			if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
			{
				PhysicsVelocity = MeshComponent->GetPhysicsLinearVelocity();
			}
			else
			{
				PhysicsVelocity = FVector::ZeroVector;
			}
		}
		
		return false;
	}
}

void FNDIStaticMesh_InstanceData::Release()
{
	/*if (MeshGpuSpawnBuffer)
	{
		BeginReleaseResource(MeshGpuSpawnBuffer);
		ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(DeleteResource, FStaticMeshGpuSpawnBuffer*, ParamValidSectionVertexBuffer, MeshGpuSpawnBuffer,
			{
				delete ParamValidSectionVertexBuffer;
			});
	}*/
}

//void UNiagaraDataInterfaceStaticMesh::ResetFilter()
//{
//#if WITH_EDITOR	TODO: Make editor only and serialize this stuff out.
//	SectionFilter.Init(this, UsesAreaWeighting());

//TODO: Vertex color filtering needs some more work.
// 	// If we have enabled vertex color sorting for emission, given that these are vertex colors
// 	// and is limited to a byte, we can make lookup relatively quick by just having a bucket
// 	// for every possible entry.
// 	// Will want a better strategy in the long run, but for now this is trivial for GDC..
// 	TrianglesSortedByVertexColor.Empty();
// 	if (bEnableVertexColorRangeSorting && bSupportingVertexColorSampling)
// 	{
// 		VertexColorToTriangleStart.AddDefaulted(256);
// 		if (UStaticMesh* ActualMesh = GetActualMesh())
// 		{
// 			FStaticMeshLODResources& Res = ActualMesh->RenderData->LODResources[0];
// 
// 			// Go over all triangles for each possible vertex color and add it to that bucket
// 			for (int32 i = 0; i < VertexColorToTriangleStart.Num(); i++)
// 			{
// 				uint32 MinVertexColorRed = i;
// 				uint32 MaxVertexColorRed = i + 1;
// 				VertexColorToTriangleStart[i] = TrianglesSortedByVertexColor.Num();
// 
// 				FIndexArrayView IndexView = Res.IndexBuffer.GetArrayView();
// 				for (int32 j = 0; j < SectionFilter.GetValidSections().Num(); j++)
// 				{
// 					int32 SectionIdx = SectionFilter.GetValidSections()[j];
// 					int32 TriStartIdx = Res.Sections[SectionIdx].FirstIndex;
// 					for (uint32 TriIdx = 0; TriIdx < Res.Sections[SectionIdx].NumTriangles; TriIdx++)
// 					{
// 						uint32 V0Idx = IndexView[TriStartIdx + TriIdx * 3 + 0];
// 						uint32 V1Idx = IndexView[TriStartIdx + TriIdx * 3 + 1];
// 						uint32 V2Idx = IndexView[TriStartIdx + TriIdx * 3 + 2];
// 
// 						uint8 MaxR = FMath::Max<uint8>(Res.ColorVertexBuffer.VertexColor(V0Idx).R,
// 							FMath::Max<uint8>(Res.ColorVertexBuffer.VertexColor(V1Idx).R,
// 								Res.ColorVertexBuffer.VertexColor(V2Idx).R));
// 						if (MaxR >= MinVertexColorRed && MaxR < MaxVertexColorRed)
// 						{
// 							TrianglesSortedByVertexColor.Add(TriStartIdx + TriIdx * 3);
// 						}
// 					}
// 				}
// 			}
// 		}
// 	}
//	bFilterInitialized = true;

//#endif
//}

//////////////////////////////////////////////////////////////////////////

struct FNDIStaticMeshParametersName
{
	FString MeshIndexBufferName;
	FString MeshVertexBufferName;
	FString MeshTangentBufferName;
	FString MeshTexCoordBufferName;
	FString MeshColorBufferName;
	FString MeshSectionBufferName;
	FString MeshTriangleBufferName;
	FString SectionCountName;
	FString InstanceTransformName;
	FString InstanceTransformInverseTransposedName;
	FString InstancePrevTransformName;
	FString InstanceRotationName;
	FString InstancePrevRotationName;
	FString InstanceInvDeltaTimeName;
	FString InstanceWorldVelocityName;
	FString AreaWeightedSamplingName;
	FString NumTexCoordName;
	FString UseColorBufferName;
	FString SocketTransformsName;
	FString FilteredAndUnfilteredSocketsName;
	FString NumSocketsAndFilteredName;
};

static void GetNiagaraDataInterfaceParametersName(FNDIStaticMeshParametersName& Names, const FString& Suffix)
{
	Names.MeshIndexBufferName = UNiagaraDataInterfaceStaticMesh::MeshIndexBufferName + Suffix;
	Names.MeshVertexBufferName = UNiagaraDataInterfaceStaticMesh::MeshVertexBufferName + Suffix;
	Names.MeshTangentBufferName = UNiagaraDataInterfaceStaticMesh::MeshTangentBufferName + Suffix;
	Names.MeshTexCoordBufferName = UNiagaraDataInterfaceStaticMesh::MeshTexCoordBufferName + Suffix;
	Names.MeshColorBufferName = UNiagaraDataInterfaceStaticMesh::MeshColorBufferName + Suffix;
	Names.MeshSectionBufferName = UNiagaraDataInterfaceStaticMesh::MeshSectionBufferName + Suffix;
	Names.MeshTriangleBufferName = UNiagaraDataInterfaceStaticMesh::MeshTriangleBufferName + Suffix;
	Names.SectionCountName = UNiagaraDataInterfaceStaticMesh::SectionCountName + Suffix;
	Names.InstanceTransformName = UNiagaraDataInterfaceStaticMesh::InstanceTransformName + Suffix;
	Names.InstanceTransformInverseTransposedName = UNiagaraDataInterfaceStaticMesh::InstanceTransformInverseTransposedName + Suffix;
	Names.InstancePrevTransformName = UNiagaraDataInterfaceStaticMesh::InstancePrevTransformName + Suffix;
	Names.InstanceRotationName = UNiagaraDataInterfaceStaticMesh::InstanceRotationName + Suffix;
	Names.InstancePrevRotationName = UNiagaraDataInterfaceStaticMesh::InstancePrevRotationName + Suffix;
	Names.InstanceInvDeltaTimeName = UNiagaraDataInterfaceStaticMesh::InstanceInvDeltaTimeName + Suffix;
	Names.InstanceWorldVelocityName = UNiagaraDataInterfaceStaticMesh::InstanceWorldVelocityName + Suffix;
	Names.AreaWeightedSamplingName = UNiagaraDataInterfaceStaticMesh::AreaWeightedSamplingName + Suffix;
	Names.NumTexCoordName = UNiagaraDataInterfaceStaticMesh::NumTexCoordName + Suffix;
	Names.UseColorBufferName = UNiagaraDataInterfaceStaticMesh::UseColorBufferName + Suffix;
	Names.SocketTransformsName = UNiagaraDataInterfaceStaticMesh::SocketTransformsName + Suffix;
	Names.FilteredAndUnfilteredSocketsName = UNiagaraDataInterfaceStaticMesh::FilteredAndUnfilteredSocketsName + Suffix;
	Names.NumSocketsAndFilteredName = UNiagaraDataInterfaceStaticMesh::NumSocketsAndFilteredName + Suffix;
}

struct FNiagaraDataInterfaceParametersCS_StaticMesh : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_StaticMesh, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FNDIStaticMeshParametersName ParamNames;
		GetNiagaraDataInterfaceParametersName(ParamNames, ParameterInfo.DataInterfaceHLSLSymbol);

		MeshIndexBuffer.Bind(ParameterMap, *ParamNames.MeshIndexBufferName);
		MeshVertexBuffer.Bind(ParameterMap, *ParamNames.MeshVertexBufferName);
		MeshTangentBuffer.Bind(ParameterMap, *ParamNames.MeshTangentBufferName);
		MeshTexCoordBuffer.Bind(ParameterMap, *ParamNames.MeshTexCoordBufferName);
		MeshColorBuffer.Bind(ParameterMap, *ParamNames.MeshColorBufferName);
		MeshSectionBuffer.Bind(ParameterMap, *ParamNames.MeshSectionBufferName);
		MeshTriangleBuffer.Bind(ParameterMap, *ParamNames.MeshTriangleBufferName);
		SectionCount.Bind(ParameterMap, *ParamNames.SectionCountName);
		InstanceTransform.Bind(ParameterMap, *ParamNames.InstanceTransformName);
		InstanceTransformInverseTransposed.Bind(ParameterMap, *ParamNames.InstanceTransformInverseTransposedName);
		InstancePrevTransform.Bind(ParameterMap, *ParamNames.InstancePrevTransformName);
		InstanceRotation.Bind(ParameterMap, *ParamNames.InstanceRotationName);
		InstancePrevRotation.Bind(ParameterMap, *ParamNames.InstancePrevRotationName);
		InstanceInvDeltaTime.Bind(ParameterMap, *ParamNames.InstanceInvDeltaTimeName);
		InstanceWorldVelocity.Bind(ParameterMap, *ParamNames.InstanceWorldVelocityName);
		AreaWeightedSampling.Bind(ParameterMap, *ParamNames.AreaWeightedSamplingName);
		NumTexCoord.Bind(ParameterMap, *ParamNames.NumTexCoordName);
		UseColorBuffer.Bind(ParameterMap, *ParamNames.UseColorBufferName);
		SocketTransforms.Bind(ParameterMap, *ParamNames.SocketTransformsName);
		FilteredAndUnfilteredSockets.Bind(ParameterMap, *ParamNames.FilteredAndUnfilteredSocketsName);
		NumSocketsAndFiltered.Bind(ParameterMap, *ParamNames.NumSocketsAndFilteredName);
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		
		{
			FNiagaraDataInterfaceProxyStaticMesh* InterfaceProxy = static_cast<FNiagaraDataInterfaceProxyStaticMesh*>(Context.DataInterface);
			FNiagaraStaticMeshData* Data = InterfaceProxy->SystemInstancesToMeshData.Find(Context.SystemInstanceID);
			ensureMsgf(Data, TEXT("Failed to find data for instance %s"), *FNiagaraUtilities::SystemInstanceIDToString(Context.SystemInstanceID));

			if (Data)
			{
				const float InvDeltaTime = Data->DeltaSeconds > 0.0f ? 1.0f / Data->DeltaSeconds : 0.0f;
				const FVector DeltaPosition = Data->Transform.GetOrigin() - Data->PrevTransform.GetOrigin();

				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransform, Data->Transform);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransformInverseTransposed, Data->Transform.Inverse().GetTransposed());
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevTransform, Data->PrevTransform);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceRotation, Data->Rotation);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevRotation, Data->PrevRotation);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceInvDeltaTime, InvDeltaTime);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceWorldVelocity, DeltaPosition * InvDeltaTime);
				SetShaderValue(RHICmdList, ComputeShaderRHI, AreaWeightedSampling, Data->bIsGpuUniformlyDistributedSampling ? 1 : 0);
			}
			else
			{
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransform, FMatrix::Identity);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransformInverseTransposed, FMatrix::Identity);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevTransform, FMatrix::Identity);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceRotation, FQuat::Identity);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevRotation, FQuat::Identity);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceInvDeltaTime, 1.0f);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceWorldVelocity, FVector::ZeroVector);
				SetShaderValue(RHICmdList, ComputeShaderRHI, AreaWeightedSampling, 0);
			}

			FStaticMeshGpuSpawnBuffer* SpawnBuffer = Data ? Data->MeshGpuSpawnBuffer : nullptr;
			if (SpawnBuffer)
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshVertexBuffer, SpawnBuffer->GetBufferPositionSRV());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTangentBuffer, SpawnBuffer->GetBufferTangentSRV());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshIndexBuffer, SpawnBuffer->GetBufferIndexSRV());

				SetShaderValue(RHICmdList, ComputeShaderRHI, NumTexCoord, SpawnBuffer->GetNumTexCoord());
				if (SpawnBuffer->GetNumTexCoord() > 0)
				{
					SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, SpawnBuffer->GetBufferTexCoordSRV());
				}
				else
				{
					SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, FNiagaraRenderer::GetDummyFloat2Buffer());
				}

				if(SpawnBuffer->GetBufferColorSRV())
				{
					SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshColorBuffer, SpawnBuffer->GetBufferColorSRV());
					SetShaderValue(RHICmdList, ComputeShaderRHI, UseColorBuffer, 1);
				}
				else
				{
					SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshColorBuffer, FNiagaraRenderer::GetDummyWhiteColorBuffer());
					SetShaderValue(RHICmdList, ComputeShaderRHI, UseColorBuffer, 0);
				}

				SetShaderValue(RHICmdList, ComputeShaderRHI, SectionCount, SpawnBuffer->GetValidSectionCount());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSectionBuffer, SpawnBuffer->GetBufferSectionSRV());
				if (Data->bIsGpuUniformlyDistributedSampling)
				{
					SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleBuffer, SpawnBuffer->GetBufferUniformTriangleSamplingSRV());
				}
				else
				{
					SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleBuffer, FNiagaraRenderer::GetDummyUIntBuffer());
				}

				SetSRVParameter(RHICmdList, ComputeShaderRHI, SocketTransforms, SpawnBuffer->GetSocketTransformsSRV());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, FilteredAndUnfilteredSockets, SpawnBuffer->GetFilteredAndUnfilteredSocketsSRV());
				SetShaderValue(RHICmdList, ComputeShaderRHI, NumSocketsAndFiltered, FIntVector(SpawnBuffer->GetNumSockets(), SpawnBuffer->GetNumFilteredSockets(), SpawnBuffer->GetNumSockets() - SpawnBuffer->GetNumFilteredSockets()));
			}
			else
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshVertexBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTangentBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshIndexBuffer, FNiagaraRenderer::GetDummyUIntBuffer());

				SetShaderValue(RHICmdList, ComputeShaderRHI, NumTexCoord, 0);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, FNiagaraRenderer::GetDummyFloat2Buffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshColorBuffer, FNiagaraRenderer::GetDummyWhiteColorBuffer());
				SetShaderValue(RHICmdList, ComputeShaderRHI, UseColorBuffer, 0);

				SetShaderValue(RHICmdList, ComputeShaderRHI, SectionCount, 0);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSectionBuffer, FNiagaraRenderer::GetDummyUInt4Buffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleBuffer, FNiagaraRenderer::GetDummyUInt4Buffer());

				SetSRVParameter(RHICmdList, ComputeShaderRHI, SocketTransforms, FNiagaraRenderer::GetDummyFloat4Buffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, FilteredAndUnfilteredSockets, FNiagaraRenderer::GetDummyUIntBuffer());
				SetShaderValue(RHICmdList, ComputeShaderRHI, NumSocketsAndFiltered, FIntVector(ForceInitToZero));
			}
		}
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, MeshIndexBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshVertexBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshTangentBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshTexCoordBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshColorBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshSectionBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshTriangleBuffer);
	LAYOUT_FIELD(FShaderParameter, SectionCount);
	LAYOUT_FIELD(FShaderParameter, InstanceTransform);
	LAYOUT_FIELD(FShaderParameter, InstanceTransformInverseTransposed);
	LAYOUT_FIELD(FShaderParameter, InstancePrevTransform);
	LAYOUT_FIELD(FShaderParameter, InstanceRotation);
	LAYOUT_FIELD(FShaderParameter, InstancePrevRotation);
	LAYOUT_FIELD(FShaderParameter, InstanceInvDeltaTime);
	LAYOUT_FIELD(FShaderParameter, InstanceWorldVelocity);
	LAYOUT_FIELD(FShaderParameter, AreaWeightedSampling);
	LAYOUT_FIELD(FShaderParameter, NumTexCoord);
	LAYOUT_FIELD(FShaderParameter, UseColorBuffer);
	LAYOUT_FIELD(FShaderResourceParameter,SocketTransforms);
	LAYOUT_FIELD(FShaderResourceParameter,FilteredAndUnfilteredSockets);
	LAYOUT_FIELD(FShaderParameter, NumSocketsAndFiltered);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_StaticMesh);


IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceStaticMesh, FNiagaraDataInterfaceParametersCS_StaticMesh);

//////////////////////////////////////////////////////////////////////////

void FNiagaraDataInterfaceProxyStaticMesh::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance, FStaticMeshGpuSpawnBuffer* MeshGPUSpawnBuffer)
{
	check(IsInRenderingThread());
	check(!SystemInstancesToMeshData.Contains(SystemInstance));

	FNiagaraStaticMeshData& Data = SystemInstancesToMeshData.Add(SystemInstance);
	Data.MeshGpuSpawnBuffer = MeshGPUSpawnBuffer;
}

void FNiagaraDataInterfaceProxyStaticMesh::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());
	//check(SystemInstancesToMeshData.Contains(SystemInstance));
	SystemInstancesToMeshData.Remove(SystemInstance);
}

void FNiagaraDataInterfaceProxyStaticMesh::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FNiagaraPassedInstanceDataForRT* SourceData = static_cast<FNiagaraPassedInstanceDataForRT*>(PerInstanceData);
	FNiagaraStaticMeshData* Data = SystemInstancesToMeshData.Find(Instance);
	// @todo-threadsafety Verify we cannot ever reach here without valid data.
	ensure(Data);
	if (Data)
	{
		//UE_LOG(LogNiagara, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... found %s"), *Instance.ToString());

		Data->bIsGpuUniformlyDistributedSampling = SourceData->bIsGpuUniformlyDistributedSampling;
		Data->DeltaSeconds = SourceData->DeltaSeconds;
		Data->Transform = SourceData->Transform;
		Data->PrevTransform = SourceData->PrevTransform;
		Data->Rotation = SourceData->Rotation;
		Data->PrevRotation = SourceData->PrevRotation;
	}
	else
	{
		UE_LOG(LogNiagara, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %s"), *FNiagaraUtilities::SystemInstanceIDToString(Instance));
	}
	SourceData->~FNiagaraPassedInstanceDataForRT();
}

//////////////////////////////////////////////////////////////////////////

UNiagaraDataInterfaceStaticMesh::UNiagaraDataInterfaceStaticMesh(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, SourceMode(ENDIStaticMesh_SourceMode::Default)
#if WITH_EDITORONLY_DATA
	, PreviewMesh(nullptr)
#endif
	, DefaultMesh(nullptr)
	, Source(nullptr)
	, bUsePhysicsBodyVelocity(false)
	, ChangeId(0)
	//, bSupportingVertexColorSampling(0)//Vertex color filtering needs some more work.
	//, bFilterInitialized(false)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyStaticMesh());
}

void UNiagaraDataInterfaceStaticMesh::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags DIFlags =
			ENiagaraTypeRegistryFlags::AllowAnyVariable |
			ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), DIFlags);

		//Still some issues with using custom structs. Convert node for example throws a wobbler. TODO after GDC.
		ENiagaraTypeRegistryFlags CoordFlags =
			ENiagaraTypeRegistryFlags::AllowAnyVariable |
			ENiagaraTypeRegistryFlags::AllowParameter |
			ENiagaraTypeRegistryFlags::AllowPayload;
		FNiagaraTypeRegistry::Register(FMeshTriCoordinate::StaticStruct(), CoordFlags);
	}
}

#if WITH_EDITOR
void UNiagaraDataInterfaceStaticMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ChangeId++;

	if (PropertyChangedEvent.Property &&
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceStaticMesh, SourceMode))
	{
		if (SourceMode != ENDIStaticMesh_SourceMode::Default && SourceMode != ENDIStaticMesh_SourceMode::Source)
		{
			// Ensure we don't have any reference to a source actor that we'll never use
			Source = nullptr;
			SourceComponent = nullptr;
		}
	
		if (SourceMode != ENDIStaticMesh_SourceMode::Default && SourceMode != ENDIStaticMesh_SourceMode::DefaultMeshOnly)
		{
			// Ensure we don't cook in a default mesh we'll never use
			DefaultMesh = nullptr;
		}
	}
}

bool UNiagaraDataInterfaceStaticMesh::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceStaticMesh, Source) &&
		SourceMode != ENDIStaticMesh_SourceMode::Default &&
		SourceMode != ENDIStaticMesh_SourceMode::Source)
	{
		// Disable Source if we'll never use it
		return false;
	}

	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceStaticMesh, DefaultMesh) &&
		SourceMode != ENDIStaticMesh_SourceMode::Default &&
		SourceMode != ENDIStaticMesh_SourceMode::DefaultMeshOnly)
	{
		// Disable Default Mesh if we'll never use it
		return false;
	}

	return true;
}
#endif //WITH_EDITOR

namespace StaticMeshHelpers
{
	static const FName IsValidName("IsValid");
	static const FName RandomSectionName("RandomSection");
	static const FName RandomTriCoordName("RandomTriCoord");
	static const FName RandomTriCoordOnSectionName("RandomTriCoordOnSection");
	static const FName RandomTriCoordVCFilteredName("RandomTriCoordUsingVertexColorFilter");

	static const FName GetTriPositionName("GetTriPosition");
	static const FName GetTriNormalName("GetTriNormal");
	static const FName GetTriTangentsName("GetTriTangents");

	static const FName GetTriPositionWSName("GetTriPositionWS");
	static const FName GetTriNormalWSName("GetTriNormalWS");
	static const FName GetTriTangentsWSName("GetTriTangentsWS");

	static const FName GetTriColorName("GetTriColor");
	static const FName GetTriUVName("GetTriUV");

	static const FName GetTriPositionAndVelocityName("GetTriPositionAndVelocityWS");

	/** Temporary solution for exposing the transform of a mesh. Ideally this would be done by allowing interfaces to add to the uniform set for a simulation. */
	static const FName GetMeshLocalToWorldName("GetLocalToWorld");
	static const FName GetMeshLocalToWorldInverseTransposedName("GetMeshLocalToWorldInverseTransposed");
	static const FName GetMeshWorldVelocityName("GetWorldVelocity");

	static const FName GetVertexPositionName("GetVertexPosition"); 
	static const FName GetVertexPositionWSName("GetVertexPositionWS"); 

	static const FName GetSocketCountName(TEXT("GetSocketCount"));
	static const FName GetFilteredSocketCountName(TEXT("GetFilteredSocketCount"));
	static const FName GetUnfilteredSocketCountName(TEXT("GetUnfilteredSocketCount"));
	static const FName GetSocketTransformName(TEXT("GetSocketTransform"));
	static const FName GetFilteredSocketTransformName(TEXT("GetFilteredSocketTransform"));
	static const FName GetUnfilteredSocketTransformName(TEXT("GetUnfilteredSocketTransform"));
	static const FName GetSocketTransformWSName(TEXT("GetSocketTransformWS"));
	static const FName GetFilteredSocketTransformWSName(TEXT("GetFilteredSocketTransformWS"));
	static const FName GetUnfilteredSocketTransformWSName(TEXT("GetUnfilteredSocketTransformWS"));
};

void UNiagaraDataInterfaceStaticMesh::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::IsValidName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::RandomSectionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Section")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::RandomTriCoordName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::RandomTriCoordVCFilteredName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraTypeDefinition::GetFloatDef()), TEXT("Start")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraTypeDefinition::GetFloatDef()), TEXT("Range")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_RandomTriCoordVCFiltered", "If bSupportingVertexColorSampling is set on the data source, will randomly find a triangle whose red channel is within the Start to Start + Range color range."));
#endif
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::RandomTriCoordOnSectionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Section")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetTriPositionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetTriPositionAndVelocityName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetTriPositionWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetTriNormalName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetTriNormalWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetTriTangentsName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetTriTangentsWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetTriColorName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetTriUVName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("UV Set")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("UV")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetMeshLocalToWorldName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Transform")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetMeshLocalToWorldInverseTransposedName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Transform")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetMeshWorldVelocityName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetVertexPositionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetVertexPositionDesc", "Returns the local space vertex position for the passed vertex.");
#endif
	} 
	
	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = StaticMeshHelpers::GetVertexPositionWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetVertexPositionWSDesc", "Returns the world space vertex position for the passed vertex.");
#endif
	}

	//////////////////////////////////////////////////////////////////////////
	// Socket functions
	{
		FNiagaraFunctionSignature Sig;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;

		OutFunctions.Add_GetRef(Sig).Name = StaticMeshHelpers::GetSocketCountName;
		OutFunctions.Add_GetRef(Sig).Name = StaticMeshHelpers::GetFilteredSocketCountName;
		OutFunctions.Add_GetRef(Sig).Name = StaticMeshHelpers::GetUnfilteredSocketCountName;
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Scale")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;

		OutFunctions.Add_GetRef(Sig).Name = StaticMeshHelpers::GetSocketTransformName;
		OutFunctions.Add_GetRef(Sig).Name = StaticMeshHelpers::GetSocketTransformWSName;
		OutFunctions.Add_GetRef(Sig).Name = StaticMeshHelpers::GetFilteredSocketTransformName;
		OutFunctions.Add_GetRef(Sig).Name = StaticMeshHelpers::GetFilteredSocketTransformWSName;
		OutFunctions.Add_GetRef(Sig).Name = StaticMeshHelpers::GetUnfilteredSocketTransformName;
		OutFunctions.Add_GetRef(Sig).Name = StaticMeshHelpers::GetUnfilteredSocketTransformWSName;
	}
}

//External function binder choosing between template specializations based on UsesAreaWeighting
template<typename NextBinder>
struct TSampleModeBinder
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		FNDIStaticMesh_InstanceData* InstData = (FNDIStaticMesh_InstanceData*)InstanceData;
		UNiagaraDataInterfaceStaticMesh* MeshInterface = CastChecked<UNiagaraDataInterfaceStaticMesh>(Interface);
		if (!InstData->bMeshValid)
		{
			NextBinder::template Bind<ParamTypes..., TSampleModeInvalid>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else if (InstData->UsesCpuUniformlyDistributedSampling())
		{
			NextBinder::template Bind<ParamTypes..., TSampleModeAreaWeighted>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NextBinder::template Bind<ParamTypes..., TSampleModeDefault>(Interface, BindingInfo, InstanceData, OutFunc);
		}
	}
};

//Helper struct for stubbing access of vertex data.
struct TNullMeshVertexAccessor
{
	TNullMeshVertexAccessor(const FStaticMeshVertexBuffer&) {}

	FORCEINLINE FVector GetTangentX(int32 Idx)const { return FVector4(1.0f, 0.0f, 0.0f, 0.0f); }
	FORCEINLINE FVector GetTangentY(int32 Idx)const { return FVector4(0.0f, 1.0f, 0.0f, 0.0f); }
	FORCEINLINE FVector GetTangentZ(int32 Idx)const { return FVector4(0.0f, 0.0f, 1.0f, 0.0f); }
	FORCEINLINE FVector2D GetUV(int32 Idx, int32 UVSet)const { return FVector2D(0.0f, 0.0f); }
};

//Helper struct for accessing typed vertex data.
template<EStaticMeshVertexTangentBasisType TangentT, EStaticMeshVertexUVType UVTypeT>
struct TTypedMeshVertexAccessor
{
	const FStaticMeshVertexBuffer& Verts;
	TTypedMeshVertexAccessor(const FStaticMeshVertexBuffer& InVerts)
		: Verts(InVerts)
	{}

	FORCEINLINE FVector GetTangentX(int32 Idx)const { return Verts.VertexTangentX_Typed<TangentT>(Idx); }
	FORCEINLINE FVector GetTangentY(int32 Idx)const { return Verts.VertexTangentY_Typed<TangentT>(Idx); }
	FORCEINLINE FVector GetTangentZ(int32 Idx)const { return Verts.VertexTangentZ_Typed<TangentT>(Idx); }
	FORCEINLINE FVector2D GetUV(int32 Idx, int32 UVSet)const { return Verts.GetVertexUV_Typed<UVTypeT>(Idx, UVSet); }
};

//External function binder choosing between template specializations based on the mesh's vertex type.
template<typename NextBinder>
struct TTypedMeshAccessorBinder
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		FNDIStaticMesh_InstanceData* InstData = (FNDIStaticMesh_InstanceData*)InstanceData;
		check(InstData);

		TRefCountPtr<const FStaticMeshLODResources> Res;
		if (InstData->bMeshValid)
		{
			Res = InstData->GetCurrentFirstLOD();
		}
		
		if (!Res.IsValid())
		{
			NextBinder::template Bind<ParamTypes..., TNullMeshVertexAccessor>(Interface, BindingInfo, InstanceData, OutFunc);
			return;
		}

		UNiagaraDataInterfaceStaticMesh* MeshInterface = CastChecked<UNiagaraDataInterfaceStaticMesh>(Interface);
		if (Res->VertexBuffers.StaticMeshVertexBuffer.GetUseHighPrecisionTangentBasis())			
		{
			if (Res->VertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
			{
				NextBinder::template Bind<ParamTypes..., TTypedMeshVertexAccessor<EStaticMeshVertexTangentBasisType::HighPrecision, EStaticMeshVertexUVType::HighPrecision>>(Interface, BindingInfo, InstanceData, OutFunc);
			}
			else
			{
				NextBinder::template Bind<ParamTypes..., TTypedMeshVertexAccessor<EStaticMeshVertexTangentBasisType::HighPrecision, EStaticMeshVertexUVType::Default>>(Interface, BindingInfo, InstanceData, OutFunc);
			}
		}
		else
		{
			if (Res->VertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
			{
				NextBinder::template Bind<ParamTypes..., TTypedMeshVertexAccessor<EStaticMeshVertexTangentBasisType::Default, EStaticMeshVertexUVType::HighPrecision>>(Interface, BindingInfo, InstanceData, OutFunc);
			}
			else
			{
				NextBinder::template Bind<ParamTypes..., TTypedMeshVertexAccessor<EStaticMeshVertexTangentBasisType::Default, EStaticMeshVertexUVType::Default>>(Interface, BindingInfo, InstanceData, OutFunc);
			}
		}
	}
};

//Final binders for all static mesh interface functions.
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, IsValid);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomSection);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomTriCoord);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomTriCoordVertexColorFiltered);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomTriCoordOnSection);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordPosition);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordNormal);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordTangents);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordColor);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordUV);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordPositionAndVelocity);

DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetVertexPosition);

void UNiagaraDataInterfaceStaticMesh::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	FNDIStaticMesh_InstanceData* InstData = (FNDIStaticMesh_InstanceData*)InstanceData;
	check(InstData);
	
	if (BindingInfo.Name == StaticMeshHelpers::IsValidName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, IsValid)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::RandomSectionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		TSampleModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomSection)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::RandomTriCoordName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 4);
		TSampleModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomTriCoord)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	//TODO: Vertex color filtering needs more work.
	else if (BindingInfo.Name == StaticMeshHelpers::RandomTriCoordVCFilteredName)
	{
		InstData->InitVertexColorFiltering();
		check(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomTriCoordVertexColorFiltered)::Bind(this, OutFunc);
	}	
	else if (BindingInfo.Name == StaticMeshHelpers::RandomTriCoordOnSectionName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 4);
		TSampleModeBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomTriCoordOnSection)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriPositionName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandlerNoop, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordPosition)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriPositionWSName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandler, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordPosition)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriNormalName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandlerNoop, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordNormal)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriNormalWSName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandler, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordNormal)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriTangentsName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 9);
		TTypedMeshAccessorBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordTangents)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriTangentsWSName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 9);
		TTypedMeshAccessorBinder<TNDIExplicitBinder<FNDITransformHandler, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordTangents)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriColorName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordColor)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriUVName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 2);
		TTypedMeshAccessorBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordUV)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriPositionAndVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 6);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordPositionAndVelocity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetMeshLocalToWorldName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 16);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceStaticMesh::GetLocalToWorld);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetMeshLocalToWorldInverseTransposedName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 16);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceStaticMesh::GetLocalToWorldInverseTransposed);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetMeshWorldVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceStaticMesh::GetWorldVelocity);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetVertexPositionName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandlerNoop, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetVertexPosition)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetVertexPositionWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandler, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetVertexPosition)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	//////////////////////////////////////////////////////////////////////////
	// Socket Functions
	else if (BindingInfo.Name == StaticMeshHelpers::GetSocketCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetSocketCount(Context); });
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetFilteredSocketCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetFilteredSocketCount(Context); });
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetUnfilteredSocketCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetUnfilteredSocketCount(Context); });
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetSocketTransformName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetSocketTransform<false>(Context); });
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetSocketTransformWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetSocketTransform<true>(Context); });
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetFilteredSocketTransformName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetFilteredSocketTransform<false>(Context); });
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetFilteredSocketTransformWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetFilteredSocketTransform<true>(Context); });
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetUnfilteredSocketTransformName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetUnfilteredSocketTransform<false>(Context); });
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetUnfilteredSocketTransformWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10);
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMContext& Context) { this->GetUnfilteredSocketTransform<true>(Context); });
	}
}

bool UNiagaraDataInterfaceStaticMesh::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceStaticMesh* OtherTyped = CastChecked<UNiagaraDataInterfaceStaticMesh>(Destination);
	OtherTyped->SourceMode = SourceMode;
	OtherTyped->Source = Source;
	OtherTyped->SourceComponent = SourceComponent;
	OtherTyped->DefaultMesh = DefaultMesh;
#if WITH_EDITORONLY_DATA
	OtherTyped->PreviewMesh = PreviewMesh;
#endif
	//OtherTyped->bEnableVertexColorRangeSorting = bEnableVertexColorRangeSorting;//TODO: Vertex color filtering needs more work.
	OtherTyped->SectionFilter = SectionFilter;
	OtherTyped->bUsePhysicsBodyVelocity = bUsePhysicsBodyVelocity;
	OtherTyped->FilteredSockets = FilteredSockets;
	return true;
}

bool UNiagaraDataInterfaceStaticMesh::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceStaticMesh* OtherTyped = CastChecked<const UNiagaraDataInterfaceStaticMesh>(Other);
	return OtherTyped->SourceMode == SourceMode &&
		OtherTyped->Source == Source &&
		OtherTyped->SourceComponent == SourceComponent &&
		OtherTyped->DefaultMesh == DefaultMesh &&
		//OtherTyped->bEnableVertexColorRangeSorting == bEnableVertexColorRangeSorting &&//TODO: Vertex color filtering needs more work.
		OtherTyped->SectionFilter.AllowedMaterialSlots == SectionFilter.AllowedMaterialSlots &&
		OtherTyped->bUsePhysicsBodyVelocity == bUsePhysicsBodyVelocity &&
		OtherTyped->FilteredSockets == FilteredSockets;
}

bool UNiagaraDataInterfaceStaticMesh::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIStaticMesh_InstanceData* Inst = new (PerInstanceData) FNDIStaticMesh_InstanceData();
	bool bSuccess = Inst->Init(this, SystemInstance);

	//UE_LOG(LogNiagara, Log, TEXT("GT: StaticMesh DI - InitPerInstanceData %s"), *SystemInstance->GetId().ToString());

	if (bSuccess)
	{
		FStaticMeshGpuSpawnBuffer* MeshGpuSpawnBuffer = nullptr;
		TRefCountPtr<const FStaticMeshLODResources> GpuMeshLODResource;
		if (Inst->bMeshValid)
		{
			GpuMeshLODResource = Inst->GetCurrentFirstLOD();
		}

		if (GpuMeshLODResource.IsValid() && IsUsedWithGPUEmitter(SystemInstance))
		{
			// Always allocate when bAllowCPUAccess (index buffer can only have SRV created in this case as of today)
			// We do not know if this interface is allocated for CPU or GPU so we allocate for both case... TODO: have some cached data created in case a GPU version is needed?
			ensure(Inst->StaticMesh->bAllowCPUAccess); // this should have been verified in Init()

			MeshGpuSpawnBuffer = new FStaticMeshGpuSpawnBuffer;
			MeshGpuSpawnBuffer->Initialise(GpuMeshLODResource, *this, Inst);
		}

		// Push instance data to RT
		{
			FNiagaraDataInterfaceProxyStaticMesh* ThisProxy = GetProxyAs<FNiagaraDataInterfaceProxyStaticMesh>();
			ENQUEUE_RENDER_COMMAND(FNiagaraDIPushInitialInstanceDataToRT) (
				[ThisProxy, InstanceID=SystemInstance->GetId(), MeshGpuSpawnBuffer, RT_MeshLODResource=GpuMeshLODResource](FRHICommandListImmediate& CmdList)
				{
					if (MeshGpuSpawnBuffer)
					{
						MeshGpuSpawnBuffer->InitResource();
					}
					ThisProxy->InitializePerInstanceData(InstanceID, MeshGpuSpawnBuffer);
					// We don't use RT_MeshLODResource but it ensures the data has not been streamed out
				}
			);
		}
	}

	return bSuccess;
}

void UNiagaraDataInterfaceStaticMesh::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIStaticMesh_InstanceData* Inst = (FNDIStaticMesh_InstanceData*)PerInstanceData;

	//UE_LOG(LogNiagara, Log, TEXT("GT: StaticMesh DI - DestroyPerInstanceData %s"), *SystemInstance->GetId().ToString());

#if WITH_EDITOR
	if (Inst->StaticMesh.IsValid())
	{
		if (UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(SystemInstance->GetAttachComponent()))
		{
			Inst->StaticMesh->GetOnMeshChanged().RemoveAll(NiagaraComponent);
		}
	}
#endif

	Inst->Release();
	Inst->~FNDIStaticMesh_InstanceData();

	{
		FNiagaraDataInterfaceProxyStaticMesh* ThisProxy = GetProxyAs<FNiagaraDataInterfaceProxyStaticMesh>();
		ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
			[ThisProxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
			{
				ThisProxy->DestroyPerInstanceData(Batcher, InstanceID);
			}
		);
	}
}

bool UNiagaraDataInterfaceStaticMesh::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIStaticMesh_InstanceData* Inst = (FNDIStaticMesh_InstanceData*)PerInstanceData;
	return Inst->Tick(this, SystemInstance, InDeltaSeconds);
}

#if WITH_EDITOR	
void UNiagaraDataInterfaceStaticMesh::GetFeedback(UNiagaraSystem* Asset, UNiagaraComponent* Component, TArray<FNiagaraDataInterfaceError>& OutErrors,
	TArray<FNiagaraDataInterfaceFeedback>& OutWarnings, TArray<FNiagaraDataInterfaceFeedback>& OutInfo)
{
	if (Source == nullptr && DefaultMesh != nullptr && !DefaultMesh->bAllowCPUAccess)
	{
		FNiagaraDataInterfaceError CPUAccessNotAllowedError(FText::Format(LOCTEXT("CPUAccessNotAllowedError", "This mesh needs CPU access in order to be used properly.({0})"), FText::FromString(DefaultMesh->GetName())),
			LOCTEXT("CPUAccessNotAllowedErrorSummary", "CPU access error"),
			FNiagaraDataInterfaceFix::CreateLambda([=]()
		{
			DefaultMesh->Modify();
			DefaultMesh->bAllowCPUAccess = true;
			return true;
		}));

		OutErrors.Add(CPUAccessNotAllowedError);
	}

	bool bHasNoMeshAssignedWarning = (Source == nullptr && DefaultMesh == nullptr);
#if WITH_EDITORONLY_DATA
	if (bHasNoMeshAssignedWarning)
	{
		if (UStaticMesh* LocalPreviewMesh = PreviewMesh.LoadSynchronous())
		{
			bHasNoMeshAssignedWarning = false;

			if (!LocalPreviewMesh->bAllowCPUAccess)
			{
				FNiagaraDataInterfaceError CPUAccessNotAllowedError(FText::Format(LOCTEXT("CPUAccessNotAllowedError", "This mesh needs CPU access in order to be used properly.({0})"), FText::FromString(LocalPreviewMesh->GetName())),
					LOCTEXT("CPUAccessNotAllowedErrorSummary", "CPU access error"),
					FNiagaraDataInterfaceFix::CreateLambda([=]()
					{
						LocalPreviewMesh->Modify();
						LocalPreviewMesh->bAllowCPUAccess = true;
						return true;
					}));

				OutErrors.Add(CPUAccessNotAllowedError);
			}
		}
	}
#endif

	if (bHasNoMeshAssignedWarning)
	{
		FNiagaraDataInterfaceFeedback NoMeshAssignedError(LOCTEXT("NoMeshAssignedError", "This Data Interface should be assigned a static mesh to operate correctly."),
			LOCTEXT("NoMeshAssignedErrorSummary", "No mesh assigned warning"),
			FNiagaraDataInterfaceFix());

		OutWarnings.Add(NoMeshAssignedError);
	}
}
#endif

UStaticMesh* UNiagaraDataInterfaceStaticMesh::GetStaticMesh(TWeakObjectPtr<USceneComponent>& OutComponent, FNiagaraSystemInstance* SystemInstance)
{
	// Helper to scour an actor (or its parents) for a valid Static mesh component
	auto FindActorMeshComponent = [](AActor* Actor, bool bRecurseParents = false) -> UStaticMeshComponent*
	{
		if (AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
		{
			UStaticMeshComponent* Comp = MeshActor->GetStaticMeshComponent();
			if (Comp && !Comp->IsPendingKill())
			{
				return Comp;
			}
		}

		// Fall back on any valid component on the actor
		while (Actor)
		{
			for (UActorComponent* ActorComp : Actor->GetComponents())
			{
				UStaticMeshComponent* Comp = Cast<UStaticMeshComponent>(ActorComp);
				if (Comp && !Comp->IsPendingKill() && Comp->GetStaticMesh() != nullptr)
				{
					return Comp;
				}
			}

			if (bRecurseParents)
			{
				Actor = Actor->GetParentActor();
			}
			else
			{
				break;
			}
		}

		return nullptr;
	};

	UStaticMeshComponent* FoundMeshComponent = nullptr;	

	const bool bTrySource = SourceMode == ENDIStaticMesh_SourceMode::Default || SourceMode == ENDIStaticMesh_SourceMode::Source;
	const bool bTryAttachParent = SourceMode == ENDIStaticMesh_SourceMode::Default || SourceMode == ENDIStaticMesh_SourceMode::AttachParent;
	const bool bTryDefaultMesh = SourceMode == ENDIStaticMesh_SourceMode::Default || SourceMode == ENDIStaticMesh_SourceMode::DefaultMeshOnly;

	if (bTrySource && SourceComponent && !SourceComponent->IsPendingKill())
	{
		FoundMeshComponent = SourceComponent;
	}
	else if (bTrySource && Source)
	{
		FoundMeshComponent = FindActorMeshComponent(Source);
	}
	else if (bTryAttachParent && SystemInstance)
	{
		if (USceneComponent* AttachComponent = SystemInstance->GetAttachComponent())
		{
			// First, try to find the mesh component up the attachment hierarchy
			for (USceneComponent* Curr = AttachComponent; Curr; Curr = Curr->GetAttachParent())
			{
				UStaticMeshComponent* ParentComp = Cast<UStaticMeshComponent>(Curr);
				if (ParentComp && !ParentComp->IsPendingKill())
				{
					FoundMeshComponent = ParentComp;
					break;
				}
			}
			
			if (!FoundMeshComponent)
			{
				// Next, try to find one in our outer chain
				UStaticMeshComponent* OuterComp = AttachComponent->GetTypedOuter<UStaticMeshComponent>();
				if (OuterComp && !OuterComp->IsPendingKill())
				{
					FoundMeshComponent = OuterComp;
				}
				else if (AActor* Actor = AttachComponent->GetAttachmentRootActor())
				{
					// Final fall-back, look for any mesh component on our root actor or any of its parents
					FoundMeshComponent = FindActorMeshComponent(Actor, true);
				}
			}
		}
	}

	UStaticMesh* Mesh = nullptr;
	OutComponent = nullptr;
	if (FoundMeshComponent)
	{
		Mesh = FoundMeshComponent->GetStaticMesh();
		OutComponent = FoundMeshComponent;
	}
	else if (bTryDefaultMesh)
	{
		Mesh = DefaultMesh;
	}

#if WITH_EDITORONLY_DATA
	if (!Mesh && !FoundMeshComponent && (!SystemInstance || !SystemInstance->GetWorld()->IsGameWorld()))
	{
		// NOTE: We don't fall back on the preview mesh if we have a valid static mesh component referenced
		Mesh = PreviewMesh.LoadSynchronous();		
	}
#endif

	return Mesh;
}

//Whether or not there is valid mesh data on this interface
void UNiagaraDataInterfaceStaticMesh::IsValid(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);
	
	FNiagaraBool Valid;
	Valid.SetValue(InstData->bMeshValid);
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutValid.GetDest() = Valid;
		OutValid.Advance();
	}
}

//RandomSection specializations.
//Each combination for SampleMode and Section filtered. NOTE: Invalid sample mode left intentionally unimplemented
template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomSection<TSampleModeAreaWeighted, true>(FRandomStream& RandStream, const FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	if (InstData->GetAreaWeightedSampler().GetNumEntries() > 0)
	{
		int32 Idx = InstData->GetAreaWeightedSampler().GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
		return InstData->GetValidSections()[Idx];
	}
	else
	{
		return 0;
	}
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomSection<TSampleModeAreaWeighted, false>(FRandomStream& RandStream, const FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	if (Res.AreaWeightedSampler.GetNumEntries() > 0)
	{
		return Res.AreaWeightedSampler.GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
	}
	else
	{
		return 0;
	}
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomSection<TSampleModeDefault, true>(FRandomStream& RandStream, const FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	int32 Idx = RandStream.RandRange(0, InstData->GetValidSections().Num() - 1);
	return InstData->GetValidSections()[Idx];
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomSection<TSampleModeDefault, false>(FRandomStream& RandStream, const FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	return RandStream.RandRange(0, Res.Sections.Num() - 1);
}

template<typename TSampleMode>
void UNiagaraDataInterfaceStaticMesh::RandomSection(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutSection(Context);

	TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutSection.GetDestAndAdvance() = RandomSection<TSampleMode, true>(Context.RandStream, *Res, InstData);
	}
}

// Invalid mesh specialization
template<>
void UNiagaraDataInterfaceStaticMesh::RandomSection<TSampleModeInvalid>(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutSection(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutSection.GetDestAndAdvance() = -1;
	}
}

//RandomTriIndex specializations.
//Each combination for SampleMode and Section filtered. NOTE: Invalid sample mode left intentionally unimplemented
template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndex<TSampleModeAreaWeighted, true>(FRandomStream& RandStream, const FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	int32 SecIdx = RandomSection<TSampleModeAreaWeighted, true>(RandStream, Res, InstData);
	if (SecIdx < Res.Sections.Num() && SecIdx < Res.AreaWeightedSectionSamplers.Num())
	{
		const FStaticMeshSection&  Sec = Res.Sections[SecIdx];
		if (Res.AreaWeightedSectionSamplers[SecIdx].GetNumEntries() > 0)
		{
			int32 Tri = Res.AreaWeightedSectionSamplers[SecIdx].GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
			return (Sec.FirstIndex / 3) + Tri;
		}
		return (Sec.FirstIndex / 3);
	}
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndex<TSampleModeAreaWeighted, false>(FRandomStream& RandStream, const FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	int32 SecIdx = RandomSection<TSampleModeAreaWeighted, false>(RandStream, Res, InstData);
	if (SecIdx < Res.Sections.Num() && SecIdx < Res.AreaWeightedSectionSamplers.Num())
	{
		const FStaticMeshSection&  Sec = Res.Sections[SecIdx];
		if (Res.AreaWeightedSectionSamplers[SecIdx].GetNumEntries() > 0)
		{
			int32 Tri = Res.AreaWeightedSectionSamplers[SecIdx].GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
			return (Sec.FirstIndex / 3) + Tri;
		}
		return (Sec.FirstIndex / 3);
	}
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndex<TSampleModeDefault, true>(FRandomStream& RandStream, const FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	int32 SecIdx = RandomSection<TSampleModeDefault, true>(RandStream, Res, InstData);
	if (SecIdx < Res.Sections.Num())
	{
		const FStaticMeshSection&  Sec = Res.Sections[SecIdx];
		int32 Tri = RandStream.RandRange(0, Sec.NumTriangles - 1);
		return (Sec.FirstIndex / 3) + Tri;
	}
	return 0;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndex<TSampleModeDefault, false>(FRandomStream& RandStream, const FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	int32 SecIdx = RandomSection<TSampleModeDefault, false>(RandStream, Res, InstData);
	if (SecIdx < Res.Sections.Num())
	{
		const FStaticMeshSection&  Sec = Res.Sections[SecIdx];
		int32 Tri = RandStream.RandRange(0, Sec.NumTriangles - 1);
		return (Sec.FirstIndex / 3) + Tri;
	}
	return 0;
}

template<typename TSampleMode>
void UNiagaraDataInterfaceStaticMesh::RandomTriCoord(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	FNDIOutputParam<int32> OutTri(Context);
	FNDIOutputParam<FVector> OutBary(Context);

	check(InstData->StaticMesh.IsValid());
	TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutTri.SetAndAdvance(RandomTriIndex<TSampleMode, true>(Context.RandStream, *Res, InstData));
		OutBary.SetAndAdvance(RandomBarycentricCoord(Context.RandStream));
	}
}

// Invalid mesh specialization
template<>
void UNiagaraDataInterfaceStaticMesh::RandomTriCoord<TSampleModeInvalid>(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	FNDIOutputParam<int32> OutTri(Context);
	FNDIOutputParam<FVector> OutBary(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutTri.SetAndAdvance(-1);
		OutBary.SetAndAdvance(FVector::ZeroVector);
	}
}

void UNiagaraDataInterfaceStaticMesh::RandomTriCoordVertexColorFiltered(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> MinValue(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> RangeValue(Context);

	FNDIOutputParam<int32> OutTri(Context);
	FNDIOutputParam<FVector> OutBary(Context);
	
	// Handle no mesh case
	//TODO: Maybe figure out a good way to stub this in bindings to prevent the branch
	if (!InstData->bMeshValid)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutTri.SetAndAdvance(-1);
			OutBary.SetAndAdvance(FVector::ZeroVector);
		}
		return;
	}
	
	FDynamicVertexColorFilterData* VCFData = InstData->DynamicVertexColorSampler.Get();
	TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
	FIndexArrayView Indices = Res->IndexBuffer.GetArrayView();

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		uint32 StartIdx = (uint32)(MinValue.GetAndAdvance()*255.0f);
		uint32 Range = (uint32)(RangeValue.GetAndAdvance()*255.0f + 0.5f);
		uint32 EndIdx = StartIdx + Range;
		// Iterate over the bucketed range and find the total number of triangles in the list.
		uint32 NumTris = 0;

		// Unfortunately, there's always the chance that the user gave us a range and value that don't have any vertex color matches.
		// In this case (hopefully rare), we keep expanding the search space until we find a valid value.
		while (NumTris == 0)
		{
			StartIdx = FMath::Clamp<uint32>(StartIdx, 0, (uint32)VCFData->VertexColorToTriangleStart.Num() - 1);
			EndIdx = FMath::Clamp<uint32>(EndIdx, StartIdx, (uint32)VCFData->VertexColorToTriangleStart.Num() - 1);
			NumTris = (EndIdx < (uint32)VCFData->VertexColorToTriangleStart.Num() - 1) ? (VCFData->VertexColorToTriangleStart[EndIdx + 1] - VCFData->VertexColorToTriangleStart[StartIdx]) :
				(uint32)VCFData->TrianglesSortedByVertexColor.Num() - VCFData->VertexColorToTriangleStart[StartIdx];

			if (NumTris == 0)
			{
				if (StartIdx > 0)
				{
					StartIdx -= 1;
				}
				Range += 1;
				EndIdx = StartIdx + Range;
			}
		}

		// Select a random triangle from the list.
		uint32 RandomTri = Context.RandStream.GetFraction()*NumTris;

		// Now emit that triangle...
		OutTri.SetAndAdvance(VCFData->TrianglesSortedByVertexColor[VCFData->VertexColorToTriangleStart[StartIdx] + RandomTri]);

		OutBary.SetAndAdvance(RandomBarycentricCoord(Context.RandStream));
	}
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndexOnSection<TSampleModeAreaWeighted>(FRandomStream& RandStream, const FStaticMeshLODResources& Res, int32 SecIdx, FNDIStaticMesh_InstanceData* InstData)
{
	const FStaticMeshSection&  Sec = Res.Sections[SecIdx];
	int32 Tri = Res.AreaWeightedSectionSamplers[SecIdx].GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
	return (Sec.FirstIndex / 3) + Tri;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndexOnSection<TSampleModeDefault>(FRandomStream& RandStream, const FStaticMeshLODResources& Res, int32 SecIdx, FNDIStaticMesh_InstanceData* InstData)
{
	const FStaticMeshSection&  Sec = Res.Sections[SecIdx];
	int32 Tri = RandStream.RandRange(0, Sec.NumTriangles - 1);
	return (Sec.FirstIndex / 3) + Tri;
}

template<typename TSampleMode>
void UNiagaraDataInterfaceStaticMesh::RandomTriCoordOnSection(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> SectionIdxParam(Context);

	FNDIOutputParam<int32> OutTri(Context);
	FNDIOutputParam<FVector> OutBary(Context);

	// This is handled on bind
	check(InstData->bMeshValid);

	TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
	FIndexArrayView Indices = Res->IndexBuffer.GetArrayView();
	const int32 MaxSection = Res->Sections.Num() - 1;
	if (MaxSection >= 0)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			int32 SecIdx = FMath::Clamp(SectionIdxParam.GetAndAdvance(), 0, MaxSection);
			OutTri.SetAndAdvance(RandomTriIndexOnSection<TSampleMode>(Context.RandStream, *Res, SecIdx, InstData));
			OutBary.SetAndAdvance(RandomBarycentricCoord(Context.RandStream));
		}
		// Early out as we are done
		return;
	}

	// Fall through that handles missing or invalid data
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutTri.SetAndAdvance(-1);
		OutBary.SetAndAdvance(FVector::ZeroVector);
	}
}

// Invalid mesh specialization
template<>
void UNiagaraDataInterfaceStaticMesh::RandomTriCoordOnSection<TSampleModeInvalid>(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> SectionIdxParam(Context);

	FNDIOutputParam<int32> OutTri(Context);
	FNDIOutputParam<FVector> OutBary(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutTri.SetAndAdvance(-1);
		OutBary.SetAndAdvance(FVector::ZeroVector);
	}
}

template<typename TransformHandlerType>
void UNiagaraDataInterfaceStaticMesh::GetTriCoordPosition(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	TransformHandlerType TransformHandler;
	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);

	FNDIOutputParam<FVector> OutPos(Context);

	if (InstData->bMeshValid)
	{
		TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
		const FIndexArrayView& Indices = Res->IndexBuffer.GetArrayView();
		const FPositionVertexBuffer& Positions = Res->VertexBuffers.PositionVertexBuffer;
		if ( (Indices.Num() > 0) && (Positions.GetNumVertices() > 0) && Positions.GetVertexData() )
		{
			const int32 NumTriangles = Indices.Num() / 3;
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 Tri = (TriParam.GetAndAdvance() % NumTriangles) * 3;
				const int32 Idx0 = Indices[Tri];
				const int32 Idx1 = Indices[Tri + 1];
				const int32 Idx2 = Indices[Tri + 2];

				FVector Pos = BarycentricInterpolate(BaryParam.GetAndAdvance(), Positions.VertexPosition(Idx0), Positions.VertexPosition(Idx1), Positions.VertexPosition(Idx2));
				TransformHandler.TransformPosition(Pos, InstData->Transform);

				OutPos.SetAndAdvance(Pos);
			}
			// Early out as we are done
			return;
		}
	}

	// Fall through that handles missing or invalid data
	FVector Pos(0.0f);
	TransformHandler.TransformPosition(Pos, InstData->Transform);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutPos.SetAndAdvance(Pos);
	}
}

template<typename TransformHandlerType>
void UNiagaraDataInterfaceStaticMesh::GetTriCoordNormal(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	TransformHandlerType TransformHandler;

	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);

	FNDIOutputParam<FVector> OutNorm(Context);

	if (InstData->bMeshValid)
	{
		TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
		const FIndexArrayView& Indices = Res->IndexBuffer.GetArrayView();
		const FStaticMeshVertexBuffer& Verts = Res->VertexBuffers.StaticMeshVertexBuffer;
		if ((Indices.Num() > 0) && (Verts.GetNumVertices() > 0) && Verts.GetTangentData())
		{
			const int32 NumTriangles = Indices.Num() / 3;
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 Tri = (TriParam.GetAndAdvance() % NumTriangles) * 3;
				const int32 Idx0 = Indices[Tri];
				const int32 Idx1 = Indices[Tri + 1];
				const int32 Idx2 = Indices[Tri + 2];

				FVector Norm = BarycentricInterpolate(BaryParam.GetAndAdvance(), Verts.VertexTangentZ(Idx0), Verts.VertexTangentZ(Idx1), Verts.VertexTangentZ(Idx2));
				TransformHandler.TransformVector(Norm, InstData->TransformInverseTransposed);

				OutNorm.SetAndAdvance(Norm);
			}
			// Early out as we are done
			return;
		}
	}

	// Fall through that handles missing or invalid data
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutNorm.SetAndAdvance(FVector::ZeroVector);
	}
}

template<typename VertexAccessorType, typename TransformHandlerType>
void UNiagaraDataInterfaceStaticMesh::GetTriCoordTangents(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	TransformHandlerType TransformHandler;

	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);

	FNDIOutputParam<FVector> OutTangent(Context);
	FNDIOutputParam<FVector> OutBinorm(Context);
	FNDIOutputParam<FVector> OutNorm(Context);

	if (InstData->bMeshValid)
	{
		TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
		const FIndexArrayView& Indices = Res->IndexBuffer.GetArrayView();
		const VertexAccessorType Verts(Res->VertexBuffers.StaticMeshVertexBuffer);
		if ((Indices.Num() > 0) && (Res->VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() > 0) && Res->VertexBuffers.StaticMeshVertexBuffer.GetTangentData())
		{
			const int32 NumTriangles = Indices.Num() / 3;
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 Tri = (TriParam.GetAndAdvance() % NumTriangles) * 3;
				const int32 Idx0 = Indices[Tri];
				const int32 Idx1 = Indices[Tri + 1];
				const int32 Idx2 = Indices[Tri + 2];
				const FVector BaryCoord(BaryParam.GetAndAdvance());
				FVector Tangent = BarycentricInterpolate(BaryCoord, Verts.GetTangentX(Idx0), Verts.GetTangentX(Idx1), Verts.GetTangentX(Idx2));
				FVector Binorm = BarycentricInterpolate(BaryCoord, Verts.GetTangentY(Idx0), Verts.GetTangentY(Idx1), Verts.GetTangentY(Idx2));
				FVector Norm = BarycentricInterpolate(BaryCoord, Verts.GetTangentZ(Idx0), Verts.GetTangentZ(Idx1), Verts.GetTangentZ(Idx2));
				TransformHandler.TransformVector(Tangent, InstData->TransformInverseTransposed);
				TransformHandler.TransformVector(Binorm, InstData->TransformInverseTransposed);
				TransformHandler.TransformVector(Norm, InstData->TransformInverseTransposed);
			}
			// Early out as we are done
			return;
		}
	}

	// Fall through that handles missing or invalid data
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutTangent.SetAndAdvance(FVector::ForwardVector);
		OutBinorm.SetAndAdvance(FVector::RightVector);
		OutNorm.SetAndAdvance(FVector::UpVector);
	}
}

void UNiagaraDataInterfaceStaticMesh::GetTriCoordColor(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);

	FNDIOutputParam<FLinearColor> OutColor(Context);

	if (InstData->bMeshValid)
	{
		TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
		const FIndexArrayView& Indices = Res->IndexBuffer.GetArrayView();
		const FColorVertexBuffer& Colors = Res->VertexBuffers.ColorVertexBuffer;
		if ((Indices.Num() > 0) && (Colors.GetNumVertices() > 0) && Colors.GetVertexData())
		{
			const int32 NumTriangles = Indices.Num() / 3;
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 Tri = (TriParam.GetAndAdvance() % NumTriangles) * 3;
				const int32 Idx0 = Indices[Tri];
				const int32 Idx1 = Indices[Tri + 1];
				const int32 Idx2 = Indices[Tri + 2];
				const FVector BaryCoord(BaryParam.GetAndAdvance());

				FLinearColor Color = BarycentricInterpolate(BaryCoord, Colors.VertexColor(Idx0).ReinterpretAsLinear(), Colors.VertexColor(Idx1).ReinterpretAsLinear(), Colors.VertexColor(Idx2).ReinterpretAsLinear());

				OutColor.SetAndAdvance(Color);
			}
			// Early out as we are done
			return;
		}
	}

	// Fall through that handles missing or invalid data
	// This mesh is invalid or doesn't have color information so set the color to white.
	FLinearColor Color = FLinearColor::White;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutColor.SetAndAdvance(Color);
	}
}

template<typename VertexAccessorType>
void UNiagaraDataInterfaceStaticMesh::GetTriCoordUV(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);
	FNDIInputParam<int32> UVSetParam(Context);

	FNDIOutputParam<FVector2D> OutUV(Context);

	if (InstData->bMeshValid)
	{
		TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
		const FIndexArrayView& Indices = Res->IndexBuffer.GetArrayView();
		const VertexAccessorType Verts(Res->VertexBuffers.StaticMeshVertexBuffer);
		if ( (Indices.Num() > 0) && (Res->VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() > 0) && Res->VertexBuffers.StaticMeshVertexBuffer.GetTexCoordData() )
		{
			const int32 NumTriangles = Indices.Num() / 3;
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 Tri = (TriParam.GetAndAdvance() % NumTriangles) * 3;
				const int32 Idx0 = Indices[Tri];
				const int32 Idx1 = Indices[Tri + 1];
				const int32 Idx2 = Indices[Tri + 2];
				const FVector BaryCoord(BaryParam.GetAndAdvance());
				const int32 UVSet = UVSetParam.GetAndAdvance();
				FVector2D UV = BarycentricInterpolate(BaryCoord, Verts.GetUV(Idx0, UVSet), Verts.GetUV(Idx1, UVSet), Verts.GetUV(Idx2, UVSet));

				OutUV.SetAndAdvance(UV);
			}
			// Early out as we are done
			return;
		}
	}

	// Fall through that handles missing or invalid data
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutUV.SetAndAdvance(FVector2D::ZeroVector);
	}
}

void UNiagaraDataInterfaceStaticMesh::GetTriCoordPositionAndVelocity(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	FNDIInputParam<int32> TriParam(Context);
	FNDIInputParam<FVector> BaryParam(Context);

	FNDIOutputParam<FVector> OutPos(Context);
	FNDIOutputParam<FVector> OutVel(Context);

	if (InstData->bMeshValid)
	{
		TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
		const FIndexArrayView& Indices = Res->IndexBuffer.GetArrayView();
		const FPositionVertexBuffer& Positions = Res->VertexBuffers.PositionVertexBuffer;
		if ( (Indices.Num() > 0) && (Positions.GetNumVertices() > 0) && Positions.GetVertexData() )
		{
			const int32 NumTriangles = Indices.Num() / 3;
			float InvDt = InstData->DeltaSeconds > 0.0f ? (1.0f / InstData->DeltaSeconds) : 0.0f;
			for (int32 i = 0; i < Context.NumInstances; ++i)
			{
				const int32 Tri = (TriParam.GetAndAdvance() % NumTriangles) * 3;
				const int32 Idx0 = Indices[Tri];
				const int32 Idx1 = Indices[Tri + 1];
				const int32 Idx2 = Indices[Tri + 2];
				const FVector BaryCoord(BaryParam.GetAndAdvance());
				FVector Pos = BarycentricInterpolate(BaryCoord, Positions.VertexPosition(Idx0), Positions.VertexPosition(Idx1), Positions.VertexPosition(Idx2));
				FVector WSPos = InstData->Transform.TransformPosition(Pos);

				FVector Vel;
				if (InstData->bUsePhysicsVelocity)
				{
					Vel = InstData->PhysicsVelocity;
				}
				else
				{
					FVector PrevWSPos = InstData->PrevTransform.TransformPosition(Pos);
					Vel = (WSPos - PrevWSPos) * InvDt;
				}

				OutPos.SetAndAdvance(WSPos);
				OutVel.SetAndAdvance(Vel);
			}
			// Early out as we are done
			return;
		}
	}

	// Fall through that handles missing or invalid data
	FVector WSPos = InstData->Transform.TransformPosition(FVector(0.0f));
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutPos.SetAndAdvance(WSPos);
		OutVel.SetAndAdvance(FVector::ZeroVector);
	}
}

void UNiagaraDataInterfaceStaticMesh::WriteTransform(const FMatrix& ToWrite, FVectorVMContext& Context)
{
	VectorVM::FExternalFuncRegisterHandler<float> Out00(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out01(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out02(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out03(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out04(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out05(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out06(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out07(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out08(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out09(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out10(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out11(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out12(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out13(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out14(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out15(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*Out00.GetDest() = ToWrite.M[0][0]; Out00.Advance();
		*Out01.GetDest() = ToWrite.M[0][1]; Out01.Advance();
		*Out02.GetDest() = ToWrite.M[0][2]; Out02.Advance();
		*Out03.GetDest() = ToWrite.M[0][3]; Out03.Advance();
		*Out04.GetDest() = ToWrite.M[1][0]; Out04.Advance();
		*Out05.GetDest() = ToWrite.M[1][1]; Out05.Advance();
		*Out06.GetDest() = ToWrite.M[1][2]; Out06.Advance();
		*Out07.GetDest() = ToWrite.M[1][3]; Out07.Advance();
		*Out08.GetDest() = ToWrite.M[2][0]; Out08.Advance();
		*Out09.GetDest() = ToWrite.M[2][1]; Out09.Advance();
		*Out10.GetDest() = ToWrite.M[2][2]; Out10.Advance();
		*Out11.GetDest() = ToWrite.M[2][3]; Out11.Advance();
		*Out12.GetDest() = ToWrite.M[3][0]; Out12.Advance();
		*Out13.GetDest() = ToWrite.M[3][1]; Out13.Advance();
		*Out14.GetDest() = ToWrite.M[3][2]; Out14.Advance();
		*Out15.GetDest() = ToWrite.M[3][3]; Out15.Advance();
	}
}

void UNiagaraDataInterfaceStaticMesh::GetLocalToWorld(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	WriteTransform(InstData->Transform, Context);
}

void UNiagaraDataInterfaceStaticMesh::GetLocalToWorldInverseTransposed(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	WriteTransform(InstData->TransformInverseTransposed, Context);
}

void UNiagaraDataInterfaceStaticMesh::GetWorldVelocity(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	FNDIOutputParam<FVector> OutVel(Context);

	FVector Velocity(0.0f, 0.0f, 0.0f);
	if (InstData->bUsePhysicsVelocity)
	{
		Velocity = InstData->PhysicsVelocity;
	}
	else
	{
		float InvDeltaTime = 1.0f / InstData->DeltaSeconds;
		if (InstData->DeltaSeconds > 0.0f)
		{
			Velocity = (FVector(InstData->Transform.M[3][0], InstData->Transform.M[3][1], InstData->Transform.M[3][2]) - 
                FVector(InstData->PrevTransform.M[3][0], InstData->PrevTransform.M[3][1], InstData->PrevTransform.M[3][2])) * InvDeltaTime;
		}
	}

	for (int32 i = 0; i < Context.NumInstances; i++)
	{
		OutVel.SetAndAdvance(Velocity);
	}
}

template<typename TransformHandlerType>
void UNiagaraDataInterfaceStaticMesh::GetVertexPosition(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	TransformHandlerType TransformHandler;
	FNDIInputParam<int32> VertexIndexParam(Context);

	FNDIOutputParam<FVector> OutPos(Context);

	if (InstData->bMeshValid)
	{
		TRefCountPtr<const FStaticMeshLODResources> Res = InstData->GetCurrentFirstLOD();
		const FPositionVertexBuffer& Positions = Res->VertexBuffers.PositionVertexBuffer;
		const int32 NumVerts = Positions.GetNumVertices();
		if ((NumVerts > 0) && Positions.GetVertexData())
		{
			FVector Pos;
			for (int32 i = 0; i < Context.NumInstances; i++)
			{
				int32 VertexIndex = VertexIndexParam.GetAndAdvance() % NumVerts;
				Pos = Positions.VertexPosition(VertexIndex);		
				TransformHandler.TransformPosition(Pos, InstData->Transform);
				OutPos.SetAndAdvance(Pos);
			}

			// Early out as we are done
			return;
		}
	}

	// Fall through that handles missing or invalid data
	FVector WSPos = InstData->Transform.TransformPosition(FVector(0.0f));
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutPos.SetAndAdvance(WSPos);
	}
}

void UNiagaraDataInterfaceStaticMesh::GetSocketCount(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	FNDIOutputParam<int32> OutNum(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutNum.SetAndAdvance(InstData->CachedSockets.Num());
	}
}

void UNiagaraDataInterfaceStaticMesh::GetFilteredSocketCount(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	FNDIOutputParam<int32> OutNum(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutNum.SetAndAdvance(InstData->NumFilteredSockets);
	}
}

void UNiagaraDataInterfaceStaticMesh::GetUnfilteredSocketCount(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	FNDIOutputParam<int32> OutNum(Context);

	const int32 NumUnfilteredSockets = InstData->CachedSockets.Num() - InstData->NumFilteredSockets;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		OutNum.SetAndAdvance(NumUnfilteredSockets);
	}
}

template<bool bWorldSpace>
void UNiagaraDataInterfaceStaticMesh::GetSocketTransform(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> InSocketIndex(Context);

	FNDIOutputParam<FVector> OutTranslate(Context);
	FNDIOutputParam<FQuat> OutRotate(Context);
	FNDIOutputParam<FVector> OutScale(Context);

	const FQuat InstRotation = bWorldSpace ? InstData->Rotation : FQuat::Identity;

	const int32 SocketMax = InstData->CachedSockets.Num() - 1;
	if (SocketMax >= 0)
	{
		for (int32 i=0; i < Context.NumInstances; ++i)
		{
			const int32 SocketIndex = FMath::Clamp(InSocketIndex.GetAndAdvance(), 0, SocketMax);

			FTransform SocketTransform = InstData->CachedSockets[SocketIndex];
			OutTranslate.SetAndAdvance(bWorldSpace ? FVector(InstData->Transform.TransformPosition(SocketTransform.GetTranslation())) : SocketTransform.GetTranslation());
			OutRotate.SetAndAdvance(bWorldSpace ? InstRotation * SocketTransform.GetRotation() : SocketTransform.GetRotation());
			OutScale.SetAndAdvance(bWorldSpace ? FVector(InstData->Transform.TransformVector(SocketTransform.GetScale3D())) : SocketTransform.GetScale3D());
		}
	}
	else
	{
		const FVector DefaultTranslate = bWorldSpace ? InstData->Transform.GetOrigin() : FVector::ZeroVector;
		const FVector DefaultScale = bWorldSpace ? InstData->Transform.ExtractScaling() : FVector::OneVector;
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutTranslate.SetAndAdvance(DefaultTranslate);
			OutRotate.SetAndAdvance(InstRotation);
			OutScale.SetAndAdvance(DefaultScale);
		}
	}
}

template<bool bWorldSpace>
void UNiagaraDataInterfaceStaticMesh::GetFilteredSocketTransform(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> InSocketIndex(Context);

	FNDIOutputParam<FVector> OutTranslate(Context);
	FNDIOutputParam<FQuat> OutRotate(Context);
	FNDIOutputParam<FVector> OutScale(Context);

	const FQuat InstRotation = bWorldSpace ? InstData->Rotation : FQuat::Identity;

	const int32 SocketMax = InstData->NumFilteredSockets - 1;
	if (SocketMax >= 0)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 SocketIndex = InstData->FilteredAndUnfilteredSockets[FMath::Clamp(InSocketIndex.GetAndAdvance(), 0, SocketMax)];

			FTransform SocketTransform = InstData->CachedSockets[SocketIndex];
			OutTranslate.SetAndAdvance(bWorldSpace ? FVector(InstData->Transform.TransformPosition(SocketTransform.GetTranslation())) : SocketTransform.GetTranslation());
			OutRotate.SetAndAdvance(bWorldSpace ? InstRotation * SocketTransform.GetRotation() : SocketTransform.GetRotation());
			OutScale.SetAndAdvance(bWorldSpace ? FVector(InstData->Transform.TransformVector(SocketTransform.GetScale3D())) : SocketTransform.GetScale3D());
		}
	}
	else
	{
		const FVector DefaultTranslate = bWorldSpace ? InstData->Transform.GetOrigin() : FVector::ZeroVector;
		const FVector DefaultScale = bWorldSpace ? InstData->Transform.ExtractScaling() : FVector::OneVector;
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutTranslate.SetAndAdvance(DefaultTranslate);
			OutRotate.SetAndAdvance(InstRotation);
			OutScale.SetAndAdvance(DefaultScale);
		}
	}
}

template<bool bWorldSpace>
void UNiagaraDataInterfaceStaticMesh::GetUnfilteredSocketTransform(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	FNDIInputParam<int32> InSocketIndex(Context);

	FNDIOutputParam<FVector> OutTranslate(Context);
	FNDIOutputParam<FQuat> OutRotate(Context);
	FNDIOutputParam<FVector> OutScale(Context);

	const FQuat InstRotation = bWorldSpace ? InstData->Rotation : FQuat::Identity;

	if (InstData->NumFilteredSockets > 0)
	{
		const int32 SocketMax = InstData->CachedSockets.Num() - InstData->NumFilteredSockets - 1;
		check(SocketMax >= 0);

		const int32 SocketOffset = InstData->NumFilteredSockets;
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 SocketIndex = InstData->FilteredAndUnfilteredSockets[FMath::Clamp(InSocketIndex.GetAndAdvance(), 0, SocketMax) + SocketOffset];

			FTransform SocketTransform = InstData->CachedSockets[SocketIndex];
			OutTranslate.SetAndAdvance(bWorldSpace ? FVector(InstData->Transform.TransformPosition(SocketTransform.GetTranslation())) : SocketTransform.GetTranslation());
			OutRotate.SetAndAdvance(bWorldSpace ? InstRotation * SocketTransform.GetRotation() : SocketTransform.GetRotation());
			OutScale.SetAndAdvance(bWorldSpace ? FVector(InstData->Transform.TransformVector(SocketTransform.GetScale3D())) : SocketTransform.GetScale3D());
		}
	}
	else if (InstData->CachedSockets.Num() > 0)
	{
		const int32 SocketMax = InstData->CachedSockets.Num() - 1;
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 SocketIndex = FMath::Clamp(InSocketIndex.GetAndAdvance(), 0, SocketMax);

			FTransform SocketTransform = InstData->CachedSockets[SocketIndex];
			OutTranslate.SetAndAdvance(bWorldSpace ? FVector(InstData->Transform.TransformPosition(SocketTransform.GetTranslation())) : SocketTransform.GetTranslation());
			OutRotate.SetAndAdvance(bWorldSpace ? InstRotation * SocketTransform.GetRotation() : SocketTransform.GetRotation());
			OutScale.SetAndAdvance(bWorldSpace ? FVector(InstData->Transform.TransformVector(SocketTransform.GetScale3D())) : SocketTransform.GetScale3D());
		}
	}
	else
	{
		const FVector DefaultTranslate = bWorldSpace ? InstData->Transform.GetOrigin() : FVector::ZeroVector;
		const FVector DefaultScale = bWorldSpace ? InstData->Transform.ExtractScaling() : FVector::OneVector;
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			OutTranslate.SetAndAdvance(DefaultTranslate);
			OutRotate.SetAndAdvance(InstRotation);
			OutScale.SetAndAdvance(DefaultScale);
		}
	}
}

void UNiagaraDataInterfaceStaticMesh::SetSourceComponentFromBlueprints(UStaticMeshComponent* ComponentToUse)
{
	// NOTE: When ChangeId changes the next tick will be skipped and a reset of the per-instance data will be initiated. 
	ChangeId++;
	SourceComponent = ComponentToUse;
	Source = ComponentToUse->GetOwner();
}

void UNiagaraDataInterfaceStaticMesh::SetDefaultMeshFromBlueprints(UStaticMesh* MeshToUse)
{
	// NOTE: When ChangeId changes the next tick will be skipped and a reset of the per-instance data will be initiated. 
	ChangeId++;
	SourceComponent = nullptr;
	Source = nullptr;
	DefaultMesh = MeshToUse;
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceStaticMesh::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FNDIStaticMeshParametersName ParamNames;
	GetNiagaraDataInterfaceParametersName(ParamNames, ParamInfo.DataInterfaceHLSLSymbol);

	FString MeshTriCoordinateStructName = "MeshTriCoordinate";

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
		{TEXT("MeshTriCoordinateStructName"), MeshTriCoordinateStructName},
		{TEXT("SectionCountName"), ParamNames.SectionCountName},
		{TEXT("MeshSectionBufferName"), ParamNames.MeshSectionBufferName},
		{TEXT("MeshIndexBufferName"), ParamNames.MeshIndexBufferName},
		{TEXT("MeshTriangleBufferName"), ParamNames.MeshTriangleBufferName},
		{TEXT("MeshVertexBufferName"), ParamNames.MeshVertexBufferName},
		{TEXT("MeshTangentBufferName"), ParamNames.MeshTangentBufferName},
		{TEXT("MeshTexCoordBufferName"), ParamNames.MeshTexCoordBufferName},
		{TEXT("MeshColorBufferName"), ParamNames.MeshColorBufferName},
		{TEXT("InstanceTransformName"), ParamNames.InstanceTransformName},
		{TEXT("InstanceTransformInverseTransposed"), ParamNames.InstanceTransformInverseTransposedName},
		{TEXT("InstancePrevTransformName"), ParamNames.InstancePrevTransformName},
		{TEXT("InstanceRotation"), ParamNames.InstanceRotationName},
		{TEXT("InstancePrevRotation"), ParamNames.InstancePrevRotationName},
		{TEXT("InstanceInvDeltaTimeName"), ParamNames.InstanceInvDeltaTimeName},
		{TEXT("InstanceWorldVelocity"), ParamNames.InstanceWorldVelocityName},
		{TEXT("AreaWeightedSamplingName"), ParamNames.AreaWeightedSamplingName},
		{TEXT("NumTexCoordName"), ParamNames.NumTexCoordName},
		{TEXT("UseColorBufferName"), ParamNames.UseColorBufferName},
		{TEXT("SocketTransforms"), ParamNames.SocketTransformsName},
		{TEXT("FilteredAndUnfilteredSockets"), ParamNames.FilteredAndUnfilteredSocketsName },
		{TEXT("NumSocketsAndFiltered"), ParamNames.NumSocketsAndFilteredName},
	};

	if (FunctionInfo.DefinitionName == StaticMeshHelpers::IsValidName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
			void {InstanceFunctionName} (out bool Out_Valid)
			{
				Out_Valid = {SectionCountName} > 0;				
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::RandomSectionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (out int Out_Section)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Section = -1;
					return;
				}

				float RandS0 = NiagaraInternalNoise(1, 2, 3);
				// Uniform sampling on mesh surface  (using alias method from Alias method from FWeightedRandomSampler)
				uint SectionIndex = min(uint(RandS0 * float({SectionCountName})), {SectionCountName}-1);
				uint4 SectionData = {MeshSectionBufferName}[SectionIndex];

				// Alias check
				float RandS1 = NiagaraInternalNoise(1, 2, 3);
				if( RandS1 > asfloat(SectionData.z) )
				{
					SectionIndex = SectionData.w;
				}
				Out_Section = SectionIndex;
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::RandomTriCoordName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (out {MeshTriCoordinateStructName} Out_Coord)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Coord.Tri = -1;
					Out_Coord.BaryCoord = (float3)0.0f;
					return;
				}

				float RandS0 = NiagaraInternalNoise(1, 2, 3);

				// Uniform sampling on mesh surface  (using alias method from Alias method from FWeightedRandomSampler)
				uint SectionIndex = min(uint(RandS0 * float({SectionCountName})), {SectionCountName}-1);
				uint4 SectionData = {MeshSectionBufferName}[SectionIndex];

				// Alias check
				float RandS1 = NiagaraInternalNoise(1, 2, 3);
				if( RandS1 > asfloat(SectionData.z) )
				{
					SectionData = {MeshSectionBufferName}[SectionData.w];
				}

				uint SectionFirstTriangle  = SectionData.x;
				uint SectionTriangleCount = SectionData.y;

				float RandT0 = NiagaraInternalNoise(1, 2, 3);
				[branch]
				if({AreaWeightedSamplingName}==0)
				{
					// Uniform triangle id selection
					Out_Coord.Tri = SectionFirstTriangle + min(uint(RandT0*float(SectionTriangleCount)), SectionTriangleCount-1); // avoid % by using mul/min to Tri = SectionTriangleCount
				}
				else
				{
					// Uniform area weighted position selection (using alias method from Alias method from FWeightedRandomSampler)
					uint TriangleIndex = min(uint(RandT0*float(SectionTriangleCount)), SectionTriangleCount-1);
					uint4 TriangleData = {MeshTriangleBufferName}[SectionFirstTriangle + TriangleIndex];

					// Alias check
					float RandT1 = NiagaraInternalNoise(1, 2, 3);
					if( RandT1 > asfloat(TriangleData.x) )
					{
						TriangleIndex = TriangleData.y;
					}
					Out_Coord.Tri = SectionFirstTriangle + TriangleIndex;
				}

				float r0 = NiagaraInternalNoise(1, 2, 3);
				float r1 = NiagaraInternalNoise(1, 2, 3);
				float sqrt0 = sqrt(r0);
				float sqrt1 = sqrt(r1);
				Out_Coord.BaryCoord = float3(1.0f - sqrt0, sqrt0 * (1.0 - r1), r1 * sqrt0);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	//else if (FunctionInfo.DefinitionName == StaticMeshHelpers::RandomTriCoordVCFilteredName)
	//{
	//}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::RandomTriCoordOnSectionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int In_Section, out {MeshTriCoordinateStructName} Out_Coord)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Coord.Tri = -1;
					Out_Coord.BaryCoord = (float3)0.0f;
					return;
				}

				int Section = clamp(In_Section, 0, (int)({SectionCountName} - 1));

				uint4 SectionData = {MeshSectionBufferName}[Section];
				uint SectionFirstTriangle = SectionData.x;
				uint SectionTriangleCount = SectionData.y;

				float RandT0 = NiagaraInternalNoise(1, 2, 3);
				[branch]
				if({AreaWeightedSamplingName}==0)
				{
					// Uniform triangle id selection
					Out_Coord.Tri = SectionFirstTriangle + min(uint(RandT0*float(SectionTriangleCount)), SectionTriangleCount-1); // avoid % by using mul/min to Tri = SectionTriangleCount
				}
				else
				{
					// Uniform area weighted position selection (using alias method from Alias method from FWeightedRandomSampler)
					uint TriangleIndex = min(uint(RandT0*float(SectionTriangleCount)), SectionTriangleCount-1);
					uint4 TriangleData = {MeshTriangleBufferName}[SectionFirstTriangle + TriangleIndex];

					// Alias check
					float RandT1 = NiagaraInternalNoise(1, 2, 3);
					if( RandT1 > asfloat(TriangleData.x) )
					{
						TriangleIndex = TriangleData.y;
					}
					Out_Coord.Tri = SectionFirstTriangle + TriangleIndex;
				}

				float r0 = NiagaraInternalNoise(1, 2, 3);
				float r1 = NiagaraInternalNoise(1, 2, 3);
				float sqrt0 = sqrt(r0);
				float sqrt1 = sqrt(r1);
				Out_Coord.BaryCoord = float3(1.0f - sqrt0, sqrt0 * (1.0 - r1), r1 * sqrt0);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriPositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float3 Out_Position)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Position = (float3)0.0f;
					return;
				}

				uint TriangleIndex = In_Coord.Tri * 3;
				uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ] * 3;
				uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1] * 3;
				uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2] * 3;

				// I could not find a R32G32B32f format to create an SRV on that buffer. So float load it is for now...
				float3 vertex0 = float3({MeshVertexBufferName}[VertexIndex0], {MeshVertexBufferName}[VertexIndex0+1], {MeshVertexBufferName}[VertexIndex0+2]);
				float3 vertex1 = float3({MeshVertexBufferName}[VertexIndex1], {MeshVertexBufferName}[VertexIndex1+1], {MeshVertexBufferName}[VertexIndex1+2]);
				float3 vertex2 = float3({MeshVertexBufferName}[VertexIndex2], {MeshVertexBufferName}[VertexIndex2+1], {MeshVertexBufferName}[VertexIndex2+2]);
				Out_Position = vertex0 * In_Coord.BaryCoord.x + vertex1 * In_Coord.BaryCoord.y + vertex2 * In_Coord.BaryCoord.z;
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriPositionWSName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float3 Out_Position)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Position = {InstanceTransformName}[3].xyz;
					return;
				}

				uint TriangleIndex = In_Coord.Tri * 3;
				uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ] * 3;
				uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1] * 3;
				uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2] * 3;

				// I could not find a R32G32B32f format to create an SRV on that buffer. So float load it is for now...
				float3 vertex0 = float3({MeshVertexBufferName}[VertexIndex0], {MeshVertexBufferName}[VertexIndex0+1], {MeshVertexBufferName}[VertexIndex0+2]);
				float3 vertex1 = float3({MeshVertexBufferName}[VertexIndex1], {MeshVertexBufferName}[VertexIndex1+1], {MeshVertexBufferName}[VertexIndex1+2]);
				float3 vertex2 = float3({MeshVertexBufferName}[VertexIndex2], {MeshVertexBufferName}[VertexIndex2+1], {MeshVertexBufferName}[VertexIndex2+2]);
				float3 Position = vertex0 * In_Coord.BaryCoord.x + vertex1 * In_Coord.BaryCoord.y + vertex2 * In_Coord.BaryCoord.z;

				Out_Position = mul(float4(Position, 1.0), {InstanceTransformName}).xyz;
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriNormalName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float3 Out_Normal)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Normal = float3(0, 0, 1);
					return;
				}

				uint TriangleIndex = In_Coord.Tri * 3;
				uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ] * 2;
				uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1] * 2;
				uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2] * 2;

				float3 Normal0 = TangentBias({MeshTangentBufferName}[VertexIndex0+1].xyz);
				float3 Normal1 = TangentBias({MeshTangentBufferName}[VertexIndex1+1].xyz);
				float3 Normal2 = TangentBias({MeshTangentBufferName}[VertexIndex2+1].xyz);

				float3 Normal   = Normal0 * In_Coord.BaryCoord.x + Normal1 * In_Coord.BaryCoord.y + Normal2 * In_Coord.BaryCoord.z;

				Out_Normal = normalize(Normal);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriNormalWSName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float3 Out_Normal)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Normal = float3(0, 0, 1);
					return;
				}

				uint TriangleIndex = In_Coord.Tri * 3;
				uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ] * 2;
				uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1] * 2;
				uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2] * 2;

				float3 Normal0 = TangentBias({MeshTangentBufferName}[VertexIndex0+1].xyz);
				float3 Normal1 = TangentBias({MeshTangentBufferName}[VertexIndex1+1].xyz);
				float3 Normal2 = TangentBias({MeshTangentBufferName}[VertexIndex2+1].xyz);

				float3 Normal   = Normal0 * In_Coord.BaryCoord.x + Normal1 * In_Coord.BaryCoord.y + Normal2 * In_Coord.BaryCoord.z;

				Out_Normal = normalize(mul(float4(Normal, 0.0), {InstanceTransformInverseTransposed}).xyz);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriTangentsName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float3 Out_Tangent, out float3 Out_Binormal, out float3 Out_Normal)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Tangent = float3(1, 0, 0);
					Out_Binormal = float3(0, 1, 0);
					Out_Normal = float3(0, 0, 1);
					return;
				}

				uint TriangleIndex = In_Coord.Tri * 3;
				uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ] * 2;
				uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1] * 2;
				uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2] * 2;

				float3 TangentX0 = TangentBias({MeshTangentBufferName}[VertexIndex0  ].xyz);
				float4 TangentZ0 = TangentBias({MeshTangentBufferName}[VertexIndex0+1].xyzw);
				float3 TangentX1 = TangentBias({MeshTangentBufferName}[VertexIndex1  ].xyz);
				float4 TangentZ1 = TangentBias({MeshTangentBufferName}[VertexIndex1+1].xyzw);
				float3 TangentX2 = TangentBias({MeshTangentBufferName}[VertexIndex2  ].xyz);
				float4 TangentZ2 = TangentBias({MeshTangentBufferName}[VertexIndex2+1].xyzw);

				float3 Binormal0   = cross(TangentZ0.xyz, TangentX0.xyz) * TangentZ0.w;
				float3 Binormal1   = cross(TangentZ1.xyz, TangentX1.xyz) * TangentZ1.w;
				float3 Binormal2   = cross(TangentZ2.xyz, TangentX2.xyz) * TangentZ2.w;

				Out_Normal   = normalize(TangentZ0.xyz * In_Coord.BaryCoord.x + TangentZ1.xyz * In_Coord.BaryCoord.y + TangentZ2.xyz * In_Coord.BaryCoord.z);  // Normal is TangentZ
				Out_Tangent  = normalize(TangentX0.xyz * In_Coord.BaryCoord.x + TangentX1.xyz * In_Coord.BaryCoord.y + TangentX2.xyz * In_Coord.BaryCoord.z);
				Out_Binormal = normalize(Binormal0.xyz * In_Coord.BaryCoord.x + Binormal1.xyz * In_Coord.BaryCoord.y + Binormal2.xyz * In_Coord.BaryCoord.z);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriTangentsWSName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float3 Out_Tangent, out float3 Out_Binormal, out float3 Out_Normal)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Tangent = float3(1, 0, 0);
					Out_Binormal = float3(0, 1, 0);
					Out_Normal = float3(0, 0, 1);
					return;
				}

				uint TriangleIndex = In_Coord.Tri * 3;
				uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ] * 2;
				uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1] * 2;
				uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2] * 2;

				float3 TangentX0 = TangentBias({MeshTangentBufferName}[VertexIndex0  ].xyz);
				float4 TangentZ0 = TangentBias({MeshTangentBufferName}[VertexIndex0+1].xyzw);
				float3 TangentX1 = TangentBias({MeshTangentBufferName}[VertexIndex1  ].xyz);
				float4 TangentZ1 = TangentBias({MeshTangentBufferName}[VertexIndex1+1].xyzw);
				float3 TangentX2 = TangentBias({MeshTangentBufferName}[VertexIndex2  ].xyz);
				float4 TangentZ2 = TangentBias({MeshTangentBufferName}[VertexIndex2+1].xyzw);

				float3 Binormal0   = cross(TangentZ0.xyz, TangentX0.xyz) * TangentZ0.w;
				float3 Binormal1   = cross(TangentZ1.xyz, TangentX1.xyz) * TangentZ1.w;
				float3 Binormal2   = cross(TangentZ2.xyz, TangentX2.xyz) * TangentZ2.w;

				float3 Normal   = TangentZ0.xyz * In_Coord.BaryCoord.x + TangentZ1.xyz * In_Coord.BaryCoord.y + TangentZ2.xyz * In_Coord.BaryCoord.z;  // Normal is TangentZ
				float3 Tangent  = TangentX0.xyz * In_Coord.BaryCoord.x + TangentX1.xyz * In_Coord.BaryCoord.y + TangentX2.xyz * In_Coord.BaryCoord.z;
				float3 Binormal = Binormal0.xyz * In_Coord.BaryCoord.x + Binormal1.xyz * In_Coord.BaryCoord.y + Binormal2.xyz * In_Coord.BaryCoord.z;

				float3 NormalWorld  = normalize(mul(float4(Normal  , 0.0), {InstanceTransformInverseTransposed}).xyz);
				float3 TangentWorld = normalize(mul(float4(Tangent , 0.0), {InstanceTransformInverseTransposed}).xyz);
				float3 BinormalWorld= normalize(mul(float4(Binormal, 0.0), {InstanceTransformInverseTransposed}).xyz);

				Out_Normal = NormalWorld;
				Out_Tangent = TangentWorld;
				Out_Binormal = BinormalWorld;
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriColorName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float4 Out_Color)
			{
				Out_Color = float4(1, 1, 1, 1);
				[branch]
				if ({UseColorBufferName})
				{
					uint TriangleIndex = In_Coord.Tri * 3;
					uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ];
					uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1];
					uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2];

					float4 Color0 = {MeshColorBufferName}[VertexIndex0] FMANUALFETCH_COLOR_COMPONENT_SWIZZLE;
					float4 Color1 = {MeshColorBufferName}[VertexIndex1] FMANUALFETCH_COLOR_COMPONENT_SWIZZLE;
					float4 Color2 = {MeshColorBufferName}[VertexIndex2] FMANUALFETCH_COLOR_COMPONENT_SWIZZLE;

					Out_Color = Color0 * In_Coord.BaryCoord.x + Color1 * In_Coord.BaryCoord.y + Color2 * In_Coord.BaryCoord.z;
				}
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriUVName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, in int In_UVSet, out float2 Out_UV)
			{
				[branch]
				if({NumTexCoordName}>0)
				{
					uint TriangleIndex = In_Coord.Tri * 3;
					uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ];
					uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1];
					uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2];

					uint stride = {NumTexCoordName};
					uint SelectedUVSet = clamp((uint)In_UVSet, 0, {NumTexCoordName}-1);
					float2 UV0 = {MeshTexCoordBufferName}[VertexIndex0 * stride + SelectedUVSet];
					float2 UV1 = {MeshTexCoordBufferName}[VertexIndex1 * stride + SelectedUVSet];
					float2 UV2 = {MeshTexCoordBufferName}[VertexIndex2 * stride + SelectedUVSet];

					Out_UV = UV0 * In_Coord.BaryCoord.x + UV1 * In_Coord.BaryCoord.y + UV2 * In_Coord.BaryCoord.z;
				}
				else	
				{
					Out_UV = 0.0f;
				}
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriPositionAndVelocityName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float3 Out_Position, out float3 Out_Velocity)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Position = {InstanceTransformName}[3].xyz;
					Out_Velocity = (float3)0.0f;
					return;
				}

				uint TriangleIndex = In_Coord.Tri * 3;
				uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ] * 3;
				uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1] * 3;
				uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2] * 3;

				// I could not find a R32G32B32f format to create an SRV on that buffer. So float load it is for now...
				float3 vertex0 = float3({MeshVertexBufferName}[VertexIndex0], {MeshVertexBufferName}[VertexIndex0+1], {MeshVertexBufferName}[VertexIndex0+2]);
				float3 vertex1 = float3({MeshVertexBufferName}[VertexIndex1], {MeshVertexBufferName}[VertexIndex1+1], {MeshVertexBufferName}[VertexIndex1+2]);
				float3 vertex2 = float3({MeshVertexBufferName}[VertexIndex2], {MeshVertexBufferName}[VertexIndex2+1], {MeshVertexBufferName}[VertexIndex2+2]);
				float3 WSPos = vertex0 * In_Coord.BaryCoord.x + vertex1 * In_Coord.BaryCoord.y + vertex2 * In_Coord.BaryCoord.z;
				float3 PrevWSPos = WSPos;

				WSPos = mul(float4(WSPos,1.0), {InstanceTransformName}).xyz;
				PrevWSPos = mul(float4(PrevWSPos,1.0), {InstancePrevTransformName}).xyz;

				Out_Position = WSPos;
				Out_Velocity = (WSPos - PrevWSPos) * {InstanceInvDeltaTimeName};
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetMeshLocalToWorldName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (out float4x4 Out_Transform)
			{
				Out_Transform = {InstanceTransformName};
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetMeshLocalToWorldInverseTransposedName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (out float4x4 Out_Transform)
			{
				Out_Transform = {InstanceTransformInverseTransposed};
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetMeshWorldVelocityName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (out float3 Out_Velocity)
			{
				Out_Velocity = {InstanceWorldVelocity};
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetVertexPositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int VertexIndex, out float3 Out_Position)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Position = (float3)0.0f;
					return;
				}

				VertexIndex *= 3;
				Out_Position = float3({MeshVertexBufferName}[VertexIndex], {MeshVertexBufferName}[VertexIndex+1], {MeshVertexBufferName}[VertexIndex+2]);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetVertexPositionWSName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int VertexIndex, out float3 Out_Position)
			{
				[branch]
				if ({SectionCountName} == 0)
				{
					Out_Position = {InstanceTransformName}[3].xyz;
					return;
				}

				VertexIndex *= 3;
				Out_Position = float3({MeshVertexBufferName}[VertexIndex], {MeshVertexBufferName}[VertexIndex+1], {MeshVertexBufferName}[VertexIndex+2]);
				Out_Position = mul(float4(Out_Position, 1.0), {InstanceTransformName}).xyz;
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	//////////////////////////////////////////////////////////////////////////
	// Socket Functions
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetSocketCountName)
	{
		static const TCHAR *FormatSample = TEXT(R"(void {InstanceFunctionName} (out int OutNumSockets) { OutNumSockets = {NumSocketsAndFiltered}[0]; } )");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetFilteredSocketCountName)
	{
		static const TCHAR *FormatSample = TEXT(R"(void {InstanceFunctionName} (out int OutNumSockets) { OutNumSockets = {NumSocketsAndFiltered}[1]; } )");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetUnfilteredSocketCountName)
	{
		static const TCHAR *FormatSample = TEXT(R"(void {InstanceFunctionName} (out int OutNumSockets) { OutNumSockets = {NumSocketsAndFiltered}[2]; } )");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetSocketTransformName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int SocketIndex, out float3 OutPosition, out float4 OutRotation, out float3 OutScale)
			{
				int NumSockets = {NumSocketsAndFiltered}[0];
				[branch]
				if (NumSockets == 0)
				{
					OutPosition = float3(0.0f, 0.0f, 0.0f);
					OutRotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
					OutScale = float3(1.0f, 1.0f, 1.0f);
				}
				else
				{
					int BufferOffset = clamp(SocketIndex, 0, NumSockets - 1) * 3;
					OutPosition = {SocketTransforms}[BufferOffset + 0].xyz;
					OutRotation = {SocketTransforms}[BufferOffset + 1];
					OutScale    = {SocketTransforms}[BufferOffset + 2].xyz;
				}
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetSocketTransformWSName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int SocketIndex, out float3 OutPosition, out float4 OutRotation, out float3 OutScale)
			{
				int NumSockets = {NumSocketsAndFiltered}[0];
				[branch]
				if (NumSockets == 0)
				{
					OutPosition = float3(0.0f, 0.0f, 0.0f);
					OutRotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
					OutScale = float3(1.0f, 1.0f, 1.0f);
				}
				else
				{
					int BufferOffset = clamp(SocketIndex, 0, NumSockets - 1) * 3;
					OutPosition = {SocketTransforms}[BufferOffset + 0].xyz;
					OutRotation = {SocketTransforms}[BufferOffset + 1];
					OutScale    = {SocketTransforms}[BufferOffset + 2].xyz;
				}
				OutPosition = mul(float4(OutPosition, 1.0), {InstanceTransformName}).xyz;
				OutRotation = NiagaraGPU_QuatMul({InstanceRotation}, OutRotation);
				OutScale = mul(float4(OutScale, 0.0), {InstanceTransformName}).xyz;
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetFilteredSocketTransformName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int SocketIndex, out float3 OutPosition, out float4 OutRotation, out float3 OutScale)
			{
				int NumSockets = {NumSocketsAndFiltered}[1];
				[branch]
				if (NumSockets == 0)
				{
					OutPosition = float3(0.0f, 0.0f, 0.0f);
					OutRotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
					OutScale = float3(1.0f, 1.0f, 1.0f);
				}
				else
				{
					SocketIndex = clamp(SocketIndex, 0, NumSockets - 1);
					int BufferOffset = {FilteredAndUnfilteredSockets}[SocketIndex] * 3;
					OutPosition = {SocketTransforms}[BufferOffset + 0].xyz;
					OutRotation = {SocketTransforms}[BufferOffset + 1];
					OutScale    = {SocketTransforms}[BufferOffset + 2].xyz;
				}
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetFilteredSocketTransformWSName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int SocketIndex, out float3 OutPosition, out float4 OutRotation, out float3 OutScale)
			{
				int NumSockets = {NumSocketsAndFiltered}[1];
				[branch]
				if (NumSockets == 0)
				{
					OutPosition = float3(0.0f, 0.0f, 0.0f);
					OutRotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
					OutScale = float3(1.0f, 1.0f, 1.0f);
				}
				else
				{
					SocketIndex = clamp(SocketIndex, 0, NumSockets - 1);
					int BufferOffset = {FilteredAndUnfilteredSockets}[SocketIndex] * 3;
					OutPosition = {SocketTransforms}[BufferOffset + 0].xyz;
					OutRotation = {SocketTransforms}[BufferOffset + 1];
					OutScale    = {SocketTransforms}[BufferOffset + 2].xyz;
				}
				OutPosition = mul(float4(OutPosition, 1.0), {InstanceTransformName}).xyz;
				OutRotation = NiagaraGPU_QuatMul({InstanceRotation}, OutRotation);
				OutScale = mul(float4(OutScale, 0.0), {InstanceTransformName}).xyz;
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetUnfilteredSocketTransformName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int SocketIndex, out float3 OutPosition, out float4 OutRotation, out float3 OutScale)
			{
				int NumSockets = {NumSocketsAndFiltered}[0];
				int NumFilteredSockets = {NumSocketsAndFiltered}[1];
				int NumUnfilteredSockets = {NumSocketsAndFiltered}[2];
				[branch]
				if (NumSockets == 0)
				{
					OutPosition = float3(0.0f, 0.0f, 0.0f);
					OutRotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
					OutScale = float3(1.0f, 1.0f, 1.0f);
				}
				else if ( NumFilteredSockets == 0 )
				{
					int BufferOffset = clamp(SocketIndex, 0, NumSockets - 1) * 3;
					OutPosition = {SocketTransforms}[BufferOffset + 0].xyz;
					OutRotation = {SocketTransforms}[BufferOffset + 1];
					OutScale    = {SocketTransforms}[BufferOffset + 2].xyz;
				}
				else
				{
					SocketIndex = clamp(SocketIndex, 0, NumUnfilteredSockets - 1) + NumFilteredSockets;
					int BufferOffset = {FilteredAndUnfilteredSockets}[SocketIndex] * 3;
					OutPosition = {SocketTransforms}[BufferOffset + 0].xyz;
					OutRotation = {SocketTransforms}[BufferOffset + 1];
					OutScale    = {SocketTransforms}[BufferOffset + 2].xyz;
				}
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetUnfilteredSocketTransformWSName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int SocketIndex, out float3 OutPosition, out float4 OutRotation, out float3 OutScale)
			{
				int NumSockets = {NumSocketsAndFiltered}[0];
				int NumFilteredSockets = {NumSocketsAndFiltered}[1];
				int NumUnfilteredSockets = {NumSocketsAndFiltered}[2];
				[branch]
				if (NumSockets == 0)
				{
					OutPosition = float3(0.0f, 0.0f, 0.0f);
					OutRotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
					OutScale = float3(1.0f, 1.0f, 1.0f);
				}
				else if ( NumFilteredSockets == 0 )
				{
					int BufferOffset = clamp(SocketIndex, 0, NumSockets - 1) * 3;
					OutPosition = {SocketTransforms}[BufferOffset + 0].xyz;
					OutRotation = {SocketTransforms}[BufferOffset + 1];
					OutScale    = {SocketTransforms}[BufferOffset + 2].xyz;
				}
				else
				{
					SocketIndex = clamp(SocketIndex, 0, NumUnfilteredSockets - 1) + NumFilteredSockets;
					int BufferOffset = {FilteredAndUnfilteredSockets}[SocketIndex] * 3;
					OutPosition = {SocketTransforms}[BufferOffset + 0].xyz;
					OutRotation = {SocketTransforms}[BufferOffset + 1];
					OutScale    = {SocketTransforms}[BufferOffset + 2].xyz;
				}
				OutPosition = mul(float4(OutPosition, 1.0), {InstanceTransformName}).xyz;
				OutRotation = NiagaraGPU_QuatMul({InstanceRotation}, OutRotation);
				OutScale = mul(float4(OutScale, 0.0), {InstanceTransformName}).xyz;
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else
	{
		// This function is not support
		return false;
	}

	OutHLSL += TEXT("\n");
	return true;
}

void UNiagaraDataInterfaceStaticMesh::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FNDIStaticMeshParametersName ParamNames;
	GetNiagaraDataInterfaceParametersName(ParamNames, ParamInfo.DataInterfaceHLSLSymbol);

	OutHLSL += TEXT("Buffer<uint> ") + ParamNames.MeshIndexBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float> ") + ParamNames.MeshVertexBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float4> ") + ParamNames.MeshTangentBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float2> ") + ParamNames.MeshTexCoordBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float4> ") + ParamNames.MeshColorBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<uint4> ") + ParamNames.MeshSectionBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<uint4> ") + ParamNames.MeshTriangleBufferName + TEXT(";\n");
	OutHLSL += TEXT("uint ") + ParamNames.UseColorBufferName + TEXT(";\n");
	OutHLSL += TEXT("uint ") + ParamNames.SectionCountName + TEXT(";\n");
	OutHLSL += TEXT("float4x4 ") + ParamNames.InstanceTransformName + TEXT(";\n");
	OutHLSL += TEXT("float4x4 ") + ParamNames.InstanceTransformInverseTransposedName + TEXT(";\n");
	OutHLSL += TEXT("float4x4 ") + ParamNames.InstancePrevTransformName + TEXT(";\n");
	OutHLSL += TEXT("float4 ") + ParamNames.InstanceRotationName + TEXT(";\n");
	OutHLSL += TEXT("float4 ") + ParamNames.InstancePrevRotationName + TEXT(";\n");
	OutHLSL += TEXT("float ") + ParamNames.InstanceInvDeltaTimeName + TEXT(";\n");
	OutHLSL += TEXT("float4 ") + ParamNames.InstanceWorldVelocityName + TEXT(";\n");
	OutHLSL += TEXT("uint ") + ParamNames.AreaWeightedSamplingName + TEXT(";\n");	// Could be used for other flags
	OutHLSL += TEXT("uint ") + ParamNames.NumTexCoordName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float4> ") + ParamNames.SocketTransformsName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<uint> ") + ParamNames.FilteredAndUnfilteredSocketsName + TEXT(";\n");
	OutHLSL += TEXT("int3 ") + ParamNames.NumSocketsAndFilteredName + TEXT(";\n");
}
#endif

void UNiagaraDataInterfaceStaticMesh::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(Proxy);

	FNDIStaticMesh_InstanceData* InstanceData = static_cast<FNDIStaticMesh_InstanceData*>(PerInstanceData);
	FNiagaraPassedInstanceDataForRT* DataToPass = static_cast<FNiagaraPassedInstanceDataForRT*>(DataForRenderThread);

	DataToPass->bIsGpuUniformlyDistributedSampling = InstanceData->bIsGpuUniformlyDistributedSampling;
	DataToPass->DeltaSeconds = InstanceData->DeltaSeconds;
	DataToPass->Transform = InstanceData->Transform;
	DataToPass->PrevTransform = InstanceData->PrevTransform;
	DataToPass->Rotation = InstanceData->Rotation;
	DataToPass->PrevRotation = InstanceData->PrevRotation;
}

//////////////////////////////////////////////////////////////////////////

bool FDynamicVertexColorFilterData::Init(FNDIStaticMesh_InstanceData* Owner)
{
	TrianglesSortedByVertexColor.Empty();
	VertexColorToTriangleStart.AddDefaulted(256);
	check(Owner->bMeshValid);

	TRefCountPtr<const FStaticMeshLODResources> Res = Owner->GetCurrentFirstLOD();
	if (!Res.IsValid() || Res->VertexBuffers.ColorVertexBuffer.GetNumVertices() == 0)
	{
		UE_LOG(LogNiagara, Log, TEXT("Cannot initialize vertex color filter data for a mesh with no color data - %s"), *GetFullNameSafe(Owner->StaticMesh.Get()));
		return false;
	}

	// Go over all triangles for each possible vertex color and add it to that bucket
	for (int32 i = 0; i < VertexColorToTriangleStart.Num(); i++)
	{
		uint32 MinVertexColorRed = i;
		uint32 MaxVertexColorRed = i + 1;
		VertexColorToTriangleStart[i] = TrianglesSortedByVertexColor.Num();

		FIndexArrayView IndexView = Res->IndexBuffer.GetArrayView();
		for (int32 j = 0; j < Owner->GetValidSections().Num(); j++)
		{
			int32 SectionIdx = Owner->GetValidSections()[j];
			int32 TriStartIdx = Res->Sections[SectionIdx].FirstIndex;
			for (uint32 TriIdx = 0; TriIdx < Res->Sections[SectionIdx].NumTriangles; TriIdx++)
			{
				uint32 V0Idx = IndexView[TriStartIdx + TriIdx * 3 + 0];
				uint32 V1Idx = IndexView[TriStartIdx + TriIdx * 3 + 1];
				uint32 V2Idx = IndexView[TriStartIdx + TriIdx * 3 + 2];

				uint8 MaxR = FMath::Max<uint8>(Res->VertexBuffers.ColorVertexBuffer.VertexColor(V0Idx).R,
					FMath::Max<uint8>(Res->VertexBuffers.ColorVertexBuffer.VertexColor(V1Idx).R,
						Res->VertexBuffers.ColorVertexBuffer.VertexColor(V2Idx).R));
				if (MaxR >= MinVertexColorRed && MaxR < MaxVertexColorRed)
				{
					TrianglesSortedByVertexColor.Add(TriStartIdx + TriIdx * 3);
				}
			}
		}
	}
	return true;
}

TMap<uint32, TSharedPtr<FDynamicVertexColorFilterData>> FNDI_StaticMesh_GeneratedData::DynamicVertexColorFilters;
FCriticalSection FNDI_StaticMesh_GeneratedData::CriticalSection;

TSharedPtr<FDynamicVertexColorFilterData> FNDI_StaticMesh_GeneratedData::GetDynamicColorFilterData(FNDIStaticMesh_InstanceData* Instance)
{
	FScopeLock Lock(&CriticalSection);

	check(Instance);
	check(Instance->bMeshValid);

	TSharedPtr<FDynamicVertexColorFilterData> Ret = nullptr;

	uint32 FilterDataHash = GetTypeHash(Instance->StaticMesh.Get());
	for (int32 ValidSec : Instance->GetValidSections())
	{
		FilterDataHash = HashCombine(GetTypeHash(ValidSec), FilterDataHash);
	}

	if (TSharedPtr<FDynamicVertexColorFilterData>* Existing = DynamicVertexColorFilters.Find(FilterDataHash))
	{
		check(Existing->IsValid());//We shouldn't be able to have an invalid ptr here.
		Ret = *Existing;		
	}
	else
	{
		Ret = MakeShared<FDynamicVertexColorFilterData>();
		if (Ret->Init(Instance))
		{
			DynamicVertexColorFilters.Add(FilterDataHash) = Ret;
		}
		else
		{
			Ret = nullptr;
		}
	}

	return Ret;
}

void FNDI_StaticMesh_GeneratedData::CleanupDynamicColorFilterData()
{
	TArray<uint32, TInlineAllocator<64>> ToRemove;
	for (TPair<uint32, TSharedPtr<FDynamicVertexColorFilterData>>& Pair : DynamicVertexColorFilters)
	{
		TSharedPtr<FDynamicVertexColorFilterData>& Ptr = Pair.Value;
		if (Ptr.IsUnique())
		{
			//If we're the only ref left then destroy this data
			ToRemove.Add(Pair.Key);
		}
	}

	for (uint32 Key : ToRemove)
	{
		DynamicVertexColorFilters.Remove(Key);
	}
}

#undef LOCTEXT_NAMESPACE
