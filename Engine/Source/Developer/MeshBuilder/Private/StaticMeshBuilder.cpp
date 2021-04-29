// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshBuilder.h"

#include "BuildOptimizationHelper.h"
#include "Components.h"
#include "Engine/StaticMesh.h"
#include "IMeshReductionInterfaces.h"
#include "IMeshReductionManagerModule.h"
#include "MeshBuild.h"
#include "MeshDescriptionHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "StaticMeshResources.h"
#include "NaniteBuilder.h"
#include "Rendering/NaniteResources.h"

DEFINE_LOG_CATEGORY(LogStaticMeshBuilder);

//////////////////////////////////////////////////////////////////////////
//Local functions definition
void BuildVertexBuffer(
	  UStaticMesh *StaticMesh
	, const FMeshDescription& MeshDescription
	, const FMeshBuildSettings& BuildSettings
	, TArray<int32>& OutWedgeMap
	, FStaticMeshSectionArray& OutSections
	, TArray<TArray<uint32> >& OutPerSectionIndices
	, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices
	, const FOverlappingCorners& OverlappingCorners
	, TArray<int32>& RemapVerts);
void BuildAllBufferOptimizations(struct FStaticMeshLODResources& StaticMeshLOD, const struct FMeshBuildSettings& LODBuildSettings, TArray< uint32 >& IndexBuffer, bool bNeeds32BitIndices, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices);
//////////////////////////////////////////////////////////////////////////

FStaticMeshBuilder::FStaticMeshBuilder()
{

}

static bool UseNativeQuadraticReduction()
{
	// Are we using our tool, or simplygon?  The tool is only changed during editor restarts
	IMeshReduction* ReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface").GetStaticMeshReductionInterface();

	FString VersionString = ReductionModule->GetVersionString();
	TArray<FString> SplitVersionString;
	VersionString.ParseIntoArray(SplitVersionString, TEXT("_"), true);

	bool bUseQuadricSimplier = SplitVersionString[0].Equals("QuadricMeshReduction");
	return bUseQuadricSimplier;
}


/**
 * Compute bounding box and sphere from position buffer
 */
static void ComputeBoundsFromPositionBuffer(const FPositionVertexBuffer& UsePositionBuffer, FVector& OriginOut, FVector& ExtentOut, float& RadiusOut)
{
	// Calculate the bounding box.
	FBox BoundingBox(ForceInit);
	for (uint32 VertexIndex = 0; VertexIndex < UsePositionBuffer.GetNumVertices(); VertexIndex++)
	{
		BoundingBox += UsePositionBuffer.VertexPosition(VertexIndex);
	}
	BoundingBox.GetCenterAndExtents(OriginOut, ExtentOut);

	// Calculate the bounding sphere, using the center of the bounding box as the origin.
	RadiusOut = 0.0f;
	for (uint32 VertexIndex = 0; VertexIndex < UsePositionBuffer.GetNumVertices(); VertexIndex++)
	{
		RadiusOut = FMath::Max(
			(UsePositionBuffer.VertexPosition(VertexIndex) - OriginOut).Size(),
			RadiusOut
		);
	}
}


/**
 * Compute bounding box and sphere from vertices
 */
static void ComputeBoundsFromVertexList(const TArray<FStaticMeshBuildVertex>& Vertices, FVector& OriginOut, FVector& ExtentOut, float& RadiusOut)
{
	// Calculate the bounding box.
	FBox BoundingBox(ForceInit);
	for (int32 VertexIndex = 0; VertexIndex < Vertices.Num(); VertexIndex++)
	{
		BoundingBox += Vertices[VertexIndex].Position;
	}
	BoundingBox.GetCenterAndExtents(OriginOut, ExtentOut);

	// Calculate the bounding sphere, using the center of the bounding box as the origin.
	RadiusOut = 0.0f;
	for (int32 VertexIndex = 0; VertexIndex < Vertices.Num(); VertexIndex++)
	{
		RadiusOut = FMath::Max(
			(Vertices[VertexIndex].Position - OriginOut).Size(),
			RadiusOut
		);
	}
}



/**
 * Utility function used inside FStaticMeshBuilder::Build() per-LOD loop to populate
 * the Sections in a FStaticMeshLODResources from PerSectionIndices, as well as
 * concatenate all section indices into CombinedIndicesOut.
 * Returned bNeeds32BitIndicesOut indicates whether max vert index is larger than max int16
 */
static void BuildCombinedSectionIndices(
	const TArray<TArray<uint32>>& PerSectionIndices, 
	FStaticMeshLODResources& StaticMeshLODInOut, 
	TArray<uint32>& CombinedIndicesOut,
	bool& bNeeds32BitIndicesOut )
{
	bNeeds32BitIndicesOut = false;
	for (int32 SectionIndex = 0; SectionIndex < StaticMeshLODInOut.Sections.Num(); SectionIndex++)
	{
		FStaticMeshSection& Section = StaticMeshLODInOut.Sections[SectionIndex];
		const TArray<uint32>& SectionIndices = PerSectionIndices[SectionIndex];
		Section.FirstIndex = 0;
		Section.NumTriangles = 0;
		Section.MinVertexIndex = 0;
		Section.MaxVertexIndex = 0;

		if (SectionIndices.Num())
		{
			Section.FirstIndex = CombinedIndicesOut.Num();
			Section.NumTriangles = SectionIndices.Num() / 3;

			CombinedIndicesOut.AddUninitialized(SectionIndices.Num());
			uint32* DestPtr = &CombinedIndicesOut[Section.FirstIndex];
			uint32 const* SrcPtr = SectionIndices.GetData();

			Section.MinVertexIndex = *SrcPtr;
			Section.MaxVertexIndex = *SrcPtr;

			for (int32 Index = 0; Index < SectionIndices.Num(); Index++)
			{
				uint32 VertIndex = *SrcPtr++;

				bNeeds32BitIndicesOut |= (VertIndex > MAX_uint16);
				Section.MinVertexIndex = FMath::Min<uint32>(VertIndex, Section.MinVertexIndex);
				Section.MaxVertexIndex = FMath::Max<uint32>(VertIndex, Section.MaxVertexIndex);
				*DestPtr++ = VertIndex;
			}
		}
	}
}


