// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Texture/InterchangeTextureTranslator.h"

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "InterchangeTranslatorBase.h"
#include "Texture/InterchangeTexturePayloadData.h"


const TOptional<Interchange::FImportImage> UInterchangeTextureTranslator::GetPayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const
{
	return TOptional<Interchange::FImportImage>();
}


