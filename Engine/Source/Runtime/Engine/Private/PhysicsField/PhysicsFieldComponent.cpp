// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsField/PhysicsFieldComponent.h"
#include "PrimitiveSceneProxy.h"
#include "RHIStaticStates.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "SceneManagement.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "ShaderParameterUtils.h"
#include "MeshMaterialShader.h"
#include "Field/FieldSystemNodes.h"

/**
*	Consolde variables
* 
*/

/** Clipmap enable/disable */
static TAutoConsoleVariable<int32> CVarPhysicsFieldEnableClipmap(
	TEXT("r.PhysicsField.EnableField"),
	0,
	TEXT("Enable/Disable the Physics field clipmap"),
	ECVF_RenderThreadSafe);

/** Clipmap max disatnce */
float GPhysicsFieldClipmapDistance = 10000;
FAutoConsoleVariableRef CVarPhysicsFieldClipmapDistance(
	TEXT("r.PhysicsField.ClipmapDistance"),
	GPhysicsFieldClipmapDistance,
	TEXT("Max distance from the clipmap center"),
	ECVF_RenderThreadSafe
);

/** Number of used clipmaps */
int32 GPhysicsFieldClipmapCount = 4;
FAutoConsoleVariableRef CVarPhysicsFieldClipmapCount(
	TEXT("r.PhysicsField.ClipmapCount"),
	GPhysicsFieldClipmapCount,
	TEXT("Number of clipmaps used for the physics field"),
	ECVF_RenderThreadSafe
);

/** Exponent used to compute each clipmaps distance */
float GPhysicsFieldClipmapExponent = 2;
FAutoConsoleVariableRef CVarPhysicsFieldClipmapExponent(
	TEXT("r.PhysicsField.ClipmapExponent"),
	GPhysicsFieldClipmapExponent,
	TEXT("Exponent used to derive each clipmap's size, together with r.PhysicsField.ClipmapDistance"),
	ECVF_RenderThreadSafe
);

/** Resolution of each clipmaps */
int32 GPhysicsFieldClipmapResolution = 64;
FAutoConsoleVariableRef CVarPhysicsFieldClipmapResolution(
	TEXT("r.PhysicsField.ClipmapResolution"),
	GPhysicsFieldClipmapResolution,
	TEXT("Resolution of the physics field.  Higher values increase fidelity but also increase memory and composition cost."),
	ECVF_RenderThreadSafe
);

/**
*	Resource Utilities
*/

template<typename BufferType, int ElementSize, EPixelFormat PixelFormat>
void InitInternalBuffer(const uint32 ElementCount, FRWBuffer& OutputBuffer)
{
	if (ElementCount > 0)
	{
		const uint32 BufferCount = ElementCount * ElementSize;
		const uint32 BufferBytes = sizeof(BufferType) * BufferCount;
		
		OutputBuffer.Initialize(sizeof(BufferType), BufferCount, PixelFormat, BUF_Static);
	}
}

template<typename BufferType, int ElementSize, EPixelFormat PixelFormat>
void UpdateInternalBuffer(const uint32 ElementCount, const BufferType* InputData, FRWBuffer& OutputBuffer)
{
	if (ElementCount > 0 && InputData)
	{
		const uint32 BufferCount = ElementCount * ElementSize;
		const uint32 BufferBytes = sizeof(BufferType) * BufferCount;

		void* OutputData = RHILockVertexBuffer(OutputBuffer.Buffer, 0, BufferBytes, RLM_WriteOnly);

		FMemory::Memcpy(OutputData, InputData, BufferBytes);
		RHIUnlockVertexBuffer(OutputBuffer.Buffer);
	}
}

template<typename BufferType, int ElementSize, EPixelFormat PixelFormat>
void InitInternalTexture(const uint32 SizeX, const uint32 SizeY, const uint32 SizeZ, FTextureRWBuffer3D& OutputBuffer)
{
	if (SizeX * SizeY * SizeZ > 0)
	{
		const uint32 BlockBytes = sizeof(BufferType) * ElementSize;

		OutputBuffer.Initialize(BlockBytes, SizeX, SizeY, SizeZ, PixelFormat);
	}
}

