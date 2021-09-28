// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceRigidMeshCollisionQuery.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "SkeletalMeshTypes.h"
#include "AnimationRuntime.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSimStageData.h"
#include "NiagaraSystemInstance.h"
#include "ShaderParameterUtils.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Renderer/Private/ScenePrivate.h"
#include "DistanceFieldAtlas.h"
#include "Renderer/Private/DistanceFieldLightingShared.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraGpuComputeDispatch.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceRigidMeshCollisionQuery"
DEFINE_LOG_CATEGORY_STATIC(LogRigidMeshCollision, Log, All);

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
static const FName GetClosestPointMeshDistanceFieldName(TEXT("GetClosestPointMeshDistanceField"));


//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfaceRigidMeshCollisionQuery::ElementOffsetsName(TEXT("ElementOffsets_"));

const FString UNiagaraDataInterfaceRigidMeshCollisionQuery::WorldTransformBufferName(TEXT("WorldTransformBuffer_"));
const FString UNiagaraDataInterfaceRigidMeshCollisionQuery::InverseTransformBufferName(TEXT("InverseTransformBuffer_"));
const FString UNiagaraDataInterfaceRigidMeshCollisionQuery::ElementExtentBufferName(TEXT("ElementExtentBuffer_"));
const FString UNiagaraDataInterfaceRigidMeshCollisionQuery::PhysicsTypeBufferName(TEXT("PhysicsTypeBuffer_"));
const FString UNiagaraDataInterfaceRigidMeshCollisionQuery::DFIndexBufferName(TEXT("DFIndexBuffer_"));

bool IsMeshDistanceFieldEnabled()
{
	static const auto* CVarGenerateMeshDistanceFields = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));
	return CVarGenerateMeshDistanceFields != nullptr && CVarGenerateMeshDistanceFields->GetValueOnAnyThread() > 0;
}

//------------------------------------------------------------------------------------------------------------

struct FNDIRigidMeshCollisionParametersName
{
	FNDIRigidMeshCollisionParametersName(const FString& Suffix)
	{
		ElementOffsetsName = UNiagaraDataInterfaceRigidMeshCollisionQuery::ElementOffsetsName + Suffix;

		WorldTransformBufferName = UNiagaraDataInterfaceRigidMeshCollisionQuery::WorldTransformBufferName + Suffix;
		InverseTransformBufferName = UNiagaraDataInterfaceRigidMeshCollisionQuery::InverseTransformBufferName + Suffix;
		ElementExtentBufferName = UNiagaraDataInterfaceRigidMeshCollisionQuery::ElementExtentBufferName + Suffix;
		PhysicsTypeBufferName = UNiagaraDataInterfaceRigidMeshCollisionQuery::PhysicsTypeBufferName + Suffix;		
		DFIndexBufferName = UNiagaraDataInterfaceRigidMeshCollisionQuery::DFIndexBufferName + Suffix;
	}

	FString ElementOffsetsName;

	FString WorldTransformBufferName;
	FString InverseTransformBufferName;
	FString ElementExtentBufferName;
	FString PhysicsTypeBufferName;
	FString DFIndexBufferName;
};

//------------------------------------------------------------------------------------------------------------

template<typename BufferType, EPixelFormat PixelFormat, uint32 ElementCount, uint32 BufferCount = 1>
void CreateInternalBuffer(FRWBuffer& OutputBuffer)
{
	if (ElementCount > 0)
	{
		OutputBuffer.Initialize(TEXT("FNDIRigidMeshCollisionBuffer"), sizeof(BufferType), ElementCount * BufferCount, PixelFormat, BUF_Static);
	}
}

template<typename BufferType, EPixelFormat PixelFormat, uint32 ElementCount, uint32 BufferCount = 1>
void UpdateInternalBuffer(const TStaticArray<BufferType,ElementCount*BufferCount>& InputData, FRWBuffer& OutputBuffer)
{
	if (ElementCount > 0 && OutputBuffer.Buffer.IsValid())
	{
		const uint32 BufferBytes = sizeof(BufferType) * ElementCount * BufferCount;

		void* OutputData = RHILockBuffer(OutputBuffer.Buffer, 0, BufferBytes, RLM_WriteOnly);

		FMemory::Memcpy(OutputData, InputData.GetData(), BufferBytes);
		RHIUnlockBuffer(OutputBuffer.Buffer);
	}
}

void FillCurrentTransforms(const FTransform& ElementTransform, uint32& ElementCount,
	TStaticArray<FVector4f,RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS>& OutCurrentTransform, TStaticArray<FVector4f, RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS>& OutCurrentInverse)
{
	// LWC_TODO: precision loss
	const uint32 ElementOffset = 3 * ElementCount;
	const FMatrix44f ElementMatrix = ElementTransform.ToMatrixWithScale();
	const FMatrix44f ElementInverse = ElementMatrix.Inverse();

	ElementMatrix.To3x4MatrixTranspose(&OutCurrentTransform[ElementOffset].X);
	ElementInverse.To3x4MatrixTranspose(&OutCurrentInverse[ElementOffset].X);
	++ElementCount;
}

