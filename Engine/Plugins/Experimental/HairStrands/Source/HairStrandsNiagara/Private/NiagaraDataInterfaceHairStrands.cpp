// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceHairStrands.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "ShaderParameterUtils.h"
#include "HairStrandsAsset.h"  

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceHairStrands"

//------------------------------------------------------------------------------------------------------------

static const FName GetStrandDensityName(TEXT("GetStrandDensity"));
static const FName GetStrandSizeName(TEXT("GetStrandSize"));
static const FName GetNumStrandsName(TEXT("GetNumStrands"));
static const FName GetRootThicknessName(TEXT("GetRootThickness"));
static const FName GetTipThicknessName(TEXT("GetTipThickness"));
static const FName GetWorldTransformName(TEXT("GetWorldTransform"));
static const FName GetVertexPositionName(TEXT("GetStrandPosition"));

//------------------------------------------------------------------------------------------------------------

static const FName ComputeNodePositionName(TEXT("ComputeNodePosition"));

//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfaceHairStrands::NumStrandsName(TEXT("NumStrands_"));
const FString UNiagaraDataInterfaceHairStrands::StrandSizeName(TEXT("StrandSize_"));
const FString UNiagaraDataInterfaceHairStrands::StrandDensityName(TEXT("StrandDensity_"));
const FString UNiagaraDataInterfaceHairStrands::RootThicknessName(TEXT("RootThickness_"));
const FString UNiagaraDataInterfaceHairStrands::TipThicknessName(TEXT("TipThickness_"));
const FString UNiagaraDataInterfaceHairStrands::WorldTransformName(TEXT("WorldTransform_"));
const FString UNiagaraDataInterfaceHairStrands::PointsPositionsBufferName(TEXT("PointsPositionsBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::CurvesOffsetsBufferName(TEXT("CurvesOffsetsBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::NodesPositionsBufferName(TEXT("NodesPositionsBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::PointsNodesBufferName(TEXT("PointsNodesBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::PointsWeightsBufferName(TEXT("PointsWeightsBuffer_"));

//------------------------------------------------------------------------------------------------------------

struct FNDIHairStrandsParametersName
{
	FNDIHairStrandsParametersName(const FString& Suffix)
	{
		NumStrandsName = UNiagaraDataInterfaceHairStrands::NumStrandsName + Suffix;
		StrandSizeName = UNiagaraDataInterfaceHairStrands::StrandSizeName + Suffix;
		StrandDensityName = UNiagaraDataInterfaceHairStrands::StrandDensityName + Suffix;
		RootThicknessName = UNiagaraDataInterfaceHairStrands::RootThicknessName + Suffix;
		TipThicknessName = UNiagaraDataInterfaceHairStrands::TipThicknessName + Suffix;
		WorldTransformName = UNiagaraDataInterfaceHairStrands::WorldTransformName + Suffix;
		PointsPositionsBufferName = UNiagaraDataInterfaceHairStrands::PointsPositionsBufferName + Suffix;
		CurvesOffsetsBufferName = UNiagaraDataInterfaceHairStrands::CurvesOffsetsBufferName + Suffix;
		NodesPositionsBufferName = UNiagaraDataInterfaceHairStrands::NodesPositionsBufferName + Suffix;
		PointsNodesBufferName = UNiagaraDataInterfaceHairStrands::PointsNodesBufferName + Suffix;
		PointsWeightsBufferName = UNiagaraDataInterfaceHairStrands::PointsWeightsBufferName + Suffix;
	}

	FString NumStrandsName;
	FString StrandSizeName;
	FString StrandDensityName;
	FString RootThicknessName;
	FString TipThicknessName;
	FString WorldTransformName;
	FString PointsPositionsBufferName;
	FString CurvesOffsetsBufferName;
	FString NodesPositionsBufferName;
	FString PointsNodesBufferName;
	FString PointsWeightsBufferName;
};

//------------------------------------------------------------------------------------------------------------

void FNDIHairStrandsBuffer::SetAsset(const UHairStrandsAsset* HairStrandsAsset)
{
	SourceAsset = HairStrandsAsset;
}

void FNDIHairStrandsBuffer::SetStrandSize(const uint32 HairStrandSize)
{
	StrandSize = HairStrandSize;
}

void FNDIHairStrandsBuffer::InitRHI()
{
	if (SourceAsset != nullptr)
	{
		if (SourceAsset->HairStrandsResource)
		{
			PointsPositionsBufferUav = SourceAsset->HairStrandsResource->PositionBuffer.UAV;
		}
		TArray<FHairStrandsPositionFormat::Type> NodesPositions;
		TArray<FHairStrandsIndexFormat::Type> PointsNodes;
		TArray<FHairStrandsWeightFormat::Type> PointsWeights;
		SourceAsset->StrandsDatas.BuildSimulationDatas(StrandSize, NodesPositions, PointsWeights, PointsNodes);
		{
			const uint32 PositionCount = NodesPositions.Num();
			const uint32 PositionBytes = FHairStrandsPositionFormat::SizeInByte*PositionCount;

			NodesPositionsBuffer.Initialize(FHairStrandsPositionFormat::SizeInByte, PositionCount, FHairStrandsPositionFormat::Format, BUF_Static);
			void* PositionBufferData = RHILockVertexBuffer(NodesPositionsBuffer.Buffer, 0, PositionBytes, RLM_WriteOnly);

			FMemory::Memcpy(PositionBufferData, NodesPositions.GetData(), PositionBytes);
			RHIUnlockVertexBuffer(NodesPositionsBuffer.Buffer);
		}
		{
			const uint32 NodesCount = PointsNodes.Num();
			const uint32 NodesBytes = FHairStrandsIndexFormat::SizeInByte*NodesCount;

			PointsNodesBuffer.Initialize(FHairStrandsIndexFormat::SizeInByte, NodesCount, FHairStrandsIndexFormat::Format, BUF_Static);
			void* NodesBufferData = RHILockVertexBuffer(PointsNodesBuffer.Buffer, 0, NodesBytes, RLM_WriteOnly);

			FMemory::Memcpy(NodesBufferData, PointsNodes.GetData(), NodesBytes);
			RHIUnlockVertexBuffer(PointsNodesBuffer.Buffer);
		}
		{
			const uint32 WeightsCount = PointsWeights.Num();
			const uint32 WeightsBytes = FHairStrandsWeightFormat::SizeInByte*WeightsCount;

			PointsWeightsBuffer.Initialize(FHairStrandsWeightFormat::SizeInByte, WeightsCount, FHairStrandsWeightFormat::Format, BUF_Static);
			void* WeightsBufferData = RHILockVertexBuffer(PointsWeightsBuffer.Buffer, 0, WeightsBytes, RLM_WriteOnly);

			FMemory::Memcpy(WeightsBufferData, PointsWeights.GetData(), WeightsBytes);
			RHIUnlockVertexBuffer(PointsWeightsBuffer.Buffer);
		}
		{
			const uint32 OffsetCount = SourceAsset->StrandsDatas.GetNumCurves() + 1;
			const uint32 OffsetBytes = sizeof(uint32)*OffsetCount;

			CurvesOffsetsBuffer.Initialize(sizeof(uint32), OffsetCount, EPixelFormat::PF_R32_UINT, BUF_Static);
			void* OffsetBufferData = RHILockVertexBuffer(CurvesOffsetsBuffer.Buffer, 0, OffsetBytes, RLM_WriteOnly);

			FMemory::Memcpy(OffsetBufferData, SourceAsset->StrandsDatas.StrandsCurves.CurvesOffset.GetData(), OffsetBytes);
			RHIUnlockVertexBuffer(CurvesOffsetsBuffer.Buffer);
		}
	}
}

void FNDIHairStrandsBuffer::ReleaseRHI()
{
	CurvesOffsetsBuffer.Release();
	NodesPositionsBuffer.Release();
	PointsNodesBuffer.Release();
	PointsWeightsBuffer.Release();
}

//------------------------------------------------------------------------------------------------------------

struct FNDIHairStrandsParametersCS : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		FNDIHairStrandsParametersName ParamNames(ParamRef.ParameterInfo.DataInterfaceHLSLSymbol);

		WorldTransform.Bind(ParameterMap, *ParamNames.WorldTransformName);
		NumStrands.Bind(ParameterMap, *ParamNames.NumStrandsName);
		StrandSize.Bind(ParameterMap, *ParamNames.StrandSizeName);
		StrandDensity.Bind(ParameterMap, *ParamNames.StrandDensityName);
		RootThickness.Bind(ParameterMap, *ParamNames.RootThicknessName);
		TipThickness.Bind(ParameterMap, *ParamNames.TipThicknessName);
		PointsPositionsBuffer.Bind(ParameterMap, *ParamNames.PointsPositionsBufferName);
		CurvesOffsetsBuffer.Bind(ParameterMap, *ParamNames.CurvesOffsetsBufferName);
		NodesPositionsBuffer.Bind(ParameterMap, *ParamNames.NodesPositionsBufferName);
		PointsNodesBuffer.Bind(ParameterMap, *ParamNames.PointsNodesBufferName);
		PointsWeightsBuffer.Bind(ParameterMap, *ParamNames.PointsWeightsBufferName);

		if (!PointsPositionsBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.PointsPositionsBufferName)
		}
		if (!CurvesOffsetsBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.CurvesOffsetsBufferName)
		}
		if (!NodesPositionsBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.NodesPositionsBufferName)
		}
		if (!PointsNodesBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.PointsNodesBufferName)
		}
		if (!PointsWeightsBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.PointsWeightsBufferName)
		}
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Ar << WorldTransform;
		Ar << NumStrands;
		Ar << StrandSize;
		Ar << StrandDensity;
		Ar << RootThickness;
		Ar << TipThickness;
		Ar << PointsPositionsBuffer;
		Ar << CurvesOffsetsBuffer;
		Ar << NodesPositionsBuffer;
		Ar << PointsNodesBuffer;
		Ar << PointsWeightsBuffer;
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();

		FNDIHairStrandsProxy* InterfaceProxy =
			static_cast<FNDIHairStrandsProxy*>(Context.DataInterface);
		FNDIHairStrandsData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstance);
		ensure(ProxyData);

		if (ProxyData != nullptr)
		{
			FNDIHairStrandsBuffer* StrandBuffer = ProxyData->StrandBuffer;
			SetUAVParameter(RHICmdList, ComputeShaderRHI, PointsPositionsBuffer, StrandBuffer->PointsPositionsBufferUav);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, CurvesOffsetsBuffer, StrandBuffer->CurvesOffsetsBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, NodesPositionsBuffer, StrandBuffer->NodesPositionsBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PointsNodesBuffer, StrandBuffer->PointsNodesBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PointsWeightsBuffer, StrandBuffer->PointsWeightsBuffer.SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldTransform, ProxyData->WorldTransform);
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumStrands, ProxyData->NumStrands);
			SetShaderValue(RHICmdList, ComputeShaderRHI, StrandSize, ProxyData->StrandSize);
			SetShaderValue(RHICmdList, ComputeShaderRHI, StrandDensity, ProxyData->StrandDensity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, RootThickness, ProxyData->RootThickness);
			SetShaderValue(RHICmdList, ComputeShaderRHI, TipThickness, ProxyData->TipThickness);
		}
		else
		{
			SetUAVParameter(RHICmdList, ComputeShaderRHI, PointsPositionsBuffer, FNiagaraRenderer::GetDummyFloatBuffer().UAV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, CurvesOffsetsBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, NodesPositionsBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PointsNodesBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, PointsWeightsBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldTransform, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumStrands, 1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, StrandSize, 1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, StrandDensity, 1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, RootThickness, 0.1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, TipThickness, 0.1);
		}
	}

