// Copyright Epic Games, Inc. All Rights Reserved.

#include "Niagara/NiagaraDataInterfacePhysicsAsset.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "SkeletalMeshTypes.h"
#include "AnimationRuntime.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "GroomComponent.h"
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

static const FName GetClosestElementName(TEXT("GetClosestElement"));
static const FName GetElementPointName(TEXT("GetElementPoint"));
static const FName GetElementDistanceName(TEXT("GetElementDistance"));
static const FName GetClosestPointName(TEXT("GetClosestPoint"));
static const FName GetClosestDistanceName(TEXT("GetClosestDistance"));
static const FName GetTexturePointName(TEXT("GetTexturePoint"));
static const FName GetProjectionPointName(TEXT("GetProjectionPoint"));

//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfacePhysicsAsset::ElementOffsetsName(TEXT("ElementOffsets_"));

const FString UNiagaraDataInterfacePhysicsAsset::WorldTransformBufferName(TEXT("WorldTransformBuffer_"));
const FString UNiagaraDataInterfacePhysicsAsset::InverseTransformBufferName(TEXT("InverseTransformBuffer_"));
const FString UNiagaraDataInterfacePhysicsAsset::ElementExtentBufferName(TEXT("ElementExtentBuffer_"));
const FString UNiagaraDataInterfacePhysicsAsset::PhysicsTypeBufferName(TEXT("PhysicsTypeBuffer_"));

const FString UNiagaraDataInterfacePhysicsAsset::BoxOriginName(TEXT("BoxOrigin_"));
const FString UNiagaraDataInterfacePhysicsAsset::BoxExtentName(TEXT("BoxExtent_"));

//------------------------------------------------------------------------------------------------------------

struct FNDIPhysicsAssetParametersName
{
	FNDIPhysicsAssetParametersName(const FString& Suffix)
	{
		ElementOffsetsName = UNiagaraDataInterfacePhysicsAsset::ElementOffsetsName + Suffix;

		WorldTransformBufferName = UNiagaraDataInterfacePhysicsAsset::WorldTransformBufferName + Suffix;
		InverseTransformBufferName = UNiagaraDataInterfacePhysicsAsset::InverseTransformBufferName + Suffix;
		ElementExtentBufferName = UNiagaraDataInterfacePhysicsAsset::ElementExtentBufferName + Suffix;
		PhysicsTypeBufferName = UNiagaraDataInterfacePhysicsAsset::PhysicsTypeBufferName + Suffix;

		BoxOriginName = UNiagaraDataInterfacePhysicsAsset::BoxOriginName + Suffix;
		BoxExtentName = UNiagaraDataInterfacePhysicsAsset::BoxExtentName + Suffix;
	}

	FString ElementOffsetsName;

	FString WorldTransformBufferName;
	FString InverseTransformBufferName;
	FString ElementExtentBufferName;
	FString PhysicsTypeBufferName;

	FString BoxOriginName;
	FString BoxExtentName;
};

//------------------------------------------------------------------------------------------------------------

template<typename BufferType, EPixelFormat PixelFormat, uint32 ElementCount, uint32 BufferCount = 1>
void CreateInternalBuffer(FRWBuffer& OutputBuffer)
{
	if (ElementCount > 0)
	{
		OutputBuffer.Initialize(sizeof(BufferType), ElementCount * BufferCount, PixelFormat, BUF_Static);
	}
}

template<typename BufferType, EPixelFormat PixelFormat, uint32 ElementCount, uint32 BufferCount = 1>
void UpdateInternalBuffer(const TStaticArray<BufferType,ElementCount*BufferCount>& InputData, FRWBuffer& OutputBuffer)
{
	if (ElementCount > 0)
	{
		const uint32 BufferBytes = sizeof(BufferType) * ElementCount * BufferCount;

		void* OutputData = RHILockVertexBuffer(OutputBuffer.Buffer, 0, BufferBytes, RLM_WriteOnly);

		FMemory::Memcpy(OutputData, InputData.GetData(), BufferBytes);
		RHIUnlockVertexBuffer(OutputBuffer.Buffer);
	}
}

void FillCurrentTransforms(const FTransform& ElementTransform, uint32& ElementCount,
	TStaticArray<FVector4,PHYSICS_ASSET_MAX_TRANSFORMS>& OutCurrentTransform, TStaticArray<FVector4, PHYSICS_ASSET_MAX_TRANSFORMS>& OutCurrentInverse)
{
	const uint32 ElementOffset = 3 * ElementCount;
	const FMatrix ElementMatrix = ElementTransform.ToMatrixWithScale();
	const FMatrix ElementInverse = ElementMatrix.Inverse();

	ElementMatrix.To3x4MatrixTranspose(&OutCurrentTransform[ElementOffset].X);
	ElementInverse.To3x4MatrixTranspose(&OutCurrentInverse[ElementOffset].X);
	++ElementCount;
}

