// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_CHAOS

#include "ChaosClothEditor/ChaosSimulationEditorExtender.h"

#include "ChaosClothEditorPrivate.h"
#include "ChaosCloth/ChaosClothingSimulationFactory.h"
#include "ChaosCloth/ChaosClothingSimulation.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "IPersonaPreviewScene.h"

#define LOCTEXT_NAMESPACE "ChaosSimulationEditorExtender"

namespace Chaos
{
	struct FVisualizationOption
	{
		// Actual option entries
		static const FVisualizationOption OptionData[];
		static const uint32 Count;

		// Chaos debug draw function
		typedef void (Chaos::ClothingSimulation::*FDebugDrawFunction)(USkeletalMeshComponent*, FPrimitiveDrawInterface*) const;
		FDebugDrawFunction DebugDrawFunction;

		FText DisplayName;         // Text for menu entries.
		FText ToolTip;             // Text for menu tooltips.
		bool bDisablesSimulation;  // Whether or not this option requires the simulation to be disabled.
		bool bHidesClothSections;  // Hides the cloth section to avoid zfighting with the debug geometry.

		FVisualizationOption(FDebugDrawFunction InDebugDrawFunction, const FText& InDisplayName, const FText& InToolTip, bool bInDisablesSimulation = false, bool bInHidesClothSections = false)
			: DebugDrawFunction(InDebugDrawFunction)
			, DisplayName(InDisplayName)
			, ToolTip(InToolTip)
			, bDisablesSimulation(bInDisablesSimulation)
			, bHidesClothSections(bInHidesClothSections)
		{}
	};
}

using namespace Chaos;

const FVisualizationOption FVisualizationOption::OptionData[] = 
{
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawPhysMeshShaded      , LOCTEXT("ChaosVisName_PhysMesh"            , "Physical Mesh (Flat Shaded)"), LOCTEXT("ChaosVisName_PhysMeshShaded_ToolTip"      , "Draws the current physical result as a doubled sided flat shaded mesh"), /*bDisablesSimulation =*/false, /*bHidesClothSections=*/true),
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawPhysMeshWired       , LOCTEXT("ChaosVisName_PhysMeshWire"        , "Physical Mesh (Wireframe)"  ), LOCTEXT("ChaosVisName_PhysMeshWired_ToolTip"       , "Draws the current physical mesh result in wireframe")),
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawPointNormals        , LOCTEXT("ChaosVisName_PointNormals"        , "Point Normals"              ), LOCTEXT("ChaosVisName_PointNormals_ToolTip"        , "Draws the current point normals for the simulation mesh")),
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawInversedPointNormals, LOCTEXT("ChaosVisName_InversedPointNormals", "Inversed Point Normals"     ), LOCTEXT("ChaosVisName_InversedPointNormals_ToolTip", "Draws the inversed point normals for the simulation mesh")),
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawFaceNormals         , LOCTEXT("ChaosVisName_FaceNormals"         , "Face Normals"               ), LOCTEXT("ChaosVisName_FaceNormals_ToolTip"         , "Draws the current face normals for the simulation mesh")),
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawInversedFaceNormals , LOCTEXT("ChaosVisName_InversedFaceNormals" , "Inversed Face Normals"      ), LOCTEXT("ChaosVisName_InversedFaceNormals_ToolTip" , "Draws the inversed face normals for the simulation mesh")),
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawCollision           , LOCTEXT("ChaosVisName_Collision"           , "Collisions"                 ), LOCTEXT("ChaosVisName_Collision_ToolTip"           , "Draws the collision bodies the simulation is currently using")),
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawBackstops           , LOCTEXT("ChaosVisName_Backstop"            , "Backstops"                  ), LOCTEXT("ChaosVisName_Backstop_ToolTip"            , "Draws the backstop offset for each simulation particle"), /*bDisablesSimulation =*/true),
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawMaxDistances        , LOCTEXT("ChaosVisName_MaxDistance"         , "Max Distances"              ), LOCTEXT("ChaosVisName_MaxDistance_ToolTip"         , "Draws the current max distances for the sim particles as a line along its normal"), true),
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawAnimDrive           , LOCTEXT("ChaosVisName_AnimDrive"           , "Anim Drive"                 ), LOCTEXT("ChaosVisName_AnimDrive_Tooltip"           , "Draws the current skinned reference mesh for the simulation which anim drive will attempt to reach if enabled")),
	FVisualizationOption(&Chaos::ClothingSimulation::DebugDrawLongRangeConstraint , LOCTEXT("ChaosVisName_LongRangeConstraint" , "Long Range Constraint"      ), LOCTEXT("ChaosVisName_LongRangeConstraint_Tooltip" , "Draws the long range attachment constraint distances"))
};
const uint32 FVisualizationOption::Count = sizeof(OptionData) / sizeof(FVisualizationOption);