private:

	FShaderParameter WorldTransform;
	FShaderParameter NumStrands;
	FShaderParameter StrandSize;
	FShaderParameter StrandDensity;
	FShaderParameter RootThickness;
	FShaderParameter TipThickness;

	FShaderResourceParameter PointsPositionsBuffer;
	FShaderResourceParameter CurvesOffsetsBuffer;
	FShaderResourceParameter NodesPositionsBuffer;
	FShaderResourceParameter PointsNodesBuffer;
	FShaderResourceParameter PointsWeightsBuffer;
};

//------------------------------------------------------------------------------------------------------------

void FNDIHairStrandsProxy::DeferredDestroy()
{
	for (const FGuid& Sys : DeferredDestroyList)
	{
		SystemInstancesToProxyData.Remove(Sys);
	}

	DeferredDestroyList.Empty();
}

void FNDIHairStrandsProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FGuid& Instance)
{
	FNDIHairStrandsData* SourceData = static_cast<FNDIHairStrandsData*>(PerInstanceData);
	FNDIHairStrandsData* TargetData = SystemInstancesToProxyData.Find(Instance);

	ensure(TargetData);
	if (TargetData)
	{
		TargetData->WorldTransform = SourceData->WorldTransform;
	}
	else
	{
		UE_LOG(LogHairStrands, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %s"), *Instance.ToString());
	}
}

void FNDIHairStrandsProxy::InitializePerInstanceData(const FGuid& SystemInstance, FNDIHairStrandsBuffer* StrandBuffer, uint32 NumStrands, const uint8 StrandSize,
	const float StrandDensity, const float RootThickness, const float TipThickness)
{
	check(IsInRenderingThread());

	FNDIHairStrandsData* TargetData = SystemInstancesToProxyData.Find(SystemInstance);
	if (TargetData != nullptr)
	{
		DeferredDestroyList.Remove(SystemInstance);
	}
	else
	{
		TargetData = &SystemInstancesToProxyData.Add(SystemInstance);
	}
	TargetData->StrandBuffer = StrandBuffer;
	TargetData->NumStrands = NumStrands;
	TargetData->StrandSize = StrandSize;
	TargetData->StrandDensity = StrandDensity;
	TargetData->RootThickness = RootThickness;
	TargetData->TipThickness = TipThickness;
}

void FNDIHairStrandsProxy::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FGuid& SystemInstance)
{
	check(IsInRenderingThread());

	DeferredDestroyList.Add(SystemInstance);
	Batcher->EnqueueDeferredDeletesForDI_RenderThread(this->AsShared());
}

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfaceHairStrands::UNiagaraDataInterfaceHairStrands(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, StrandSize(EHairStrandsSize::Size4)
	, StrandDensity(1.0)
	, RootThickness(0.1)
	, TipThickness(0.1)
	, SourceAsset(nullptr)
	, SourceTransform(FMatrix::Identity)
	, StaticMesh(nullptr)
{
	Proxy = MakeShared<FNDIHairStrandsProxy, ESPMode::ThreadSafe>();
}

bool UNiagaraDataInterfaceHairStrands::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIHairStrandsData* InstanceData = new (PerInstanceData) FNDIHairStrandsData();

	if (SourceAsset == nullptr)
	{
		UE_LOG(LogHairStrands, Log, TEXT("Hair Strands data interface has no valid asset. Failed InitPerInstanceData - %s"), *GetFullName());
		return false;
	}
	else
	{
		FNDIHairStrandsBuffer* StrandBuffer = new FNDIHairStrandsBuffer;
		StrandBuffer->SetAsset(SourceAsset);

		// Push instance data to RT
		{
			const uint32 LocalNumStrands = SourceAsset->StrandsDatas.GetNumCurves();
			const uint8 LocalStrandSize = static_cast<uint8>(StrandSize);
			const FBox LocalStrandsBox;// = SourceAsset->ComputeStrandsBounds();
			StrandBuffer->SetStrandSize(LocalStrandSize);

			
			/*SourceAsset->StrandsDatas.ResampleInternalDatas(static_cast<uint32>(StrandSize));
			if (StaticMesh)
			{
				SourceAsset->StrandsDatas.AttachStrandsRoots(StaticMesh,SourceTransform);
			}*/

			UE_LOG(LogHairStrands, Log, TEXT("Num Strands = %d | Strand Size = %d | Num Vertices = %d | Min = %f %f %f | Max = %f %f %f"), LocalNumStrands, LocalStrandSize,
				SourceAsset->StrandsDatas.GetNumPoints(), LocalStrandsBox.Min[0], LocalStrandsBox.Min[1], LocalStrandsBox.Min[2], LocalStrandsBox.Max[0], LocalStrandsBox.Max[1], LocalStrandsBox.Max[2] );

			const float LocalStrandDensity = StrandDensity;
			const float LocalRootThickness = RootThickness;
			const float LocalTipThickness = TipThickness;

			InstanceData->WorldTransform = SourceTransform;// SourceAsset->GetComponentToWorld().ToMatrixWithScale();
			InstanceData->StrandSize = LocalStrandSize;
			InstanceData->StrandDensity = LocalStrandDensity;
			InstanceData->RootThickness = LocalRootThickness;
			InstanceData->TipThickness = LocalTipThickness;
			InstanceData->NumStrands = LocalNumStrands;
			InstanceData->StrandBuffer = StrandBuffer;

			FNDIHairStrandsProxy* ThisProxy = GetProxyAs<FNDIHairStrandsProxy>();
			ENQUEUE_RENDER_COMMAND(FNiagaraDIPushInitialInstanceDataToRT) (
				[ThisProxy, InstanceID = SystemInstance->GetId(), StrandBuffer, LocalNumStrands, LocalStrandSize, LocalStrandDensity, LocalRootThickness, LocalTipThickness](FRHICommandListImmediate& CmdList)
			{
				StrandBuffer->InitResource();
				ThisProxy->InitializePerInstanceData(InstanceID, StrandBuffer, LocalNumStrands, LocalStrandSize, LocalStrandDensity, LocalRootThickness, LocalTipThickness);
			}
			);
		}
	}
	return true;
}

