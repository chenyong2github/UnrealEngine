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
		typedef void (Chaos::FClothingSimulation::*FDebugDrawFunction)(FPrimitiveDrawInterface*) const;
		typedef void (Chaos::FClothingSimulation::*FDebugDrawTextsFunction)(FCanvas*, const FSceneView*) const;
		FDebugDrawFunction DebugDrawFunction;
		FDebugDrawTextsFunction DebugDrawTextsFunction;

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
		FVisualizationOption(FDebugDrawTextsFunction InDebugDrawTextsFunction, const FText& InDisplayName, const FText& InToolTip, bool bInDisablesSimulation = false, bool bInHidesClothSections = false)
			: DebugDrawTextsFunction(InDebugDrawTextsFunction)
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
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawPhysMeshShaded      , LOCTEXT("ChaosVisName_PhysMesh"            , "Physical Mesh (Flat Shaded)"), LOCTEXT("ChaosVisName_PhysMeshShaded_ToolTip"      , "Draws the current physical result as a doubled sided flat shaded mesh"), /*bDisablesSimulation =*/false, /*bHidesClothSections=*/true),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawPhysMeshWired       , LOCTEXT("ChaosVisName_PhysMeshWire"        , "Physical Mesh (Wireframe)"  ), LOCTEXT("ChaosVisName_PhysMeshWired_ToolTip"       , "Draws the current physical mesh result in wireframe")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawAnimMeshWired       , LOCTEXT("ChaosVisName_AnimMeshWire"        , "Animated Mesh (Wireframe)"  ), LOCTEXT("ChaosVisName_AnimMeshWired_ToolTip"       , "Draws the current animated mesh input in wireframe")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawParticleIndices     , LOCTEXT("ChaosVisName_ParticleIndices"     , "Particle Indices"           ), LOCTEXT("ChaosVisName_ParticleIndices_ToolTip"     , "Draws the particle indices as instantiated by the solver")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawElementIndices      , LOCTEXT("ChaosVisName_ElementIndices"      , "Element Indices"            ), LOCTEXT("ChaosVisName_ElementIndices_ToolTip"      , "Draws the element's (triangle or other) indices as instantiated by the solver")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawPointNormals        , LOCTEXT("ChaosVisName_PointNormals"        , "Point Normals"              ), LOCTEXT("ChaosVisName_PointNormals_ToolTip"        , "Draws the current point normals for the simulation mesh")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawInversedPointNormals, LOCTEXT("ChaosVisName_InversedPointNormals", "Inversed Point Normals"     ), LOCTEXT("ChaosVisName_InversedPointNormals_ToolTip", "Draws the inversed point normals for the simulation mesh")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawCollision           , LOCTEXT("ChaosVisName_Collision"           , "Collisions"                 ), LOCTEXT("ChaosVisName_Collision_ToolTip"           , "Draws the collision bodies the simulation is currently using")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawBackstops           , LOCTEXT("ChaosVisName_Backstop"            , "Backstops"                  ), LOCTEXT("ChaosVisName_Backstop_ToolTip"            , "Draws the backstop radius and position for each simulation particle")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawBackstopDistances   , LOCTEXT("ChaosVisName_BackstopDistance"    , "Backstop Distances"         ), LOCTEXT("ChaosVisName_BackstopDistance_ToolTip"    , "Draws the backstop distance offset for each simulation particle"), /*bDisablesSimulation =*/true),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawMaxDistances        , LOCTEXT("ChaosVisName_MaxDistance"         , "Max Distances"              ), LOCTEXT("ChaosVisName_MaxDistance_ToolTip"         , "Draws the current max distances for the sim particles as a line along its normal"), /*bDisablesSimulation =*/true),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawMaxDistanceValues   , LOCTEXT("ChaosVisName_MaxDistanceValue"    , "Max Distances As Numbers"   ), LOCTEXT("ChaosVisName_MaxDistanceValue_ToolTip"    , "Draws the current max distances as numbers")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawAnimDrive           , LOCTEXT("ChaosVisName_AnimDrive"           , "Anim Drive"                 ), LOCTEXT("ChaosVisName_AnimDrive_Tooltip"           , "Draws the current skinned reference mesh for the simulation which anim drive will attempt to reach if enabled")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawBendingConstraint   , LOCTEXT("ChaosVisName_BendingConstraint"   , "Bending Constraint"         ), LOCTEXT("ChaosVisName_BendingConstraint_Tooltip"   , "Draws the bending spring constraints")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawLongRangeConstraint , LOCTEXT("ChaosVisName_LongRangeConstraint" , "Long Range Constraint"      ), LOCTEXT("ChaosVisName_LongRangeConstraint_Tooltip" , "Draws the long range attachment constraint distances")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawWindForces          , LOCTEXT("ChaosVisName_WindForces"          , "Wind Aerodynamic Forces"    ), LOCTEXT("ChaosVisName_Wind_Tooltip"                , "Draws the Wind drag and lift forces")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawLocalSpace          , LOCTEXT("ChaosVisName_LocalSpace"          , "Local Space Reference Bone" ), LOCTEXT("ChaosVisName_LocalSpace_Tooltip"          , "Draws the local space reference bone")),
	FVisualizationOption(&Chaos::FClothingSimulation::DebugDrawSelfCollision       , LOCTEXT("ChaosVisName_SelfCollision"       , "Self Collision"             ), LOCTEXT("ChaosVisName_SelfCollision_Tooltip"       , "Draws the self collision thickness/debugging information")),
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

void FSimulationEditorExtender::DebugDrawSimulation(const IClothingSimulation* Simulation, USkeletalMeshComponent* /*OwnerComponent*/, FPrimitiveDrawInterface* PDI)
{
	if (!ensure(Simulation)) { return; }

	const FClothingSimulation* const ChaosSimulation = static_cast<const FClothingSimulation*>(Simulation);

	for (int32 OptionIndex = 0; OptionIndex < FVisualizationOption::Count; ++OptionIndex)
	{
		if (Flags[OptionIndex] && FVisualizationOption::OptionData[OptionIndex].DebugDrawFunction)
		{
			(ChaosSimulation->*(FVisualizationOption::OptionData[OptionIndex].DebugDrawFunction))(PDI);
		}
	}
}

void FSimulationEditorExtender::DebugDrawSimulationTexts(const IClothingSimulation* Simulation, USkeletalMeshComponent* /*OwnerComponent*/, FCanvas* Canvas, const FSceneView* SceneView)
{
	if (!ensure(Simulation)) { return; }

	const FClothingSimulation* const ChaosSimulation = static_cast<const FClothingSimulation*>(Simulation);

	for (int32 OptionIndex = 0; OptionIndex < FVisualizationOption::Count; ++OptionIndex)
	{
		if (Flags[OptionIndex] && FVisualizationOption::OptionData[OptionIndex].DebugDrawTextsFunction)
		{
			(ChaosSimulation->*(FVisualizationOption::OptionData[OptionIndex].DebugDrawTextsFunction))(Canvas, SceneView);
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