/**
 * If the StaticMesh has a valid HiRes SourceModel, this is the highest-resolution data available
 * and it's what we want to build Nanite from. This function does just that. Generally this does
 * the same steps as the loop over LODs in FStaticMeshBuilder::Build() below, however we skip
 * various steps like reduction.
 * 
 * Note that this currently discards the returned Nanite-fractional-cut that NaniteBuilderModule.Build() returns.
 * Current assumption is that if the HiRes SourceModel exists, then an authored lower-resolution
 * LOD0 SourceModel already exists and should be used directly, instead of a fractional cut.
 */
static bool BuildNaniteFromHiResSourceModel(
	UStaticMesh* StaticMesh, 
	const FMeshNaniteSettings NaniteSettings, 
	FBoxSphereBounds& HiResBoundsOut, 
	Nanite::FResources& NaniteResourcesOut)
{
	if (ensure(StaticMesh->IsHiResMeshDescriptionValid()) == false)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshBuilder::BuildNaniteFromHiResSourceModel);

	FMeshDescription HiResMeshDescription = *StaticMesh->GetHiResMeshDescription();
	FStaticMeshSourceModel& HiResSrcModel = StaticMesh->GetHiResSourceModel();
	FMeshBuildSettings& HiResBuildSettings = HiResSrcModel.BuildSettings;		// cannot be const because FMeshDescriptionHelper modifies the LightmapIndex fields ?!?

	// compute tangents, lightmap UVs, etc
	FMeshDescriptionHelper MeshDescriptionHelper(&HiResBuildSettings);
	MeshDescriptionHelper.SetupRenderMeshDescription(StaticMesh, HiResMeshDescription);

	// make temporary RenderData storage that we can construct to pass to Nanite build
	FStaticMeshRenderData HiResTempRenderData;
	HiResTempRenderData.AllocateLODResources(1);
	FStaticMeshLODResources& HiResStaticMeshLOD = HiResTempRenderData.LODResources[0];
	HiResStaticMeshLOD.MaxDeviation = 0.0f;

	//Prepare the PerSectionIndices array so we can optimize the index buffer for the GPU
	//TODO: we do not need this for Nanite build, we only need CombinedIndices. Can we compute that directly?
	TArray<TArray<uint32>> PerSectionIndices;
	PerSectionIndices.AddDefaulted(HiResMeshDescription.PolygonGroups().Num());
	HiResStaticMeshLOD.Sections.Empty(HiResMeshDescription.PolygonGroups().Num());

	// Build the vertex and index buffer. We do not need WedgeMap or RemapVerts
	TArray<int32> WedgeMap, RemapVerts;
	TArray<FStaticMeshBuildVertex> StaticMeshBuildVertices;
	BuildVertexBuffer(StaticMesh, HiResMeshDescription, HiResBuildSettings, WedgeMap, HiResStaticMeshLOD.Sections, PerSectionIndices, StaticMeshBuildVertices, MeshDescriptionHelper.GetOverlappingCorners(), RemapVerts);
	WedgeMap.Empty();

	const uint32 NumTextureCoord = HiResMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate).GetNumChannels();

	// Only the render data and vertex buffers will be used from now on, so we can discard Render MeshDescription
	HiResMeshDescription.Empty();

	// Concatenate the per-section index buffers.
	TArray<uint32> CombinedIndices;
	bool bNeeds32BitIndices = false;
	BuildCombinedSectionIndices(PerSectionIndices, HiResStaticMeshLOD, CombinedIndices, bNeeds32BitIndices);

	// compute bounds from the HiRes mesh before we do Nanite build because it will modify StaticMeshBuildVertices
	ComputeBoundsFromVertexList(StaticMeshBuildVertices, HiResBoundsOut.Origin, HiResBoundsOut.BoxExtent, HiResBoundsOut.SphereRadius);

	// Nanite build requires the section material indices to have already been resolved from the SectionInfoMap
	// as the indices are baked into the FMaterialTriangles.
	for (int32 SectionIndex = 0; SectionIndex < HiResStaticMeshLOD.Sections.Num(); SectionIndex++)
	{
		HiResStaticMeshLOD.Sections[SectionIndex].MaterialIndex = StaticMesh->GetSectionInfoMap().Get(0, SectionIndex).MaterialIndex;
	}

	// run nanite build
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshBuilder::BuildNaniteFromHiResSourceModel::Nanite);
		Nanite::IBuilderModule& NaniteBuilderModule = Nanite::IBuilderModule::Get();
		if (!NaniteBuilderModule.Build(NaniteResourcesOut, StaticMeshBuildVertices, CombinedIndices, HiResStaticMeshLOD.Sections, NumTextureCoord, NaniteSettings))
		{
			UE_LOG(LogStaticMesh, Error, TEXT("Failed to build Nanite for HiRes static mesh. See previous line(s) for details."));
			return false;
		}
	}

	return true;
}