void GetNumPrimitives(TArray<AStaticMeshActor*> StaticMeshActors, uint32& NumBoxes, uint32& NumSpheres, uint32& NumCapsules)
{
	NumBoxes = 0;
	NumSpheres = 0;
	NumCapsules = 0;

	for (int32 ActorIndex = 0; ActorIndex < StaticMeshActors.Num(); ++ActorIndex)
	{
		AStaticMeshActor* StaticMeshActor = StaticMeshActors[ActorIndex];
		UStaticMeshComponent* StaticMeshComponent = StaticMeshActor != nullptr ? StaticMeshActor->GetStaticMeshComponent() : nullptr;
		UBodySetup* BodySetup = StaticMeshComponent != nullptr ? StaticMeshComponent->GetBodySetup() : nullptr;
		if (BodySetup != nullptr)
		{
			// #todo(dmp): save static mesh component reference
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

void CompactInternalArrays(FNDIRigidMeshCollisionArrays* OutAssetArrays)
{
	for (uint32 TransformIndex = 0; TransformIndex < RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS; ++TransformIndex)
	{
		uint32 OffsetIndex = TransformIndex;
		OutAssetArrays->WorldTransform[OffsetIndex] = OutAssetArrays->CurrentTransform[TransformIndex];
		OutAssetArrays->InverseTransform[OffsetIndex] = OutAssetArrays->CurrentInverse[TransformIndex];

		OffsetIndex += RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS;
		OutAssetArrays->WorldTransform[OffsetIndex] = OutAssetArrays->PreviousTransform[TransformIndex];
		OutAssetArrays->InverseTransform[OffsetIndex] = OutAssetArrays->PreviousInverse[TransformIndex];		
	}
}

void CreateInternalArrays(TArray<AStaticMeshActor*> StaticMeshActors,
	FNDIRigidMeshCollisionArrays* OutAssetArrays)
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

		GetNumPrimitives(StaticMeshActors, NumBoxes, NumSpheres, NumCapsules);
		
		if ((NumBoxes + NumSpheres + NumCapsules) < RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES)
		{
			OutAssetArrays->ElementOffsets.BoxOffset = 0;
			OutAssetArrays->ElementOffsets.SphereOffset = OutAssetArrays->ElementOffsets.BoxOffset + NumBoxes;
			OutAssetArrays->ElementOffsets.CapsuleOffset = OutAssetArrays->ElementOffsets.SphereOffset + NumSpheres;
			OutAssetArrays->ElementOffsets.NumElements = OutAssetArrays->ElementOffsets.CapsuleOffset + NumCapsules;

			const uint32 NumTransforms = OutAssetArrays->ElementOffsets.NumElements * 2;
			const uint32 NumExtents = OutAssetArrays->ElementOffsets.NumElements;

			uint32 BoxCount = OutAssetArrays->ElementOffsets.BoxOffset;
			uint32 SphereCount = OutAssetArrays->ElementOffsets.SphereOffset;
			uint32 CapsuleCount = OutAssetArrays->ElementOffsets.CapsuleOffset;

			for (int32 ActorIndex = 0; ActorIndex < StaticMeshActors.Num(); ++ActorIndex)
			{				
				AStaticMeshActor* StaticMeshActor = StaticMeshActors[ActorIndex];
				UStaticMeshComponent* StaticMeshComponent = StaticMeshActor != nullptr ? StaticMeshActor->GetStaticMeshComponent() : nullptr;
				UBodySetup* BodySetup = StaticMeshComponent != nullptr ? StaticMeshComponent->GetBodySetup() : nullptr;				

				if (BodySetup != nullptr)
				{										
					const FTransform MeshTransform = StaticMeshActor->GetTransform();				

					for (const FKConvexElem& ConvexElem : BodySetup->AggGeom.ConvexElems)
					{
						if (CollisionEnabledHasPhysics(ConvexElem.GetCollisionEnabled()))
						{
							UE_LOG(LogRigidMeshCollision, Warning, TEXT("Convex collision objects encountered and will be skipped"));
						}
					}
					for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
					{
						if (CollisionEnabledHasPhysics(BoxElem.GetCollisionEnabled()))
						{							
							OutAssetArrays->PhysicsType[BoxCount] = (BoxElem.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);							
							OutAssetArrays->SourceSceneProxy[BoxCount] = StaticMeshComponent->SceneProxy;

							const FTransform ElementTransform = FTransform(BoxElem.Rotation, BoxElem.Center) * MeshTransform;
							OutAssetArrays->ElementExtent[BoxCount] = FVector4(BoxElem.X, BoxElem.Y, BoxElem.Z, 0);
							FillCurrentTransforms(ElementTransform, BoxCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);							
						}
					}

					for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
					{
						if (CollisionEnabledHasPhysics(SphereElem.GetCollisionEnabled()))
						{														
							OutAssetArrays->PhysicsType[SphereCount] = (SphereElem.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);							
							OutAssetArrays->SourceSceneProxy[BoxCount] = StaticMeshComponent->SceneProxy;

							const FTransform ElementTransform = FTransform(SphereElem.Center) * MeshTransform;
							OutAssetArrays->ElementExtent[SphereCount] = FVector4(SphereElem.Radius, 0, 0, 0);
							FillCurrentTransforms(ElementTransform, SphereCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);							
						}
					}

					for (const FKSphylElem& CapsuleElem : BodySetup->AggGeom.SphylElems)
					{
						if (CollisionEnabledHasPhysics(CapsuleElem.GetCollisionEnabled()))
						{														
							OutAssetArrays->PhysicsType[CapsuleCount] = (CapsuleElem.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);							
							OutAssetArrays->SourceSceneProxy[BoxCount] = StaticMeshComponent->SceneProxy;

							const FTransform ElementTransform = FTransform(CapsuleElem.Rotation, CapsuleElem.Center) * MeshTransform;
							OutAssetArrays->ElementExtent[CapsuleCount] = FVector4(CapsuleElem.Radius, CapsuleElem.Length, 0, 0);
							FillCurrentTransforms(ElementTransform, CapsuleCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);							
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
			UE_LOG(LogRigidMeshCollision, Warning, TEXT("Number of Collision DI primitives is higher than the niagara %d limit"), RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES);
		}
	}
}

void UpdateInternalArrays(const TArray<AStaticMeshActor*> &StaticMeshActors, FNDIRigidMeshCollisionArrays* OutAssetArrays)
{
	if (OutAssetArrays != nullptr && OutAssetArrays->ElementOffsets.NumElements < RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES)
	{
		uint32 NumBoxes = 0;
		uint32 NumSpheres = 0;
		uint32 NumCapsules = 0;

		GetNumPrimitives(StaticMeshActors, NumBoxes, NumSpheres, NumCapsules);

		if (((OutAssetArrays->ElementOffsets.SphereOffset - OutAssetArrays->ElementOffsets.BoxOffset) != NumBoxes) || 
			((OutAssetArrays->ElementOffsets.CapsuleOffset - OutAssetArrays->ElementOffsets.SphereOffset) != NumSpheres) ||
			((OutAssetArrays->ElementOffsets.NumElements - OutAssetArrays->ElementOffsets.CapsuleOffset) != NumCapsules))
		{
			CreateInternalArrays(StaticMeshActors, OutAssetArrays);
		}

		OutAssetArrays->PreviousTransform = OutAssetArrays->CurrentTransform;
		OutAssetArrays->PreviousInverse = OutAssetArrays->CurrentInverse;

		uint32 BoxCount = OutAssetArrays->ElementOffsets.BoxOffset;
		uint32 SphereCount = OutAssetArrays->ElementOffsets.SphereOffset;
		uint32 CapsuleCount = OutAssetArrays->ElementOffsets.CapsuleOffset;

		for (int32 ActorIndex = 0; ActorIndex < StaticMeshActors.Num(); ++ActorIndex)
		{
			AStaticMeshActor* StaticMeshActor = StaticMeshActors[ActorIndex];
			UStaticMeshComponent* StaticMeshComponent = StaticMeshActor != nullptr ? StaticMeshActor->GetStaticMeshComponent() : nullptr;
			UBodySetup* BodySetup = StaticMeshComponent != nullptr ? StaticMeshComponent->GetBodySetup() : nullptr;
			if (BodySetup != nullptr)
			{
				const FTransform MeshTransform = StaticMeshActor->GetTransform();

				for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
				{
					if (CollisionEnabledHasPhysics(BoxElem.GetCollisionEnabled()))
					{
						const FTransform ElementTransform = FTransform(BoxElem.Rotation, BoxElem.Center) * MeshTransform;
						FillCurrentTransforms(ElementTransform, BoxCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
					}
				}

				for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
				{
					if (CollisionEnabledHasPhysics(SphereElem.GetCollisionEnabled()))
					{
						const FTransform ElementTransform = FTransform(SphereElem.Center) * MeshTransform;
						FillCurrentTransforms(ElementTransform, SphereCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
					}
				}

				for (const FKSphylElem& CapsuleElem : BodySetup->AggGeom.SphylElems)
				{
					if (CollisionEnabledHasPhysics(CapsuleElem.GetCollisionEnabled()))
					{
						const FTransform ElementTransform = FTransform(CapsuleElem.Rotation, CapsuleElem.Center) * MeshTransform;
						FillCurrentTransforms(ElementTransform, CapsuleCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
					}
				}
			}
		}

		CompactInternalArrays(OutAssetArrays);
	}
}

//------------------------------------------------------------------------------------------------------------

void FNDIRigidMeshCollisionBuffer::InitRHI()
{
	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F, RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS, 2>(WorldTransformBuffer);
	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F, RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS, 2>(InverseTransformBuffer);

	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F, RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES>(ElementExtentBuffer);
	CreateInternalBuffer<uint32, EPixelFormat::PF_R32_UINT, RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES>(PhysicsTypeBuffer);
	CreateInternalBuffer<uint32, EPixelFormat::PF_R32_UINT, RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES>(DFIndexBuffer);
}

void FNDIRigidMeshCollisionBuffer::ReleaseRHI()
{
	WorldTransformBuffer.Release();
	InverseTransformBuffer.Release();
	ElementExtentBuffer.Release();
	PhysicsTypeBuffer.Release();
	DFIndexBuffer.Release();
}

//------------------------------------------------------------------------------------------------------------



void FNDIRigidMeshCollisionData::Release()
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

void FNDIRigidMeshCollisionData::Init(UNiagaraDataInterfaceRigidMeshCollisionQuery* Interface, FNiagaraSystemInstance* SystemInstance)
{
	AssetBuffer = nullptr;

	if (Interface != nullptr && SystemInstance != nullptr)
	{
		UWorld* World = SystemInstance->GetWorld();

		StaticMeshActors.Empty();

		if (Interface->StaticMesh)
		{
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				AStaticMeshActor* StaticMeshActor = *It;

				if ((!Interface->OnlyUseMoveable || (Interface->OnlyUseMoveable && StaticMeshActor->IsRootComponentMovable())) &&
					(Interface->Tag == FString("") || (Interface->Tag != FString("") && StaticMeshActor->Tags.Contains(FName(Interface->Tag)))))
				{
					StaticMeshActors.Add(StaticMeshActor);
				}
			}
		}

		if (0 < StaticMeshActors.Num() && StaticMeshActors[0] != nullptr)
		{
			// @note: we are not creating internal arrays here and letting it happen on the call to update
			 //CreateInternalArrays(StaticMeshActors, &AssetArrays);
		}

		AssetBuffer = new FNDIRigidMeshCollisionBuffer();
		BeginInitResource(AssetBuffer);
	}
}

void FNDIRigidMeshCollisionData::Update(UNiagaraDataInterfaceRigidMeshCollisionQuery* Interface, FNiagaraSystemInstance* SystemInstance)
{
	if (Interface != nullptr && SystemInstance != nullptr)
	{		
		TickingGroup = ComputeTickingGroup();

		if (0 < StaticMeshActors.Num() && StaticMeshActors[0] != nullptr)
		{
			UpdateInternalArrays(StaticMeshActors, &AssetArrays);
		}
	}
}

ETickingGroup FNDIRigidMeshCollisionData::ComputeTickingGroup()
{
	TickingGroup = NiagaraFirstTickGroup;
	for (int32 ComponentIndex = 0; ComponentIndex < StaticMeshActors.Num(); ++ComponentIndex)
	{
		if (StaticMeshActors[ComponentIndex] != nullptr)
		{			
			UStaticMeshComponent* Component = StaticMeshActors[ComponentIndex]->GetStaticMeshComponent();

			if (Component == nullptr)
			{
				continue;
			}
				const ETickingGroup ComponentTickGroup = FMath::Max(Component->PrimaryComponentTick.TickGroup, Component->PrimaryComponentTick.EndTickGroup);
				const ETickingGroup PhysicsTickGroup = ComponentTickGroup;
				const ETickingGroup ClampedTickGroup = FMath::Clamp(ETickingGroup(PhysicsTickGroup + 1), NiagaraFirstTickGroup, NiagaraLastTickGroup);		

			TickingGroup = FMath::Max(TickingGroup, ClampedTickGroup);
		}
	}
	return TickingGroup;
}

//------------------------------------------------------------------------------------------------------------

class FDistanceFieldParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FDistanceFieldParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		SceneObjectBounds.Bind(ParameterMap, TEXT("SceneObjectBounds"));
		SceneObjectData.Bind(ParameterMap, TEXT("SceneObjectData"));
		NumSceneObjects.Bind(ParameterMap, TEXT("NumSceneObjects"));
		SceneDistanceFieldAssetData.Bind(ParameterMap, TEXT("SceneDistanceFieldAssetData"));
		DistanceFieldIndirectionTable.Bind(ParameterMap, TEXT("DistanceFieldIndirectionTable"));
		DistanceFieldBrickTexture.Bind(ParameterMap, TEXT("DistanceFieldBrickTexture"));
		DistanceFieldSampler.Bind(ParameterMap, TEXT("DistanceFieldSampler"));
		DistanceFieldBrickSize.Bind(ParameterMap, TEXT("DistanceFieldBrickSize"));
		DistanceFieldUniqueDataBrickSize.Bind(ParameterMap, TEXT("DistanceFieldUniqueDataBrickSize"));
		DistanceFieldBrickAtlasSizeInBricks.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasSizeInBricks"));
		DistanceFieldBrickAtlasMask.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasMask"));
		DistanceFieldBrickAtlasSizeLog2.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasSizeLog2"));
		DistanceFieldBrickAtlasTexelSize.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasTexelSize"));
		DistanceFieldBrickAtlasHalfTexelSize.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasHalfTexelSize"));
		DistanceFieldBrickOffsetToAtlasUVScale.Bind(ParameterMap, TEXT("DistanceFieldBrickOffsetToAtlasUVScale"));
		DistanceFieldUniqueDataBrickSizeInAtlasTexels.Bind(ParameterMap, TEXT("DistanceFieldUniqueDataBrickSizeInAtlasTexels"));
	}

	bool IsBound() const
	{
		return SceneDistanceFieldAssetData.IsBound();
	}

	friend FArchive& operator<<(FArchive& Ar, FDistanceFieldParameters& Parameters)
	{
		Ar << Parameters.SceneObjectBounds;
		Ar << Parameters.SceneObjectData;
		Ar << Parameters.NumSceneObjects;
		Ar << Parameters.SceneDistanceFieldAssetData;
		Ar << Parameters.DistanceFieldIndirectionTable;
		Ar << Parameters.DistanceFieldBrickTexture;
		Ar << Parameters.DistanceFieldSampler;
		Ar << Parameters.DistanceFieldBrickSize;
		Ar << Parameters.DistanceFieldUniqueDataBrickSize;
		Ar << Parameters.DistanceFieldBrickAtlasSizeInBricks;
		Ar << Parameters.DistanceFieldBrickAtlasMask;
		Ar << Parameters.DistanceFieldBrickAtlasSizeLog2;
		Ar << Parameters.DistanceFieldBrickAtlasTexelSize;
		Ar << Parameters.DistanceFieldBrickAtlasHalfTexelSize;
		Ar << Parameters.DistanceFieldBrickOffsetToAtlasUVScale;
		Ar << Parameters.DistanceFieldUniqueDataBrickSizeInAtlasTexels;

		return Ar;
	}

	template<typename ShaderRHIParamRef>
	FORCEINLINE_DEBUGGABLE void Set(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FDistanceFieldSceneData* ParameterData) const
	{
		if (IsBound())
		{
			SetSRVParameter(RHICmdList, ShaderRHI, SceneObjectBounds, ParameterData->GetCurrentObjectBuffers()->Bounds.SRV);
			SetSRVParameter(RHICmdList, ShaderRHI, SceneObjectData, ParameterData->GetCurrentObjectBuffers()->Data.SRV);
			SetShaderValue(RHICmdList, ShaderRHI, NumSceneObjects, ParameterData->NumObjectsInBuffer);
			SetSRVParameter(RHICmdList, ShaderRHI, SceneDistanceFieldAssetData, ParameterData->AssetDataBuffer.SRV);
			SetSRVParameter(RHICmdList, ShaderRHI, DistanceFieldIndirectionTable, ParameterData->IndirectionTable.SRV);
			SetTextureParameter(RHICmdList, ShaderRHI, DistanceFieldBrickTexture, ParameterData->DistanceFieldBrickVolumeTexture->GetRenderTargetItem().ShaderResourceTexture);
			SetSamplerParameter(RHICmdList, ShaderRHI, DistanceFieldSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickSize, FVector3f(DistanceField::BrickSize));
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldUniqueDataBrickSize, FVector3f(DistanceField::UniqueDataBrickSize));
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasSizeInBricks, ParameterData->BrickTextureDimensionsInBricks);
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasMask, ParameterData->BrickTextureDimensionsInBricks - FIntVector(1));
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasSizeLog2, FIntVector(
				FMath::FloorLog2(ParameterData->BrickTextureDimensionsInBricks.X),
				FMath::FloorLog2(ParameterData->BrickTextureDimensionsInBricks.Y),
				FMath::FloorLog2(ParameterData->BrickTextureDimensionsInBricks.Z)));
			FVector3f DistanceFieldBrickAtlasTexelSizeTmp = FVector3f(1.0f) / FVector3f(ParameterData->BrickTextureDimensionsInBricks * DistanceField::BrickSize);
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasTexelSize, DistanceFieldBrickAtlasTexelSizeTmp);
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasHalfTexelSize, 0.5f * DistanceFieldBrickAtlasTexelSizeTmp);
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickOffsetToAtlasUVScale, FVector3f(DistanceField::BrickSize) * DistanceFieldBrickAtlasTexelSizeTmp);
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldUniqueDataBrickSizeInAtlasTexels, FVector3f(DistanceField::UniqueDataBrickSize) * DistanceFieldBrickAtlasTexelSizeTmp);
		}
	}