FSimulationEditorExtender::FSimulationEditorExtender()
	: Flags(false, FVisualizationOption::Count)
{
}

UClass* FSimulationEditorExtender::GetSupportedSimulationFactoryClass()
{
	return UChaosClothingSimulationFactory::StaticClass();
}

void FSimulationEditorExtender::ExtendViewportShowMenu(FMenuBuilder& MenuBuilder, TSharedRef<IPersonaPreviewScene> PreviewScene)
{
	MenuBuilder.BeginSection(TEXT("ChaosSimulation_Visualizations"), LOCTEXT("VisualizationSection", "Visualizations"));
	{
		for (uint32 OptionIndex = 0; OptionIndex < FVisualizationOption::Count; ++OptionIndex)
		{
			// Handler for visualization entry being clicked
			const FExecuteAction ExecuteAction = FExecuteAction::CreateLambda([this, OptionIndex, PreviewScene]()
			{
				Flags[OptionIndex] = !Flags[OptionIndex];

				// If we need to toggle the disabled or visibility states, handle it
				if (UDebugSkelMeshComponent* const MeshComponent = PreviewScene->GetPreviewMeshComponent())
				{
					// Disable simulation
					const bool bShouldDisableSimulation = ShouldDisableSimulation();
					if (bShouldDisableSimulation && MeshComponent->bDisableClothSimulation != bShouldDisableSimulation)
					{
						MeshComponent->bDisableClothSimulation = !MeshComponent->bDisableClothSimulation;
					}
					// Hide cloth section
					if (FVisualizationOption::OptionData[OptionIndex].bHidesClothSections)
					{
						const bool bIsClothSectionsVisible = !Flags[OptionIndex];
						ShowClothSections(MeshComponent, bIsClothSectionsVisible);
					}
				}
			});

			// Checkstate function for visualization entries
			const FIsActionChecked IsActionChecked = FIsActionChecked::CreateLambda([this, OptionIndex]()
			{
				return Flags[OptionIndex];
			});

			const FUIAction Action(ExecuteAction, FCanExecuteAction(), IsActionChecked);

			MenuBuilder.AddMenuEntry(FVisualizationOption::OptionData[OptionIndex].DisplayName, FVisualizationOption::OptionData[OptionIndex].ToolTip, FSlateIcon(), Action, NAME_None, EUserInterfaceActionType::ToggleButton);
		}
	}
	MenuBuilder.EndSection();
}

void FSimulationEditorExtender::DebugDrawSimulation(const IClothingSimulation* Simulation, USkeletalMeshComponent* OwnerComponent, FPrimitiveDrawInterface* PDI)
{
	if (!ensure(Simulation)) { return; }

	const ClothingSimulation* const ChaosSimulation = static_cast<const ClothingSimulation*>(Simulation);

	for (int32 OptionIndex = 0; OptionIndex < FVisualizationOption::Count; ++OptionIndex)
	{
		if (Flags[OptionIndex])
		{
			(ChaosSimulation->*(FVisualizationOption::OptionData[OptionIndex].DebugDrawFunction))(OwnerComponent, PDI);
		}
	}
}

bool FSimulationEditorExtender::ShouldDisableSimulation() const
{
	for (uint32 OptionIndex = 0; OptionIndex < FVisualizationOption::Count; ++OptionIndex)
	{
		if (Flags[OptionIndex])
		{
			const FVisualizationOption& Data = FVisualizationOption::OptionData[OptionIndex];

			if (Data.bDisablesSimulation)
			{
				return true;
			}
		}
	}
	return false;
}

void FSimulationEditorExtender::ShowClothSections(USkeletalMeshComponent* MeshComponent, bool bIsClothSectionsVisible) const
{
	if (FSkeletalMeshRenderData* const SkeletalMeshRenderData = MeshComponent->GetSkeletalMeshRenderData())
	{
		for (int32 LODIndex = 0; LODIndex < SkeletalMeshRenderData->LODRenderData.Num(); ++LODIndex)
		{
			FSkeletalMeshLODRenderData& SkeletalMeshLODRenderData = SkeletalMeshRenderData->LODRenderData[LODIndex];

			for (int32 SectionIndex = 0; SectionIndex < SkeletalMeshLODRenderData.RenderSections.Num(); ++SectionIndex)
			{
				FSkelMeshRenderSection& SkelMeshRenderSection = SkeletalMeshLODRenderData.RenderSections[SectionIndex];

				if (SkelMeshRenderSection.HasClothingData())
				{
					MeshComponent->ShowMaterialSection(SkelMeshRenderSection.MaterialIndex, SectionIndex, bIsClothSectionsVisible, LODIndex);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

#endif  // #if WITH_CHAOS