// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operators/UVEditorUVTransformOp.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Parameterization/MeshUVPacking.h"
#include "Selections/MeshConnectedComponents.h"
#include "Utilities/MeshUDIMClassifier.h"
#include "Async/ParallelFor.h"
#include "UVEditorUXSettings.h"

using namespace UE::Geometry;

void FUVEditorUVTransformBaseOp::SetTransform(const FTransformSRT3d& Transform)
{
	ResultTransform = Transform;
}

void FUVEditorUVTransformBaseOp::RebuildBoundingBoxes()
{	
	int32 NumComponents = UVComponents->Num();
	PerComponentBoundingBoxes.SetNum(NumComponents);
	OverallBoundingBox = FAxisAlignedBox2d::Empty();

	ParallelFor(NumComponents, [&](int32 k)
		{
			FVector2d TriangleUV[3];
			PerComponentBoundingBoxes[k] = FAxisAlignedBox2d::Empty();
			const TArray<int>& Triangles = (*UVComponents)[k].Indices;
			for (int tid : Triangles)
			{
				FIndex3i TriElements = ActiveUVLayer->GetTriangle(tid);
				if (!TriElements.Contains(FDynamicMesh3::InvalidID))
				{
					for (int j = 0; j < 3; ++j)
					{
						TriangleUV[j] = FVector2d(FUVEditorUXSettings::InternalUVToExternalUV(ActiveUVLayer->GetElement(TriElements[j])));
						PerComponentBoundingBoxes[k].Contain(TriangleUV[j]);
					}
				}
			}
		});
	for (int32 Cid = 0; Cid < NumComponents; ++Cid)
	{
		OverallBoundingBox.Contain(PerComponentBoundingBoxes[Cid]);
	}
}

void FUVEditorUVTransformBaseOp::CollectTransformElements()
{
	if (Selection.IsSet())
	{
		TransformingElements = TSet<int32>();
	}

	int32 NumComponents = UVComponents->Num();

	for (int32 k = 0; k < NumComponents; ++k)
	{
		const TArray<int>& Triangles = (*UVComponents)[k].Indices;
		for (int tid : Triangles)
		{
			FIndex3i Elements = ActiveUVLayer->GetTriangle(tid);
			for (int j = 0; j < 3; ++j)
			{
				if (TransformingElements.IsSet())
				{
					TransformingElements.GetValue().Add(Elements[j]);
				}
				ElementToComponent.Add(Elements[j], k);
			}
		}
	}
}

FVector2f FUVEditorUVTransformBaseOp::GetAlignmentPointFromBoundingBoxAndDirection(EUVEditorAlignDirectionBackend Direction, const FAxisAlignedBox2d& BoundingBox)
{
	FVector2f AlignmentPoint = FVector2f();

	switch (Direction)
	{
	case EUVEditorAlignDirectionBackend::Top:
		AlignmentPoint = FVector2f(BoundingBox.Center().X, BoundingBox.Max.Y);
		break;
	case EUVEditorAlignDirectionBackend::Bottom:
		AlignmentPoint = FVector2f(BoundingBox.Center().X, BoundingBox.Min.Y);
		break;
	case EUVEditorAlignDirectionBackend::Left:
		AlignmentPoint = FVector2f(BoundingBox.Min.X, BoundingBox.Center().Y);
		break;
	case EUVEditorAlignDirectionBackend::Right:
		AlignmentPoint = FVector2f(BoundingBox.Max.X, BoundingBox.Center().Y);
		break;
	case EUVEditorAlignDirectionBackend::CenterVertically:
		AlignmentPoint = FVector2f(BoundingBox.Center().X, BoundingBox.Center().Y);
		break;
	case EUVEditorAlignDirectionBackend::CenterHorizontally:
		AlignmentPoint = FVector2f(BoundingBox.Center().X, BoundingBox.Center().Y);
		break;
	default:
		ensure(false);
	}

	return AlignmentPoint;
}

