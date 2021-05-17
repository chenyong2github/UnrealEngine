// Copyright Epic Games, Inc. All Rights Reserved.

/////////////////////////////////////////////////////
// UMaterialGraph

#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode_Comment.h"
#include "MaterialGraph/MaterialGraphNode_Composite.h"
#include "MaterialGraph/MaterialGraphNode_PinBase.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"

#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionComposite.h"
#include "Materials/MaterialExpressionPinBase.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionExecBegin.h"
#include "Materials/MaterialExpressionExecEnd.h"

#include "MaterialGraphNode_Knot.h"

#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "MaterialGraph"

UMaterialGraph::UMaterialGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UMaterialGraph::RebuildGraph()
{
	check(Material);

	// Pre-group expressions & comments per subgraph to avoid unnecessary iteration over all material expressions
	TMap<UMaterialExpression*, TArray<UMaterialExpression*>> SubgraphExpressionMap;
	TMap<UMaterialExpression*, TArray<UMaterialExpressionComment*>> SubgraphCommentMap;
	for (UMaterialExpression* Expression : Material->Expressions)
	{
		SubgraphExpressionMap.FindOrAdd(Expression->SubgraphExpression).Add(Expression);
	}
	for (UMaterialExpressionComment* Comment : Material->EditorComments)
	{
		if (Comment)
		{
			SubgraphCommentMap.FindOrAdd(Comment->SubgraphExpression).Add(Comment);
		}
	}

	RebuildGraphInternal(SubgraphExpressionMap, SubgraphCommentMap);
}

template<typename NodeType>
static UMaterialGraphNode* InitExpressionNewNode(UMaterialGraph* Graph, UMaterialExpression* Expression, bool bUserInvoked)
{
	UMaterialGraphNode* NewNode = nullptr;

	FGraphNodeCreator<NodeType> NodeCreator(*Graph);
	if (bUserInvoked)
	{
		NewNode = NodeCreator.CreateUserInvokedNode();
	}
	else
	{
		NewNode = NodeCreator.CreateNode(false);
	}
	NewNode->MaterialExpression = Expression;
	NewNode->RealtimeDelegate = Graph->RealtimeDelegate;
	NewNode->MaterialDirtyDelegate = Graph->MaterialDirtyDelegate;
	Expression->GraphNode = NewNode;
	Expression->SubgraphExpression = Graph->SubgraphExpression;
	NodeCreator.Finalize();

	return NewNode;
}

