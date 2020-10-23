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
#include "ShaderParameterStruct.h"
#include "MeshMaterialShader.h"
#include "Field/FieldSystemNodes.h"

/**
*	Stats
*
*/

DECLARE_STATS_GROUP(TEXT("PhysicsFields"), STATGROUP_PhysicsFields, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("Create Scene Proxy [GT]"), STAT_PhysicsFields_CreateSceneProxy, STATGROUP_PhysicsFields);
DECLARE_CYCLE_STAT(TEXT("Send Render Data [GT]"), STAT_PhysicsFields_SendRenderData, STATGROUP_PhysicsFields);
DECLARE_CYCLE_STAT(TEXT("UpdateResource [RT]"), STAT_PhysicsFields_UpdateResource_RT, STATGROUP_PhysicsFields);
DECLARE_GPU_STAT(PhysicsFields);

/**
*	Console variables
* 
*/
DEFINE_LOG_CATEGORY_STATIC(LogGlobalField, Log, All);


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
int32 GPhysicsFieldClipmapResolution = 32;
FAutoConsoleVariableRef CVarPhysicsFieldClipmapResolution(
	TEXT("r.PhysicsField.ClipmapResolution"),
	GPhysicsFieldClipmapResolution,
	TEXT("Resolution of the physics field.  Higher values increase fidelity but also increase memory and composition cost."),
	ECVF_RenderThreadSafe
);

/** Single Target Limit */
int32 GPhysicsFieldSingleTarget = 0;
FAutoConsoleVariableRef CVarPhysicsFieldSingleTarget(
	TEXT("r.PhysicsField.SingleTarget"),
	GPhysicsFieldSingleTarget,
	TEXT("Limnit the physics field build to only one target, the linear force"),
	ECVF_RenderThreadSafe
);

