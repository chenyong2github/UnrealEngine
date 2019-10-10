// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfacePhysicsAsset.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalMeshTypes.h"
#include "AnimationRuntime.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "ShaderParameterUtils.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfacePhysicsAsset"
DEFINE_LOG_CATEGORY_STATIC(LogPhysicsAsset, Log, All);

//------------------------------------------------------------------------------------------------------------

static const FName GetNumBoxesName(TEXT("GetNumBoxes"));
static const FName GetNumSpheresName(TEXT("GetNumSpheres"));
static const FName GetNumCapsulesName(TEXT("GetNumCapsules"));

//------------------------------------------------------------------------------------------------------------

static const FName GetClosestPointName(TEXT("GetClosestPoint"));

//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfacePhysicsAsset::ElementOffsetsName(TEXT("ElementOffsets_"));

const FString UNiagaraDataInterfacePhysicsAsset::CurrentTransformBufferName(TEXT("CurrentTransformBuffer_"));
const FString UNiagaraDataInterfacePhysicsAsset::PreviousTransformBufferName(TEXT("PreviousTransformBuffer_"));
const FString UNiagaraDataInterfacePhysicsAsset::InverseTransformBufferName(TEXT("InverseTransformBuffer_"));
const FString UNiagaraDataInterfacePhysicsAsset::ElementExtentBufferName(TEXT("ElementExtentBuffer_"));

//------------------------------------------------------------------------------------------------------------

struct FNDIPhysicsAssetParametersName
{
	FNDIPhysicsAssetParametersName(const FString& Suffix)
	{
		ElementOffsetsName = UNiagaraDataInterfacePhysicsAsset::ElementOffsetsName + Suffix;

		CurrentTransformBufferName = UNiagaraDataInterfacePhysicsAsset::CurrentTransformBufferName + Suffix;
		PreviousTransformBufferName = UNiagaraDataInterfacePhysicsAsset::PreviousTransformBufferName + Suffix;
		InverseTransformBufferName = UNiagaraDataInterfacePhysicsAsset::InverseTransformBufferName + Suffix;
		ElementExtentBufferName = UNiagaraDataInterfacePhysicsAsset::ElementExtentBufferName + Suffix;
	}

	FString ElementOffsetsName;
	
	FString CurrentTransformBufferName;
	FString PreviousTransformBufferName;
	FString InverseTransformBufferName;
	FString ElementExtentBufferName;
};

//------------------------------------------------------------------------------------------------------------

template<typename BufferType, typename DataType, int ElementSize, EPixelFormat PixelFormat, bool InitBuffer>
void CreateInternalBuffer(const uint32 ElementCount, const TArray<DataType>& InputData, FRWBuffer& OutputBuffer)
{
	if (ElementCount > 0)
	{
		const uint32 BufferCount = ElementCount * ElementSize;
		const uint32 BufferBytes = sizeof(BufferType)*BufferCount;

		if (InitBuffer)
		{
			OutputBuffer.Initialize(sizeof(BufferType), BufferCount, PixelFormat, BUF_Static);
		}
		void* OutputData = RHILockVertexBuffer(OutputBuffer.Buffer, 0, BufferBytes, RLM_WriteOnly);

		FMemory::Memcpy(OutputData, InputData.GetData(), BufferBytes);
		RHIUnlockVertexBuffer(OutputBuffer.Buffer);
	}
}

void FillCurrentTransforms(const FTransform& ElementTransform, uint32& ElementCount,
	TArray<FVector4>& OutCurrentTransform, TArray<FVector4>& OutInverseTransform)
{
	const uint32 ElementOffset = 3 * ElementCount;
	const FMatrix ElementMatrix = ElementTransform.ToMatrixWithScale();
	const FMatrix ElementInverse = ElementMatrix.Inverse();

	ElementMatrix.To3x4MatrixTranspose(&OutCurrentTransform[ElementOffset].X);
	ElementInverse.To3x4MatrixTranspose(&OutInverseTransform[ElementOffset].X);
	++ElementCount;
}

