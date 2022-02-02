// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceGeometryCollection.h"
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
#include "Renderer/Private/ScenePrivate.h"
#include "DistanceFieldAtlas.h"
#include "Renderer/Private/DistanceFieldLightingShared.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "GeometryCollection/GeometryCollectionComponent.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceGeometryCollection"
DEFINE_LOG_CATEGORY_STATIC(LogGeometryCollection, Log, All);

//------------------------------------------------------------------------------------------------------------

static const FName GetClosestPointNoNormalName(TEXT("GetClosestPointNoNormal"));


//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfaceGeometryCollection::BoundsMinName(TEXT("BoundsMin_"));
const FString UNiagaraDataInterfaceGeometryCollection::BoundsMaxName(TEXT("BoundsMax_"));
const FString UNiagaraDataInterfaceGeometryCollection::NumPiecesName(TEXT("NumPieces_"));
const FString UNiagaraDataInterfaceGeometryCollection::WorldTransformBufferName(TEXT("WorldTransformBuffer_"));
const FString UNiagaraDataInterfaceGeometryCollection::PrevWorldTransformBufferName(TEXT("PrevWorldTransformBuffer_"));
const FString UNiagaraDataInterfaceGeometryCollection::WorldInverseTransformBufferName(TEXT("WorldInverseTransformBuffer_"));
const FString UNiagaraDataInterfaceGeometryCollection::PrevWorldInverseTransformBufferName(TEXT("PrevWorldInverseTransformBuffer_"));
const FString UNiagaraDataInterfaceGeometryCollection::BoundsBufferName(TEXT("BoundsBuffer_"));


//------------------------------------------------------------------------------------------------------------

struct FNDIGeometryCollectionParametersName
{
	FNDIGeometryCollectionParametersName(const FString& Suffix)
	{
		BoundsMinName = UNiagaraDataInterfaceGeometryCollection::BoundsMinName + Suffix;
		BoundsMaxName = UNiagaraDataInterfaceGeometryCollection::BoundsMaxName + Suffix;
		NumPiecesName = UNiagaraDataInterfaceGeometryCollection::NumPiecesName + Suffix;
		WorldTransformBufferName = UNiagaraDataInterfaceGeometryCollection::WorldTransformBufferName + Suffix;
		PrevWorldTransformBufferName = UNiagaraDataInterfaceGeometryCollection::PrevWorldTransformBufferName + Suffix;
		WorldInverseTransformBufferName = UNiagaraDataInterfaceGeometryCollection::WorldInverseTransformBufferName + Suffix;
		PrevWorldInverseTransformBufferName = UNiagaraDataInterfaceGeometryCollection::PrevWorldInverseTransformBufferName + Suffix;
		BoundsBufferName = UNiagaraDataInterfaceGeometryCollection::BoundsBufferName + Suffix;
	}

	FString BoundsMinName;
	FString BoundsMaxName;

	FString NumPiecesName;
	FString WorldTransformBufferName;
	FString PrevWorldTransformBufferName;
	FString WorldInverseTransformBufferName;
	FString PrevWorldInverseTransformBufferName;
	FString BoundsBufferName;
};

//------------------------------------------------------------------------------------------------------------