bool FStaticMeshBuilder::Build(FStaticMeshRenderData& StaticMeshRenderData, UStaticMesh* StaticMesh, const FStaticMeshLODGroup& LODGroup)
{
	const bool bNaniteBuildEnabled = StaticMesh->NaniteSettings.bEnabled;
	const bool bHaveHiResSourceModel = StaticMesh->IsHiResMeshDescriptionValid();
	int32 NumTasks = (bNaniteBuildEnabled && bHaveHiResSourceModel) ? (StaticMesh->GetNumSourceModels() + 1) : (StaticMesh->GetNumSourceModels());
	FScopedSlowTask SlowTask(NumTasks, NSLOCTEXT("StaticMeshEditor", "StaticMeshBuilderBuild", "Building static mesh render data."));
	SlowTask.MakeDialog();

	// The tool can only been switch by restarting the editor
	static bool bIsThirdPartyReductiontool = !UseNativeQuadraticReduction();

	if (!StaticMesh->IsMeshDescriptionValid(0))
	{
		//Warn the user that there is no mesh description data
		UE_LOG(LogStaticMeshBuilder, Error, TEXT("Cannot find a valid mesh description to build the asset."));
		return false;
	}

	if (StaticMeshRenderData.LODResources.Num() > 0)
	{
		//At this point the render data is suppose to be empty
		UE_LOG(LogStaticMeshBuilder, Error, TEXT("Cannot build static mesh render data twice [%s]."), *StaticMesh->GetFullName());
		
		//Crash in debug
		checkSlow(StaticMeshRenderData.LODResources.Num() == 0);

		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshBuilder::Build);

	const int32 NumSourceModels = StaticMesh->GetNumSourceModels();
	StaticMeshRenderData.AllocateLODResources(NumSourceModels);

	TArray<FMeshDescription> MeshDescriptions;
	MeshDescriptions.SetNum(NumSourceModels);

	const FMeshSectionInfoMap BeforeBuildSectionInfoMap = StaticMesh->GetSectionInfoMap();
	const FMeshSectionInfoMap BeforeBuildOriginalSectionInfoMap = StaticMesh->GetOriginalSectionInfoMap();
	const FMeshNaniteSettings NaniteSettings = StaticMesh->NaniteSettings;

	bool bNaniteDataBuilt = false;		// true once we have finished building Nanite, which can happen in multiple places

	// bounds of the pre-Nanite mesh
	FBoxSphereBounds HiResBounds;
	bool bHaveHiResBounds = false;

	// do nanite build for HiRes SourceModel if we have one. In that case we skip the inline nanite build
	// below that would happen with LOD0 build
	if (bHaveHiResSourceModel && bNaniteBuildEnabled)
	{
		SlowTask.EnterProgressFrame(1);
		if (BuildNaniteFromHiResSourceModel(StaticMesh, NaniteSettings, HiResBounds, StaticMeshRenderData.NaniteResources))
		{
			bHaveHiResBounds = true;
			bNaniteDataBuilt = true;	// we are done building nanite data, and should not do it inline with LOD0 creation
		}
	}

	// build render data for each LOD
	for (int32 LodIndex = 0; LodIndex < NumSourceModels; ++LodIndex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("FStaticMeshBuilder::Build LOD");
		SlowTask.EnterProgressFrame(1);
		FScopedSlowTask BuildLODSlowTask(3);
		BuildLODSlowTask.EnterProgressFrame(1);

		FStaticMeshSourceModel& SrcModel = StaticMesh->GetSourceModel(LodIndex);

		float MaxDeviation = 0.0f;
		FMeshBuildSettings& LODBuildSettings = SrcModel.BuildSettings;
		bool bIsMeshDescriptionValid = StaticMesh->CloneMeshDescription(LodIndex, MeshDescriptions[LodIndex]);
		FMeshDescriptionHelper MeshDescriptionHelper(&LODBuildSettings);

		FMeshReductionSettings ReductionSettings = LODGroup.GetSettings(SrcModel.ReductionSettings, LodIndex);

		//Make sure we do not reduce a non custom LOD by himself
		const int32 BaseReduceLodIndex = FMath::Clamp<int32>(ReductionSettings.BaseLODModel, 0, bIsMeshDescriptionValid ? LodIndex : LodIndex - 1);
		// Use simplifier if a reduction in triangles or verts has been requested.
		bool bUseReduction = StaticMesh->IsReductionActive(LodIndex);

		if (bIsMeshDescriptionValid)
		{
			MeshDescriptionHelper.SetupRenderMeshDescription(StaticMesh, MeshDescriptions[LodIndex]);
		}
		else
		{
			if (bUseReduction)
			{
				// Initialize an empty mesh description that the reduce will fill
				FStaticMeshAttributes(MeshDescriptions[LodIndex]).Register();
			}
			else
			{
				//Duplicate the lodindex 0 we have a 100% reduction which is like a duplicate
				MeshDescriptions[LodIndex] = MeshDescriptions[BaseReduceLodIndex];
				//Set the overlapping threshold
				float ComparisonThreshold = StaticMesh->GetSourceModel(BaseReduceLodIndex).BuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;
				MeshDescriptionHelper.FindOverlappingCorners(MeshDescriptions[LodIndex], ComparisonThreshold);
				if (LodIndex > 0)
				{
					
					//Make sure the SectionInfoMap is taken from the Base RawMesh
					int32 SectionNumber = StaticMesh->GetOriginalSectionInfoMap().GetSectionNumber(BaseReduceLodIndex);
					for (int32 SectionIndex = 0; SectionIndex < SectionNumber; ++SectionIndex)
					{
						//Keep the old data if its valid
						bool bHasValidLODInfoMap = StaticMesh->GetSectionInfoMap().IsValidSection(LodIndex, SectionIndex);
						//Section material index have to be remap with the ReductionSettings.BaseLODModel SectionInfoMap to create
						//a valid new section info map for the reduced LOD.
						if (!bHasValidLODInfoMap && StaticMesh->GetSectionInfoMap().IsValidSection(BaseReduceLodIndex, SectionIndex))
						{
							//Copy the BaseLODModel section info to the reduce LODIndex.
							FMeshSectionInfo SectionInfo = StaticMesh->GetSectionInfoMap().Get(BaseReduceLodIndex, SectionIndex);
							FMeshSectionInfo OriginalSectionInfo = StaticMesh->GetOriginalSectionInfoMap().Get(BaseReduceLodIndex, SectionIndex);
							StaticMesh->GetSectionInfoMap().Set(LodIndex, SectionIndex, SectionInfo);
							StaticMesh->GetOriginalSectionInfoMap().Set(LodIndex, SectionIndex, OriginalSectionInfo);
						}
					}
				}
			}

			if (LodIndex > 0)
			{
				LODBuildSettings = StaticMesh->GetSourceModel(BaseReduceLodIndex).BuildSettings;
			}
		}

		// Reduce LODs (if not building for Nanite).
		// TODO: Skip all LOD reduction for Nanite?
		if (bUseReduction && (!bNaniteBuildEnabled || LodIndex != 0))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_STR("FStaticMeshBuilder::Build - Reduce LOD");
			
			float OverlappingThreshold = LODBuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;
			FOverlappingCorners OverlappingCorners;
			FStaticMeshOperations::FindOverlappingCorners(OverlappingCorners, MeshDescriptions[BaseReduceLodIndex], OverlappingThreshold);

			int32 OldSectionInfoMapCount = StaticMesh->GetSectionInfoMap().GetSectionNumber(LodIndex);

			if (LodIndex == BaseReduceLodIndex)
			{
				//When using LOD 0, we use a copy of the mesh description since reduce do not support inline reducing
				FMeshDescription BaseMeshDescription = MeshDescriptions[BaseReduceLodIndex];
				MeshDescriptionHelper.ReduceLOD(BaseMeshDescription, MeshDescriptions[LodIndex], ReductionSettings, OverlappingCorners, MaxDeviation);
			}
			else
			{
				MeshDescriptionHelper.ReduceLOD(MeshDescriptions[BaseReduceLodIndex], MeshDescriptions[LodIndex], ReductionSettings, OverlappingCorners, MaxDeviation);
			}
			

			const TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescriptions[LodIndex].PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
			const TPolygonGroupAttributesRef<FName> BasePolygonGroupImportedMaterialSlotNames = MeshDescriptions[BaseReduceLodIndex].PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
			// Recompute adjacency information. Since we change the vertices when we reduce
			MeshDescriptionHelper.FindOverlappingCorners(MeshDescriptions[LodIndex], OverlappingThreshold);
			
			//Make sure the static mesh SectionInfoMap is up to date with the new reduce LOD
			//We have to remap the material index with the ReductionSettings.BaseLODModel sectionInfoMap
			//Set the new SectionInfoMap for this reduced LOD base on the ReductionSettings.BaseLODModel SectionInfoMap
			TArray<int32> BaseUniqueMaterialIndexes;
			//Find all unique Material in used order
			for (const FPolygonGroupID PolygonGroupID : MeshDescriptions[BaseReduceLodIndex].PolygonGroups().GetElementIDs())
			{
				int32 MaterialIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(BasePolygonGroupImportedMaterialSlotNames[PolygonGroupID]);
				if (MaterialIndex == INDEX_NONE)
				{
					MaterialIndex = PolygonGroupID.GetValue();
				}
				BaseUniqueMaterialIndexes.AddUnique(MaterialIndex);
			}
			TArray<int32> UniqueMaterialIndex;
			//Find all unique Material in used order
			for (const FPolygonGroupID PolygonGroupID : MeshDescriptions[LodIndex].PolygonGroups().GetElementIDs())
			{
				int32 MaterialIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(PolygonGroupImportedMaterialSlotNames[PolygonGroupID]);
				if (MaterialIndex == INDEX_NONE)
				{
					MaterialIndex = PolygonGroupID.GetValue();
				}
				UniqueMaterialIndex.AddUnique(MaterialIndex);
			}

			//If the reduce did not output the same number of section use the base LOD sectionInfoMap
			bool bIsOldMappingInvalid = OldSectionInfoMapCount != MeshDescriptions[LodIndex].PolygonGroups().Num();

			bool bValidBaseSectionInfoMap = BeforeBuildSectionInfoMap.GetSectionNumber(BaseReduceLodIndex) > 0;
			//All used material represent a different section
			for (int32 SectionIndex = 0; SectionIndex < UniqueMaterialIndex.Num(); ++SectionIndex)
			{
				//Keep the old data
				bool bHasValidLODInfoMap = !bIsOldMappingInvalid && BeforeBuildSectionInfoMap.IsValidSection(LodIndex, SectionIndex);
				//Section material index have to be remap with the ReductionSettings.BaseLODModel SectionInfoMap to create
				//a valid new section info map for the reduced LOD.

				//Find the base LOD section using this material
				if (!bHasValidLODInfoMap)
				{
					bool bSectionInfoSet = false;
					if (bValidBaseSectionInfoMap)
					{
						for (int32 BaseSectionIndex = 0; BaseSectionIndex < BaseUniqueMaterialIndexes.Num(); ++BaseSectionIndex)
						{
							if (UniqueMaterialIndex[SectionIndex] == BaseUniqueMaterialIndexes[BaseSectionIndex])
							{
								//Copy the base sectionInfoMap
								FMeshSectionInfo SectionInfo = BeforeBuildSectionInfoMap.Get(BaseReduceLodIndex, BaseSectionIndex);
								FMeshSectionInfo OriginalSectionInfo = BeforeBuildOriginalSectionInfoMap.Get(BaseReduceLodIndex, BaseSectionIndex);
								StaticMesh->GetSectionInfoMap().Set(LodIndex, SectionIndex, SectionInfo);
								StaticMesh->GetOriginalSectionInfoMap().Set(LodIndex, BaseSectionIndex, OriginalSectionInfo);
								bSectionInfoSet = true;
								break;
							}
						}
					}

					if (!bSectionInfoSet)
					{
						//Just set the default section info in case we did not found any match with the Base Lod
						FMeshSectionInfo SectionInfo;
						SectionInfo.MaterialIndex = SectionIndex;
						StaticMesh->GetSectionInfoMap().Set(LodIndex, SectionIndex, SectionInfo);
						StaticMesh->GetOriginalSectionInfoMap().Set(LodIndex, SectionIndex, SectionInfo);
					}
				}
			}
		}
		BuildLODSlowTask.EnterProgressFrame(1);
		const FPolygonGroupArray& PolygonGroups = MeshDescriptions[LodIndex].PolygonGroups();

		FStaticMeshLODResources& StaticMeshLOD = StaticMeshRenderData.LODResources[LodIndex];
		StaticMeshLOD.MaxDeviation = MaxDeviation;

		//Build new vertex buffers
		TArray< FStaticMeshBuildVertex > StaticMeshBuildVertices;

		StaticMeshLOD.Sections.Empty(PolygonGroups.Num());
		TArray<int32> RemapVerts; //Because we will remove MeshVertex that are redundant, we need a remap
								  //Render data Wedge map is only set for LOD 0???

		TArray<int32>& WedgeMap = StaticMeshLOD.WedgeMap;
		WedgeMap.Reset();

		//Prepare the PerSectionIndices array so we can optimize the index buffer for the GPU
		TArray<TArray<uint32> > PerSectionIndices;
		PerSectionIndices.AddDefaulted(MeshDescriptions[LodIndex].PolygonGroups().Num());

		//Build the vertex and index buffer
		BuildVertexBuffer(StaticMesh, MeshDescriptions[LodIndex], LODBuildSettings, WedgeMap, StaticMeshLOD.Sections, PerSectionIndices, StaticMeshBuildVertices, MeshDescriptionHelper.GetOverlappingCorners(), RemapVerts);

		const uint32 NumTextureCoord = MeshDescriptions[LodIndex].VertexInstanceAttributes().GetAttributesRef<FVector2D>( MeshAttribute::VertexInstance::TextureCoordinate ).GetNumChannels();

		// Only the render data and vertex buffers will be used from now on unless we have more than one source models
		// This will help with memory usage for Nanite Mesh by releasing memory before doing the build
		if (NumSourceModels == 1)
		{
			MeshDescriptions.Empty();
		}

		// Concatenate the per-section index buffers.
		TArray<uint32> CombinedIndices;
		bool bNeeds32BitIndices = false;
		BuildCombinedSectionIndices(PerSectionIndices, StaticMeshLOD, CombinedIndices, bNeeds32BitIndices);

		// If we want Nanite build, and have not already done it, do it based on LOD0 built render data.
		// This will replace the output VertexBuffers/etc with the fractional Nanite cut to be stored as LOD0 RenderData.
		// If we /have/ already done the Nanite build, it's because we have a separate HiRes mesh, and the LOD0 SourceModel
		// has been created by some other means and should be used as the real LOD0
		if (bNaniteBuildEnabled && LodIndex == 0 && bNaniteDataBuilt == false )
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshBuilder::Build::Nanite);

			ComputeBoundsFromVertexList(StaticMeshBuildVertices, HiResBounds.Origin, HiResBounds.BoxExtent, HiResBounds.SphereRadius);
			bHaveHiResBounds = true;

			// Nanite build requires the section material indices to have already been resolved from the SectionInfoMap
			// as the indices are baked into the FMaterialTriangles.
			for (int32 SectionIndex = 0; SectionIndex < StaticMeshLOD.Sections.Num(); SectionIndex++)
			{
				StaticMeshLOD.Sections[SectionIndex].MaterialIndex = StaticMesh->GetSectionInfoMap().Get(LodIndex, SectionIndex).MaterialIndex;
			}

			WedgeMap.Empty();	// Make sure to not keep the large WedgeMap from the input mesh around.
								// No need to calculate a new one for the coarse mesh, because Nanite meshes don't need it yet.
			Nanite::IBuilderModule& NaniteBuilderModule = Nanite::IBuilderModule::Get();
			if( !NaniteBuilderModule.Build( StaticMeshRenderData.NaniteResources, StaticMeshBuildVertices, CombinedIndices, StaticMeshLOD.Sections, NumTextureCoord, NaniteSettings ) )
			{
				UE_LOG(LogStaticMesh, Error, TEXT("Failed to build Nanite for static mesh. See previous line(s) for details."));
			}

			bNaniteDataBuilt = true;
		}

		// create the vertex and index buffers
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshBuilder::Build::BufferInit);
			StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer.SetUseHighPrecisionTangentBasis(LODBuildSettings.bUseHighPrecisionTangentBasis);
			StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(LODBuildSettings.bUseFullPrecisionUVs);
			StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer.Init(StaticMeshBuildVertices, NumTextureCoord);
			StaticMeshLOD.VertexBuffers.PositionVertexBuffer.Init(StaticMeshBuildVertices);
			StaticMeshLOD.VertexBuffers.ColorVertexBuffer.Init(StaticMeshBuildVertices);

			const EIndexBufferStride::Type IndexBufferStride = bNeeds32BitIndices ? EIndexBufferStride::Force32Bit : EIndexBufferStride::Force16Bit;
			StaticMeshLOD.IndexBuffer.SetIndices(CombinedIndices, IndexBufferStride);
		}

		// post-process the index buffer
		BuildLODSlowTask.EnterProgressFrame(1);
		BuildAllBufferOptimizations(StaticMeshLOD, LODBuildSettings, CombinedIndices, bNeeds32BitIndices, StaticMeshBuildVertices);

	} //End of LOD for loop

	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("FStaticMeshBuilder::Build - Calculate Bounds");

		// Calculate the bounding box of LOD0 buffer
		FPositionVertexBuffer& BasePositionVertexBuffer = StaticMeshRenderData.LODResources[0].VertexBuffers.PositionVertexBuffer;
		ComputeBoundsFromPositionBuffer(BasePositionVertexBuffer, StaticMeshRenderData.Bounds.Origin, StaticMeshRenderData.Bounds.BoxExtent, StaticMeshRenderData.Bounds.SphereRadius);
		// combine with high-res bounds if it was computed
		if (bHaveHiResBounds)
		{
			StaticMeshRenderData.Bounds = StaticMeshRenderData.Bounds + HiResBounds;
		}
	}


	return true;
}