private:

	LAYOUT_FIELD(FShaderResourceParameter, SceneObjectBounds)
		LAYOUT_FIELD(FShaderResourceParameter, SceneObjectData)
		LAYOUT_FIELD(FShaderParameter, NumSceneObjects)
		LAYOUT_FIELD(FShaderResourceParameter, SceneDistanceFieldAssetData)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldIndirectionTable)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldBrickTexture)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldSampler)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickSize)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldUniqueDataBrickSize)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasSizeInBricks)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasMask)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasSizeLog2)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasTexelSize)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasHalfTexelSize)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickOffsetToAtlasUVScale)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldUniqueDataBrickSizeInAtlasTexels)
};

//------------------------------------------------------------------------------------------------------------

struct FNDIRigidMeshCollisionParametersCS : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNDIRigidMeshCollisionParametersCS, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FNDIRigidMeshCollisionParametersName ParamNames(*ParameterInfo.DataInterfaceHLSLSymbol);

		ElementOffsets.Bind(ParameterMap, *ParamNames.ElementOffsetsName);

		WorldTransformBuffer.Bind(ParameterMap, *ParamNames.WorldTransformBufferName);
		InverseTransformBuffer.Bind(ParameterMap, *ParamNames.InverseTransformBufferName);
		ElementExtentBuffer.Bind(ParameterMap, *ParamNames.ElementExtentBufferName);
		PhysicsTypeBuffer.Bind(ParameterMap, *ParamNames.PhysicsTypeBufferName);	
		DFIndexBuffer.Bind(ParameterMap, *ParamNames.DFIndexBufferName);

		DistanceFieldParameters.Bind(ParameterMap);
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		FNDIRigidMeshCollisionProxy* InterfaceProxy =
			static_cast<FNDIRigidMeshCollisionProxy*>(Context.DataInterface);
		FNDIRigidMeshCollisionData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstanceID);

		if (ProxyData != nullptr && ProxyData->AssetBuffer && ProxyData->AssetBuffer->IsInitialized())
		{
			FNDIRigidMeshCollisionBuffer* AssetBuffer = ProxyData->AssetBuffer;

			FRHITransitionInfo Transitions[] = {
				FRHITransitionInfo(AssetBuffer->WorldTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				FRHITransitionInfo(AssetBuffer->InverseTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				FRHITransitionInfo(AssetBuffer->ElementExtentBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				FRHITransitionInfo(AssetBuffer->PhysicsTypeBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				FRHITransitionInfo(AssetBuffer->DFIndexBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute)
			};
			RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));

			SetSRVParameter(RHICmdList, ComputeShaderRHI, WorldTransformBuffer, AssetBuffer->WorldTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, InverseTransformBuffer, AssetBuffer->InverseTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ElementExtentBuffer, AssetBuffer->ElementExtentBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PhysicsTypeBuffer, AssetBuffer->PhysicsTypeBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DFIndexBuffer, AssetBuffer->DFIndexBuffer.SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, ElementOffsets, ProxyData->AssetArrays.ElementOffsets);

			if (DistanceFieldParameters.IsBound())
			{
				const FDistanceFieldSceneData *DistanceFieldSceneData = static_cast<const FNiagaraGpuComputeDispatch*>(Context.ComputeDispatchInterface)->GetMeshDistanceFieldParameters();	//-BATCHERTODO:

				if (DistanceFieldSceneData == nullptr)
				{
					UE_LOG(LogRigidMeshCollision, Error, TEXT("Distance fields are not available for use"));
					// #todo(dmp): should we set something here in the case where distance field data is bound but we don't have it?
					// there is no trivial constructor
					//FDistanceFieldSceneData DummyDistanceFieldSceneData(Context.Shader->GetShaderPlatform());
					//DistanceFieldParameters.SetEmpty(RHICmdList, ComputeShaderRHI, DummyDistanceFieldSceneData);
				}
				else 
				{
					DistanceFieldParameters.Set(RHICmdList, ComputeShaderRHI, DistanceFieldSceneData);
				}				
			}
		}
		else
		{
			SetSRVParameter(RHICmdList, ComputeShaderRHI, WorldTransformBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, InverseTransformBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ElementExtentBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PhysicsTypeBuffer, FNiagaraRenderer::GetDummyIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DFIndexBuffer, FNiagaraRenderer::GetDummyIntBuffer());

			static const FElementOffset DummyOffsets(0, 0, 0, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ElementOffsets, DummyOffsets);	
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
	LAYOUT_FIELD(FShaderResourceParameter, DFIndexBuffer);

	LAYOUT_FIELD(FDistanceFieldParameters, DistanceFieldParameters);	
};

IMPLEMENT_TYPE_LAYOUT(FNDIRigidMeshCollisionParametersCS);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceRigidMeshCollisionQuery, FNDIRigidMeshCollisionParametersCS);


//------------------------------------------------------------------------------------------------------------

void FNDIRigidMeshCollisionProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	check(IsInRenderingThread());

	FNDIRigidMeshCollisionData* SourceData = static_cast<FNDIRigidMeshCollisionData*>(PerInstanceData);	
	FNDIRigidMeshCollisionData* TargetData = &(SystemInstancesToProxyData.FindOrAdd(Instance));

	ensure(TargetData);
	if (TargetData)
	{
		TargetData->AssetBuffer = SourceData->AssetBuffer;		
		TargetData->AssetArrays = SourceData->AssetArrays;
		TargetData->TickingGroup = SourceData->TickingGroup;

		// loop over proxies and compute df indices on the RT only on new data
		// @todo(dmp): for now we do this every frame because it seems like sometimes DFindices are not set

		for (uint32 i = 0; i < SourceData->AssetArrays.ElementOffsets.NumElements; ++i)
		{
			FPrimitiveSceneProxy* Proxy = SourceData->AssetArrays.SourceSceneProxy[i];
								
			if (Proxy != nullptr && Proxy->GetPrimitiveSceneInfo() != nullptr)
			{
				const TArray<int32, TInlineAllocator<1>>& DFIndices = Proxy->GetPrimitiveSceneInfo()->DistanceFieldInstanceIndices;				
				TargetData->AssetArrays.DFIndex[i] = DFIndices.Num() > 0 ? DFIndices[0] : -1;
			}
			else
			{
				TargetData->AssetArrays.DFIndex[i] = -1;
			}
		}	
	}
	else
	{
		UE_LOG(LogRigidMeshCollision, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %d"), Instance);
	}
}

void FNDIRigidMeshCollisionProxy::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	check(!SystemInstancesToProxyData.Contains(SystemInstance));
	FNDIRigidMeshCollisionData* TargetData = &SystemInstancesToProxyData.Add(SystemInstance);
}

