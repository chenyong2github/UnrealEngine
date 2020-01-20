// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceStaticMesh.h"
#include "NiagaraEmitterInstance.h"
#include "Engine/StaticMeshActor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraRenderer.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraScript.h"
#include "ShaderParameterUtils.h"
#include "NiagaraEmitterInstanceBatcher.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceStaticMesh"

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
const FString UNiagaraDataInterfaceStaticMesh::InstanceInvDeltaTimeName(TEXT("InstanceInvDeltaTime_"));
const FString UNiagaraDataInterfaceStaticMesh::InstanceWorldVelocityName(TEXT("InstanceWorldVelocity_"));
const FString UNiagaraDataInterfaceStaticMesh::AreaWeightedSamplingName(TEXT("AreaWeightedSamplingName_"));
const FString UNiagaraDataInterfaceStaticMesh::NumTexCoordName(TEXT("NumTexCoordName_"));

FStaticMeshFilteredAreaWeightedSectionSampler::FStaticMeshFilteredAreaWeightedSectionSampler()
	: Res(nullptr)
	, Owner(nullptr)
{
}

void FStaticMeshFilteredAreaWeightedSectionSampler::Init(FStaticMeshLODResources* InRes, FNDIStaticMesh_InstanceData* InOwner)
{
	Res = InRes;
	Owner = InOwner;

	FStaticMeshAreaWeightedSectionSampler::Init(Res);
}

float FStaticMeshFilteredAreaWeightedSectionSampler::GetWeights(TArray<float>& OutWeights)
{
	check(Owner && Owner->Mesh);
	float Total = 0.0f;
	OutWeights.Empty(Owner->GetValidSections().Num());
	FStaticMeshLODResources& LODRes = Owner->Mesh->RenderData->LODResources[0];
	for (int32 i = 0; i < Owner->GetValidSections().Num(); ++i)
	{
		int32 SecIdx = Owner->GetValidSections()[i];
		float T = LODRes.AreaWeightedSectionSamplers[SecIdx].GetTotalWeight();
		OutWeights.Add(T);
		Total += T;
	}
	return Total;
}

//////////////////////////////////////////////////////////////////////////
// FStaticMeshGpuSpawnBuffer
FStaticMeshGpuSpawnBuffer::~FStaticMeshGpuSpawnBuffer()
{
	//ValidSections.Empty();
}

void FStaticMeshGpuSpawnBuffer::Initialise(const FStaticMeshLODResources& Res, const UNiagaraDataInterfaceStaticMesh& Interface, bool bIsGpuUniformlyDistributedSampling, const TArray<int32>& ValidSection, const FStaticMeshFilteredAreaWeightedSectionSampler& SectionSamplerParam)
{
	// In this function we prepare some data to be uploaded on GPU from the available mesh data. This is a thread safe place to create this data.
	// The section buffer needs to be specific to the current UI being built (section/material culling).
	SectionRenderData = &Res;

	const uint32 ValidSectionCount = ValidSection.Num();
	const TArray<float>& Prob = SectionSamplerParam.GetProb();
	const TArray<int32>& Alias = SectionSamplerParam.GetAlias();
	check(ValidSectionCount == Prob.Num());
	// Build data that will be uploaded to GPU later from the render thread.
	// The array contains data used to select regions for uniform particle spawning on them, as well as section triangle ranges.
	ValidSections.Reserve(ValidSectionCount);
	for (uint32 i = 0; i < ValidSectionCount; ++i)
	{
		uint32 ValidSectionId = ValidSection[i];
		const FStaticMeshSection& Section = Res.Sections[ValidSectionId];
		SectionInfo NewSectionInfo = { Section.FirstIndex / 3, Section.NumTriangles, Prob[i], (uint32)Alias[i] };
		ValidSections.Add(NewSectionInfo);

		check(!bIsGpuUniformlyDistributedSampling || bIsGpuUniformlyDistributedSampling && Res.AreaWeightedSectionSamplers[ValidSectionId].GetProb().Num() == Section.NumTriangles);
	}

	if (bIsGpuUniformlyDistributedSampling)
		BufferUniformTriangleSamplingSRV = Res.AreaWeightedSectionSamplersBuffer.GetBufferSRV(); // Cache that SRV for later
}

void FStaticMeshGpuSpawnBuffer::InitRHI()
{
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
	}
}

void FStaticMeshGpuSpawnBuffer::ReleaseRHI()
{
	MeshIndexBufferSrv.SafeRelease();
	BufferSectionSRV.SafeRelease();
	BufferSectionRHI.SafeRelease();

	MeshIndexBufferSrv.SafeRelease();
	MeshVertexBufferSrv.SafeRelease();
	MeshTangentBufferSrv.SafeRelease();
	MeshTexCoordBufferSrv.SafeRelease();
	MeshColorBufferSRV.SafeRelease();
	BufferSectionSRV.SafeRelease();
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
	UStaticMesh* PrevMesh = Mesh;
	Component = nullptr;
	Mesh = nullptr;
	Transform = FMatrix::Identity;
	TransformInverseTransposed = FMatrix::Identity;
	PrevTransform = FMatrix::Identity;
	PrevTransformInverseTransposed = FMatrix::Identity;
	DeltaSeconds = 0.0f;
	ChangeId = Interface->ChangeId;

	if (Interface->SourceComponent)
	{
		Component = Interface->SourceComponent;
		Mesh = Interface->SourceComponent->GetStaticMesh();
	}
	else if (Interface->Source)
	{
		AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Interface->Source);
		UStaticMeshComponent* SourceComp = nullptr;
		if (MeshActor != nullptr)
		{
			SourceComp = MeshActor->GetStaticMeshComponent();
		}
		else
		{
			SourceComp = Interface->Source->FindComponentByClass<UStaticMeshComponent>();
		}

		if (SourceComp)
		{
			Mesh = SourceComp->GetStaticMesh();
			Component = SourceComp;
		}
		else
		{
			Component = Interface->Source->GetRootComponent();
		}
	}
	else
	{
		if (UNiagaraComponent* SimComp = SystemInstance->GetComponent())
		{
			if (UStaticMeshComponent* ParentComp = Cast<UStaticMeshComponent>(SimComp->GetAttachParent()))
			{
				Component = ParentComp;
				Mesh = ParentComp->GetStaticMesh();
			}
			else if (UStaticMeshComponent* OuterComp = SimComp->GetTypedOuter<UStaticMeshComponent>())
			{
				Component = OuterComp;
				Mesh = OuterComp->GetStaticMesh();
			}
			else if (AActor* Owner = SimComp->GetAttachmentRootActor())
			{
				for (UActorComponent* ActorComp : Owner->GetComponents())
				{
					UStaticMeshComponent* SourceComp = Cast<UStaticMeshComponent>(ActorComp);
					if (SourceComp)
					{
						UStaticMesh* PossibleMesh = SourceComp->GetStaticMesh();
						if (PossibleMesh != nullptr && PossibleMesh->bAllowCPUAccess)
						{
							Mesh = PossibleMesh;
							Component = SourceComp;
							break;
						}
					}
				}
			}

			if (!Component.IsValid())
			{
				Component = SimComp;
			}
		}
	}

	check(Component.IsValid());

	if (!Mesh && Interface->DefaultMesh)
	{
		Mesh = Interface->DefaultMesh;
	}

	if (Component.IsValid() && Mesh)
	{
		PrevTransform = Transform;
		PrevTransformInverseTransposed = TransformInverseTransposed;
		Transform = Component->GetComponentToWorld().ToMatrixWithScale();
		TransformInverseTransposed = Transform.InverseFast().GetTransposed();
	}

	if (!Mesh)
	{
		UE_LOG(LogNiagara, Log, TEXT("StaticMesh data interface has no valid mesh. Failed InitPerInstanceData - %s"), *Interface->GetFullName());
		return false;
	}

	if (!Mesh->bAllowCPUAccess)
	{
		UE_LOG(LogNiagara, Log, TEXT("StaticMesh data interface using a mesh that does not allow CPU access. Failed InitPerInstanceData - Mesh: %s"), *Mesh->GetFullName());
		return false;
	}

	if (!Component.IsValid())
	{
		UE_LOG(LogNiagara, Log, TEXT("StaticMesh data interface has no valid component. Failed InitPerInstanceData - %s"), *Interface->GetFullName());
		return false;
	}