FVector2f FUVEditorUVTransformBaseOp::GetAlignmentPointFromUDIMAndDirection(EUVEditorAlignDirectionBackend Direction, FVector2i UDIMTile)
{
	FVector2f AlignmentPoint = FVector2f();

	FVector2f UDIMLowerCorner(UDIMTile);

	switch (Direction)
	{
	case EUVEditorAlignDirectionBackend::Top:
		AlignmentPoint = FVector2f(UDIMLowerCorner.X + 0.5f, UDIMLowerCorner.Y + 1.0f);
		break;
	case EUVEditorAlignDirectionBackend::Bottom:
		AlignmentPoint = FVector2f(UDIMLowerCorner.X + 0.5f, UDIMLowerCorner.Y);
		break;
	case EUVEditorAlignDirectionBackend::Left:
		AlignmentPoint = FVector2f(UDIMLowerCorner.X, UDIMLowerCorner.Y + 0.5f);
		break;
	case EUVEditorAlignDirectionBackend::Right:
		AlignmentPoint = FVector2f(UDIMLowerCorner.X + 1.0f, UDIMLowerCorner.Y + 0.5f);
		break;
	case EUVEditorAlignDirectionBackend::CenterVertically:
		AlignmentPoint = FVector2f(UDIMLowerCorner.X + 0.5f, UDIMLowerCorner.Y + 0.5f);
		break;
	case EUVEditorAlignDirectionBackend::CenterHorizontally:
		AlignmentPoint = FVector2f(UDIMLowerCorner.X + 0.5f, UDIMLowerCorner.Y + 0.5f);
		break;
	default:
		ensure(false);
	}

	return AlignmentPoint;
}

void FUVEditorUVTransformBaseOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(*OriginalMesh, true, true, true, true);
	if (!ensureMsgf(ResultMesh->HasAttributes(), TEXT("Attributes not found on mesh? Conversion should always create them, so this operator should not need to do so.")))
	{
		ResultMesh->EnableAttributes();
	}

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	int UVLayerInput = UVLayerIndex;
	ActiveUVLayer = ResultMesh->Attributes()->GetUVLayer(UVLayerInput);

	auto UVIslandPredicate = [this](int32 Triangle0, int32 Triangle1)
	{
		return ActiveUVLayer->AreTrianglesConnected(Triangle0, Triangle1);
	};

	UVComponents = MakeShared<FMeshConnectedComponents>(ResultMesh.Get());
	if (Selection.IsSet())
	{
		UVComponents->FindConnectedTriangles(Selection.GetValue().Array(), UVIslandPredicate);
	}
	else
	{
		UVComponents->FindConnectedTriangles(UVIslandPredicate);
	}

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	CollectTransformElements();

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	HandleTransformationOp(Progress);
}

FVector2f FUVEditorUVTransformOp::GetPivotFromMode(int32 ElementID)
{
	FVector2f Pivot;
	const int32* Component;

	switch (PivotMode)
	{
	case EUVEditorPivotTypeBackend::Origin:
		Pivot = FVector2f(0, 0);
		break;
	case EUVEditorPivotTypeBackend::BoundingBoxCenter:
		Pivot = FVector2f(OverallBoundingBox.Center());
		break;
	case EUVEditorPivotTypeBackend::IndividualBoundingBoxCenter:
		Component = ElementToComponent.Find(ElementID);
		if (ensure(Component != nullptr))
		{
			Pivot = FVector2f(PerComponentBoundingBoxes[*Component].Center());
		}
		break;
	case EUVEditorPivotTypeBackend::Manual:
		Pivot = FVector2f(ManualPivot);
		break;
	}
	return Pivot;
}