void GetNumPrimitives(const TArray<TWeakObjectPtr<UPhysicsAsset>>& PhysicsAssets, const TArray<TWeakObjectPtr<USkeletalMeshComponent>>& SkeletalMeshs, uint32& NumBoxes, uint32& NumSpheres, uint32& NumCapsules)
{
	NumBoxes = 0;
	NumSpheres = 0;
	NumCapsules = 0;

	for (int32 ComponentIndex = 0; ComponentIndex < SkeletalMeshs.Num(); ++ComponentIndex)
	{
		TWeakObjectPtr<UPhysicsAsset> PhysicsAsset = PhysicsAssets[ComponentIndex];
		if (PhysicsAsset.IsValid() && PhysicsAsset.Get() != nullptr)
		{
			USkeletalMesh* SkelMesh = (SkeletalMeshs[ComponentIndex].Get() && SkeletalMeshs[ComponentIndex]->SkeletalMesh) ? SkeletalMeshs[ComponentIndex]->SkeletalMesh : PhysicsAsset->GetPreviewMesh();
			if (!SkelMesh)
			{
				continue;
			}

			const FReferenceSkeleton& RefSkeleton = SkelMesh->GetRefSkeleton();
			if (RefSkeleton.GetNum() > 0)
			{
				for (const UBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
				{
					const FName BoneName = BodySetup->BoneName;
					const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
					if (BoneIndex != INDEX_NONE && BoneIndex < RefSkeleton.GetNum())
					{
						for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
						{
							if (CollisionEnabledHasPhysics(BoxElem.GetCollisionEnabled()))
							{
								NumBoxes += 1;
							}
						}
						for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
						{
							if (CollisionEnabledHasPhysics(SphereElem.GetCollisionEnabled()))
							{
								NumSpheres += 1;
							}
						}
						for (const FKSphylElem& CapsuleElem : BodySetup->AggGeom.SphylElems)
						{
							if (CollisionEnabledHasPhysics(CapsuleElem.GetCollisionEnabled()))
							{
								NumCapsules += 1;
							}
						}
					}
				}
			}
		}
	}
}

void CompactInternalArrays(FNDIPhysicsAssetArrays* OutAssetArrays)
{
	for (uint32 TransformIndex = 0; TransformIndex < PHYSICS_ASSET_MAX_TRANSFORMS; ++TransformIndex)
	{
		uint32 OffsetIndex = TransformIndex;
		OutAssetArrays->WorldTransform[OffsetIndex] = OutAssetArrays->CurrentTransform[TransformIndex];
		OutAssetArrays->InverseTransform[OffsetIndex] = OutAssetArrays->CurrentInverse[TransformIndex];

		OffsetIndex += PHYSICS_ASSET_MAX_TRANSFORMS;
		OutAssetArrays->WorldTransform[OffsetIndex] = OutAssetArrays->PreviousTransform[TransformIndex];
		OutAssetArrays->InverseTransform[OffsetIndex] = OutAssetArrays->PreviousInverse[TransformIndex];

		OffsetIndex += PHYSICS_ASSET_MAX_TRANSFORMS;
		OutAssetArrays->WorldTransform[OffsetIndex] = OutAssetArrays->RestTransform[TransformIndex];
		OutAssetArrays->InverseTransform[OffsetIndex] = OutAssetArrays->RestInverse[TransformIndex];
	}
}

void CreateInternalArrays(const TArray<TWeakObjectPtr<UPhysicsAsset>>& PhysicsAssets, const TArray<TWeakObjectPtr<USkeletalMeshComponent>>& SkeletalMeshs,
	FNDIPhysicsAssetArrays* OutAssetArrays, const FTransform& InWorldTransform)
{
	if (OutAssetArrays != nullptr)
	{
		OutAssetArrays->ElementOffsets.BoxOffset = 0;
		OutAssetArrays->ElementOffsets.SphereOffset = 0;
		OutAssetArrays->ElementOffsets.CapsuleOffset = 0;
		OutAssetArrays->ElementOffsets.NumElements = 0;

		uint32 NumBoxes = 0;
		uint32 NumSpheres = 0;
		uint32 NumCapsules = 0;

		GetNumPrimitives(PhysicsAssets, SkeletalMeshs, NumBoxes, NumSpheres, NumCapsules);
		
		if ((NumBoxes + NumSpheres + NumCapsules) < PHYSICS_ASSET_MAX_PRIMITIVES)
		{
			OutAssetArrays->ElementOffsets.BoxOffset = 0;
			OutAssetArrays->ElementOffsets.SphereOffset = OutAssetArrays->ElementOffsets.BoxOffset + NumBoxes;
			OutAssetArrays->ElementOffsets.CapsuleOffset = OutAssetArrays->ElementOffsets.SphereOffset + NumSpheres;
			OutAssetArrays->ElementOffsets.NumElements = OutAssetArrays->ElementOffsets.CapsuleOffset + NumCapsules;

			const uint32 NumTransforms = OutAssetArrays->ElementOffsets.NumElements * 3;
			const uint32 NumExtents = OutAssetArrays->ElementOffsets.NumElements;

			uint32 BoxCount = OutAssetArrays->ElementOffsets.BoxOffset;
			uint32 SphereCount = OutAssetArrays->ElementOffsets.SphereOffset;
			uint32 CapsuleCount = OutAssetArrays->ElementOffsets.CapsuleOffset;

			for (int32 ComponentIndex = 0; ComponentIndex < SkeletalMeshs.Num(); ++ComponentIndex)
			{
				TWeakObjectPtr<USkeletalMeshComponent> SkeletalMesh = SkeletalMeshs[ComponentIndex];
				const bool IsSkelMeshValid = SkeletalMesh.IsValid() && SkeletalMesh.Get() != nullptr;
				const FTransform WorldTransform = IsSkelMeshValid ? SkeletalMesh->GetComponentTransform() : InWorldTransform;

				TWeakObjectPtr<UPhysicsAsset> PhysicsAsset = PhysicsAssets[ComponentIndex];
				if (PhysicsAsset.IsValid() && PhysicsAsset.Get() != nullptr)
				{
					USkeletalMesh* SkelMesh = (SkeletalMeshs[ComponentIndex].Get() && SkeletalMeshs[ComponentIndex]->SkeletalMesh) ? SkeletalMeshs[ComponentIndex]->SkeletalMesh : PhysicsAsset->GetPreviewMesh();
					if (!SkelMesh)
					{
						continue;
					}
					const FReferenceSkeleton& RefSkeleton = SkelMesh->GetRefSkeleton();
					TArray<FTransform> RestTransforms;
					FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, RefSkeleton.GetRefBonePose(), RestTransforms);

					if (RefSkeleton.GetNum() > 0)
					{
						for (const UBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
						{
							const FName BoneName = BodySetup->BoneName;
							const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
							if (BoneIndex != INDEX_NONE && BoneIndex < RestTransforms.Num())
							{
								const FTransform RestTransform = RestTransforms[BoneIndex];
								const FTransform BoneTransform = IsSkelMeshValid ? SkeletalMesh->GetBoneTransform(BoneIndex) : RestTransform * WorldTransform;

								for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
								{
									if (CollisionEnabledHasPhysics(BoxElem.GetCollisionEnabled()))
									{
										const FTransform RestElement = FTransform(BoxElem.Rotation, BoxElem.Center) * RestTransform;
										FillCurrentTransforms(RestElement, BoxCount, OutAssetArrays->RestTransform, OutAssetArrays->RestInverse);
										--BoxCount;

										OutAssetArrays->PhysicsType[BoxCount] = (BoxElem.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);

										const FTransform ElementTransform = FTransform(BoxElem.Rotation, BoxElem.Center) * BoneTransform;
										OutAssetArrays->ElementExtent[BoxCount] = FVector4(BoxElem.X, BoxElem.Y, BoxElem.Z, 0);
										FillCurrentTransforms(ElementTransform, BoxCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
									}
								}

								for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
								{
									if (CollisionEnabledHasPhysics(SphereElem.GetCollisionEnabled()))
									{
										const FTransform RestElement = FTransform(SphereElem.Center) * RestTransform;
										FillCurrentTransforms(RestElement, SphereCount, OutAssetArrays->RestTransform, OutAssetArrays->RestInverse);
										--SphereCount;

										OutAssetArrays->PhysicsType[SphereCount] = (SphereElem.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);

										const FTransform ElementTransform = FTransform(SphereElem.Center) * BoneTransform;
										OutAssetArrays->ElementExtent[SphereCount] = FVector4(SphereElem.Radius, 0, 0, 0);
										FillCurrentTransforms(ElementTransform, SphereCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
									}
								}

								for (const FKSphylElem& CapsuleElem : BodySetup->AggGeom.SphylElems)
								{
									if (CollisionEnabledHasPhysics(CapsuleElem.GetCollisionEnabled()))
									{
										const FTransform RestElement = FTransform(CapsuleElem.Rotation, CapsuleElem.Center) * RestTransform;
										FillCurrentTransforms(RestElement, CapsuleCount, OutAssetArrays->RestTransform, OutAssetArrays->RestInverse);
										--CapsuleCount;

										OutAssetArrays->PhysicsType[CapsuleCount] = (CapsuleElem.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);

										const FTransform ElementTransform = FTransform(CapsuleElem.Rotation, CapsuleElem.Center) * BoneTransform;
										OutAssetArrays->ElementExtent[CapsuleCount] = FVector4(CapsuleElem.Radius, CapsuleElem.Length, 0, 0);
										FillCurrentTransforms(ElementTransform, CapsuleCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
									}
								}
							}
						}
					}
				}
			}
			OutAssetArrays->PreviousTransform = OutAssetArrays->CurrentTransform;
			OutAssetArrays->PreviousInverse = OutAssetArrays->CurrentInverse;

			CompactInternalArrays(OutAssetArrays);
		}
		else
		{
			UE_LOG(LogPhysicsAsset, Warning, TEXT("Number of hysics asset primitives is higher than the niagara %d limit"), PHYSICS_ASSET_MAX_PRIMITIVES);
		}
	}
}

void UpdateInternalArrays(const TArray<TWeakObjectPtr<UPhysicsAsset>>& PhysicsAssets, const TArray<TWeakObjectPtr<USkeletalMeshComponent>>& SkeletalMeshs,
	FNDIPhysicsAssetArrays* OutAssetArrays, const FTransform& InWorldTransform)
{
	if (OutAssetArrays != nullptr && OutAssetArrays->ElementOffsets.NumElements < PHYSICS_ASSET_MAX_PRIMITIVES)
	{
		uint32 NumBoxes = 0;
		uint32 NumSpheres = 0;
		uint32 NumCapsules = 0;

		GetNumPrimitives(PhysicsAssets, SkeletalMeshs, NumBoxes, NumSpheres, NumCapsules);

		if (((OutAssetArrays->ElementOffsets.SphereOffset - OutAssetArrays->ElementOffsets.BoxOffset) != NumBoxes) || 
			((OutAssetArrays->ElementOffsets.CapsuleOffset - OutAssetArrays->ElementOffsets.SphereOffset) != NumSpheres) ||
			((OutAssetArrays->ElementOffsets.NumElements - OutAssetArrays->ElementOffsets.CapsuleOffset) != NumCapsules))
		{
			CreateInternalArrays(PhysicsAssets, SkeletalMeshs, OutAssetArrays, InWorldTransform);
		}

		OutAssetArrays->PreviousTransform = OutAssetArrays->CurrentTransform;
		OutAssetArrays->PreviousInverse = OutAssetArrays->CurrentInverse;

		uint32 BoxCount = OutAssetArrays->ElementOffsets.BoxOffset;
		uint32 SphereCount = OutAssetArrays->ElementOffsets.SphereOffset;
		uint32 CapsuleCount = OutAssetArrays->ElementOffsets.CapsuleOffset;

		for (int32 ComponentIndex = 0; ComponentIndex < SkeletalMeshs.Num(); ++ComponentIndex)
		{
			TWeakObjectPtr<USkeletalMeshComponent> SkeletalMesh = SkeletalMeshs[ComponentIndex];
			const bool IsSkelMeshValid = SkeletalMesh.IsValid() && SkeletalMesh.Get() != nullptr;
			const FTransform WorldTransform = IsSkelMeshValid ? SkeletalMesh->GetComponentTransform() : InWorldTransform;

			TWeakObjectPtr<UPhysicsAsset> PhysicsAsset = PhysicsAssets[ComponentIndex];
			if (PhysicsAsset.IsValid() && PhysicsAsset.Get() != nullptr)
			{
				USkeletalMesh* SkelMesh = (SkeletalMeshs[ComponentIndex].Get() && SkeletalMeshs[ComponentIndex]->SkeletalMesh) ? SkeletalMeshs[ComponentIndex]->SkeletalMesh : PhysicsAsset->GetPreviewMesh();
				if (!SkelMesh)
				{
					continue;
				}
				const FReferenceSkeleton& RefSkeleton = SkelMesh->GetRefSkeleton();

				{
					TArray<FTransform> RestTransforms;
					FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, RefSkeleton.GetRefBonePose(), RestTransforms);
					
					for (const UBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
					{
						const FName BoneName = BodySetup->BoneName;
						const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
						if (BoneIndex != INDEX_NONE && BoneIndex < RestTransforms.Num())
						{
							const FTransform BoneTransform = IsSkelMeshValid ? SkeletalMesh->GetBoneTransform(BoneIndex) : RestTransforms[BoneIndex] * WorldTransform;

							for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
							{
								if (CollisionEnabledHasPhysics(BoxElem.GetCollisionEnabled()))
								{
									const FTransform ElementTransform = FTransform(BoxElem.Rotation, BoxElem.Center) * BoneTransform;
									FillCurrentTransforms(ElementTransform, BoxCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
								}
							}

							for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
							{
								if (CollisionEnabledHasPhysics(SphereElem.GetCollisionEnabled()))
								{
									const FTransform ElementTransform = FTransform(SphereElem.Center) * BoneTransform;
									FillCurrentTransforms(ElementTransform, SphereCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
								}
							}

							for (const FKSphylElem& CapsuleElem : BodySetup->AggGeom.SphylElems)
							{
								if (CollisionEnabledHasPhysics(CapsuleElem.GetCollisionEnabled()))
								{
									const FTransform ElementTransform = FTransform(CapsuleElem.Rotation, CapsuleElem.Center) * BoneTransform;
									FillCurrentTransforms(ElementTransform, CapsuleCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
								}
							}
						}
					}
				}
			}
		}
		CompactInternalArrays(OutAssetArrays);
	}
}

//------------------------------------------------------------------------------------------------------------

void FNDIPhysicsAssetBuffer::InitRHI()
{
	CreateInternalBuffer<FVector4, EPixelFormat::PF_A32B32G32R32F, PHYSICS_ASSET_MAX_TRANSFORMS, 3>(WorldTransformBuffer);
	CreateInternalBuffer<FVector4, EPixelFormat::PF_A32B32G32R32F, PHYSICS_ASSET_MAX_TRANSFORMS, 3>(InverseTransformBuffer);

	CreateInternalBuffer<FVector4, EPixelFormat::PF_A32B32G32R32F, PHYSICS_ASSET_MAX_PRIMITIVES>(ElementExtentBuffer);
	CreateInternalBuffer<uint32, EPixelFormat::PF_R32_UINT, PHYSICS_ASSET_MAX_PRIMITIVES>(PhysicsTypeBuffer);
}

void FNDIPhysicsAssetBuffer::ReleaseRHI()
{
	WorldTransformBuffer.Release();
	InverseTransformBuffer.Release();
	ElementExtentBuffer.Release();
	PhysicsTypeBuffer.Release();
}

//------------------------------------------------------------------------------------------------------------

ETickingGroup ComputeTickingGroup(const TArray<TWeakObjectPtr<class USkeletalMeshComponent>> SkeletalMeshes)
{
	ETickingGroup TickingGroup = NiagaraFirstTickGroup;
	for (int32 ComponentIndex = 0; ComponentIndex < SkeletalMeshes.Num(); ++ComponentIndex)
	{
		if (SkeletalMeshes[ComponentIndex].Get() != nullptr)
		{
			const USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(SkeletalMeshes[ComponentIndex].Get());

			const ETickingGroup ComponentTickGroup = FMath::Max(Component->PrimaryComponentTick.TickGroup, Component->PrimaryComponentTick.EndTickGroup);
			const ETickingGroup PhysicsTickGroup = Component->bBlendPhysics ? FMath::Max(ComponentTickGroup, TG_EndPhysics) : ComponentTickGroup;
			const ETickingGroup ClampedTickGroup = FMath::Clamp(ETickingGroup(PhysicsTickGroup + 1), NiagaraFirstTickGroup, NiagaraLastTickGroup);

			TickingGroup = FMath::Max(TickingGroup, ClampedTickGroup);
		}
	}
	return TickingGroup;
}

void FNDIPhysicsAssetData::Release()
{
	if (AssetBuffer)
	{
		BeginReleaseResource(AssetBuffer);
		ENQUEUE_RENDER_COMMAND(DeleteResource)(
			[ParamPointerToRelease = AssetBuffer](FRHICommandListImmediate& RHICmdList)
			{
				delete ParamPointerToRelease;
			});
		AssetBuffer = nullptr;
	}
}

void FNDIPhysicsAssetData::Init(UNiagaraDataInterfacePhysicsAsset* Interface, FNiagaraSystemInstance* SystemInstance)
{
	AssetBuffer = nullptr;

	if (Interface != nullptr && SystemInstance != nullptr)
	{
		Interface->ExtractSourceComponent(SystemInstance);
		TickingGroup = ComputeTickingGroup(Interface->SourceComponents);

		if(0 < Interface->PhysicsAssets.Num() && Interface->PhysicsAssets[0].IsValid() && Interface->PhysicsAssets[0].Get() != nullptr && 
			Interface->PhysicsAssets.Num() == Interface->SourceComponents.Num() )
		{
			CreateInternalArrays(Interface->PhysicsAssets, Interface->SourceComponents, &AssetArrays, SystemInstance->GetWorldTransform());
		}

		AssetBuffer = new FNDIPhysicsAssetBuffer();
		BeginInitResource(AssetBuffer);

		FVector BoxMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		FVector BoxMin = -BoxMax;

		FBox BoundingBox(BoxMin, BoxMax);
		for (int32 ComponentIndex = 0; ComponentIndex < Interface->PhysicsAssets.Num(); ++ComponentIndex)
		{
			TWeakObjectPtr<UPhysicsAsset> PhysicsAsset = Interface->PhysicsAssets[ComponentIndex];
			if (PhysicsAsset.IsValid() && PhysicsAsset.Get() != nullptr)
			{
				const bool HasPreviewMesh = PhysicsAsset != nullptr && PhysicsAsset->GetPreviewMesh() != nullptr;
				if (HasPreviewMesh)
				{
					BoundingBox += PhysicsAsset->GetPreviewMesh()->GetImportedBounds().GetBox();
				}
			}
		}
		BoxOrigin = 0.5 * (BoundingBox.Max + BoundingBox.Min);
		BoxExtent = (BoundingBox.Max - BoundingBox.Min);
	}
}

void FNDIPhysicsAssetData::Update(UNiagaraDataInterfacePhysicsAsset* Interface, FNiagaraSystemInstance* SystemInstance)
{
	if (Interface != nullptr && SystemInstance != nullptr)
	{
		Interface->ExtractSourceComponent(SystemInstance);
		TickingGroup = ComputeTickingGroup(Interface->SourceComponents);

		if (0 < Interface->PhysicsAssets.Num() && Interface->PhysicsAssets[0].IsValid() && Interface->PhysicsAssets[0].Get() != nullptr &&
			Interface->PhysicsAssets.Num() == Interface->SourceComponents.Num())
		{
			UpdateInternalArrays(Interface->PhysicsAssets, Interface->SourceComponents, &AssetArrays, SystemInstance->GetWorldTransform());
		}
	}
}

//------------------------------------------------------------------------------------------------------------

struct FNDIPhysicsAssetParametersCS : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNDIPhysicsAssetParametersCS, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FNDIPhysicsAssetParametersName ParamNames(*ParameterInfo.DataInterfaceHLSLSymbol);

		ElementOffsets.Bind(ParameterMap, *ParamNames.ElementOffsetsName);

		WorldTransformBuffer.Bind(ParameterMap, *ParamNames.WorldTransformBufferName);
		InverseTransformBuffer.Bind(ParameterMap, *ParamNames.InverseTransformBufferName);
		ElementExtentBuffer.Bind(ParameterMap, *ParamNames.ElementExtentBufferName);
		PhysicsTypeBuffer.Bind(ParameterMap, *ParamNames.PhysicsTypeBufferName);

		BoxOrigin.Bind(ParameterMap, *ParamNames.BoxOriginName);
		BoxExtent.Bind(ParameterMap, *ParamNames.BoxExtentName);

		
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		FNDIPhysicsAssetProxy* InterfaceProxy =
			static_cast<FNDIPhysicsAssetProxy*>(Context.DataInterface);
		FNDIPhysicsAssetData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstanceID);

		if (ProxyData != nullptr && ProxyData->AssetBuffer && ProxyData->AssetBuffer->IsInitialized())
		{
			FNDIPhysicsAssetBuffer* AssetBuffer = ProxyData->AssetBuffer;
			SetSRVParameter(RHICmdList, ComputeShaderRHI, WorldTransformBuffer, AssetBuffer->WorldTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, InverseTransformBuffer, AssetBuffer->InverseTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ElementExtentBuffer, AssetBuffer->ElementExtentBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PhysicsTypeBuffer, AssetBuffer->PhysicsTypeBuffer.SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, ElementOffsets, ProxyData->AssetArrays.ElementOffsets);
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoxOrigin, ProxyData->BoxOrigin);
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoxExtent, ProxyData->BoxExtent);
		}
		else
		{
			SetSRVParameter(RHICmdList, ComputeShaderRHI, WorldTransformBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, InverseTransformBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ElementExtentBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PhysicsTypeBuffer, FNiagaraRenderer::GetDummyIntBuffer());

			static const FElementOffset DummyOffsets(0, 0, 0, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ElementOffsets, DummyOffsets);
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoxOrigin, FVector(0, 0, 0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoxExtent, FVector(0, 0, 0));
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
	}

private:

	LAYOUT_FIELD(FShaderParameter, ElementOffsets);

	LAYOUT_FIELD(FShaderResourceParameter, WorldTransformBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, InverseTransformBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, ElementExtentBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, PhysicsTypeBuffer);

	LAYOUT_FIELD(FShaderParameter, BoxOrigin);
	LAYOUT_FIELD(FShaderParameter, BoxExtent);
};

IMPLEMENT_TYPE_LAYOUT(FNDIPhysicsAssetParametersCS);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfacePhysicsAsset, FNDIPhysicsAssetParametersCS);


//------------------------------------------------------------------------------------------------------------

void FNDIPhysicsAssetProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FNDIPhysicsAssetData* SourceData = static_cast<FNDIPhysicsAssetData*>(PerInstanceData);
	FNDIPhysicsAssetData* TargetData = &(SystemInstancesToProxyData.FindOrAdd(Instance));

	ensure(TargetData);
	if (TargetData)
	{
		TargetData->AssetBuffer = SourceData->AssetBuffer;
		TargetData->BoxOrigin = SourceData->BoxOrigin;
		TargetData->BoxExtent = SourceData->BoxExtent;
		TargetData->AssetArrays = SourceData->AssetArrays;
		TargetData->TickingGroup = SourceData->TickingGroup;
	}
	else
	{
		UE_LOG(LogPhysicsAsset, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %d"), Instance);
	}
	SourceData->~FNDIPhysicsAssetData();
}

void FNDIPhysicsAssetProxy::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	FNDIPhysicsAssetData* TargetData = SystemInstancesToProxyData.Find(SystemInstance);
	TargetData = &SystemInstancesToProxyData.Add(SystemInstance);
}

void FNDIPhysicsAssetProxy::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());
	SystemInstancesToProxyData.Remove(SystemInstance);
}

void FNDIPhysicsAssetProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	FNDIPhysicsAssetData* ProxyData =
		SystemInstancesToProxyData.Find(Context.SystemInstanceID);

	if (ProxyData != nullptr && ProxyData->AssetBuffer)
	{
		if (Context.SimStageData->bFirstStage)
		{
			UpdateInternalBuffer<FVector4, EPixelFormat::PF_A32B32G32R32F, PHYSICS_ASSET_MAX_TRANSFORMS, 3>(ProxyData->AssetArrays.WorldTransform, ProxyData->AssetBuffer->WorldTransformBuffer);
			UpdateInternalBuffer<FVector4, EPixelFormat::PF_A32B32G32R32F, PHYSICS_ASSET_MAX_TRANSFORMS, 3>(ProxyData->AssetArrays.InverseTransform, ProxyData->AssetBuffer->InverseTransformBuffer);

			UpdateInternalBuffer<FVector4, EPixelFormat::PF_A32B32G32R32F, PHYSICS_ASSET_MAX_PRIMITIVES>(ProxyData->AssetArrays.ElementExtent, ProxyData->AssetBuffer->ElementExtentBuffer);
			UpdateInternalBuffer<uint32, EPixelFormat::PF_R32_UINT, PHYSICS_ASSET_MAX_PRIMITIVES>(ProxyData->AssetArrays.PhysicsType, ProxyData->AssetBuffer->PhysicsTypeBuffer);
		}
	}
}

void FNDIPhysicsAssetProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{}

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfacePhysicsAsset::UNiagaraDataInterfacePhysicsAsset(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultSource(nullptr)
	, SourceActor(nullptr)
	, SourceComponents()
	, PhysicsAssets()
{
	Proxy.Reset(new FNDIPhysicsAssetProxy());
}

void UNiagaraDataInterfacePhysicsAsset::ExtractSourceComponent(FNiagaraSystemInstance* SystemInstance)
{
	// Track down the source component
	TWeakObjectPtr<USkeletalMeshComponent> SourceComponent;
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
	else if (USceneComponent* AttachComponent = SystemInstance->GetAttachComponent())
	{
		// Try to find the component by walking the attachment hierarchy
		for (USceneComponent* Curr = AttachComponent; Curr; Curr = Curr->GetAttachParent())
		{
			USkeletalMeshComponent* SkelMeshComp = Cast<USkeletalMeshComponent>(Curr);
			if (SkelMeshComp && SkelMeshComp->SkeletalMesh)
			{
				SourceComponent = SkelMeshComp;
				break;
			}
		}

		if (!SourceComponent.IsValid())
		{
			// Fall back on the attach component's outer chain if we aren't attached to the skeletal mesh 
			if (USkeletalMeshComponent* OuterComp = AttachComponent->GetTypedOuter<USkeletalMeshComponent>())
			{
				SourceComponent = OuterComp;
			}
		}
	}

	// Try to find the groom physics asset by walking the attachment hierarchy
	UPhysicsAsset* GroomPhysicsAsset = DefaultSource;
	for (USceneComponent* Curr = SystemInstance->GetAttachComponent(); Curr; Curr = Curr->GetAttachParent())
	{
		UGroomComponent* GroomComponent = Cast<UGroomComponent>(Curr);
		if (GroomComponent && GroomComponent->PhysicsAsset)
		{
			GroomPhysicsAsset = GroomComponent->PhysicsAsset;
			break;
		}
	}

	const bool IsAssetMatching = (SourceComponent != nullptr) && (GroomPhysicsAsset != nullptr) &&
		(GroomPhysicsAsset->GetPreviewMesh() == SourceComponent->SkeletalMesh);

	SourceComponents.Empty();
	PhysicsAssets.Empty();
	if (SourceComponent != nullptr)
	{
		SourceComponents.Add(SourceComponent);
		if (IsAssetMatching)
		{
			PhysicsAssets.Add(GroomPhysicsAsset);
		}
		else
		{
			PhysicsAssets.Add(SourceComponent->GetPhysicsAsset());

			if (USkeletalMeshComponent* ParentComp = Cast<USkeletalMeshComponent>(SourceComponent->GetAttachParent()))
			{
				SourceComponents.Add(ParentComp);
				PhysicsAssets.Add(ParentComp->GetPhysicsAsset());

				TArray<USceneComponent*> SceneComponents;
				ParentComp->GetChildrenComponents(true, SceneComponents);

				for (USceneComponent* ActorComp : SceneComponents)
				{
					USkeletalMeshComponent* SourceComp = Cast<USkeletalMeshComponent>(ActorComp);
					if (SourceComp && SourceComp->SkeletalMesh && SourceComp->GetPhysicsAsset() && SourceComp != SourceComponent)
					{
						SourceComponents.Add(SourceComp);
						PhysicsAssets.Add(SourceComp->GetPhysicsAsset());
					}
				}
			}
		}
	}
	else if (GroomPhysicsAsset != nullptr)
	{
		SourceComponents.Add(nullptr);
		PhysicsAssets.Add(GroomPhysicsAsset);
	}
}

bool UNiagaraDataInterfacePhysicsAsset::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIPhysicsAssetData* InstanceData = new (PerInstanceData) FNDIPhysicsAssetData();

	check(InstanceData);
	InstanceData->Init(this, SystemInstance);

	return true;
}