void FNDIRigidMeshCollisionProxy::DestroyPerInstanceData(const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());
	SystemInstancesToProxyData.Remove(SystemInstance);
}

void FNDIRigidMeshCollisionProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	check(SystemInstancesToProxyData.Contains(Context.SystemInstanceID));

	FNDIRigidMeshCollisionData* ProxyData =
		SystemInstancesToProxyData.Find(Context.SystemInstanceID);

	if (ProxyData != nullptr && ProxyData->AssetBuffer)
	{
		if (Context.SimStageData->bFirstStage)
		{
			FRHITransitionInfo Transitions[] = {
				FRHITransitionInfo(ProxyData->AssetBuffer->WorldTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(ProxyData->AssetBuffer->InverseTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(ProxyData->AssetBuffer->PhysicsTypeBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(ProxyData->AssetBuffer->ElementExtentBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(ProxyData->AssetBuffer->DFIndexBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
			};
			RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));

			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F, RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS, 2>(ProxyData->AssetArrays.WorldTransform, ProxyData->AssetBuffer->WorldTransformBuffer);
			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F, RIGID_MESH_COLLISION_QUERY_MAX_TRANSFORMS, 2>(ProxyData->AssetArrays.InverseTransform, ProxyData->AssetBuffer->InverseTransformBuffer);
			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F, RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES>(ProxyData->AssetArrays.ElementExtent, ProxyData->AssetBuffer->ElementExtentBuffer);
			UpdateInternalBuffer<uint32, EPixelFormat::PF_R32_UINT, RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES>(ProxyData->AssetArrays.PhysicsType, ProxyData->AssetBuffer->PhysicsTypeBuffer);
			UpdateInternalBuffer<uint32, EPixelFormat::PF_R32_UINT, RIGID_MESH_COLLISION_QUERY_MAX_PRIMITIVES>(ProxyData->AssetArrays.DFIndex, ProxyData->AssetBuffer->DFIndexBuffer);
		}
	}
}

void FNDIRigidMeshCollisionProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{}

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfaceRigidMeshCollisionQuery::UNiagaraDataInterfaceRigidMeshCollisionQuery(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)	
{
	Proxy.Reset(new FNDIRigidMeshCollisionProxy());
}

bool UNiagaraDataInterfaceRigidMeshCollisionQuery::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIRigidMeshCollisionData* InstanceData = new (PerInstanceData) FNDIRigidMeshCollisionData();

	check(InstanceData);
	InstanceData->Init(this, SystemInstance);

	return true;
}

ETickingGroup UNiagaraDataInterfaceRigidMeshCollisionQuery::CalculateTickGroup(const void* PerInstanceData) const
{
	const FNDIRigidMeshCollisionData* InstanceData = static_cast<const FNDIRigidMeshCollisionData*>(PerInstanceData);

	if (InstanceData)
	{
		return InstanceData->TickingGroup;
	}
	return NiagaraFirstTickGroup;
}

void UNiagaraDataInterfaceRigidMeshCollisionQuery::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIRigidMeshCollisionData* InstanceData = static_cast<FNDIRigidMeshCollisionData*>(PerInstanceData);

	InstanceData->Release();
	InstanceData->~FNDIRigidMeshCollisionData();

	FNDIRigidMeshCollisionProxy* ThisProxy = GetProxyAs<FNDIRigidMeshCollisionProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& CmdList)
	{
		ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
	}
	);
}