void FUVEditorUVTransformOp::HandleTransformationOp(FProgressCancel * Progress)
{
	auto ScaleFunc = [&](int32 Vid)
	{
		FVector2f ScalePivot = GetPivotFromMode(Vid);
		FVector2f UV = FUVEditorUXSettings::InternalUVToExternalUV(ActiveUVLayer->GetElement(Vid));
		UV = (UV - ScalePivot);
		UV[0] *= Scale[0];
		UV[1] *= Scale[1];
		UV = (UV + ScalePivot);
		ActiveUVLayer->SetElement(Vid, FUVEditorUXSettings::ExternalUVToInternalUV(UV));
	};

	auto RotFunc = [&](int32 Vid)
	{
		FVector2f RotationPivot = GetPivotFromMode(Vid);
		FVector2f UV = FUVEditorUXSettings::InternalUVToExternalUV(ActiveUVLayer->GetElement(Vid));
		FVector2f UV_Rotated;
		double RotationInRadians = Rotation / 180.0 * UE_PI;
		UV = (UV - RotationPivot);
		UV_Rotated[0] = UV[0] * FMath::Cos(RotationInRadians) - UV[1] * FMath::Sin(RotationInRadians);
		UV_Rotated[1] = UV[0] * FMath::Sin(RotationInRadians) + UV[1] * FMath::Cos(RotationInRadians);
		UV = (UV_Rotated + RotationPivot);
		ActiveUVLayer->SetElement(Vid, FUVEditorUXSettings::ExternalUVToInternalUV(UV));
	};

	auto TranslateFunc = [&](int32 Vid)
	{	
		FVector2f RotationPivot = FVector2f(0,0);
		if (TranslationMode == EUVEditorTranslationModeBackend::Absolute)
		{
			RotationPivot = GetPivotFromMode(Vid);
		}				
		FVector2f UV = FUVEditorUXSettings::InternalUVToExternalUV(ActiveUVLayer->GetElement(Vid));
		UV = (UV + FVector2f(Translation) - RotationPivot);
		ActiveUVLayer->SetElement(Vid, FUVEditorUXSettings::ExternalUVToInternalUV(UV));
	};

	auto ApplyTransformFunc = [&](TFunction<void(int32 Vid)> TransformFunc)
	{
		RebuildBoundingBoxes();

		if (TransformingElements.IsSet())
		{
			for (int ElementID : TransformingElements.GetValue())
			{
				TransformFunc(ElementID);

				if (Progress && Progress->Cancelled())
				{
					return;
				}
			}
		}
		else
		{
			for (int ElementID : ActiveUVLayer->ElementIndicesItr())
			{
				TransformFunc(ElementID);

				if (Progress && Progress->Cancelled())
				{
					return;
				}
			}
		}
	};

	if (!FMath::IsNearlyEqual(Scale.X, 1.0f) || !FMath::IsNearlyEqual(Scale.Y, 1.0f))
	{
		ApplyTransformFunc(ScaleFunc);
	}
	if (!FMath::IsNearlyZero(Rotation))
	{
		ApplyTransformFunc(RotFunc);
	}
	if (!FMath::IsNearlyZero(Translation.X) || !FMath::IsNearlyZero(Translation.Y)
		|| TranslationMode == EUVEditorTranslationModeBackend::Absolute)
	{
		ApplyTransformFunc(TranslateFunc);
	}

}