template<typename BufferType, EPixelFormat PixelFormat>
void CreateInternalBuffer(FRWBuffer& OutputBuffer, uint32 ElementCount)
{
	if (ElementCount > 0)
	{
		OutputBuffer.Initialize(TEXT("FNDIGeometryCollectionBuffer"), sizeof(BufferType), ElementCount, PixelFormat, BUF_Static);
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


//------------------------------------------------------------------------------------------------------------

void FNDIGeometryCollectionBuffer::InitRHI()
{
	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(WorldTransformBuffer, 3 * NumPieces);
	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(PrevWorldTransformBuffer, 3 * NumPieces);

	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(WorldInverseTransformBuffer, 3 * NumPieces);
	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(PrevWorldInverseTransformBuffer, 3 * NumPieces);

	CreateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(BoundsBuffer, NumPieces);
}

void FNDIGeometryCollectionBuffer::ReleaseRHI()
{
	WorldTransformBuffer.Release();
	PrevWorldTransformBuffer.Release();
	WorldInverseTransformBuffer.Release();
	PrevWorldInverseTransformBuffer.Release();
	BoundsBuffer.Release();
}

//------------------------------------------------------------------------------------------------------------



void FNDIGeometryCollectionData::Release()
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

void FNDIGeometryCollectionData::Init(UNiagaraDataInterfaceGeometryCollection* Interface, FNiagaraSystemInstance* SystemInstance)
{
	AssetBuffer = nullptr;

	if (Interface != nullptr && SystemInstance != nullptr)
	{		
		if (Interface->GeometryCollectionActor != nullptr && Interface->GeometryCollectionActor->GetGeometryCollectionComponent() != nullptr)
		{			
			const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe>
				Collection = Interface->GeometryCollectionActor->GetGeometryCollectionComponent()->RestCollection->GetGeometryCollection();
			const TManagedArray<FBox>& BoundingBoxes = Collection->BoundingBox;
			const TManagedArray<int32>& TransformIndex = Collection->TransformIndex;
			const TManagedArray<FTransform>& Transforms = Interface->GeometryCollectionActor->GetGeometryCollectionComponent()->GetTransformArray();
			int NumPieces = BoundingBoxes.Num();

			AssetArrays = new FNDIGeometryCollectionArrays();
			AssetArrays->Resize(NumPieces);

			AssetBuffer = new FNDIGeometryCollectionBuffer();
			AssetBuffer->SetNumPieces(NumPieces);
			BeginInitResource(AssetBuffer);

			FVector Origin;
			FVector Extents;
			Interface->GeometryCollectionActor->GetActorBounds(false, Origin, Extents, true);

			BoundsOrigin = (FVector3f)Origin;	// LWC_TODO: Precision Loss
			BoundsExtent = (FVector3f)Extents;	// LWC_TODO: Precision Loss

			for (int i = 0; i < NumPieces; ++i)
			{
				FBox CurrBox = BoundingBoxes[i];
				FVector3f BoxSize = FVector3f(CurrBox.Max - CurrBox.Min);
				AssetArrays->BoundsBuffer[i] = FVector4f(BoxSize.X, BoxSize.Y, BoxSize.Z, 0);
			}
		}
		else
		{
			AssetArrays = new FNDIGeometryCollectionArrays();
			AssetArrays->Resize(1);

			AssetBuffer = new FNDIGeometryCollectionBuffer();
			AssetBuffer->SetNumPieces(1);
			BeginInitResource(AssetBuffer);
		}
	}

}

void FNDIGeometryCollectionData::Update(UNiagaraDataInterfaceGeometryCollection* Interface, FNiagaraSystemInstance* SystemInstance)
{
	if (Interface != nullptr && SystemInstance != nullptr)
	{		
		TickingGroup = ComputeTickingGroup();
		
		if (Interface->GeometryCollectionActor != nullptr && Interface->GeometryCollectionActor->GetGeometryCollectionComponent() != nullptr &&
			Interface->GeometryCollectionActor->GetGeometryCollectionComponent()->RestCollection != nullptr)
		{			
			const FTransform ActorTransform = Interface->GeometryCollectionActor->GetTransform();

			const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe>
				Collection = Interface->GeometryCollectionActor->GetGeometryCollectionComponent()->RestCollection->GetGeometryCollection();
			const TManagedArray<FBox>& BoundingBoxes = Collection->BoundingBox;
			const TManagedArray<int32>& TransformIndexArray = Collection->TransformIndex;
			const TManagedArray<FTransform>& Transforms = Interface->GeometryCollectionActor->GetGeometryCollectionComponent()->GetTransformArray();
			const TArray<FMatrix>& GlobalMatrices = Interface->GeometryCollectionActor->GetGeometryCollectionComponent()->GetGlobalMatrices();

			// #todo(dmp): rename transform to pieces or something
			int32 NumPieces = BoundingBoxes.Num();

			if (GlobalMatrices.Num() != Transforms.Num())
			{
				return;
			}

			if (NumPieces != AssetArrays->BoundsBuffer.Num())
			{
				Init(Interface, SystemInstance);
			}
			
			FVector Origin;
			FVector Extents;
			Interface->GeometryCollectionActor->GetActorBounds(false, Origin, Extents, true);

			BoundsOrigin = (FVector3f)Origin;
			BoundsExtent = (FVector3f)Extents;

			for (int i = 0; i < NumPieces; ++i)
			{
				int32 TransformIndex = 3 * i;
				AssetArrays->PrevWorldInverseTransformBuffer[TransformIndex] = AssetArrays->WorldInverseTransformBuffer[TransformIndex];
				AssetArrays->PrevWorldInverseTransformBuffer[TransformIndex + 1] = AssetArrays->WorldInverseTransformBuffer[TransformIndex + 1];
				AssetArrays->PrevWorldInverseTransformBuffer[TransformIndex + 2] = AssetArrays->WorldInverseTransformBuffer[TransformIndex + 2];

				AssetArrays->PrevWorldTransformBuffer[TransformIndex] = AssetArrays->WorldTransformBuffer[TransformIndex];
				AssetArrays->PrevWorldTransformBuffer[TransformIndex + 1] = AssetArrays->WorldTransformBuffer[TransformIndex + 1];
				AssetArrays->PrevWorldTransformBuffer[TransformIndex + 2] = AssetArrays->WorldTransformBuffer[TransformIndex + 2];

				FBox CurrBox = BoundingBoxes[i];

				// #todo(dmp): save this somewhere in an array?
				FVector LocalTranslation = (CurrBox.Max + CurrBox.Min) * .5;
				FTransform LocalOffset(LocalTranslation);
								
				int32 CurrTransformIndex = TransformIndexArray[i];

				FMatrix44f CurrTransform = FMatrix44f(LocalOffset.ToMatrixWithScale() * GlobalMatrices[CurrTransformIndex] * ActorTransform.ToMatrixWithScale());
				CurrTransform.To3x4MatrixTranspose(&AssetArrays->WorldTransformBuffer[TransformIndex].X);

				FMatrix44f CurrInverse = CurrTransform.Inverse();
				CurrInverse.To3x4MatrixTranspose(&AssetArrays->WorldInverseTransformBuffer[TransformIndex].X);
			}
		}		
	}
}

ETickingGroup FNDIGeometryCollectionData::ComputeTickingGroup()
{
	TickingGroup = NiagaraFirstTickGroup;
	return TickingGroup;
}


//------------------------------------------------------------------------------------------------------------

struct FNDIGeometryCollectionParametersCS : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNDIGeometryCollectionParametersCS, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FNDIGeometryCollectionParametersName ParamNames(*ParameterInfo.DataInterfaceHLSLSymbol);

		BoundsMin.Bind(ParameterMap, *ParamNames.BoundsMinName);
		BoundsMax.Bind(ParameterMap, *ParamNames.BoundsMaxName);

		NumPieces.Bind(ParameterMap, *ParamNames.NumPiecesName);
		WorldTransformBuffer.Bind(ParameterMap, *ParamNames.WorldTransformBufferName);
		PrevWorldTransformBuffer.Bind(ParameterMap, *ParamNames.PrevWorldTransformBufferName);
		WorldInverseTransformBuffer.Bind(ParameterMap, *ParamNames.WorldInverseTransformBufferName);
		PrevWorldInverseTransformBuffer.Bind(ParameterMap, *ParamNames.PrevWorldInverseTransformBufferName);
		BoundsBuffer.Bind(ParameterMap, *ParamNames.BoundsBufferName);
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		FNDIGeometryCollectionProxy* InterfaceProxy =
			static_cast<FNDIGeometryCollectionProxy*>(Context.DataInterface);
		FNDIGeometryCollectionData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstanceID);

		if (ProxyData != nullptr)
		{

			FNDIGeometryCollectionBuffer* AssetBuffer = ProxyData->AssetBuffer;

			FRHITransitionInfo Transitions[] = {
				FRHITransitionInfo(AssetBuffer->WorldTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				FRHITransitionInfo(AssetBuffer->PrevWorldTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				FRHITransitionInfo(AssetBuffer->WorldInverseTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				FRHITransitionInfo(AssetBuffer->PrevWorldInverseTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				FRHITransitionInfo(AssetBuffer->BoundsBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
			};
			RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));		
			
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoundsMin, ProxyData->BoundsOrigin - ProxyData->BoundsExtent);
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoundsMax, ProxyData->BoundsOrigin + ProxyData->BoundsExtent);

			SetShaderValue(RHICmdList, ComputeShaderRHI, NumPieces, ProxyData->AssetBuffer->NumPieces);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, WorldTransformBuffer, ProxyData->AssetBuffer->WorldTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PrevWorldTransformBuffer, ProxyData->AssetBuffer->PrevWorldTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, WorldInverseTransformBuffer, ProxyData->AssetBuffer->WorldInverseTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PrevWorldInverseTransformBuffer, ProxyData->AssetBuffer->PrevWorldInverseTransformBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, BoundsBuffer, ProxyData->AssetBuffer->BoundsBuffer.SRV);
		}
		else
		{
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoundsMin, FVector3f(0, 0, 0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoundsMax, FVector3f(0, 0, 0));

			SetShaderValue(RHICmdList, ComputeShaderRHI, NumPieces, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, WorldTransformBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PrevWorldTransformBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, WorldInverseTransformBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PrevWorldInverseTransformBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, BoundsBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
	}

private:

	LAYOUT_FIELD(FShaderParameter, BoundsMin);
	LAYOUT_FIELD(FShaderParameter, BoundsMax);

	LAYOUT_FIELD(FShaderParameter, NumPieces);

	LAYOUT_FIELD(FShaderResourceParameter, WorldTransformBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, PrevWorldTransformBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, WorldInverseTransformBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, PrevWorldInverseTransformBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, BoundsBuffer);
};

IMPLEMENT_TYPE_LAYOUT(FNDIGeometryCollectionParametersCS);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceGeometryCollection, FNDIGeometryCollectionParametersCS);


