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
static const FName SamplePositionTargetName(TEXT("SamplePositionTarget"));

static const FName SampleExternalClusterStrainName(TEXT("SampleExternalClusterStrain"));
static const FName SampleInternalClusterStrainName(TEXT("SampleInternalClusterStrain"));
static const FName SampleFieldKillName(TEXT("SampleFieldKill"));
static const FName SampleDynamicConstraintName(TEXT("SampleDynamicConstraint"));
static const FName SampleSleepingThresholdName(TEXT("SampleSleepingThreshold"));
static const FName SampleDisableThresholdName(TEXT("SampleDisableThreshold"));

static const FName SampleDynamicStateName(TEXT("SampleDynamicState"));
static const FName SampleActivateDisabledName(TEXT("SampleActivateDisabled"));
static const FName SampleCollisionGroupName(TEXT("SampleCollisionGroup"));
static const FName SamplePositionAnimatedName(TEXT("SamplePositionAnimated"));
static const FName SamplePositionStaticName(TEXT("SamplePositionStatic"));

static const FName GetFieldDimensionsName(TEXT("GetFieldDimensions"));
static const FName GetFieldBoundsName(TEXT("GetFieldBounds"));

//------------------------------------------------------------------------------------------------------------

static const TArray<EFieldPhysicsType> VectorTypes = { EFieldPhysicsType::Field_LinearForce, 
													   EFieldPhysicsType::Field_LinearVelocity,
													   EFieldPhysicsType::Field_AngularVelociy, 
													   EFieldPhysicsType::Field_AngularTorque, 
													   EFieldPhysicsType::Field_PositionTarget};

enum EFieldVectorIndices
{
	Vector_LinearForce,
	Vector_LinearVelocity,
	Vector_AngularVelocity,
	Vector_AngularTorque,
	Vector_PositionTarget
};

static const TArray<EFieldPhysicsType> ScalarTypes = { EFieldPhysicsType::Field_ExternalClusterStrain,
													   EFieldPhysicsType::Field_Kill,
													   EFieldPhysicsType::Field_SleepingThreshold,
												       EFieldPhysicsType::Field_DisableThreshold,
													   EFieldPhysicsType::Field_InternalClusterStrain,
													   EFieldPhysicsType::Field_DynamicConstraint};

enum EFieldScalarIndices
{
	Scalar_ExternalClusterStrain,
	Scalar_Kill,
	Scalar_SleepingThreshold,
	Scalar_DisableThreshold,
	Scalar_InternalClusterStrain,
	Scalar_DynamicConstraint
};

static const TArray<EFieldPhysicsType> IntegerTypes = { EFieldPhysicsType::Field_DynamicState, 
														EFieldPhysicsType::Field_ActivateDisabled,
														EFieldPhysicsType::Field_CollisionGroup,
														EFieldPhysicsType::Field_PositionAnimated,
														EFieldPhysicsType::Field_PositionStatic};

enum EFieldIntegerIndices
{
	Integer_DynamicState,
	Integer_ActivateDisabled,
	Integer_CollisionGroup,
	Integer_PositionAnimated,
	Integer_PositionStatic
};

//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfaceFieldSystem::FieldCommandsNodesBufferName(TEXT("FieldCommandsNodesBuffer_"));
const FString UNiagaraDataInterfaceFieldSystem::FieldNodesParamsBufferName(TEXT("FieldNodesParamsBuffer_"));
const FString UNiagaraDataInterfaceFieldSystem::FieldNodesOffsetsBufferName(TEXT("FieldNodesOffsetsBuffer_"));

const FString UNiagaraDataInterfaceFieldSystem::VectorFieldTextureName(TEXT("VectorFieldTexture_"));
const FString UNiagaraDataInterfaceFieldSystem::VectorFieldSamplerName(TEXT("VectorFieldSampler_"));

const FString UNiagaraDataInterfaceFieldSystem::ScalarFieldTextureName(TEXT("ScalarFieldTexture_"));
const FString UNiagaraDataInterfaceFieldSystem::ScalarFieldSamplerName(TEXT("ScalarFieldSampler_"));

const FString UNiagaraDataInterfaceFieldSystem::IntegerFieldTextureName(TEXT("IntegerFieldTexture_"));
const FString UNiagaraDataInterfaceFieldSystem::IntegerFieldSamplerName(TEXT("IntegerFieldSampler_"));

const FString UNiagaraDataInterfaceFieldSystem::FieldDimensionsName(TEXT("FieldDimensions_"));
const FString UNiagaraDataInterfaceFieldSystem::MinBoundsName(TEXT("MinBounds_"));
const FString UNiagaraDataInterfaceFieldSystem::MaxBoundsName(TEXT("MaxBounds_"));

//------------------------------------------------------------------------------------------------------------

struct FNDIFieldSystemParametersName
{
	FNDIFieldSystemParametersName(const FString& Suffix)
	{
		FieldCommandsNodesBufferName = UNiagaraDataInterfaceFieldSystem::FieldCommandsNodesBufferName + Suffix;
		FieldNodesParamsBufferName = UNiagaraDataInterfaceFieldSystem::FieldNodesParamsBufferName + Suffix;
		FieldNodesOffsetsBufferName = UNiagaraDataInterfaceFieldSystem::FieldNodesOffsetsBufferName + Suffix;

		VectorFieldTextureName = UNiagaraDataInterfaceFieldSystem::VectorFieldTextureName + Suffix;
		VectorFieldSamplerName = UNiagaraDataInterfaceFieldSystem::VectorFieldSamplerName + Suffix;

		ScalarFieldTextureName = UNiagaraDataInterfaceFieldSystem::ScalarFieldTextureName + Suffix;
		ScalarFieldSamplerName = UNiagaraDataInterfaceFieldSystem::ScalarFieldSamplerName + Suffix;

		IntegerFieldTextureName = UNiagaraDataInterfaceFieldSystem::IntegerFieldTextureName + Suffix;
		IntegerFieldSamplerName = UNiagaraDataInterfaceFieldSystem::IntegerFieldSamplerName + Suffix;

		FieldDimensionsName = UNiagaraDataInterfaceFieldSystem::FieldDimensionsName + Suffix;
		MinBoundsName = UNiagaraDataInterfaceFieldSystem::MinBoundsName + Suffix;
		MaxBoundsName = UNiagaraDataInterfaceFieldSystem::MaxBoundsName + Suffix;
	}

	FString FieldCommandsNodesBufferName;
	FString FieldNodesParamsBufferName;
	FString FieldNodesOffsetsBufferName;

	FString VectorFieldTextureName;
	FString VectorFieldSamplerName;

	FString ScalarFieldTextureName;
	FString ScalarFieldSamplerName;

	FString IntegerFieldTextureName;
	FString IntegerFieldSamplerName;