bool FStaticMeshBuilder::BuildMeshVertexPositions(
	UStaticMesh* StaticMesh,
	TArray<uint32>& BuiltIndices,
	TArray<FVector>& BuiltVertices)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshBuilder::BuildMeshVertexPositions);

	if (!StaticMesh->IsMeshDescriptionValid(0))
	{
		//Warn the user that there is no mesh description data
		UE_LOG(LogStaticMeshBuilder, Error, TEXT("Cannot find a valid mesh description to build the asset."));
		return false;
	}

	const int32 NumSourceModels = StaticMesh->GetNumSourceModels();
	if (NumSourceModels > 0)
	{
		FMeshDescription MeshDescription;
		const bool bIsMeshDescriptionValid = StaticMesh->CloneMeshDescription(/*LodIndex*/ 0, MeshDescription);
		if (bIsMeshDescriptionValid)
		{
			const FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(0).BuildSettings;

			const FStaticMeshConstAttributes Attributes(MeshDescription);
			TArrayView<const FVector> VertexPositions = Attributes.GetVertexPositions().GetRawArray();
			TArrayView<const FVertexID> VertexIndices = Attributes.GetTriangleVertexIndices().GetRawArray();

			BuiltVertices.Reserve(VertexPositions.Num());
			for (int32 VertexIndex = 0; VertexIndex < VertexPositions.Num(); ++VertexIndex)
			{
				BuiltVertices.Add(VertexPositions[VertexIndex] * BuildSettings.BuildScale3D);
			}

			BuiltIndices.Reserve(VertexIndices.Num());
			for (int32 TriangleIndex = 0; TriangleIndex < VertexIndices.Num() / 3; ++TriangleIndex)
			{
				const uint32 I0 = VertexIndices[TriangleIndex * 3 + 0];
				const uint32 I1 = VertexIndices[TriangleIndex * 3 + 1];
				const uint32 I2 = VertexIndices[TriangleIndex * 3 + 2];

				const FVector V0 = BuiltVertices[I0];
				const FVector V1 = BuiltVertices[I1];
				const FVector V2 = BuiltVertices[I2];

				const FVector TriangleNormal = ((V1 - V2) ^ (V0 - V2));
				const bool bDegenerateTriangle = TriangleNormal.SizeSquared() < SMALL_NUMBER;
				if (!bDegenerateTriangle)
				{
					BuiltIndices.Add(I0);
					BuiltIndices.Add(I1);
					BuiltIndices.Add(I2);
				}
			}
		}
	}

	return true;
}