/** Spatial culling */
int32 GPhysicsFieldEnableCulling = 1;
FAutoConsoleVariableRef CVarPhysicsFieldEnableCulling(
	TEXT("r.PhysicsField.EnableCulling"),
	GPhysicsFieldEnableCulling,
	TEXT("Enable the spatial culling based on the field nodes bounds"),
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

		if (OutputBuffer.UAV)
		{
			FRHICommandListExecutor::GetImmediateCommandList().ClearUAVFloat(OutputBuffer.UAV, FVector4(ForceInitToZero));
		}
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

FVector MinVector(const FVector& VectorA, const FVector& VectorB)
{
	return FVector(FMath::Min(VectorA.X, VectorB.X), FMath::Min(VectorA.Y, VectorB.Y), FMath::Min(VectorA.Z, VectorB.Z));
}

FVector MaxVector(const FVector& VectorA, const FVector& VectorB)
{
	return FVector(FMath::Max(VectorA.X, VectorB.X), FMath::Max(VectorA.Y, VectorB.Y), FMath::Max(VectorA.Z, VectorB.Z));
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
		OutEnvironment.SetDefine(TEXT("MAX_TARGETS_ARRAY"), MAX_TARGETS_ARRAY);
	}

	FBuildPhysicsFieldClipmapCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NodesParams.Bind(Initializer.ParameterMap, TEXT("NodesParams"));
		NodesOffsets.Bind(Initializer.ParameterMap, TEXT("NodesOffsets"));
		TargetsOffsets.Bind(Initializer.ParameterMap, TEXT("TargetsOffsets"));
		FieldClipmap.Bind(Initializer.ParameterMap, TEXT("FieldClipmap"));

		BoundsMin.Bind(Initializer.ParameterMap, TEXT("BoundsMin"));
		BoundsMax.Bind(Initializer.ParameterMap, TEXT("BoundsMax"));

		ClipmapResolution.Bind(Initializer.ParameterMap, TEXT("ClipmapResolution"));
		ClipmapDistance.Bind(Initializer.ParameterMap, TEXT("ClipmapDistance"));
		ClipmapCenter.Bind(Initializer.ParameterMap, TEXT("ClipmapCenter"));
		ClipmapCount.Bind(Initializer.ParameterMap, TEXT("ClipmapCount"));
		ClipmapExponent.Bind(Initializer.ParameterMap, TEXT("ClipmapExponent"));

		PhysicsTargets.Bind(Initializer.ParameterMap, TEXT("PhysicsTargets"));
		TargetCount.Bind(Initializer.ParameterMap, TEXT("TargetCount"));
		TimeSeconds.Bind(Initializer.ParameterMap, TEXT("TimeSeconds"));
	}

	FBuildPhysicsFieldClipmapCS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, FPhysicsFieldResource* FieldResource, const float InTimeSeconds)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		if (FieldResource)
		{
			RHICmdList.Transition(FRHITransitionInfo(FieldResource->FieldClipmap.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));

			SetSRVParameter(RHICmdList, ShaderRHI, NodesParams, FieldResource->NodesParams.SRV);
			SetSRVParameter(RHICmdList, ShaderRHI, NodesOffsets, FieldResource->NodesOffsets.SRV);
			SetSRVParameter(RHICmdList, ShaderRHI, TargetsOffsets, FieldResource->TargetsOffsets.SRV);
			SetUAVParameter(RHICmdList, ShaderRHI, FieldClipmap, FieldResource->FieldClipmap.UAV);

			SetSRVParameter(RHICmdList, ShaderRHI, BoundsMin, FieldResource->BoundsMin.SRV);
			SetSRVParameter(RHICmdList, ShaderRHI, BoundsMax, FieldResource->BoundsMax.SRV);

			SetShaderValue(RHICmdList, ShaderRHI, ClipmapResolution, FieldResource->FieldInfos.ClipmapResolution);
			SetShaderValue(RHICmdList, ShaderRHI, ClipmapDistance, FieldResource->FieldInfos.ClipmapDistance);
			SetShaderValue(RHICmdList, ShaderRHI, ClipmapCount, FieldResource->FieldInfos.ClipmapCount);
			SetShaderValue(RHICmdList, ShaderRHI, ClipmapCenter, FieldResource->FieldInfos.ClipmapCenter);
			SetShaderValue(RHICmdList, ShaderRHI, ClipmapExponent, FieldResource->FieldInfos.ClipmapExponent);

			SetShaderValue(RHICmdList, ShaderRHI, PhysicsTargets, FieldResource->FieldInfos.PhysicsTargets);
			SetShaderValue(RHICmdList, ShaderRHI, TargetCount, FieldResource->FieldInfos.TargetCount);
			SetShaderValue(RHICmdList, ShaderRHI, TimeSeconds, InTimeSeconds);
		}
	}

	void UnsetParameters(FRHICommandList& RHICmdList, FPhysicsFieldResource* FieldResource)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, FieldClipmap, nullptr);

		RHICmdList.Transition(FRHITransitionInfo(FieldResource->FieldClipmap.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
	}

private:
	
	LAYOUT_FIELD(FShaderResourceParameter, NodesParams);
	LAYOUT_FIELD(FShaderResourceParameter, NodesOffsets);
	LAYOUT_FIELD(FShaderResourceParameter, TargetsOffsets);
	LAYOUT_FIELD(FShaderResourceParameter, FieldClipmap);

	LAYOUT_FIELD(FShaderResourceParameter, BoundsMin);
	LAYOUT_FIELD(FShaderResourceParameter, BoundsMax);

	LAYOUT_FIELD(FShaderParameter, ClipmapResolution);
	LAYOUT_FIELD(FShaderParameter, ClipmapDistance);
	LAYOUT_FIELD(FShaderParameter, ClipmapCenter);
	LAYOUT_FIELD(FShaderParameter, ClipmapCount);
	LAYOUT_FIELD(FShaderParameter, ClipmapExponent);

	LAYOUT_FIELD(FShaderParameter, PhysicsTargets);
	LAYOUT_FIELD(FShaderParameter, TargetCount);
	LAYOUT_FIELD(FShaderParameter, TimeSeconds);
};

IMPLEMENT_SHADER_TYPE(, FBuildPhysicsFieldClipmapCS, TEXT("/Engine/Private/PhysicsFieldBuilder.usf"), TEXT("BuildPhysicsFieldClipmapCS"), SF_Compute);


/**
*	FPhysicsFieldResource
*/