void CreateInternalArrays(const TWeakObjectPtr<UPhysicsAsset> PhysicsAsset, const TWeakObjectPtr<USkeletalMeshComponent> SkeletalMesh,
	FNDIPhysicsAssetArrays* OutAssetArrays, const FTransform& WorldTransform)
{
	if (OutAssetArrays != nullptr)
	{
		OutAssetArrays->ElementOffsets.BoxOffset = 0;
		OutAssetArrays->ElementOffsets.SphereOffset = 0;
		OutAssetArrays->ElementOffsets.CapsuleOffset = 0;
		OutAssetArrays->ElementOffsets.NumElements = 0;
		if (PhysicsAsset != nullptr && OutAssetArrays != nullptr)
		{
			const FReferenceSkeleton* RefSkeleton = &PhysicsAsset->GetPreviewMesh()->RefSkeleton;
			if (RefSkeleton != nullptr)
			{
				TArray<FTransform> BoneTransforms;
				if (SkeletalMesh != nullptr)
				{
					BoneTransforms = SkeletalMesh->GetComponentSpaceTransforms();
				}
				else
				{
					FAnimationRuntime::FillUpComponentSpaceTransforms(*RefSkeleton, RefSkeleton->GetRefBonePose(), BoneTransforms);
				}
				const bool bHasMasterPoseComponent = (SkeletalMesh != nullptr) && SkeletalMesh->MasterPoseComponent.IsValid();

				uint32 NumBoxes = 0;
				uint32 NumSpheres = 0;
				uint32 NumCapsules = 0;
				for (const UBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
				{
					const FName BoneName = BodySetup->BoneName;
					const int32 BoneIndex = RefSkeleton->FindBoneIndex(BoneName);
					if (BoneIndex != INDEX_NONE)
					{
						NumBoxes += BodySetup->AggGeom.BoxElems.Num();
						NumSpheres += BodySetup->AggGeom.SphereElems.Num();
						NumCapsules += BodySetup->AggGeom.SphylElems.Num();
					}
				}

				OutAssetArrays->ElementOffsets.BoxOffset = 0;
				OutAssetArrays->ElementOffsets.SphereOffset = OutAssetArrays->ElementOffsets.BoxOffset + NumBoxes;
				OutAssetArrays->ElementOffsets.CapsuleOffset = OutAssetArrays->ElementOffsets.SphereOffset + NumSpheres;
				OutAssetArrays->ElementOffsets.NumElements = OutAssetArrays->ElementOffsets.CapsuleOffset + NumCapsules;

				const uint32 NumTransforms = OutAssetArrays->ElementOffsets.NumElements * 3;
				const uint32 NumExtents = OutAssetArrays->ElementOffsets.NumElements;

				OutAssetArrays->CurrentTransform.SetNum(NumTransforms);
				OutAssetArrays->InverseTransform.SetNum(NumTransforms);
				OutAssetArrays->PreviousTransform.SetNum(NumTransforms);
				OutAssetArrays->ElementExtent.SetNum(NumExtents);

				uint32 ElementCount = 0;
				for (const UBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
				{
					const FName BoneName = BodySetup->BoneName;
					const int32 BoneIndex = RefSkeleton->FindBoneIndex(BoneName);
					if (BoneIndex != INDEX_NONE)
					{
						const FTransform BoneTransform = bHasMasterPoseComponent ? SkeletalMesh->GetBoneTransform(BoneIndex) : BoneTransforms[BoneIndex] * WorldTransform;

						for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
						{
							const FTransform ElementTransform = FTransform(BoxElem.Rotation, BoxElem.Center) * BoneTransform;
							OutAssetArrays->ElementExtent[ElementCount] = FVector4(BoxElem.X, BoxElem.Y, BoxElem.Z, 0);
							FillCurrentTransforms(ElementTransform, ElementCount, OutAssetArrays->CurrentTransform, OutAssetArrays->InverseTransform);
						}

						for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
						{
							const FTransform ElementTransform = FTransform(SphereElem.Center) * BoneTransform;
							OutAssetArrays->ElementExtent[ElementCount] = FVector4(SphereElem.Radius, 0, 0, 0);
							FillCurrentTransforms(ElementTransform, ElementCount, OutAssetArrays->CurrentTransform, OutAssetArrays->InverseTransform);
						}

						for (const FKSphylElem& CapsuleElem : BodySetup->AggGeom.SphylElems)
						{
							const FTransform ElementTransform = FTransform(CapsuleElem.Rotation, CapsuleElem.Center) * BoneTransform;
							OutAssetArrays->ElementExtent[ElementCount] = FVector4(CapsuleElem.Radius, CapsuleElem.Length, 0, 0);
							FillCurrentTransforms(ElementTransform, ElementCount, OutAssetArrays->CurrentTransform, OutAssetArrays->InverseTransform);
						}
					}
				}
				OutAssetArrays->PreviousTransform = OutAssetArrays->CurrentTransform;
			}
		}
	}
}

void UpdateInternalArrays(const TWeakObjectPtr<UPhysicsAsset> PhysicsAsset, const TWeakObjectPtr<USkeletalMeshComponent> SkeletalMesh,
	FNDIPhysicsAssetArrays* OutAssetArrays, const FTransform& WorldTransform)
{
	if (PhysicsAsset != nullptr && OutAssetArrays != nullptr)
	{
		const FReferenceSkeleton* RefSkeleton = &PhysicsAsset->GetPreviewMesh()->RefSkeleton;
		if (RefSkeleton != nullptr)
		{
			TArray<FTransform> BoneTransforms;
			if (SkeletalMesh != nullptr)
			{
				BoneTransforms = SkeletalMesh->GetComponentSpaceTransforms();
			}
			else
			{
				FAnimationRuntime::FillUpComponentSpaceTransforms(*RefSkeleton, RefSkeleton->GetRefBonePose(), BoneTransforms);
			}
			const bool bHasMasterPoseComponent = (SkeletalMesh != nullptr) && SkeletalMesh->MasterPoseComponent.IsValid();

			OutAssetArrays->PreviousTransform = OutAssetArrays->CurrentTransform;

			uint32 ElementCount = 0;
			for (const UBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
			{
				const FName BoneName = BodySetup->BoneName;
				const int32 BoneIndex = RefSkeleton->FindBoneIndex(BoneName);
				if (BoneIndex != INDEX_NONE)
				{
					const FTransform BoneTransform = bHasMasterPoseComponent ? SkeletalMesh->GetBoneTransform(BoneIndex) : BoneTransforms[BoneIndex] * WorldTransform;

					for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
					{
						const FTransform ElementTransform = FTransform(BoxElem.Rotation, BoxElem.Center) * BoneTransform;
						FillCurrentTransforms(ElementTransform, ElementCount, OutAssetArrays->CurrentTransform, OutAssetArrays->InverseTransform);
					}

					for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
					{
						const FTransform ElementTransform = FTransform(SphereElem.Center) * BoneTransform;
						FillCurrentTransforms(ElementTransform, ElementCount, OutAssetArrays->CurrentTransform, OutAssetArrays->InverseTransform);
					}

					for (const FKSphylElem& CapsuleElem : BodySetup->AggGeom.SphylElems)
					{
						const FTransform ElementTransform = FTransform(CapsuleElem.Rotation, CapsuleElem.Center) * BoneTransform;
						FillCurrentTransforms(ElementTransform, ElementCount, OutAssetArrays->CurrentTransform, OutAssetArrays->InverseTransform);
					}
				}
			}
		}
	}
}
//------------------------------------------------------------------------------------------------------------


bool FNDIPhysicsAssetBuffer::IsValid() const
{
	return (PhysicsAsset.IsValid() && PhysicsAsset.Get() != nullptr) && (AssetArrays.IsValid() && AssetArrays.Get() != nullptr);
}

void FNDIPhysicsAssetBuffer::SetupArrays(const TWeakObjectPtr<UPhysicsAsset> InPhysicsAsset, const TWeakObjectPtr<USkeletalMeshComponent> InSkeletalMesh, const FTransform& InWorldTransform)
{
	PhysicsAsset = InPhysicsAsset;
	SkeletalMesh = InSkeletalMesh;
	WorldTransform = InWorldTransform;

	AssetArrays = MakeUnique<FNDIPhysicsAssetArrays>();

	if (IsValid())
	{
		CreateInternalArrays(PhysicsAsset, SkeletalMesh, AssetArrays.Get(), WorldTransform);
	}
}

void FNDIPhysicsAssetBuffer::UpdateBuffers()
{
	if (IsValid())
	{
		UpdateInternalArrays(PhysicsAsset, SkeletalMesh, AssetArrays.Get(), WorldTransform);

		FNDIPhysicsAssetBuffer* ThisBuffer = this;
		ENQUEUE_RENDER_COMMAND(UpdatePhysicsAsset)(
			[ThisBuffer](FRHICommandListImmediate& RHICmdList) mutable
		{
			CreateInternalBuffer<FVector4, FVector4, 1, EPixelFormat::PF_A32B32G32R32F, false>(ThisBuffer->AssetArrays->CurrentTransform.Num(), ThisBuffer->AssetArrays->CurrentTransform, ThisBuffer->CurrentTransformBuffer);
			CreateInternalBuffer<FVector4, FVector4, 1, EPixelFormat::PF_A32B32G32R32F, false>(ThisBuffer->AssetArrays->PreviousTransform.Num(), ThisBuffer->AssetArrays->PreviousTransform, ThisBuffer->PreviousTransformBuffer);
			CreateInternalBuffer<FVector4, FVector4, 1, EPixelFormat::PF_A32B32G32R32F, false>(ThisBuffer->AssetArrays->InverseTransform.Num(), ThisBuffer->AssetArrays->InverseTransform, ThisBuffer->InverseTransformBuffer);
		}
		);
	}
}

void FNDIPhysicsAssetBuffer::InitRHI()
{
	if (IsValid())
	{
		CreateInternalBuffer<FVector4, FVector4, 1, EPixelFormat::PF_A32B32G32R32F, true>(AssetArrays->CurrentTransform.Num(), AssetArrays->CurrentTransform, CurrentTransformBuffer);
		CreateInternalBuffer<FVector4, FVector4, 1, EPixelFormat::PF_A32B32G32R32F, true>(AssetArrays->PreviousTransform.Num(), AssetArrays->PreviousTransform, PreviousTransformBuffer);
		CreateInternalBuffer<FVector4, FVector4, 1, EPixelFormat::PF_A32B32G32R32F, true>(AssetArrays->InverseTransform.Num(), AssetArrays->InverseTransform, InverseTransformBuffer);
		CreateInternalBuffer<FVector4, FVector4, 1, EPixelFormat::PF_A32B32G32R32F, true>(AssetArrays->ElementExtent.Num(), AssetArrays->ElementExtent, ElementExtentBuffer);

		UE_LOG(LogPhysicsAsset, Warning, TEXT("Num Capsules = %d | Num Spheres = %d | Num Boxes = %d"), AssetArrays->ElementOffsets.NumElements - AssetArrays->ElementOffsets.CapsuleOffset,
			AssetArrays->ElementOffsets.CapsuleOffset - AssetArrays->ElementOffsets.SphereOffset, AssetArrays->ElementOffsets.SphereOffset - AssetArrays->ElementOffsets.BoxOffset);
	}
}

void FNDIPhysicsAssetBuffer::ReleaseRHI()
{
	CurrentTransformBuffer.Release();
	PreviousTransformBuffer.Release();
	InverseTransformBuffer.Release();
	ElementExtentBuffer.Release();
}

//------------------------------------------------------------------------------------------------------------

struct FNDIPhysicsAssetParametersCS : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		FNDIPhysicsAssetParametersName ParamNames(ParamRef.ParameterInfo.DataInterfaceHLSLSymbol);

		ElementOffsets.Bind(ParameterMap, *ParamNames.ElementOffsetsName);

		CurrentTransformBuffer.Bind(ParameterMap, *ParamNames.CurrentTransformBufferName);
		PreviousTransformBuffer.Bind(ParameterMap, *ParamNames.PreviousTransformBufferName);
		InverseTransformBuffer.Bind(ParameterMap, *ParamNames.InverseTransformBufferName);
		ElementExtentBuffer.Bind(ParameterMap, *ParamNames.ElementExtentBufferName);

		if (!CurrentTransformBuffer.IsBound())
		{
			UE_LOG(LogPhysicsAsset, Warning, TEXT("Binding failed for FNDIPhysicsAssetParametersCS %s. Was it optimized out?"), *ParamNames.CurrentTransformBufferName)
		}
		if (!PreviousTransformBuffer.IsBound())
		{
			UE_LOG(LogPhysicsAsset, Warning, TEXT("Binding failed for FNDIPhysicsAssetParametersCS %s. Was it optimized out?"), *ParamNames.PreviousTransformBufferName)
		}
		if (!InverseTransformBuffer.IsBound())
		{
			UE_LOG(LogPhysicsAsset, Warning, TEXT("Binding failed for FNDIPhysicsAssetParametersCS %s. Was it optimized out?"), *ParamNames.InverseTransformBufferName)
		}
		if (!ElementExtentBuffer.IsBound())
		{
			UE_LOG(LogPhysicsAsset, Warning, TEXT("Binding failed for FNDIPhysicsAssetParametersCS %s. Was it optimized out?"), *ParamNames.ElementExtentBufferName)
		}
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Ar << ElementOffsets;
		Ar << CurrentTransformBuffer;
		Ar << PreviousTransformBuffer;
		Ar << InverseTransformBuffer;
		Ar << ElementExtentBuffer;
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();

		FNDIPhysicsAssetProxy* InterfaceProxy =
			static_cast<FNDIPhysicsAssetProxy*>(Context.DataInterface);
		FNDIPhysicsAssetData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstance);
		ensure(ProxyData);

		if (ProxyData != nullptr)
		{
			FNDIPhysicsAssetBuffer* AssetBuffer = ProxyData->AssetBuffer;
			SetSRVParameter(RHICmdList, ComputeShaderRHI, CurrentTransformBuffer, AssetBuffer->CurrentTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PreviousTransformBuffer, AssetBuffer->PreviousTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, InverseTransformBuffer, AssetBuffer->InverseTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ElementExtentBuffer, AssetBuffer->ElementExtentBuffer.SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, ElementOffsets, AssetBuffer->AssetArrays->ElementOffsets);
			
		}
		else
		{
			SetSRVParameter(RHICmdList, ComputeShaderRHI, CurrentTransformBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PreviousTransformBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, InverseTransformBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ElementExtentBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);

			static const FElementOffset DummyOffsets(0,0,0,0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ElementOffsets, DummyOffsets);
		}
	}

	virtual void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
	}