void UNiagaraDataInterfaceHairStrands::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIHairStrandsData* InstanceData = static_cast<FNDIHairStrandsData*>(PerInstanceData);
	InstanceData->~FNDIHairStrandsData();

	if(InstanceData->StrandBuffer)
	{
		FNDIHairStrandsProxy* ThisProxy = GetProxyAs<FNDIHairStrandsProxy>();
		FNDIHairStrandsBuffer* InBuffer = InstanceData->StrandBuffer;
		ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
			[ThisProxy, InBuffer, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
			InBuffer->ReleaseResource();
			ThisProxy->DestroyPerInstanceData(Batcher, InstanceID);
			delete InBuffer;
		}
		);
		InstanceData->StrandBuffer = nullptr;
	}
}

bool UNiagaraDataInterfaceHairStrands::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIHairStrandsData* InstanceData = static_cast<FNDIHairStrandsData*>(PerInstanceData);

	bool RequireReset = false;
	if (SourceAsset != nullptr)
	{
		InstanceData->WorldTransform = SourceTransform; //SourceAsset->GetComponentToWorld().ToMatrixWithScale();
	}
	else
	{
		InstanceData->WorldTransform = SourceTransform;
		RequireReset = true;
	}
	return RequireReset;
}

bool UNiagaraDataInterfaceHairStrands::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceHairStrands* OtherTyped = CastChecked<UNiagaraDataInterfaceHairStrands>(Destination);
	OtherTyped->StrandSize = StrandSize;
	OtherTyped->StrandDensity = StrandDensity;
	OtherTyped->RootThickness = RootThickness;
	OtherTyped->TipThickness = TipThickness;
	OtherTyped->SourceAsset = SourceAsset;
	OtherTyped->SourceTransform = SourceTransform;

	return true;
}