bool UNiagaraDataInterfaceRigidMeshCollisionQuery::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIRigidMeshCollisionData* InstanceData = static_cast<FNDIRigidMeshCollisionData*>(PerInstanceData);
	if (InstanceData && InstanceData->AssetBuffer && SystemInstance)
	{
		InstanceData->Update(this, SystemInstance);
	}
	return false;
}

bool UNiagaraDataInterfaceRigidMeshCollisionQuery::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceRigidMeshCollisionQuery* OtherTyped = CastChecked<UNiagaraDataInterfaceRigidMeshCollisionQuery>(Destination);		

	OtherTyped->StaticMesh = StaticMesh;
	OtherTyped->Tag = Tag;
	OtherTyped->OnlyUseMoveable = OnlyUseMoveable;

	return true;
}

bool UNiagaraDataInterfaceRigidMeshCollisionQuery::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceRigidMeshCollisionQuery* OtherTyped = CastChecked<const UNiagaraDataInterfaceRigidMeshCollisionQuery>(Other);

	return  (OtherTyped->StaticMesh == StaticMesh) && (OtherTyped->Tag == Tag) && (OtherTyped->OnlyUseMoveable == OnlyUseMoveable);
}

void UNiagaraDataInterfaceRigidMeshCollisionQuery::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