ETickingGroup UNiagaraDataInterfacePhysicsAsset::CalculateTickGroup(const void* PerInstanceData) const
{
	const FNDIPhysicsAssetData* InstanceData = static_cast<const FNDIPhysicsAssetData*>(PerInstanceData);

	if (InstanceData)
	{
		return InstanceData->TickingGroup;
	}
	return NiagaraFirstTickGroup;
}

void UNiagaraDataInterfacePhysicsAsset::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIPhysicsAssetData* InstanceData = static_cast<FNDIPhysicsAssetData*>(PerInstanceData);

	InstanceData->Release();
	InstanceData->~FNDIPhysicsAssetData();

	FNDIPhysicsAssetProxy* ThisProxy = GetProxyAs<FNDIPhysicsAssetProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
	{
		ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
	}
	);
}

bool UNiagaraDataInterfacePhysicsAsset::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIPhysicsAssetData* InstanceData = static_cast<FNDIPhysicsAssetData*>(PerInstanceData);
	if (InstanceData && InstanceData->AssetBuffer && SystemInstance)
	{
		InstanceData->Update(this, SystemInstance);
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
	OtherTyped->PhysicsAssets = PhysicsAssets;
	OtherTyped->SourceActor = SourceActor;
	OtherTyped->SourceComponents = SourceComponents;
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

	return  (OtherTyped->PhysicsAssets == PhysicsAssets) && (OtherTyped->SourceActor == SourceActor) && (OtherTyped->SourceComponents == SourceComponents) && (OtherTyped->DefaultSource == DefaultSource);
}

