// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceFieldSystem.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "ShaderParameterUtils.h"
#include "Field/FieldSystemActor.h"
#include "Field/FieldSystem.h"
#include "Field/FieldSystemNodes.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceFieldSystem"
DEFINE_LOG_CATEGORY_STATIC(LogFieldSystem, Log, All);

//------------------------------------------------------------------------------------------------------------

static const FName SampleLinearVelocityName(TEXT("SampleLinearVelocity"));
static const FName SampleAngularVelocityName(TEXT("SampleAngularVelocity"));
static const FName SampleLinearForceName(TEXT("SampleLinearForce"));
static const FName SampleAngularTorqueName(TEXT("SampleAngularTorque"));

//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfaceFieldSystem::FieldCommandsNodesBufferName(TEXT("FieldCommandsNodesBuffer_"));
const FString UNiagaraDataInterfaceFieldSystem::FieldNodesParamsBufferName(TEXT("FieldNodesParamsBuffer_"));
const FString UNiagaraDataInterfaceFieldSystem::FieldNodesOffsetsBufferName(TEXT("FieldNodesOffsetsBuffer_"));

//------------------------------------------------------------------------------------------------------------

struct FNDIFieldSystemParametersName
{
	FNDIFieldSystemParametersName(const FString& Suffix)
	{
		FieldCommandsNodesBufferName = UNiagaraDataInterfaceFieldSystem::FieldCommandsNodesBufferName + Suffix;
		FieldNodesParamsBufferName = UNiagaraDataInterfaceFieldSystem::FieldNodesParamsBufferName + Suffix;
		FieldNodesOffsetsBufferName = UNiagaraDataInterfaceFieldSystem::FieldNodesOffsetsBufferName + Suffix;
	}

	FString FieldCommandsNodesBufferName;
	FString FieldNodesParamsBufferName;
	FString FieldNodesOffsetsBufferName;
};

//------------------------------------------------------------------------------------------------------------

template<typename BufferType, int ElementSize, EPixelFormat PixelFormat, bool InitBuffer>
void CreateInternalBuffer(const uint32 ElementCount, const BufferType* InputData, FRWBuffer& OutputBuffer)
{
	if (ElementCount > 0)
	{
		const uint32 BufferCount = ElementCount * ElementSize;
		const uint32 BufferBytes = sizeof(BufferType) * BufferCount;

		if (InitBuffer)  
		{
			OutputBuffer.Initialize(sizeof(BufferType), BufferCount, PixelFormat, BUF_Static);
		}
		void* OutputData = RHILockVertexBuffer(OutputBuffer.Buffer, 0, BufferBytes, RLM_WriteOnly);

		FMemory::Memcpy(OutputData, InputData, BufferBytes);
		RHIUnlockVertexBuffer(OutputBuffer.Buffer);
	}
}