	FString FieldDimensionsName;
	FString MinBoundsName;
	FString MaxBoundsName;
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
template<typename BufferType, int ElementSize, EPixelFormat PixelFormat, bool InitBuffer>
void CreateInternalTexture(const uint32 DimensionX, const uint32 DimensionY, const uint32 DimensionZ, const BufferType* InputData, FTextureRWBuffer3D& OutputBuffer)
{
	if (DimensionX * DimensionY * DimensionZ > 0)
	{
		const uint32 BlockBytes = sizeof(BufferType) * ElementSize;

		if (InitBuffer)
		{
			OutputBuffer.Initialize(BlockBytes, DimensionX, DimensionY, DimensionZ, PixelFormat);
		}
		FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, DimensionX, DimensionY, DimensionZ);

		const uint8* TextureDatas = (const uint8*)InputData;
		RHIUpdateTexture3D(OutputBuffer.Buffer, 0, UpdateRegion, DimensionX * BlockBytes,
			DimensionX * DimensionY * BlockBytes, TextureDatas);
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
			OutAssetArrays->FieldNodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FRadialIntMask);
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


template<typename DataType>
FFieldNode<DataType>* GetFieldNode(const TArray<TWeakObjectPtr<class UFieldSystem>>& FieldSystems, const EFieldPhysicsType FieldType)
{
	for (int32 SystemIndex = 0; SystemIndex < FieldSystems.Num(); ++SystemIndex)
	{
		TWeakObjectPtr<UFieldSystem> FieldSystem = FieldSystems[SystemIndex];
		if (FieldSystem.IsValid() && FieldSystem.Get() != nullptr)
		{
			TArray< FFieldSystemCommand >& FieldCommands = FieldSystem->Commands;
			for (int32 CommandIndex = 0; CommandIndex < FieldCommands.Num(); ++CommandIndex)
			{
				const EFieldPhysicsType CommandType = GetFieldPhysicsType(FieldCommands[CommandIndex].TargetAttribute);
				if (CommandType == FieldType && FieldCommands[CommandIndex].RootNode.Get())
				{
					return static_cast<FFieldNode<DataType>*>(
						FieldCommands[CommandIndex].RootNode.Get());
				}
			}
		}
	}
	return nullptr;
}

void BakeFieldArrays(const TArray<TWeakObjectPtr<UFieldSystem>>& FieldSystems, const TArray<TWeakObjectPtr<UFieldSystemComponent>>& FieldComponents,
	FNDIFieldSystemArrays* OutAssetArrays)
{
	const int32 FieldSize = OutAssetArrays->FieldDimensions.X * OutAssetArrays->FieldDimensions.Y * OutAssetArrays->FieldDimensions.Z;

	OutAssetArrays->ArrayFieldDatas.Init(0.0, FieldSize * 4 * VectorTypes.Num());
	OutAssetArrays->VectorFieldDatas.Init(FVector(0.f), FieldSize * VectorTypes.Num());
	OutAssetArrays->ScalarFieldDatas.Init(0.0, FieldSize * ScalarTypes.Num());
	OutAssetArrays->IntegerFieldDatas.Init(0, FieldSize * IntegerTypes.Num());

	TArray<ContextIndex> IndicesArray;
	ContextIndex::ContiguousIndices(IndicesArray, FieldSize);

	TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

	TArray<FVector> SamplesArray;
	SamplesArray.Init(FVector(0.f), FieldSize);

	const FVector CellSize = (OutAssetArrays->MaxBounds - OutAssetArrays->MinBounds) /
		FVector(OutAssetArrays->FieldDimensions.X - 1, OutAssetArrays->FieldDimensions.Y - 1, OutAssetArrays->FieldDimensions.Z - 1);

	int32 SampleIndex = 0;
	//for (int32 GridIndexX = 0; GridIndexX < OutAssetArrays->FieldDimensions.X; ++GridIndexX)
	for (int32 GridIndexZ = 0; GridIndexZ < OutAssetArrays->FieldDimensions.Z; ++GridIndexZ)
	{
		for (int32 GridIndexY = 0; GridIndexY < OutAssetArrays->FieldDimensions.Y; ++GridIndexY)
		{
			for (int32 GridIndexX = 0; GridIndexX < OutAssetArrays->FieldDimensions.X; ++GridIndexX)
				//for (int32 GridIndexZ = 0; GridIndexZ < OutAssetArrays->FieldDimensions.Z; ++GridIndexZ)
			{
				SamplesArray[SampleIndex++] = OutAssetArrays->MinBounds + FVector(GridIndexX, GridIndexY, GridIndexZ) * CellSize;
			}
		}
	}
	TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), FieldSize);

	FFieldContext FieldContext{
		IndexView,
		SamplesView,
		FFieldContext::UniquePointerMap()
	};

	int32 VectorBegin = 0;
	for (int32 TypeIndex = 0; TypeIndex < VectorTypes.Num(); ++TypeIndex)
	{
		TArrayView<FVector> ResultsView(&(OutAssetArrays->VectorFieldDatas[0]) + VectorBegin, FieldSize);
		FFieldNode<FVector>* CommandRoot = GetFieldNode<FVector>(FieldSystems, VectorTypes[TypeIndex]);
		if (CommandRoot)
		{
			CommandRoot->Evaluate(FieldContext, ResultsView);

			for (int32 ArrayIndex = VectorBegin, VectorEnd = VectorBegin + FieldSize; ArrayIndex < VectorEnd; ++ArrayIndex)
			{
				//UE_LOG(LogFieldSystem, Warning, TEXT("Sample Field = %d %d %s"), TypeIndex, ArrayIndex, *OutAssetArrays->VectorFieldDatas[ArrayIndex].ToString());
				OutAssetArrays->ArrayFieldDatas[4 * ArrayIndex] = OutAssetArrays->VectorFieldDatas[ArrayIndex].X;
				OutAssetArrays->ArrayFieldDatas[4 * ArrayIndex + 1] = OutAssetArrays->VectorFieldDatas[ArrayIndex].Y;
				OutAssetArrays->ArrayFieldDatas[4 * ArrayIndex + 2] = OutAssetArrays->VectorFieldDatas[ArrayIndex].Z;
			}
		}
		VectorBegin += FieldSize;
	}
	int32 ScalarBegin = 0;
	for (int32 TypeIndex = 0; TypeIndex < ScalarTypes.Num(); ++TypeIndex)
	{
		TArrayView<float> ResultsView(&(OutAssetArrays->ScalarFieldDatas[0]) + ScalarBegin, FieldSize);
		FFieldNode<float>* CommandRoot = GetFieldNode<float>(FieldSystems, ScalarTypes[TypeIndex]);
		if (CommandRoot)
		{
			CommandRoot->Evaluate(FieldContext, ResultsView);
		}
		ScalarBegin += FieldSize;
	}
	int32 IntegerBegin = 0;
	for (int32 TypeIndex = 0; TypeIndex < IntegerTypes.Num(); ++TypeIndex)
	{
		TArrayView<int32> ResultsView(&(OutAssetArrays->IntegerFieldDatas[0]) + IntegerBegin, FieldSize);
		FFieldNode<int32>* CommandRoot = GetFieldNode<int32>(FieldSystems, IntegerTypes[TypeIndex]);
		if (CommandRoot)
		{
			CommandRoot->Evaluate(FieldContext, ResultsView);
		}
		IntegerBegin += FieldSize;
	}
}