private:

	FShaderParameter ElementOffsets;

	FShaderResourceParameter CurrentTransformBuffer;
	FShaderResourceParameter PreviousTransformBuffer;
	FShaderResourceParameter InverseTransformBuffer;
	FShaderResourceParameter ElementExtentBuffer;
};

//------------------------------------------------------------------------------------------------------------

void FNDIPhysicsAssetProxy::DeferredDestroy()
{
	for (const FNiagaraSystemInstanceID& Sys : DeferredDestroyList)
	{
		SystemInstancesToProxyData.Remove(Sys);
	}

	DeferredDestroyList.Empty();
}

void FNDIPhysicsAssetProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FNDIPhysicsAssetData* SourceData = static_cast<FNDIPhysicsAssetData*>(PerInstanceData);
	FNDIPhysicsAssetData* TargetData = SystemInstancesToProxyData.Find(Instance);

	ensure(TargetData);
	if (TargetData)
	{
		TargetData->AssetBuffer = SourceData->AssetBuffer;
	}
	else
	{
		UE_LOG(LogPhysicsAsset, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %d"), Instance);
	}
}

void FNDIPhysicsAssetProxy::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance, FNDIPhysicsAssetBuffer* AssetBuffer)
{
	check(IsInRenderingThread());

	FNDIPhysicsAssetData* TargetData = SystemInstancesToProxyData.Find(SystemInstance);
	if (TargetData != nullptr)
	{
		DeferredDestroyList.Remove(SystemInstance);
	}
	else
	{
		TargetData = &SystemInstancesToProxyData.Add(SystemInstance);
	}
	TargetData->AssetBuffer = AssetBuffer;
}