void BuildNodeParams(FFieldNodeBase* FieldNode, FNDIFieldSystemArrays* OutAssetArrays)
{
	if (FieldNode && OutAssetArrays)
	{
		if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FUniformInteger)
		{
			FUniformInteger* LocalNode = StaticCast<FUniformInteger*>(FieldNode);

			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FUniformInteger);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Magnitude);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FRadialIntMask)
		{
			FRadialIntMask* LocalNode = StaticCast<FRadialIntMask*>(FieldNode);
			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add( FFieldNodeBase::ESerializationType::FieldNode_FRadialIntMask);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Radius);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.Z);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->InteriorValue);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->ExteriorValue);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->SetMaskCondition);

		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FUniformScalar)
		{
			FUniformScalar* LocalNode = StaticCast<FUniformScalar*>(FieldNode);
			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FUniformScalar);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Magnitude);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FRadialFalloff)
		{
			FRadialFalloff* LocalNode = StaticCast<FRadialFalloff*>(FieldNode);
			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FRadialFalloff);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Magnitude);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->MinRange);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->MaxRange);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Default);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Radius);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.Z);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Falloff);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FPlaneFalloff)
		{
			FPlaneFalloff* LocalNode = StaticCast<FPlaneFalloff*>(FieldNode);
			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FPlaneFalloff);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Magnitude);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->MinRange);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->MaxRange);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Default);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Distance);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.Z);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Normal.X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Normal.Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Normal.Z);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Falloff);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FBoxFalloff)
		{
			FBoxFalloff* LocalNode = StaticCast<FBoxFalloff*>(FieldNode);
			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FBoxFalloff);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Magnitude);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->MinRange);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->MaxRange);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Default);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetRotation().X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetRotation().Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetRotation().Z);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetRotation().W);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetTranslation().X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetTranslation().Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetTranslation().Z);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetScale3D().X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetScale3D().Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetScale3D().Z);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Falloff);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FNoiseField)
		{
			FNoiseField* LocalNode = StaticCast<FNoiseField*>(FieldNode);
			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FNoiseField);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->MinRange);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->MaxRange);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetRotation().X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetRotation().Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetRotation().Z);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetRotation().W);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetTranslation().X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetTranslation().Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetTranslation().Z);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetScale3D().X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetScale3D().Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Transform.GetScale3D().Z);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FUniformVector)
		{
			FUniformVector* LocalNode = StaticCast<FUniformVector*>(FieldNode);
			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FUniformVector);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Magnitude);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Direction.X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Direction.Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Direction.Z);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FRadialVector)
		{
			FRadialVector* LocalNode = StaticCast<FRadialVector*>(FieldNode);
			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FRadialVector);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Magnitude);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.X);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.Y);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Position.Z);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FRandomVector)
		{
			FRandomVector* LocalNode = StaticCast<FRandomVector*>(FieldNode);
			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FRandomVector);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Magnitude);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FSumScalar)
		{
			FSumScalar* LocalNode = StaticCast<FSumScalar*>(FieldNode);

			BuildNodeParams(LocalNode->ScalarRight.Get(), OutAssetArrays);
			BuildNodeParams(LocalNode->ScalarLeft.Get(), OutAssetArrays);

			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FSumScalar);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Magnitude);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->ScalarRight != nullptr);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->ScalarLeft != nullptr);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Operation);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FSumVector)
		{
			FSumVector* LocalNode = StaticCast<FSumVector*>(FieldNode);

			BuildNodeParams(LocalNode->Scalar.Get(), OutAssetArrays);
			BuildNodeParams(LocalNode->VectorRight.Get(), OutAssetArrays);
			BuildNodeParams(LocalNode->VectorLeft.Get(), OutAssetArrays);

			OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
			OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FSumVector);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Magnitude);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Scalar.Get() != nullptr);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->VectorRight.Get() != nullptr);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->VectorLeft.Get() != nullptr);
			OutAssetArrays->FieldNodesParams.Add(LocalNode->Operation);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FConversionField)
		{
			if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_Int32)
			{
				FConversionField<float, int32>* LocalNode = StaticCast<FConversionField<float, int32>*>(FieldNode);

				BuildNodeParams(LocalNode->InputField.Get(), OutAssetArrays);

				OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
				OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
				OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FConversionField);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->InputField.Get() != nullptr);
			}
			else if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_Float)
			{
				FConversionField<int32, float>* LocalNode = StaticCast<FConversionField<int32, float>*>(FieldNode);

				BuildNodeParams(LocalNode->InputField.Get(), OutAssetArrays);

				OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
				OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
				OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FConversionField);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->InputField.Get() != nullptr);
			}
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FCullingField)
		{
			if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_Int32)
			{
				FCullingField<int32>* LocalNode = StaticCast<FCullingField<int32>*>(FieldNode);

				BuildNodeParams(LocalNode->Culling.Get(), OutAssetArrays);
				BuildNodeParams(LocalNode->Input.Get(), OutAssetArrays);

				OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
				OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
				OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FCullingField);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->Culling.Get() != nullptr);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->Input.Get() != nullptr);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->Operation);
			}
			else if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_Float)
			{
				FCullingField<float>* LocalNode = StaticCast<FCullingField<float>*>(FieldNode);

				BuildNodeParams(LocalNode->Culling.Get(), OutAssetArrays);
				BuildNodeParams(LocalNode->Input.Get(), OutAssetArrays);

				OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
				OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
				OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FCullingField);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->Culling.Get() != nullptr);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->Input.Get() != nullptr);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->Operation);
			}
			else if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_FVector)
			{
				FCullingField<FVector>* LocalNode = StaticCast<FCullingField<FVector>*>(FieldNode);

				BuildNodeParams(LocalNode->Culling.Get(), OutAssetArrays);
				BuildNodeParams(LocalNode->Input.Get(), OutAssetArrays);

				OutAssetArrays->FieldNodesOffsets.Add(OutAssetArrays->FieldNodesParams.Num());
				OutAssetArrays->FieldNodesParams.Add(FieldNode->Type());
				OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FCullingField);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->Culling.Get() != nullptr);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->Input.Get() != nullptr);
				OutAssetArrays->FieldNodesParams.Add(LocalNode->Operation);
			}
		}
	}
}

