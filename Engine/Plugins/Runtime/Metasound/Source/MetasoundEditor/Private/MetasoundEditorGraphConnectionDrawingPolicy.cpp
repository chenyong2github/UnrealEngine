// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphConnectionDrawingPolicy.h"

#include "Analysis/MetasoundFrontendVertexAnalyzerEnvelopeFollower.h"
#include "Analysis/MetasoundFrontendVertexAnalyzerTriggerDensity.h"
#include "Components/AudioComponent.h"
#include "Editor.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorSettings.h"
#include "Misc/App.h"
#include "Templates/Function.h"


#define METASOUND_DRAW_CONNECTION_DEBUG_DATA 0

namespace Metasound
{
	namespace Editor
	{
		namespace DrawingPolicyPrivate
		{
#if METASOUND_DRAW_CONNECTION_DEBUG_DATA
			// Draw the bounding box for debugging
			void DrawDebugConnection(FSlateWindowElementList& InDrawElementsList, uint32 InLayerId, const FDrawConnectionData& InDrawConnectionData)
			{
				const FVector2D& TL = InDrawConnectionData.Bounds.Min;
				const FVector2D& BR = InDrawConnectionData.Bounds.Max;
				const FVector2D TR = FVector2D(InDrawConnectionData.Bounds.Max.X, InDrawConnectionData.Bounds.Min.Y);
				const FVector2D BL = FVector2D(InDrawConnectionData.Bounds.Min.X, InDrawConnectionData.Bounds.Max.Y);

				auto DrawSpaceLine = [&](const FVector2D& Point1, const FVector2D& Point2, const FLinearColor& InColor)
				{
					const FVector2D FakeTangent = (Point2 - Point1).GetSafeNormal();
					FSlateDrawElement::MakeDrawSpaceSpline(InDrawElementsList, InLayerId, Point1, FakeTangent, Point2, FakeTangent, 1.0f, ESlateDrawEffect::None);
				};

				const FLinearColor BoundsWireColor = InDrawConnectionData.bCloseToSpline ? FLinearColor::Green : FLinearColor::White;
				DrawSpaceLine(TL, TR, BoundsWireColor);
				DrawSpaceLine(TR, BR, BoundsWireColor);
				DrawSpaceLine(BR, BL, BoundsWireColor);
				DrawSpaceLine(BL, TL, BoundsWireColor);
			}
#endif // METASOUND_DRAW_CONNECTION_DEBUG_DATA

			template<typename TAnalyzerType>
			bool DetermineWiringStyle_Envelope(TSharedPtr<FEditor> MetasoundEditor, UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& OutParams)
			{
				using namespace Frontend;

				OutParams.bDrawBubbles = false;

				if (MetasoundEditor.IsValid())
				{
					FConstOutputHandle OutputHandle = FGraphBuilder::GetConstOutputHandleFromPin(OutputPin);
					const FGuid NodeId = OutputHandle->GetOwningNodeID();
					FName OutputName = OutputHandle->GetName();

					OutParams.bDrawBubbles = MetasoundEditor->GetConnectionManager().IsTracked(NodeId, OutputName, TAnalyzerType::GetAnalyzerName());
					if (OutParams.bDrawBubbles)
					{
						float WindowValue = MetasoundEditor->GetConnectionManager().UpdateValueWindow<TAnalyzerType>(NodeId, OutputName);
						OutParams.WireThickness = FMath::Clamp(WindowValue, 0.0f, 1.0f);
						FLinearColor HSV = OutParams.WireColor.LinearRGBToHSV();
						HSV.G = FMath::Lerp(HSV.G, 1.0f, WindowValue);
						OutParams.WireColor = HSV.HSVToLinearRGB();
					}

					return true;
				}

				return false;
			}