template<typename BufferType, int ElementSize, EPixelFormat PixelFormat>
void UpdateInternalTexture(const uint32 SizeX, const uint32 SizeY, const uint32 SizeZ, const BufferType* InputData, FTextureRWBuffer3D& OutputBuffer)
{
	if (SizeX * SizeY * SizeZ > 0 && InputData)
	{
		const uint32 BlockBytes = sizeof(BufferType) * ElementSize;

		FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, SizeX, SizeY, SizeZ);

		const uint8* TextureDatas = (const uint8*)InputData;
		RHIUpdateTexture3D(OutputBuffer.Buffer, 0, UpdateRegion, SizeX * BlockBytes,
			SizeX * SizeY * BlockBytes, TextureDatas);
	}
}

/**
*	Clipmap construction
*/

class FBuildPhysicsFieldClipmapCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBuildPhysicsFieldClipmapCS, Global)

public:

	static const uint32 ThreadGroupSize = 4;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("BUILD_FIELD_THREADGROUP_SIZEX"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("BUILD_FIELD_THREADGROUP_SIZEY"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("BUILD_FIELD_THREADGROUP_SIZEZ"), ThreadGroupSize);
	}

	FBuildPhysicsFieldClipmapCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NodesParams.Bind(Initializer.ParameterMap, TEXT("NodesParams"));
		NodesOffsets.Bind(Initializer.ParameterMap, TEXT("NodesOffsets"));
		TargetsOffsets.Bind(Initializer.ParameterMap, TEXT("TargetsOffsets"));
		FieldClipmap.Bind(Initializer.ParameterMap, TEXT("FieldClipmap"));

		ClipmapResolution.Bind(Initializer.ParameterMap, TEXT("ClipmapResolution"));
		ClipmapDistance.Bind(Initializer.ParameterMap, TEXT("ClipmapDistance"));
		ClipmapCenter.Bind(Initializer.ParameterMap, TEXT("ClipmapCenter"));
		ClipmapCount.Bind(Initializer.ParameterMap, TEXT("ClipmapCount"));
		ClipmapExponent.Bind(Initializer.ParameterMap, TEXT("ClipmapExponent"));

		ClipmapIndex.Bind(Initializer.ParameterMap, TEXT("ClipmapIndex"));
		DatasIndex.Bind(Initializer.ParameterMap, TEXT("DatasIndex"));
		TargetType.Bind(Initializer.ParameterMap, TEXT("TargetType"));
	}

	FBuildPhysicsFieldClipmapCS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, FPhysicsFieldResource* FieldResource, 
		const int32 InClipmapIndex, const int32 InDatasIndex, const EFieldPhysicsType InTargetType)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		if (FieldResource)
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, FieldResource->FieldClipmap.UAV);

			SetSRVParameter(RHICmdList, ShaderRHI, NodesParams, FieldResource->NodesParams.SRV);
			SetSRVParameter(RHICmdList, ShaderRHI, NodesOffsets, FieldResource->NodesOffsets.SRV);
			SetSRVParameter(RHICmdList, ShaderRHI, TargetsOffsets, FieldResource->TargetsOffsets.SRV);
			SetUAVParameter(RHICmdList, ShaderRHI, FieldClipmap, FieldResource->FieldClipmap.UAV);

			SetShaderValue(RHICmdList, ShaderRHI, ClipmapResolution, FieldResource->FieldInfos.ClipmapResolution);
			SetShaderValue(RHICmdList, ShaderRHI, ClipmapDistance, FieldResource->FieldInfos.ClipmapDistance);
			SetShaderValue(RHICmdList, ShaderRHI, ClipmapCount, FieldResource->FieldInfos.ClipmapCount);
			SetShaderValue(RHICmdList, ShaderRHI, ClipmapCenter, FieldResource->FieldInfos.ClipmapCenter);
			SetShaderValue(RHICmdList, ShaderRHI, ClipmapExponent, FieldResource->FieldInfos.ClipmapExponent);

			SetShaderValue(RHICmdList, ShaderRHI, DatasIndex, InDatasIndex);
			SetShaderValue(RHICmdList, ShaderRHI, TargetType, InTargetType);
			SetShaderValue(RHICmdList, ShaderRHI, ClipmapIndex, InClipmapIndex);
		}
	}

	void UnsetParameters(FRHICommandList& RHICmdList, FPhysicsFieldResource* FieldResource)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, FieldClipmap, nullptr);
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, FieldResource->FieldClipmap.UAV);
	}