bool UNiagaraDataInterfaceHairStrands::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceHairStrands* OtherTyped = CastChecked<const UNiagaraDataInterfaceHairStrands>(Other);

	return  (OtherTyped->StrandSize == StrandSize)
		&& (OtherTyped->StrandDensity == StrandDensity) && (OtherTyped->RootThickness == RootThickness)
		&& (OtherTyped->TipThickness == TipThickness) && (OtherTyped->SourceAsset == SourceAsset) && (OtherTyped->SourceTransform == SourceTransform);
}

void UNiagaraDataInterfaceHairStrands::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

void UNiagaraDataInterfaceHairStrands::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumStrandsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Strand Hair")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Strands")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStrandDensityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Strand Hair")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Strand Density")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStrandSizeName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Strand Hair")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Strand Size")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetRootThicknessName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Strand Hair")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Root Thickness")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetTipThicknessName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Strand Hair")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Tip Thickness")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetWorldTransformName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Strand Hair")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("World Transform")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVertexPositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Strand Hair")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Vertex Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeNodePositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Strand Hair")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetNumStrands);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandDensity);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetRootThickness);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetTipThickness);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetWorldTransform);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetVertexPosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodePosition);

void UNiagaraDataInterfaceHairStrands::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == GetNumStrandsName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetNumStrands)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStrandDensityName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandDensity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStrandSizeName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetRootThicknessName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetRootThickness)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetTipThicknessName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetTipThickness)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetWorldTransformName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 16);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetWorldTransform)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetVertexPositionName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetVertexPosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeNodePositionName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodePosition)::Bind(this, OutFunc);
	}
}