void FUVEditorUVAlignOp::HandleTransformationOp(FProgressCancel* Progress)
{
	int32 NumComponents = UVComponents->Num();
	RebuildBoundingBoxes();

	auto TranslateFunc = [&](const FVector2f& Translation, int32 Vid)
	{
		FVector2f UV = FUVEditorUXSettings::InternalUVToExternalUV(ActiveUVLayer->GetElement(Vid));
		UV = (UV + Translation);
		ActiveUVLayer->SetElement(Vid, FUVEditorUXSettings::ExternalUVToInternalUV(UV));
	};

	auto TranslationFromAlignmentPoints = [this](const FVector2f& PointTo, const FVector2f& PointFrom)
	{
		switch (AlignDirection)
		{
		case EUVEditorAlignDirectionBackend::Top:
			return FVector2f(0, PointTo.Y - PointFrom.Y);
		case EUVEditorAlignDirectionBackend::Bottom:
			return FVector2f(0, PointTo.Y - PointFrom.Y);
		case EUVEditorAlignDirectionBackend::Left:
			return FVector2f(PointTo.X - PointFrom.X, 0);
		case EUVEditorAlignDirectionBackend::Right:
			return FVector2f(PointTo.X - PointFrom.X, 0);
		case EUVEditorAlignDirectionBackend::CenterVertically:
			return FVector2f(PointTo.X - PointFrom.X, 0);
		case EUVEditorAlignDirectionBackend::CenterHorizontally:
			return FVector2f(0, PointTo.Y - PointFrom.Y);
		default:
			ensure(false);
			return FVector2f(0, 0);
		}
	};

	TArray<FVector2f> PerComponentTranslation;
	PerComponentTranslation.SetNum(NumComponents);
	for (int32 Cid = 0; Cid < NumComponents; ++Cid)
	{
		FVector2f ComponentAlignmentPoint = GetAlignmentPointFromBoundingBoxAndDirection(AlignDirection, PerComponentBoundingBoxes[Cid]);
		switch (AlignAnchor)
		{
		case EUVEditorAlignAnchorBackend::UDIMTile:
		{
			FVector2i UDIM = FDynamicMeshUDIMClassifier::ClassifyTrianglesToUDIM(ActiveUVLayer, UVComponents->Components[Cid].Indices);
			FVector2f TileAlignmentPoint = GetAlignmentPointFromUDIMAndDirection(AlignDirection, UDIM);
			PerComponentTranslation[Cid] = TranslationFromAlignmentPoints(TileAlignmentPoint, ComponentAlignmentPoint);
		}
			break;
		case EUVEditorAlignAnchorBackend::BoundingBox:
		{		
			FVector2f BoundingBoxAlignmentPoint = GetAlignmentPointFromBoundingBoxAndDirection(AlignDirection, OverallBoundingBox);
			PerComponentTranslation[Cid] = TranslationFromAlignmentPoints(BoundingBoxAlignmentPoint, ComponentAlignmentPoint);
		}
		break;
		case EUVEditorAlignAnchorBackend::Manual:
		{
			PerComponentTranslation[Cid] = TranslationFromAlignmentPoints((FVector2f)ManualAnchor, ComponentAlignmentPoint);
		}
		break;
		default:
			ensure(false);
			PerComponentTranslation[Cid] = FVector2f();
			break;
		}
	}

	if (TransformingElements.IsSet())
	{
		for (int ElementID : TransformingElements.GetValue())
		{
			TranslateFunc(PerComponentTranslation[*ElementToComponent.Find(ElementID)], ElementID);
			if (Progress && Progress->Cancelled())
			{
				return;
			}
		}
	}
	else
	{
		for (int ElementID : ActiveUVLayer->ElementIndicesItr())
		{
			TranslateFunc(PerComponentTranslation[*ElementToComponent.Find(ElementID)], ElementID);
			if (Progress && Progress->Cancelled())
			{
				return;
			}
		}
	}
	

}