private:
	
	LAYOUT_FIELD(FShaderResourceParameter, NodesParams);
	LAYOUT_FIELD(FShaderResourceParameter, NodesOffsets);
	LAYOUT_FIELD(FShaderResourceParameter, TargetsOffsets);
	LAYOUT_FIELD(FShaderResourceParameter, FieldClipmap);

	LAYOUT_FIELD(FShaderParameter, ClipmapResolution);
	LAYOUT_FIELD(FShaderParameter, ClipmapDistance);
	LAYOUT_FIELD(FShaderParameter, ClipmapCenter);
	LAYOUT_FIELD(FShaderParameter, ClipmapCount);
	LAYOUT_FIELD(FShaderParameter, ClipmapExponent);

	LAYOUT_FIELD(FShaderParameter, ClipmapIndex);
	LAYOUT_FIELD(FShaderParameter, DatasIndex);
	LAYOUT_FIELD(FShaderParameter, TargetType);
};

IMPLEMENT_SHADER_TYPE(, FBuildPhysicsFieldClipmapCS, TEXT("/Engine/Private/PhysicsFieldBuilder.usf"), TEXT("BuildPhysicsFieldClipmapCS"), SF_Compute);

/**
*	FPhysicsFieldResource
*/

FPhysicsFieldResource::FPhysicsFieldResource(const int32 TargetCount, const TArray<EFieldPhysicsType>& TargetTypes, 
		const TStaticArray<int32, MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY>& VectorTargets, const TStaticArray<int32, MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY>& ScalarTargets, const TStaticArray<int32, MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY>& IntegerTargets) : FRenderResource()
{
	FieldInfos.TargetCount = TargetCount;
	FieldInfos.TargetTypes = TargetTypes;
	FieldInfos.VectorTargets = VectorTargets;
	FieldInfos.ScalarTargets = ScalarTargets;
	FieldInfos.IntegerTargets = IntegerTargets;

	FieldInfos.ClipmapExponent = GPhysicsFieldClipmapExponent;
	FieldInfos.ClipmapCount = GPhysicsFieldClipmapCount;
	FieldInfos.ClipmapDistance = GPhysicsFieldClipmapDistance;
	FieldInfos.ClipmapResolution = GPhysicsFieldClipmapResolution;
}

void FPhysicsFieldResource::InitRHI()
{
	const int32 DatasCount = FieldInfos.ClipmapCount * FieldInfos.TargetCount;
	InitInternalTexture<float, 1, EPixelFormat::PF_R32_FLOAT>(FieldInfos.ClipmapResolution, FieldInfos.ClipmapResolution, FieldInfos.ClipmapResolution * DatasCount + DatasCount-1, FieldClipmap);
	InitInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT>(EFieldPhysicsType::Field_PhysicsType_Max + 1, TargetsOffsets);
}

void FPhysicsFieldResource::ReleaseRHI()
{
	FieldClipmap.Release();
	NodesParams.Release();
	NodesOffsets.Release();
	TargetsOffsets.Release();
}

void FPhysicsFieldResource::UpdateResource(FRHICommandListImmediate& RHICmdList, const int32 NodesCount, const int32 ParamsCount,
				const int32* TargetsOffsetsDatas, const int32* NodesOffsetsDatas, const float* NodesParamsDatas)
{
	
	InitInternalBuffer<float, 1, EPixelFormat::PF_R32_FLOAT>(ParamsCount, NodesParams);
	InitInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT>(NodesCount, NodesOffsets);

	UpdateInternalBuffer<float, 1, EPixelFormat::PF_R32_FLOAT>(ParamsCount, NodesParamsDatas, NodesParams);
	UpdateInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT>(NodesCount, NodesOffsetsDatas, NodesOffsets);
	UpdateInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT>(EFieldPhysicsType::Field_PhysicsType_Max + 1, TargetsOffsetsDatas, TargetsOffsets);

	FieldInfos.ClipmapCenter = FieldInfos.ViewOrigin;

	TShaderMapRef<FBuildPhysicsFieldClipmapCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
	RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

	const uint32 NumGroups = FMath::DivideAndRoundUp<int32>(FieldInfos.ClipmapResolution, FBuildPhysicsFieldClipmapCS::ThreadGroupSize);

	for (int32 ClipmapIndex = 0; ClipmapIndex < FieldInfos.ClipmapCount; ++ClipmapIndex)
	{
		int32 DatasIndex = 0;
		for (auto& TargetType : FieldInfos.TargetTypes)
		{
			ComputeShader->SetParameters(RHICmdList, this, ClipmapIndex, DatasIndex, TargetType);
			DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), NumGroups, NumGroups, NumGroups);
			ComputeShader->UnsetParameters(RHICmdList, this);

			DatasIndex += (GetFieldTargetOutput(TargetType) == EFieldOutputType::Field_Output_Vector) ? 3 : 1;
		}
	}
}

