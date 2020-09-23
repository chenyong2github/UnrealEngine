// Copyright Epic Games, Inc. All Rights Reserved.


#include "HairStrandsCluster.h"
#include "HairStrandsUtils.h"
#include "SceneRendering.h"
#include "SceneManagement.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"

class FHairMacroGroupAABBCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairMacroGroupAABBCS);
	SHADER_USE_PARAMETER_STRUCT(FHairMacroGroupAABBCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER(uint32, MacroGroupValid)
		SHADER_PARAMETER(uint32, bClearBuffer)
		SHADER_PARAMETER_SRV(Buffer, InGroupAABBBuffer0)
		SHADER_PARAMETER_SRV(Buffer, InGroupAABBBuffer1)
		SHADER_PARAMETER_SRV(Buffer, InGroupAABBBuffer2)
		SHADER_PARAMETER_SRV(Buffer, InGroupAABBBuffer3)
		SHADER_PARAMETER_SRV(Buffer, InGroupAABBBuffer4)
		SHADER_PARAMETER_SRV(Buffer, InGroupAABBBuffer5)
		SHADER_PARAMETER_SRV(Buffer, InGroupAABBBuffer6)
		SHADER_PARAMETER_SRV(Buffer, InGroupAABBBuffer7)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutMacroGroupAABBBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_AABBUPDATE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairMacroGroupAABBCS, "/Engine/Private/HairStrands/HairStrandsAABB.usf", "Main", SF_Compute);

static void AddHairMacroGroupAABBPass(
	FRDGBuilder& GraphBuilder,
	FHairStrandsMacroGroupData& MacroGroup,
	FRDGBufferUAVRef& OutHairMacroGroupAABBBufferUAV)
{
	const uint32 PrimitiveCount = MacroGroup.PrimitivesInfos.Num();
	if (PrimitiveCount == 0)
		return;

	const uint32 GroupPerPass = 8;
	bool bNeedClear = true;
	const uint32 MacroGroupId = MacroGroup.MacroGroupId;
	const uint32 IterationCount = FMath::CeilToInt(PrimitiveCount / float(GroupPerPass));
	for (uint32 PassIt = 0; PassIt < IterationCount; ++PassIt)
	{
		FHairMacroGroupAABBCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairMacroGroupAABBCS::FParameters>();
		Parameters->MacroGroupId = MacroGroupId;
		Parameters->OutMacroGroupAABBBuffer = OutHairMacroGroupAABBBufferUAV;

		uint32 MacroGroupValid = 1;
		uint32 CurrentGroupIt = 1;
		for (uint32 PassPrimitiveIt = 0, PassPrimitiveCount = FMath::Min(GroupPerPass, PrimitiveCount - PassIt * GroupPerPass); PassPrimitiveIt < PassPrimitiveCount; ++PassPrimitiveIt)
		{
			const uint32 PrimitiveIndex = PassIt * GroupPerPass + PassPrimitiveIt;
			const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo = MacroGroup.PrimitivesInfos[PrimitiveIndex];
			FShaderResourceViewRHIRef GroupAABBBufferSRV = PrimitiveInfo.PublicDataPtr->GetGroupAABBBuffer().SRV;

			// Default value
			if (PassPrimitiveIt == 0 && PassPrimitiveCount != GroupPerPass)
			{
				Parameters->InGroupAABBBuffer0 = GroupAABBBufferSRV;
				Parameters->InGroupAABBBuffer1 = GroupAABBBufferSRV;
				Parameters->InGroupAABBBuffer2 = GroupAABBBufferSRV;
				Parameters->InGroupAABBBuffer3 = GroupAABBBufferSRV;
				Parameters->InGroupAABBBuffer4 = GroupAABBBufferSRV;
				Parameters->InGroupAABBBuffer5 = GroupAABBBufferSRV;
				Parameters->InGroupAABBBuffer6 = GroupAABBBufferSRV;
				Parameters->InGroupAABBBuffer7 = GroupAABBBufferSRV;
			}

			switch (PassPrimitiveIt)
			{
				case 0 : Parameters->InGroupAABBBuffer0 = GroupAABBBufferSRV; break;
				case 1 : Parameters->InGroupAABBBuffer1 = GroupAABBBufferSRV; break;
				case 2 : Parameters->InGroupAABBBuffer2 = GroupAABBBufferSRV; break;
				case 3 : Parameters->InGroupAABBBuffer3 = GroupAABBBufferSRV; break;
				case 4 : Parameters->InGroupAABBBuffer4 = GroupAABBBufferSRV; break;
				case 5 : Parameters->InGroupAABBBuffer5 = GroupAABBBufferSRV; break;
				case 6 : Parameters->InGroupAABBBuffer6 = GroupAABBBufferSRV; break;
				case 7 : Parameters->InGroupAABBBuffer7 = GroupAABBBufferSRV; break;
			}
			MacroGroupValid |= 1 << PassPrimitiveIt;

		}

		Parameters->MacroGroupValid = MacroGroupValid;
		Parameters->bClearBuffer = bNeedClear ? 1 : 0;

		TShaderMapRef<FHairMacroGroupAABBCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsMacroGroupAABBUpdate"),
			ComputeShader,
			Parameters,
			FIntVector(1,1,1));

		bNeedClear = false;
	}
}

static bool DoesGroupExists(uint32 ResourceId, uint32 GroupIndex, const FHairStrandsMacroGroupData::TPrimitiveInfos& PrimitivesGroups)
{
	// Simple linear search as the expected number of groups is supposed to be low (<10)
	for (const FHairStrandsMacroGroupData::PrimitiveInfo& Group : PrimitivesGroups)
	{
		if (Group.GroupIndex == GroupIndex && Group.ResourceId == ResourceId)
		{
			return true;
		}
	}
	return false;
}