void UMaterialGraph::RebuildGraphInternal(const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& SubgraphExpressionMap, const TMap<UMaterialExpression*, TArray<UMaterialExpressionComment*>>& SubgraphCommentMap)
{
	Modify();

	RemoveAllNodes();

	if (!MaterialFunction && !SubgraphExpression)
	{
		// This needs to be done before building the new material inputs to guarantee that the shading model field is up to date
		Material->RebuildShadingModelField();

		// Initialize the material input list.
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_BaseColor, Material), MP_BaseColor, LOCTEXT("BaseColorToolTip", "Defines the overall color of the Material. Each channel is automatically clamped between 0 and 1")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_Metallic, Material), MP_Metallic, LOCTEXT("MetallicToolTip", "Controls how \"metal-like\" your surface looks like")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_Specular, Material), MP_Specular, LOCTEXT("SpecularToolTip", "Used to scale the current amount of specularity on non-metallic surfaces and is a value between 0 and 1, default at 0.5")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_Roughness, Material), MP_Roughness, LOCTEXT("RoughnessToolTip", "Controls how rough the Material is. Roughness of 0 (smooth) is a mirror reflection and 1 (rough) is completely matte or diffuse")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_Anisotropy, Material), MP_Anisotropy, LOCTEXT("AnisotropyToolTip", "Determines the extent the specular highlight is stretched along the tangent. Anisotropy from 0 to 1 results in a specular highlight that stretches from uniform to maximally stretched along the tangent direction.")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_EmissiveColor, Material), MP_EmissiveColor, LOCTEXT("EmissiveToolTip", "Controls which parts of your Material will appear to glow")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_Opacity, Material), MP_Opacity, LOCTEXT("OpacityToolTip", "Controls the translucency of the Material")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_OpacityMask, Material), MP_OpacityMask, LOCTEXT("OpacityMaskToolTip", "When in Masked mode, a Material is either completely visible or completely invisible")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_Normal, Material), MP_Normal, LOCTEXT("NormalToolTip", "Takes the input of a normal map")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_Tangent, Material), MP_Tangent, LOCTEXT("TangentToolTip", "Takes the input of a tangent map. Useful for specifying anisotropy direction.")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_WorldPositionOffset, Material), MP_WorldPositionOffset, LOCTEXT("WorldPositionOffsetToolTip", "Allows for the vertices of a mesh to be manipulated in world space by the Material")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_SubsurfaceColor, Material), MP_SubsurfaceColor, LOCTEXT("SubsurfaceToolTip", "Allows you to add a color to your Material to simulate shifts in color when light passes through the surface")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_CustomData0, Material), MP_CustomData0, FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_CustomData0, Material)));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_CustomData1, Material), MP_CustomData1, FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_CustomData1, Material)));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_AmbientOcclusion, Material), MP_AmbientOcclusion, LOCTEXT("AmbientOcclusionToolTip", "Simulate the self-shadowing that happens within crevices of a surface, or of a volume for volumetric clouds only")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_Refraction, Material), MP_Refraction, LOCTEXT("RefractionToolTip", "Takes in a texture or value that simulates the index of refraction of the surface")));

		for (int32 UVIndex = 0; UVIndex < UE_ARRAY_COUNT(Material->CustomizedUVs); UVIndex++)
		{
			//@todo - localize
			MaterialInputs.Add(FMaterialInputInfo(FText::FromString(FString::Printf(TEXT("Customized UV%u"), UVIndex)), (EMaterialProperty)(MP_CustomizedUVs0 + UVIndex), FText::FromString(FString::Printf(TEXT("CustomizedUV%uToolTip"), UVIndex))));
		}

		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_PixelDepthOffset, Material), MP_PixelDepthOffset, LOCTEXT("PixelDepthOffsetToolTip", "Pixel Depth Offset")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_ShadingModel, Material), MP_ShadingModel, LOCTEXT("ShadingModelToolTip", "Selects which shading model should be used per pixel")));
		MaterialInputs.Add(FMaterialInputInfo(FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(MP_FrontMaterial, Material), MP_FrontMaterial, LOCTEXT("FrontMaterialToolTip", "Specify the front facing material")));

		//^^^ New material properties go above here. ^^^^
		MaterialInputs.Add(FMaterialInputInfo(LOCTEXT("MaterialAttributes", "Material Attributes"), MP_MaterialAttributes, LOCTEXT("MaterialAttributesToolTip", "Material Attributes")));

		if (Material->IsCompiledWithExecutionFlow())
		{
			check(Material->ExpressionExecBegin);
			InitExpressionNewNode<UMaterialGraphNode>(this, Material->ExpressionExecBegin, false);
		}
		
		// Add Root Node
		{
			FGraphNodeCreator<UMaterialGraphNode_Root> NodeCreator(*this);
			RootNode = NodeCreator.CreateNode();
			RootNode->Material = Material;
			NodeCreator.Finalize();
		}
	}

	TArray<UMaterialExpression*> ChildSubGraphExpressions;

	// Composite's use reroutes under the hood that we don't want to create nodes for, gather their expressions for checking
	TArray<UMaterialExpressionReroute*> CompositeRerouteExpressions;
	if (UMaterialExpressionComposite* SubgraphParentComposite = Cast<UMaterialExpressionComposite>(SubgraphExpression))
	{
		CompositeRerouteExpressions = SubgraphParentComposite->GetCurrentReroutes();
	}

	if (const TArray<UMaterialExpression*>* Expressions = SubgraphExpressionMap.Find(SubgraphExpression))
	{
		for (UMaterialExpression* Expression : *Expressions)
		{
			if (!CompositeRerouteExpressions.Contains(Expression))
			{
				AddExpression(Expression, false);

				//@TODO: Make a better way to check if an expression represents a subgraph than by type.
				if (Cast<UMaterialExpressionComposite>(Expression))
				{
					ChildSubGraphExpressions.Add(Expression);
				}
			}
		}
	}

	if (const TArray<UMaterialExpressionComment*>* Comments = SubgraphCommentMap.Find(SubgraphExpression))
	{
		for (UMaterialExpressionComment* Comment : *Comments)
		{
			AddComment(Comment);
		}
	}

	for (UMaterialExpression* ChildSubGraphExpression : ChildSubGraphExpressions)
	{
		UMaterialGraph* Subgraph = AddSubGraph(ChildSubGraphExpression);

		if (UMaterialGraphNode_Composite* CompositeNode = Cast<UMaterialGraphNode_Composite>(ChildSubGraphExpression->GraphNode))
		{
			CompositeNode->BoundGraph = Subgraph;
			Subgraph->Rename(*CastChecked<UMaterialExpressionComposite>(CompositeNode->MaterialExpression)->SubgraphName);
		}

		Subgraph->RebuildGraphInternal(SubgraphExpressionMap, SubgraphCommentMap);
	}

	LinkGraphNodesFromMaterial();
}