/**
*	FPhysicsFieldInstance
*/

void FPhysicsFieldInstance::InitInstance( const TArray<EFieldPhysicsType>& TargetTypes)
{
	TStaticArray<int32,MAX_TARGETS_ARRAY, MAX_TARGETS_ARRAY> VectorTargets(-1), ScalarTargets(-1), IntegerTargets(-1);

	static const TArray<EFieldPhysicsType> VectorTypes = GetFieldTargetTypes(EFieldOutputType::Field_Output_Vector);
	static const TArray<EFieldPhysicsType> ScalarTypes = GetFieldTargetTypes(EFieldOutputType::Field_Output_Scalar);
	static const TArray<EFieldPhysicsType> IntegerTypes = GetFieldTargetTypes(EFieldOutputType::Field_Output_Integer);

	int32 TargetIndex = 0;
	int32 TargetCount = 0;
	for (auto& TargetType : TargetTypes)
	{
		const EFieldOutputType OutputType = GetFieldTargetIndex(VectorTypes, ScalarTypes, IntegerTypes, TargetType, TargetIndex);
		if (OutputType == EFieldOutputType::Field_Output_Vector)
		{
			VectorTargets[TargetIndex] = TargetCount;
			TargetCount += 3;
		}
		else if (OutputType == EFieldOutputType::Field_Output_Scalar)
		{
			ScalarTargets[TargetIndex] = TargetCount;
			TargetCount += 1;
		}
		else if (OutputType == EFieldOutputType::Field_Output_Integer)
		{
			IntegerTargets[TargetIndex] = TargetCount;
			TargetCount += 1;
		}
	}
	
	if (!FieldResource)
	{
		FieldResource = new FPhysicsFieldResource(TargetCount, TargetTypes, VectorTargets, ScalarTargets, IntegerTargets);

		FPhysicsFieldResource* LocalFieldResource = FieldResource;
		ENQUEUE_RENDER_COMMAND(FInitPhysicsFieldResourceCommand)(
			[LocalFieldResource](FRHICommandList& RHICmdList)
			{
				LocalFieldResource->InitResource();
			});
	}
}

void FPhysicsFieldInstance::ReleaseInstance()
{
	if (FieldResource)
	{
		FPhysicsFieldResource* LocalFieldResource = FieldResource;
		ENQUEUE_RENDER_COMMAND(FDestroyPhysicsFieldResourceCommand)(
			[LocalFieldResource](FRHICommandList& RHICmdList)
			{
				LocalFieldResource->ReleaseResource();
				delete LocalFieldResource;
			});
		FieldResource = nullptr;
	}

	NodesOffsets.Empty();
	NodesParams.Empty();
}