void FNDIPhysicsAssetProxy::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	DeferredDestroyList.Add(SystemInstance);
	Batcher->EnqueueDeferredDeletesForDI_RenderThread(this->AsShared());
}

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfacePhysicsAsset::UNiagaraDataInterfacePhysicsAsset(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultSource(nullptr)
	, SourceActor(nullptr)
	, SourceComponent(nullptr)
	, PhysicsAsset(nullptr)
{
	Proxy = MakeShared<FNDIPhysicsAssetProxy, ESPMode::ThreadSafe>();
}

bool UNiagaraDataInterfacePhysicsAsset::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIPhysicsAssetData* InstanceData = new (PerInstanceData) FNDIPhysicsAssetData();

	SourceComponent = nullptr;
	if (SourceActor)
	{
		ASkeletalMeshActor* SkeletalMeshActor = Cast<ASkeletalMeshActor>(SourceActor);
		if (SkeletalMeshActor != nullptr)
		{
			SourceComponent = SkeletalMeshActor->GetSkeletalMeshComponent();
		}
		else
		{
			SourceComponent = SourceActor->FindComponentByClass<USkeletalMeshComponent>();
		}
	}
	else
	{
		if (UNiagaraComponent* SimComp = SystemInstance->GetComponent())
		{
			if (USkeletalMeshComponent* ParentComp = Cast<USkeletalMeshComponent>(SimComp->GetAttachParent()))
			{
				SourceComponent = ParentComp;
			}
			else if (USkeletalMeshComponent* OuterComp = SimComp->GetTypedOuter<USkeletalMeshComponent>())
			{
				SourceComponent = OuterComp;
			}
			else if (AActor* Owner = SimComp->GetAttachmentRootActor())
			{
				for (UActorComponent* ActorComp : Owner->GetComponents())
				{
					USkeletalMeshComponent* SourceComp = Cast<USkeletalMeshComponent>(ActorComp);
					if (SourceComp && SourceComp->SkeletalMesh)
					{
						SourceComponent = SourceComp;
						break;
					}
				}
			}
		}
	}

	if (SourceComponent != nullptr)
	{
		UE_LOG(LogPhysicsAsset, Log, TEXT("Found Skeletal Mesh %d %d %d"), SourceComponent.Get(), SourceComponent->GetPhysicsAsset(), DefaultSource);

	}

	PhysicsAsset = (SourceComponent != nullptr) ? SourceComponent->GetPhysicsAsset() : ( DefaultSource != nullptr ) ? DefaultSource : nullptr;

	if (PhysicsAsset == nullptr )
	{
		UE_LOG(LogPhysicsAsset, Log, TEXT("Physics Asset data interface has no valid asset. Failed InitPerInstanceData - %s %d %d"), *GetFullName(), PhysicsAsset.Get(), DefaultSource);
		return false;
	}
	else
	{
		const FTransform WorldTransform = (SourceComponent != nullptr) ? SourceComponent->GetComponentTransform() : SystemInstance->GetComponent()->GetComponentTransform();
		FNDIPhysicsAssetBuffer* AssetBuffer = new FNDIPhysicsAssetBuffer;
		AssetBuffer->SetupArrays(PhysicsAsset, SourceComponent, WorldTransform);

		// Push instance data to RT
		{
			InstanceData->AssetBuffer = AssetBuffer;

			FNDIPhysicsAssetProxy* ThisProxy = GetProxyAs<FNDIPhysicsAssetProxy>();
			ENQUEUE_RENDER_COMMAND(FNiagaraDIPushInitialInstanceDataToRT) (
				[ThisProxy, InstanceID = SystemInstance->GetId(), AssetBuffer](FRHICommandListImmediate& CmdList)
			{
				AssetBuffer->InitResource();
				ThisProxy->InitializePerInstanceData(InstanceID, AssetBuffer);
			}
			);
		}
	}
	return true;
}

