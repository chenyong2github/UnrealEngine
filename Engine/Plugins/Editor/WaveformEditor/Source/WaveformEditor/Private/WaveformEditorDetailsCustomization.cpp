// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorDetailsCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Sound/SoundWave.h"
#include "WaveformEditorCustomDetailsHelpers.h"

void FWaveformTransformationsDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{	
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailLayout.GetObjectsBeingCustomized(Objects);

	//there should only one soundwave per editor instance open
	check(Objects.Num() == 1)

	if (UWaveformTransformationsViewHelper* TransformationsView = CastChecked<UWaveformTransformationsViewHelper>(Objects.Last()))
	{
		SoundWaveObject = TransformationsView->GetSoundWave();
	}

	IDetailCategoryBuilder& CategoryBuilder = DetailLayout.EditCategory("General");

	TArray<UObject*> ObjArray{ SoundWaveObject.Get() };
	CategoryBuilder.AddExternalObjectProperty(ObjArray, GET_MEMBER_NAME_CHECKED(USoundWave, Transformations));
	CategoryBuilder.InitiallyCollapsed(false);
}