			template<typename TNumericType>
			void DrawConnectionSpline_Numeric(FGraphConnectionDrawingPolicy& InDrawingPolicy, int32 InLayerId, const FDrawConnectionData& InData, TNumericType InDefaultValue)
			{
				if (!InData.OutputPin)
				{
					return;
				}

				const UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(InData.OutputPin->GetOwningNode());
				if (!ensureMsgf(Node, TEXT("Expected MetaSound pin to be member of MetaSound node")))
				{
					return;
				}

				const UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(Node->GetGraph());
				if (!ensureMsgf(Graph, TEXT("Expected MetaSound node to be member of MetaSound graph")))
				{
					return;
				}

				const TSharedPtr<const FEditor> Editor = static_cast<const FGraphConnectionDrawingPolicy&>(InDrawingPolicy).GetEditor();
				if (!Editor)
				{
					return;
				}

				const FGuid NodeID = Node->GetNodeID();
				TNumericType Value = InDefaultValue;
				FName OutputName = InData.OutputHandle->GetName();
				
				Editor->GetConnectionManager().GetValue(NodeID, OutputName, Value);

				FLinearColor InnerColor = Frontend::DisplayStyle::EdgeAnimation::DefaultColor;

				if (!InData.EdgeStyle || InData.EdgeStyle->LiteralColorPairs.IsEmpty())
				{
					InDrawingPolicy.DrawConnectionSpline(InLayerId, InData);
					return;
				}

				for (int32 i = 0; i < InData.EdgeStyle->LiteralColorPairs.Num() - 1; ++i)
				{
					const FMetasoundFrontendEdgeStyleLiteralColorPair& PairA = InData.EdgeStyle->LiteralColorPairs[i];

					float ValueFloat = static_cast<float>(Value);
					TNumericType ValueA = InDefaultValue;
					if (PairA.Value.TryGet(ValueA))
					{
						const float ValueAFloat = static_cast<float>(ValueA);
						if (ValueFloat > ValueAFloat)
						{
							const FMetasoundFrontendEdgeStyleLiteralColorPair& PairB = InData.EdgeStyle->LiteralColorPairs[i + 1];
							TNumericType ValueB = InDefaultValue;
							if (PairB.Value.TryGet(ValueB))
							{
								float Denom = static_cast<float>(ValueB) - ValueAFloat;
								if (FMath::IsNearlyZero(Denom))
								{
									Denom = SMALL_NUMBER;
								}
								const float Alpha = (ValueFloat - ValueAFloat) / Denom;
								InnerColor = PairA.Color + FMath::Clamp(Alpha, 0.0f, 1.0f) * (PairB.Color - PairA.Color);
							}
							break;
						}
					}

					InnerColor = PairA.Color;
				}

				// Draw the outer spline, which uses standardized color syntax
				InDrawingPolicy.DrawSpline(InData);

				// Draw the inner spline, which uses interpolated custom color syntax.
				FDrawConnectionData InnerSplineData = InData;
				InnerSplineData.Params.WireThickness *= 0.6666f;
				InnerSplineData.Params.WireColor = InnerColor;
				InDrawingPolicy.DrawAnimatedSpline(InnerSplineData);
			}
		} // namespace DrawingPolicyPrivate