void UNiagaraDataInterfacePhysicsAsset::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIPhysicsAssetData* InstanceData = static_cast<FNDIPhysicsAssetData*>(PerInstanceData);
	InstanceData->~FNDIPhysicsAssetData();

	if (InstanceData->AssetBuffer)
	{
		FNDIPhysicsAssetProxy* ThisProxy = GetProxyAs<FNDIPhysicsAssetProxy>();
		FNDIPhysicsAssetBuffer* InBuffer = InstanceData->AssetBuffer;
		ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
			[ThisProxy, InBuffer,  InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
			InBuffer->ReleaseResource();
			ThisProxy->DestroyPerInstanceData(Batcher, InstanceID);
			delete InBuffer;
		}
		);
		InstanceData->AssetBuffer = nullptr;
	}
}

bool UNiagaraDataInterfacePhysicsAsset::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIPhysicsAssetData* InstanceData = static_cast<FNDIPhysicsAssetData*>(PerInstanceData);
	if (InstanceData->AssetBuffer)
	{
		InstanceData->AssetBuffer->WorldTransform = (InstanceData->AssetBuffer->SkeletalMesh != nullptr) ? InstanceData->AssetBuffer->SkeletalMesh->GetComponentTransform() :
			SystemInstance->GetComponent()->GetComponentTransform();
		InstanceData->AssetBuffer->UpdateBuffers();
	}
	return false;
}