void CreateInternalArrays(const TArray<TWeakObjectPtr<UFieldSystem>>& FieldSystems, const TArray<TWeakObjectPtr<UFieldSystemComponent>>& FieldComponents,
	FNDIFieldSystemArrays* OutAssetArrays)
{
	if (OutAssetArrays != nullptr)
	{
		OutAssetArrays->FieldNodesOffsets.Empty();
		for (uint32 FieldIndex = 0; FieldIndex < FNDIFieldSystemArrays::NumCommands+1; ++FieldIndex)
		{
			OutAssetArrays->FieldCommandsNodes[FieldIndex] = 0;
		}
		for (int32 SystemIndex = 0; SystemIndex < FieldSystems.Num(); ++SystemIndex)
		{
			TWeakObjectPtr<UFieldSystem> FieldSystem = FieldSystems[SystemIndex];
			if (FieldSystem.IsValid() && FieldSystem.Get() != nullptr)
			{
				TArray< FFieldSystemCommand >& FieldCommands = FieldSystem->Commands;
				for (int32 CommandIndex = 0; CommandIndex < FieldCommands.Num(); ++CommandIndex)
				{
					const EFieldPhysicsType CommandType = GetFieldPhysicsType(FieldCommands[CommandIndex].TargetAttribute);
					OutAssetArrays->FieldCommandsNodes[CommandType+1] = OutAssetArrays->FieldNodesOffsets.Num();

					TUniquePtr<FFieldNodeBase>& RootNode = FieldCommands[CommandIndex].RootNode;
					BuildNodeParams(RootNode.Get(), OutAssetArrays);

					OutAssetArrays->FieldCommandsNodes[CommandType+1] = OutAssetArrays->FieldNodesOffsets.Num() - 
						OutAssetArrays->FieldCommandsNodes[CommandType+1];

					//UE_LOG(LogFieldSystem, Warning, TEXT("Params offset = %d %d %d %d %d"), CommandIndex, CommandType, 
					//	OutAssetArrays->FieldCommandsNodes[CommandType+1], OutAssetArrays->FieldNodesOffsets.Num(), OutAssetArrays->FieldNodesParams.Num());
				}
			}
		}
		for (uint32 FieldIndex = 1; FieldIndex < FNDIFieldSystemArrays::NumCommands + 1; ++FieldIndex)
		{
			OutAssetArrays->FieldCommandsNodes[FieldIndex] += OutAssetArrays->FieldCommandsNodes[FieldIndex-1];
		}
		/*for (auto& OffsetsValue : OutAssetArrays->FieldNodesOffsets)
		{
			UE_LOG(LogFieldSystem, Warning, TEXT("Offsets Value = %d"), OffsetsValue);
		}
		for (auto& ParamsValue : OutAssetArrays->FieldNodesParams)
		{
			UE_LOG(LogFieldSystem, Warning, TEXT("Params Value = %f"), ParamsValue);
		}
		for (auto& CommandsNodes : OutAssetArrays->FieldCommandsNodes)
		{
			UE_LOG(LogFieldSystem, Warning, TEXT("Commands Nodes = %d"), CommandsNodes);
		}*/
	}
}