		FDrawConnectionData::FDrawConnectionData(const FVector2D& InStart, const FVector2D& InEnd, const FVector2D& InSplineTangent, const FConnectionParams& InParams, const UGraphEditorSettings& InSettings, const FVector2D& InMousePosition)
			: P0(InStart)
			, P0Tangent(InParams.StartDirection == EGPD_Output ? InSplineTangent : -InSplineTangent)
			, P1(InEnd)
			, P1Tangent(InParams.EndDirection == EGPD_Input ? InSplineTangent : -InSplineTangent)
			, Params(InParams)
			, OutputHandle(Frontend::IOutputController::GetInvalidHandle())
		{
			if (Params.AssociatedPin1)
			{
				OutputPin = Params.AssociatedPin1->Direction == EGPD_Output ? Params.AssociatedPin1 : Params.AssociatedPin2;
				OutputHandle = FGraphBuilder::GetConstOutputHandleFromPin(OutputPin);
			}

			// The curve will include the endpoints but can extend out of a tight bounds because of the tangents
			// P0Tangent coefficient maximizes to 4/27 at a=1/3, and P1Tangent minimizes to -4/27 at a=2/3.
			constexpr float MaximumTangentContribution = 4.0f / 27.0f;
			Bounds = FBox2D(ForceInit);

			Bounds += FVector2D(P0);
			Bounds += FVector2D(P0 + MaximumTangentContribution * P0Tangent);
			Bounds += FVector2D(P1);
			Bounds += FVector2D(P1 - MaximumTangentContribution * P1Tangent);

			if (InSettings.bTreatSplinesLikePins)
			{
				QueryDistanceTriggerThresholdSquared = FMath::Square(InSettings.SplineHoverTolerance + Params.WireThickness * 0.5f);
				QueryDistanceForCloseSquared = FMath::Square(FMath::Sqrt(QueryDistanceTriggerThresholdSquared) + InSettings.SplineCloseTolerance);
				bCloseToSpline = Bounds.ComputeSquaredDistanceToPoint(InMousePosition) < QueryDistanceForCloseSquared;
			}

			ClosestPoint = FVector2D(ForceInit);
			if (InSettings.bTreatSplinesLikePins)
			{
				if (bCloseToSpline)
				{
					const int32 NumStepsToTest = 16;
					const float StepInterval = 1.0f / (float)NumStepsToTest;
					FVector2D Point1 = FMath::CubicInterp(P0, P0Tangent, P1, P1Tangent, 0.0f);
					for (float TestAlpha = 0.0f; TestAlpha < 1.0f; TestAlpha += StepInterval)
					{
						const FVector2D Point2 = FMath::CubicInterp(P0, P0Tangent, P1, P1Tangent, TestAlpha + StepInterval);

						const FVector2D ClosestPointToSegment = FMath::ClosestPointOnSegment2D(InMousePosition, Point1, Point2);
						const float DistanceSquared = (InMousePosition - ClosestPointToSegment).SizeSquared();

						if (DistanceSquared < ClosestDistanceSquared)
						{
							ClosestDistanceSquared = DistanceSquared;
							ClosestPoint = ClosestPointToSegment;
						}

						Point1 = Point2;
					}
				}
			}

			EdgeStyle = FGraphBuilder::GetOutputEdgeStyle(OutputHandle);
		}

		void FDrawConnectionData::UpdateSplineOverlap(FGraphSplineOverlapResult& OutResult) const
		{
			if (!bCloseToSpline)
			{
				return;
			}

			if (ClosestDistanceSquared < QueryDistanceTriggerThresholdSquared)
			{
				if (ClosestDistanceSquared < OutResult.GetDistanceSquared())
				{
					const float SquaredDistToPin1 = Params.AssociatedPin1 ? (P0 - ClosestPoint).SizeSquared() : TNumericLimits<float>::Max();
					const float SquaredDistToPin2 = Params.AssociatedPin2 ? (P1 - ClosestPoint).SizeSquared() : TNumericLimits<float>::Max();

					OutResult = FGraphSplineOverlapResult(Params.AssociatedPin1, Params.AssociatedPin2, ClosestDistanceSquared, SquaredDistToPin1, SquaredDistToPin2, bCloseToSpline);
				}
			}
			else if (ClosestDistanceSquared < QueryDistanceForCloseSquared)
			{
				OutResult.SetCloseToSpline(bCloseToSpline);
			}
		}

		FConnectionDrawingPolicy* FGraphConnectionDrawingPolicyFactory::CreateConnectionPolicy(
			const UEdGraphSchema* Schema,
			int32 InBackLayerID,
			int32 InFrontLayerID,
			float ZoomFactor,
			const FSlateRect& InClippingRect,
			FSlateWindowElementList& InDrawElements,
			UEdGraph* InGraphObj) const
		{
			if (Schema->IsA(UMetasoundEditorGraphSchema::StaticClass()))
			{
				return new FGraphConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj);
			}

			return nullptr;
		}

		FGraphConnectionDrawingPolicy::FGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
			: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
			, GraphObj(InGraphObj)
		{
			WireAnimationLayerID = (InFrontLayerID + InBackLayerID) / 2;
			ActiveWireThickness = Settings->TraceAttackWireThickness;
			InactiveWireThickness = Settings->TraceReleaseWireThickness;

			// Numeric wires are inflated up to account for interior animation
			if (const UMetasoundEditorSettings* MetasoundSettings = GetDefault<UMetasoundEditorSettings>())
			{
				bAnimateConnections = MetasoundSettings->bAnimateConnections;
				ActiveAnalyzerWireThickness = MetasoundSettings->ActiveAnalyzerWireThickness;
				ActiveAnalyzerWireSignalScalarMin = MetasoundSettings->ActiveAnalyzerWireSignalScalarMin;
				ActiveAnalyzerWireSignalScalarMax = MetasoundSettings->ActiveAnalyzerWireSignalScalarMax;
			}

			if (GraphObj)
			{
				MetasoundEditor = FGraphBuilder::GetEditorForGraph(*GraphObj);
			}
		}