//------------------------------------------------------------------------------------------------------------

void FNDIGeometryCollectionProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	check(IsInRenderingThread());

	FNDIGeometryCollectionData* SourceData = static_cast<FNDIGeometryCollectionData*>(PerInstanceData);	
	FNDIGeometryCollectionData* TargetData = &(SystemInstancesToProxyData.FindOrAdd(Instance));

	ensure(TargetData);
	if (TargetData)
	{
		TargetData->AssetBuffer = SourceData->AssetBuffer;		
		TargetData->AssetArrays = SourceData->AssetArrays;
		TargetData->TickingGroup = SourceData->TickingGroup;
		TargetData->BoundsOrigin = SourceData->BoundsOrigin;
		TargetData->BoundsExtent = SourceData->BoundsExtent;
	}
	else
	{
		UE_LOG(LogGeometryCollection, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %d"), Instance);
	}
}

void FNDIGeometryCollectionProxy::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	check(!SystemInstancesToProxyData.Contains(SystemInstance));
	FNDIGeometryCollectionData* TargetData = &SystemInstancesToProxyData.Add(SystemInstance);
}

void FNDIGeometryCollectionProxy::DestroyPerInstanceData(const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());	

	SystemInstancesToProxyData.Remove(SystemInstance);
}

void FNDIGeometryCollectionProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	check(SystemInstancesToProxyData.Contains(Context.SystemInstanceID));

	FNDIGeometryCollectionData* ProxyData =
		SystemInstancesToProxyData.Find(Context.SystemInstanceID);

	if (ProxyData != nullptr && ProxyData->AssetBuffer)
	{
		if (Context.SimStageData->bFirstStage)
		{
			FNDIGeometryCollectionBuffer* AssetBuffer = ProxyData->AssetBuffer;

			FRHITransitionInfo Transitions[] = {
				FRHITransitionInfo(AssetBuffer->WorldTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AssetBuffer->PrevWorldTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AssetBuffer->WorldInverseTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AssetBuffer->PrevWorldInverseTransformBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(AssetBuffer->BoundsBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
			};
			RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));

			// #todo(dmp): bounds buffer doesn't need to be updated each frame
			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(ProxyData->AssetArrays->WorldTransformBuffer, ProxyData->AssetBuffer->WorldTransformBuffer);
			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(ProxyData->AssetArrays->PrevWorldTransformBuffer, ProxyData->AssetBuffer->PrevWorldTransformBuffer);
			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(ProxyData->AssetArrays->WorldInverseTransformBuffer, ProxyData->AssetBuffer->WorldInverseTransformBuffer);
			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(ProxyData->AssetArrays->PrevWorldInverseTransformBuffer, ProxyData->AssetBuffer->PrevWorldInverseTransformBuffer);
			UpdateInternalBuffer<FVector4f, EPixelFormat::PF_A32B32G32R32F>(ProxyData->AssetArrays->BoundsBuffer, ProxyData->AssetBuffer->BoundsBuffer);
		}
	}
}

void FNDIGeometryCollectionProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{}

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfaceGeometryCollection::UNiagaraDataInterfaceGeometryCollection(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)	
{
	Proxy.Reset(new FNDIGeometryCollectionProxy());
}

bool UNiagaraDataInterfaceGeometryCollection::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIGeometryCollectionData* InstanceData = new (PerInstanceData) FNDIGeometryCollectionData();

	check(InstanceData);
	InstanceData->Init(this, SystemInstance);
	
	return true;
}

ETickingGroup UNiagaraDataInterfaceGeometryCollection::CalculateTickGroup(const void* PerInstanceData) const
{
	const FNDIGeometryCollectionData* InstanceData = static_cast<const FNDIGeometryCollectionData*>(PerInstanceData);

	if (InstanceData)
	{
		return InstanceData->TickingGroup;
	}
	return NiagaraFirstTickGroup;
}

void UNiagaraDataInterfaceGeometryCollection::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIGeometryCollectionData* InstanceData = static_cast<FNDIGeometryCollectionData*>(PerInstanceData);

	InstanceData->Release();	
	InstanceData->~FNDIGeometryCollectionData();

	FNDIGeometryCollectionProxy* ThisProxy = GetProxyAs<FNDIGeometryCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& CmdList)
	{
		FNDIGeometryCollectionData* ProxyData =
			ThisProxy->SystemInstancesToProxyData.Find(InstanceID);

		if (ProxyData != nullptr && ProxyData->AssetArrays)
		{			
			ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
			delete ProxyData->AssetArrays;
		}		
	}
	);
}

