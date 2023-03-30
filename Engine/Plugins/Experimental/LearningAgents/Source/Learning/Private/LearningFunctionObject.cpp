// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningFunctionObject.h"

#include "LearningRandom.h"

namespace UE::Learning
{
	FFunctionObject::FFunctionObject(const TSharedRef<FArrayMap>& InInstanceData) : InstanceData(InInstanceData) {}

	FSequentialFunction::FSequentialFunction(
		const TLearningArrayView<1, const TSharedRef<FFunctionObject>> InFunctions,
		const TSharedRef<FArrayMap>& InInstanceData)
		: FFunctionObject(InInstanceData)
		, Functions(InFunctions) {}

	void FSequentialFunction::Evaluate(const FIndexSet Instances)
	{
		for (const TSharedRef<FFunctionObject>& Function : Functions)
		{
			Function->Evaluate(Instances);
		}
	}

	FCopyVectorsFunction::FCopyVectorsFunction(
		const FName& InIdentifier,
		const TSharedRef<FArrayMap>& InInstanceData,
		const int32 InMaxInstanceNum,
		const int32 InDimensionsNum)
		: FFunctionObject(InInstanceData)
	{
		InputHandle = InstanceData->Add<2, float>({ InIdentifier, TEXT("Seed") }, { InMaxInstanceNum, InDimensionsNum }, 0.0f);
		OutputHandle = InstanceData->Add<2, float>({ InIdentifier, TEXT("Min") }, { InMaxInstanceNum, InDimensionsNum }, 0.0f);
	}