bool AreVerticesEqual(FStaticMeshBuildVertex const& A, FStaticMeshBuildVertex const& B, float ComparisonThreshold)
{
	if (   !A.Position.Equals(B.Position, ComparisonThreshold)
		|| !NormalsEqual(A.TangentX, B.TangentX)
		|| !NormalsEqual(A.TangentY, B.TangentY)
		|| !NormalsEqual(A.TangentZ, B.TangentZ)
		|| A.Color != B.Color)
	{
		return false;
	}

	// UVs
	for (int32 UVIndex = 0; UVIndex < MAX_STATIC_TEXCOORDS; UVIndex++)
	{
		if (!UVsEqual(A.UVs[UVIndex], B.UVs[UVIndex]))
		{
			return false;
		}
	}

	return true;
}

void BuildVertexBuffer(
	  UStaticMesh *StaticMesh
	, const FMeshDescription& MeshDescription
	, const FMeshBuildSettings& BuildSettings
	, TArray<int32>& OutWedgeMap
	, FStaticMeshSectionArray& OutSections
	, TArray<TArray<uint32> >& OutPerSectionIndices
	, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices
	, const FOverlappingCorners& OverlappingCorners
	, TArray<int32>& RemapVerts)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildVertexBuffer);

	TArray<int32> RemapVertexInstanceID;
	// set up vertex buffer elements
	const int32 NumVertexInstances = MeshDescription.VertexInstances().GetArraySize();
	StaticMeshBuildVertices.Reserve(NumVertexInstances);

	FStaticMeshConstAttributes Attributes(MeshDescription);

	TPolygonGroupAttributesConstRef<FName> PolygonGroupImportedMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
	TVertexAttributesConstRef<FVector> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesConstRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesConstRef<FVector4> VertexInstanceColors = Attributes.GetVertexInstanceColors();
	TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

	const bool bHasColors = VertexInstanceColors.IsValid();
	const bool bIgnoreTangents = StaticMesh->NaniteSettings.bEnabled;

	const uint32 NumTextureCoord = VertexInstanceUVs.GetNumChannels();
	const FMatrix ScaleMatrix = FScaleMatrix(BuildSettings.BuildScale3D).Inverse().GetTransposed();

	TMap<FPolygonGroupID, int32> PolygonGroupToSectionIndex;

	for (const FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
	{
		int32& SectionIndex = PolygonGroupToSectionIndex.FindOrAdd(PolygonGroupID);
		SectionIndex = OutSections.Add(FStaticMeshSection());
		FStaticMeshSection& StaticMeshSection = OutSections[SectionIndex];
		StaticMeshSection.MaterialIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(PolygonGroupImportedMaterialSlotNames[PolygonGroupID]);
		if (StaticMeshSection.MaterialIndex == INDEX_NONE)
		{
			StaticMeshSection.MaterialIndex = PolygonGroupID.GetValue();
		}
	}

	int32 ReserveIndicesCount = MeshDescription.Triangles().Num() * 3;

	//Fill the remap array
	RemapVerts.AddZeroed(ReserveIndicesCount);
	for (int32& RemapIndex : RemapVerts)
	{
		RemapIndex = INDEX_NONE;
	}

	//Initialize the wedge map array tracking correspondence between wedge index and rendering vertex index
	OutWedgeMap.Reset();
	OutWedgeMap.AddZeroed(ReserveIndicesCount);

	float VertexComparisonThreshold = BuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;

	int32 WedgeIndex = 0;
	for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
	{
		const FPolygonGroupID PolygonGroupID = MeshDescription.GetTrianglePolygonGroup(TriangleID);
		const int32 SectionIndex = PolygonGroupToSectionIndex[PolygonGroupID];
		TArray<uint32>& SectionIndices = OutPerSectionIndices[SectionIndex];

		TArrayView<const FVertexID> VertexIDs = MeshDescription.GetTriangleVertices(TriangleID);

		FVector CornerPositions[3];
		for (int32 TriVert = 0; TriVert < 3; ++TriVert)
		{
			CornerPositions[TriVert] = VertexPositions[VertexIDs[TriVert]];
		}
		FOverlappingThresholds OverlappingThresholds;
		OverlappingThresholds.ThresholdPosition = VertexComparisonThreshold;
		// Don't process degenerate triangles.
		if (PointsEqual(CornerPositions[0], CornerPositions[1], OverlappingThresholds)
			|| PointsEqual(CornerPositions[0], CornerPositions[2], OverlappingThresholds)
			|| PointsEqual(CornerPositions[1], CornerPositions[2], OverlappingThresholds))
		{
			WedgeIndex += 3;
			continue;
		}

		TArrayView<const FVertexInstanceID> VertexInstanceIDs = MeshDescription.GetTriangleVertexInstances(TriangleID);
		for (int32 TriVert = 0; TriVert < 3; ++TriVert, ++WedgeIndex)
		{
			const FVertexInstanceID VertexInstanceID = VertexInstanceIDs[TriVert];
			const FVector& VertexPosition = CornerPositions[TriVert];
			const FVector& VertexInstanceNormal = VertexInstanceNormals[VertexInstanceID];
			const FVector& VertexInstanceTangent = VertexInstanceTangents[VertexInstanceID];
			const float VertexInstanceBinormalSign = VertexInstanceBinormalSigns[VertexInstanceID];

			FStaticMeshBuildVertex StaticMeshVertex;

			StaticMeshVertex.Position = VertexPosition * BuildSettings.BuildScale3D;
			if( bIgnoreTangents )
			{
				StaticMeshVertex.TangentX = FVector( 1.0f, 0.0f, 0.0f );
				StaticMeshVertex.TangentY = FVector( 0.0f, 1.0f, 0.0f );
			}
			else
			{
				StaticMeshVertex.TangentX = ScaleMatrix.TransformVector(VertexInstanceTangent).GetSafeNormal();
				StaticMeshVertex.TangentY = ScaleMatrix.TransformVector(FVector::CrossProduct(VertexInstanceNormal, VertexInstanceTangent) * VertexInstanceBinormalSign).GetSafeNormal();
			}
			StaticMeshVertex.TangentZ = ScaleMatrix.TransformVector(VertexInstanceNormal).GetSafeNormal();
				
			if (bHasColors)
			{
				const FVector4& VertexInstanceColor = VertexInstanceColors[VertexInstanceID];
				const FLinearColor LinearColor(VertexInstanceColor);
				StaticMeshVertex.Color = LinearColor.ToFColor(true);
			}
			else
			{
				StaticMeshVertex.Color = FColor::White;
			}

			const uint32 MaxNumTexCoords = FMath::Min<int32>(MAX_MESH_TEXTURE_COORDS_MD, MAX_STATIC_TEXCOORDS);
			for (uint32 UVIndex = 0; UVIndex < MaxNumTexCoords; ++UVIndex)
			{
				if(UVIndex < NumTextureCoord)
				{
					StaticMeshVertex.UVs[UVIndex] = VertexInstanceUVs.Get(VertexInstanceID, UVIndex);
				}
				else
				{
					StaticMeshVertex.UVs[UVIndex] = FVector2D(0.0f, 0.0f);
				}
			}
					

			//Never add duplicated vertex instance
			//Use WedgeIndex since OverlappingCorners has been built based on that
			const TArray<int32>& DupVerts = OverlappingCorners.FindIfOverlapping(WedgeIndex);

			int32 Index = INDEX_NONE;
			for (int32 k = 0; k < DupVerts.Num(); k++)
			{
				if (DupVerts[k] >= WedgeIndex)
				{
					break;
				}
				int32 Location = RemapVerts.IsValidIndex(DupVerts[k]) ? RemapVerts[DupVerts[k]] : INDEX_NONE;
				if (Location != INDEX_NONE && AreVerticesEqual(StaticMeshVertex, StaticMeshBuildVertices[Location], VertexComparisonThreshold))
				{
					Index = Location;
					break;
				}
			}
			if (Index == INDEX_NONE)
			{
				Index = StaticMeshBuildVertices.Add(StaticMeshVertex);
			}
			RemapVerts[WedgeIndex] = Index;
			OutWedgeMap[WedgeIndex] = Index;
			SectionIndices.Add( Index );
		}
	}


	//Optimize before setting the buffer
	if (NumVertexInstances < 100000 * 3)
	{
		BuildOptimizationHelper::CacheOptimizeVertexAndIndexBuffer(StaticMeshBuildVertices, OutPerSectionIndices, OutWedgeMap);
		//check(OutWedgeMap.Num() == MeshDescription->VertexInstances().Num());
	}
}