void UNiagaraDataInterfaceRigidMeshCollisionQuery::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumBoxesName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("World Position")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("World Position")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("World Position")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));

		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestPointMeshDistanceFieldName;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Velocity")));

		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceRigidMeshCollisionQuery::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceRigidMeshCollisionQuery::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FNDIRigidMeshCollisionParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
		{TEXT("ElementOffsetsName"), ParamNames.ElementOffsetsName},
		{TEXT("WorldTransformBufferName"), ParamNames.WorldTransformBufferName},
		{TEXT("InverseTransformBufferName"), ParamNames.InverseTransformBufferName},
		{TEXT("ElementExtentBufferName"), ParamNames.ElementExtentBufferName},
		{TEXT("RigidMeshCollisionContextName"), TEXT("DIRIGIDMESHCOLLISIONQUERY_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (FunctionInfo.DefinitionName == GetNumBoxesName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int OutNumBoxes)
		{
			{RigidMeshCollisionContextName}
			OutNumBoxes = DIRigidMeshCollision_GetNumBoxes(DIContext);
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
			{RigidMeshCollisionContextName}
			OutNumCapsules = DIRigidMeshCollision_GetNumCapsules(DIContext);
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
			{RigidMeshCollisionContextName}
			OutNumSpheres = DIRigidMeshCollision_GetNumSpheres(DIContext);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetClosestPointName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in float DeltaTime, in float TimeFraction, out float ClosestDistance, out float3 OutClosestPosition, 
							out float3 OutClosestNormal, out float3 OutClosestVelocity)
		{
			{RigidMeshCollisionContextName} DIRigidMeshCollision_GetClosestPoint(DIContext,WorldPosition,DeltaTime,TimeFraction, ClosestDistance,
				OutClosestPosition,OutClosestNormal,OutClosestVelocity);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetClosestElementName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in float TimeFraction, out int OutClosestElement)
		{
			{RigidMeshCollisionContextName} DIRigidMeshCollision_GetClosestElement(DIContext,WorldPosition,TimeFraction,
				OutClosestElement);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetElementPointName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in float DeltaTime, in float TimeFraction, in int ElementIndex, out float3 OutClosestPosition, 
							out float3 OutClosestNormal, out float3 OutClosestVelocity)
		{
			{RigidMeshCollisionContextName} DIRigidMeshCollision_GetElementPoint(DIContext,WorldPosition,DeltaTime,TimeFraction,ElementIndex,
				OutClosestPosition,OutClosestNormal,OutClosestVelocity);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetElementDistanceName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in float TimeFraction, in int ElementIndex, out float OutClosestDistance)
		{
			{RigidMeshCollisionContextName} DIRigidMeshCollision_GetElementDistance(DIContext,WorldPosition,TimeFraction,ElementIndex,
				OutClosestDistance);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetClosestDistanceName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in float TimeFraction, out float OutClosestDistance)
		{
			{RigidMeshCollisionContextName} DIRigidMeshCollision_GetClosestDistance(DIContext,WorldPosition,TimeFraction,OutClosestDistance);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetClosestPointMeshDistanceFieldName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in float DeltaTime, in float TimeFraction, out float ClosestDistance, out float3 OutClosestPosition, 
							out float3 OutClosestNormal, out float3 OutClosestVelocity)
		{
			{RigidMeshCollisionContextName} DIRigidMeshCollision_GetClosestPointMeshDistanceField(DIContext,WorldPosition,DeltaTime,TimeFraction, ClosestDistance,
				OutClosestPosition,OutClosestNormal,OutClosestVelocity);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}

	OutHLSL += TEXT("\n");
	return false;
}

void UNiagaraDataInterfaceRigidMeshCollisionQuery::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Engine/Private/DistanceFieldLightingShared.ush\"\n");
	OutHLSL += TEXT("#include \"/Engine/Private/MeshDistanceFieldCommon.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/FX/Niagara/Private/NiagaraDataInterfaceRigidMeshCollisionQuery.ush\"\n");			
}