	void FCopyVectorsFunction::Evaluate(const FIndexSet Instances)
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FCopyVectorsFunction::Evaluate);

		const TLearningArrayView<2, const float> Input = InstanceData->ConstView(InputHandle);
		TLearningArrayView<2, float> Output = InstanceData->View(OutputHandle);

		Array::Copy(Output, Input, Instances);
	}

	FExtractRotationsFromTransformsFunction::FExtractRotationsFromTransformsFunction(
		const FName& InIdentifier,
		const TSharedRef<FArrayMap>& InInstanceData,
		const int32 InMaxInstanceNum)
		: FFunctionObject(InInstanceData)
	{
		TransformHandle = InstanceData->Add<1, FTransform>({ InIdentifier, TEXT("Transform") }, { InMaxInstanceNum }, FTransform::Identity);
		RotationHandle = InstanceData->Add<1, FQuat>({ InIdentifier, TEXT("Rotation") }, { InMaxInstanceNum }, FQuat::Identity);
	}

	void FExtractRotationsFromTransformsFunction::Evaluate(const FIndexSet Instances)
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FExtractRotationsFromTransformsFunction::Evaluate);

		const TLearningArrayView<1, const FTransform> Transform = InstanceData->ConstView(TransformHandle);
		TLearningArrayView<1, FQuat> Rotation = InstanceData->View(RotationHandle);

		for (const int32 InstanceIdx : Instances)
		{
			Rotation[InstanceIdx] = Transform[InstanceIdx].GetRotation();
		}
	}

	FExtractPositionsAndRotationsFromTransformsFunction::FExtractPositionsAndRotationsFromTransformsFunction(
		const FName& InIdentifier,
		const TSharedRef<FArrayMap>& InInstanceData,
		const int32 InMaxInstanceNum)
		: FFunctionObject(InInstanceData)
	{
		TransformHandle = InstanceData->Add<1, FTransform>({ InIdentifier, TEXT("Transform") }, { InMaxInstanceNum }, FTransform::Identity);
		RotationHandle = InstanceData->Add<1, FQuat>({ InIdentifier, TEXT("Rotation") }, { InMaxInstanceNum }, FQuat::Identity);
		PositionHandle = InstanceData->Add<1, FVector>({ InIdentifier, TEXT("Position") }, { InMaxInstanceNum }, FVector::ZeroVector);
	}

	void FExtractPositionsAndRotationsFromTransformsFunction::Evaluate(const FIndexSet Instances)
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FExtractPositionsAndRotationsFromTransformsFunction::Evaluate);

		const TLearningArrayView<1, const FTransform> Transform = InstanceData->ConstView(TransformHandle);
		TLearningArrayView<1, FQuat> Rotation = InstanceData->View(RotationHandle);
		TLearningArrayView<1, FVector> Position = InstanceData->View(PositionHandle);

		for (const int32 InstanceIdx : Instances)
		{
			Rotation[InstanceIdx] = Transform[InstanceIdx].GetRotation();
			Position[InstanceIdx] = Transform[InstanceIdx].GetTranslation();
		}
	}

	FRandomUniformFunction::FRandomUniformFunction(
		const FName& InIdentifier,
		const TSharedRef<FArrayMap>& InInstanceData,
		const int32 InMaxInstanceNum,
		const uint32 InSeed,
		const float InMin,
		const float InMax)
		: FFunctionObject(InInstanceData)
	{
		SeedHandle = InstanceData->Add<1, uint32>({ InIdentifier, TEXT("Seed") }, { InMaxInstanceNum });
		MinHandle = InstanceData->Add<1, float>({ InIdentifier, TEXT("Min") }, { InMaxInstanceNum }, InMin);
		MaxHandle = InstanceData->Add<1, float>({ InIdentifier, TEXT("Max") }, { InMaxInstanceNum }, InMax);
		ValueHandle = InstanceData->Add<1, float>({ InIdentifier, TEXT("Value") }, { InMaxInstanceNum }, 0.0f);

		Random::IntArray(InstanceData->View(SeedHandle), InSeed);
	}

	void FRandomUniformFunction::Evaluate(const FIndexSet Instances)
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FRandomUniformFunction::Evaluate);

		const TLearningArrayView<1, const float> Min = InstanceData->ConstView(MinHandle);
		const TLearningArrayView<1, const float> Max = InstanceData->ConstView(MaxHandle);
		TLearningArrayView<1, uint32> Seed = InstanceData->View(SeedHandle);
		TLearningArrayView<1, float> Values = InstanceData->View(ValueHandle);

		for (const int32 InstanceIdx : Instances)
		{
			Values[InstanceIdx] = Random::SampleUniform(
				Seed[InstanceIdx],
				Min[InstanceIdx],
				Max[InstanceIdx]);
		}
	}

	FRandomPlanarClippedGaussianFunction::FRandomPlanarClippedGaussianFunction(
		const FName& InIdentifier,
		const TSharedRef<FArrayMap>& InInstanceData,
		const int32 InMaxInstanceNum,
		const uint32 InSeed,
		const float InMean,
		const float InStd,
		const float InClip)
		: FFunctionObject(InInstanceData)
	{
		SeedHandle = InstanceData->Add<1, uint32>({ InIdentifier, TEXT("Seed") }, { InMaxInstanceNum });
		MeanHandle = InstanceData->Add<1, float>({ InIdentifier, TEXT("Mean") }, { InMaxInstanceNum }, InMean);
		StdHandle = InstanceData->Add<1, float>({ InIdentifier, TEXT("Std") }, { InMaxInstanceNum }, InStd);
		ClipHandle = InstanceData->Add<1, float>({ InIdentifier, TEXT("Clip") }, { InMaxInstanceNum }, InClip);
		ValueHandle = InstanceData->Add<1, FVector>({ InIdentifier, TEXT("Value") }, { InMaxInstanceNum }, FVector::ZeroVector);

		Random::IntArray(InstanceData->View(SeedHandle), InSeed);
	}

	void FRandomPlanarClippedGaussianFunction::Evaluate(const FIndexSet Instances)
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FRandomPlanarClippedGaussianFunction::Evaluate);

		const TLearningArrayView<1, const float> Mean = InstanceData->ConstView(MeanHandle);
		const TLearningArrayView<1, const float> Std = InstanceData->ConstView(StdHandle);
		const TLearningArrayView<1, const float> Clip = InstanceData->ConstView(ClipHandle);
		TLearningArrayView<1, uint32> Seed = InstanceData->View(SeedHandle);
		TLearningArrayView<1, FVector> Values = InstanceData->View(ValueHandle);

		for (const int32 InstanceIdx : Instances)
		{
			Values[InstanceIdx] = Random::SamplePlanarClippedGaussian(
				Seed[InstanceIdx],
				Mean[InstanceIdx],
				Std[InstanceIdx],
				Clip[InstanceIdx]);
		}
	}

	FRandomPlanarDirectionFunction::FRandomPlanarDirectionFunction(
		const FName& InIdentifier,
		const TSharedRef<FArrayMap>& InInstanceData,
		const uint32 InSeed,
		const int32 InMaxInstanceNum)
		: FFunctionObject(InInstanceData)
	{
		SeedHandle = InstanceData->Add<1, uint32>({ InIdentifier, TEXT("Seed") }, { InMaxInstanceNum });
		DirectionHandle = InstanceData->Add<1, FVector>({ InIdentifier, TEXT("Direction") }, { InMaxInstanceNum }, FVector::ForwardVector);

		Random::IntArray(InstanceData->View(SeedHandle), InSeed);
	}

	void FRandomPlanarDirectionFunction::Evaluate(const FIndexSet Instances)
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FRandomPlanarDirectionFunction::Evaluate);

		TLearningArrayView<1, uint32> Seed = InstanceData->View(SeedHandle);
		TLearningArrayView<1, FVector> Direction = InstanceData->View(DirectionHandle);

		for (const int32 InstanceIdx : Instances)
		{
			Direction[InstanceIdx] = Random::SamplePlanarDirection(Seed[InstanceIdx]);
		}
	}

	FRandomPlanarDirectionVelocityFunction::FRandomPlanarDirectionVelocityFunction(
		const FName& InIdentifier,
		const TSharedRef<FArrayMap>& InInstanceData,
		const int32 InMaxInstanceNum,
		const uint32 InSeed,
		const float InVelocityScale)
		: FFunctionObject(InInstanceData)
	{
		SeedHandle = InstanceData->Add<1, uint32>({ InIdentifier, TEXT("Seed") }, { InMaxInstanceNum });
		VelocityScaleHandle = InstanceData->Add<1, float>({ InIdentifier, TEXT("VelocityScale") }, { InMaxInstanceNum }, InVelocityScale);
		DirectionHandle = InstanceData->Add<1, FVector>({ InIdentifier, TEXT("Direction") }, { InMaxInstanceNum }, FVector::ForwardVector);
		VelocityHandle = InstanceData->Add<1, FVector>({ InIdentifier, TEXT("Velocity") }, { InMaxInstanceNum }, FVector::ZeroVector);

		Random::IntArray(InstanceData->View(SeedHandle), InSeed);
	}

	void FRandomPlanarDirectionVelocityFunction::Evaluate(const FIndexSet Instances)
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FRandomPlanarDirectionVelocityFunction::Evaluate);

		const TLearningArrayView<1, const float> VelocityScale = InstanceData->ConstView(VelocityScaleHandle);
		TLearningArrayView<1, uint32> Seed = InstanceData->View(SeedHandle);
		TLearningArrayView<1, FVector> Direction = InstanceData->View(DirectionHandle);
		TLearningArrayView<1, FVector> Velocity = InstanceData->View(VelocityHandle);

		for (const int32 InstanceIdx : Instances)
		{
			const FVector RandomDirection = Random::SamplePlanarDirection(Seed[InstanceIdx]);
			Direction[InstanceIdx] = RandomDirection;
			Velocity[InstanceIdx] = VelocityScale[InstanceIdx] * RandomDirection;
		}
	}

}