#if WITH_EDITOR
	Mesh->GetOnMeshChanged().AddUObject(SystemInstance->GetComponent(), &UNiagaraComponent::ReinitializeSystem);
#endif

	bMeshAllowsCpuAccess       = Mesh->bAllowCPUAccess;
	bIsCpuUniformlyDistributedSampling = Mesh->bSupportUniformlyDistributedSampling;
	bIsGpuUniformlyDistributedSampling = bIsCpuUniformlyDistributedSampling && Mesh->bSupportGpuUniformlyDistributedSampling;

	//Init the instance filter
	ValidSections.Empty();
	FStaticMeshLODResources& Res = Mesh->RenderData->LODResources[0];
	for (int32 i = 0; i < Res.Sections.Num(); ++i)
	{
		if (Interface->SectionFilter.AllowedMaterialSlots.Num() == 0 || Interface->SectionFilter.AllowedMaterialSlots.Contains(Res.Sections[i].MaterialIndex))
		{
			ValidSections.Add(i);
		}
	}

	if (GetValidSections().Num() == 0)
	{
		UE_LOG(LogNiagara, Log, TEXT("StaticMesh data interface has a section filter preventing any spawning. Failed InitPerInstanceData - %s"), *Interface->GetFullName());
		return false;
	}

	Sampler.Init(&Res, this);

	return true;
}

bool FNDIStaticMesh_InstanceData::ResetRequired(UNiagaraDataInterfaceStaticMesh* Interface)const
{
	check(GetActualMesh());

	if (!Component.IsValid())
	{
		//The component we were bound to is no longer valid so we have to trigger a reset.
		return true;
	}

	if (Interface != nullptr && ChangeId != Interface->ChangeId)
	{
		return true;
	}

	//bool bPrevVCSampling = bSupportingVertexColorSampling;//TODO: Vertex color filtering needs more work.
	bool bReset = false;
	if (Mesh)
	{
		const bool bNewMeshAllowsCpuAccess = Mesh->bAllowCPUAccess;
		const bool bNewIsCpuAreaWeightedSampling = Mesh->bSupportUniformlyDistributedSampling;
		const bool bNewIsGpuAreaWeightedSampling = bIsCpuUniformlyDistributedSampling && Mesh->bSupportGpuUniformlyDistributedSampling;

		//bSupportingVertexColorSampling = bEnableVertexColorRangeSorting && MeshHasColors();
		bReset = bNewMeshAllowsCpuAccess != bMeshAllowsCpuAccess || bNewIsCpuAreaWeightedSampling != bIsCpuUniformlyDistributedSampling || bNewIsGpuAreaWeightedSampling != bIsGpuUniformlyDistributedSampling /* || bSupportingVertexColorSampling != bPrevVCSampling*/;
	}
	return bReset;
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
		if (Component.IsValid() && Mesh)
		{
			PrevTransform = Transform;
			PrevTransformInverseTransposed = TransformInverseTransposed;
			Transform = Component->GetComponentToWorld().ToMatrixWithScale();
			TransformInverseTransposed = Transform.InverseFast().GetTransposed();
		}
		else
		{
			PrevTransform = FMatrix::Identity;
			PrevTransformInverseTransposed = FMatrix::Identity;
			Transform = FMatrix::Identity;
			TransformInverseTransposed = FMatrix::Identity;
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
	FString InstanceInvDeltaTimeName;
	FString InstanceWorldVelocityName;
	FString AreaWeightedSamplingName;
	FString NumTexCoordName;
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
	Names.InstanceInvDeltaTimeName = UNiagaraDataInterfaceStaticMesh::InstanceInvDeltaTimeName + Suffix;
	Names.InstanceWorldVelocityName = UNiagaraDataInterfaceStaticMesh::InstanceWorldVelocityName + Suffix;
	Names.AreaWeightedSamplingName = UNiagaraDataInterfaceStaticMesh::AreaWeightedSamplingName + Suffix;
	Names.NumTexCoordName = UNiagaraDataInterfaceStaticMesh::NumTexCoordName + Suffix;
}

struct FNiagaraDataInterfaceParametersCS_StaticMesh : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		FNDIStaticMeshParametersName ParamNames;
		GetNiagaraDataInterfaceParametersName(ParamNames, ParamRef.ParameterInfo.DataInterfaceHLSLSymbol);

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
		InstanceInvDeltaTime.Bind(ParameterMap, *ParamNames.InstanceInvDeltaTimeName);
		InstanceWorldVelocity.Bind(ParameterMap, *ParamNames.InstanceWorldVelocityName);
		AreaWeightedSampling.Bind(ParameterMap, *ParamNames.AreaWeightedSamplingName);
		NumTexCoord.Bind(ParameterMap, *ParamNames.NumTexCoordName);
	}

	virtual void Serialize(FArchive& Ar)override
	{
		Ar << MeshIndexBuffer;
		Ar << MeshVertexBuffer;
		Ar << MeshTangentBuffer;
		Ar << MeshTexCoordBuffer;
		Ar << MeshColorBuffer;
		Ar << SectionCount;
		Ar << MeshSectionBuffer;
		Ar << MeshTriangleBuffer;
		Ar << InstanceTransform;
		Ar << InstanceTransformInverseTransposed;
		Ar << InstancePrevTransform;
		Ar << InstanceInvDeltaTime;
		Ar << InstanceWorldVelocity;
		Ar << AreaWeightedSampling;
		Ar << NumTexCoord;
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();
		
		{
			FNiagaraDataInterfaceProxyStaticMesh* InterfaceProxy = static_cast<FNiagaraDataInterfaceProxyStaticMesh*>(Context.DataInterface);
			FNiagaraStaticMeshData* Data = InterfaceProxy->SystemInstancesToMeshData.Find(Context.SystemInstance);
			ensureMsgf(Data, TEXT("Failed to find data for instance %s"), *FNiagaraUtilities::SystemInstanceIDToString(Context.SystemInstance));
			if (Data != nullptr)
			{
				FStaticMeshGpuSpawnBuffer* SpawnBuffer = Data->MeshGpuSpawnBuffer;

				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshVertexBuffer, SpawnBuffer->GetBufferPositionSRV());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTangentBuffer, SpawnBuffer->GetBufferTangentSRV());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshIndexBuffer, SpawnBuffer->GetBufferIndexSRV());

				SetShaderValue(RHICmdList, ComputeShaderRHI, NumTexCoord, SpawnBuffer->GetNumTexCoord());
				if (SpawnBuffer->GetNumTexCoord() > 0)
				{
					SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, SpawnBuffer->GetBufferTexCoordSRV());
				}
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshColorBuffer, SpawnBuffer->GetBufferColorSRV());

				check(SpawnBuffer); // should always be allocated, we always need the GPU buffer for a GpuSimulation.
				SetShaderValue(RHICmdList, ComputeShaderRHI, SectionCount, SpawnBuffer->GetValidSectionCount());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSectionBuffer, SpawnBuffer->GetBufferSectionSRV());
				if (Data->bIsGpuUniformlyDistributedSampling)
				{
					SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleBuffer, SpawnBuffer->GetBufferUniformTriangleSamplingSRV());
				}

				const float InvDeltaTime = Data->DeltaSeconds > 0.0f ? 1.0f / Data->DeltaSeconds : 0.0f;
				const FVector DeltaPosition = Data->Transform.GetOrigin() - Data->PrevTransform.GetOrigin();

				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransform, Data->Transform);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransformInverseTransposed, Data->Transform.InverseFast().GetTransposed());
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevTransform, Data->PrevTransform);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceInvDeltaTime, InvDeltaTime);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceWorldVelocity, DeltaPosition * InvDeltaTime);
				SetShaderValue(RHICmdList, ComputeShaderRHI, AreaWeightedSampling, Data->bIsGpuUniformlyDistributedSampling ? 1 : 0);
			}
			else
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshVertexBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTangentBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshIndexBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);

				SetShaderValue(RHICmdList, ComputeShaderRHI, NumTexCoord, 0);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshColorBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);

				SetShaderValue(RHICmdList, ComputeShaderRHI, SectionCount, 0);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSectionBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);

				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransform, FMatrix::Identity);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransformInverseTransposed, FMatrix::Identity);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevTransform, FMatrix::Identity);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceInvDeltaTime, 1.0f);
				SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceWorldVelocity, FVector::ZeroVector);
				SetShaderValue(RHICmdList, ComputeShaderRHI, AreaWeightedSampling, 0);
			}
		}
	}