bool UNiagaraDataInterfaceGeometryCollection::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIGeometryCollectionData* InstanceData = static_cast<FNDIGeometryCollectionData*>(PerInstanceData);
	if (InstanceData && InstanceData->AssetBuffer && SystemInstance)
	{
		InstanceData->Update(this, SystemInstance);
	}
	return false;
}

bool UNiagaraDataInterfaceGeometryCollection::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceGeometryCollection* OtherTyped = CastChecked<UNiagaraDataInterfaceGeometryCollection>(Destination);		

	OtherTyped->GeometryCollectionActor = GeometryCollectionActor;

	return true;
}

bool UNiagaraDataInterfaceGeometryCollection::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceGeometryCollection* OtherTyped = CastChecked<const UNiagaraDataInterfaceGeometryCollection>(Other);

	return  OtherTyped->GeometryCollectionActor == GeometryCollectionActor;
}

void UNiagaraDataInterfaceGeometryCollection::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

void UNiagaraDataInterfaceGeometryCollection::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetClosestPointNoNormalName;
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
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Closest Velocity")));

		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceGeometryCollection::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceGeometryCollection::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FNDIGeometryCollectionParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {

		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
		{TEXT("NumPiecesName"), ParamNames.NumPiecesName},
		{TEXT("WorldTransformBufferName"), ParamNames.WorldTransformBufferName},
		{TEXT("PrevWorldTransformBufferName"), ParamNames.PrevWorldTransformBufferName},
		{TEXT("WorldInverseTransformBufferName"), ParamNames.WorldInverseTransformBufferName},
		{TEXT("PrevWorldInverseTransformBufferName"), ParamNames.PrevWorldInverseTransformBufferName},
		{TEXT("BoundsBufferName"), ParamNames.BoundsBufferName},
		{TEXT("GeometryCollectionContextName"), TEXT("DIGEOMETRYCOLLECTION_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (FunctionInfo.DefinitionName == GetClosestPointNoNormalName)
	{
	static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in float DeltaTime, in float TimeFraction, out float ClosestDistance, out float3 OutClosestPosition, 
							out float3 OutClosestVelocity)
		{
			{GeometryCollectionContextName} DIGeometryCollection_GetClosestPointNoNormal(DIContext,WorldPosition,DeltaTime,TimeFraction, ClosestDistance,
				OutClosestPosition,OutClosestVelocity);
		}
		)");
	OutHLSL += FString::Format(FormatSample, ArgsSample);
	return true;
	}
	OutHLSL += TEXT("\n");
	return false;
}

void UNiagaraDataInterfaceGeometryCollection::GetCommonHLSL(FString& OutHLSL)
{	
	OutHLSL += TEXT("#include \"/Plugin/Experimental/ChaosNiagara/NiagaraDataInterfaceGeometryCollection.ush\"\n");			
}

void UNiagaraDataInterfaceGeometryCollection::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIGEOMETRYCOLLECTION_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}
#endif

#if WITH_EDITOR
void UNiagaraDataInterfaceGeometryCollection::ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors)
{
}
#endif

void UNiagaraDataInterfaceGeometryCollection::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIGeometryCollectionData* GameThreadData = static_cast<FNDIGeometryCollectionData*>(PerInstanceData);
	FNDIGeometryCollectionData* RenderThreadData = static_cast<FNDIGeometryCollectionData*>(DataForRenderThread);

	if (GameThreadData != nullptr && RenderThreadData != nullptr)
	{		
		RenderThreadData->AssetBuffer = GameThreadData->AssetBuffer;				

		RenderThreadData->AssetArrays = new FNDIGeometryCollectionArrays();
		RenderThreadData->AssetArrays->CopyFrom(GameThreadData->AssetArrays);		
		RenderThreadData->TickingGroup = GameThreadData->TickingGroup;
		RenderThreadData->BoundsOrigin = GameThreadData->BoundsOrigin;
		RenderThreadData->BoundsExtent = GameThreadData->BoundsExtent;
	}
	check(Proxy);
}

#undef LOCTEXT_NAMESPACE