void FUVEditorUVDistributeOp::HandleTransformationOp(FProgressCancel* Progress)
{
	int32 NumComponents = UVComponents->Num();
	RebuildBoundingBoxes();
	TArray<FVector2f> PerComponentTranslation;
	

	auto ComputeDistributeTranslations = [this, NumComponents](bool bVertical, EUVEditorAlignDirectionBackend EdgeDirection, float SpreadDirection, bool bEqualizeSpacing)
	{
		TArray<FVector2f> PerComponentTranslation;
		PerComponentTranslation.SetNum(NumComponents);
		float TotalDistance = 0.0f;
		for (int32 Cid = 0; Cid < NumComponents; ++Cid)
		{
			TotalDistance += bVertical ? PerComponentBoundingBoxes[Cid].Height() : PerComponentBoundingBoxes[Cid].Width();
		}
		float BoundingBoxDistance = bVertical ? OverallBoundingBox.Height() : OverallBoundingBox.Width();
		float GapSpace = (BoundingBoxDistance - TotalDistance) / (NumComponents-1);
		float PerComponentSpace = BoundingBoxDistance / (NumComponents);

		float NextPosition = 0.0f;
		FVector2f OverallAlignmentPoint = GetAlignmentPointFromBoundingBoxAndDirection(EdgeDirection, OverallBoundingBox);
		for (int32 Cid = 0; Cid < NumComponents; ++Cid)
		{
			FVector2f ComponentAlignmentPoint = GetAlignmentPointFromBoundingBoxAndDirection(EdgeDirection, PerComponentBoundingBoxes[Cid]);
			if (bVertical)
			{
				PerComponentTranslation[Cid] = FVector2f(0.0, NextPosition + (OverallAlignmentPoint.Y - ComponentAlignmentPoint.Y));
			}
			else
			{
				PerComponentTranslation[Cid] = FVector2f(NextPosition + (OverallAlignmentPoint.X - ComponentAlignmentPoint.X), 0.0);
			}
			if (bEqualizeSpacing)
			{
				float ComponentSpace = bVertical ? PerComponentBoundingBoxes[Cid].Height() : PerComponentBoundingBoxes[Cid].Width();
				NextPosition += SpreadDirection * (ComponentSpace + GapSpace);
			}
			else
			{
				NextPosition += SpreadDirection * PerComponentSpace;
			}
		}
		return PerComponentTranslation;
	};

	switch (DistributeMode)
	{
	case EUVEditorDistributeModeBackend::TopEdges:
		PerComponentTranslation = ComputeDistributeTranslations(true, EUVEditorAlignDirectionBackend::Top, -1.0f, false);
		break;
	case EUVEditorDistributeModeBackend::BottomEdges:
		PerComponentTranslation = ComputeDistributeTranslations(true, EUVEditorAlignDirectionBackend::Bottom, 1.0f, false);
		break;
	case EUVEditorDistributeModeBackend::LeftEdges:
		PerComponentTranslation = ComputeDistributeTranslations(false, EUVEditorAlignDirectionBackend::Left, 1.0f, false);
		break;
	case EUVEditorDistributeModeBackend::RightEdges:
		PerComponentTranslation = ComputeDistributeTranslations(false, EUVEditorAlignDirectionBackend::Right, -1.0f, false);
		break;
	case EUVEditorDistributeModeBackend::CentersVertically:
		PerComponentTranslation = ComputeDistributeTranslations(false, EUVEditorAlignDirectionBackend::CenterVertically, 1.0f, false);
		break;
	case EUVEditorDistributeModeBackend::CentersHorizontally:
		PerComponentTranslation = ComputeDistributeTranslations(true, EUVEditorAlignDirectionBackend::CenterHorizontally, 1.0f, false);
		break;
	case EUVEditorDistributeModeBackend::HorizontalSpace:
		PerComponentTranslation = ComputeDistributeTranslations(false, EUVEditorAlignDirectionBackend::Left, 1.0f, true);
		break;
	case EUVEditorDistributeModeBackend::VerticalSpace:
		PerComponentTranslation = ComputeDistributeTranslations(true, EUVEditorAlignDirectionBackend::Bottom, 1.0f, true);
		break;
	default:
		ensure(false);
	}

	auto TranslateFunc = [&](const FVector2f& Translation, int32 Vid)
	{
		FVector2f UV = FUVEditorUXSettings::InternalUVToExternalUV(ActiveUVLayer->GetElement(Vid));
		UV = (UV + Translation);
		ActiveUVLayer->SetElement(Vid, FUVEditorUXSettings::ExternalUVToInternalUV(UV));
	};

	if (TransformingElements.IsSet())
	{
		for (int ElementID : TransformingElements.GetValue())
		{
			TranslateFunc(PerComponentTranslation[*ElementToComponent.Find(ElementID)], ElementID);
			if (Progress && Progress->Cancelled())
			{
				return;
			}
		}
	}
	else
	{
		for (int ElementID : ActiveUVLayer->ElementIndicesItr())
		{
			TranslateFunc(PerComponentTranslation[*ElementToComponent.Find(ElementID)], ElementID);
			if (Progress && Progress->Cancelled())
			{
				return;
			}
		}
	}

}


