// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceRigidMeshCollisionQuery.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "AnimationRuntime.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSimStageData.h"
#include "NiagaraSystemInstance.h"
#include "ShaderParameterUtils.h"
#include "EngineUtils.h"
#include "Renderer/Private/ScenePrivate.h"
#include "DistanceFieldAtlas.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraGpuComputeDispatch.h"
#include "DataInterface/NiagaraDistanceFieldParameters.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceRigidMeshCollisionQuery"
DEFINE_LOG_CATEGORY_STATIC(LogRigidMeshCollision, Log, All);

struct FNiagaraRigidMeshCollisionDIFunctionVersion
{
	enum Type
	{
		InitialVersion = 0,
		LargeWorldCoordinates = 1,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
};

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
static const FName GetClosestPointMeshDistanceFieldNoNormalName(TEXT("GetClosestPointMeshDistanceFieldNoNormal"));


//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfaceRigidMeshCollisionQuery::MaxTransformsName(TEXT("MaxTransforms_"));
const FString UNiagaraDataInterfaceRigidMeshCollisionQuery::CurrentOffsetName(TEXT("CurrentOffset_"));
const FString UNiagaraDataInterfaceRigidMeshCollisionQuery::PreviousOffsetName(TEXT("PreviousOffset_"));

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
		MaxTransformsName = UNiagaraDataInterfaceRigidMeshCollisionQuery::MaxTransformsName + Suffix;
		CurrentOffsetName = UNiagaraDataInterfaceRigidMeshCollisionQuery::CurrentOffsetName + Suffix;
		PreviousOffsetName = UNiagaraDataInterfaceRigidMeshCollisionQuery::PreviousOffsetName + Suffix;

		ElementOffsetsName = UNiagaraDataInterfaceRigidMeshCollisionQuery::ElementOffsetsName + Suffix;

		WorldTransformBufferName = UNiagaraDataInterfaceRigidMeshCollisionQuery::WorldTransformBufferName + Suffix;
		InverseTransformBufferName = UNiagaraDataInterfaceRigidMeshCollisionQuery::InverseTransformBufferName + Suffix;
		ElementExtentBufferName = UNiagaraDataInterfaceRigidMeshCollisionQuery::ElementExtentBufferName + Suffix;
		PhysicsTypeBufferName = UNiagaraDataInterfaceRigidMeshCollisionQuery::PhysicsTypeBufferName + Suffix;		
		DFIndexBufferName = UNiagaraDataInterfaceRigidMeshCollisionQuery::DFIndexBufferName + Suffix;
	}

	FString MaxTransformsName;
	FString CurrentOffsetName;
	FString PreviousOffsetName;

	FString ElementOffsetsName;

	FString WorldTransformBufferName;
	FString InverseTransformBufferName;
	FString ElementExtentBufferName;
	FString PhysicsTypeBufferName;
	FString DFIndexBufferName;
};

//------------------------------------------------------------------------------------------------------------

template<typename BufferType, EPixelFormat PixelFormat>
void CreateInternalBuffer(FRWBuffer& OutputBuffer, uint32 ElementCount)
{
	if (ElementCount > 0)
	{
		OutputBuffer.Initialize(TEXT("FNDIRigidMeshCollisionBuffer"), sizeof(BufferType), ElementCount, PixelFormat, BUF_Static);
	}
}

template<typename BufferType, EPixelFormat PixelFormat>
void UpdateInternalBuffer(const TArray<BufferType>& InputData, FRWBuffer& OutputBuffer)
{
	uint32 ElementCount = InputData.Num();
	if (ElementCount > 0 && OutputBuffer.Buffer.IsValid())
	{
		const uint32 BufferBytes = sizeof(BufferType) * ElementCount;

		void* OutputData = RHILockBuffer(OutputBuffer.Buffer, 0, BufferBytes, RLM_WriteOnly);

		FMemory::Memcpy(OutputData, InputData.GetData(), BufferBytes);
		RHIUnlockBuffer(OutputBuffer.Buffer);
	}
}

