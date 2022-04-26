// Copyright Epic Games, Inc. All Rights Reserved.


#include "IWaveformTransformation.h"
#include "Templates/SharedPointer.h"

TArray<Audio::FTransformationPtr> UWaveformTransformationChain::CreateTransformations() const
{
	TArray<Audio::FTransformationPtr> TransformationPtrs;

	for(UWaveformTransformationBase* Transformation : Transformations)
	{
		if(Transformation)
		{
			TransformationPtrs.Add(Transformation->CreateTransformation());
		}
	}
	
	return TransformationPtrs;
}