UMaterialGraphNode* UMaterialGraph::AddExpression(UMaterialExpression* Expression, bool bUserInvoked)
{
	// Node for UMaterialExpressionExecBegin is explicitly placed if needed
	// We don't created any node for UMaterialExpressionExecEnd, it's handled as part of the root node
	if (Expression &&
		!Expression->IsA(UMaterialExpressionExecBegin::StaticClass()) &&
		!Expression->IsA(UMaterialExpressionExecEnd::StaticClass()))
	{
		Modify();

		if (Expression->IsA(UMaterialExpressionReroute::StaticClass()))
		{
			return InitExpressionNewNode<UMaterialGraphNode_Knot>(this, Expression, false);
		}
		else if (Expression->IsA(UMaterialExpressionComposite::StaticClass()))
		{
			return InitExpressionNewNode<UMaterialGraphNode_Composite>(this, Expression, false);
		}
		else if (Expression->IsA(UMaterialExpressionPinBase::StaticClass()))
		{
			return InitExpressionNewNode<UMaterialGraphNode_PinBase>(this, Expression, false);
		}
		else 
		{
			return InitExpressionNewNode<UMaterialGraphNode>(this, Expression, bUserInvoked);
		}
	}

	return nullptr;
}

UMaterialGraphNode_Comment* UMaterialGraph::AddComment(UMaterialExpressionComment* Comment, bool bIsUserInvoked)
{
	UMaterialGraphNode_Comment* NewComment = NULL;
	if (Comment)
	{
		Modify();
		FGraphNodeCreator<UMaterialGraphNode_Comment> NodeCreator(*this);
		if (bIsUserInvoked)
		{
			NewComment = NodeCreator.CreateUserInvokedNode(true);
		}
		else
		{
			NewComment = NodeCreator.CreateNode(false);
		}
		NewComment->MaterialExpressionComment = Comment;
		NewComment->MaterialDirtyDelegate = MaterialDirtyDelegate;
		Comment->GraphNode = NewComment;
		Comment->SubgraphExpression = SubgraphExpression;
		NodeCreator.Finalize();
	}

	return NewComment;
}