private:

	FShaderResourceParameter MeshIndexBuffer;
	FShaderResourceParameter MeshVertexBuffer;
	FShaderResourceParameter MeshTangentBuffer;
	FShaderResourceParameter MeshTexCoordBuffer;
	FShaderResourceParameter MeshColorBuffer;
	FShaderResourceParameter MeshSectionBuffer;
	FShaderResourceParameter MeshTriangleBuffer;
	FShaderParameter SectionCount;
	FShaderParameter InstanceTransform;
	FShaderParameter InstanceTransformInverseTransposed;
	FShaderParameter InstancePrevTransform;
	FShaderParameter InstanceInvDeltaTime;
	FShaderParameter InstanceWorldVelocity;
	FShaderParameter AreaWeightedSampling;
	FShaderParameter NumTexCoord;
};

//////////////////////////////////////////////////////////////////////////

void FNiagaraDataInterfaceProxyStaticMesh::DeferredDestroy()
{
	//-TODO: This is incorrect, we could be destroying instance data for a batcher is yet to tick, we should only be destroying data for this batcher!
	for (const FNiagaraSystemInstanceID& Sys : DeferredDestroyList)
	{
		SystemInstancesToMeshData.Remove(Sys);
		//UE_LOG(LogNiagara, Log, TEXT("RT: StaticMesh DI - DeferredDestroy %s"), *Sys.ToString());
	}

	DeferredDestroyList.Empty();
}

void FNiagaraDataInterfaceProxyStaticMesh::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance, FStaticMeshGpuSpawnBuffer* MeshGPUSpawnBuffer)
{
	check(IsInRenderingThread());

	FNiagaraStaticMeshData* Data = SystemInstancesToMeshData.Find(SystemInstance);
	if (Data != nullptr)
	{
		DeferredDestroyList.Remove(SystemInstance);
	}
	else
	{
		Data = &SystemInstancesToMeshData.Add(SystemInstance);
	}
	//UE_LOG(LogNiagara, Log, TEXT("RT: StaticMesh DI - InitializePerInstanceData %s"), *SystemInstance.ToString());

	// @todo-threadsafety We should not ever see this case! Though it's not really an error...
	if (Data->MeshGpuSpawnBuffer)
	{
		Data->MeshGpuSpawnBuffer->ReleaseResource();
		delete Data->MeshGpuSpawnBuffer;
	}

	Data->MeshGpuSpawnBuffer = MeshGPUSpawnBuffer;
}

void FNiagaraDataInterfaceProxyStaticMesh::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	//UE_LOG(LogNiagara, Log, TEXT("RT: StaticMesh DI - DestroyPerInstanceData %s"), *SystemInstance.ToString());

	// @todo-threadsafety verify this destroys the MeshGPUSpawnBuffer data. This thread owns it now.
	//SystemInstancesToMeshData.Remove(SystemInstance);
	DeferredDestroyList.Add(SystemInstance);
	Batcher->EnqueueDeferredDeletesForDI_RenderThread(this->AsShared());
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
		check(Data->MeshGpuSpawnBuffer);

		Data->bIsGpuUniformlyDistributedSampling = SourceData->bIsGpuUniformlyDistributedSampling;
		Data->DeltaSeconds = SourceData->DeltaSeconds;
		Data->Transform = SourceData->Transform;
		Data->PrevTransform = SourceData->PrevTransform;
	}
	else
	{
		UE_LOG(LogNiagara, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %s"), *FNiagaraUtilities::SystemInstanceIDToString(Instance));
	}
}

//////////////////////////////////////////////////////////////////////////

UNiagaraDataInterfaceStaticMesh::UNiagaraDataInterfaceStaticMesh(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultMesh(nullptr)
	, Source(nullptr)	
	, ChangeId(0)
	//, bSupportingVertexColorSampling(0)//Vertex color filtering needs some more work.
	//, bFilterInitialized(false)
{
	Proxy = MakeShared<FNiagaraDataInterfaceProxyStaticMesh, ESPMode::ThreadSafe>();
}

void UNiagaraDataInterfaceStaticMesh::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);

		//Still some issues with using custom structs. Convert node for example throws a wobbler. TODO after GDC.
		FNiagaraTypeRegistry::Register(FMeshTriCoordinate::StaticStruct(), true, true, false);
	}
}

#if WITH_EDITOR
void UNiagaraDataInterfaceStaticMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ChangeId++;
}

#endif //WITH_EDITOR

namespace StaticMeshHelpers
{
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
};

void UNiagaraDataInterfaceStaticMesh::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::RandomSectionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Section")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::RandomTriCoordName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
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
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::RandomTriCoordOnSectionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Section")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetTriPositionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetTriPositionAndVelocityName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetTriPositionWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetTriNormalName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetTriNormalWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetTriTangentsName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetTriTangentsWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetTriColorName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetTriUVName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("UV Set")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("UV")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetMeshLocalToWorldName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Transform")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetMeshLocalToWorldInverseTransposedName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Transform")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetMeshWorldVelocityName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetVertexPositionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetVertexPositionDesc", "Returns the local space vertex position for the passed vertex.");
#endif
		OutFunctions.Add(Sig);
	} 
	
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = StaticMeshHelpers::GetVertexPositionWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("StaticMesh")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
#if WITH_EDITORONLY_DATA
		Sig.Description = LOCTEXT("GetVertexPositionWSDesc", "Returns the world space vertex position for the passed vertex.");