FPhysicsFieldResource::FPhysicsFieldResource(const int32 TargetCount, const TArray<EFieldPhysicsType>& TargetTypes, 
		const FPhysicsFieldInfos::TargetsOffsetsType& VectorTargets, const FPhysicsFieldInfos::TargetsOffsetsType& ScalarTargets, 
	    const FPhysicsFieldInfos::TargetsOffsetsType& IntegerTargets, const FPhysicsFieldInfos::TargetsOffsetsType& PhysicsTargets) : FRenderResource()
{
	FieldInfos.TargetCount = TargetCount;
	FieldInfos.TargetTypes = TargetTypes;
	FieldInfos.VectorTargets = VectorTargets;
	FieldInfos.ScalarTargets = ScalarTargets;
	FieldInfos.IntegerTargets = IntegerTargets;
	FieldInfos.PhysicsTargets = PhysicsTargets;

	FieldInfos.ClipmapExponent = GPhysicsFieldClipmapExponent;
	FieldInfos.ClipmapCount = GPhysicsFieldClipmapCount;
	FieldInfos.ClipmapDistance = GPhysicsFieldClipmapDistance;
	FieldInfos.ClipmapResolution = GPhysicsFieldClipmapResolution;

	const int32 DatasCount = FieldInfos.ClipmapCount * FieldInfos.TargetCount;
	const int32 TextureSize = FieldInfos.ClipmapResolution * DatasCount + DatasCount - 1;

	if (TextureSize > 2048 && DatasCount > 0)
	{
		FieldInfos.ClipmapResolution = (2048 + 1 - DatasCount) / DatasCount;
		UE_LOG(LogGlobalField, Warning, TEXT("Texture Size out of the 2048 limit. Clamping the resolution to : %d"), FieldInfos.ClipmapResolution);
	}
}

void FPhysicsFieldResource::InitRHI()
{
	SCOPE_CYCLE_COUNTER(STAT_PhysicsFields_UpdateResource_RT);

	const int32 DatasCount = FieldInfos.ClipmapCount * FieldInfos.TargetCount;
	InitInternalTexture<float, 4, EPixelFormat::PF_A32B32G32R32F>(FieldInfos.ClipmapResolution, FieldInfos.ClipmapResolution, FieldInfos.ClipmapResolution * DatasCount + DatasCount-1, FieldClipmap);
	InitInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT>(EFieldPhysicsType::Field_PhysicsType_Max + 1, TargetsOffsets);

	const int32 BoundsCount = EFieldPhysicsType::Field_PhysicsType_Max;
	InitInternalBuffer<FVector4, 1, EPixelFormat::PF_A32B32G32R32F>(BoundsCount, BoundsMin);
	InitInternalBuffer<FVector4, 1, EPixelFormat::PF_A32B32G32R32F>(BoundsCount, BoundsMax);
}

void FPhysicsFieldResource::ReleaseRHI()
{
	FieldClipmap.Release();
	NodesParams.Release();
	NodesOffsets.Release();
	TargetsOffsets.Release();
	BoundsMin.Release();
	BoundsMax.Release();
}

void FPhysicsFieldResource::UpdateResource(FRHICommandListImmediate& RHICmdList, const int32 NodesCount, const int32 ParamsCount,
				const TStaticArray<int32, EFieldPhysicsType::Field_PhysicsType_Max + 1>& TargetsOffsetsDatas, const TArray<int32>& NodesOffsetsDatas, const TArray<float>& NodesParamsDatas,
				const TArray<FVector4>& BoundsMinDatas, const TArray<FVector4>& BoundsMaxDatas, const float TimeSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_PhysicsFields_UpdateResource_RT);
	SCOPED_DRAW_EVENT(RHICmdList, PhysicsFields);
	SCOPED_GPU_STAT(RHICmdList, PhysicsFields);

	InitInternalBuffer<float, 1, EPixelFormat::PF_R32_FLOAT>(ParamsCount, NodesParams);
	InitInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT>(NodesCount, NodesOffsets);

	UpdateInternalBuffer<float, 1, EPixelFormat::PF_R32_FLOAT>(ParamsCount, NodesParamsDatas.GetData(), NodesParams);
	UpdateInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT>(NodesCount, NodesOffsetsDatas.GetData(), NodesOffsets);
	UpdateInternalBuffer<int32, 1, EPixelFormat::PF_R32_SINT>(EFieldPhysicsType::Field_PhysicsType_Max + 1, TargetsOffsetsDatas.GetData(), TargetsOffsets);

	FieldInfos.ClipmapCenter = FieldInfos.ViewOrigin;

	UpdateInternalBuffer<FVector4, 1, EPixelFormat::PF_A32B32G32R32F>(EFieldPhysicsType::Field_PhysicsType_Max, BoundsMinDatas.GetData(), BoundsMin);
	UpdateInternalBuffer<FVector4, 1, EPixelFormat::PF_A32B32G32R32F>(EFieldPhysicsType::Field_PhysicsType_Max, BoundsMaxDatas.GetData(), BoundsMax);

	FRHIUnorderedAccessView* FieldClipmapUAV = FieldClipmap.UAV;
	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, FieldClipmapUAV);

	if (FieldClipmapUAV != nullptr)
	{
		RHICmdList.ClearUAVFloat(FieldClipmapUAV, FVector4(ForceInitToZero));
	}

	TShaderMapRef<FBuildPhysicsFieldClipmapCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
	RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

	const uint32 NumGroups = FMath::DivideAndRoundUp<int32>(FieldInfos.ClipmapResolution, FBuildPhysicsFieldClipmapCS::ThreadGroupSize);

	ComputeShader->SetParameters(RHICmdList, this, TimeSeconds);
	DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), NumGroups, NumGroups, NumGroups);
	ComputeShader->UnsetParameters(RHICmdList, this);
}