void CreateInternalArrays(const TArray<TWeakObjectPtr<UFieldSystem>>& FieldSystems, const TArray<TWeakObjectPtr<UFieldSystemComponent>>& FieldComponents,
	FNDIFieldSystemArrays* OutAssetArrays)
{
	if (OutAssetArrays != nullptr)
	{
		OutAssetArrays->FieldNodesOffsets.Empty();
		OutAssetArrays->FieldNodesParams.Empty();

		for (uint32 FieldIndex = 0; FieldIndex < FNDIFieldSystemArrays::NumCommands + 1; ++FieldIndex)
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
					OutAssetArrays->FieldCommandsNodes[CommandType + 1] = OutAssetArrays->FieldNodesOffsets.Num();

					TUniquePtr<FFieldNodeBase>& RootNode = FieldCommands[CommandIndex].RootNode;
					BuildNodeParams(RootNode.Get(), OutAssetArrays);

					OutAssetArrays->FieldCommandsNodes[CommandType + 1] = OutAssetArrays->FieldNodesOffsets.Num() -
						OutAssetArrays->FieldCommandsNodes[CommandType + 1];
				}
			}
		}
		for (uint32 FieldIndex = 1; FieldIndex < FNDIFieldSystemArrays::NumCommands + 1; ++FieldIndex)
		{
			OutAssetArrays->FieldCommandsNodes[FieldIndex] += OutAssetArrays->FieldCommandsNodes[FieldIndex - 1];
		}

		BakeFieldArrays(FieldSystems, FieldComponents, OutAssetArrays);
	}
}

void UpdateInternalArrays(const TArray<TWeakObjectPtr<UFieldSystem>>& FieldSystems, const TArray<TWeakObjectPtr<UFieldSystemComponent>>& FieldComponents,
	FNDIFieldSystemArrays* OutAssetArrays)
{
	CreateInternalArrays(FieldSystems, FieldComponents, OutAssetArrays);
}

//------------------------------------------------------------------------------------------------------------


bool FNDIFieldSystemBuffer::IsValid() const
{
	return (0 < FieldSystems.Num() && FieldSystems[0].IsValid() &&
		FieldSystems[0].Get() != nullptr) && (AssetArrays.IsValid() && AssetArrays.Get() != nullptr) && FieldSystems.Num() == FieldSystems.Num();
}

void FNDIFieldSystemBuffer::Initialize(const TArray<TWeakObjectPtr<UFieldSystem>>& InFieldSystems, const TArray<TWeakObjectPtr<UFieldSystemComponent>>& InFieldComponents,
	const FIntVector& FieldDimensions, const FVector& MinBounds, const FVector& MaxBounds)
{
	FieldSystems = InFieldSystems;
	FieldComponents = InFieldComponents;

	AssetArrays = MakeUnique<FNDIFieldSystemArrays>();

	if (IsValid())
	{
		AssetArrays->FieldDimensions = FieldDimensions;
		AssetArrays->MinBounds = MinBounds;
		AssetArrays->MaxBounds = MaxBounds;

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

				CreateInternalTexture<float, 4, EPixelFormat::PF_A32B32G32R32F, false>(
					ThisBuffer->AssetArrays->FieldDimensions.X, ThisBuffer->AssetArrays->FieldDimensions.Y, ThisBuffer->AssetArrays->FieldDimensions.Z * VectorTypes.Num(),
					ThisBuffer->AssetArrays->ArrayFieldDatas.GetData(), ThisBuffer->VectorFieldTexture);
				CreateInternalTexture<float, 1, EPixelFormat::PF_R32_FLOAT, false>(
					ThisBuffer->AssetArrays->FieldDimensions.X, ThisBuffer->AssetArrays->FieldDimensions.Y, ThisBuffer->AssetArrays->FieldDimensions.Z * ScalarTypes.Num(),
					ThisBuffer->AssetArrays->ScalarFieldDatas.GetData(), ThisBuffer->ScalarFieldTexture);
				CreateInternalTexture<int32, 1, EPixelFormat::PF_R32_SINT, false>(
					ThisBuffer->AssetArrays->FieldDimensions.X, ThisBuffer->AssetArrays->FieldDimensions.Y, ThisBuffer->AssetArrays->FieldDimensions.Z * IntegerTypes.Num(),
					ThisBuffer->AssetArrays->IntegerFieldDatas.GetData(), ThisBuffer->IntegerFieldTexture);
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

		CreateInternalTexture<float, 4, EPixelFormat::PF_A32B32G32R32F, true>(
			AssetArrays->FieldDimensions.X, AssetArrays->FieldDimensions.Y, AssetArrays->FieldDimensions.Z * VectorTypes.Num(),
			AssetArrays->ArrayFieldDatas.GetData(), VectorFieldTexture);
		CreateInternalTexture<float, 1, EPixelFormat::PF_R32_FLOAT, true>(
			AssetArrays->FieldDimensions.X, AssetArrays->FieldDimensions.Y, AssetArrays->FieldDimensions.Z * ScalarTypes.Num(),
			AssetArrays->ScalarFieldDatas.GetData(), ScalarFieldTexture);
		CreateInternalTexture<int32, 1, EPixelFormat::PF_R32_SINT, true>(
			AssetArrays->FieldDimensions.X, AssetArrays->FieldDimensions.Y, AssetArrays->FieldDimensions.Z * IntegerTypes.Num(),
			AssetArrays->IntegerFieldDatas.GetData(), IntegerFieldTexture);
	}
}

