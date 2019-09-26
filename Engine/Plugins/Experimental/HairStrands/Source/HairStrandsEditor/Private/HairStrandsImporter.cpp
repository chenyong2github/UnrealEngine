// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsImporter.h"

#include "HairDescription.h"
#include "GroomAsset.h"
#include "GroomComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogHairImporter, Log, All);

FHairImportContext::FHairImportContext(UObject* InParent, UClass* InClass, FName InName, EObjectFlags InFlags)
	: Parent(InParent)
	, Class(InClass)
	, Name(InName)
	, Flags(InFlags)
{
}

UGroomAsset* FHairStrandsImporter::ImportHair(const FHairImportContext& ImportContext, FHairDescription& HairDescription, UGroomAsset* ExistingHair)
{
	// For now, just convert HairDescription to HairStrandsDatas
	int32 NumCurves = HairDescription.GetNumStrands();
	int32 NumVertices = HairDescription.GetNumVertices();

	// Check for required attributes
	TGroomAttributesRef<int> MajorVersion = HairDescription.GroomAttributes().GetAttributesRef<int>(HairAttribute::Groom::MajorVersion);
	TGroomAttributesRef<int> MinorVersion = HairDescription.GroomAttributes().GetAttributesRef<int>(HairAttribute::Groom::MinorVersion);

	if (!MajorVersion.IsValid() || !MinorVersion.IsValid())
	{
		UE_LOG(LogHairImporter, Warning, TEXT("Failed to import hair: No version number attributes found. Please re-export the input file."));
		return nullptr;
	}

	FGroomID GroomID(0);
	
	TGroomAttributesRef<float> GroomHairWidthAttribute = HairDescription.GroomAttributes().GetAttributesRef<float>(HairAttribute::Groom::Width);
	TOptional<float> GroomHairWidth;
	if (GroomHairWidthAttribute.IsValid())
	{
		GroomHairWidth = GroomHairWidthAttribute[GroomID];
	}

	TGroomAttributesRef<FVector> GroomHairColorAttribute = HairDescription.GroomAttributes().GetAttributesRef<FVector>(HairAttribute::Groom::Color);
	TOptional<FVector> GroomHairColor;
	if (GroomHairColorAttribute.IsValid())
	{
		GroomHairColor = GroomHairColorAttribute[GroomID];
	}

	TVertexAttributesRef<FVector> VertexPositions = HairDescription.VertexAttributes().GetAttributesRef<FVector>(HairAttribute::Vertex::Position);
	TStrandAttributesRef<int> StrandNumVertices = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::VertexCount);

	if (!VertexPositions.IsValid() || !StrandNumVertices.IsValid())
	{
		UE_LOG(LogHairImporter, Warning, TEXT("Failed to import hair: No vertices or curves data found."));
		return nullptr;
	}

	TVertexAttributesRef<float> VertexWidths = HairDescription.VertexAttributes().GetAttributesRef<float>(HairAttribute::Vertex::Width);
	TStrandAttributesRef<float> StrandWidths = HairDescription.StrandAttributes().GetAttributesRef<float>(HairAttribute::Strand::Width);

	TStrandAttributesRef<FVector2D> StrandRootUV = HairDescription.StrandAttributes().GetAttributesRef<FVector2D>(HairAttribute::Strand::RootUV);
	bool bHasUVData = StrandRootUV.IsValid();

	TStrandAttributesRef<bool> StrandGuides = HairDescription.StrandAttributes().GetAttributesRef<bool>(HairAttribute::Strand::Guide);
	TStrandAttributesRef<int> GroupIDs = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::GroupID);

	FHairStrandsDatas HairRenderData;
	FHairStrandsDatas HairSimulationData;
	FHairStrandsDatas* CurrentHairStrandsDatas = &HairRenderData;

	TMap<int32, FHairGroupRenderSettings> RenderHairGroups;
	TMap<int32, FHairGroupSimulationSettings> SimHairGroups;

	int32 GlobalVertexIndex = 0;
	int32 NumHairCurves = 0;
	int32 NumGuideCurves = 0;
	int32 NumHairPoints = 0;
	int32 NumGuidePoints = 0;
	for (int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
	{
		FStrandID StrandID(CurveIndex);

		bool bIsGuide = false;
		if (StrandGuides.IsValid())
		{
			bIsGuide = StrandGuides[StrandID];
		}

		int32 CurveNumVertices = StrandNumVertices[StrandID];

		int32 GroupID = 0;
		if (GroupIDs.IsValid())
		{
			GroupID = GroupIDs[StrandID];
		}

		if (!bIsGuide)
		{
			++NumHairCurves;
			NumHairPoints += CurveNumVertices;
			CurrentHairStrandsDatas = &HairRenderData;

			FHairGroupRenderSettings& GroupSettings = RenderHairGroups.FindOrAdd(GroupID);
			GroupSettings.GroupID = GroupID;
			++GroupSettings.NumCurves;
		}
		else
		{
			++NumGuideCurves;
			NumGuidePoints += CurveNumVertices;
			CurrentHairStrandsDatas = &HairSimulationData;

			FHairGroupSimulationSettings& GroupSettings = SimHairGroups.FindOrAdd(GroupID);
			GroupSettings.GroupID = GroupID;
			++GroupSettings.NumCurves;
		}

		CurrentHairStrandsDatas->StrandsCurves.CurvesCount.Add(CurveNumVertices);
		CurrentHairStrandsDatas->StrandsCurves.CurvesGroupID.Add(GroupID);

		if (bHasUVData)
		{
			CurrentHairStrandsDatas->StrandsCurves.CurvesRootUV.Add(StrandRootUV[StrandID]);
		}

		float StrandWidth = GroomHairWidth ? GroomHairWidth.GetValue() : 0.f;
		if (StrandWidths.IsValid())
		{
			StrandWidth = StrandWidths[StrandID];
		}

		for (int32 VertexIndex = 0; VertexIndex < CurveNumVertices; ++VertexIndex, ++GlobalVertexIndex)
		{
			FVertexID VertexID(GlobalVertexIndex);

			CurrentHairStrandsDatas->StrandsPoints.PointsPosition.Add(VertexPositions[VertexID]);

			float VertexWidth = 0.f;
			if (VertexWidths.IsValid())
			{
				VertexWidth = VertexWidths[VertexID];
			}

			// Fall back to strand width if there was no vertex width
			if (VertexWidth == 0.f && StrandWidth != 0.f)
			{
				VertexWidth = StrandWidth;
			}

			CurrentHairStrandsDatas->StrandsPoints.PointsRadius.Add(VertexWidth * 0.5f);
		}
	}

	FGroomComponentRecreateRenderStateContext RecreateRenderContext(ExistingHair);

	UGroomAsset* HairAsset = nullptr;
	if (ExistingHair)
	{
		HairAsset = ExistingHair;

		ExistingHair->Reset();
	}

	if (!HairAsset)
	{
		HairAsset = NewObject<UGroomAsset>(ImportContext.Parent, ImportContext.Class, ImportContext.Name, ImportContext.Flags);

		if (!HairAsset)
		{
			UE_LOG(LogHairImporter, Warning, TEXT("Failed to import hair: Could not allocate memory to create asset."));
			return nullptr;
		}
	}

	HairRenderData.StrandsCurves.SetNum(NumHairCurves);
	HairRenderData.StrandsPoints.SetNum(NumHairPoints);
	HairRenderData.BuildInternalDatas(!bHasUVData);
	HairAsset->HairRenderData = MoveTemp(HairRenderData);

	if (NumGuideCurves > 0)
	{
		HairSimulationData.StrandsCurves.SetNum(NumGuideCurves);
		HairSimulationData.StrandsPoints.SetNum(NumGuidePoints);
		HairSimulationData.BuildInternalDatas(true); // Imported guides don't currently have root UVs so force computing them
		HairAsset->HairSimulationData = MoveTemp(HairSimulationData);
	}
	else
	{
		DecimateStrandData(HairAsset->HairRenderData, FMath::Clamp(HairAsset->HairToGuideDensity, 0.f, 1.f), HairAsset->HairSimulationData);
	}

	HairAsset->HairDescription = MakeUnique<FHairDescription>(MoveTemp(HairDescription));

	if (RenderHairGroups.Num() > 0)
	{
		for (TPair<int32, FHairGroupRenderSettings>& Pair : RenderHairGroups)
		{
			HairAsset->RenderHairGroups.Add(MoveTemp(Pair.Value));
		}
	}
	else
	{
		FHairGroupRenderSettings& GroupSettings = HairAsset->RenderHairGroups.AddDefaulted_GetRef();
		GroupSettings.GroupID = 0;
		GroupSettings.NumCurves = HairRenderData.GetNumCurves();
	}

	if (SimHairGroups.Num() > 0)
	{
		for (TPair<int32, FHairGroupSimulationSettings>& Pair : SimHairGroups)
		{
			HairAsset->SimulationHairGroups.Add(MoveTemp(Pair.Value));
		}
	}
	else
	{
		FHairGroupSimulationSettings& GroupSettings = HairAsset->SimulationHairGroups.AddDefaulted_GetRef();
		GroupSettings.GroupID = 0;
		GroupSettings.NumCurves = HairAsset->HairSimulationData.GetNumCurves();
		GroupSettings.bIsAutoGenerated = true;
	}

	HairAsset->InitResource();

	return HairAsset;
}