/**
*	FPhysicsFieldInstance
*/

void FPhysicsFieldInstance::InitInstance( const TArray<EFieldPhysicsType>& TargetTypes)
{
	FPhysicsFieldInfos::TargetsOffsetsType VectorTargets(-1), ScalarTargets(-1), IntegerTargets(-1), PhysicsTargets(-1);

	static const TArray<EFieldPhysicsType> VectorTypes = GetFieldTargetTypes(EFieldOutputType::Field_Output_Vector);
	static const TArray<EFieldPhysicsType> ScalarTypes = GetFieldTargetTypes(EFieldOutputType::Field_Output_Scalar);
	static const TArray<EFieldPhysicsType> IntegerTypes = GetFieldTargetTypes(EFieldOutputType::Field_Output_Integer);

	int32 TargetIndex = 0;
	int32 VectorCount = 0, ScalarCount = 0, IntegerCount = 0;
	for (auto& TargetType : TargetTypes)
	{
		const EFieldOutputType OutputType = GetFieldTargetIndex(VectorTypes, ScalarTypes, IntegerTypes, TargetType, TargetIndex);
		if (OutputType == EFieldOutputType::Field_Output_Vector)
		{
			VectorTargets[TargetIndex] = VectorCount;
			PhysicsTargets[TargetType - 1] = VectorCount;
			VectorCount += 1;
		}
		else if (OutputType == EFieldOutputType::Field_Output_Scalar)
		{
			ScalarTargets[TargetIndex] = ScalarCount;
			PhysicsTargets[TargetType - 1] = ScalarCount;
			ScalarCount += 1;
		}
		else if (OutputType == EFieldOutputType::Field_Output_Integer)
		{
			IntegerTargets[TargetIndex] = IntegerCount;
			PhysicsTargets[TargetType - 1] = IntegerCount;
			IntegerCount += 1;
		}
	}
	const int32 TargetCount = FMath::Max3(VectorCount, ScalarCount, IntegerCount);
	
	if (!FieldResource)
	{
		FieldResource = new FPhysicsFieldResource(TargetCount, TargetTypes, VectorTargets, ScalarTargets, IntegerTargets, PhysicsTargets);

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
	BoundsMin.Empty();
	BoundsMax.Empty();
}

void FPhysicsFieldInstance::UpdateInstance(const float TimeSeconds)
{
	NodesOffsets.Empty();
	NodesParams.Empty();

	if (FieldResource)
	{
		BoundsMin.Init(FVector4(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX), EFieldPhysicsType::Field_PhysicsType_Max);
		BoundsMax.Init(FVector4(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX), EFieldPhysicsType::Field_PhysicsType_Max);

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
			if (TargetRoots.Num() == 1)
			{
				TargetNode = TargetRoots[0];
			}
			else if (TargetRoots.Num() > 1)
			{
				const EFieldOutputType OutputType = GetFieldTargetOutput(TargetType);
				if (OutputType == EFieldOutputType::Field_Output_Vector)
				{
					FFieldNode<FVector>* PreviousNode = StaticCast<FFieldNode<FVector>*>(TargetRoots[0]->NewCopy());
					FFieldNode<FVector>* NextNode = nullptr;
					for (int32 TargetIndex = 1; TargetIndex < TargetRoots.Num(); ++TargetIndex)
					{
						NextNode = StaticCast<FFieldNode<FVector>*>(TargetRoots[TargetIndex]->NewCopy());
						PreviousNode = new FSumVector(1.0, nullptr, PreviousNode,
							NextNode, EFieldOperationType::Field_Add);
					}
					TargetNode = PreviousNode;
				}
				else if (OutputType == EFieldOutputType::Field_Output_Scalar)
				{
					FFieldNode<float>* PreviousNode = StaticCast<FFieldNode<float>*>(TargetRoots[0]->NewCopy());
					FFieldNode<float>* NextNode = nullptr;
					for (int32 TargetIndex = 1; TargetIndex < TargetRoots.Num(); ++TargetIndex)
					{
						NextNode = StaticCast<FFieldNode<float>*>(TargetRoots[TargetIndex]->NewCopy());
						PreviousNode = new FSumScalar(1.0, PreviousNode,
							NextNode, EFieldOperationType::Field_Add);
					}
					TargetNode = PreviousNode;
				}
				else if (OutputType == EFieldOutputType::Field_Output_Integer)
				{
					FFieldNode<float>* PreviousNode = new FConversionField<int32, float>(StaticCast<FFieldNode<int32>*>(TargetRoots[0]->NewCopy()));
					FFieldNode<float>* NextNode = nullptr;
					for (int32 TargetIndex = 1; TargetIndex < TargetRoots.Num(); ++TargetIndex)
					{
						NextNode = new FConversionField<int32, float>(StaticCast<FFieldNode<int32>*>(TargetRoots[TargetIndex]->NewCopy()));
						PreviousNode = new FSumScalar(1.0, PreviousNode,
							NextNode, EFieldOperationType::Field_Add);
					}
					TargetNode = PreviousNode;
				}
			}
			const int32 PreviousNodes = NodesOffsets.Num();
			FVector MinBound(-FLT_MAX), MaxBound(FLT_MAX);
			if (TargetNode)
			{
				BuildNodeBounds(TargetNode, MinBound, MaxBound);
				BuildNodeParams(TargetNode);
				if (TargetRoots.Num() > 1) delete TargetNode;
			}
			TargetsOffsets[TargetType + 1] = NodesOffsets.Num() - PreviousNodes;

			if (GPhysicsFieldEnableCulling == 1)
			{
				BoundsMin[TargetType] = FVector4(MinBound, 0);
				BoundsMax[TargetType] = FVector4(MaxBound, 0);
			}
		}
		
		for (uint32 FieldIndex = 1; FieldIndex < EFieldPhysicsType::Field_PhysicsType_Max + 1; ++FieldIndex)
		{
			TargetsOffsets[FieldIndex] += TargetsOffsets[FieldIndex - 1];
		}
		{
			TStaticArray<int32, EFieldPhysicsType::Field_PhysicsType_Max + 1> LocalTargetsOffsets = TargetsOffsets;
			TArray<int32> LocalNodesOffsets = NodesOffsets;
			TArray<float> LocalNodesParams = NodesParams;

			TArray<FVector4> LocalBoundsMin = BoundsMin;
			TArray<FVector4> LocalBoundsMax = BoundsMax;

			const int32 LocalNodesCount = NodesOffsets.Num();
			const int32 LocalParamsCount = NodesParams.Num();
			const float LocalTimeSeconds = TimeSeconds;

			FPhysicsFieldResource* LocalFieldResource = FieldResource;
			ENQUEUE_RENDER_COMMAND(FUpdateFieldInstanceCommand)(
				[LocalFieldResource, LocalNodesCount, LocalParamsCount, LocalNodesParams, LocalNodesOffsets, LocalTargetsOffsets, LocalBoundsMin, LocalBoundsMax, LocalTimeSeconds](FRHICommandListImmediate& RHICmdList)
				{
					LocalFieldResource->UpdateResource(RHICmdList, LocalNodesCount, LocalParamsCount,
						LocalTargetsOffsets, LocalNodesOffsets, LocalNodesParams, LocalBoundsMin, LocalBoundsMax, LocalTimeSeconds);
				});
		}
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
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FWaveScalar)
		{
			FWaveScalar* LocalNode = StaticCast<FWaveScalar*>(FieldNode);
			NodesOffsets.Add(NodesParams.Num());
			NodesParams.Add(FieldNode->Type());
			NodesParams.Add(FFieldNodeBase::ESerializationType::FieldNode_FWaveScalar);
			NodesParams.Add(LocalNode->Magnitude);
			NodesParams.Add(LocalNode->Position.X);
			NodesParams.Add(LocalNode->Position.Y);
			NodesParams.Add(LocalNode->Position.Z);
			NodesParams.Add(LocalNode->Wavelength);
			NodesParams.Add(LocalNode->Period);
			NodesParams.Add(LocalNode->Time);
			NodesParams.Add(LocalNode->Function);
			NodesParams.Add(LocalNode->Falloff);
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

void FPhysicsFieldInstance::BuildNodeBounds(FFieldNodeBase* FieldNode, FVector& MinBounds, FVector& MaxBounds)
{
	MinBounds = FVector(-FLT_MAX);
	MaxBounds = FVector(FLT_MAX);

	if (FieldNode)
	{
		if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FRadialIntMask)
		{
			FRadialIntMask* LocalNode = StaticCast<FRadialIntMask*>(FieldNode);

			MinBounds = (LocalNode->ExteriorValue == 0) ? LocalNode->Position - FVector(LocalNode->Radius) : FVector(-FLT_MAX);
			MaxBounds = (LocalNode->ExteriorValue == 0) ? LocalNode->Position + FVector(LocalNode->Radius) : FVector(FLT_MAX);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FRadialFalloff)
		{
			FRadialFalloff* LocalNode = StaticCast<FRadialFalloff*>(FieldNode);

			MinBounds = (LocalNode->Default == 0) ? LocalNode->Position - FVector(LocalNode->Radius) : FVector(-FLT_MAX);
			MaxBounds = (LocalNode->Default == 0) ? LocalNode->Position + FVector(LocalNode->Radius) : FVector(FLT_MAX);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FBoxFalloff)
		{
			FBoxFalloff* LocalNode = StaticCast<FBoxFalloff*>(FieldNode);

			MinBounds = (LocalNode->Default == 0) ? LocalNode->Transform.GetTranslation() - LocalNode->Transform.GetScale3D() : FVector(-FLT_MAX);
			MaxBounds = (LocalNode->Default == 0) ? LocalNode->Transform.GetTranslation() + LocalNode->Transform.GetScale3D() : FVector(FLT_MAX);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FSumScalar)
		{
			FSumScalar* LocalNode = StaticCast<FSumScalar*>(FieldNode);

			FVector MinBoundsA(-FLT_MAX), MaxBoundsA(FLT_MAX), MinBoundsB(-FLT_MAX), MaxBoundsB(FLT_MAX);
			BuildNodeBounds(LocalNode->ScalarRight.Get(), MinBoundsA, MaxBoundsA);
			BuildNodeBounds(LocalNode->ScalarLeft.Get(), MinBoundsB, MaxBoundsB);

			if (LocalNode->Operation == EFieldOperationType::Field_Multiply ||
				LocalNode->Operation == EFieldOperationType::Field_Divide)
			{
				MinBounds = MaxVector(MinBoundsA, MinBoundsB);
				MaxBounds = MinVector(MaxBoundsA, MaxBoundsB);
			}
			else if (LocalNode->Operation == EFieldOperationType::Field_Add ||
				LocalNode->Operation == EFieldOperationType::Field_Substract)
			{
				MinBounds = MinVector(MinBoundsA, MinBoundsB);
				MaxBounds = MaxVector(MaxBoundsA, MaxBoundsB);
			}
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FSumVector)
		{
			FSumVector* LocalNode = StaticCast<FSumVector*>(FieldNode);

			FVector MinBoundsA(-FLT_MAX), MaxBoundsA(FLT_MAX), MinBoundsB(-FLT_MAX), MaxBoundsB(FLT_MAX), MinBoundsC(-FLT_MAX), MaxBoundsC(FLT_MAX);
			BuildNodeBounds(LocalNode->Scalar.Get(), MinBoundsA, MaxBoundsA);
			BuildNodeBounds(LocalNode->VectorRight.Get(), MinBoundsB, MaxBoundsB);
			BuildNodeBounds(LocalNode->VectorLeft.Get(), MinBoundsC, MaxBoundsC);

			if (LocalNode->Operation == EFieldOperationType::Field_Multiply ||
				LocalNode->Operation == EFieldOperationType::Field_Divide)
			{
				MinBounds = MaxVector(MinBoundsB, MinBoundsC);
				MaxBounds = MinVector(MaxBoundsB, MaxBoundsC);
			}
			else if (LocalNode->Operation == EFieldOperationType::Field_Add ||
				LocalNode->Operation == EFieldOperationType::Field_Substract)
			{
				MinBounds = MinVector(MinBoundsB, MinBoundsC);
				MaxBounds = MaxVector(MaxBoundsB, MaxBoundsC);
			}
			MinBounds = MaxVector(MinBounds, MinBoundsA);
			MaxBounds = MinVector(MaxBounds, MaxBoundsA);
		}
		else if (FieldNode->SerializationType() == FFieldNodeBase::ESerializationType::FieldNode_FCullingField)
		{
			if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_Int32)
			{
				FCullingField<int32>* LocalNode = StaticCast<FCullingField<int32>*>(FieldNode);

				FVector MinBoundsA(-FLT_MAX), MaxBoundsA(FLT_MAX), MinBoundsB(-FLT_MAX), MaxBoundsB(FLT_MAX);
				BuildNodeBounds(LocalNode->Culling.Get(), MinBoundsA, MaxBoundsA);
				BuildNodeBounds(LocalNode->Input.Get(), MinBoundsB, MaxBoundsB);

				if (LocalNode->Operation == EFieldCullingOperationType::Field_Culling_Inside)
				{
					MinBounds = MinVector(MinBoundsA, MinBoundsB);
					MaxBounds = MaxVector(MaxBoundsA, MaxBoundsB);
				}
				else if (LocalNode->Operation == EFieldCullingOperationType::Field_Culling_Outside)
				{
					MinBounds = MaxVector(MinBoundsA, MinBoundsB);
					MaxBounds = MinVector(MaxBoundsA, MaxBoundsB);
				}
			}
			else if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_Float)
			{
				FCullingField<float>* LocalNode = StaticCast<FCullingField<float>*>(FieldNode);

				FVector MinBoundsA(-FLT_MAX), MaxBoundsA(FLT_MAX), MinBoundsB(-FLT_MAX), MaxBoundsB(FLT_MAX);
				BuildNodeBounds(LocalNode->Culling.Get(), MinBoundsA, MaxBoundsA);
				BuildNodeBounds(LocalNode->Input.Get(), MinBoundsB, MaxBoundsB);

				if (LocalNode->Operation == EFieldCullingOperationType::Field_Culling_Inside)
				{
					MinBounds = MinVector(MinBoundsA, MinBoundsB);
					MaxBounds = MaxVector(MaxBoundsA, MaxBoundsB);
				}
				else if (LocalNode->Operation == EFieldCullingOperationType::Field_Culling_Outside)
				{
					MinBounds = MaxVector(MinBoundsA, MinBoundsB);
					MaxBounds = MinVector(MaxBoundsA, MaxBoundsB);
				}
			}
			else if (FieldNode->Type() == FFieldNodeBase::EFieldType::EField_FVector)
			{
				FCullingField<FVector>* LocalNode = StaticCast<FCullingField<FVector>*>(FieldNode);

				FVector MinBoundsA(-FLT_MAX), MaxBoundsA(FLT_MAX), MinBoundsB(-FLT_MAX), MaxBoundsB(FLT_MAX);
				BuildNodeBounds(LocalNode->Culling.Get(), MinBoundsA, MaxBoundsA);
				BuildNodeBounds(LocalNode->Input.Get(), MinBoundsB, MaxBoundsB);

				if (LocalNode->Operation == EFieldCullingOperationType::Field_Culling_Inside)
				{
					MinBounds = MinVector(MinBoundsA, MinBoundsB);
					MaxBounds = MaxVector(MaxBoundsA, MaxBoundsB);
				}
				else if (LocalNode->Operation == EFieldCullingOperationType::Field_Culling_Outside)
				{
					MinBounds = MaxVector(MinBoundsA, MinBoundsB);
					MaxBounds = MinVector(MaxBoundsA, MaxBoundsB);
				}
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

void UPhysicsFieldComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	SCOPE_CYCLE_COUNTER(STAT_PhysicsFields_CreateSceneProxy);

	Super::CreateRenderState_Concurrent(Context);

	if (!FieldProxy)
	{
		FieldProxy = new FPhysicsFieldSceneProxy(this);
	}

	if (FieldProxy && GetWorld() && GetWorld()->Scene)
	{
		GetWorld()->Scene->SetPhysicsField(FieldProxy);
	}
}

void UPhysicsFieldComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();

	if (FieldProxy && GetWorld() && GetWorld()->Scene)
	{
		GetWorld()->Scene->ResetPhysicsField();

		FPhysicsFieldSceneProxy* SceneProxy = FieldProxy;
		ENQUEUE_RENDER_COMMAND(FDestroySkyLightCommand)(
			[SceneProxy](FRHICommandList& RHICmdList)
			{
				delete SceneProxy;
			});

		FieldProxy = nullptr;
	}
}

void UPhysicsFieldComponent::SendRenderDynamicData_Concurrent()
{
	SCOPE_CYCLE_COUNTER(STAT_PhysicsFields_SendRenderData);

	Super::SendRenderTransform_Concurrent();

	if (FieldInstance)
	{
		const bool bPreviousUpdate = FieldInstance->FieldCommands.Num() > 0;

		FieldInstance->FieldCommands.Empty();
		FieldInstance->FieldCommands.Append(PersistentCommands);
		FieldInstance->FieldCommands.Append(TransientCommands);
		TransientCommands.Empty();

		const bool bCurrentUpdate = FieldInstance->FieldCommands.Num() > 0;

		if (bCurrentUpdate || bPreviousUpdate)
		{
			const float TimeSeconds = GetWorld() ? GetWorld()->TimeSeconds : 0.0;
			FieldInstance->UpdateInstance(TimeSeconds);
		}
	}
}

void UPhysicsFieldComponent::OnRegister()
{
	Super::OnRegister();

	if (!FieldInstance)
	{
		FieldInstance = new FPhysicsFieldInstance();

		if (GPhysicsFieldSingleTarget == 1)
		{
			TArray<EFieldPhysicsType> TargetTypes = { EFieldPhysicsType::Field_LinearForce };
			FieldInstance->InitInstance(TargetTypes);
		}
		else
		{
			TArray<EFieldPhysicsType> TargetTypes = {   EFieldPhysicsType::Field_LinearForce,
														EFieldPhysicsType::Field_ExternalClusterStrain,
														EFieldPhysicsType::Field_Kill,
														EFieldPhysicsType::Field_LinearVelocity,
														EFieldPhysicsType::Field_AngularVelociy,
														EFieldPhysicsType::Field_AngularTorque,
														EFieldPhysicsType::Field_InternalClusterStrain,
														EFieldPhysicsType::Field_DisableThreshold,
														EFieldPhysicsType::Field_SleepingThreshold,
														EFieldPhysicsType::Field_PositionTarget,
														EFieldPhysicsType::Field_DynamicConstraint };
			TargetTypes.Sort();

			FieldInstance->InitInstance(TargetTypes);
		}
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

void UPhysicsFieldComponent::AddTransientCommand(const FFieldSystemCommand& FieldCommand)
{
	TransientCommands.Add(FieldCommand);
}

void UPhysicsFieldComponent::AddPersistentCommand(const FFieldSystemCommand& FieldCommand)
{
	PersistentCommands.Add(FieldCommand);
}

void UPhysicsFieldComponent::RemoveTransientCommand(const FFieldSystemCommand& FieldCommand)
{
	TransientCommands.Remove(FieldCommand);
}

void UPhysicsFieldComponent::RemovePersistentCommand(const FFieldSystemCommand& FieldCommand)
{
	PersistentCommands.Remove(FieldCommand);
}

/**
 * FPhysicsFieldSceneProxy.
 */

FPhysicsFieldSceneProxy::FPhysicsFieldSceneProxy(UPhysicsFieldComponent* PhysicsFieldComponent)
{
	//bWillEverBeLit = false;
	if (PhysicsFieldComponent && PhysicsFieldComponent->FieldInstance)
	{
		FieldResource = PhysicsFieldComponent->FieldInstance->FieldResource;
	}
}

FPhysicsFieldSceneProxy::~FPhysicsFieldSceneProxy()
{}


