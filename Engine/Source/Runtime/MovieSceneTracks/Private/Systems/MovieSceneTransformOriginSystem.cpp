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

struct FAssignTransformOrigin
{
	const TSparseArray<FTransform>* TransformOriginsByInstanceID;

	void ForEachAllocation(const FEntityAllocation* Allocation, TRead<FInstanceHandle> Instances, TRead<UObject*> BoundObjects,
		TWriteOptional<float> LocationX, TWriteOptional<float> LocationY, TWriteOptional<float> LocationZ,
		TWriteOptional<float> RotationX, TWriteOptional<float> RotationY, TWriteOptional<float> RotationZ,
		TWriteOptional<FSourceFloatChannelFlags> FlagsLocationX, TWriteOptional<FSourceFloatChannelFlags> FlagsLocationY, TWriteOptional<FSourceFloatChannelFlags> FlagsLocationZ,
		TWriteOptional<FSourceFloatChannelFlags> FlagsRotationX, TWriteOptional<FSourceFloatChannelFlags> FlagsRotationY, TWriteOptional<FSourceFloatChannelFlags> FlagsRotationZ)
	{
		TransformLocation(Instances, BoundObjects, LocationX, LocationY, LocationZ, RotationX, RotationY, RotationZ, FlagsLocationX, FlagsLocationY, FlagsLocationZ, FlagsRotationX, FlagsRotationY, FlagsRotationZ, Allocation->Num());
	}

	void TransformLocation(const FInstanceHandle* Instances, const UObject* const * BoundObjects,
		float* OutLocationX, float* OutLocationY, float* OutLocationZ,
		float* OutRotationX, float* OutRotationY, float* OutRotationZ,
		FSourceFloatChannelFlags* OutFlagsLocationX, FSourceFloatChannelFlags* OutFlagsLocationY, FSourceFloatChannelFlags* OutFlagsLocationZ,
		FSourceFloatChannelFlags* OutFlagsRotationX, FSourceFloatChannelFlags* OutFlagsRotationY, FSourceFloatChannelFlags* OutFlagsRotationZ,
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

			FVector  CurrentTranslation(OutLocationX ? OutLocationX[Index] : 0.f, OutLocationY ? OutLocationY[Index] : 0.f, OutLocationZ ? OutLocationZ[Index] : 0.f);
			FRotator CurrentRotation(OutRotationY ? OutRotationY[Index] : 0.f, OutRotationZ ? OutRotationZ[Index] : 0.f, OutRotationX ? OutRotationX[Index] : 0.f);

			FTransform NewTransform = FTransform(CurrentRotation, CurrentTranslation)*Origin;

			FVector  NewTranslation = NewTransform.GetTranslation();
			FRotator NewRotation    = NewTransform.GetRotation().Rotator();

			if (OutLocationX) { OutLocationX[Index] = NewTranslation.X; }
			if (OutLocationY) { OutLocationY[Index] = NewTranslation.Y; }
			if (OutLocationZ) { OutLocationZ[Index] = NewTranslation.Z; }

			if (OutRotationX) { OutRotationX[Index] = NewRotation.Roll; }
			if (OutRotationY) { OutRotationY[Index] = NewRotation.Pitch; }
			if (OutRotationZ) { OutRotationZ[Index] = NewRotation.Yaw; }

			if (OutFlagsLocationX) { OutFlagsLocationX[Index].bNeedsEvaluate = true; }
			if (OutFlagsLocationY) { OutFlagsLocationY[Index].bNeedsEvaluate = true; }
			if (OutFlagsLocationZ) { OutFlagsLocationZ[Index].bNeedsEvaluate = true; }

			if (OutFlagsRotationX) { OutFlagsRotationX[Index].bNeedsEvaluate = true; }
			if (OutFlagsRotationY) { OutFlagsRotationY[Index].bNeedsEvaluate = true; }
			if (OutFlagsRotationZ) { OutFlagsRotationZ[Index].bNeedsEvaluate = true; }
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
		.WriteOptional(BuiltInComponents->FloatResult[3])
		.WriteOptional(BuiltInComponents->FloatResult[4])
		.WriteOptional(BuiltInComponents->FloatResult[5])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[0])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[1])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[2])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[3])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[4])
		.WriteOptional(BuiltInComponents->FloatChannelFlags[5])
		.CombineFilter(Filter)
		// Must contain at least one float result
		.FilterAny({ BuiltInComponents->FloatResult[0], BuiltInComponents->FloatResult[1], BuiltInComponents->FloatResult[2],
			BuiltInComponents->FloatResult[3], BuiltInComponents->FloatResult[4], BuiltInComponents->FloatResult[5] })
		.Dispatch_PerAllocation<FAssignTransformOrigin>(&Linker->EntityManager, InPrerequisites, &Subsequents, &TransformOriginsByInstanceID);
	}
}

