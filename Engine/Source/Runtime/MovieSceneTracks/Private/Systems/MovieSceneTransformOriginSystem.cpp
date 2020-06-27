// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneTransformOriginSystem.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "Tracks/IMovieSceneTransformOrigin.h"
#include "MovieSceneTracksComponentTypes.h"

#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/MovieScenePiecewiseFloatBlenderSystem.h"
#include "Systems/MovieSceneComponentTransformSystem.h"

#include "IMovieScenePlayer.h"
#include "IMovieScenePlaybackClient.h"

namespace UE
{
namespace MovieScene
{

struct FAssignTransformOriginLocation
{
	const TSparseArray<FTransform>* TransformOriginsByInstanceID;

	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<FInstanceHandle> InstanceAccessor, TRead<UObject*> BoundObjectAccessor,
		TWriteOptional<float> LocationXAccessor, TWriteOptional<float> LocationYAccessor, TWriteOptional<float> LocationZAccessor,
		TWriteOptional<FSourceFloatChannelFlags> FlagsXAccessor, TWriteOptional<FSourceFloatChannelFlags> FlagsYAccessor, TWriteOptional<FSourceFloatChannelFlags> FlagsZAccessor)
	{
		const FInstanceHandle* Instances = InstanceAccessor.Resolve(Allocation);
		const UObject* const * BoundObjects = BoundObjectAccessor.Resolve(Allocation);

		float* LocationX = LocationXAccessor.Resolve(Allocation);
		float* LocationY = LocationYAccessor.Resolve(Allocation);
		float* LocationZ = LocationZAccessor.Resolve(Allocation);

		FSourceFloatChannelFlags* FlagsX = FlagsXAccessor.Resolve(Allocation);
		FSourceFloatChannelFlags* FlagsY = FlagsYAccessor.Resolve(Allocation);
		FSourceFloatChannelFlags* FlagsZ = FlagsZAccessor.Resolve(Allocation);

		TransformLocation(Instances, BoundObjects, LocationX, LocationY, LocationZ, FlagsX, FlagsY, FlagsZ, Allocation->Num());
	}

	void TransformLocation(const FInstanceHandle* Instances, const UObject* const * BoundObjects,
		float* OutLocationX, float* OutLocationY, float* OutLocationZ,
		FSourceFloatChannelFlags* OutFlagsX, FSourceFloatChannelFlags* OutFlagsY, FSourceFloatChannelFlags* OutFlagsZ,
		int32 Num)
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			FInstanceHandle InstanceHandle = Instances[Index];
			if (!TransformOriginsByInstanceID->IsValidIndex(InstanceHandle.InstanceID))
			{
				continue;
			}

			// Do not apply transform origins to attached objects
			const USceneComponent* SceneComponent = CastChecked<const USceneComponent>(BoundObjects[Index]);
			if (SceneComponent->GetAttachParent() != nullptr)
			{
				continue;
			}

			FTransform Origin = (*TransformOriginsByInstanceID)[InstanceHandle.InstanceID];

			// This transform has an origin
			FVector CurrentTranslation(OutLocationX ? OutLocationX[Index] : 0.f, OutLocationY ? OutLocationY[Index] : 0.f, OutLocationZ ? OutLocationZ[Index] : 0.f);
			FVector NewTranslation = Origin.GetRotation()*(Origin.GetScale3D()*CurrentTranslation) + Origin.GetTranslation();

			if (OutLocationX) { OutLocationX[Index] = NewTranslation.X; }
			if (OutLocationY) { OutLocationY[Index] = NewTranslation.Y; }
			if (OutLocationZ) { OutLocationZ[Index] = NewTranslation.Z; }

			if (OutFlagsX) { OutFlagsX[Index].bNeedsEvaluate = true; }
			if (OutFlagsY) { OutFlagsY[Index].bNeedsEvaluate = true; }
			if (OutFlagsZ) { OutFlagsZ[Index].bNeedsEvaluate = true; }
		}
	}
};