void FPhysicsFieldInstance::UpdateInstance(const TArray<FFieldSystemCommand>& FieldCommands)
{
	NodesOffsets.Empty();
	NodesParams.Empty();

	for (auto& TargetOffset : TargetsOffsets)
	{
		TargetOffset = 0;
	}
	
	for (auto& TargetType : FieldResource->FieldInfos.TargetTypes)
	{
		TArray<FFieldNodeBase*> TargetRoots;
		for (auto& FieldCommand : FieldCommands)
		{
			const EFieldPhysicsType CommandType = GetFieldPhysicsType(FieldCommand.TargetAttribute);
			if (CommandType == TargetType)
			{
				const TUniquePtr<FFieldNodeBase>& RootNode = FieldCommand.RootNode;
				TargetRoots.Add(RootNode.Get());
			}
		}
		FFieldNodeBase* TargetNode = nullptr;
		TArray<FFieldNodeBase*> CompositeNodes;
		if (TargetRoots.Num() == 1)
		{
			TargetNode = TargetRoots[0];
		}
		else if (TargetRoots.Num() > 1)
		{
			const EFieldOutputType OutputType = GetFieldTargetOutput(TargetType);
			if (OutputType == EFieldOutputType::Field_Output_Vector)
			{
				FFieldNode<FVector>* PreviousNode = nullptr;
				FFieldNode<FVector>* NextNode = nullptr;
				for (auto& TargetRoot : TargetRoots)
				{
					NextNode = StaticCast<FFieldNode<FVector>*>(TargetRoot);
					PreviousNode = new FSumVector(1.0, nullptr, PreviousNode,
						NextNode, EFieldOperationType::Field_Add);

					CompositeNodes.Add(PreviousNode);
				}
			}
			else if (OutputType == EFieldOutputType::Field_Output_Scalar)
			{
				FFieldNode<float>* PreviousNode = nullptr;
				FFieldNode<float>* NextNode = nullptr;
				for (auto& TargetRoot : TargetRoots)
				{
					NextNode = StaticCast<FFieldNode<float>*>(TargetRoot);
					PreviousNode = new FSumScalar(1.0, PreviousNode,
						NextNode, EFieldOperationType::Field_Add);

					CompositeNodes.Add(PreviousNode);
				}
			}
			else if (OutputType == EFieldOutputType::Field_Output_Integer)
			{
				FFieldNode<float>* PreviousNode = nullptr;
				FFieldNode<float>* NextNode = nullptr;
				for (auto& TargetRoot : TargetRoots)
				{
					NextNode = new FConversionField<int32, float>(StaticCast<FFieldNode<int32>*>(TargetRoot));
					PreviousNode = new FSumScalar(1.0, PreviousNode,
						NextNode, EFieldOperationType::Field_Add);

					CompositeNodes.Add(PreviousNode);
					CompositeNodes.Add(NextNode);
				}
			}
			TargetNode = CompositeNodes.Last();
		}
		const int32 PreviousNodes = NodesOffsets.Num();
		if (TargetNode)
		{
			BuildNodeParams(TargetNode);
		}
		TargetsOffsets[TargetType + 1] = NodesOffsets.Num()- PreviousNodes;

		for (int32 CompositeIndex = CompositeNodes.Num()-1; CompositeIndex >= 0; --CompositeIndex)
		{
			delete CompositeNodes[CompositeIndex];
		}
		CompositeNodes.Empty();
	}
	for (uint32 FieldIndex = 1; FieldIndex < TargetsCount + 1; ++FieldIndex)
	{
		TargetsOffsets[FieldIndex] += TargetsOffsets[FieldIndex - 1];
	}

	if (FieldResource)
	{
		int32* LocalTargetsOffsets = TargetsOffsets.GetData();
		int32* LocalNodesOffsets = NodesOffsets.GetData();
		float* LocalNodesParams = NodesParams.GetData();

		const int32 LocalNodesCount = NodesOffsets.Num();
		const int32 LocalParamsCount = NodesParams.Num();

		FPhysicsFieldResource* LocalFieldResource = FieldResource;
		ENQUEUE_RENDER_COMMAND(FUpdateFieldInstanceCommand)(
			[LocalFieldResource, LocalNodesCount, LocalParamsCount, LocalNodesParams, LocalNodesOffsets, LocalTargetsOffsets](FRHICommandListImmediate& RHICmdList)
			{
				LocalFieldResource->UpdateResource(RHICmdList,LocalNodesCount, LocalParamsCount,
					LocalTargetsOffsets, LocalNodesOffsets, LocalNodesParams);
			});
	}
}