		void FGraphConnectionDrawingPolicy::ScaleEnvelopeWireThickness(FConnectionParams& OutParams) const
		{
			if (OutParams.bDrawBubbles)
			{
				OutParams.WireThickness = FMath::Lerp(InactiveWireThickness * ActiveAnalyzerWireSignalScalarMin, ActiveAnalyzerWireThickness * ActiveAnalyzerWireSignalScalarMax, OutParams.WireThickness);
			}
			else
			{
				OutParams.WireThickness = InactiveWireThickness;
			}
		}

		const TSharedPtr<const FEditor> FGraphConnectionDrawingPolicy::GetEditor() const
		{
			return MetasoundEditor.Pin();
		}

		TSharedPtr<FEditor> FGraphConnectionDrawingPolicy::GetEditor()
		{
			return MetasoundEditor.Pin();
		}

		void FGraphConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* InPinA, UEdGraphPin* InPinB, FConnectionParams& OutParams)
		{
			using namespace Frontend;

			TSharedPtr<FEditor> EditorPtr = MetasoundEditor.Pin();
			if (!EditorPtr)
			{
				FConnectionDrawingPolicy::DetermineWiringStyle(InPinA, InPinB, OutParams);
				return;
			}

			UEdGraphPin* InputPin = nullptr;
			UEdGraphPin* OutputPin = nullptr;
			if (InPinA && InPinA->Direction == EGPD_Input)
			{
				InputPin = InPinA;
			}
			else if (InPinB && InPinB->Direction == EGPD_Input)
			{
				InputPin = InPinB;
			}

			if (InPinA && InPinA->Direction == EGPD_Output)
			{
				OutputPin = InPinA;
			}
			else if (InPinB && InPinB->Direction == EGPD_Output)
			{
				OutputPin = InPinB;
			}

			OutParams.AssociatedPin1 = InputPin;
			OutParams.AssociatedPin2 = OutputPin;

			const UEdGraphPin* AnyPin = InputPin ? InputPin : OutputPin;

			const bool bInputOrphaned = InputPin && InputPin->bOrphanedPin;
			const bool bOutputOrphaned = OutputPin && OutputPin->bOrphanedPin;
			if (bInputOrphaned || bOutputOrphaned)
			{
				OutParams.WireColor = FLinearColor::Red;
			}
			else if (AnyPin)
			{
				OutParams.WireColor = FGraphBuilder::GetPinCategoryColor(AnyPin->PinType);
			}

			if (AnyPin && bAnimateConnections)
			{
				if (AnyPin->PinType.PinCategory == FGraphBuilder::PinCategoryTrigger)
				{
					DrawingPolicyPrivate::DetermineWiringStyle_Envelope<FVertexAnalyzerTriggerDensity>(EditorPtr, OutputPin, InputPin, OutParams);
					ScaleEnvelopeWireThickness(OutParams);
				}
				else if (AnyPin->PinType.PinCategory == FGraphBuilder::PinCategoryAudio)
				{
					DrawingPolicyPrivate::DetermineWiringStyle_Envelope<FVertexAnalyzerEnvelopeFollower>(EditorPtr, OutputPin, InputPin, OutParams);
					ScaleEnvelopeWireThickness(OutParams);
				}
				else if (FGraphBuilder::CanInspectPin(AnyPin))
				{
					bool bEdgeStyleValid = false;
					if (const FMetasoundFrontendEdgeStyle* Style = FGraphBuilder::GetOutputEdgeStyle(OutputPin))
					{
						bEdgeStyleValid = !Style->LiteralColorPairs.IsEmpty();
					}

					if (bEdgeStyleValid && EditorPtr->IsPlaying())
					{
						OutParams.WireThickness = ActiveAnalyzerWireThickness;
					}
					else
					{
						OutParams.WireThickness = InactiveWireThickness;
					}
				}
			}
			else
			{
				OutParams.WireThickness = InactiveWireThickness;
			}
		}

		void FGraphConnectionDrawingPolicy::DrawConnection(int32 LayerId, const FVector2D& Start, const FVector2D& End, const FConnectionParams& Params)
		{
			using namespace Frontend;

			TSharedPtr<FEditor> EditorPtr = MetasoundEditor.Pin();
			if (!EditorPtr)
			{
				FConnectionDrawingPolicy::DrawConnection(LayerId, Start, End, Params);
				return;
			}

			if (!GraphObj || !EditorPtr->IsPlaying() || !bAnimateConnections)
			{
				FConnectionDrawingPolicy::DrawConnection(LayerId, Start, End, Params);
				return;
			}

			const FDrawConnectionData ConnectionData(Start, End, FGraphConnectionDrawingPolicy::ComputeSplineTangent(Start, End), Params, *Settings, LocalMousePosition);
			if (Settings->bTreatSplinesLikePins)
			{
#if METASOUND_DRAW_CONNECTION_DEBUG_DATA
				DrawingPolicyPrivate::DrawDebugConnection(DrawElementsList, ArrowLayerID, ConnectionData);
#endif // METASOUND_DRAW_CONNECTION_DEBUG_DATA

				ConnectionData.UpdateSplineOverlap(SplineOverlapResult);
			}

			if (ConnectionData.OutputHandle->IsValid() && MetasoundEditor.IsValid())
			{
				if (ConnectionData.OutputHandle->GetDataType() == GetMetasoundDataTypeName<float>())
				{
					DrawingPolicyPrivate::DrawConnectionSpline_Numeric<float>(*this, LayerId, ConnectionData, 0.0f);
				}
				else if (ConnectionData.OutputHandle->GetDataType() == GetMetasoundDataTypeName<int32>())
				{
					DrawingPolicyPrivate::DrawConnectionSpline_Numeric<int32>(*this, LayerId, ConnectionData, 0);
				}
				else if (ConnectionData.OutputHandle->GetDataType() == GetMetasoundDataTypeName<bool>())
				{
					DrawingPolicyPrivate::DrawConnectionSpline_Numeric<bool>(*this, LayerId, ConnectionData, 0);
				}
				else
				{
					DrawConnectionSpline(LayerId, ConnectionData);
				}
			}
			else
			{
				DrawConnectionSpline(LayerId, ConnectionData);
			}

			if (Params.bDrawBubbles || MidpointImage)
			{
				// Maps distance along curve to alpha
				FInterpCurve<float> SplineReparamTable;
				const float SplineLength = MakeSplineReparamTable(ConnectionData.P0, ConnectionData.P0Tangent, ConnectionData.P1, ConnectionData.P1Tangent, SplineReparamTable);

				// Draw bubbles on the spline
				if (Params.bDrawBubbles)
				{
					const float AppTime = FPlatformTime::Seconds() - GStartTime;
					FDrawConnectionSignalData SignalParams = { AppTime, LayerId, SplineReparamTable, SplineLength, ConnectionData };
					if (SignalParams.ConnectionData.OutputHandle->GetDataType() == GetMetasoundDataTypeName<FAudioBuffer>())
					{
						SignalParams.SpacingFactor = 2.f;
						SignalParams.SpeedFactor = 256.f;
						const FName AnalyzerName = Frontend::FVertexAnalyzerEnvelopeFollower::GetAnalyzerName();
						DrawConnectionSignal_Envelope(SignalParams, AnalyzerName);
					}
					else if (SignalParams.ConnectionData.OutputHandle->GetDataType() == GetMetasoundDataTypeName<FTrigger>())
					{
						SignalParams.SpacingFactor = 2.f;
						SignalParams.SpeedFactor = 64.f;
						const FName AnalyzerName = Frontend::FVertexAnalyzerTriggerDensity::GetAnalyzerName();
						DrawConnectionSignal_Envelope(SignalParams, AnalyzerName);
					}
					else
					{
						DrawConnectionSignal(SignalParams);
					}
				}

				if (MidpointImage)
				{
					DrawConnection_MidpointImage(LayerId, SplineReparamTable, SplineLength, ConnectionData);
				}
			}
		}

		void FGraphConnectionDrawingPolicy::DrawConnection_MidpointImage(int32 InLayerId, const FInterpCurve<float>& InSplineReparamTable, float InSplineLength, const FDrawConnectionData& InData)
		{
			check(MidpointImage);

			// Determine the spline position for the midpoint
			const float MidpointAlpha = InSplineReparamTable.Eval(InSplineLength * 0.5f, 0.f);
			const FVector2D Midpoint = FMath::CubicInterp(InData.P0, InData.P0Tangent, InData.P1, InData.P1Tangent, MidpointAlpha);

			// Approximate the slope at the midpoint (to orient the midpoint image to the spline)
			const FVector2D MidpointPlusE = FMath::CubicInterp(InData.P0, InData.P0Tangent, InData.P1, InData.P1Tangent, MidpointAlpha + KINDA_SMALL_NUMBER);
			const FVector2D MidpointMinusE = FMath::CubicInterp(InData.P0, InData.P0Tangent, InData.P1, InData.P1Tangent, MidpointAlpha - KINDA_SMALL_NUMBER);
			const FVector2D SlopeUnnormalized = MidpointPlusE - MidpointMinusE;

			// Draw the arrow
			const FVector2D MidpointDrawPos = Midpoint - MidpointRadius;
			const float AngleInRadians = SlopeUnnormalized.IsNearlyZero() ? 0.0f : FMath::Atan2(SlopeUnnormalized.Y, SlopeUnnormalized.X);

			FSlateDrawElement::MakeRotatedBox(
				DrawElementsList,
				InLayerId,
				FPaintGeometry(MidpointDrawPos, MidpointImage->ImageSize * ZoomFactor, ZoomFactor),
				MidpointImage,
				ESlateDrawEffect::None,
				AngleInRadians,
				TOptional<FVector2D>(),
				FSlateDrawElement::RelativeToElement,
				InData.Params.WireColor
			);
		}

		void FGraphConnectionDrawingPolicy::DrawConnectionSpline(int32 InLayerId, const FDrawConnectionData& InData)
		{
			FSlateDrawElement::MakeDrawSpaceSpline(
				DrawElementsList,
				InLayerId,
				InData.P0, InData.P0Tangent,
				InData.P1, InData.P1Tangent,
				InData.Params.WireThickness,
				ESlateDrawEffect::None,
				InData.Params.WireColor
			);
		}

		void FGraphConnectionDrawingPolicy::DrawConnectionSignal(const FDrawConnectionSignalData& InParams)
		{
			const float BubbleSpacing = InParams.SpacingFactor * ZoomFactor;
			float BubbleSpeed = InParams.SpeedFactor * ZoomFactor;

			const FVector2D BubbleSize = BubbleImage->ImageSize * ZoomFactor * 0.2f * InParams.ConnectionData.Params.WireThickness;

			const float BubbleOffset = FMath::Fmod(InParams.AppTime * BubbleSpeed, BubbleSpacing);
			const int32 NumBubbles = FMath::CeilToInt(InParams.SplineLength / BubbleSpacing);

			for (int32 i = 0; i < NumBubbles; ++i)
			{
				const float Distance = (i * BubbleSpacing) + BubbleOffset;
				if (Distance < InParams.SplineLength)
				{
					const float Alpha = InParams.SplineReparamTable.Eval(Distance, 0.f);
					FVector2D BubblePos = FMath::CubicInterp(InParams.ConnectionData.P0, InParams.ConnectionData.P0Tangent, InParams.ConnectionData.P1, InParams.ConnectionData.P1Tangent, Alpha);
					BubblePos -= BubbleSize * 0.5f;

					FSlateDrawElement::MakeBox(
						DrawElementsList,
						InParams.LayerId,
						FPaintGeometry(BubblePos, BubbleSize, ZoomFactor),
						BubbleImage,
						ESlateDrawEffect::None,
						InParams.ConnectionData.Params.WireColor
					);
				}
			}
		}

		void FGraphConnectionDrawingPolicy::DrawConnectionSignal_Envelope(const FDrawConnectionSignalData& InParams, FName InAnalyzerName)
		{
			using namespace Frontend;

			TSharedPtr<FEditor> Editor = GetEditor();
			if (!Editor)
			{
				return;
			}

			const float BubbleSpacing = InParams.SpacingFactor * ZoomFactor;
			float BubbleSpeed = InParams.SpeedFactor * ZoomFactor;
			const FVector2D BubbleSize = BubbleImage->ImageSize * ZoomFactor * 0.2f * InParams.ConnectionData.Params.WireThickness;
			const float BubbleOffset = FMath::Fmod(InParams.AppTime * BubbleSpeed, BubbleSpacing);
			const int32 NumBubbles = FMath::CeilToInt(InParams.SplineLength / BubbleSpacing);

			TArray<FVector2D> ScaledBubbles;
			ScaledBubbles.Init(BubbleSize, NumBubbles);
			if (const UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(InParams.ConnectionData.OutputPin->GetOwningNode()))
			{
				if (const UEdGraph* Graph = Node->GetGraph())
				{
					if (MetasoundEditor.IsValid())
					{
						FConstOutputHandle OutputHandle = FGraphBuilder::GetConstOutputHandleFromPin(InParams.ConnectionData.OutputPin);
						const FGuid NodeId = OutputHandle->GetOwningNodeID();
						FName OutputName = OutputHandle->GetName();

						TArray<float> Values;
						{
							// Only set to max as multiple edges can exist and each may have a different window size.
							FGraphConnectionManager& ConnectionManager = Editor->GetConnectionManager();
							if (const FFloatMovingWindow* Window = ConnectionManager.GetValueWindow(NodeId, OutputName, InAnalyzerName))
							{
								if (NumBubbles > Window->Num())
								{
									ConnectionManager.TrackValue(NodeId, OutputName, InAnalyzerName, NumBubbles);
								}
								Values = Window->ToArray();
								Values.SetNum(NumBubbles);
								for (int32 i = 0; i < Values.Num(); ++i)
								{
									const float ClampedScalar = FMath::Clamp(Values[i], 0.0f, 1.0f) * ActiveAnalyzerWireSignalScalarMax;
									ScaledBubbles[i].X *= ClampedScalar;
									ScaledBubbles[i].Y *= ClampedScalar;
								}
							}
							else
							{
								ConnectionManager.TrackValue(NodeId, OutputName, InAnalyzerName, NumBubbles);
							}
						}
					}
				}
			}

			for (int32 i = 0; i < ScaledBubbles.Num(); ++i)
			{
				const float Distance = (i * BubbleSpacing) + BubbleOffset;
				if (Distance < InParams.SplineLength)
				{
					const float Alpha = InParams.SplineReparamTable.Eval(Distance, 0.f);
					FVector2D BubblePos = FMath::CubicInterp(InParams.ConnectionData.P0, InParams.ConnectionData.P0Tangent, InParams.ConnectionData.P1, InParams.ConnectionData.P1Tangent, Alpha);
					BubblePos -= (ScaledBubbles[i] * 0.5f);

					FSlateDrawElement::MakeBox(
						DrawElementsList,
						InParams.LayerId,
						FPaintGeometry(BubblePos, ScaledBubbles[i], ZoomFactor),
						BubbleImage,
						ESlateDrawEffect::None,
						InParams.ConnectionData.Params.WireColor
					);
				}
			}
		}

		void FGraphConnectionDrawingPolicy::DrawSpline(const FDrawConnectionData& InData)
		{
			FSlateDrawElement::MakeDrawSpaceSpline(
				DrawElementsList,
				WireLayerID,
				InData.P0, InData.P0Tangent,
				InData.P1, InData.P1Tangent,
				InData.Params.WireThickness,
				ESlateDrawEffect::None,
				InData.Params.WireColor
			);
		}

		void FGraphConnectionDrawingPolicy::DrawAnimatedSpline(const FDrawConnectionData& InData)
		{
			FSlateDrawElement::MakeDrawSpaceSpline(
				DrawElementsList,
				WireAnimationLayerID,
				InData.P0, InData.P0Tangent,
				InData.P1, InData.P1Tangent,
				InData.Params.WireThickness,
				ESlateDrawEffect::None,
				InData.Params.WireColor
			);
		}
	} // namespace Editor
} // namespace Metasound