UMaterialGraph* UMaterialGraph::AddSubGraph(UMaterialExpression* InSubgraphExpression)
{
	UMaterialGraph* SubGraph = CastChecked<UMaterialGraph>(FBlueprintEditorUtils::CreateNewGraph(InSubgraphExpression->GraphNode, NAME_None, UMaterialGraph::StaticClass(), Schema));
	check(SubGraph);

	SubGraph->Material = Material;
	SubGraph->MaterialFunction = MaterialFunction;
	SubGraph->RealtimeDelegate = RealtimeDelegate;
	SubGraph->MaterialDirtyDelegate = MaterialDirtyDelegate;
	SubGraph->ToggleCollapsedDelegate = ToggleCollapsedDelegate;
	SubGraph->SubgraphExpression = InSubgraphExpression;
	SubGraphs.Add(SubGraph);

	// If we are a subgraph ourselves, mark that on the expression.
	InSubgraphExpression->SubgraphExpression = SubgraphExpression;

	return SubGraph;
}

void UMaterialGraph::LinkGraphNodesFromMaterial()
{
	struct ExpressionMatchesPredicate
	{
		ExpressionMatchesPredicate(UMaterialExpressionReroute* InCompositeReroute)
			: CompositeReroute(InCompositeReroute)
		{}

		bool operator()(const FCompositeReroute& Reroute)
		{
			return Reroute.Expression == CompositeReroute;
		}

		UMaterialExpressionReroute* CompositeReroute;
	};

	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		Nodes[Index]->BreakAllNodeLinks();
	}

	if (RootNode)
	{
		// Use Material Inputs to make GraphNode Connections
		for (int32 Index = 0; Index < MaterialInputs.Num(); ++Index)
		{
			UEdGraphPin* InputPin = RootNode->GetInputPin(Index);
			auto ExpressionInput = MaterialInputs[Index].GetExpressionInput(Material);

			if (ExpressionInput.Expression)
			{
				if (UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(ExpressionInput.Expression->GraphNode))
				{
					InputPin->MakeLinkTo(GraphNode->GetOutputPin(GetValidOutputIndex(&ExpressionInput)));
				}
				else if (UMaterialExpressionReroute* CompositeReroute = CastChecked<UMaterialExpressionReroute>(ExpressionInput.Expression))
				{
					// This is an unseen composite reroute expression, find the actual expression output to connect to.
					UMaterialExpressionComposite* OwningComposite = CastChecked<UMaterialExpressionComposite>(CompositeReroute->SubgraphExpression);

					UMaterialGraphNode* OutputGraphNode;
					int32 OutputPinIndex = OwningComposite->InputExpressions->ReroutePins.FindLastByPredicate(ExpressionMatchesPredicate(CompositeReroute));
					if (OutputPinIndex != INDEX_NONE)
					{
						OutputGraphNode = CastChecked<UMaterialGraphNode>(OwningComposite->InputExpressions->GraphNode);
					}
					else
					{
						// Output pin base in the subgraph cannot have outputs, if this reroute isn't in the inputs connect to composite's outputs
						OutputPinIndex = OwningComposite->OutputExpressions->ReroutePins.FindLastByPredicate(ExpressionMatchesPredicate(CompositeReroute));
						OutputGraphNode = CastChecked<UMaterialGraphNode>(OwningComposite->GraphNode);
					}
					InputPin->MakeLinkTo(OutputGraphNode->GetOutputPin(OutputPinIndex));
				}
			}
		}
	}

	for (int32 Index = 0; Index < Material->Expressions.Num(); Index++)
	{
		UMaterialExpression* Expression = Material->Expressions[Index];
		if (!Expression)
		{
			continue;
		}

		UMaterialGraphNode* MaterialGraphNode = Cast<UMaterialGraphNode>(Expression->GraphNode);
		if (!MaterialGraphNode)
		{
			continue;
		}

		const TArray<FExpressionInput*> ExpressionInputs = Expression->GetInputs();

		TArray<FExpressionExecOutputEntry> ExecOutputs;
		Expression->GetExecOutputs(ExecOutputs);

		for (const auto& It : MaterialGraphNode->PinInfoMap)
		{
			UEdGraphPin* Pin = It.Key;
			const FMaterialGraphPinInfo& PinInfo = It.Value;
			if (Pin->Direction == EGPD_Input && PinInfo.PinType == EMaterialGraphPinType::Data)
			{
				if (ExpressionInputs[PinInfo.Index]->Expression)
				{
					// Unclear why this is null sometimes outside of composite reroute, but this is safer than crashing
					if (UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(ExpressionInputs[PinInfo.Index]->Expression->GraphNode))
					{
						// if GraphNode is a material function call for a missing material function, it may not have any output pins
						UEdGraphPin* OutputPin = GraphNode->TryGetOutputPin(GetValidOutputIndex(ExpressionInputs[PinInfo.Index]));
						if (LIKELY(OutputPin))
						{
							Pin->MakeLinkTo(OutputPin);
						}
					}
					else if (UMaterialExpressionReroute* CompositeReroute = Cast<UMaterialExpressionReroute>(ExpressionInputs[PinInfo.Index]->Expression))
					{
						// This is an unseen composite reroute expression, find the actual expression output to connect to.
						UMaterialExpressionComposite* OwningComposite = CastChecked<UMaterialExpressionComposite>(CompositeReroute->SubgraphExpression);

						UMaterialGraphNode* OutputGraphNode;
						int32 OutputPinIndex = OwningComposite->InputExpressions->ReroutePins.FindLastByPredicate(ExpressionMatchesPredicate(CompositeReroute));
						if (OutputPinIndex != INDEX_NONE)
						{
							OutputGraphNode = CastChecked<UMaterialGraphNode>(OwningComposite->InputExpressions->GraphNode);
						}
						else
						{
							// Output pin base in the subgraph cannot have outputs, if this reroute isn't in the inputs connect to composite's outputs
							OutputPinIndex = OwningComposite->OutputExpressions->ReroutePins.FindLastByPredicate(ExpressionMatchesPredicate(CompositeReroute));
							OutputGraphNode = CastChecked<UMaterialGraphNode>(OwningComposite->GraphNode);
						}
						Pin->MakeLinkTo(OutputGraphNode->GetOutputPin(OutputPinIndex));
					}
				}
			}
			else if (Pin->Direction == EGPD_Output && PinInfo.PinType == EMaterialGraphPinType::Exec)
			{
				FExpressionExecOutput* ExecOutput = ExecOutputs[PinInfo.Index].Output;
				UMaterialExpression* ConnectedExpression = ExecOutput->GetExpression();
				if (ConnectedExpression)
				{
					if (ConnectedExpression == Material->ExpressionExecEnd)
					{
						// Exec end point is the root node
						Pin->MakeLinkTo(RootNode->GetExecInputPin());
					}
					else if (UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(ConnectedExpression->GraphNode))
					{
						Pin->MakeLinkTo(GraphNode->GetExecInputPin());
					}
					// TODO - UMaterialExpressionReroute?
				}
			}
		}
	}

	NotifyGraphChanged();
}