void UNiagaraDataInterfaceRigidMeshCollisionQuery::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIRIGIDMESHCOLLISIONQUERY_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}
#endif

#if WITH_EDITOR
void UNiagaraDataInterfaceRigidMeshCollisionQuery::ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors)
{
	if (Function.Name == GetClosestPointMeshDistanceFieldName)
	{
		if (!IsMeshDistanceFieldEnabled())
		{
			OutValidationErrors.Add(NSLOCTEXT("UNiagaraDataInterfaceRigidMeshCollisionQuery", "NiagaraDistanceFieldNotEnabledMsg", "The mesh distance field generation is currently not enabled, please check the project settings.\nNiagara cannot query the mesh distance fields otherwise."));
		}
	}
}
#endif

void UNiagaraDataInterfaceRigidMeshCollisionQuery::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIRigidMeshCollisionData* GameThreadData = static_cast<FNDIRigidMeshCollisionData*>(PerInstanceData);
	FNDIRigidMeshCollisionData* RenderThreadData = static_cast<FNDIRigidMeshCollisionData*>(DataForRenderThread);

	if (GameThreadData != nullptr && RenderThreadData != nullptr)
	{		
		RenderThreadData->AssetBuffer = GameThreadData->AssetBuffer;		
		RenderThreadData->AssetArrays = GameThreadData->AssetArrays;
		RenderThreadData->TickingGroup = GameThreadData->TickingGroup;
	}
	check(Proxy);
}

#undef LOCTEXT_NAMESPACE