#endif
		OutFunctions.Add(Sig);
	}
}

//External function binder choosing between template specializations based on UsesAreaWeighting
template<typename NextBinder>
struct TUsesAreaWeightingBinder
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		FNDIStaticMesh_InstanceData* InstData = (FNDIStaticMesh_InstanceData*)InstanceData;
		UNiagaraDataInterfaceStaticMesh* MeshInterface = CastChecked<UNiagaraDataInterfaceStaticMesh>(Interface);
		if (InstData->UsesCpuUniformlyDistributedSampling())
		{
			NextBinder::template Bind<ParamTypes..., TIntegralConstant<bool, true>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NextBinder::template Bind<ParamTypes..., TIntegralConstant<bool, false>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
	}
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
		UNiagaraDataInterfaceStaticMesh* MeshInterface = CastChecked<UNiagaraDataInterfaceStaticMesh>(Interface);
		check(InstData->Mesh);
		FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
		if (Res.VertexBuffers.StaticMeshVertexBuffer.GetUseHighPrecisionTangentBasis())			
		{
			if (Res.VertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
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
			if (Res.VertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
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
	check(InstData && InstData->Mesh && InstData->Component.IsValid());

	bool bNeedsVertexPositions = false;
	bool bNeedsVertexColors = false;
	bool bNeedsVertMain = true;//Assuming we always need this?
	
	if (BindingInfo.Name == StaticMeshHelpers::RandomSectionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		TUsesAreaWeightingBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomSection)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::RandomTriCoordName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 4);
		TUsesAreaWeightingBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomTriCoord)>::Bind(this, BindingInfo, InstanceData, OutFunc);
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
		TUsesAreaWeightingBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, RandomTriCoordOnSection)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriPositionName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		bNeedsVertexPositions = true;
		TNDIExplicitBinder<FNDITransformHandlerNoop, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordPosition)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriPositionWSName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		bNeedsVertexPositions = true;
		TNDIExplicitBinder<FNDITransformHandler, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordPosition)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriNormalName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		bNeedsVertMain = true;
		TNDIExplicitBinder<FNDITransformHandlerNoop, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordNormal)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriNormalWSName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		bNeedsVertMain = true;
		TNDIExplicitBinder<FNDITransformHandler, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordNormal)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriTangentsName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 9);
		bNeedsVertMain = true;
		TTypedMeshAccessorBinder<TNDIExplicitBinder<FNDITransformHandlerNoop, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordTangents)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriTangentsWSName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 9);
		bNeedsVertMain = true;
		TTypedMeshAccessorBinder<TNDIExplicitBinder<FNDITransformHandler, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordTangents)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriColorName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 4);
		bNeedsVertexColors = true;
		NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordColor)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriUVName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 2);
		bNeedsVertMain = true;
		TTypedMeshAccessorBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetTriCoordUV)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetTriPositionAndVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 6);
		bNeedsVertMain = true;
		bNeedsVertexPositions = true;  
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
		bNeedsVertexPositions = true;
		TNDIExplicitBinder<FNDITransformHandlerNoop, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetVertexPosition)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == StaticMeshHelpers::GetVertexPositionWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		bNeedsVertexPositions = true;
		TNDIExplicitBinder<FNDITransformHandler, NDI_FUNC_BINDER(UNiagaraDataInterfaceStaticMesh, GetVertexPosition)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}

	if (bNeedsVertexPositions && !InstData->MeshHasPositions())
	{
		UE_LOG(LogNiagara, Log, TEXT("Static Mesh data interface is cannot run as it's reading position data on a mesh that does not provide it. - Mesh:%s  "), *InstData->Mesh->GetFullName());
	}
	if (bNeedsVertexColors && !InstData->MeshHasColors())
	{
		UE_LOG(LogNiagara, Log, TEXT("Static Mesh data interface is cannot run as it's reading color data on a mesh that does not provide it. - Mesh:%s  "), *InstData->Mesh->GetFullName());
	}
	if (bNeedsVertMain && !InstData->MeshHasVerts())
	{
		UE_LOG(LogNiagara, Log, TEXT("Static Mesh data interface is cannot run as it's reading vertex data on a mesh with no vertex data. - Mesh:%s  "), *InstData->Mesh->GetFullName());
	}
}

bool UNiagaraDataInterfaceStaticMesh::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceStaticMesh* OtherTyped = CastChecked<UNiagaraDataInterfaceStaticMesh>(Destination);
	OtherTyped->Source = Source;
	OtherTyped->DefaultMesh = DefaultMesh;
	//OtherTyped->bEnableVertexColorRangeSorting = bEnableVertexColorRangeSorting;//TODO: Vertex color filtering needs more work.
	OtherTyped->SectionFilter = SectionFilter;
	return true;
}

bool UNiagaraDataInterfaceStaticMesh::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceStaticMesh* OtherTyped = CastChecked<const UNiagaraDataInterfaceStaticMesh>(Other);
	return OtherTyped->Source == Source &&
		OtherTyped->DefaultMesh == DefaultMesh &&
		//OtherTyped->bEnableVertexColorRangeSorting == bEnableVertexColorRangeSorting &&//TODO: Vertex color filtering needs more work.
		OtherTyped->SectionFilter.AllowedMaterialSlots == SectionFilter.AllowedMaterialSlots;
}

bool UNiagaraDataInterfaceStaticMesh::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIStaticMesh_InstanceData* Inst = new (PerInstanceData) FNDIStaticMesh_InstanceData();
	bool bSuccess = Inst->Init(this, SystemInstance);

	//UE_LOG(LogNiagara, Log, TEXT("GT: StaticMesh DI - InitPerInstanceData %s"), *SystemInstance->GetId().ToString());

	if (bSuccess)
	{
		// Always allocate when bAllowCPUAccess (index buffer can only have SRV created in this case as of today)
		// We do not know if this interface is allocated for CPU or GPU so we allocate for both case... TODO: have some cached data created in case a GPU version is needed?
		if (Inst->Mesh->bAllowCPUAccess)
		{
			FStaticMeshLODResources& Res = Inst->Mesh->RenderData->LODResources[0];

			FStaticMeshGpuSpawnBuffer* MeshGpuSpawnBuffer = new FStaticMeshGpuSpawnBuffer;
			MeshGpuSpawnBuffer->Initialise(Res, *this, Inst->bIsGpuUniformlyDistributedSampling, Inst->ValidSections, Inst->Sampler);

			// Push instance data to RT
			{
				FNiagaraDataInterfaceProxyStaticMesh* ThisProxy = GetProxyAs<FNiagaraDataInterfaceProxyStaticMesh>();
				ENQUEUE_RENDER_COMMAND(FNiagaraDIPushInitialInstanceDataToRT) (
					[ThisProxy, InstanceID = SystemInstance->GetId(), MeshGpuSpawnBuffer](FRHICommandListImmediate& CmdList)
					{
						MeshGpuSpawnBuffer->InitResource();
						ThisProxy->InitializePerInstanceData(InstanceID, MeshGpuSpawnBuffer);
					}
				);
			}
		}
		else
		{
			ensure(Inst->Mesh->bAllowCPUAccess);
		}
	}
	else
	{
		UE_LOG(LogNiagara, Warning, TEXT("UNiagaraDataInterfaceStaticMesh::InitPerInstanceData> Invalid per-instance init!!!"));
	}

	return bSuccess;
}

