// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigVisualGraphUtils.h"

FString FControlRigVisualGraphUtils::DumpRigHierarchyToDotGraph(URigHierarchy* InHierarchy)
{
	check(InHierarchy);

	FVisualGraph Graph(TEXT("Rig"));

	struct Local
	{
		static TArray<int32> VisitParents(const FRigBaseElement* InElement, URigHierarchy* InHierarchy, FVisualGraph& OutGraph)
		{
			TArray<int32> Result;
			FRigBaseElementParentArray Parents = InHierarchy->GetParents(InElement);

			for(int32 ParentIndex = 0; ParentIndex < Parents.Num(); ParentIndex++)
			{
				const int32 ParentNodeIndex = VisitElement(Parents[ParentIndex], InHierarchy, OutGraph);
				Result.Add(ParentNodeIndex);
			}

			return Result;
		}

		static int32 VisitElement(const FRigBaseElement* InElement, URigHierarchy* InHierarchy, FVisualGraph& OutGraph)
		{
			if(InElement->GetType() == ERigElementType::Curve)
			{
				return INDEX_NONE;
			}
			
			const FName NodeName = *FString::Printf(TEXT("Element_%d"), InElement->GetIndex());
			int32 NodeIndex = OutGraph.FindNode(NodeName);
			if(NodeIndex != INDEX_NONE)
			{
				return NodeIndex;
			}

			EVisualGraphShape Shape = EVisualGraphShape::Ellipse;
			TOptional<FLinearColor> Color;

			switch(InElement->GetType())
			{
				case ERigElementType::Bone:
				{
					Shape = EVisualGraphShape::Box;
						
					if(Cast<FRigBoneElement>(InElement)->BoneType == ERigBoneType::User)
					{
						Color = FLinearColor::Green;
					}
					break;
				}
				case ERigElementType::Null:
				{
					Shape = EVisualGraphShape::Diamond;
					break;
				}
				case ERigElementType::Control:
				{
					Color = Cast<FRigControlElement>(InElement)->Settings.GizmoColor;
					break;
				}
				default:
				{
					break;
				}
			}

			NodeIndex = OutGraph.AddNode(NodeName, InElement->GetName(), Color, Shape);

			if(NodeIndex != INDEX_NONE)
			{
				TArray<int32> Parents = VisitParents(InElement, InHierarchy, OutGraph);
				TArray<FRigElementWeight> Weights = InHierarchy->GetParentWeightArray(InElement);
				for(int32 ParentIndex = 0; ParentIndex < Parents.Num(); ParentIndex++)
				{
					const int32 ParentNodeIndex = Parents[ParentIndex];
					if(ParentNodeIndex != INDEX_NONE)
					{
						const TOptional<FLinearColor> EdgeColor;
						TOptional<EVisualGraphStyle> Style;
						if(Weights.IsValidIndex(ParentIndex))
						{
							if(Weights[ParentIndex].IsAlmostZero())
							{
								Style = EVisualGraphStyle::Dotted;
							}
						}
						OutGraph.AddEdge(
							NodeIndex,
							ParentNodeIndex,
							EVisualGraphEdgeDirection::SourceToTarget,
							NAME_None,
							TOptional<FName>(),
							EdgeColor,
							Style);
					}
				}
			}

			return NodeIndex;
		}
	};

	InHierarchy->ForEach([InHierarchy, &Graph](const FRigBaseElement* InElement)
	{
		Local::VisitElement(InElement, InHierarchy, Graph);
		return true;
	});

	return Graph.DumpDot();
}