void UpdateInternalArrays(const TArray<TWeakObjectPtr<UFieldSystem>>& FieldSystems, const TArray<TWeakObjectPtr<UFieldSystemComponent>>& FieldComponents,
	FNDIFieldSystemArrays* OutAssetArrays)
{
}

//------------------------------------------------------------------------------------------------------------


bool FNDIFieldSystemBuffer::IsValid() const
{
	return (0 < FieldSystems.Num() && FieldSystems[0].IsValid() &&
		FieldSystems[0].Get() != nullptr) && (AssetArrays.IsValid() && AssetArrays.Get() != nullptr) && FieldSystems.Num() == FieldSystems.Num();
}

void FNDIFieldSystemBuffer::Initialize(const TArray<TWeakObjectPtr<UFieldSystem>>& InFieldSystems, const TArray<TWeakObjectPtr<UFieldSystemComponent>>& InFieldComponents)
{
	FieldSystems = InFieldSystems;
	FieldComponents = InFieldComponents;

	AssetArrays = MakeUnique<FNDIFieldSystemArrays>();

	if (IsValid())
	{
		CreateInternalArrays(FieldSystems, FieldComponents, AssetArrays.Get());
	}
}

void FNDIFieldSystemBuffer::Update()
{
	if (IsValid())
	{
		UpdateInternalArrays(FieldSystems, FieldComponents, AssetArrays.Get());

		FNDIFieldSystemBuffer* ThisBuffer = this;
		ENQUEUE_RENDER_COMMAND(UpdateFieldSystem)(
			[ThisBuffer](FRHICommandListImmediate& RHICmdList) mutable
			{
				CreateInternalBuffer<float, 1, EPixelFormat::PF_R32_FLOAT, false>(ThisBuffer->AssetArrays->FieldNodesParams.Num(), ThisBuffer->AssetArrays->FieldNodesParams.GetData(), ThisBuffer->FieldNodesParamsBuffer);
				CreateInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT, false>(ThisBuffer->AssetArrays->FieldCommandsNodes.Num(), ThisBuffer->AssetArrays->FieldCommandsNodes.GetData(), ThisBuffer->FieldCommandsNodesBuffer);
				CreateInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT, false>(ThisBuffer->AssetArrays->FieldNodesOffsets.Num(), ThisBuffer->AssetArrays->FieldNodesOffsets.GetData(), ThisBuffer->FieldNodesOffsetsBuffer);
			}
		);
	}
}

void FNDIFieldSystemBuffer::InitRHI()
{
	if (IsValid())
	{
		CreateInternalBuffer<float, 1, EPixelFormat::PF_R32_FLOAT, true>(AssetArrays->FieldNodesParams.Num(), AssetArrays->FieldNodesParams.GetData(), FieldNodesParamsBuffer);
		CreateInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT, true>(AssetArrays->FieldCommandsNodes.Num(), AssetArrays->FieldCommandsNodes.GetData(), FieldCommandsNodesBuffer);
		CreateInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT, true>(AssetArrays->FieldNodesOffsets.Num(), AssetArrays->FieldNodesOffsets.GetData(), FieldNodesOffsetsBuffer);
	}
}

void FNDIFieldSystemBuffer::ReleaseRHI()
{
	FieldNodesParamsBuffer.Release();
	FieldCommandsNodesBuffer.Release();
	FieldNodesOffsetsBuffer.Release();
}

//------------------------------------------------------------------------------------------------------------

void FNDIFieldSystemData::Release()
{
	if (FieldSystemBuffer)
	{
		BeginReleaseResource(FieldSystemBuffer);
		ENQUEUE_RENDER_COMMAND(DeleteResource)(
			[ParamPointerToRelease = FieldSystemBuffer](FRHICommandListImmediate& RHICmdList)
			{
				delete ParamPointerToRelease;
			});
		FieldSystemBuffer = nullptr;
	}
}

