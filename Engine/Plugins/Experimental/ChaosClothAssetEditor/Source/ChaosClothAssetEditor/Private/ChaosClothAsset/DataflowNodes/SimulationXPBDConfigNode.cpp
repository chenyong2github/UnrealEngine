// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/DataflowNodes/SimulationXPBDConfigNode.h"
#include "ChaosClothAsset/DataflowNodes/DataflowNodes.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "ChaosCloth/ChaosClothingSimulationConfig.h"
#include "Chaos/CollectionPropertyFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimulationXPBDConfigNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetSimulationXPBDConfigNode"

FChaosClothAssetSimulationXPBDConfigNode::FChaosClothAssetSimulationXPBDConfigNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
	, SharedSimulationConfig(NewObject<UChaosClothSharedSimConfig>())
{
	RegisterInputConnection(&Collection);

	RegisterInputConnection(&bEnableXPBDStretchConstraints);
	RegisterInputConnection(&StretchStiffness);
	RegisterInputConnection(&StretchDampingRatio);

	RegisterInputConnection(&bEnableXPBDBendConstraints);
	RegisterInputConnection(&bEnableBendAnisotropy);
	RegisterInputConnection(&BendingStiffness);
	RegisterInputConnection(&BendingStiffnessWeft);
	RegisterInputConnection(&BendingStiffnessBias);
	RegisterInputConnection(&BendingDampingRatio);
	RegisterInputConnection(&BucklingRatio);
	RegisterInputConnection(&BucklingStiffness);
	RegisterInputConnection(&BucklingStiffnessWeft);
	RegisterInputConnection(&BucklingStiffnessBias);

	RegisterInputConnection(&bEnableXPBDAreaConstraints);
	RegisterInputConnection(&AreaStiffness);

	RegisterOutputConnection(&Collection, &Collection);
}

void FChaosClothAssetSimulationXPBDConfigNode::Serialize(FArchive& Ar)
{
	if (!SharedSimulationConfig)
	{
		SharedSimulationConfig = NewObject<UChaosClothSharedSimConfig>();
	}
	SharedSimulationConfig->Serialize(Ar);
}

void FChaosClothAssetSimulationXPBDConfigNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace Chaos;
	using namespace Chaos::Softs;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InCollection));

		FCollectionPropertyMutableFacade Properties(ClothCollection);
		Properties.DefineSchema();

		// Set xpbd properties
		SetStretchProperties(Properties);
		SetBendingProperties(Properties);
		SetAreaProperties(Properties);
		
		// Copy shared config properties
		// Solver properties
		if (SharedSimulationConfig)
		{
			constexpr bool bEnable = true;
			constexpr bool bAnimatable = true;
			Properties.AddValue(TEXT("NumIterations"), SharedSimulationConfig->IterationCount, bEnable, bAnimatable);
			Properties.AddValue(TEXT("MaxNumIterations"), SharedSimulationConfig->MaxIterationCount, bEnable, bAnimatable);
			Properties.AddValue(TEXT("NumSubsteps"), SharedSimulationConfig->SubdivisionCount, bEnable, bAnimatable);
		}


		SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
	}
}

void FChaosClothAssetSimulationXPBDConfigNode::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SharedSimulationConfig);
}

FString FChaosClothAssetSimulationXPBDConfigNode::GetReferencerName() const
{
	return TEXT("FChaosClothAssetSimulationXPBDConfigNode");
}