void UMaterialGraph::LinkMaterialExpressionsFromGraph() const
{
	// Use GraphNodes to make Material Expression Connections
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		if (RootNode && RootNode == Nodes[NodeIndex])
		{
			// Setup Material's inputs from root node
			Material->Modify();
			Material->EditorX = RootNode->NodePosX;
			Material->EditorY = RootNode->NodePosY;
			check(RootNode->InputPins.Num() == MaterialInputs.Num());
			for (int32 PinIndex = 0; PinIndex < RootNode->InputPins.Num() && PinIndex < MaterialInputs.Num(); ++PinIndex)
			{
				FExpressionInput& MaterialInput = MaterialInputs[PinIndex].GetExpressionInput(Material);

				if (RootNode->InputPins[PinIndex]->LinkedTo.Num() > 0)
				{
					UMaterialGraphNode* ConnectedNode = CastChecked<UMaterialGraphNode>(RootNode->InputPins[PinIndex]->LinkedTo[0]->GetOwningNode());
					// Work out the index of the connected pin
					for (int32 OutPinIndex = 0; OutPinIndex < ConnectedNode->OutputPins.Num(); ++OutPinIndex)
					{
						if (ConnectedNode->OutputPins[OutPinIndex] == RootNode->InputPins[PinIndex]->LinkedTo[0])
						{
							if (!ConnectedNode->MaterialExpression->IsExpressionConnected(&MaterialInput, OutPinIndex))
							{
								ConnectedNode->MaterialExpression->Modify();
								MaterialInput.Connect(OutPinIndex, ConnectedNode->MaterialExpression);
							}
							break;
						}
					}
				}
				else if (MaterialInput.Expression)
				{
					MaterialInput.Expression = NULL;
				}
			}
		}
		else
		{
			if (UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(Nodes[NodeIndex]))
			{
				// Need to be sure that we are changing the expression before calling modify -
				// triggers a rebuild of its preview when it is called
				UMaterialExpression* Expression = GraphNode->MaterialExpression;
				bool bModifiedExpression = false;
				if (Expression)
				{
					if (Expression->MaterialExpressionEditorX != GraphNode->NodePosX
						|| Expression->MaterialExpressionEditorY != GraphNode->NodePosY
						|| Expression->Desc != GraphNode->NodeComment)
					{
						bModifiedExpression = true;

						Expression->Modify();

						// Update positions and comments
						Expression->MaterialExpressionEditorX = GraphNode->NodePosX;
						Expression->MaterialExpressionEditorY = GraphNode->NodePosY;
						Expression->Desc = GraphNode->NodeComment;
					}

					const TArray<FExpressionInput*> ExpressionInputs = Expression->GetInputs();

					TArray<FExpressionExecOutputEntry> ExecOutputs;
					Expression->GetExecOutputs(ExecOutputs);

					for (const auto& It : GraphNode->PinInfoMap)
					{
						UEdGraphPin* Pin = It.Key;
						const FMaterialGraphPinInfo& PinInfo = It.Value;
						if (Pin->Direction == EGPD_Input && PinInfo.PinType == EMaterialGraphPinType::Data)
						{
							// Wire up non-execution input pins
							FExpressionInput* ExpressionInput = ExpressionInputs[PinInfo.Index];
							if (Pin->LinkedTo.Num() > 0)
							{
								UMaterialGraphNode* ConnectedNode = CastChecked<UMaterialGraphNode>(Pin->LinkedTo[0]->GetOwningNode());

								// Work out the index of the connected pin
								for (int32 OutPinIndex = 0; OutPinIndex < ConnectedNode->OutputPins.Num(); ++OutPinIndex)
								{
									if (ConnectedNode->OutputPins[OutPinIndex] == Pin->LinkedTo[0])
									{
										if (ExpressionInput && !ConnectedNode->MaterialExpression->IsExpressionConnected(ExpressionInput, OutPinIndex))
										{
											if (!bModifiedExpression)
											{
												bModifiedExpression = true;
												Expression->Modify();
											}

											ConnectedNode->MaterialExpression->Modify();
											ExpressionInput->Connect(OutPinIndex, ConnectedNode->MaterialExpression);
										}
										break;
									}
								}
							}
							else if (ExpressionInput && ExpressionInput->Expression)
							{
								if (!bModifiedExpression)
								{
									bModifiedExpression = true;
									Expression->Modify();
								}
								ExpressionInput->Expression = NULL;
							}
						}
						else if (Pin->Direction == EGPD_Output && PinInfo.PinType == EMaterialGraphPinType::Exec)
						{
							// Wire up execution output pins
							FExpressionExecOutput* ExpressionOutput = ExecOutputs[PinInfo.Index].Output;

							if (Pin->LinkedTo.Num() > 0)
							{
								if (Pin->LinkedTo[0]->GetOwningNode() == RootNode)
								{
									if (!bModifiedExpression)
									{
										bModifiedExpression = true;
										Expression->Modify();
									}
									ExpressionOutput->Connect(Material->ExpressionExecEnd);
								}
								else
								{
									UMaterialGraphNode* ConnectedNode = CastChecked<UMaterialGraphNode>(Pin->LinkedTo[0]->GetOwningNode());
									if (ExpressionOutput &&
										ExpressionOutput->GetExpression() != ConnectedNode->MaterialExpression &&
										ConnectedNode->MaterialExpression->HasExecInput())
									{
										if (!bModifiedExpression)
										{
											bModifiedExpression = true;
											Expression->Modify();
										}

										ConnectedNode->MaterialExpression->Modify();
										ExpressionOutput->Connect(ConnectedNode->MaterialExpression);
									}
								}
							}
							else if (ExpressionOutput && ExpressionOutput->GetExpression())
							{
								if (!bModifiedExpression)
								{
									bModifiedExpression = true;
									Expression->Modify();
								}
								ExpressionOutput->Connect(nullptr);
							}
						}
					}
				}
			}
			else if (UMaterialGraphNode_Comment* CommentNode = Cast<UMaterialGraphNode_Comment>(Nodes[NodeIndex]))
			{
				UMaterialExpressionComment* Comment = CommentNode->MaterialExpressionComment;
				if (Comment)
				{
					if (Comment->MaterialExpressionEditorX != CommentNode->NodePosX
						|| Comment->MaterialExpressionEditorY != CommentNode->NodePosY
						|| Comment->Text != CommentNode->NodeComment
						|| Comment->SizeX != CommentNode->NodeWidth
						|| Comment->SizeY != CommentNode->NodeHeight
						|| Comment->CommentColor != CommentNode->CommentColor)
					{
						Comment->Modify();

						// Update positions and comments
						Comment->MaterialExpressionEditorX = CommentNode->NodePosX;
						Comment->MaterialExpressionEditorY = CommentNode->NodePosY;
						Comment->Text = CommentNode->NodeComment;
						Comment->SizeX = CommentNode->NodeWidth;
						Comment->SizeY = CommentNode->NodeHeight;
						Comment->CommentColor = CommentNode->CommentColor;
					}
				}
			}
		}
	}

	// Also link subgraphs?
	for (const UEdGraph* SubGraph : SubGraphs)
	{
		CastChecked<UMaterialGraph>(SubGraph)->LinkMaterialExpressionsFromGraph();
	}
}

