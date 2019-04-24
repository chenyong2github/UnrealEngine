// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Drawing/ControlRigDrawInterface.h"
#include "Math/ControlRigMathLibrary.h"

void FControlRigDrawInterface::DrawPoint(const FTransform& WorldOffset, const FVector& Position, float Size, const FLinearColor& Color)
{
	FDrawIntruction Instruction(EDrawType_Point, Color, Size);
	Instruction.Positions.Add(WorldOffset.TransformPosition(Position));
	DrawInstructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawPoints(const FTransform& WorldOffset, const TArray<FVector>& Points, float Size, const FLinearColor& Color)
{
	FDrawIntruction Instruction(EDrawType_Point, Color, Size);
	for(const FVector& Point : Points)
	{
		Instruction.Positions.Add(WorldOffset.TransformPosition(Point));
	}
	DrawInstructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawLine(const FTransform& WorldOffset, const FVector& LineStart, const FVector& LineEnd, const FLinearColor& Color, float Thickness)
{
	FDrawIntruction Instruction(EDrawType_Lines, Color, Thickness);
	Instruction.Positions.Add(WorldOffset.TransformPosition(LineStart));
	Instruction.Positions.Add(WorldOffset.TransformPosition(LineEnd));
	DrawInstructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawLines(const FTransform& WorldOffset, const TArray<FVector>& Positions, const FLinearColor& Color, float Thickness)
{
	FDrawIntruction Instruction(EDrawType_Lines, Color, Thickness);
	for (const FVector& Point : Positions)
	{
		Instruction.Positions.Add(WorldOffset.TransformPosition(Point));
	}
	DrawInstructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawLineStrip(const FTransform& WorldOffset, const TArray<FVector>& Positions, const FLinearColor& Color, float Thickness)
{
	FDrawIntruction Instruction(EDrawType_LineStrip, Color, Thickness);
	for (const FVector& Point : Positions)
	{
		Instruction.Positions.Add(WorldOffset.TransformPosition(Point));
	}
	DrawInstructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawBox(const FTransform& WorldOffset, const FTransform& Transform, const FLinearColor& Color, float Thickness)
{
	FTransform DrawTransform = Transform * WorldOffset;

	FDrawIntruction Instruction(EDrawType_Lines, Color, Thickness);

	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, 0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, -0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, -0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, -0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, -0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, 0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, 0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, 0.5f, 0.5f)));

	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, 0.5f, -0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, -0.5f, -0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, -0.5f, -0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, -0.5f, -0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, -0.5f, -0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, 0.5f, -0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, 0.5f, -0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, 0.5f, -0.5f)));

	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, 0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, 0.5f, -0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, -0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(0.5f, -0.5f, -0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, -0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, -0.5f, -0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, 0.5f, 0.5f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-0.5f, 0.5f, -0.5f)));

	DrawInstructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawAxes(const FTransform& WorldOffset, const FTransform& Transform, float Size, float Thickness)
{
	DrawLine(WorldOffset, Transform.GetLocation(), Transform.TransformPosition(FVector(Size, 0.f, 0.f)), FLinearColor::Red, Thickness);
	DrawLine(WorldOffset, Transform.GetLocation(), Transform.TransformPosition(FVector(0.f, Size, 0.f)), FLinearColor::Green, Thickness);
	DrawLine(WorldOffset, Transform.GetLocation(), Transform.TransformPosition(FVector(0.f, 0.f, Size)), FLinearColor::Blue, Thickness);
}