TUniquePtr<FDynamicMeshOperator> UUVEditorUVTransformOperatorFactory::MakeNewOperator()
{
	switch (Settings->TransformType)
	{
	case EUVEditorUVTransformType::Transform:
	{
		TUniquePtr<FUVEditorUVTransformOp> Op = MakeUnique<FUVEditorUVTransformOp>();
		Op->OriginalMesh = OriginalMesh;
		Op->SetTransform(TargetTransform);
		Op->Selection = Selection;
		Op->UVLayerIndex = GetSelectedUVChannel();

		Op->Scale = Settings->Scale;
		Op->Rotation = Settings->Rotation;
		Op->Translation = Settings->Translation;

		switch (Settings->TranslationMode)
		{
		case EUVEditorTranslationMode::Relative:
			Op->TranslationMode = EUVEditorTranslationModeBackend::Relative;
			break;
		case EUVEditorTranslationMode::Absolute:
			Op->TranslationMode = EUVEditorTranslationModeBackend::Absolute;
			break;
		}

		switch (Settings->PivotMode)
		{
		case EUVEditorPivotType::Origin:
			Op->PivotMode = EUVEditorPivotTypeBackend::Origin;
			break;
		case EUVEditorPivotType::IndividualBoundingBoxCenter:
			Op->PivotMode = EUVEditorPivotTypeBackend::IndividualBoundingBoxCenter;
			break;
		case EUVEditorPivotType::BoundingBoxCenter:
			Op->PivotMode = EUVEditorPivotTypeBackend::BoundingBoxCenter;
			break;
		case EUVEditorPivotType::Manual:
			Op->PivotMode = EUVEditorPivotTypeBackend::Manual;
			break;
		}
		Op->ManualPivot = Settings->ManualPivot;


		return Op;
	}
	break;
	case EUVEditorUVTransformType::Align:
	{
		TUniquePtr<FUVEditorUVAlignOp> Op = MakeUnique<FUVEditorUVAlignOp>();
		Op->OriginalMesh = OriginalMesh;
		Op->SetTransform(TargetTransform);
		Op->Selection = Selection;
		Op->UVLayerIndex = GetSelectedUVChannel();

		switch (Settings->AlignAnchor)
		{
		//case EUVEditorAlignAnchor::FirstItem:
		//	Op->AlignAnchor = EUVEditorAlignAnchorBackend::FirstItem;
		//	break;
		case EUVEditorAlignAnchor::UDIMTile:
			Op->AlignAnchor = EUVEditorAlignAnchorBackend::UDIMTile;
			break;
		case EUVEditorAlignAnchor::BoundingBox:
			Op->AlignAnchor = EUVEditorAlignAnchorBackend::BoundingBox;
			break;
		case EUVEditorAlignAnchor::Manual:
			Op->AlignAnchor = EUVEditorAlignAnchorBackend::Manual;
			break;
		}

		switch (Settings->AlignDirection)
		{
		case EUVEditorAlignDirection::Top:
			Op->AlignDirection = EUVEditorAlignDirectionBackend::Top;
			break;
		case EUVEditorAlignDirection::Bottom:
			Op->AlignDirection = EUVEditorAlignDirectionBackend::Bottom;
			break;
		case EUVEditorAlignDirection::Left:
			Op->AlignDirection = EUVEditorAlignDirectionBackend::Left;
			break;
		case EUVEditorAlignDirection::Right:
			Op->AlignDirection = EUVEditorAlignDirectionBackend::Right;
			break;
		case EUVEditorAlignDirection::CenterVertically:
			Op->AlignDirection = EUVEditorAlignDirectionBackend::CenterVertically;
			break;
		case EUVEditorAlignDirection::CenterHorizontally:
			Op->AlignDirection = EUVEditorAlignDirectionBackend::CenterHorizontally;
			break;
		}

		Op->ManualAnchor = Settings->ManualAnchor;

		return Op;
	}
	break;
	case EUVEditorUVTransformType::Distribute:
	{
		TUniquePtr<FUVEditorUVDistributeOp> Op = MakeUnique<FUVEditorUVDistributeOp>();
		Op->OriginalMesh = OriginalMesh;
		Op->SetTransform(TargetTransform);
		Op->Selection = Selection;
		Op->UVLayerIndex = GetSelectedUVChannel();

		switch (Settings->DistributeMode)
		{
		case EUVEditorDistributeMode::LeftEdges:
			Op->DistributeMode = EUVEditorDistributeModeBackend::LeftEdges;
			break;
		case EUVEditorDistributeMode::RightEdges:
			Op->DistributeMode = EUVEditorDistributeModeBackend::RightEdges;
			break;
		case EUVEditorDistributeMode::TopEdges:
			Op->DistributeMode = EUVEditorDistributeModeBackend::TopEdges;
			break;
		case EUVEditorDistributeMode::BottomEdges:
			Op->DistributeMode = EUVEditorDistributeModeBackend::BottomEdges;
			break;
		case EUVEditorDistributeMode::CentersVertically:
			Op->DistributeMode = EUVEditorDistributeModeBackend::CentersVertically;
			break;
		case EUVEditorDistributeMode::CentersHorizontally:
			Op->DistributeMode = EUVEditorDistributeModeBackend::CentersHorizontally;
			break;
		case EUVEditorDistributeMode::VerticalSpace:
			Op->DistributeMode = EUVEditorDistributeModeBackend::VerticalSpace;
			break;
		case EUVEditorDistributeMode::HorizontalSpace:
			Op->DistributeMode = EUVEditorDistributeModeBackend::HorizontalSpace;
			break;
		}

		return Op;
	}
	break;
	default:
		return nullptr;
	}
}