void UNiagaraDataInterfaceStaticMesh::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIStaticMesh_InstanceData* Inst = (FNDIStaticMesh_InstanceData*)PerInstanceData;

	//UE_LOG(LogNiagara, Log, TEXT("GT: StaticMesh DI - DestroyPerInstanceData %s"), *SystemInstance->GetId().ToString());

#if WITH_EDITOR
	if (Inst->Mesh)
	{
		Inst->Mesh->GetOnMeshChanged().RemoveAll(SystemInstance->GetComponent());
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
TArray<FNiagaraDataInterfaceError> UNiagaraDataInterfaceStaticMesh::GetErrors()
{
	TArray<FNiagaraDataInterfaceError> Errors;
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

		Errors.Add(CPUAccessNotAllowedError);
	}
	return Errors;
}
#endif

//RandomSection specializations.
//Each combination for AreaWeighted and Section filtered.
template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomSection<TIntegralConstant<bool, true>, true>(FRandomStream& RandStream, FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	checkSlow(InstData->GetValidSections().Num() > 0);
	int32 Idx = InstData->GetAreaWeigtedSampler().GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
	return InstData->GetValidSections()[Idx];
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomSection<TIntegralConstant<bool, true>, false>(FRandomStream& RandStream, FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	return Res.AreaWeightedSampler.GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomSection<TIntegralConstant<bool, false>, true>(FRandomStream& RandStream, FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	checkSlow(InstData->GetValidSections().Num() > 0);
	int32 Idx = RandStream.RandRange(0, InstData->GetValidSections().Num() - 1);
	return InstData->GetValidSections()[Idx];
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomSection<TIntegralConstant<bool, false>, false>(FRandomStream& RandStream, FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	return RandStream.RandRange(0, Res.Sections.Num() - 1);
}

template<typename TAreaWeighted>
void UNiagaraDataInterfaceStaticMesh::RandomSection(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutSection(Context);

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutSection.GetDest() = RandomSection<TAreaWeighted, true>(Context.RandStream, Res, InstData);
		OutSection.Advance();
	}
}

//RandomTriIndex specializations.
//Each combination for AreaWeighted and Section filtered.
template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndex<TIntegralConstant<bool, true>, true>(FRandomStream& RandStream, FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	int32 SecIdx = RandomSection<TIntegralConstant<bool, true>, true>(RandStream, Res, InstData);
	FStaticMeshSection&  Sec = Res.Sections[SecIdx];
	int32 Tri = Res.AreaWeightedSectionSamplers[SecIdx].GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
	return (Sec.FirstIndex / 3) + Tri;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndex<TIntegralConstant<bool, true>, false>(FRandomStream& RandStream, FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	int32 SecIdx = RandomSection<TIntegralConstant<bool, true>, false>(RandStream, Res, InstData);
	FStaticMeshSection&  Sec = Res.Sections[SecIdx];
	int32 Tri = Res.AreaWeightedSectionSamplers[SecIdx].GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
	return (Sec.FirstIndex / 3) + Tri;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndex<TIntegralConstant<bool, false>, true>(FRandomStream& RandStream, FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	int32 SecIdx = RandomSection<TIntegralConstant<bool, false>, true>(RandStream, Res, InstData);
	FStaticMeshSection&  Sec = Res.Sections[SecIdx];
	int32 Tri = RandStream.RandRange(0, Sec.NumTriangles - 1);
	return (Sec.FirstIndex / 3) + Tri;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndex<TIntegralConstant<bool, false>, false>(FRandomStream& RandStream, FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData)
{
	int32 SecIdx = RandomSection<TIntegralConstant<bool, false>, false>(RandStream, Res, InstData);
	FStaticMeshSection&  Sec = Res.Sections[SecIdx];
	int32 Tri = RandStream.RandRange(0, Sec.NumTriangles - 1);
	return (Sec.FirstIndex / 3) + Tri;
}

template<typename TAreaWeighted>
void UNiagaraDataInterfaceStaticMesh::RandomTriCoord(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutTri(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryZ(Context);

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	FIndexArrayView Indices = Res.IndexBuffer.GetArrayView();
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutTri.GetDest() = RandomTriIndex<TAreaWeighted, true>(Context.RandStream, Res, InstData);
		FVector Bary = RandomBarycentricCoord(Context.RandStream);
		*OutBaryX.GetDest() = Bary.X;
		*OutBaryY.GetDest() = Bary.Y;
		*OutBaryZ.GetDest() = Bary.Z;
		
		OutTri.Advance();
		OutBaryX.Advance();
		OutBaryY.Advance();
		OutBaryZ.Advance();
	}
}

void UNiagaraDataInterfaceStaticMesh::RandomTriCoordVertexColorFiltered(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncRegisterHandler<int32> MinValue(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> RangeValue(Context);
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutTri(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryZ(Context);
	
	FDynamicVertexColorFilterData* VCFData = InstData->DynamicVertexColorSampler.Get();

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	FIndexArrayView Indices = Res.IndexBuffer.GetArrayView();

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		uint32 StartIdx = (uint32)(MinValue.Get()*255.0f);
		uint32 Range = (uint32)(RangeValue.Get()*255.0f + 0.5f);
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
		*OutTri.GetDest() = VCFData->TrianglesSortedByVertexColor[VCFData->VertexColorToTriangleStart[StartIdx] + RandomTri];

		FVector Bary = RandomBarycentricCoord(Context.RandStream);
		*OutBaryX.GetDest() = Bary.X;
		*OutBaryY.GetDest() = Bary.Y;
		*OutBaryZ.GetDest() = Bary.Z;

		MinValue.Advance();
		RangeValue.Advance();
		OutTri.Advance();
		OutBaryX.Advance();
		OutBaryY.Advance();
		OutBaryZ.Advance();
	}
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndexOnSection<TIntegralConstant<bool, true>>(FRandomStream& RandStream, FStaticMeshLODResources& Res, int32 SecIdx, FNDIStaticMesh_InstanceData* InstData)
{
	FStaticMeshSection&  Sec = Res.Sections[SecIdx];
	int32 Tri = Res.AreaWeightedSectionSamplers[SecIdx].GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());
	return (Sec.FirstIndex / 3) + Tri;
}

template<>
FORCEINLINE int32 UNiagaraDataInterfaceStaticMesh::RandomTriIndexOnSection<TIntegralConstant<bool, false>>(FRandomStream& RandStream, FStaticMeshLODResources& Res, int32 SecIdx, FNDIStaticMesh_InstanceData* InstData)
{
	FStaticMeshSection&  Sec = Res.Sections[SecIdx];
	int32 Tri = RandStream.RandRange(0, Sec.NumTriangles - 1);
	return (Sec.FirstIndex / 3) + Tri;
}

template<typename TAreaWeighted>
void UNiagaraDataInterfaceStaticMesh::RandomTriCoordOnSection(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<int32> SectionIdxParam(Context);
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutTri(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBaryZ(Context);

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	FIndexArrayView Indices = Res.IndexBuffer.GetArrayView();
	const int32 MaxSection = Res.Sections.Num() - 1;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{

		int32 SecIdx = FMath::Clamp(SectionIdxParam.Get(), 0, MaxSection);
		*OutTri.GetDest() = RandomTriIndexOnSection<TAreaWeighted>(Context.RandStream, Res, SecIdx, InstData);
		FVector Bary = RandomBarycentricCoord(Context.RandStream);
		*OutBaryX.GetDest() = Bary.X;
		*OutBaryY.GetDest() = Bary.Y;
		*OutBaryZ.GetDest() = Bary.Z;

		SectionIdxParam.Advance();
		OutTri.Advance();
		OutBaryX.Advance();
		OutBaryY.Advance();
		OutBaryZ.Advance();
	}
}

template<typename TransformHandlerType>
void UNiagaraDataInterfaceStaticMesh::GetTriCoordPosition(FVectorVMContext& Context)
{
	TransformHandlerType TransformHandler;
	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosZ(Context);

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	const FIndexArrayView& Indices = Res.IndexBuffer.GetArrayView();
	const FPositionVertexBuffer& Positions = Res.VertexBuffers.PositionVertexBuffer;

	const int32 NumTriangles = Indices.Num() / 3;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = (TriParam.Get() % NumTriangles) * 3;
		const int32 Idx0 = Indices[Tri];
		const int32 Idx1 = Indices[Tri + 1];
		const int32 Idx2 = Indices[Tri + 2];

		FVector Pos = BarycentricInterpolate(BaryXParam.Get(), BaryYParam.Get(), BaryZParam.Get(), Positions.VertexPosition(Idx0), Positions.VertexPosition(Idx1), Positions.VertexPosition(Idx2));
		TransformHandler.TransformPosition(Pos, InstData->Transform);

		*OutPosX.GetDest() = Pos.X;
		*OutPosY.GetDest() = Pos.Y;
		*OutPosZ.GetDest() = Pos.Z;

		TriParam.Advance();
		BaryXParam.Advance();
		BaryYParam.Advance();
		BaryZParam.Advance();
		OutPosX.Advance();
		OutPosY.Advance();
		OutPosZ.Advance();
	}
}

template<typename TransformHandlerType>
void UNiagaraDataInterfaceStaticMesh::GetTriCoordNormal(FVectorVMContext& Context)
{
	TransformHandlerType TransformHandler;

	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutNormX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutNormY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutNormZ(Context);

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	const FIndexArrayView& Indices = Res.IndexBuffer.GetArrayView();
	const FStaticMeshVertexBuffer& Verts = Res.VertexBuffers.StaticMeshVertexBuffer;

	const int32 NumTriangles = Indices.Num() / 3;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = (TriParam.Get() % NumTriangles) * 3;
		const int32 Idx0 = Indices[Tri];
		const int32 Idx1 = Indices[Tri + 1];
		const int32 Idx2 = Indices[Tri + 2];

		FVector Norm = BarycentricInterpolate(BaryXParam.Get(), BaryYParam.Get(), BaryZParam.Get(), Verts.VertexTangentZ(Idx0), Verts.VertexTangentZ(Idx1), Verts.VertexTangentZ(Idx2));
		TransformHandler.TransformVector(Norm, InstData->TransformInverseTransposed);

		*OutNormX.GetDest() = Norm.X;
		*OutNormY.GetDest() = Norm.Y;
		*OutNormZ.GetDest() = Norm.Z;
		TriParam.Advance();
		BaryXParam.Advance();
		BaryYParam.Advance();
		BaryZParam.Advance();
		OutNormX.Advance();
		OutNormY.Advance();
		OutNormZ.Advance();
	}
}

template<typename VertexAccessorType, typename TransformHandlerType>
void UNiagaraDataInterfaceStaticMesh::GetTriCoordTangents(FVectorVMContext& Context)
{
	TransformHandlerType TransformHandler;	

	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	const FIndexArrayView& Indices = Res.IndexBuffer.GetArrayView();
	const VertexAccessorType Verts(Res.VertexBuffers.StaticMeshVertexBuffer);

	VectorVM::FExternalFuncRegisterHandler<float> OutTangentX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutTangentY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutTangentZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBinormX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBinormY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBinormZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutNormX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutNormY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutNormZ(Context);

	const int32 NumTriangles = Indices.Num() / 3;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = (TriParam.Get() % NumTriangles) * 3;
		const int32 Idx0 = Indices[Tri];
		const int32 Idx1 = Indices[Tri + 1];
		const int32 Idx2 = Indices[Tri + 2];
		FVector Tangent = BarycentricInterpolate(BaryXParam.Get(), BaryYParam.Get(), BaryZParam.Get(), Verts.GetTangentX(Idx0), Verts.GetTangentX(Idx1), Verts.GetTangentX(Idx2));
		FVector Binorm = BarycentricInterpolate(BaryXParam.Get(), BaryYParam.Get(), BaryZParam.Get(), Verts.GetTangentY(Idx0), Verts.GetTangentY(Idx1), Verts.GetTangentY(Idx2));
		FVector Norm = BarycentricInterpolate(BaryXParam.Get(), BaryYParam.Get(), BaryZParam.Get(), Verts.GetTangentZ(Idx0), Verts.GetTangentZ(Idx1), Verts.GetTangentZ(Idx2));
		TransformHandler.TransformVector(Tangent, InstData->TransformInverseTransposed);
		TransformHandler.TransformVector(Binorm, InstData->TransformInverseTransposed);
		TransformHandler.TransformVector(Norm, InstData->TransformInverseTransposed);
		*OutTangentX.GetDest() = Tangent.X;
		*OutTangentY.GetDest() = Tangent.Y;
		*OutTangentZ.GetDest() = Tangent.Z;
		*OutBinormX.GetDest() = Binorm.X;
		*OutBinormY.GetDest() = Binorm.Y;
		*OutBinormZ.GetDest() = Binorm.Z;
		*OutNormX.GetDest() = Norm.X;
		*OutNormY.GetDest() = Norm.Y;
		*OutNormZ.GetDest() = Norm.Z;

		TriParam.Advance();
		BaryXParam.Advance();
		BaryYParam.Advance();
		BaryZParam.Advance();
		OutTangentX.Advance();
		OutTangentY.Advance();
		OutTangentZ.Advance();
		OutBinormX.Advance();
		OutBinormY.Advance();
		OutBinormZ.Advance();
		OutNormX.Advance();
		OutNormY.Advance();
		OutNormZ.Advance();
	}
}

void UNiagaraDataInterfaceStaticMesh::GetTriCoordColor(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutColorR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutColorA(Context);

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	const FIndexArrayView& Indices = Res.IndexBuffer.GetArrayView();
	const FColorVertexBuffer& Colors = Res.VertexBuffers.ColorVertexBuffer;

	if (Colors.GetNumVertices() > 0)
	{
		const int32 NumTriangles = Indices.Num() / 3;
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const int32 Tri = (TriParam.Get() % NumTriangles) * 3;
			const int32 Idx0 = Indices[Tri];
			const int32 Idx1 = Indices[Tri + 1];
			const int32 Idx2 = Indices[Tri + 2];

			FLinearColor Color = BarycentricInterpolate(BaryXParam.Get(), BaryYParam.Get(), BaryZParam.Get(),
				Colors.VertexColor(Idx0).ReinterpretAsLinear(), Colors.VertexColor(Idx1).ReinterpretAsLinear(), Colors.VertexColor(Idx2).ReinterpretAsLinear());

			*OutColorR.GetDest() = Color.R;
			*OutColorG.GetDest() = Color.G;
			*OutColorB.GetDest() = Color.B;
			*OutColorA.GetDest() = Color.A;
			TriParam.Advance();
			BaryXParam.Advance();
			BaryYParam.Advance();
			BaryZParam.Advance();
			OutColorR.Advance();
			OutColorG.Advance();
			OutColorB.Advance();
			OutColorA.Advance();
		}
	}
	else
	{
		// This mesh doesn't have color information so set the color to white.
		FLinearColor Color = FLinearColor::White;
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			*OutColorR.GetDest() = Color.R;
			*OutColorG.GetDest() = Color.G;
			*OutColorB.GetDest() = Color.B;
			*OutColorA.GetDest() = Color.A;
			TriParam.Advance();
			BaryXParam.Advance();
			BaryYParam.Advance();
			BaryZParam.Advance();
			OutColorR.Advance();
			OutColorG.Advance();
			OutColorB.Advance();
			OutColorA.Advance();
		}
	}
}

template<typename VertexAccessorType>
void UNiagaraDataInterfaceStaticMesh::GetTriCoordUV(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> UVSetParam(Context);
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutU(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutV(Context);

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	const FIndexArrayView& Indices = Res.IndexBuffer.GetArrayView();
	const VertexAccessorType Verts(Res.VertexBuffers.StaticMeshVertexBuffer);

	const int32 NumTriangles = Indices.Num() / 3;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = (TriParam.Get() % NumTriangles) * 3;
		const int32 Idx0 = Indices[Tri];
		const int32 Idx1 = Indices[Tri + 1];
		const int32 Idx2 = Indices[Tri + 2];

		int32 UVSet = UVSetParam.Get();
		FVector2D UV = BarycentricInterpolate(BaryXParam.Get(), BaryYParam.Get(), BaryZParam.Get(),	Verts.GetUV(Idx0, UVSet), Verts.GetUV(Idx1, UVSet),	Verts.GetUV(Idx2, UVSet));

		*OutU.GetDest() = UV.X;
		*OutV.GetDest() = UV.Y;

		TriParam.Advance();
		BaryXParam.Advance();
		BaryYParam.Advance();
		BaryZParam.Advance();
		UVSetParam.Advance();
		OutU.Advance();
		OutV.Advance();
	}
}

void UNiagaraDataInterfaceStaticMesh::GetTriCoordPositionAndVelocity(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<int32> TriParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> BaryZParam(Context);
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelZ(Context);

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	const FIndexArrayView& Indices = Res.IndexBuffer.GetArrayView();
	const FPositionVertexBuffer& Positions = Res.VertexBuffers.PositionVertexBuffer;

	const int32 NumTriangles = Indices.Num() / 3;
	float InvDt = 1.0f / InstData->DeltaSeconds;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const int32 Tri = (TriParam.Get() % NumTriangles) * 3;
		const int32 Idx0 = Indices[Tri];
		const int32 Idx1 = Indices[Tri + 1];
		const int32 Idx2 = Indices[Tri + 2];

		FVector Pos = BarycentricInterpolate(BaryXParam.Get(), BaryYParam.Get(), BaryZParam.Get(), Positions.VertexPosition(Idx0), Positions.VertexPosition(Idx1), Positions.VertexPosition(Idx2));

		FVector PrevWSPos = InstData->PrevTransform.TransformPosition(Pos);
		FVector WSPos = InstData->Transform.TransformPosition(Pos);

		FVector Vel = (WSPos - PrevWSPos) * InvDt;
		*OutPosX.GetDest() = WSPos.X;
		*OutPosY.GetDest() = WSPos.Y;
		*OutPosZ.GetDest() = WSPos.Z;
		*OutVelX.GetDest() = Vel.X;
		*OutVelY.GetDest() = Vel.Y;
		*OutVelZ.GetDest() = Vel.Z;
		TriParam.Advance();
		BaryXParam.Advance();
		BaryYParam.Advance();
		BaryZParam.Advance();
		OutPosX.Advance();
		OutPosY.Advance();
		OutPosZ.Advance();
		OutVelX.Advance();
		OutVelY.Advance();
		OutVelZ.Advance();
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
		*Out01.GetDest() = ToWrite.M[0][0]; Out01.Advance();
		*Out02.GetDest() = ToWrite.M[0][0]; Out02.Advance();
		*Out03.GetDest() = ToWrite.M[0][0]; Out03.Advance();
		*Out04.GetDest() = ToWrite.M[0][0]; Out04.Advance();
		*Out05.GetDest() = ToWrite.M[0][0]; Out05.Advance();
		*Out06.GetDest() = ToWrite.M[0][0]; Out06.Advance();
		*Out07.GetDest() = ToWrite.M[0][0]; Out07.Advance();
		*Out08.GetDest() = ToWrite.M[0][0]; Out08.Advance();
		*Out09.GetDest() = ToWrite.M[0][0]; Out09.Advance();
		*Out10.GetDest() = ToWrite.M[0][0]; Out10.Advance();
		*Out11.GetDest() = ToWrite.M[0][0]; Out11.Advance();
		*Out12.GetDest() = ToWrite.M[0][0]; Out12.Advance();
		*Out13.GetDest() = ToWrite.M[0][0]; Out13.Advance();
		*Out14.GetDest() = ToWrite.M[0][0]; Out14.Advance();
		*Out15.GetDest() = ToWrite.M[0][0]; Out15.Advance();
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

	VectorVM::FExternalFuncRegisterHandler<float> OutVelX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelZ(Context);

	FVector Velocity(0.0f, 0.0f, 0.0f);
	float InvDeltaTime = 1.0f / InstData->DeltaSeconds;
	if (InstData->DeltaSeconds > 0.0f)
	{
		Velocity = (FVector(InstData->Transform.M[3][0], InstData->Transform.M[3][1], InstData->Transform.M[3][2]) - 
			FVector(InstData->PrevTransform.M[3][0], InstData->PrevTransform.M[3][1], InstData->PrevTransform.M[3][2])) * InvDeltaTime;
	}

	for (int32 i = 0; i < Context.NumInstances; i++)
	{
		*OutVelX.GetDest() = Velocity.X;
		*OutVelY.GetDest() = Velocity.Y;
		*OutVelZ.GetDest() = Velocity.Z;
		OutVelX.Advance();
		OutVelY.Advance();
		OutVelZ.Advance();
	}
}

template<typename TransformHandlerType>
void UNiagaraDataInterfaceStaticMesh::GetVertexPosition(FVectorVMContext& Context)
{
	TransformHandlerType TransformHandler;
	VectorVM::FExternalFuncInputHandler<int32> VertexIndexParam(Context);
	VectorVM::FUserPtrHandler<FNDIStaticMesh_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosZ(Context);

	FStaticMeshLODResources& Res = InstData->Mesh->RenderData->LODResources[0];
	const FPositionVertexBuffer& Positions = Res.VertexBuffers.PositionVertexBuffer;

	const int32 NumVerts = Positions.GetNumVertices();
	FVector Pos;
	for (int32 i = 0; i < Context.NumInstances; i++)
	{
		int32 VertexIndex = VertexIndexParam.Get() % NumVerts;
		Pos = Positions.VertexPosition(VertexIndex);		
		TransformHandler.TransformPosition(Pos, InstData->Transform);
		VertexIndexParam.Advance();
		*OutPosX.GetDestAndAdvance() = Pos.X;
		*OutPosY.GetDestAndAdvance() = Pos.Y;
		*OutPosZ.GetDestAndAdvance() = Pos.Z;
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
		{TEXT("InstanceInvDeltaTimeName"), ParamNames.InstanceInvDeltaTimeName},
		{TEXT("InstanceWorldVelocity"), ParamNames.InstanceWorldVelocityName},
		{TEXT("AreaWeightedSamplingName"), ParamNames.AreaWeightedSamplingName},
		{TEXT("NumTexCoordName"), ParamNames.NumTexCoordName},
	};

	if (FunctionInfo.DefinitionName == StaticMeshHelpers::RandomSectionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (out int Out_Section)
			{
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
			uint TriangleIndex = In_Coord.Tri * 3;
			uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ] * 2;
			uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1] * 2;
			uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2] * 2;

			float3 Normal0 = TangentBias({MeshTangentBufferName}[VertexIndex0+1].xyz);
			float3 Normal1 = TangentBias({MeshTangentBufferName}[VertexIndex1+1].xyz);
			float3 Normal2 = TangentBias({MeshTangentBufferName}[VertexIndex2+1].xyz);

			float3 Normal   = Normal0 * In_Coord.BaryCoord.x + Normal1 * In_Coord.BaryCoord.y + Normal2 * In_Coord.BaryCoord.z;

			Out_Normal = normalize(mul(float4(Normal, 0.0), {InstanceTransformName}).xyz);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriTangentsName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float3 Out_Tangent, out float3 Out_Binormal, out float3 Out_Normal)
		{
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

			float3 NormalWorld  = normalize(mul(float4(Normal  , 0.0), {InstanceTransformName}).xyz);
			float3 TangentWorld = normalize(mul(float4(Tangent , 0.0), {InstanceTransformName}).xyz);
			float3 BinormalWorld= normalize(mul(float4(Binormal, 0.0), {InstanceTransformName}).xyz);

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
				uint TriangleIndex = In_Coord.Tri * 3;
				uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ];
				uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1];
				uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2];

				float4 Color0 = {MeshColorBufferName}[VertexIndex0] FMANUALFETCH_COLOR_COMPONENT_SWIZZLE;
				float4 Color1 = {MeshColorBufferName}[VertexIndex1] FMANUALFETCH_COLOR_COMPONENT_SWIZZLE;
				float4 Color2 = {MeshColorBufferName}[VertexIndex2] FMANUALFETCH_COLOR_COMPONENT_SWIZZLE;

				Out_Color = Color0 * In_Coord.BaryCoord.x + Color1 * In_Coord.BaryCoord.y + Color2 * In_Coord.BaryCoord.z;
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == StaticMeshHelpers::GetTriUVName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, in int In_UVSet, out float2 Out_UV)
			{
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
					VertexIndex *= 3;
					Out_Position = float3({MeshVertexBufferName}[VertexIndex], {MeshVertexBufferName}[VertexIndex+1], {MeshVertexBufferName}[VertexIndex+2]);
					Out_Position = mul(float4(Out_Position, 1.0), {InstanceTransformName}).xyz;
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
	OutHLSL += TEXT("uint ") + ParamNames.SectionCountName + TEXT(";\n");
	OutHLSL += TEXT("float4x4 ") + ParamNames.InstanceTransformName + TEXT(";\n");
	OutHLSL += TEXT("float4x4 ") + ParamNames.InstanceTransformInverseTransposedName + TEXT(";\n");
	OutHLSL += TEXT("float4x4 ") + ParamNames.InstancePrevTransformName + TEXT(";\n");
	OutHLSL += TEXT("float ") + ParamNames.InstanceInvDeltaTimeName + TEXT(";\n");
	OutHLSL += TEXT("float4 ") + ParamNames.InstanceWorldVelocityName + TEXT(";\n");
	OutHLSL += TEXT("uint ") + ParamNames.AreaWeightedSamplingName + TEXT(";\n");	// Could be used for other flags
	OutHLSL += TEXT("uint ") + ParamNames.NumTexCoordName + TEXT(";\n");
}