void FNDIFieldSystemBuffer::ReleaseRHI()
{
	FieldNodesParamsBuffer.Release();
	FieldCommandsNodesBuffer.Release();
	FieldNodesOffsetsBuffer.Release();

	VectorFieldTexture.Release();
	ScalarFieldTexture.Release();
	IntegerFieldTexture.Release();
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

		const FTransform WorldTransform = SystemInstance->GetWorldTransform();

		FieldSystemBuffer = new FNDIFieldSystemBuffer();
		FieldSystemBuffer->Initialize(Interface->FieldSystems, Interface->SourceComponents,
			Interface->FieldDimensions, WorldTransform.GetTranslation() + Interface->MinBounds,
			WorldTransform.GetTranslation() + Interface->MaxBounds);

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

		VectorFieldTexture.Bind(ParameterMap, *ParamNames.VectorFieldTextureName);
		VectorFieldSampler.Bind(ParameterMap, *ParamNames.VectorFieldSamplerName);

		ScalarFieldTexture.Bind(ParameterMap, *ParamNames.ScalarFieldTextureName);
		ScalarFieldSampler.Bind(ParameterMap, *ParamNames.ScalarFieldSamplerName);

		IntegerFieldTexture.Bind(ParameterMap, *ParamNames.IntegerFieldTextureName);
		IntegerFieldSampler.Bind(ParameterMap, *ParamNames.IntegerFieldSamplerName);

		FieldDimensions.Bind(ParameterMap, *ParamNames.FieldDimensionsName);
		MinBounds.Bind(ParameterMap, *ParamNames.MinBoundsName);
		MaxBounds.Bind(ParameterMap, *ParamNames.MaxBoundsName);

	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		FNDIFieldSystemProxy* InterfaceProxy =
			static_cast<FNDIFieldSystemProxy*>(Context.DataInterface);
		FNDIFieldSystemData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstanceID);

		FRHISamplerState* SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		if (ProxyData != nullptr && ProxyData->FieldSystemBuffer && ProxyData->FieldSystemBuffer->IsInitialized()
			&& ProxyData->FieldSystemBuffer->AssetArrays)
		{
			FNDIFieldSystemBuffer* AssetBuffer = ProxyData->FieldSystemBuffer;
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldNodesParamsBuffer, AssetBuffer->FieldNodesParamsBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldCommandsNodesBuffer, AssetBuffer->FieldCommandsNodesBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldNodesOffsetsBuffer, AssetBuffer->FieldNodesOffsetsBuffer.SRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, VectorFieldTexture, AssetBuffer->VectorFieldTexture.SRV);
			SetSamplerParameter(RHICmdList, ComputeShaderRHI, VectorFieldSampler, SamplerState);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, ScalarFieldTexture, AssetBuffer->ScalarFieldTexture.SRV);
			SetSamplerParameter(RHICmdList, ComputeShaderRHI, ScalarFieldSampler, SamplerState);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, IntegerFieldTexture, AssetBuffer->IntegerFieldTexture.SRV);
			SetSamplerParameter(RHICmdList, ComputeShaderRHI, IntegerFieldSampler, SamplerState);

			SetShaderValue(RHICmdList, ComputeShaderRHI, FieldDimensions, AssetBuffer->AssetArrays->FieldDimensions);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MinBounds, AssetBuffer->AssetArrays->MinBounds);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MaxBounds, AssetBuffer->AssetArrays->MaxBounds);
		}
		else
		{
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldNodesParamsBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldCommandsNodesBuffer, FNiagaraRenderer::GetDummyIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FieldNodesOffsetsBuffer, FNiagaraRenderer::GetDummyIntBuffer());

			SetSRVParameter(RHICmdList, ComputeShaderRHI, VectorFieldTexture, FNiagaraRenderer::GetDummyTextureReadBuffer2D());
			SetSamplerParameter(RHICmdList, ComputeShaderRHI, VectorFieldSampler, SamplerState);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, ScalarFieldTexture, FNiagaraRenderer::GetDummyTextureReadBuffer2D());
			SetSamplerParameter(RHICmdList, ComputeShaderRHI, ScalarFieldSampler, SamplerState);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, IntegerFieldTexture, FNiagaraRenderer::GetDummyTextureReadBuffer2D());
			SetSamplerParameter(RHICmdList, ComputeShaderRHI, IntegerFieldSampler, SamplerState);

			SetShaderValue(RHICmdList, ComputeShaderRHI, FieldDimensions, FIntVector(1, 1, 1));
			SetShaderValue(RHICmdList, ComputeShaderRHI, MinBounds, FVector(0, 0, 0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, MaxBounds, FVector(0, 0, 0));
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
	}

private:

	LAYOUT_FIELD(FShaderResourceParameter, FieldNodesParamsBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, FieldCommandsNodesBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, FieldNodesOffsetsBuffer);

	LAYOUT_FIELD(FShaderResourceParameter, VectorFieldTexture);
	LAYOUT_FIELD(FShaderResourceParameter, VectorFieldSampler);

	LAYOUT_FIELD(FShaderResourceParameter, ScalarFieldTexture);
	LAYOUT_FIELD(FShaderResourceParameter, ScalarFieldSampler);

	LAYOUT_FIELD(FShaderResourceParameter, IntegerFieldTexture);
	LAYOUT_FIELD(FShaderResourceParameter, IntegerFieldSampler);

	LAYOUT_FIELD(FShaderParameter, FieldDimensions);
	LAYOUT_FIELD(FShaderParameter, MinBounds);
	LAYOUT_FIELD(FShaderParameter, MaxBounds);
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

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfaceFieldSystem::UNiagaraDataInterfaceFieldSystem(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, BlueprintSource(nullptr)
	, SourceActor(nullptr)
	, FieldDimensions(10, 10, 10)
	, MinBounds(-50, -50, -50)
	, MaxBounds(50, 50, 50)
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
	else if (USceneComponent* AttachComponent = SystemInstance->GetAttachComponent())
	{
		// First try to find the source component up the attach hierarchy
		for (USceneComponent* Curr = AttachComponent; Curr; Curr = Curr->GetAttachParent())
		{
			UFieldSystemComponent* SourceComp = Cast<UFieldSystemComponent>(Curr);
			if (SourceComp && SourceComp->FieldSystem)
			{
				SourceComponent = SourceComp;
				break;
			}
		}

		if (!SourceComponent.IsValid())
		{
			// Fall back on the outer chain to find the component
			if (UFieldSystemComponent* OuterComp = AttachComponent->GetTypedOuter<UFieldSystemComponent>())
			{
				SourceComponent = OuterComp;
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
	OtherTyped->BlueprintSource = BlueprintSource;
	OtherTyped->FieldDimensions = FieldDimensions;
	OtherTyped->MinBounds = MinBounds;
	OtherTyped->MaxBounds = MaxBounds;

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
		(OtherTyped->SourceComponents == SourceComponents)
		&& (OtherTyped->BlueprintSource == BlueprintSource && (OtherTyped->FieldDimensions == FieldDimensions)
	    && (OtherTyped->MinBounds == MinBounds) && (OtherTyped->MaxBounds == MaxBounds));
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
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Angular Torque")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SamplePositionTargetName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position Target")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleExternalClusterStrainName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("External Cluster Strain")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleInternalClusterStrainName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Internal Cluster Strain")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleFieldKillName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Field Kill")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSleepingThresholdName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sleeping Threshold")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleDisableThresholdName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Disable Threshold")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleDynamicConstraintName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Dynamic Constraint")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleDynamicStateName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Dynamic State")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleCollisionGroupName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Collision Group")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SamplePositionStaticName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Position Static")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SamplePositionAnimatedName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Position Animated")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleActivateDisabledName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Activate Disabled")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetFieldDimensionsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Field Dimensions")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetFieldBoundsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Field System")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Min Bounds")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Max Bounds")));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleLinearVelocity);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleAngularVelocity);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleLinearForce);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleAngularTorque);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SamplePositionTarget);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleExternalClusterStrain);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleInternalClusterStrain);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleSleepingThreshold);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleDisableThreshold);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleDynamicConstraint);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleFieldKill);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SamplePositionAnimated);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SamplePositionStatic);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleCollisionGroup);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleDynamicState);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleActivateDisabled);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, GetFieldDimensions);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, GetFieldBounds);