bool UNiagaraDataInterfacePhysicsAsset::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfacePhysicsAsset* OtherTyped = CastChecked<UNiagaraDataInterfacePhysicsAsset>(Destination);
	OtherTyped->PhysicsAsset = PhysicsAsset;
	OtherTyped->SourceActor = SourceActor;
	OtherTyped->SourceComponent = SourceComponent;
	OtherTyped->DefaultSource = DefaultSource;

	return true;
}

bool UNiagaraDataInterfacePhysicsAsset::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfacePhysicsAsset* OtherTyped = CastChecked<const UNiagaraDataInterfacePhysicsAsset>(Other);

	return  (OtherTyped->PhysicsAsset == PhysicsAsset) && (OtherTyped->SourceActor == SourceActor) && (OtherTyped->SourceComponent == SourceComponent) && (OtherTyped->DefaultSource == DefaultSource);
}

void UNiagaraDataInterfacePhysicsAsset::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

void UNiagaraDataInterfacePhysicsAsset::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumBoxesName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Boxes")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumSpheresName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Spheres")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumCapsulesName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Capsules")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestPointName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetNumBoxes);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetNumSpheres);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetNumCapsules);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetClosestPoint);

void UNiagaraDataInterfacePhysicsAsset::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == GetNumBoxesName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetNumBoxes)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetNumSpheresName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetNumSpheres)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetNumCapsulesName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetNumCapsules)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetClosestPointName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 10);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetClosestPoint)::Bind(this, OutFunc);
	}
}