void FPhysicsFieldInstance::BuildNodeParams(FFieldNodeBase* FieldNode)
{
	if (FieldNode)
	{
		if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FUniformInteger)
		{
			FUniformInteger* LocalNode = StaticCast<FUniformInteger*>(FieldNode);

			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FUniformInteger);
			NodesParams.Add(LocalNode->Magnitude);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FRadialIntMask)
		{
			FRadialIntMask* LocalNode = StaticCast<FRadialIntMask*>(FieldNode);
			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FRadialIntMask);
			NodesParams.Add(LocalNode->Radius);
			NodesParams.Add(LocalNode->Position.X);
			NodesParams.Add(LocalNode->Position.Y);
			NodesParams.Add(LocalNode->Position.Z);
			NodesParams.Add(LocalNode->InteriorValue);
			NodesParams.Add(LocalNode->ExteriorValue);
			NodesParams.Add(LocalNode->SetMaskCondition);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FUniformScalar)
		{
			FUniformScalar* LocalNode = StaticCast<FUniformScalar*>(FieldNode);
			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FUniformScalar);
			NodesParams.Add(LocalNode->Magnitude);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FRadialFalloff)
		{
			FRadialFalloff* LocalNode = StaticCast<FRadialFalloff*>(FieldNode);
			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FRadialFalloff);
			NodesParams.Add(LocalNode->Magnitude);
			NodesParams.Add(LocalNode->MinRange);
			NodesParams.Add(LocalNode->MaxRange);
			NodesParams.Add(LocalNode->Default);
			NodesParams.Add(LocalNode->Radius);
			NodesParams.Add(LocalNode->Position.X);
			NodesParams.Add(LocalNode->Position.Y);
			NodesParams.Add(LocalNode->Position.Z);
			NodesParams.Add(LocalNode->Falloff);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FPlaneFalloff)
		{
			FPlaneFalloff* LocalNode = StaticCast<FPlaneFalloff*>(FieldNode);
			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FPlaneFalloff);
			NodesParams.Add(LocalNode->Magnitude);
			NodesParams.Add(LocalNode->MinRange);
			NodesParams.Add(LocalNode->MaxRange);
			NodesParams.Add(LocalNode->Default);
			NodesParams.Add(LocalNode->Distance);
			NodesParams.Add(LocalNode->Position.X);
			NodesParams.Add(LocalNode->Position.Y);
			NodesParams.Add(LocalNode->Position.Z);
			NodesParams.Add(LocalNode->Normal.X);
			NodesParams.Add(LocalNode->Normal.Y);
			NodesParams.Add(LocalNode->Normal.Z);
			NodesParams.Add(LocalNode->Falloff);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FBoxFalloff)
		{
			FBoxFalloff* LocalNode = StaticCast<FBoxFalloff*>(FieldNode);
			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FBoxFalloff);
			NodesParams.Add(LocalNode->Magnitude);
			NodesParams.Add(LocalNode->MinRange);
			NodesParams.Add(LocalNode->MaxRange);
			NodesParams.Add(LocalNode->Default);
			NodesParams.Add(LocalNode->Transform.GetRotation().X);
			NodesParams.Add(LocalNode->Transform.GetRotation().Y);
			NodesParams.Add(LocalNode->Transform.GetRotation().Z);
			NodesParams.Add(LocalNode->Transform.GetRotation().W);
			NodesParams.Add(LocalNode->Transform.GetTranslation().X);
			NodesParams.Add(LocalNode->Transform.GetTranslation().Y);
			NodesParams.Add(LocalNode->Transform.GetTranslation().Z);
			NodesParams.Add(LocalNode->Transform.GetScale3D().X);
			NodesParams.Add(LocalNode->Transform.GetScale3D().Y);
			NodesParams.Add(LocalNode->Transform.GetScale3D().Z);
			NodesParams.Add(LocalNode->Falloff);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FNoiseField)
		{
			FNoiseField* LocalNode = StaticCast<FNoiseField*>(FieldNode);
			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FNoiseField);
			NodesParams.Add(LocalNode->MinRange);
			NodesParams.Add(LocalNode->MaxRange);
			NodesParams.Add(LocalNode->Transform.GetRotation().X);
			NodesParams.Add(LocalNode->Transform.GetRotation().Y);
			NodesParams.Add(LocalNode->Transform.GetRotation().Z);
			NodesParams.Add(LocalNode->Transform.GetRotation().W);
			NodesParams.Add(LocalNode->Transform.GetTranslation().X);
			NodesParams.Add(LocalNode->Transform.GetTranslation().Y);
			NodesParams.Add(LocalNode->Transform.GetTranslation().Z);
			NodesParams.Add(LocalNode->Transform.GetScale3D().X);
			NodesParams.Add(LocalNode->Transform.GetScale3D().Y);
			NodesParams.Add(LocalNode->Transform.GetScale3D().Z);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FUniformVector)
		{
			FUniformVector* LocalNode = StaticCast<FUniformVector*>(FieldNode);
			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FUniformVector);
			NodesParams.Add(LocalNode->Magnitude);
			NodesParams.Add(LocalNode->Direction.X);
			NodesParams.Add(LocalNode->Direction.Y);
			NodesParams.Add(LocalNode->Direction.Z);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FRadialVector)
		{
			FRadialVector* LocalNode = StaticCast<FRadialVector*>(FieldNode);
			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FRadialVector);
			NodesParams.Add(LocalNode->Magnitude);
			NodesParams.Add(LocalNode->Position.X);
			NodesParams.Add(LocalNode->Position.Y);
			NodesParams.Add(LocalNode->Position.Z);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FRandomVector)
		{
			FRandomVector* LocalNode = StaticCast<FRandomVector*>(FieldNode);
			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FRandomVector);
			NodesParams.Add(LocalNode->Magnitude);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FSumScalar)
		{
			FSumScalar* LocalNode = StaticCast<FSumScalar*>(FieldNode);

			BuildNodeParams(LocalNode->ScalarRight.Get());
			BuildNodeParams(LocalNode->ScalarLeft.Get());

			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FSumScalar);
			NodesParams.Add(LocalNode->Magnitude);
			NodesParams.Add(LocalNode->ScalarRight != nullptr);
			NodesParams.Add(LocalNode->ScalarLeft != nullptr);
			NodesParams.Add(LocalNode->Operation);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FSumVector)
		{
			FSumVector* LocalNode = StaticCast<FSumVector*>(FieldNode);

			BuildNodeParams(LocalNode->Scalar.Get());
			BuildNodeParams(LocalNode->VectorRight.Get());
			BuildNodeParams(LocalNode->VectorLeft.Get());

			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FSumVector);
			NodesParams.Add(LocalNode->Magnitude);
			NodesParams.Add(LocalNode->Scalar.Get() != nullptr);
			NodesParams.Add(LocalNode->VectorRight.Get() != nullptr);
			NodesParams.Add(LocalNode->VectorLeft.Get() != nullptr);
			NodesParams.Add(LocalNode->Operation);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FConversionField)
		{
			if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_Int32)
			{
				FConversionField<float, int32>* LocalNode = StaticCast<FConversionField<float, int32>*>(FieldNode);

				BuildNodeParams(LocalNode->InputField.Get());

				NodesOffsets.Add(NodesParams.Num());
				NodesParams.Add(FieldNode->Type());
				NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FConversionField);
				NodesParams.Add(LocalNode->InputField.Get() != nullptr);
			}
			else if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_Float)
			{
				FConversionField<int32, float>* LocalNode = StaticCast<FConversionField<int32, float>*>(FieldNode);

				BuildNodeParams(LocalNode->InputField.Get());

				NodesOffsets.Add(NodesParams.Num());
				NodesParams.Add(FieldNode->Type());
				NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FConversionField);
				NodesParams.Add(LocalNode->InputField.Get() != nullptr);
			}
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FCullingField)
		{
			if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_Int32)
			{
				FCullingField<int32>* LocalNode = StaticCast<FCullingField<int32>*>(FieldNode);

				BuildNodeParams(LocalNode->Culling.Get());
				BuildNodeParams(LocalNode->Input.Get());

				NodesOffsets.Add(NodesParams.Num());
				NodesParams.Add(FieldNode->Type());
				NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FCullingField);
				NodesParams.Add(LocalNode->Culling.Get() != nullptr);
				NodesParams.Add(LocalNode->Input.Get() != nullptr);
				NodesParams.Add(LocalNode->Operation);
			}
			else if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_Float)
			{
				FCullingField<float>* LocalNode = StaticCast<FCullingField<float>*>(FieldNode);

				BuildNodeParams(LocalNode->Culling.Get());
				BuildNodeParams(LocalNode->Input.Get());

				NodesOffsets.Add(NodesParams.Num());
				NodesParams.Add(FieldNode->Type());
				NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FCullingField);
				NodesParams.Add(LocalNode->Culling.Get() != nullptr);
				NodesParams.Add(LocalNode->Input.Get() != nullptr);
				NodesParams.Add(LocalNode->Operation);
			}
			else if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_FVector)
			{
				FCullingField<FVector>* LocalNode = StaticCast<FCullingField<FVector>*>(FieldNode);

				BuildNodeParams(LocalNode->Culling.Get());
				BuildNodeParams(LocalNode->Input.Get());

				NodesOffsets.Add(NodesParams.Num());
				NodesParams.Add(FieldNode->Type());
				NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FCullingField);
				NodesParams.Add(LocalNode->Culling.Get() != nullptr);
				NodesParams.Add(LocalNode->Input.Get() != nullptr);
				NodesParams.Add(LocalNode->Operation);
			}
		}
	}
}