struct FAssignTransformOriginRotation
{
	const TSparseArray<FTransform>* TransformOriginsByInstanceID;

	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<FInstanceHandle> InstanceAccessor, TRead<UObject*> BoundObjectAccessor,
		TWriteOptional<float> RotationXAccessor, TWriteOptional<float> RotationYAccessor, TWriteOptional<float> RotationZAccessor,
		TWriteOptional<FSourceFloatChannelFlags> FlagsXAccessor, TWriteOptional<FSourceFloatChannelFlags> FlagsYAccessor, TWriteOptional<FSourceFloatChannelFlags> FlagsZAccessor)
	{
		const FInstanceHandle* Instances = InstanceAccessor.Resolve(Allocation);
		const UObject* const * BoundObjects = BoundObjectAccessor.Resolve(Allocation);

		float* RotationX = RotationXAccessor.Resolve(Allocation);
		float* RotationY = RotationYAccessor.Resolve(Allocation);
		float* RotationZ = RotationZAccessor.Resolve(Allocation);

		FSourceFloatChannelFlags* FlagsX = FlagsXAccessor.Resolve(Allocation);
		FSourceFloatChannelFlags* FlagsY = FlagsYAccessor.Resolve(Allocation);
		FSourceFloatChannelFlags* FlagsZ = FlagsZAccessor.Resolve(Allocation);

		TransformRotation(Instances, BoundObjects, RotationX, RotationY, RotationZ, FlagsX, FlagsY, FlagsZ, Allocation->Num());
	}

	void TransformRotation(const FInstanceHandle* Instances, const UObject* const * BoundObjects,
		float* OutRotationX, float* OutRotationY, float* OutRotationZ,
		FSourceFloatChannelFlags* OutFlagsX, FSourceFloatChannelFlags* OutFlagsY, FSourceFloatChannelFlags* OutFlagsZ,
		int32 Num)
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			FInstanceHandle InstanceHandle = Instances[Index];
			if (!TransformOriginsByInstanceID->IsValidIndex(InstanceHandle.InstanceID))
			{
				continue;
			}

			// Do not apply transform origins to attached objects
			const USceneComponent* SceneComponent = CastChecked<const USceneComponent>(BoundObjects[Index]);
			if (SceneComponent->GetAttachParent() != nullptr)
			{
				continue;
			}

			FTransform Origin = (*TransformOriginsByInstanceID)[InstanceHandle.InstanceID];

			// This transform has an origin
			FRotator CurrentRotation(OutRotationY ? OutRotationY[Index] : 0.f, OutRotationZ ? OutRotationZ[Index] : 0.f, OutRotationX ? OutRotationX[Index] : 0.f);
			FRotator NewRotation = Origin.GetRotation().Rotator() + CurrentRotation;

			if (OutRotationX) { OutRotationX[Index] = NewRotation.Roll; }
			if (OutRotationY) { OutRotationY[Index] = NewRotation.Pitch; }
			if (OutRotationZ) { OutRotationZ[Index] = NewRotation.Yaw; }

			if (OutFlagsX) { OutFlagsX[Index].bNeedsEvaluate = true; }
			if (OutFlagsY) { OutFlagsY[Index].bNeedsEvaluate = true; }
			if (OutFlagsZ) { OutFlagsZ[Index].bNeedsEvaluate = true; }
		}
	}
};


} // namespace MovieScene
} // namespace UE


UMovieSceneTransformOriginSystem::UMovieSceneTransformOriginSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// This system relies upon anything that creates entities
		DefineImplicitPrerequisite(GetClass(), UMovieScenePiecewiseFloatBlenderSystem::StaticClass());
		DefineImplicitPrerequisite(GetClass(), UMovieSceneComponentTransformSystem::StaticClass());

		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatResult[0]);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatResult[1]);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatResult[2]);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatResult[3]);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatResult[4]);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatResult[5]);

		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatChannelFlags[0]);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatChannelFlags[1]);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatChannelFlags[2]);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatChannelFlags[3]);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatChannelFlags[4]);
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->FloatChannelFlags[5]);
	}
}

bool UMovieSceneTransformOriginSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	using namespace UE::MovieScene;

	for (const FSequenceInstance& Instance : InLinker->GetInstanceRegistry()->GetSparseInstances())
	{
		const IMovieScenePlaybackClient*  Client       = Instance.GetPlayer()->GetPlaybackClient();
		const UObject*                    InstanceData = Client ? Client->GetInstanceData() : nullptr;
		const IMovieSceneTransformOrigin* RawInterface = Cast<const IMovieSceneTransformOrigin>(InstanceData);

		if (RawInterface || (InstanceData && InstanceData->GetClass()->ImplementsInterface(UMovieSceneTransformOrigin::StaticClass())))
		{
			return true;
		}
	}

	return false;
}

void UMovieSceneTransformOriginSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

	TransformOriginsByInstanceID.Empty(InstanceRegistry->GetSparseInstances().Num());

	const TSparseArray<FSequenceInstance>& SparseInstances = InstanceRegistry->GetSparseInstances();
	for (int32 Index = 0; Index < SparseInstances.GetMaxIndex(); ++Index)
	{
		if (!SparseInstances.IsValidIndex(Index))
		{
			continue;
		}

		const FSequenceInstance& Instance = SparseInstances[Index];

		const IMovieScenePlaybackClient*  Client       = Instance.GetPlayer()->GetPlaybackClient();
		const UObject*                    InstanceData = Client ? Client->GetInstanceData() : nullptr;
		const IMovieSceneTransformOrigin* RawInterface = Cast<const IMovieSceneTransformOrigin>(InstanceData);

		const bool bHasInterface = RawInterface || (InstanceData && InstanceData->GetClass()->ImplementsInterface(UMovieSceneTransformOrigin::StaticClass()));
		if (bHasInterface)
		{
			// Retrieve the current origin
			FTransform TransformOrigin = RawInterface ? RawInterface->GetTransformOrigin() : IMovieSceneTransformOrigin::Execute_BP_GetTransformOrigin(InstanceData);

			TransformOriginsByInstanceID.Insert(Index, TransformOrigin);
		}
	}

	if (TransformOriginsByInstanceID.Num() != 0)
	{
		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
		FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();

		FEntityComponentFilter Filter;
		Filter.All({ TracksComponents->ComponentTransform.PropertyTag, BuiltInComponents->Tags.AbsoluteBlend });
		Filter.None({ BuiltInComponents->BlendChannelOutput });

		FEntityTaskBuilder()
		.Read(BuiltInComponents->InstanceHandle)
		.Read(BuiltInComponents->BoundObject)
		.WriteOptional(BuiltInComponents->FloatResult[0])
		.WriteOptional(BuiltInComponents->FloatResult[1])
		.WriteOptional(BuiltInComponents->FloatResult[2])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[0])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[1])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[2])
		.CombineFilter(Filter)
		// Must contain at least one float result
		.FilterAny({ BuiltInComponents->FloatResult[0], BuiltInComponents->FloatResult[1], BuiltInComponents->FloatResult[2] })
		.Dispatch_PerAllocation<FAssignTransformOriginLocation>(&Linker->EntityManager, InPrerequisites, &Subsequents, &TransformOriginsByInstanceID);

		FEntityTaskBuilder()
		.Read(BuiltInComponents->InstanceHandle)
		.Read(BuiltInComponents->BoundObject)
		.WriteOptional(BuiltInComponents->FloatResult[3])
		.WriteOptional(BuiltInComponents->FloatResult[4])
		.WriteOptional(BuiltInComponents->FloatResult[5])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[3])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[4])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[5])
		.CombineFilter(Filter)
		// Must contain at least one float result
		.FilterAny({ BuiltInComponents->FloatResult[4], BuiltInComponents->FloatResult[4], BuiltInComponents->FloatResult[5] })
		.Dispatch_PerAllocation<FAssignTransformOriginRotation>(&Linker->EntityManager, InPrerequisites, &Subsequents, &TransformOriginsByInstanceID);
	}
}

