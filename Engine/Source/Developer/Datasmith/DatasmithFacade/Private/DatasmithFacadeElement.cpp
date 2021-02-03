// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeElement.h"


FDatasmithFacadeElement::ECoordinateSystemType FDatasmithFacadeElement::WorldCoordinateSystemType = FDatasmithFacadeElement::ECoordinateSystemType::LeftHandedZup;

float FDatasmithFacadeElement::WorldUnitScale = 1.0;

FDatasmithFacadeElement::ConvertVertexMethod FDatasmithFacadeElement::ConvertPosition  = nullptr;
FDatasmithFacadeElement::ConvertVectorMethod FDatasmithFacadeElement::ConvertBackPosition = nullptr;
FDatasmithFacadeElement::ConvertVertexMethod FDatasmithFacadeElement::ConvertDirection = nullptr;
FDatasmithFacadeElement::ConvertVectorMethod FDatasmithFacadeElement::ConvertBackDirection = nullptr;

void FDatasmithFacadeElement::SetCoordinateSystemType(
	ECoordinateSystemType InWorldCoordinateSystemType
)
{
	WorldCoordinateSystemType = InWorldCoordinateSystemType;

	switch (WorldCoordinateSystemType)
	{
		case ECoordinateSystemType::LeftHandedYup:
		{
			ConvertPosition  = ConvertFromPositionLeftHandedYup;
			ConvertBackPosition = ConvertToPositionLeftHandedYup;
			ConvertDirection = ConvertFromDirectionLeftHandedYup;
			ConvertBackDirection = ConvertToDirectionLeftHandedYup;
			break;
		}
		case ECoordinateSystemType::LeftHandedZup:
		{
			ConvertPosition  = ConvertFromPositionLeftHandedZup;
			ConvertBackPosition = ConvertToPositionLeftHandedZup;
			ConvertDirection = ConvertFromDirectionLeftHandedZup;
			ConvertBackDirection = ConvertToDirectionLeftHandedZup;
			break;
		}
		case ECoordinateSystemType::RightHandedZup:
		{
			ConvertPosition  = ConvertFromPositionRightHandedZup;
			ConvertBackPosition = ConvertToPositionRightHandedZup;
			ConvertDirection = ConvertFromDirectionRightHandedZup;
			ConvertBackDirection = ConvertToDirectionRightHandedZup;
			break;
		}
	}
}

void FDatasmithFacadeElement::SetWorldUnitScale(
	float InWorldUnitScale
)
{
	WorldUnitScale = FMath::IsNearlyZero(InWorldUnitScale) ? SMALL_NUMBER : InWorldUnitScale;
}

FDatasmithFacadeElement::FDatasmithFacadeElement(
	const TSharedRef<IDatasmithElement>& InElement
)
	: InternalDatasmithElement(InElement)
{}

void FDatasmithFacadeElement::GetStringHash(const TCHAR* InString, TCHAR OutBuffer[33], size_t BufferSize)
{
	FString HashedName = FMD5::HashAnsiString(InString);
	FCString::Strncpy(OutBuffer, *HashedName, BufferSize);
}

void FDatasmithFacadeElement::SetName(
	const TCHAR* InElementName
)
{
	InternalDatasmithElement->SetName(InElementName);
}

const TCHAR* FDatasmithFacadeElement::GetName() const
{
	return InternalDatasmithElement->GetName();
}

void FDatasmithFacadeElement::SetLabel(
	const TCHAR* InElementLabel
)
{
	InternalDatasmithElement->SetLabel(InElementLabel);
}

const TCHAR* FDatasmithFacadeElement::GetLabel() const
{
	return InternalDatasmithElement->GetLabel();
}

FVector FDatasmithFacadeElement::ConvertTranslation(
	FVector const& InVertex
)
{
	return ConvertPosition(InVertex.X, InVertex.Y, InVertex.Z);
}

void FDatasmithFacadeElement::ExportAsset(
	FString const& InAssetFolder
)
{
	// By default, there is no Datasmith scene element asset to build and export.
}

FVector FDatasmithFacadeElement::ConvertFromPositionLeftHandedYup(
	float InX,
	float InY,
	float InZ
)
{
	return FVector(InX * WorldUnitScale, -InZ * WorldUnitScale, InY * WorldUnitScale);
}

FVector FDatasmithFacadeElement::ConvertToPositionLeftHandedYup(
	const FVector& InVector
)
{
	float Scale = 1.f / WorldUnitScale;
	return FVector(InVector.X * Scale, InVector.Z * Scale, -InVector.Y * Scale);
}

FVector FDatasmithFacadeElement::ConvertFromPositionLeftHandedZup(
	float InX,
	float InY,
	float InZ
)
{
	return FVector(InX * WorldUnitScale, InY * WorldUnitScale, InZ * WorldUnitScale);
}

FVector FDatasmithFacadeElement::ConvertToPositionLeftHandedZup(
	const FVector& InVector
)
{
	return InVector / WorldUnitScale;
}

FVector FDatasmithFacadeElement::ConvertFromPositionRightHandedZup(
	float InX,
	float InY,
	float InZ
)
{
	// Convert the position from the source right-hand Z-up coordinate system to the Unreal left-hand Z-up coordinate system.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	return FVector(InX * WorldUnitScale, -InY * WorldUnitScale, InZ * WorldUnitScale);
}

FVector FDatasmithFacadeElement::ConvertToPositionRightHandedZup(
	const FVector& InVector
)
{
	// Convert the position from the source right-hand Z-up coordinate system to the Unreal left-hand Z-up coordinate system.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	float Scale = 1.f / WorldUnitScale;
	return FVector(InVector.X * Scale, -InVector.Y * Scale, InVector.Z * Scale);
}

FVector FDatasmithFacadeElement::ConvertFromDirectionLeftHandedYup(
	float InX,
	float InY,
	float InZ
)
{
	return FVector(InX, -InZ, InY);
}

FVector FDatasmithFacadeElement::ConvertToDirectionLeftHandedYup(
	const FVector& InVector
)
{
	return FVector(InVector.X, InVector.Z, -InVector.Y);
}

FVector FDatasmithFacadeElement::ConvertFromDirectionLeftHandedZup(
	float InX,
	float InY,
	float InZ
)
{
	return FVector(InX, InY, InZ);
}

FVector FDatasmithFacadeElement::ConvertToDirectionLeftHandedZup(
	const FVector& InVector
)
{
	return InVector;
}

FVector FDatasmithFacadeElement::ConvertFromDirectionRightHandedZup(
	float InX,
	float InY,
	float InZ
)
{
	// Convert the direction from the source right-hand Z-up coordinate system to the Unreal left-hand Z-up coordinate system.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	return FVector(InX, -InY, InZ);
}

FVector FDatasmithFacadeElement::ConvertToDirectionRightHandedZup(
	const FVector& InVector
)
{
	// Convert the direction from the source right-hand Z-up coordinate system to the Unreal left-hand Z-up coordinate system.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	return FVector(InVector.X, -InVector.Y, InVector.Z);
}