void BuildAllBufferOptimizations(FStaticMeshLODResources& StaticMeshLOD, const FMeshBuildSettings& LODBuildSettings, TArray< uint32 >& IndexBuffer, bool bNeeds32BitIndices, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildAllBufferOptimizations);

	if (StaticMeshLOD.AdditionalIndexBuffers == nullptr)
	{
		StaticMeshLOD.AdditionalIndexBuffers = new FAdditionalStaticMeshIndexBuffers();
	}

	const EIndexBufferStride::Type IndexBufferStride = bNeeds32BitIndices ? EIndexBufferStride::Force32Bit : EIndexBufferStride::Force16Bit;

	// Build the reversed index buffer.
	if (LODBuildSettings.bBuildReversedIndexBuffer)
	{
		TArray<uint32> InversedIndices;
		const int32 IndexCount = IndexBuffer.Num();
		InversedIndices.AddUninitialized(IndexCount);

		for (int32 SectionIndex = 0; SectionIndex < StaticMeshLOD.Sections.Num(); ++SectionIndex)
		{
			const FStaticMeshSection& SectionInfo = StaticMeshLOD.Sections[SectionIndex];
			const int32 SectionIndexCount = SectionInfo.NumTriangles * 3;

			for (int32 i = 0; i < SectionIndexCount; ++i)
			{
				InversedIndices[SectionInfo.FirstIndex + i] = IndexBuffer[SectionInfo.FirstIndex + SectionIndexCount - 1 - i];
			}
		}
		StaticMeshLOD.AdditionalIndexBuffers->ReversedIndexBuffer.SetIndices(InversedIndices, IndexBufferStride);
	}

	// Build the depth-only index buffer.
	TArray<uint32> DepthOnlyIndices;
	{
		BuildOptimizationHelper::BuildDepthOnlyIndexBuffer(
			DepthOnlyIndices,
			StaticMeshBuildVertices,
			IndexBuffer,
			StaticMeshLOD.Sections
		);

		if (DepthOnlyIndices.Num() < 50000 * 3)
		{
			BuildOptimizationThirdParty::CacheOptimizeIndexBuffer(DepthOnlyIndices);
		}

		StaticMeshLOD.DepthOnlyIndexBuffer.SetIndices(DepthOnlyIndices, IndexBufferStride);
	}

	// Build the inversed depth only index buffer.
	if (LODBuildSettings.bBuildReversedIndexBuffer)
	{
		TArray<uint32> ReversedDepthOnlyIndices;
		const int32 IndexCount = DepthOnlyIndices.Num();
		ReversedDepthOnlyIndices.AddUninitialized(IndexCount);
		for (int32 i = 0; i < IndexCount; ++i)
		{
			ReversedDepthOnlyIndices[i] = DepthOnlyIndices[IndexCount - 1 - i];
		}
		StaticMeshLOD.AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.SetIndices(ReversedDepthOnlyIndices, IndexBufferStride);
	}

	// Build a list of wireframe edges in the static mesh.
	{
		TArray<BuildOptimizationHelper::FMeshEdge> Edges;
		TArray<uint32> WireframeIndices;

		BuildOptimizationHelper::FStaticMeshEdgeBuilder(IndexBuffer, StaticMeshBuildVertices, Edges).FindEdges();
		WireframeIndices.Empty(2 * Edges.Num());
		for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); EdgeIndex++)
		{
			BuildOptimizationHelper::FMeshEdge&	Edge = Edges[EdgeIndex];
			WireframeIndices.Add(Edge.Vertices[0]);
			WireframeIndices.Add(Edge.Vertices[1]);
		}
		StaticMeshLOD.AdditionalIndexBuffers->WireframeIndexBuffer.SetIndices(WireframeIndices, IndexBufferStride);
	}
}