void UNiagaraDataInterfacePhysicsAsset::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

void UNiagaraDataInterfacePhysicsAsset::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumBoxesName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Boxes")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumSpheresName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Spheres")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumCapsulesName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Capsules")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestPointName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Velocity")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestElementName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Closest Element")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetElementPointName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Element Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Velocity")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetElementDistanceName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Element Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestDistanceName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetTexturePointName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Element Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Texture Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetProjectionPointName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Asset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Element Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Texture Value")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Texture Gradient")));
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
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetClosestElement);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetElementPoint);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetElementDistance);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetClosestDistance);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetTexturePoint);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetProjectionPoint);

void UNiagaraDataInterfacePhysicsAsset::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
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
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 9);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetClosestPoint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetClosestElementName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetClosestElement)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetElementPointName)
	{
		check(BindingInfo.GetNumInputs() == 7 && BindingInfo.GetNumOutputs() == 9);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetElementPoint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetElementDistanceName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetElementDistance)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetClosestDistanceName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetClosestDistance)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetTexturePointName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetTexturePoint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetProjectionPointName)
	{
		check(BindingInfo.GetNumInputs() == 10 && BindingInfo.GetNumOutputs() == 10);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsAsset, GetProjectionPoint)::Bind(this, OutFunc);
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

void UNiagaraDataInterfacePhysicsAsset::GetClosestElement(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfacePhysicsAsset::GetElementPoint(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfacePhysicsAsset::GetElementDistance(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfacePhysicsAsset::GetClosestDistance(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfacePhysicsAsset::GetTexturePoint(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfacePhysicsAsset::GetProjectionPoint(FVectorVMContext& Context)
{
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfacePhysicsAsset::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FNDIPhysicsAssetParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
		{TEXT("ElementOffsetsName"), ParamNames.ElementOffsetsName},
		{TEXT("WorldTransformBufferName"), ParamNames.WorldTransformBufferName},
		{TEXT("InverseTransformBufferName"), ParamNames.InverseTransformBufferName},
		{TEXT("ElementExtentBufferName"), ParamNames.ElementExtentBufferName},
		{TEXT("PhysicsAssetContextName"), TEXT("DIPHYSICSASSET_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (FunctionInfo.DefinitionName == GetNumBoxesName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int OutNumBoxes)
		{
			{PhysicsAssetContextName}
			OutNumBoxes = DIPhysicsAsset_GetNumBoxes(DIContext);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetNumCapsulesName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int OutNumCapsules)
		{
			{PhysicsAssetContextName}
			OutNumCapsules = DIPhysicsAsset_GetNumCapsules(DIContext);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetNumSpheresName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int OutNumSpheres)
		{
			{PhysicsAssetContextName}
			OutNumSpheres = DIPhysicsAsset_GetNumSpheres(DIContext);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetClosestPointName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 NodePosition, in float DeltaTime, in float TimeFraction, out float3 OutClosestPosition, 
							out float3 OutClosestNormal, out float3 OutClosestVelocity)
		{
			{PhysicsAssetContextName} DIPhysicsAsset_GetClosestPoint(DIContext,NodePosition,DeltaTime,TimeFraction,
				OutClosestPosition,OutClosestNormal,OutClosestVelocity);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetClosestElementName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 NodePosition, in float TimeFraction, out int OutClosestElement)
		{
			{PhysicsAssetContextName} DIPhysicsAsset_GetClosestElement(DIContext,NodePosition,TimeFraction,
				OutClosestElement);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetElementPointName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 NodePosition, in float DeltaTime, in float TimeFraction, in int ElementIndex, out float3 OutClosestPosition, 
							out float3 OutClosestNormal, out float3 OutClosestVelocity)
		{
			{PhysicsAssetContextName} DIPhysicsAsset_GetElementPoint(DIContext,NodePosition,DeltaTime,TimeFraction,ElementIndex,
				OutClosestPosition,OutClosestNormal,OutClosestVelocity);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetElementDistanceName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 NodePosition, in float TimeFraction, in int ElementIndex, out float OutClosestDistance)
		{
			{PhysicsAssetContextName} DIPhysicsAsset_GetElementDistance(DIContext,NodePosition,TimeFraction,ElementIndex,
				OutClosestDistance);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetClosestDistanceName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 NodePosition, in float TimeFraction, out float OutClosestDistance)
		{
			{PhysicsAssetContextName} DIPhysicsAsset_GetClosestDistance(DIContext,NodePosition,TimeFraction,OutClosestDistance);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetTexturePointName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 NodePosition, out int OutElementIndex, out float3 OutTexturePosition)
		{
			{PhysicsAssetContextName} DIPhysicsAsset_GetTexturePoint(DIContext,NodePosition,OutElementIndex,OutTexturePosition);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetProjectionPointName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 NodePosition, in float DeltaTime, in int ElementIndex, in float TextureValue, in float3 TextureGradient, out float3 OutClosestPosition, 
							out float3 OutClosestNormal, out float3 OutClosestVelocity, out float OutClosestDistance)
		{
			{PhysicsAssetContextName} DIPhysicsAsset_GetProjectionPoint(DIContext,NodePosition,DeltaTime,ElementIndex,TextureValue,TextureGradient,
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
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraDataInterfacePhysicsAsset.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraQuaternionUtils.ush\"\n");
}

void UNiagaraDataInterfacePhysicsAsset::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIPHYSICSASSET_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}
#endif

void UNiagaraDataInterfacePhysicsAsset::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIPhysicsAssetData* GameThreadData = static_cast<FNDIPhysicsAssetData*>(PerInstanceData);
	FNDIPhysicsAssetData* RenderThreadData = static_cast<FNDIPhysicsAssetData*>(DataForRenderThread);

	if (GameThreadData != nullptr && RenderThreadData != nullptr)
	{
		RenderThreadData->AssetBuffer = GameThreadData->AssetBuffer;
		RenderThreadData->BoxOrigin = GameThreadData->BoxOrigin;
		RenderThreadData->BoxExtent = GameThreadData->BoxExtent;
		RenderThreadData->AssetArrays = GameThreadData->AssetArrays;
		RenderThreadData->TickingGroup = GameThreadData->TickingGroup;
	}
	check(Proxy);
}

#undef LOCTEXT_NAMESPACE