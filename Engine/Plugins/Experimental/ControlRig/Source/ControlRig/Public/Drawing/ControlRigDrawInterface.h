// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Math/Simulation/CRSimPointContainer.h"
#include "ControlRigDrawInterface.generated.h"

UENUM()
namespace EControlRigDrawHierarchyMode
{
	enum Type
	{
		/** Draw as axes */
		Axes,

		/** MAX - invalid */
		Max UMETA(Hidden),
	};
}

struct CONTROLRIG_API FControlRigDrawInterface
{
public:

	FControlRigDrawInterface() {}
	virtual ~FControlRigDrawInterface() {}


	void DrawPoint(const FTransform& WorldOffset, const FVector& Position, float Size, const FLinearColor& Color);
	void DrawPoints(const FTransform& WorldOffset, const TArrayView<FVector>& Points, float Size, const FLinearColor& Color);
	void DrawLine(const FTransform& WorldOffset, const FVector& LineStart, const FVector& LineEnd, const FLinearColor& Color, float Thickness = 0.f);
	void DrawLines(const FTransform& WorldOffset, const TArrayView<FVector>& Positions, const FLinearColor& Color, float Thickness = 0.f);
	void DrawLineStrip(const FTransform& WorldOffset, const TArrayView<FVector>& Positions, const FLinearColor& Color, float Thickness = 0.f);
	void DrawBox(const FTransform& WorldOffset, const FTransform& Transform, const FLinearColor& Color, float Thickness = 0.f);
	void DrawAxes(const FTransform& WorldOffset, const FTransform& Transform, float Size, float Thickness = 0.f);
	void DrawRectangle(const FTransform& WorldOffset, const FTransform& Transform, float Size, const FLinearColor& Color, float Thickness);
	void DrawArc(const FTransform& WorldOffset, const FTransform& Transform, float Radius, float MinimumAngle, float MaximumAngle, const FLinearColor& Color, float Thickness, int32 Detail);
	void DrawBezier(const FTransform& WorldOffset, const FCRFourPointBezier& InBezier, float MinimumU, float MaximumU, const FLinearColor& Color, float Thickness, int32 Detail);
	void DrawHierarchy(const FTransform& WorldOffset, const FRigBoneHierarchy& Hierarchy, EControlRigDrawHierarchyMode::Type Mode, float Scale, const FLinearColor& Color, float Thickness);
	void DrawPointSimulation(const FTransform& WorldOffset, const FCRSimPointContainer& Simulation, const FLinearColor& Color, float Thickness, float PrimitiveSize = 0.f, bool bDrawPointsAsSphere = false);

private:

	enum EDrawType
	{
		EDrawType_Point,
		EDrawType_Lines,
		EDrawType_LineStrip
	};

	struct FDrawIntruction
	{
		EDrawType DrawType;
		TArray<FVector> Positions;
		FLinearColor Color;
		float Thickness;

		FDrawIntruction()
			: DrawType(EDrawType_Lines)
			, Color(FLinearColor::Red)
			, Thickness(0.f)
		{}

		FDrawIntruction(EDrawType InDrawType, const FLinearColor& InColor, float InThickness = 0.f)
			: DrawType(InDrawType)
			, Color(InColor)
			, Thickness(InThickness)
		{}
	};

	TArray<FDrawIntruction> DrawInstructions;

	friend class FControlRigEditMode;
	friend class UControlRig;
};