void UNiagaraDataInterfaceStaticMesh::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(Proxy);

	FNDIStaticMesh_InstanceData* InstanceData = static_cast<FNDIStaticMesh_InstanceData*>(PerInstanceData);
	FNiagaraPassedInstanceDataForRT* DataToPass = static_cast<FNiagaraPassedInstanceDataForRT*>(DataForRenderThread);

	DataToPass->bIsGpuUniformlyDistributedSampling = InstanceData->bIsGpuUniformlyDistributedSampling;
	DataToPass->DeltaSeconds = InstanceData->DeltaSeconds;
	DataToPass->Transform = InstanceData->Transform;
	DataToPass->PrevTransform = InstanceData->PrevTransform;
}

FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceStaticMesh::ConstructComputeParameters() const 
{
	return new FNiagaraDataInterfaceParametersCS_StaticMesh();
}

//////////////////////////////////////////////////////////////////////////

bool FDynamicVertexColorFilterData::Init(FNDIStaticMesh_InstanceData* Owner)
{
	TrianglesSortedByVertexColor.Empty();
	VertexColorToTriangleStart.AddDefaulted(256);
	check(Owner->Mesh);

	FStaticMeshLODResources& Res = Owner->Mesh->RenderData->LODResources[0];

	if (Res.VertexBuffers.ColorVertexBuffer.GetNumVertices() == 0)
	{
		UE_LOG(LogNiagara, Log, TEXT("Cannot initialize vertex color filter data for a mesh with no color data - %s"), *Owner->Mesh->GetFullName());
		return false;
	}

	// Go over all triangles for each possible vertex color and add it to that bucket
	for (int32 i = 0; i < VertexColorToTriangleStart.Num(); i++)
	{
		uint32 MinVertexColorRed = i;
		uint32 MaxVertexColorRed = i + 1;
		VertexColorToTriangleStart[i] = TrianglesSortedByVertexColor.Num();

		FIndexArrayView IndexView = Res.IndexBuffer.GetArrayView();
		for (int32 j = 0; j < Owner->GetValidSections().Num(); j++)
		{
			int32 SectionIdx = Owner->GetValidSections()[j];
			int32 TriStartIdx = Res.Sections[SectionIdx].FirstIndex;
			for (uint32 TriIdx = 0; TriIdx < Res.Sections[SectionIdx].NumTriangles; TriIdx++)
			{
				uint32 V0Idx = IndexView[TriStartIdx + TriIdx * 3 + 0];
				uint32 V1Idx = IndexView[TriStartIdx + TriIdx * 3 + 1];
				uint32 V2Idx = IndexView[TriStartIdx + TriIdx * 3 + 2];

				uint8 MaxR = FMath::Max<uint8>(Res.VertexBuffers.ColorVertexBuffer.VertexColor(V0Idx).R,
					FMath::Max<uint8>(Res.VertexBuffers.ColorVertexBuffer.VertexColor(V1Idx).R,
						Res.VertexBuffers.ColorVertexBuffer.VertexColor(V2Idx).R));
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
	check(Instance->Mesh);

	TSharedPtr<FDynamicVertexColorFilterData> Ret = nullptr;

	uint32 FilterDataHash = GetTypeHash(Instance->Mesh);
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