void UNiagaraDataInterfaceFieldSystem::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	if (BindingInfo.Name == SampleLinearVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleLinearVelocity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleAngularVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleAngularVelocity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleLinearForceName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleLinearForce)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleAngularTorqueName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleAngularTorque)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SamplePositionTargetName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SamplePositionTarget)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleExternalClusterStrainName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleExternalClusterStrain)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleInternalClusterStrainName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleInternalClusterStrain)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleSleepingThresholdName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleSleepingThreshold)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleDisableThresholdName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleDisableThreshold)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleFieldKillName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleFieldKill)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleDynamicConstraintName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleDynamicConstraint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SamplePositionAnimatedName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SamplePositionAnimated)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SamplePositionStaticName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SamplePositionStatic)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleDynamicStateName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleDynamicState)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleCollisionGroupName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleCollisionGroup)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleActivateDisabledName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleActivateDisabled)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetFieldDimensionsName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleLinearForce)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetFieldBoundsName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 6);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceFieldSystem, SampleAngularTorque)::Bind(this, OutFunc);
	}
}

void UNiagaraDataInterfaceFieldSystem::GetFieldDimensions(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFieldSystemData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutDimensionX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutDimensionY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutDimensionZ(Context);

	const FIntVector FieldDimension = (InstData && InstData->FieldSystemBuffer && InstData->FieldSystemBuffer->AssetArrays) ?
		InstData->FieldSystemBuffer->AssetArrays->FieldDimensions : FIntVector(1, 1, 1);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutDimensionX.GetDest() = FieldDimension.X;
		*OutDimensionY.GetDest() = FieldDimension.Y;
		*OutDimensionZ.GetDest() = FieldDimension.Z;

		OutDimensionX.Advance();
		OutDimensionY.Advance();
		OutDimensionZ.Advance();
	}
}

void UNiagaraDataInterfaceFieldSystem::GetFieldBounds(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIFieldSystemData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutMinX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMinY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMinZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMaxX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMaxY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMaxZ(Context);

	const FVector MinBound = (InstData && InstData->FieldSystemBuffer && InstData->FieldSystemBuffer->AssetArrays) ?
		InstData->FieldSystemBuffer->AssetArrays->MinBounds : FVector(0, 0, 0);

	const FVector MaxBound = (InstData && InstData->FieldSystemBuffer && InstData->FieldSystemBuffer->AssetArrays) ?
		InstData->FieldSystemBuffer->AssetArrays->MinBounds : FVector(0, 0, 0);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutMinX.GetDest() = MinBound.X;
		*OutMinY.GetDest() = MinBound.Y;
		*OutMinZ.GetDest() = MinBound.Z;
		*OutMaxX.GetDest() = MaxBound.X;
		*OutMaxY.GetDest() = MaxBound.Y;
		*OutMaxZ.GetDest() = MaxBound.Z;

		OutMinX.Advance();
		OutMinY.Advance();
		OutMinZ.Advance();
		OutMaxX.Advance();
		OutMaxY.Advance();
		OutMaxZ.Advance();
	}
}

void SampleVectorField(FVectorVMContext& Context, const EFieldPhysicsType VectorType, const int32 VectorIndex)
{
	VectorVM::FUserPtrHandler<FNDIFieldSystemData> InstData(Context);

	// Inputs 
	VectorVM::FExternalFuncInputHandler<float> SamplePositionXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> SamplePositionYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> SamplePositionZParam(Context);

	// Outputs...
	VectorVM::FExternalFuncRegisterHandler<float> OutVectoreFieldXParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVectoreFieldYParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVectoreFieldZParam(Context);

	const bool HasValidArrays = InstData && InstData->FieldSystemBuffer && InstData->FieldSystemBuffer->AssetArrays &&
		(InstData->FieldSystemBuffer->AssetArrays->VectorFieldDatas.Num() != 0);
	if (HasValidArrays)
	{
		const FVector MinBounds = InstData->FieldSystemBuffer->AssetArrays->MinBounds;
		const FVector MaxBounds = InstData->FieldSystemBuffer->AssetArrays->MaxBounds;

		const FIntVector FieldDimensions = InstData->FieldSystemBuffer->AssetArrays->FieldDimensions;
		const int32 TypeSize = FieldDimensions.X * FieldDimensions.Y * FieldDimensions.Z;

		const FVector FieldSize(FieldDimensions.X, FieldDimensions.Y, FieldDimensions.Z * VectorTypes.Num());
		const FVector BoundSize = MaxBounds - MinBounds;
		const FVector InverseBounds = (BoundSize.X > 0.0 && BoundSize.Y > 0.0 && BoundSize.Z > 0.0) ?
			FVector(1, 1, 1) / BoundSize : FVector(0, 0, 0);

		FVector* FieldData = &InstData->FieldSystemBuffer->AssetArrays->VectorFieldDatas[0];

		for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
		{
			FVector SamplePoint = (FVector(SamplePositionXParam.Get(),
				SamplePositionYParam.Get(),
				SamplePositionZParam.Get()) - MinBounds) * InverseBounds;

			SamplePoint = FVector(FMath::Clamp(SamplePoint.X, 0.0f, 1.0f),
				FMath::Clamp(SamplePoint.Y, 0.0f, 1.0f),
				FMath::Clamp(SamplePoint.Z, 0.0f, 1.0f));

			SamplePoint.Z = (VectorTypes.Num() != 0) ?
				(SamplePoint.Z * (1.0 - 1.0 / FieldDimensions.Z) + VectorIndex) / VectorTypes.Num() : SamplePoint.Z;

			SamplePoint = SamplePoint * FieldSize;

			FVector IndexMin(FGenericPlatformMath::FloorToFloat(SamplePoint.X),
				FGenericPlatformMath::FloorToFloat(SamplePoint.Y),
				FGenericPlatformMath::FloorToFloat(SamplePoint.Z));

			FVector IndexMax = IndexMin + FVector(1, 1, 1);
			FVector V(0, 0, 0);
			if (IndexMin.X < FieldSize.X && IndexMin.Y < FieldSize.Y && IndexMin.Z < FieldSize.Z &&
				IndexMax.X < FieldSize.X && IndexMax.Y < FieldSize.Y && IndexMax.Z < FieldSize.Z)
			{

				FVector SampleFraction = SamplePoint - IndexMin;

				FVector V000 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				FVector V100 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				FVector V010 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				FVector V110 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				FVector V001 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];
				FVector V101 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];
				FVector V011 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];
				FVector V111 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];

				// Blend x-axis
				FVector V00 = FMath::Lerp(V000, V100, SampleFraction.X);
				FVector V01 = FMath::Lerp(V001, V101, SampleFraction.X);
				FVector V10 = FMath::Lerp(V010, V110, SampleFraction.X);
				FVector V11 = FMath::Lerp(V011, V111, SampleFraction.X);

				// Blend y-axis
				FVector V0 = FMath::Lerp(V00, V10, SampleFraction.Y);
				FVector V1 = FMath::Lerp(V01, V11, SampleFraction.Y);

				// Blend z-axis
				V = FMath::Lerp(V0, V1, SampleFraction.Z);

				//UE_LOG(LogFieldSystem, Warning, TEXT("Instance Index = %d | Sample Position = %s | Sample Result = %s | Sample Index = %d"),
				//	InstanceIdx, *SamplePoint.ToString(), *V000.ToString(), int32(IndexMin.X + FieldSize.X * IndexMin.Y +
				//		FieldSize.X * FieldSize.Y * IndexMin.Z));
			}

			// Write final output...
			*OutVectoreFieldXParam.GetDest() = V.X;
			*OutVectoreFieldYParam.GetDest() = V.Y;
			*OutVectoreFieldZParam.GetDest() = V.Z;

			SamplePositionXParam.Advance();
			SamplePositionXParam.Advance();
			SamplePositionXParam.Advance();

			OutVectoreFieldXParam.Advance();
			OutVectoreFieldYParam.Advance();
			OutVectoreFieldZParam.Advance();
		}
	}
}

