// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SelectionToWeightMapNode.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/CollectionClothSelectionFacade.h"
#include "ChaosClothAsset/ClothDataflowTools.h"
#include "Dataflow/DataflowInputOutput.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SelectionToWeightMapNode)
#define LOCTEXT_NAMESPACE "FChaosClothAssetSelectionToWeightMapNode"

FChaosClothAssetSelectionToWeightMapNode::FChaosClothAssetSelectionToWeightMapNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection, &Collection);
	RegisterInputConnection(&Name);
	RegisterOutputConnection(&Name, &Name);
}


void FChaosClothAssetSelectionToWeightMapNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(InCollection));
		FCollectionClothFacade ClothFacade(ClothCollection);

		const FCollectionClothSelectionConstFacade SelectionFacade(ClothCollection);
		const FString InName = GetValue<FString>(Context, &Name);

		if (SelectionFacade.IsValid())
		{
			const int32 FoundSelectionIndex = SelectionFacade.FindSelection(InName);

			if (FoundSelectionIndex != INDEX_NONE)
			{
				const FString& SelectionType = SelectionFacade.GetType()[FoundSelectionIndex];

				if (SelectionType == "SimVertex3D" || SelectionType == "SimVertex2D")
				{
					const FName MapName(InName);
					ClothFacade.AddWeightMap(MapName);
					TArrayView<float> OutClothWeights = ClothFacade.GetWeightMap(MapName);

					const TSet<int32>& Selection = SelectionFacade.GetIndices()[FoundSelectionIndex];

					if (SelectionType == "SimVertex3D")
					{
						for (int32 VertexIndex = 0; VertexIndex < OutClothWeights.Num(); ++VertexIndex)
						{
							OutClothWeights[VertexIndex] = Selection.Contains(VertexIndex) ? 1.0f : 0.0f;
						}
					}
					else
					{
						check(SelectionType == "SimVertex2D");

						// We are given a selection over the set of 2D vertices, but weight maps only exist for 3D vertices, so
						// we need a bit of translation

						const TConstArrayView<TArray<int32>> Vertex3DTo2D = ClothFacade.GetSimVertex2DLookup();

						for (int32 Vertex3DIndex = 0; Vertex3DIndex < OutClothWeights.Num(); ++Vertex3DIndex)
						{
							// If any corresponding 2D vertex is selected, set the 3D weight map value to one, otherwise it gets zero
							OutClothWeights[Vertex3DIndex] = 0.0;
							for (const int32 Vertex2DIndex : Vertex3DTo2D[Vertex3DIndex])
							{
								if (Selection.Contains(Vertex2DIndex))
								{
									OutClothWeights[Vertex3DIndex] = 1.0;
									break;
								}
							}
						}
					}
				}
				else
				{
					FClothDataflowTools::LogAndToastWarning(*this,
						LOCTEXT("SelectionTypeNotCorrectHeadline", "Selection type is incompatible."),
						FText::Format(LOCTEXT("SelectionTypeNotCorrectDetails", "Selection with Name \"{0}\" does not have Type \"SimVertex3D\" or \"SimVertex2D\"."),
							FText::FromString(InName)));
				}
			}
			else
			{
				FClothDataflowTools::LogAndToastWarning(*this,
					LOCTEXT("SelectionNameNotFoundHeadline", "Selection Name was not found."),
					FText::Format(LOCTEXT("SelectionNameNotFoundDetails", "A Selection with Name \"{0}\" was not found in the Collection."),
						FText::FromString(InName)));
			}
		}

		SetValue(Context, MoveTemp(*ClothCollection), &Collection);
		SetValue(Context, InName, &Name);
	}
}


#undef LOCTEXT_NAMESPACE
