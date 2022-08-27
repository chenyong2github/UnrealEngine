// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFVariantUtility.h"
#include "LevelVariantSets.h"
#include "VariantSet.h"
#include "Variant.h"
#include "VariantObjectBinding.h"
#include "PropertyValue.h"

bool FGLTFVariantUtility::TryGetPropertyValue(UPropertyValue* Property, void* OutData, uint32 OutSize)
{
	if (Property == nullptr || !Property->HasRecordedData())
	{
		return false;
	}

	const TArray<uint8>& RecordedData = Property->GetRecordedData();
	check(OutSize == RecordedData.Num());

	FMemory::Memcpy(OutData, RecordedData.GetData(), OutSize);
	return true;
}

FString FGLTFVariantUtility::GetLogContext(const UPropertyValue* Property)
{
	const UVariantObjectBinding* Parent = Property->GetParent();
	return GetLogContext(Parent) + TEXT("/") + Property->GetFullDisplayString();
}

FString FGLTFVariantUtility::GetLogContext(const UVariantObjectBinding* Binding)
{
	const UVariant* Parent = const_cast<UVariantObjectBinding*>(Binding)->GetParent();
	return GetLogContext(Parent) + TEXT("/") + Binding->GetDisplayText().ToString();
}

FString FGLTFVariantUtility::GetLogContext(const UVariant* Variant)
{
	const UVariantSet* Parent = const_cast<UVariant*>(Variant)->GetParent();
	return GetLogContext(Parent) + TEXT("/") + Variant->GetDisplayText().ToString();
}

FString FGLTFVariantUtility::GetLogContext(const UVariantSet* VariantSet)
{
	const ULevelVariantSets* Parent = const_cast<UVariantSet*>(VariantSet)->GetParent();
	return GetLogContext(Parent) + TEXT("/") + VariantSet->GetDisplayText().ToString();
}

FString FGLTFVariantUtility::GetLogContext(const ULevelVariantSets* LevelVariantSets)
{
	return LevelVariantSets->GetName();
}