void UNiagaraDataInterfaceHairStrands::GetNumStrands(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutNumStrands(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutNumStrands.GetDestAndAdvance() = InstData->NumStrands;
	}
}

void UNiagaraDataInterfaceHairStrands::GetStrandSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutStrandSize(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutStrandSize.GetDestAndAdvance() = InstData->StrandSize;
	}
}

void UNiagaraDataInterfaceHairStrands::GetStrandDensity(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutStrandDensity(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutStrandDensity.GetDestAndAdvance() = InstData->StrandDensity;
	}
}

void UNiagaraDataInterfaceHairStrands::GetRootThickness(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutRootThickness(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutRootThickness.GetDestAndAdvance() = InstData->RootThickness;
	}
}

void UNiagaraDataInterfaceHairStrands::GetTipThickness(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutTipThickness(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutTipThickness.GetDestAndAdvance() = InstData->TipThickness;
	}
}

void UNiagaraDataInterfaceHairStrands::GetWorldTransform(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<FMatrix> OutWorldTransform(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutWorldTransform.GetDestAndAdvance() = InstData->WorldTransform;
	}
}

void UNiagaraDataInterfaceHairStrands::GetVertexPosition(FVectorVMContext& Context)
{
	if (SourceAsset)
	{
		VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
		VectorVM::FExternalFuncInputHandler<int32> InVertexIndex(Context);

		VectorVM::FExternalFuncRegisterHandler<float> OutVertexPosX(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutVertexPosY(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutVertexPosZ(Context);

		FMatrix& WorldTransform = InstData->WorldTransform;

		const uint32 NumVerts = InstData->NumStrands * InstData->StrandSize;
		FVector Pos;
		for (int32 i = 0; i < Context.NumInstances; i++)
		{
			const uint32 VertexIndex = InVertexIndex.Get() % NumVerts;
			Pos = SourceAsset->StrandsDatas.StrandsPoints.PointsPosition[VertexIndex];
			WorldTransform.TransformPosition(Pos);

			*OutVertexPosX.GetDestAndAdvance() = Pos.X;
			*OutVertexPosY.GetDestAndAdvance() = Pos.Y;
			*OutVertexPosZ.GetDestAndAdvance() = Pos.Z;
		}
	}
}

void UNiagaraDataInterfaceHairStrands::ComputeNodePosition(FVectorVMContext& Context)
{
	if (SourceAsset)
	{
		VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
		VectorVM::FExternalFuncInputHandler<int32> InVertexIndex(Context);

		VectorVM::FExternalFuncRegisterHandler<float> OutVertexPosX(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutVertexPosY(Context);
		VectorVM::FExternalFuncRegisterHandler<float> OutVertexPosZ(Context);

		FMatrix& WorldTransform = InstData->WorldTransform;

		const uint32 NumVerts = InstData->NumStrands * InstData->StrandSize;
		FVector Pos;
		for (int32 i = 0; i < Context.NumInstances; i++)
		{
			const uint32 VertexIndex = InVertexIndex.Get() % NumVerts;
			Pos = SourceAsset->StrandsDatas.StrandsPoints.PointsPosition[VertexIndex];
			WorldTransform.TransformPosition(Pos);

			*OutVertexPosX.GetDestAndAdvance() = Pos.X;
			*OutVertexPosY.GetDestAndAdvance() = Pos.Y;
			*OutVertexPosZ.GetDestAndAdvance() = Pos.Z;
		}
	}
}

bool UNiagaraDataInterfaceHairStrands::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FNDIHairStrandsParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), InstanceFunctionName},
		{TEXT("NumStrandsName"), ParamNames.NumStrandsName},
		{TEXT("StrandSizeName"), ParamNames.StrandSizeName},
		{TEXT("StrandDensityName"), ParamNames.StrandDensityName},
		{TEXT("RootThicknessName"), ParamNames.RootThicknessName},
		{TEXT("TipThicknessName"), ParamNames.TipThicknessName},
		{TEXT("WorldTransformName"), ParamNames.WorldTransformName},
		{TEXT("PointsPositionsBufferName"), ParamNames.PointsPositionsBufferName},
		{TEXT("CurvesOffsetsBufferName"), ParamNames.CurvesOffsetsBufferName},
		{TEXT("NodesPositionsBufferName"), ParamNames.NodesPositionsBufferName},
		{TEXT("PointsNodesBufferName"), ParamNames.PointsNodesBufferName},
		{TEXT("PointsWeightsBufferName"), ParamNames.PointsWeightsBufferName},
		{TEXT("HairStrandsContextName"), TEXT("DIHAIRSTRANDS_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (DefinitionFunctionName == GetStrandDensityName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float Out_StrandDensity)
		{
			Out_StrandDensity = {StrandDensitysName};
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetStrandSizeName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int Out_StrandSize)
		{
			Out_StrandSize = {StrandSizeName};
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetNumStrandsName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int Out_NumStrands)
		{
			Out_NumStrands = {NumStrandsName};
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetRootThicknessName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float Out_RootThickness)
		{
			Out_RootThickness = {RootThicknessName};
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetTipThicknessName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float Out_TipThickness)
		{
			Out_TipThickness = {TipThicknessName};
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetWorldTransformName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float4x4 Out_WorldTransform)
		{
			Out_WorldTransform = {WorldTransformName};
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetVertexPositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int VertexIndex, out float3 Out_VertexPosition)
			{
				VertexIndex *= 3 ;
				Out_VertexPosition = float3({PointsPositionsBufferName}[VertexIndex], {PointsPositionsBufferName}[VertexIndex+1], {PointsPositionsBufferName}[VertexIndex+2]);
				Out_VertexPosition = mul(float4(Out_VertexPosition, 1.0), {WorldTransformName}).xyz;
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	/*else if (DefinitionFunctionName == ComputeNodePositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int VertexIndex, out float3 Out_VertexPosition)
			{
				VertexIndex *= 3 ;
				Out_VertexPosition = float3({PointsPositionsBufferName}[VertexIndex], {PointsPositionsBufferName}[VertexIndex+1], {PointsPositionsBufferName}[VertexIndex+2]);
				Out_VertexPosition = mul(float4(Out_VertexPosition, 1.0), {WorldTransformName}).xyz;
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}*/

	//else if (DefinitionFunctionName == ComputeStrandIndexName)
	//{
	//	static const TCHAR *FormatSample = TEXT(R"(
	//		void {InstanceFunctionName} (in int NodeIndex, out int2 Out_StrandIndex)
	//		{
	//			Out_StrandIndex.x = NodeIndex / {StrandSizeName};
	//			Out_StrandIndex.y = NodeIndex - Out_StrandIndex.x * {StrandSizeName};
	//		}
	//		)");
	//	OutHLSL += FString::Format(FormatSample, ArgsSample);
	//	return true;
	//}
	else if (DefinitionFunctionName == ComputeNodePositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int NodeIndex, out float3 Out_NodePosition)
			{
				{HairStrandsContextName} DIHairStrands_ComputeNodePosition(DIContext,NodeIndex,Out_NodePosition);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}

	OutHLSL += TEXT("\n");
	return false;
}

void UNiagaraDataInterfaceHairStrands::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraQuaternionUtils.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraDataInterfaceHairStrands.ush\"\n");
}

void UNiagaraDataInterfaceHairStrands::GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIHAIRSTRANDS_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}

void UNiagaraDataInterfaceHairStrands::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FGuid& SystemInstance)
{
	check(Proxy);

	FNDIHairStrandsData* GameThreadData = static_cast<FNDIHairStrandsData*>(PerInstanceData);
	FNDIHairStrandsData* RenderThreadData = static_cast<FNDIHairStrandsData*>(DataForRenderThread);

	RenderThreadData->WorldTransform = GameThreadData->WorldTransform;
}

FNiagaraDataInterfaceParametersCS*
UNiagaraDataInterfaceHairStrands::ConstructComputeParameters() const
{
	return new FNDIHairStrandsParametersCS();
}


#undef LOCTEXT_NAMESPACE