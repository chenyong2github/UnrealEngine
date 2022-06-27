// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationTransformLayout.h"

#include "Containers/Array.h"
#include "SoundModulationPatch.h"
#include "UObject/NoExportTypes.h"


const EWaveTableResolution* FSoundModulationTransformLayoutCustomization::GetResolution() const
{
	if (ensure(WaveTableOptionsHandle.IsValid()))
	{
		TArray<UObject*> OuterObjects;
		WaveTableOptionsHandle->GetOuterObjects(OuterObjects);
		if (OuterObjects.Num() == 1)
		{
			if (USoundModulationPatch* Patch = Cast<USoundModulationPatch>(OuterObjects.Last()))
			{
				FSoundControlModulationPatch& PatchSettings = Patch->PatchSettings;
				return &PatchSettings.WaveTableResolution;
			}
		}
	}

	return nullptr;
}

FWaveTableTransform* FSoundModulationTransformLayoutCustomization::GetTransform() const
{
	if (ensure(WaveTableOptionsHandle.IsValid()))
	{
		TArray<UObject*> OuterObjects;
		WaveTableOptionsHandle->GetOuterObjects(OuterObjects);
		if (OuterObjects.Num() == 1)
		{
			if (USoundModulationPatch* Patch = Cast<USoundModulationPatch>(OuterObjects.Last()))
			{
				const int32 InputIndex = GetOwningArrayIndex();
				if (InputIndex != INDEX_NONE)
				{
					FSoundControlModulationPatch& PatchSettings = Patch->PatchSettings;
					if (InputIndex < PatchSettings.Inputs.Num())
					{
						return &PatchSettings.Inputs[InputIndex].Transform;
					}
				}
			}
		}
	}

	return nullptr;
}

bool FSoundModulationTransformLayoutCustomization::IsBipolar() const
{
	return false;
}