bool FNDIFieldSystemData::Init(UNiagaraDataInterfaceFieldSystem* Interface, FNiagaraSystemInstance* SystemInstance)
{
	FieldSystemBuffer = nullptr;

	if (Interface != nullptr && SystemInstance != nullptr)
	{
		Interface->ExtractSourceComponent(SystemInstance);

		FieldSystemBuffer = new FNDIFieldSystemBuffer();
		FieldSystemBuffer->Initialize(Interface->FieldSystems, Interface->SourceComponents);

		BeginInitResource(FieldSystemBuffer);
	}

	return true;
}

//------------------------------------------------------------------------------------------------------------

struct FNDIFieldSystemParametersCS : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNDIFieldSystemParametersCS, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FNDIFieldSystemParametersName ParamNames(*ParameterInfo.DataInterfaceHLSLSymbol);

		FieldCommandsNodesBuffer.Bind(ParameterMap, *ParamNames.FieldCommandsNodesBufferName);
		FieldNodesParamsBuffer.Bind(ParameterMap, *ParamNames.FieldNodesParamsBufferName);
		FieldNodesOffsetsBuffer.Bind(ParameterMap, *ParamNames.FieldNodesOffsetsBufferName);

		if (!FieldNodesParamsBuffer.IsBound())
		{
			UE_LOG(LogFieldSystem, Warning, TEXT("Binding failed for FNDIFieldSystemParametersCS %s. Was it optimized out?"), *ParamNames.FieldNodesParamsBufferName)
		}
		if (!FieldCommandsNodesBuffer.IsBound())
		{
			UE_LOG(LogFieldSystem, Warning, TEXT("Binding failed for FNDIFieldSystemParametersCS %s. Was it optimized out?"), *ParamNames.FieldCommandsNodesBufferName)
		}
		if (!FieldNodesOffsetsBuffer.IsBound())
		{
			UE_LOG(LogFieldSystem, Warning, TEXT("Binding failed for FNDIFieldSystemParametersCS %s. Was it optimized out?"), *ParamNames.FieldNodesOffsetsBufferName)
		}
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		FNDIFieldSystemProxy* InterfaceProxy =
			static_cast<FNDIFieldSystemProxy*>(Context.DataInterface);
		FNDIFieldSystemData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstance);

		if (ProxyData != nullptr && ProxyData->FieldSystemBuffer && ProxyData->FieldSystemBuffer->IsInitialized())
		{
			FNDIFieldSystemBuffer* AssetBuffer = ProxyData->FieldSystemBuffer;
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldNodesParamsBuffer, AssetBuffer->FieldNodesParamsBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldCommandsNodesBuffer, AssetBuffer->FieldCommandsNodesBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldNodesOffsetsBuffer, AssetBuffer->FieldNodesOffsetsBuffer.SRV);
		}
		else
		{
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldNodesParamsBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldCommandsNodesBuffer, FNiagaraRenderer::GetDummyIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldNodesOffsetsBuffer, FNiagaraRenderer::GetDummyIntBuffer());
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
	}

private:

	LAYOUT_FIELD(FShaderResourceParameter, FieldNodesParamsBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, FieldCommandsNodesBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, FieldNodesOffsetsBuffer);
};

IMPLEMENT_TYPE_LAYOUT(FNDIFieldSystemParametersCS);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceFieldSystem, FNDIFieldSystemParametersCS);


//------------------------------------------------------------------------------------------------------------

void FNDIFieldSystemProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FNDIFieldSystemData* SourceData = static_cast<FNDIFieldSystemData*>(PerInstanceData);
	FNDIFieldSystemData* TargetData = &(SystemInstancesToProxyData.FindOrAdd(Instance));

	ensure(TargetData);
	if (TargetData)
	{
		TargetData->FieldSystemBuffer = SourceData->FieldSystemBuffer;
	}
	else
	{
		UE_LOG(LogFieldSystem, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %d"), Instance);
	}
}

void FNDIFieldSystemProxy::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	FNDIFieldSystemData* TargetData = SystemInstancesToProxyData.Find(SystemInstance);
	TargetData = &SystemInstancesToProxyData.Add(SystemInstance);
}

void FNDIFieldSystemProxy::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());
	SystemInstancesToProxyData.Remove(SystemInstance);
}

void FNDIFieldSystemProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
}

void FNDIFieldSystemProxy::PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
}

void FNDIFieldSystemProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
}

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfaceFieldSystem::UNiagaraDataInterfaceFieldSystem(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultSource(nullptr)
	, BlueprintSource(nullptr)
	, SourceActor(nullptr)
	, SourceComponents()
	, FieldSystems()
{
	Proxy.Reset(new FNDIFieldSystemProxy());
}

void UNiagaraDataInterfaceFieldSystem::ExtractSourceComponent(FNiagaraSystemInstance* SystemInstance)
{
	TWeakObjectPtr<UFieldSystemComponent> SourceComponent;
	if (SourceActor)
	{
		AFieldSystemActor* FieldSystemActor = Cast<AFieldSystemActor>(SourceActor);
		if (FieldSystemActor != nullptr)
		{
			SourceComponent = FieldSystemActor->GetFieldSystemComponent();
		}
		else
		{
			SourceComponent = SourceActor->FindComponentByClass<UFieldSystemComponent>();
		}
	}
	else
	{
		if (UNiagaraComponent* SimComp = SystemInstance->GetComponent())
		{
			if (UFieldSystemComponent* ParentComp = Cast<UFieldSystemComponent>(SimComp->GetAttachParent()))
			{
				SourceComponent = ParentComp;
			}
			else if (UFieldSystemComponent* OuterComp = SimComp->GetTypedOuter<UFieldSystemComponent>())
			{
				SourceComponent = OuterComp;
			}
			else
			{
				TArray<USceneComponent*> SceneComponents;
				SimComp->GetParentComponents(SceneComponents);

				for (USceneComponent* ActorComp : SceneComponents)
				{
					UFieldSystemComponent* SourceComp = Cast<UFieldSystemComponent>(ActorComp);
					if (SourceComp && SourceComp->FieldSystem)
					{
						SourceComponent = SourceComp;
						break;
					}
				}
			}
		}
	}
	if (BlueprintSource) 
	{
		AFieldSystemActor* FieldSystemActor = Cast<AFieldSystemActor>(BlueprintSource->GeneratedClass.GetDefaultObject());
		if (FieldSystemActor != nullptr)
		{ 
			SourceComponent = FieldSystemActor->FieldSystemComponent;
		}
	}

	SourceComponents.Empty();
	FieldSystems.Empty();
	if (SourceComponent != nullptr)
	{
		SourceComponents.Add(SourceComponent);
		FieldSystems.Add(SourceComponent->FieldSystem);
	}
	else if (DefaultSource != nullptr)
	{
		SourceComponents.Add(nullptr);
		FieldSystems.Add(DefaultSource);
	}
}

bool UNiagaraDataInterfaceFieldSystem::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIFieldSystemData* InstanceData = new (PerInstanceData) FNDIFieldSystemData();

	check(InstanceData);

	return InstanceData->Init(this, SystemInstance);
}

void UNiagaraDataInterfaceFieldSystem::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIFieldSystemData* InstanceData = static_cast<FNDIFieldSystemData*>(PerInstanceData);

	InstanceData->Release();
	InstanceData->~FNDIFieldSystemData();

	FNDIFieldSystemProxy* ThisProxy = GetProxyAs<FNDIFieldSystemProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
	{
		ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
	}
	);
}

bool UNiagaraDataInterfaceFieldSystem::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIFieldSystemData* InstanceData = static_cast<FNDIFieldSystemData*>(PerInstanceData);
	if (InstanceData->FieldSystemBuffer && SystemInstance)
	{
		InstanceData->FieldSystemBuffer->Update();
	}
	return false;
}

bool UNiagaraDataInterfaceFieldSystem::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceFieldSystem* OtherTyped = CastChecked<UNiagaraDataInterfaceFieldSystem>(Destination);
	OtherTyped->FieldSystems = FieldSystems;
	OtherTyped->SourceActor = SourceActor;
	OtherTyped->SourceComponents = SourceComponents;
	OtherTyped->DefaultSource = DefaultSource;
	OtherTyped->BlueprintSource = BlueprintSource;

	return true;
}