void FControlRigDrawInterface::DrawRectangle(const FTransform& WorldOffset, const FTransform& Transform, float Size, const FLinearColor& Color, float Thickness)
{
	FTransform DrawTransform = Transform * WorldOffset;

	FDrawIntruction Instruction(EDrawType_LineStrip, Color, Thickness);

	float Extent = Size * 0.5f;
	Instruction.Positions.Reserve(5);
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-Extent, -Extent, 0.f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(-Extent, Extent, 0.f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(Extent, Extent, 0.f)));
	Instruction.Positions.Add(DrawTransform.TransformPosition(FVector(Extent, -Extent, 0.f)));
	Instruction.Positions.Add(Instruction.Positions[0]);

	DrawInstructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawArc(const FTransform& WorldOffset, const FTransform& Transform, float Radius, float MinimumAngle, float MaximumAngle, const FLinearColor& Color, float Thickness, int32 Detail)
{
	int32 Count = FMath::Clamp<int32>(Detail, 4, 32);
	
	FDrawIntruction Instruction(EDrawType_LineStrip, Color, Thickness);
	Instruction.Positions.Reserve(Count);

	FTransform DrawTransform = Transform * WorldOffset;

	FVector V = FVector(Radius, 0.f, 0.f);
	FQuat Rotation(FVector(0.f, 0.f, 1.f), MinimumAngle);
	V = Rotation.RotateVector(V);
	Instruction.Positions.Add(DrawTransform.TransformPosition(V));
	Rotation = FQuat(FVector(0.f, 0.f, 1.f), (MaximumAngle - MinimumAngle) / float(Count));
	for(int32 Index=1;Index<Count;Index++)
	{
		V = Rotation.RotateVector(V);
		Instruction.Positions.Add(DrawTransform.TransformPosition(V));
	}

	DrawInstructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawBezier(const FTransform& WorldOffset, const FVector& A, const FVector& B, const FVector& C, const FVector& D, float MinimumU, float MaximumU, const FLinearColor& Color, float Thickness, int32 Detail)
{
	int32 Count = FMath::Clamp<int32>(Detail, 4, 64);
	FDrawIntruction Instruction(EDrawType_LineStrip, Color, Thickness);
	Instruction.Positions.SetNumUninitialized(Count);

	FVector P0 = WorldOffset.TransformPosition(A);
	FVector P1 = WorldOffset.TransformPosition(B);
	FVector P2 = WorldOffset.TransformPosition(C);
	FVector P3 = WorldOffset.TransformPosition(D);

	float T = MinimumU;
	float Step = (MaximumU - MinimumU) / float(Detail-1);
	for(int32 Index=0;Index<Count;Index++)
	{
		FVector Tangent;
		FControlRigMathLibrary::FourPointBezier(P0, P1, P2, P3, T, Instruction.Positions[Index], Tangent);
		T += Step;
	}

	DrawInstructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawHierarchy(const FTransform& WorldOffset, const FRigHierarchy& Hierarchy, EControlRigDrawHierarchyMode::Type Mode, float Scale, const FLinearColor& Color, float Thickness)
{
	switch (Mode)
	{
		case EControlRigDrawHierarchyMode::Axes:
		{
			FDrawIntruction InstructionX(EDrawType_Lines, FLinearColor::Red, Thickness);
			FDrawIntruction InstructionY(EDrawType_Lines, FLinearColor::Green, Thickness);
			FDrawIntruction InstructionZ(EDrawType_Lines, FLinearColor::Blue, Thickness);
			FDrawIntruction InstructionParent(EDrawType_Lines, Color, Thickness);
			InstructionX.Positions.Reserve(Hierarchy.Bones.Num() * 2);
			InstructionY.Positions.Reserve(Hierarchy.Bones.Num() * 2);
			InstructionZ.Positions.Reserve(Hierarchy.Bones.Num() * 2);
			InstructionParent.Positions.Reserve(Hierarchy.Bones.Num() * 6);

			for (const FRigBone& Bone : Hierarchy.Bones)
			{
				FTransform Transform = Bone.GlobalTransform * WorldOffset;
				FVector P0 = Transform.GetLocation();
				FVector PX = Transform.TransformPosition(FVector(Scale, 0.f, 0.f));
				FVector PY = Transform.TransformPosition(FVector(0.f, Scale, 0.f));
				FVector PZ = Transform.TransformPosition(FVector(0.f, 0.f, Scale));
				InstructionX.Positions.Add(P0);
				InstructionX.Positions.Add(PX);
				InstructionY.Positions.Add(P0);
				InstructionY.Positions.Add(PY);
				InstructionZ.Positions.Add(P0);
				InstructionZ.Positions.Add(PZ);

				if (Bone.ParentIndex != INDEX_NONE)
				{
					FTransform ParentTransform = Hierarchy.Bones[Bone.ParentIndex].GlobalTransform * WorldOffset;
					FVector P1 = ParentTransform.GetLocation();
					InstructionParent.Positions.Add(P0);
					InstructionParent.Positions.Add(P1);
				}
			}

			DrawInstructions.Add(InstructionX);
			DrawInstructions.Add(InstructionY);
			DrawInstructions.Add(InstructionZ);
			DrawInstructions.Add(InstructionParent);
			break;
		}
	}
}