/**
*	PhysicsFieldComponent
*/

UPhysicsFieldComponent::UPhysicsFieldComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

FPrimitiveSceneProxy* UPhysicsFieldComponent::CreateSceneProxy()
{
	FPrimitiveSceneProxy* Proxy = NULL;
	if (FieldInstance)
	{
		Proxy = new FPhysicsFieldSceneProxy(this);
	}
	return Proxy;
}

void UPhysicsFieldComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	if (SceneProxy && GetWorld() && GetWorld()->Scene)
	{
		GetWorld()->Scene->SetPhysicsField(StaticCast<FPhysicsFieldSceneProxy*>(SceneProxy));
	}
}

void UPhysicsFieldComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();

	if (SceneProxy && GetWorld() && GetWorld()->Scene)
	{
		GetWorld()->Scene->ResetPhysicsField();
	}
}

FBoxSphereBounds UPhysicsFieldComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBoxSphereBounds NewBounds;
	if (FieldInstance && FieldInstance->FieldResource)
	{
		NewBounds.Origin = FieldInstance->FieldResource->FieldInfos.ClipmapCenter;
		NewBounds.BoxExtent = FVector(FieldInstance->FieldResource->FieldInfos.ClipmapDistance);
	}
	else
	{
		NewBounds.Origin = FVector::ZeroVector;
		NewBounds.BoxExtent = FVector(1.0);
	}
	NewBounds.SphereRadius = NewBounds.BoxExtent.Size();

	return NewBounds.TransformBy(LocalToWorld);
}

void UPhysicsFieldComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{}

void UPhysicsFieldComponent::OnRegister()
{
	Super::OnRegister();

	if (!FieldInstance)
	{
		FieldInstance = new FPhysicsFieldInstance();

		//static const TArray<EFieldPhysicsType> TargetTypes = {EFieldPhysicsType::Field_LinearForce};
		static const TArray<EFieldPhysicsType> TargetTypes = { EFieldPhysicsType::Field_LinearForce,
																EFieldPhysicsType::Field_LinearVelocity};
		/*static const TArray<EFieldPhysicsType> TargetTypes = {	EFieldPhysicsType::Field_LinearForce,
																EFieldPhysicsType::Field_LinearVelocity,
																EFieldPhysicsType::Field_AngularVelociy,
																EFieldPhysicsType::Field_AngularTorque,
																EFieldPhysicsType::Field_PositionTarget, 
																EFieldPhysicsType::Field_ExternalClusterStrain,
																EFieldPhysicsType::Field_Kill,
																EFieldPhysicsType::Field_DisableThreshold,
																EFieldPhysicsType::Field_SleepingThreshold,
																EFieldPhysicsType::Field_InternalClusterStrain,
																EFieldPhysicsType::Field_DynamicConstraint,
																EFieldPhysicsType::Field_DynamicState,
																EFieldPhysicsType::Field_ActivateDisabled,
																EFieldPhysicsType::Field_CollisionGroup,
																EFieldPhysicsType::Field_PositionAnimated,
																EFieldPhysicsType::Field_PositionStatic };*/
		FieldInstance->InitInstance(TargetTypes);
	}
}

void UPhysicsFieldComponent::OnUnregister()
{
	if (FieldInstance)
	{
		FieldInstance->ReleaseInstance();

		FPhysicsFieldInstance* LocalFieldInstance = FieldInstance;
		ENQUEUE_RENDER_COMMAND(FDestroyVectorFieldInstanceCommand)(
			[LocalFieldInstance](FRHICommandList& RHICmdList)
			{
				delete LocalFieldInstance;
			});
		FieldInstance = nullptr;
	}
	Super::OnUnregister();
}

void UPhysicsFieldComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	MarkRenderDynamicDataDirty();
}

void UPhysicsFieldComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderTransform_Concurrent();

	if (FieldInstance && FieldCommands.Num() > 0)
	{
		FieldInstance->UpdateInstance(FieldCommands);
		FieldCommands.Empty();
	}
}

void UPhysicsFieldComponent::BufferCommand(const FFieldSystemCommand& FieldCommand)
{
	FieldCommands.Add(FieldCommand);
}


/**
 * FPhysicsFieldSceneProxy.
 */

FPhysicsFieldSceneProxy::FPhysicsFieldSceneProxy(UPhysicsFieldComponent* PhysicsFieldComponent)
	: FPrimitiveSceneProxy(PhysicsFieldComponent)
{
	bWillEverBeLit = false;
	if (PhysicsFieldComponent && PhysicsFieldComponent->FieldInstance)
	{
		FieldResource = PhysicsFieldComponent->FieldInstance->FieldResource;
	}
}

FPhysicsFieldSceneProxy::~FPhysicsFieldSceneProxy()
{}

SIZE_T FPhysicsFieldSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FPhysicsFieldSceneProxy::CreateRenderThreadResources()
{}

FPrimitiveViewRelevance FPhysicsFieldSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bDynamicRelevance = true;
	Result.bOpaque = true;
	return Result;
}

uint32 FPhysicsFieldSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize();
}




