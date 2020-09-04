// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

namespace UE
{
	class FUsdStage;
}

class USDSTAGEEDITORVIEWMODELS_API FUsdReference : public TSharedFromThis< FUsdReference >
{
public:
	FString AssetPath;
};

class USDSTAGEEDITORVIEWMODELS_API FUsdReferencesViewModel
{
public:
	void UpdateReferences( const UE::FUsdStage& UsdStage, const TCHAR* PrimPath );

public:
	TArray< TSharedPtr< FUsdReference > > References;
};