bool UNiagaraDataInterfaceFieldSystem::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceFieldSystem* OtherTyped = CastChecked<const UNiagaraDataInterfaceFieldSystem>(Other);

	return  (OtherTyped->FieldSystems == FieldSystems) && (OtherTyped->SourceActor == SourceActor) && 
		(OtherTyped->SourceComponents == SourceComponents) && (OtherTyped->DefaultSource == DefaultSource) 
		&& ( OtherTyped->BlueprintSource == BlueprintSource);
}

void UNiagaraDataInterfaceFieldSystem::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

void UNiagaraDataInterfaceFieldSystem::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleLinearVelocityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Min Bound")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Max Bound")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Linear Velocity")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleAngularVelocityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Min Bound")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Max Bound")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Angular Velocity")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleLinearForceName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Min Bound")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Max Bound")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Linear Force")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleAngularTorqueName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Min Bound")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Max Bound")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Angular Torque")));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleLinearVelocity);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleAngularVelocity);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleLinearForce);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleAngularTorque);

void UNiagaraDataInterfaceFieldSystem::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	if (BindingInfo.Name == SampleLinearVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 10 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleLinearVelocity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleAngularVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 10 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleAngularVelocity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleLinearForceName)
	{
		check(BindingInfo.GetNumInputs() == 10 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleLinearForce)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleAngularTorqueName)
	{
		check(BindingInfo.GetNumInputs() == 10 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleAngularTorque)::Bind(this, OutFunc);
	}
}

void UNiagaraDataInterfaceFieldSystem::SampleLinearVelocity(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfaceFieldSystem::SampleAngularVelocity(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfaceFieldSystem::SampleLinearForce(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfaceFieldSystem::SampleAngularTorque(FVectorVMContext& Context)
{
}

bool UNiagaraDataInterfaceFieldSystem::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FNDIFieldSystemParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
		{TEXT("FieldSystemContextName"), TEXT("DIFieldSystem_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (FunctionInfo.DefinitionName == SampleLinearVelocityName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, in float3 MinBound, in float3 MaxBound, out float3 OutLinearVelocity)
		{
			{FieldSystemContextName}
			OutLinearVelocity = DIFieldSystem_SampleFieldVector(DIContext,SamplePosition,MinBound,MaxBound,LINEAR_VELOCITY);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleLinearForceName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, in float3 MinBound, in float3 MaxBound, out float3 OutLinearForce)
		{
			{FieldSystemContextName}
			OutLinearForce = DIFieldSystem_SampleFieldVector(DIContext,SamplePosition,MinBound,MaxBound,LINEAR_FORCE);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleAngularVelocityName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, in float3 MinBound, in float3 MaxBound, out float3 OutAngularVelocity)
		{
			{FieldSystemContextName}
			OutAngularVelocity = DIFieldSystem_SampleFieldVector(DIContext,SamplePosition,MinBound,MaxBound,ANGULAR_VELOCITY);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleAngularTorqueName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, in float3 MinBound, in float3 MaxBound, out float3 OutAngularTorque)
		{
			{FieldSystemContextName}
			OutAngularTorque = DIFieldSystem_SampleFieldVector(DIContext,SamplePosition,MinBound,MaxBound,ANGULAR_TORQUE);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	
	OutHLSL += TEXT("\n");
	return false;
}

void UNiagaraDataInterfaceFieldSystem::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraDataInterfaceFieldSystem.ush\"\n");
}

void UNiagaraDataInterfaceFieldSystem::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIFieldSystem_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}

void UNiagaraDataInterfaceFieldSystem::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIFieldSystemData* GameThreadData = static_cast<FNDIFieldSystemData*>(PerInstanceData);
	FNDIFieldSystemData* RenderThreadData = static_cast<FNDIFieldSystemData*>(DataForRenderThread);

	if (GameThreadData != nullptr && RenderThreadData != nullptr)
	{
		RenderThreadData->FieldSystemBuffer = GameThreadData->FieldSystemBuffer;
	}
	check(Proxy);
}

#undef LOCTEXT_NAMESPACE