bool UMaterialGraph::IsInputActive(UEdGraphPin* GraphPin) const
{
	if (Material && RootNode)
	{
		int32 Index = INDEX_NONE;
		if (RootNode->InputPins.Find(GraphPin, Index))
		{
			return Material->IsPropertyActiveInEditor(MaterialInputs[Index].GetProperty());
		}
	}
	return true;
}

void UMaterialGraph::GetUnusedExpressions(TArray<UEdGraphNode*>& UnusedNodes) const
{
	UnusedNodes.Empty();

	TArray<UEdGraphNode*> NodesToCheck;

	if (RootNode)
	{
		for (int32 Index = 0; Index < RootNode->InputPins.Num(); ++Index)
		{
			if (MaterialInputs[Index].IsVisiblePin(Material)
				&& RootNode->InputPins[Index]->LinkedTo.Num() > 0 && RootNode->InputPins[Index]->LinkedTo[0])
			{
				NodesToCheck.Push(RootNode->InputPins[Index]->LinkedTo[0]->GetOwningNode());
			}
		}

		for (int32 Index = 0; Index < Nodes.Num(); Index++)
		{
			UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(Nodes[Index]);
			if (GraphNode)
			{
				UMaterialExpressionCustomOutput* CustomOutput = Cast<UMaterialExpressionCustomOutput>(GraphNode->MaterialExpression);
				if (CustomOutput)
				{
					NodesToCheck.Push(GraphNode);
				}
			}
		}
	}
	else if (MaterialFunction)
	{
		for (int32 Index = 0; Index < Nodes.Num(); Index++)
		{
			UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(Nodes[Index]);
			if (GraphNode)
			{
				UMaterialExpressionFunctionOutput* FunctionOutput = Cast<UMaterialExpressionFunctionOutput>(GraphNode->MaterialExpression);
				if (FunctionOutput)
				{
					NodesToCheck.Push(GraphNode);
				}
			}
		}
	}

	// Depth-first traverse the material expression graph.
	TArray<UEdGraphNode*> UsedNodes;
	TMap<UEdGraphNode*, int32> ReachableNodes;
	while (NodesToCheck.Num() > 0)
	{
		UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(NodesToCheck.Pop());
		if (GraphNode)
		{
			int32* AlreadyVisited = ReachableNodes.Find(GraphNode);
			if (!AlreadyVisited)
			{
				// Mark the expression as reachable.
				ReachableNodes.Add(GraphNode, 0);
				UsedNodes.Add(GraphNode);

				// Iterate over the expression's inputs and add them to the pending stack.
				for (int32 Index = 0; Index < GraphNode->InputPins.Num(); ++Index)
				{
					if (GraphNode->InputPins[Index]->LinkedTo.Num() > 0 && GraphNode->InputPins[Index]->LinkedTo[0])
					{
						NodesToCheck.Push(GraphNode->InputPins[Index]->LinkedTo[0]->GetOwningNode());
					}
				}

				// Since named reroute nodes don't have any input pins, we manually push the declaration node here
				if (const UMaterialExpressionNamedRerouteUsage* NamedRerouteUsage = Cast<UMaterialExpressionNamedRerouteUsage>(GraphNode->MaterialExpression))
				{
					if (NamedRerouteUsage->Declaration && NamedRerouteUsage->Declaration->GraphNode)
					{
						NodesToCheck.Push(NamedRerouteUsage->Declaration->GraphNode);
					}
				}
			}
		}
	}

	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(Nodes[Index]);

		if (GraphNode && !UsedNodes.Contains(GraphNode))
		{
			UnusedNodes.Add(GraphNode);
		}
	}
}