void SampleScalarField(FVectorVMContext& Context, const EFieldPhysicsType ScalarType, const int32 VectorIndex)
{
	VectorVM::FUserPtrHandler<FNDIFieldSystemData> InstData(Context);

	// Inputs 
	VectorVM::FExternalFuncInputHandler<float> SamplePositionXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> SamplePositionYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> SamplePositionZParam(Context);

	// Outputs...
	VectorVM::FExternalFuncRegisterHandler<float> OutScalarFieldParam(Context);

	const bool HasValidArrays = InstData && InstData->FieldSystemBuffer && InstData->FieldSystemBuffer->AssetArrays &&
		(InstData->FieldSystemBuffer->AssetArrays->ScalarFieldDatas.Num() != 0);
	if (HasValidArrays)
	{
		const FVector MinBounds = InstData->FieldSystemBuffer->AssetArrays->MinBounds;
		const FVector MaxBounds = InstData->FieldSystemBuffer->AssetArrays->MaxBounds;

		const FIntVector FieldDimensions = InstData->FieldSystemBuffer->AssetArrays->FieldDimensions;
		const int32 TypeSize = FieldDimensions.X * FieldDimensions.Y * FieldDimensions.Z;

		const FVector FieldSize(FieldDimensions.X, FieldDimensions.Y, FieldDimensions.Z * ScalarTypes.Num());
		const FVector BoundSize = MaxBounds - MinBounds;
		const FVector InverseBounds = (BoundSize.X > 0.0 && BoundSize.Y > 0.0 && BoundSize.Z > 0.0) ?
			FVector(1, 1, 1) / BoundSize : FVector(0, 0, 0);

		float* FieldData = &InstData->FieldSystemBuffer->AssetArrays->ScalarFieldDatas[0];

		for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
		{
			FVector SamplePoint = (FVector(SamplePositionXParam.Get(),
				SamplePositionYParam.Get(),
				SamplePositionZParam.Get()) - MinBounds) * InverseBounds;

			SamplePoint = FVector(FMath::Clamp(SamplePoint.X, 0.0f, 1.0f),
				FMath::Clamp(SamplePoint.Y, 0.0f, 1.0f),
				FMath::Clamp(SamplePoint.Z, 0.0f, 1.0f));

			SamplePoint.Z = (ScalarTypes.Num() != 0) ?
				(SamplePoint.Z * (1.0 - 1.0 / FieldDimensions.Z) + VectorIndex) / ScalarTypes.Num() : SamplePoint.Z;

			SamplePoint = SamplePoint * FieldSize;

			FVector IndexMin(FGenericPlatformMath::FloorToFloat(SamplePoint.X),
				FGenericPlatformMath::FloorToFloat(SamplePoint.Y),
				FGenericPlatformMath::FloorToFloat(SamplePoint.Z));

			FVector IndexMax = IndexMin + FVector(1, 1, 1);
			float V = 0.0;
			if (IndexMin.X < FieldSize.X && IndexMin.Y < FieldSize.Y && IndexMin.Z < FieldSize.Z &&
				IndexMax.X < FieldSize.X && IndexMax.Y < FieldSize.Y && IndexMax.Z < FieldSize.Z)
			{

				FVector SampleFraction = SamplePoint - IndexMin;

				float V000 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				float V100 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				float V010 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				float V110 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				float V001 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];
				float V101 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];
				float V011 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];
				float V111 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];

				// Blend x-axis
				float V00 = FMath::Lerp(V000, V100, SampleFraction.X);
				float V01 = FMath::Lerp(V001, V101, SampleFraction.X);
				float V10 = FMath::Lerp(V010, V110, SampleFraction.X);
				float V11 = FMath::Lerp(V011, V111, SampleFraction.X);

				// Blend y-axis
				float V0 = FMath::Lerp(V00, V10, SampleFraction.Y);
				float V1 = FMath::Lerp(V01, V11, SampleFraction.Y);

				// Blend z-axis
				V = FMath::Lerp(V0, V1, SampleFraction.Z);

				//UE_LOG(LogFieldSystem, Warning, TEXT("Instance Index = %d | Sample Position = %s | Sample Result = %s | Sample Index = %d"),
				//	InstanceIdx, *SamplePoint.ToString(), *V000.ToString(), int32(IndexMin.X + FieldSize.X * IndexMin.Y +
				//		FieldSize.X * FieldSize.Y * IndexMin.Z));
			}

			// Write final output...
			*OutScalarFieldParam.GetDest() = V;

			SamplePositionXParam.Advance();
			SamplePositionXParam.Advance();
			SamplePositionXParam.Advance();

			OutScalarFieldParam.Advance();
		}
	}
}