void FChaosClothAssetSimulationXPBDConfigNode::SetStretchProperties(Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const
{
	if (bEnableXPBDStretchConstraints)
	{
		const int32 StretchStiffnessIndex = Properties.AddProperty(TEXT("XPBDEdgeSpringStiffness"));
		Properties.SetWeightedValue(StretchStiffnessIndex, StretchStiffness.Low, StretchStiffness.High);
		Properties.SetStringValue(StretchStiffnessIndex, TEXT("EdgeStiffness"));

		const int32 StretchDampingRatioIndex = Properties.AddProperty(TEXT("XPBDEdgeSpringDamping"));
		Properties.SetWeightedValue(StretchDampingRatioIndex, StretchDampingRatio.Low, StretchDampingRatio.High);
		Properties.SetStringValue(StretchDampingRatioIndex, TEXT("EdgeDamping"));
	}
	else
	{
		Properties.SetEnabled(TEXT("XPBDEdgeSpringStiffness"), false);
	}
}

void FChaosClothAssetSimulationXPBDConfigNode::SetBendingProperties(Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const
{
	if(bEnableXPBDBendConstraints)
	{
		if (bEnableBendAnisotropy)
		{
			const int32 BendingStiffnessWarpIndex = Properties.AddProperty(TEXT("XPBDBendingElementStiffnessWarp"));
			Properties.SetWeightedValue(BendingStiffnessWarpIndex, BendingStiffness.Low, BendingStiffness.High);
			Properties.SetStringValue(BendingStiffnessWarpIndex, TEXT("BendingStiffnessWarp"));
			const int32 BendingStiffnessWeftIndex = Properties.AddProperty(TEXT("XPBDBendingElementStiffnessWeft"));
			Properties.SetWeightedValue(BendingStiffnessWeftIndex, BendingStiffnessWeft.Low, BendingStiffnessWeft.High);
			Properties.SetStringValue(BendingStiffnessWeftIndex, TEXT("BendingStiffnessWeft"));
			const int32 BendingStiffnessBiasIndex = Properties.AddProperty(TEXT("XPBDBendingElementStiffnessBias"));
			Properties.SetWeightedValue(BendingStiffnessBiasIndex, BendingStiffnessBias.Low, BendingStiffnessBias.High);
			Properties.SetStringValue(BendingStiffnessBiasIndex, TEXT("BendingStiffnessBias"));

			const int32 BendingDampingRatioIndex = Properties.AddProperty(TEXT("XPBDBendingElementDamping"));
			Properties.SetWeightedValue(BendingDampingRatioIndex, BendingDampingRatio.Low, BendingDampingRatio.High);
			Properties.SetStringValue(BendingDampingRatioIndex, TEXT("BendingDampingRatio"));

			Properties.AddValue(TEXT("XPBDBucklingRatio"), BucklingRatio, true, true);

			const int32 BucklingStiffnessWarpIndex = Properties.AddProperty(TEXT("XPBDBucklingStiffnessWarp"));
			Properties.SetWeightedValue(BucklingStiffnessWarpIndex, BucklingStiffness.Low, BucklingStiffness.High);
			Properties.SetStringValue(BucklingStiffnessWarpIndex, TEXT("BucklingStiffnessWarp"));
			const int32 BucklingStiffnessWeftIndex = Properties.AddProperty(TEXT("XPBDBucklingStiffnessWeft"));
			Properties.SetWeightedValue(BucklingStiffnessWeftIndex, BucklingStiffnessWeft.Low, BucklingStiffnessWeft.High);
			Properties.SetStringValue(BucklingStiffnessWeftIndex, TEXT("BucklingStiffnessWeft"));
			const int32 BucklingStiffnessBiasIndex = Properties.AddProperty(TEXT("XPBDBucklingStiffnessBias"));
			Properties.SetWeightedValue(BucklingStiffnessBiasIndex, BucklingStiffnessBias.Low, BucklingStiffnessBias.High);
			Properties.SetStringValue(BucklingStiffnessBiasIndex, TEXT("BucklingStiffnessBias"));

			Properties.SetEnabled(TEXT("XPBDBendingElementStiffness"), false);
		}
		else
		{
			const int32 BendingStiffnessIndex = Properties.AddProperty(TEXT("XPBDBendingElementStiffness"));
			Properties.SetWeightedValue(BendingStiffnessIndex, BendingStiffness.Low, BendingStiffness.High);
			Properties.SetStringValue(BendingStiffnessIndex, TEXT("BendingStiffness"));

			const int32 BendingDampingRatioIndex = Properties.AddProperty(TEXT("XPBDBendingElementDamping"));
			Properties.SetWeightedValue(BendingDampingRatioIndex, BendingDampingRatio.Low, BendingDampingRatio.High);
			Properties.SetStringValue(BendingDampingRatioIndex, TEXT("BendingDampingRatio"));

			Properties.AddValue(TEXT("XPBDBucklingRatio"), BucklingRatio, true, true);

			const int32 BucklingStiffnessIndex = Properties.AddProperty(TEXT("XPBDBucklingStiffness"));
			Properties.SetWeightedValue(BucklingStiffnessIndex, BucklingStiffness.Low, BucklingStiffness.High);
			Properties.SetStringValue(BucklingStiffnessIndex, TEXT("BucklingStiffness"));

			Properties.SetEnabled(TEXT("XPBDBendingElementStiffnessWarp"), false);
		}
	}
	else
	{
		Properties.SetEnabled(TEXT("XPBDBendingElementStiffness"), false);
		Properties.SetEnabled(TEXT("XPBDBendingElementStiffnessWarp"), false);
	}
}

void FChaosClothAssetSimulationXPBDConfigNode::SetAreaProperties(Chaos::Softs::FCollectionPropertyMutableFacade& Properties) const
{
	if (bEnableXPBDAreaConstraints)
	{
		const int32 AreaStiffnessIndex = Properties.AddProperty(TEXT("XPBDAreaSpringStiffness"));
		Properties.SetWeightedValue(AreaStiffnessIndex, AreaStiffness.Low, AreaStiffness.High);
		Properties.SetStringValue(AreaStiffnessIndex, TEXT("AreaStiffness"));
	}
	else
	{
		Properties.SetEnabled(TEXT("XPBDAreaSpringStiffness"), false);
	}
}

#undef LOCTEXT_NAMESPACE
