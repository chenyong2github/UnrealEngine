// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SimulationBaseConfigNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "Chaos/CollectionPropertyFacade.h"

#define LOCTEXT_NAMESPACE "ChaosClothAssetSimulationBaseConfigNode"

namespace UE::Chaos::ClothAsset::Private
{
	static void LogAndToastDuplicateProperty(const FName& NodeName, const FName& PropertyName)
	{
		using namespace UE::Chaos::ClothAsset::DataflowNodes;

		const FText Message = FText::Format(
			LOCTEXT(
				"DuplicateProperty",
				"Cloth collection property '{1}' was already set in an upstream node, and its values are now overriden by node '{0}'."),
			FText::FromName(NodeName),
			FText::FromName(PropertyName));

		LogAndToastWarning(Message);
	}

	static void LogAndToastSimilarProperty(const FName& NodeName, const FName& PropertyName, const FName& SimilarPropertyName)
	{
		using namespace UE::Chaos::ClothAsset::DataflowNodes;

		const FText Message = FText::Format(
			LOCTEXT(
				"SimilarProperty",
				"Cloth collection property '{1}' set in node and '{0}' is similar to the property '{2}' already set in an upstream node, "
				"which might result in an undefined simulation behavior."),
			FText::FromName(NodeName),
			FText::FromName(PropertyName),
			FText::FromName(SimilarPropertyName));

		LogAndToastWarning(Message);
	}
}

FChaosClothAssetSimulationBaseConfigNode::FChaosClothAssetSimulationBaseConfigNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{}

void FChaosClothAssetSimulationBaseConfigNode::RegisterCollectionConnections()
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection, &Collection);
}

void FChaosClothAssetSimulationBaseConfigNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace Chaos::Softs;
	using namespace UE::Chaos::ClothAsset::DataflowNodes;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InCollection));

		FCollectionPropertyMutableFacade Properties(ClothCollection);
		Properties.DefineSchema();

		AddProperties(Properties);

		EvaluateClothCollection(Context, ClothCollection);

		SetValue(Context, MoveTemp(*ClothCollection), &Collection);
	}
}

int32 FChaosClothAssetSimulationBaseConfigNode::AddPropertyHelper(
	::Chaos::Softs::FCollectionPropertyMutableFacade& Properties,
	const FName& PropertyName,
	bool bIsAnimatable,
	const TArray<FName>& SimilarPropertyNames) const
{
	using namespace UE::Chaos::ClothAsset::Private;

	constexpr bool bIsEnabled = true;

	int32 KeyIndex = Properties.GetKeyIndex(PropertyName.ToString());
	if (KeyIndex == INDEX_NONE)
	{
		KeyIndex = Properties.AddProperty(PropertyName.ToString(), bIsEnabled, bIsAnimatable);
	}
	else
	{
		Properties.SetAnimatable(KeyIndex, bIsAnimatable);
		LogAndToastDuplicateProperty(FDataflowNode::Name, PropertyName);
	}

	for (const FName& SimilarPropertyName : SimilarPropertyNames)
	{
		if (Properties.GetKeyIndex(SimilarPropertyName.ToString()) != INDEX_NONE)
		{
			LogAndToastSimilarProperty(FDataflowNode::Name, PropertyName, SimilarPropertyName);
		}
	}

	return KeyIndex;
}

#undef LOCTEXT_NAMESPACE