void SampleIntegerField(FVectorVMContext& Context, const EFieldPhysicsType ScalarType, const int32 VectorIndex)
{
	VectorVM::FUserPtrHandler<FNDIFieldSystemData> InstData(Context);

	// Inputs 
	VectorVM::FExternalFuncInputHandler<float> SamplePositionXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> SamplePositionYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> SamplePositionZParam(Context);

	// Outputs...
	VectorVM::FExternalFuncRegisterHandler<int32> OutIntegerFieldParam(Context);

	const bool HasValidArrays = InstData && InstData->FieldSystemBuffer && InstData->FieldSystemBuffer->AssetArrays &&
		(InstData->FieldSystemBuffer->AssetArrays->IntegerFieldDatas.Num() != 0);
	if (HasValidArrays)
	{
		const FVector MinBounds = InstData->FieldSystemBuffer->AssetArrays->MinBounds;
		const FVector MaxBounds = InstData->FieldSystemBuffer->AssetArrays->MaxBounds;

		const FIntVector FieldDimensions = InstData->FieldSystemBuffer->AssetArrays->FieldDimensions;
		const int32 TypeSize = FieldDimensions.X * FieldDimensions.Y * FieldDimensions.Z;

		const FVector FieldSize(FieldDimensions.X, FieldDimensions.Y, FieldDimensions.Z * IntegerTypes.Num());
		const FVector BoundSize = MaxBounds - MinBounds;
		const FVector InverseBounds = (BoundSize.X > 0.0 && BoundSize.Y > 0.0 && BoundSize.Z > 0.0) ?
			FVector(1, 1, 1) / BoundSize : FVector(0, 0, 0);

		int32* FieldData = &InstData->FieldSystemBuffer->AssetArrays->IntegerFieldDatas[0];

		for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
		{
			FVector SamplePoint = (FVector(SamplePositionXParam.Get(),
				SamplePositionYParam.Get(),
				SamplePositionZParam.Get()) - MinBounds) * InverseBounds;

			SamplePoint = FVector(FMath::Clamp(SamplePoint.X, 0.0f, 1.0f),
				FMath::Clamp(SamplePoint.Y, 0.0f, 1.0f),
				FMath::Clamp(SamplePoint.Z, 0.0f, 1.0f));

			SamplePoint.Z = (IntegerTypes.Num() != 0) ?
				(SamplePoint.Z * (1.0 - 1.0 / FieldDimensions.Z) + VectorIndex) / IntegerTypes.Num() : SamplePoint.Z;

			SamplePoint = SamplePoint * FieldSize;

			FVector IndexMin(FGenericPlatformMath::FloorToFloat(SamplePoint.X),
				FGenericPlatformMath::FloorToFloat(SamplePoint.Y),
				FGenericPlatformMath::FloorToFloat(SamplePoint.Z));

			FVector IndexMax = IndexMin + FVector(1, 1, 1);
			float V = 0.0;
			if (IndexMin.X < FieldSize.X && IndexMin.Y < FieldSize.Y && IndexMin.Z < FieldSize.Z &&
				IndexMax.X < FieldSize.X && IndexMax.Y < FieldSize.Y && IndexMax.Z < FieldSize.Z)
			{

				FVector SampleFraction = SamplePoint - IndexMin;

				float V000 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				float V100 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				float V010 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				float V110 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMin.Z)];
				float V001 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];
				float V101 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMin.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];
				float V011 = FieldData[int32(IndexMin.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];
				float V111 = FieldData[int32(IndexMax.X + FieldSize.X * IndexMax.Y +
					FieldSize.X * FieldSize.Y * IndexMax.Z)];

				// Blend x-axis
				float V00 = FMath::Lerp(V000, V100, SampleFraction.X);
				float V01 = FMath::Lerp(V001, V101, SampleFraction.X);
				float V10 = FMath::Lerp(V010, V110, SampleFraction.X);
				float V11 = FMath::Lerp(V011, V111, SampleFraction.X);

				// Blend y-axis
				float V0 = FMath::Lerp(V00, V10, SampleFraction.Y);
				float V1 = FMath::Lerp(V01, V11, SampleFraction.Y);

				// Blend z-axis
				V = FMath::Lerp(V0, V1, SampleFraction.Z);

				//UE_LOG(LogFieldSystem, Warning, TEXT("Instance Index = %d | Sample Position = %s | Sample Result = %s | Sample Index = %d"),
				//	InstanceIdx, *SamplePoint.ToString(), *V000.ToString(), int32(IndexMin.X + FieldSize.X * IndexMin.Y +
				//		FieldSize.X * FieldSize.Y * IndexMin.Z));
			}

			// Write final output...
			*OutIntegerFieldParam.GetDest() = V;

			SamplePositionXParam.Advance();
			SamplePositionXParam.Advance();
			SamplePositionXParam.Advance();

			OutIntegerFieldParam.Advance();
		}
	}
}

void UNiagaraDataInterfaceFieldSystem::SampleLinearVelocity(FVectorVMContext& Context)
{
	SampleVectorField(Context, EFieldPhysicsType::Field_LinearVelocity, 
		EFieldVectorIndices::Vector_LinearVelocity);
}

void UNiagaraDataInterfaceFieldSystem::SampleAngularVelocity(FVectorVMContext& Context)
{
	SampleVectorField(Context, EFieldPhysicsType::Field_AngularVelociy, 
		EFieldVectorIndices::Vector_AngularVelocity);
}

void UNiagaraDataInterfaceFieldSystem::SampleLinearForce(FVectorVMContext& Context)
{
	SampleVectorField(Context, EFieldPhysicsType::Field_LinearForce, 
		EFieldVectorIndices::Vector_LinearForce);
}

void UNiagaraDataInterfaceFieldSystem::SampleAngularTorque(FVectorVMContext& Context)
{
	SampleVectorField(Context, EFieldPhysicsType::Field_AngularTorque,
		EFieldVectorIndices::Vector_AngularTorque);
}

void UNiagaraDataInterfaceFieldSystem::SamplePositionTarget(FVectorVMContext& Context)
{
	SampleVectorField(Context, EFieldPhysicsType::Field_PositionStatic, 
		EFieldVectorIndices::Vector_PositionTarget);
}

void UNiagaraDataInterfaceFieldSystem::SampleExternalClusterStrain(FVectorVMContext& Context)
{
	SampleScalarField(Context, EFieldPhysicsType::Field_ExternalClusterStrain, 
		EFieldScalarIndices::Scalar_ExternalClusterStrain);
}

void UNiagaraDataInterfaceFieldSystem::SampleInternalClusterStrain(FVectorVMContext& Context)
{
	SampleScalarField(Context, EFieldPhysicsType::Field_InternalClusterStrain,
		EFieldScalarIndices::Scalar_InternalClusterStrain);
}

void UNiagaraDataInterfaceFieldSystem::SampleSleepingThreshold(FVectorVMContext& Context)
{
	SampleScalarField(Context, EFieldPhysicsType::Field_SleepingThreshold,
		EFieldScalarIndices::Scalar_SleepingThreshold);
}

void UNiagaraDataInterfaceFieldSystem::SampleDisableThreshold(FVectorVMContext& Context)
{
	SampleScalarField(Context, EFieldPhysicsType::Field_DisableThreshold,
		EFieldScalarIndices::Scalar_DisableThreshold);
}

void UNiagaraDataInterfaceFieldSystem::SampleFieldKill(FVectorVMContext& Context)
{
	SampleScalarField(Context, EFieldPhysicsType::Field_Kill,
		EFieldScalarIndices::Scalar_Kill);
}