void UNiagaraDataInterfacePhysicsAsset::GetNumBoxes(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfacePhysicsAsset::GetNumSpheres(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfacePhysicsAsset::GetNumCapsules(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfacePhysicsAsset::GetClosestPoint(FVectorVMContext& Context)
{
}

bool UNiagaraDataInterfacePhysicsAsset::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FNDIPhysicsAssetParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), InstanceFunctionName},
		{TEXT("ElementOffsetsName"), ParamNames.ElementOffsetsName},
		{TEXT("CurrentTransformBufferName"), ParamNames.CurrentTransformBufferName},
		{TEXT("PreviousTransformBufferName"), ParamNames.PreviousTransformBufferName},
		{TEXT("InverseTransformBufferName"), ParamNames.InverseTransformBufferName},
		{TEXT("ElementExtentBufferName"), ParamNames.ElementExtentBufferName},
		{TEXT("PhysicsAssetContextName"), TEXT("DIPHYSICSASSET_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (DefinitionFunctionName == GetNumBoxesName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int OutNumBoxes)
		{
			{PhysicsAssetContextName}
			OutNumBoxes = DIPhysicsAsset_GetNumBoxes(DIContext);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetNumCapsulesName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int OutNumCapsules)
		{
			{PhysicsAssetContextName}
			OutNumCapsules = DIPhysicsAsset_GetNumCapsules(DIContext);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetNumSpheresName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int OutNumSpheres)
		{
			{PhysicsAssetContextName}
			OutNumSpheres = DIPhysicsAsset_GetNumSpheres(DIContext);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetClosestPointName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in float DeltaTime, out float3 OutClosestPosition, 
							out float3 OutClosestNormal, out float3 OutClosestVelocity, out float OutClosestDistance)
		{
			{PhysicsAssetContextName} DIPhysicsAsset_GetClosestPoint(DIContext,WorldPosition,DeltaTime,
				OutClosestPosition,OutClosestNormal,OutClosestVelocity,OutClosestDistance);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}

	OutHLSL += TEXT("\n");
	return false;
}

void UNiagaraDataInterfacePhysicsAsset::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraDataInterfacePhysicsAsset.ush\"\n");
}

void UNiagaraDataInterfacePhysicsAsset::GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIPHYSICSASSET_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}

void UNiagaraDataInterfacePhysicsAsset::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIPhysicsAssetData* GameThreadData = static_cast<FNDIPhysicsAssetData*>(PerInstanceData);
	FNDIPhysicsAssetData* RenderThreadData = static_cast<FNDIPhysicsAssetData*>(DataForRenderThread);

	if (GameThreadData != nullptr && RenderThreadData != nullptr)
	{
		RenderThreadData->AssetBuffer = GameThreadData->AssetBuffer;
	}
	check(Proxy);
}

FNiagaraDataInterfaceParametersCS*
UNiagaraDataInterfacePhysicsAsset::ConstructComputeParameters() const
{
	return new FNDIPhysicsAssetParametersCS();
}


#undef LOCTEXT_NAMESPACE