inline const FHairGroupPublicData* GetHairStrandsPublicData(const FMeshBatchAndRelevance& InMeshBatchAndRelevance)
{
	 return reinterpret_cast<const FHairGroupPublicData*>(InMeshBatchAndRelevance.Mesh->Elements[0].VertexFactoryUserData);
}

static void InternalUpdateMacroGroup(FHairStrandsMacroGroupData& MacroGroup, int32& MaterialId, const FMeshBatchAndRelevance* MeshBatchAndRelevance, const FPrimitiveSceneProxy* Proxy)
{
	if (MeshBatchAndRelevance)
	{
		check(MeshBatchAndRelevance->Mesh);
		check(MeshBatchAndRelevance->Mesh->Elements.Num() == 1);

		FHairGroupPublicData* HairGroupPublicData = reinterpret_cast<FHairGroupPublicData*>(MeshBatchAndRelevance->Mesh->Elements[0].VertexFactoryUserData);
		if (HairGroupPublicData->VFInput.Strands.bScatterSceneLighting)
		{
			MacroGroup.bNeedScatterSceneLighting = true;
		}

		FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo = MacroGroup.PrimitivesInfos.AddZeroed_GetRef();
		PrimitiveInfo.MeshBatchAndRelevance = *MeshBatchAndRelevance;
		PrimitiveInfo.MaterialId = MaterialId++;
		PrimitiveInfo.ResourceId = reinterpret_cast<uint64>(MeshBatchAndRelevance->Mesh->Elements[0].UserData);
		PrimitiveInfo.GroupIndex = HairGroupPublicData->GetGroupIndex();
		PrimitiveInfo.PublicDataPtr = HairGroupPublicData;
		check(PrimitiveInfo.GroupIndex < 32); // Sanity check
	}
}

FHairStrandsMacroGroupViews CreateHairStrandsMacroGroups(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const TArray<FViewInfo>& Views)
{
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	FHairStrandsMacroGroupViews MacroGroupsViews;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		if (View.Family)
		{
			int32 MaterialId = 0;
			FHairStrandsMacroGroupDatas& MacroGroups = MacroGroupsViews.Views.AddDefaulted_GetRef();

			if (View.HairStrandsMeshElements.Num() == 0 || View.bIsPlanarReflection || View.bIsReflectionCapture)
			{
				continue;
			}

			// Aggregate all hair primitives within the same area into macro groups, for allocating/rendering DOM/voxel
			uint32 MacroGroupId = 0;
			auto UpdateMacroGroup = [&MacroGroups, &View, &MacroGroupId, &MaterialId](const FMeshBatchAndRelevance* MeshBatchAndRelevance, const FPrimitiveSceneProxy* Proxy)
			{
				const bool bIsHairStrandsFactory = MeshBatchAndRelevance->Mesh->VertexFactory->GetType()->GetHashedName() == CompatibleVF->GetHashedName();
				if (!bIsHairStrandsFactory)
					return;

				const FBoxSphereBounds& PrimitiveBounds = Proxy->GetBounds();

				bool bFound = false;
				for (FHairStrandsMacroGroupData& MacroGroup : MacroGroups.Datas)
				{
					const bool bIntersect = FBoxSphereBounds::SpheresIntersect(MacroGroup.Bounds, PrimitiveBounds);
					if (bIntersect)
					{
						MacroGroup.Bounds = Union(MacroGroup.Bounds, PrimitiveBounds);
						InternalUpdateMacroGroup(MacroGroup, MaterialId, MeshBatchAndRelevance, Proxy);
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					FHairStrandsMacroGroupData MacroGroup;
					MacroGroup.MacroGroupId = MacroGroupId++;
					InternalUpdateMacroGroup(MacroGroup, MaterialId, MeshBatchAndRelevance, Proxy);
					MacroGroup.Bounds = PrimitiveBounds;
					MacroGroups.Datas.Add(MacroGroup);
				}
			};

			for (const FMeshBatchAndRelevance& MeshBatchAndRelevance : View.HairStrandsMeshElements)
			{
				UpdateMacroGroup(&MeshBatchAndRelevance, MeshBatchAndRelevance.PrimitiveSceneProxy);
			}

			for (FHairStrandsMacroGroupData& MacroGroup : MacroGroups.Datas)
			{
				MacroGroup.ScreenRect = ComputeProjectedScreenRect(MacroGroup.Bounds.GetBox(), View);
			}

			// Build hair macro group AABBB
			const uint32 MacroGroupCount = MacroGroups.Datas.Num();
			if (MacroGroupCount > 0)
			{
				DECLARE_GPU_STAT(HairStrandsAABB);
				RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsAABB");
				RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsAABB);

				MacroGroups.MacroGroupResources.MacroGroupAABBsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4, 6 * MacroGroupCount), TEXT("HairMacroGroupAABBBuffer"));
				FRDGBufferUAVRef MacroGroupAABBBufferUAV = GraphBuilder.CreateUAV(MacroGroups.MacroGroupResources.MacroGroupAABBsBuffer, PF_R32_SINT);
				for (FHairStrandsMacroGroupData& MacroGroup : MacroGroups.Datas)
				{				
					AddHairMacroGroupAABBPass(GraphBuilder, MacroGroup, MacroGroupAABBBufferUAV);
				}
				MacroGroups.MacroGroupResources.MacroGroupCount = MacroGroups.Datas.Num();
			}
		}
	}

	return MacroGroupsViews;
}

bool FHairStrandsMacroGroupData::PrimitiveInfo::IsCullingEnable() const
{
	const FHairGroupPublicData* HairGroupPublicData = GetHairStrandsPublicData(MeshBatchAndRelevance);
	return HairGroupPublicData->GetCullingResultAvailable();
}