void UNiagaraDataInterfaceFieldSystem::SampleDynamicConstraint(FVectorVMContext& Context)
{
	SampleScalarField(Context, EFieldPhysicsType::Field_DynamicConstraint,
		EFieldScalarIndices::Scalar_DynamicConstraint);
}

void UNiagaraDataInterfaceFieldSystem::SampleDynamicState(FVectorVMContext& Context)
{
	SampleIntegerField(Context, EFieldPhysicsType::Field_DynamicState,
		EFieldIntegerIndices::Integer_DynamicState);
}

void UNiagaraDataInterfaceFieldSystem::SampleCollisionGroup(FVectorVMContext& Context)
{
	SampleIntegerField(Context, EFieldPhysicsType::Field_CollisionGroup,
		EFieldIntegerIndices::Integer_CollisionGroup);
}

void UNiagaraDataInterfaceFieldSystem::SamplePositionStatic(FVectorVMContext& Context)
{
	SampleIntegerField(Context, EFieldPhysicsType::Field_PositionStatic,
		EFieldIntegerIndices::Integer_PositionStatic);
}

void UNiagaraDataInterfaceFieldSystem::SamplePositionAnimated(FVectorVMContext& Context)
{
	SampleIntegerField(Context, EFieldPhysicsType::Field_PositionAnimated,
		EFieldIntegerIndices::Integer_PositionAnimated);
}

void UNiagaraDataInterfaceFieldSystem::SampleActivateDisabled(FVectorVMContext& Context)
{
	SampleIntegerField(Context, EFieldPhysicsType::Field_ActivateDisabled,
		EFieldIntegerIndices::Integer_ActivateDisabled);
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
		void {InstanceFunctionName}(in float3 SamplePosition, out float3 OutLinearVelocity)
		{
			{FieldSystemContextName}
			OutLinearVelocity = DIFieldSystem_SampleFieldVector(DIContext,SamplePosition,LINEAR_VELOCITY,VECTOR_LINEARVELOCITY);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleLinearForceName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float3 OutLinearForce)
		{
			{FieldSystemContextName}
			OutLinearForce = DIFieldSystem_SampleFieldVector(DIContext,SamplePosition,LINEAR_FORCE,VECTOR_LINEARFORCE);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleAngularVelocityName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float3 OutAngularVelocity)
		{
			{FieldSystemContextName}
			OutAngularVelocity = DIFieldSystem_SampleFieldVector(DIContext,SamplePosition,ANGULAR_VELOCITY,VECTOR_ANGULARVELOCITY);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleAngularTorqueName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float3 OutAngularTorque)
		{
			{FieldSystemContextName}
			OutAngularTorque = DIFieldSystem_SampleFieldVector(DIContext,SamplePosition,ANGULAR_TORQUE,VECTOR_ANGULARTORQUE);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SamplePositionTargetName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float3 OutPositionTarget)
		{
			{FieldSystemContextName}
			OutPositionTorque = DIFieldSystem_SampleFieldVector(DIContext,SamplePosition,POSITION_TARGET,VECTOR_POSITIONTARGET);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleExternalClusterStrainName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float OutExternalClusterStrain)
		{
			{FieldSystemContextName}
			OutExternalClusterStrain = DIFieldSystem_SampleFieldScalar(DIContext,SamplePosition,EXTERNAL_CLUSTER_STRAIN,SCALAR_EXTERNALCLUSTERSTRAIN);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleFieldKillName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float OutFieldKill)
		{
			{FieldSystemContextName}
			OutFieldKill = DIFieldSystem_SampleFieldScalar(DIContext,SamplePosition,FIELD_KILL,SCALAR_FIELDKILL);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleSleepingThresholdName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float OutSleepingThreshold)
		{
			{FieldSystemContextName}
			OutSleepingThreshold = DIFieldSystem_SampleFieldScalar(DIContext,SamplePosition,SLEEPING_THRESHOLD,SCALAR_SLEEPINGTHRESHOLD);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleDisableThresholdName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float OutDisableThreshold)
		{
			{FieldSystemContextName}
			OutSleepingThreshold = DIFieldSystem_SampleFieldScalar(DIContext,SamplePosition,DISABLE_THRESHOLD,SCALAR_DISABLETHRESHOLD);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleInternalClusterStrainName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float OutInternalClusterStrain)
		{
			{FieldSystemContextName}
			OutInternalClusterStrain = DIFieldSystem_SampleFieldScalar(DIContext,SamplePosition,INTERNAL_CLUSTER_STRAIN,SCALAR_INTERNALCLUSTERSTRAIN);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleDynamicConstraintName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float OutDynamicConstraint)
		{
			{FieldSystemContextName}
			OutDynamicConstraint = DIFieldSystem_SampleFieldScalar(DIContext,SamplePosition,DYNAMIC_CONSTRAINT,SCALAR_DYNAMICCONSTRAINT);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleDynamicStateName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out int OutDynamicState)
		{
			{FieldSystemContextName}
			OutDynamicState = DIFieldSystem_SampleFieldInteger(DIContext,SamplePosition,DYNAMIC_STATE,INTEGER_DYNAMICSTATE);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleActivateDisabledName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out int OutActivateDisabled)
		{
			{FieldSystemContextName}
			OutActivateDisabled = DIFieldSystem_SampleFieldInteger(DIContext,SamplePosition,ACTIVATE_DISABLED,INTEGER_ACTIVATEDISABLED);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleCollisionGroupName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out int OutCollisionGroup)
		{
			{FieldSystemContextName}
			OutCollisionGroup = DIFieldSystem_SampleFieldInteger(DIContext,SamplePosition,COLLISION_GROUP,INTEGER_COLLISIONGROUP);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SamplePositionAnimatedName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out int OutPositionAnimated)
		{
			{FieldSystemContextName}
			OutPositionAnimated = DIFieldSystem_SampleFieldInteger(DIContext,SamplePosition,POSITION_ANIMATED,INTEGER_POSITIONANIMATED);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SamplePositionStaticName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out int OutPositionStatic)
		{
			{FieldSystemContextName}
			OutPositionStatic = DIFieldSystem_SampleFieldInteger(DIContext,SamplePosition,POSITION_STATIC,INTEGER_POSITIONSTATIC);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetFieldDimensionsName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float3 OutFieldDimensions)
		{
			{FieldSystemContextName}
			OutFieldDimensions = DIContext.FieldDimensions);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetFieldBoundsName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 SamplePosition, out float3 OutMinBounds, out float3 OutMaxBounds)
		{
			{FieldSystemContextName}
			OutMinBounds = DIContext.MinBounds;
			OutMaxBounds = DICOntext.MaxBounds;
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
	OutHLSL += TEXT("#include \"/Plugin/Experimental/ChaosNiagara/NiagaraDataInterfaceFieldSystem.ush\"\n");
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