void UMaterialGraph::RemoveAllNodes()
{
	MaterialInputs.Empty();

	RootNode = NULL;

	TArray<UEdGraphNode*> NodesToRemove = Nodes;
	for (int32 NodeIndex = 0; NodeIndex < NodesToRemove.Num(); ++NodeIndex)
	{
		NodesToRemove[NodeIndex]->Modify();
		RemoveNode(NodesToRemove[NodeIndex]);
	}
}

int32 UMaterialGraph::GetValidOutputIndex(FExpressionInput* Input) const
{
	int32 OutputIndex = 0;

	if (Input->Expression)
	{
		TArray<FExpressionOutput>& Outputs = Input->Expression->GetOutputs();

		if (Outputs.Num() > 0)
		{
			const bool bOutputIndexIsValid = Outputs.IsValidIndex(Input->OutputIndex)
				// Attempt to handle legacy connections before OutputIndex was used that had a mask
				&& (Input->OutputIndex != 0 || Input->Mask == 0);

			for( ; OutputIndex < Outputs.Num() ; ++OutputIndex )
			{
				const FExpressionOutput& Output = Outputs[OutputIndex];

				if((bOutputIndexIsValid && OutputIndex == Input->OutputIndex)
					|| (!bOutputIndexIsValid
					&& Output.Mask == Input->Mask
					&& Output.MaskR == Input->MaskR
					&& Output.MaskG == Input->MaskG
					&& Output.MaskB == Input->MaskB
					&& Output.MaskA == Input->MaskA))
				{
					break;
				}
			}

			if (OutputIndex >= Outputs.Num())
			{
				// Work around for non-reproducible crash where OutputIndex would be out of bounds
				OutputIndex = Outputs.Num() - 1;
			}
		}
	}

	return OutputIndex;
}

#undef LOCTEXT_NAMESPACE