void FillCurrentTransforms(const FTransform& ElementTransform, uint32& ElementCount, TArray<FVector4f>& OutCurrentTransform, TArray<FVector4f>& OutCurrentInverse)
{
	// LWC_TODO: precision loss
	const uint32 ElementOffset = 3 * ElementCount;
	const FMatrix44f ElementMatrix = FMatrix44f(ElementTransform.ToMatrixWithScale());
	const FMatrix44f ElementInverse = ElementMatrix.Inverse();

	ElementMatrix.To3x4MatrixTranspose(&OutCurrentTransform[ElementOffset].X);
	ElementInverse.To3x4MatrixTranspose(&OutCurrentInverse[ElementOffset].X);
	++ElementCount;
}

void GetNumPrimitives(TArray<AActor*> Actors, uint32& NumBoxes, uint32& NumSpheres, uint32& NumCapsules)
{
	NumBoxes = 0;
	NumSpheres = 0;
	NumCapsules = 0;

	for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
	{
		AActor* Actor = Actors[ActorIndex];
		UStaticMeshComponent* StaticMeshComponent = nullptr;
		if (Actor != nullptr)
		{
			StaticMeshComponent = Cast<UStaticMeshComponent>(Actor->GetComponentByClass(UStaticMeshComponent::StaticClass()));
		}
		UBodySetup* BodySetup = StaticMeshComponent != nullptr ? StaticMeshComponent->GetBodySetup() : nullptr;
		if (BodySetup != nullptr)
		{
			// #todo(dmp): save static mesh component reference

			for (const FKConvexElem& ConvexElem : BodySetup->AggGeom.ConvexElems)
			{
				if (CollisionEnabledHasPhysics(ConvexElem.GetCollisionEnabled()))
				{
					NumBoxes += 1;
				}
			}
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
	for (uint32 TransformIndex = 0; TransformIndex < OutAssetArrays->MaxTransforms; ++TransformIndex)
	{
		uint32 OffsetIndex = TransformIndex;
		OutAssetArrays->WorldTransform[OffsetIndex] = OutAssetArrays->CurrentTransform[TransformIndex];
		OutAssetArrays->InverseTransform[OffsetIndex] = OutAssetArrays->CurrentInverse[TransformIndex];

		OffsetIndex += OutAssetArrays->MaxTransforms;
		OutAssetArrays->WorldTransform[OffsetIndex] = OutAssetArrays->PreviousTransform[TransformIndex];
		OutAssetArrays->InverseTransform[OffsetIndex] = OutAssetArrays->PreviousInverse[TransformIndex];		
	}
}

void CreateInternalArrays(TArray<AActor*> Actors, FNDIRigidMeshCollisionArrays* OutAssetArrays, FVector LWCTile)
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

		GetNumPrimitives(Actors, NumBoxes, NumSpheres, NumCapsules);
		
		if ((NumBoxes + NumSpheres + NumCapsules) < OutAssetArrays->MaxPrimitives)
		{
			OutAssetArrays->ElementOffsets.BoxOffset = 0;
			OutAssetArrays->ElementOffsets.SphereOffset = OutAssetArrays->ElementOffsets.BoxOffset + NumBoxes;
			OutAssetArrays->ElementOffsets.CapsuleOffset = OutAssetArrays->ElementOffsets.SphereOffset + NumSpheres;
			OutAssetArrays->ElementOffsets.NumElements = OutAssetArrays->ElementOffsets.CapsuleOffset + NumCapsules;

			uint32 BoxCount = OutAssetArrays->ElementOffsets.BoxOffset;
			uint32 SphereCount = OutAssetArrays->ElementOffsets.SphereOffset;
			uint32 CapsuleCount = OutAssetArrays->ElementOffsets.CapsuleOffset;

			for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
			{				
				AActor* Actor = Actors[ActorIndex];
				
				if (Actor != nullptr && Actor->GetComponentByClass(UStaticMeshComponent::StaticClass()) != nullptr)
				{
					UStaticMeshComponent* StaticMeshComponent = Cast< UStaticMeshComponent>(Actor->GetComponentByClass(UStaticMeshComponent::StaticClass()));
					UBodySetup* BodySetup = StaticMeshComponent->GetBodySetup();
					bool FoundCollisionShapes = false;
					if (BodySetup != nullptr)
					{
						FTransform MeshTransform = Actor->GetTransform();
						MeshTransform.AddToTranslation(LWCTile * -FLargeWorldRenderScalar::GetTileSize());

						for (const FKConvexElem& ConvexElem : BodySetup->AggGeom.ConvexElems)
						{							
							if (CollisionEnabledHasPhysics(ConvexElem.GetCollisionEnabled()))
							{
								UE_LOG(LogRigidMeshCollision, Warning, TEXT("Convex collision objects encountered and will be interpreted as a bounding box on %s"), *Actor->GetName());


								FBox BBox = ConvexElem.ElemBox;
								FVector3f Extent = FVector3f(BBox.Max - BBox.Min);
								FVector Center = (BBox.Max + BBox.Min) * .5;
								OutAssetArrays->PhysicsType[BoxCount] = (ConvexElem.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
								OutAssetArrays->SourceSceneProxy[BoxCount] = StaticMeshComponent->SceneProxy;
								
								const FTransform ElementTransform = FTransform(Center) * MeshTransform;
								OutAssetArrays->ElementExtent[BoxCount] = FVector4f(Extent.X, Extent.Y, Extent.Z, 0);
								FillCurrentTransforms(ElementTransform, BoxCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);

								FoundCollisionShapes = true;
							}
						}
						for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
						{
							if (CollisionEnabledHasPhysics(BoxElem.GetCollisionEnabled()))
							{
								OutAssetArrays->PhysicsType[BoxCount] = (BoxElem.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
								OutAssetArrays->SourceSceneProxy[BoxCount] = StaticMeshComponent->SceneProxy;

								const FTransform ElementTransform = FTransform(BoxElem.Rotation, BoxElem.Center) * MeshTransform;
								OutAssetArrays->ElementExtent[BoxCount] = FVector4f(BoxElem.X, BoxElem.Y, BoxElem.Z, 0);
								FillCurrentTransforms(ElementTransform, BoxCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);

								FoundCollisionShapes = true;
							}
						}

						for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
						{
							if (CollisionEnabledHasPhysics(SphereElem.GetCollisionEnabled()))
							{
								OutAssetArrays->PhysicsType[SphereCount] = (SphereElem.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
								OutAssetArrays->SourceSceneProxy[BoxCount] = StaticMeshComponent->SceneProxy;

								const FTransform ElementTransform = FTransform(SphereElem.Center) * MeshTransform;
								OutAssetArrays->ElementExtent[SphereCount] = FVector4f(SphereElem.Radius, 0, 0, 0);
								FillCurrentTransforms(ElementTransform, SphereCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);

								FoundCollisionShapes = true;
							}
						}

						for (const FKSphylElem& CapsuleElem : BodySetup->AggGeom.SphylElems)
						{
							if (CollisionEnabledHasPhysics(CapsuleElem.GetCollisionEnabled()))
							{
								OutAssetArrays->PhysicsType[CapsuleCount] = (CapsuleElem.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
								OutAssetArrays->SourceSceneProxy[BoxCount] = StaticMeshComponent->SceneProxy;

								const FTransform ElementTransform = FTransform(CapsuleElem.Rotation, CapsuleElem.Center) * MeshTransform;
								OutAssetArrays->ElementExtent[CapsuleCount] = FVector4f(CapsuleElem.Radius, CapsuleElem.Length, 0, 0);
								FillCurrentTransforms(ElementTransform, CapsuleCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);

								FoundCollisionShapes = true;
							}
						}
					}

					if (!FoundCollisionShapes)
					{
						UE_LOG(LogRigidMeshCollision, Warning, TEXT("No useable collision body setup found on mesh %s"), *Actor->GetName());

					}
				}
			}
			OutAssetArrays->PreviousTransform = OutAssetArrays->CurrentTransform;
			OutAssetArrays->PreviousInverse = OutAssetArrays->CurrentInverse;

			CompactInternalArrays(OutAssetArrays);
		}
		else
		{
			UE_LOG(LogRigidMeshCollision, Warning, TEXT("Number of Collision DI primitives is higher than the %d limit.  Please increase it."), OutAssetArrays->MaxPrimitives);
		}
	}
}

void UpdateInternalArrays(const TArray<AActor*> &Actors, FNDIRigidMeshCollisionArrays* OutAssetArrays, FVector LWCTile)
{
	if (OutAssetArrays != nullptr && OutAssetArrays->ElementOffsets.NumElements < OutAssetArrays->MaxPrimitives)
	{
		uint32 NumBoxes = 0;
		uint32 NumSpheres = 0;
		uint32 NumCapsules = 0;		

		GetNumPrimitives(Actors, NumBoxes, NumSpheres, NumCapsules);

		if (((OutAssetArrays->ElementOffsets.SphereOffset - OutAssetArrays->ElementOffsets.BoxOffset) != NumBoxes) || 
			((OutAssetArrays->ElementOffsets.CapsuleOffset - OutAssetArrays->ElementOffsets.SphereOffset) != NumSpheres) ||
			((OutAssetArrays->ElementOffsets.NumElements - OutAssetArrays->ElementOffsets.CapsuleOffset) != NumCapsules))
		{
			CreateInternalArrays(Actors, OutAssetArrays, LWCTile);
		}

		OutAssetArrays->PreviousTransform = OutAssetArrays->CurrentTransform;
		OutAssetArrays->PreviousInverse = OutAssetArrays->CurrentInverse;

		uint32 BoxCount = OutAssetArrays->ElementOffsets.BoxOffset;
		uint32 SphereCount = OutAssetArrays->ElementOffsets.SphereOffset;
		uint32 CapsuleCount = OutAssetArrays->ElementOffsets.CapsuleOffset;

		for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
		{
			AActor* Actor = Actors[ActorIndex];
			UStaticMeshComponent* StaticMeshComponent = nullptr;
			if (Actor != nullptr)
			{
				StaticMeshComponent = Cast<UStaticMeshComponent>(Actor->GetComponentByClass(UStaticMeshComponent::StaticClass()));
			}
			UBodySetup* BodySetup = StaticMeshComponent != nullptr ? StaticMeshComponent->GetBodySetup() : nullptr;
			if (BodySetup != nullptr)
			{
				FTransform MeshTransform = Actor->GetTransform();
				MeshTransform.AddToTranslation(LWCTile * -FLargeWorldRenderScalar::GetTileSize());

				for (const FKConvexElem& ConvexElem : BodySetup->AggGeom.ConvexElems)
				{
					if (CollisionEnabledHasPhysics(ConvexElem.GetCollisionEnabled()))
					{
						FBox BBox = ConvexElem.ElemBox;												
						FVector Center = (BBox.Max + BBox.Min) * .5;

						OutAssetArrays->SourceSceneProxy[BoxCount] = StaticMeshComponent->SceneProxy;
						const FTransform ElementTransform = FTransform(Center) * MeshTransform;
						FillCurrentTransforms(ElementTransform, BoxCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
					}
				}

				for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
				{
					if (CollisionEnabledHasPhysics(BoxElem.GetCollisionEnabled()))
					{
						OutAssetArrays->SourceSceneProxy[BoxCount] = StaticMeshComponent->SceneProxy;
						const FTransform ElementTransform = FTransform(BoxElem.Rotation, BoxElem.Center) * MeshTransform;
						FillCurrentTransforms(ElementTransform, BoxCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
					}
				}

				for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
				{
					if (CollisionEnabledHasPhysics(SphereElem.GetCollisionEnabled()))
					{
						OutAssetArrays->SourceSceneProxy[SphereCount] = StaticMeshComponent->SceneProxy;
						const FTransform ElementTransform = FTransform(SphereElem.Center) * MeshTransform;
						FillCurrentTransforms(ElementTransform, SphereCount, OutAssetArrays->CurrentTransform, OutAssetArrays->CurrentInverse);
					}
				}

				for (const FKSphylElem& CapsuleElem : BodySetup->AggGeom.SphylElems)
				{
					if (CollisionEnabledHasPhysics(CapsuleElem.GetCollisionEnabled()))
					{
						OutAssetArrays->SourceSceneProxy[CapsuleCount] = StaticMeshComponent->SceneProxy;
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
	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(WorldTransformBuffer, 3 * MaxNumTransforms);
	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(InverseTransformBuffer, 3 * MaxNumTransforms);

	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(ElementExtentBuffer, MaxNumPrimitives);
	CreateInternalBuffer<uint32, EPixelFormat::PF_R32_UINT>(PhysicsTypeBuffer, MaxNumPrimitives);
	CreateInternalBuffer<uint32, EPixelFormat::PF_R32_UINT>(DFIndexBuffer, MaxNumPrimitives);
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

		Actors.Empty();

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;

			if ((!Interface->OnlyUseMoveable || (Interface->OnlyUseMoveable && Actor->IsRootComponentMovable())) &&
				(Interface->Tag == FString("") || (Interface->Tag != FString("") && Actor->Tags.Contains(FName(Interface->Tag)))))
			{
				Actors.Add(Actor);
			}
		}	

		if (0 < Actors.Num() && Actors[0] != nullptr)
		{
			// @note: we are not creating internal arrays here and letting it happen on the call to update
			 //CreateInternalArrays(Actors, &AssetArrays);
		}
		AssetArrays = new FNDIRigidMeshCollisionArrays();
		AssetArrays->Resize(Interface->MaxNumPrimitives);

		AssetBuffer = new FNDIRigidMeshCollisionBuffer();
		AssetBuffer->SetMaxNumPrimitives(Interface->MaxNumPrimitives);		
		BeginInitResource(AssetBuffer);
	}
}

void FNDIRigidMeshCollisionData::Update(UNiagaraDataInterfaceRigidMeshCollisionQuery* Interface, FNiagaraSystemInstance* SystemInstance)
{
	if (Interface != nullptr && SystemInstance != nullptr)
	{		
		TArray<AActor*> ActorsTmp;		


		// #todo(dmp): loop over actors and count number of potential colliders.  This might change frame to frame and we might need a
		// better way of doing this
		UWorld* World = SystemInstance->GetWorld();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;

			if ((!Interface->OnlyUseMoveable || (Interface->OnlyUseMoveable && Actor->IsRootComponentMovable())) &&
				(Interface->Tag == FString("") || (Interface->Tag != FString("") && Actor->Tags.Contains(FName(Interface->Tag)))))
			{
				ActorsTmp.Add(Actor);
			}
		}
		

		// check if the number of active actors is the same.  If not, reinitialize
		if (ActorsTmp.Num() != Actors.Num())
		{
			Init(Interface, SystemInstance);
		}

		TickingGroup = ComputeTickingGroup();

		if (0 < Actors.Num() && Actors[0] != nullptr)
		{
			UpdateInternalArrays(Actors, AssetArrays, FVector(SystemInstance->GetLWCTile()));
		}
	}
}

ETickingGroup FNDIRigidMeshCollisionData::ComputeTickingGroup()
{
	TickingGroup = NiagaraFirstTickGroup;
	for (int32 ComponentIndex = 0; ComponentIndex < Actors.Num(); ++ComponentIndex)
	{
		if (Actors[ComponentIndex] != nullptr)
		{			
		
			UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Actors[ComponentIndex]->GetComponentByClass(UStaticMeshComponent::StaticClass()));

			if (Component == nullptr)
			{
				continue;
			}
				const ETickingGroup ComponentTickGroup = FMath::Max(Component->PrimaryComponentTick.TickGroup, Component->PrimaryComponentTick.EndTickGroup);
				const ETickingGroup PhysicsTickGroup = ComponentTickGroup;
				const ETickingGroup ClampedTickGroup = FMath::Clamp(static_cast<ETickingGroup>(PhysicsTickGroup + 1), NiagaraFirstTickGroup, NiagaraLastTickGroup);		

			TickingGroup = FMath::Max(TickingGroup, ClampedTickGroup);
		}
	}
	return TickingGroup;
}

//------------------------------------------------------------------------------------------------------------

struct FNDIRigidMeshCollisionParametersCS : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNDIRigidMeshCollisionParametersCS, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FNDIRigidMeshCollisionParametersName ParamNames(*ParameterInfo.DataInterfaceHLSLSymbol);

		MaxTransforms.Bind(ParameterMap, *ParamNames.MaxTransformsName);
		CurrentOffset.Bind(ParameterMap, *ParamNames.CurrentOffsetName);
		PreviousOffset.Bind(ParameterMap, *ParamNames.PreviousOffsetName);

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


			SetShaderValue(RHICmdList, ComputeShaderRHI, MaxTransforms, ProxyData->AssetArrays->MaxTransforms);
			SetShaderValue(RHICmdList, ComputeShaderRHI, CurrentOffset, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, PreviousOffset, ProxyData->AssetArrays->MaxTransforms);

			SetShaderValue(RHICmdList, ComputeShaderRHI, ElementOffsets, ProxyData->AssetArrays->ElementOffsets);

			if (DistanceFieldParameters.IsBound())
			{
				const FDistanceFieldSceneData *DistanceFieldSceneData = static_cast<const FNiagaraGpuComputeDispatch*>(Context.ComputeDispatchInterface)->GetMeshDistanceFieldParameters();	//-BATCHERTODO:

				if (DistanceFieldSceneData == nullptr)
				{
					// UE_LOG(LogRigidMeshCollision, Error, TEXT("Distance fields are not available for use"));
					// #todo(dmp): for now, we'll disable collisions when distance field data is not available
					// There is no Dummy distance field data we can use.
									
					//FDistanceFieldSceneData DummyDistanceFieldSceneData(Context.Shader->GetShaderPlatform());
					//DistanceFieldParameters.SetEmpty(RHICmdList, ComputeShaderRHI, DummyDistanceFieldSceneData);

					SetSRVParameter(RHICmdList, ComputeShaderRHI, WorldTransformBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
					SetSRVParameter(RHICmdList, ComputeShaderRHI, InverseTransformBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
					SetSRVParameter(RHICmdList, ComputeShaderRHI, ElementExtentBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
					SetSRVParameter(RHICmdList, ComputeShaderRHI, PhysicsTypeBuffer, FNiagaraRenderer::GetDummyIntBuffer());
					SetSRVParameter(RHICmdList, ComputeShaderRHI, DFIndexBuffer, FNiagaraRenderer::GetDummyIntBuffer());

					static const FElementOffset DummyOffsets(0, 0, 0, 0);
					SetShaderValue(RHICmdList, ComputeShaderRHI, MaxTransforms, 0);
					SetShaderValue(RHICmdList, ComputeShaderRHI, CurrentOffset, 0);
					SetShaderValue(RHICmdList, ComputeShaderRHI, PreviousOffset, 0);
					SetShaderValue(RHICmdList, ComputeShaderRHI, ElementOffsets, DummyOffsets);
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
			SetShaderValue(RHICmdList, ComputeShaderRHI, MaxTransforms, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, CurrentOffset, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, PreviousOffset, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ElementOffsets, DummyOffsets);	
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
	}

private:

	LAYOUT_FIELD(FShaderParameter, MaxTransforms);
	LAYOUT_FIELD(FShaderParameter, CurrentOffset);
	LAYOUT_FIELD(FShaderParameter, PreviousOffset);

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

		for (uint32 i = 0; i < SourceData->AssetArrays->ElementOffsets.NumElements; ++i)
		{
			FPrimitiveSceneProxy* Proxy = SourceData->AssetArrays->SourceSceneProxy[i];
								
			if (Proxy != nullptr && Proxy->GetPrimitiveSceneInfo() != nullptr)
			{
				const TArray<int32, TInlineAllocator<1>>& DFIndices = Proxy->GetPrimitiveSceneInfo()->DistanceFieldInstanceIndices;				
				TargetData->AssetArrays->DFIndex[i] = DFIndices.Num() > 0 ? DFIndices[0] : -1;
			}
			else
			{
				TargetData->AssetArrays->DFIndex[i] = -1;
			}
		}	
	}
	else
	{
		UE_LOG(LogRigidMeshCollision, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %d"), Instance);
	}
	SourceData->~FNDIRigidMeshCollisionData();
}

void FNDIRigidMeshCollisionProxy::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	check(!SystemInstancesToProxyData.Contains(SystemInstance));
	SystemInstancesToProxyData.Add(SystemInstance);
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

			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(ProxyData->AssetArrays->WorldTransform, ProxyData->AssetBuffer->WorldTransformBuffer);
			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(ProxyData->AssetArrays->InverseTransform, ProxyData->AssetBuffer->InverseTransformBuffer);
			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(ProxyData->AssetArrays->ElementExtent, ProxyData->AssetBuffer->ElementExtentBuffer);
			UpdateInternalBuffer<uint32, EPixelFormat::PF_R32_UINT>(ProxyData->AssetArrays->PhysicsType, ProxyData->AssetBuffer->PhysicsTypeBuffer);
			UpdateInternalBuffer<uint32, EPixelFormat::PF_R32_UINT>(ProxyData->AssetArrays->DFIndex, ProxyData->AssetBuffer->DFIndexBuffer);
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
	if (const FNDIRigidMeshCollisionData* InstanceData = static_cast<const FNDIRigidMeshCollisionData*>(PerInstanceData))
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
		FNDIRigidMeshCollisionData* ProxyData =
			ThisProxy->SystemInstancesToProxyData.Find(InstanceID);

		if (ProxyData != nullptr && ProxyData->AssetArrays)
		{			
			ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
			delete ProxyData->AssetArrays;
		}		
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

	return  (OtherTyped->Tag == Tag) && (OtherTyped->OnlyUseMoveable == OnlyUseMoveable);
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
		Sig.SetDescription(LOCTEXT("GetNumBoxesNameDescription", "Returns the number of box primitives for the collection of static meshes the DI represents."));
		Sig.SetFunctionVersion(FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion);
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Boxes")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumSpheresName;
		Sig.SetDescription(LOCTEXT("GetNumSpheresNameDescription", "Returns the number of sphere primitives for the collection of static meshes the DI represents."));
		Sig.SetFunctionVersion(FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion);
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Spheres")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumCapsulesName;
		Sig.SetDescription(LOCTEXT("GetNumCapsulesNameDescription", "Returns the number of capsule primitives for the collection of static meshes the DI represents."));
		Sig.SetFunctionVersion(FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion);
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Capsules")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestPointName;
		Sig.SetDescription(LOCTEXT("GetClosestPointDescription", "Given a world space position, computes the static mesh's closest point. Also returns normal and velocity for that point."));
		Sig.SetFunctionVersion(FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion);
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Closest Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Velocity")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestElementName;
		Sig.SetDescription(LOCTEXT("GetClosestElementDescription", "Given a world space position, computes the static mesh's closest element."));
		Sig.SetFunctionVersion(FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion);
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Closest Element")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetElementPointName;
		Sig.SetDescription(LOCTEXT("GetClosestElementPointDescription", "Given a world space position and an element index, computes the static mesh's closest point. Also returns normal and velocity for that point."));
		Sig.SetFunctionVersion(FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion);
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Element Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Closest Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Velocity")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetElementDistanceName;
		Sig.SetDescription(LOCTEXT("GetElementDistanceDescription", "Given a world space position and element index, computes the distance to the closest point for the static mesh."));
		Sig.SetFunctionVersion(FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion);
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Element Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestDistanceName;
		Sig.SetDescription(LOCTEXT("GetClosestDistanceDescription", "Given a world space position, computes the distance to the closest point for the static mesh."));
		Sig.SetFunctionVersion(FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion);
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));

		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestPointMeshDistanceFieldName;
		Sig.SetDescription(LOCTEXT("GetClosestPointMeshDistanceFieldDescription", "Given a world space position, computes the distance to the closest point for the static mesh, using the mesh's distance field."));
		Sig.SetFunctionVersion(FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion);
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Closest Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Normal")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Velocity")));

		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestPointMeshDistanceFieldNoNormalName;
		Sig.SetDescription(LOCTEXT("GetClosestPointMeshDistanceFieldNNDescription", "Given a world space position, computes the distance to the closest point for the static mesh, using the mesh's distance field.\nSkips the normal calculation and is more performant than it's counterpart with normal."));
		Sig.SetFunctionVersion(FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion);
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.bMemberFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Collision DI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time Fraction")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Closest Distance")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Closest Position")));		
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
		{TEXT("MaxTransformsName"), ParamNames.MaxTransformsName},
		{TEXT("CurrentOffsetName"), ParamNames.CurrentOffsetName},
		{TEXT("PreviousOffsetName"), ParamNames.PreviousOffsetName},
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
	else if (FunctionInfo.DefinitionName == GetClosestPointMeshDistanceFieldNoNormalName)
	{
	static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in float DeltaTime, in float TimeFraction, out float ClosestDistance, out float3 OutClosestPosition, 
							out float3 OutClosestVelocity)
		{
			{RigidMeshCollisionContextName} DIRigidMeshCollision_GetClosestPointMeshDistanceFieldNoNormal(DIContext,WorldPosition,DeltaTime,TimeFraction, ClosestDistance,
				OutClosestPosition,OutClosestVelocity);
		}
		)");
	OutHLSL += FString::Format(FormatSample, ArgsSample);
	return true;
	}
	OutHLSL += TEXT("\n");
	return false;
}

bool UNiagaraDataInterfaceRigidMeshCollisionQuery::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool bChanged = false;
	
	// upgrade from lwc changes, only parameter types changed there
	if (FunctionSignature.FunctionVersion < FNiagaraRigidMeshCollisionDIFunctionVersion::LargeWorldCoordinates)
	{
		if (FunctionSignature.Name == GetClosestPointName && ensure(FunctionSignature.Inputs.Num() == 4) && ensure(FunctionSignature.Outputs.Num() == 4))
		{
			FunctionSignature.Inputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			FunctionSignature.Outputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			bChanged = true;
		}
		if (FunctionSignature.Name == GetClosestElementName && ensure(FunctionSignature.Inputs.Num() == 3))
		{
			FunctionSignature.Inputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			bChanged = true;
		}
		if (FunctionSignature.Name == GetElementPointName && ensure(FunctionSignature.Inputs.Num() == 5) && ensure(FunctionSignature.Outputs.Num() == 3))
		{
			FunctionSignature.Inputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			FunctionSignature.Outputs[0].SetType(FNiagaraTypeDefinition::GetPositionDef());
			bChanged = true;
		}
		if (FunctionSignature.Name == GetElementDistanceName && ensure(FunctionSignature.Inputs.Num() == 4))
		{
			FunctionSignature.Inputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			bChanged = true;
		}
		if (FunctionSignature.Name == GetClosestDistanceName && ensure(FunctionSignature.Inputs.Num() == 3))
		{
			FunctionSignature.Inputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			bChanged = true;
		}
		if (FunctionSignature.Name == GetClosestPointMeshDistanceFieldName && ensure(FunctionSignature.Inputs.Num() == 4) && ensure(FunctionSignature.Outputs.Num() == 4))
		{
			FunctionSignature.Inputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			FunctionSignature.Outputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			bChanged = true;
		}
		if (FunctionSignature.Name == GetClosestPointMeshDistanceFieldNoNormalName && ensure(FunctionSignature.Inputs.Num() == 4) && ensure(FunctionSignature.Outputs.Num() == 3))
		{
			FunctionSignature.Inputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			FunctionSignature.Outputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			bChanged = true;
		}
	}
	FunctionSignature.FunctionVersion = FNiagaraRigidMeshCollisionDIFunctionVersion::LatestVersion;

	return bChanged;
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
	if (Function.Name == GetClosestPointMeshDistanceFieldName || Function.Name == GetClosestPointMeshDistanceFieldNoNormalName)
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
	FNDIRigidMeshCollisionData* RenderThreadData = new(DataForRenderThread) FNDIRigidMeshCollisionData();

	if (GameThreadData != nullptr && RenderThreadData != nullptr)
	{		
		RenderThreadData->AssetBuffer = GameThreadData->AssetBuffer;				

		RenderThreadData->AssetArrays = new FNDIRigidMeshCollisionArrays();
		RenderThreadData->AssetArrays->CopyFrom(GameThreadData->AssetArrays);		
		RenderThreadData->TickingGroup = GameThreadData->TickingGroup;
	}
	check(Proxy);
}

#undef LOCTEXT_NAMESPACE