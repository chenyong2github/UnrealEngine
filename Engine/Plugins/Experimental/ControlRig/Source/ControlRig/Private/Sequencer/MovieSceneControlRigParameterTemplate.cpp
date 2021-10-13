// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneControlRigParameterTemplate.h"
#include "Evaluation/Blending/MovieSceneMultiChannelBlending.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "ControlRig.h"
#include "Evaluation/MovieSceneEvaluation.h"
#include "IMovieScenePlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "AnimCustomInstanceHelper.h"
#include "Sequencer/ControlRigLayerInstance.h"
#include "IControlRigObjectBinding.h"
#include "Animation/AnimData/BoneMaskFilter.h"
#include "ControlRigObjectBinding.h"
#include "Evaluation/Blending/BlendableTokenStack.h"
#include "Evaluation/Blending/MovieSceneBlendingActuatorID.h"
#include "TransformNoScale.h"
#include "ControlRigComponent.h"
#include "SkeletalMeshRestoreState.h"
#include "Rigs/FKControlRig.h"
#include "UObject/UObjectAnnotation.h"

//#include "Particles/ParticleSystemComponent.h"

DECLARE_CYCLE_STAT(TEXT("ControlRig Parameter Track Evaluate"), MovieSceneEval_ControlRigTemplateParameter_Evaluate, STATGROUP_MovieSceneEval);
DECLARE_CYCLE_STAT(TEXT("ControlRig Parameter Track Token Execute"), MovieSceneEval_ControlRigParameterTrack_TokenExecute, STATGROUP_MovieSceneEval);

template<typename T>
struct TNameAndValue
{
	FName Name;
	T Value;
};


/**
 * Structure representing the animated value of a scalar parameter.
 */
struct FScalarParameterStringAndValue
{
	/** Creates a new FScalarParameterAndValue with a parameter name and a value. */
	FScalarParameterStringAndValue(FName InParameterName, float InValue)
	{
		ParameterName = InParameterName;
		Value = InValue;
	}

	/** The name of the scalar parameter. */
	FName ParameterName;
	/** The animated value of the scalar parameter. */
	float Value;
};

/**
 * Structure representing the animated value of a bool parameter.
 */
struct FBoolParameterStringAndValue
{
	/** Creates a new FBoolParameterAndValue with a parameter name and a value. */
	FBoolParameterStringAndValue(FName InParameterName, bool InValue)
	{
		ParameterName = InParameterName;
		Value = InValue;
	}

	/** The name of the bool parameter. */
	FName ParameterName;
	/** The animated value of the bool parameter. */
	bool Value;
};

/**
 * Structure representing the animated value of a int parameter.
 */
struct FIntegerParameterStringAndValue
{
	FIntegerParameterStringAndValue(FName InParameterName, int32 InValue)
	{
		ParameterName = InParameterName;
		Value = InValue;
	}

	FName ParameterName;
	int32 Value;
};

/**
 * Structure representing the animated value of a vector2D parameter.
 */
struct FVector2DParameterStringAndValue
{
	/** Creates a new FVector2DParameterAndValue with a parameter name and a value. */
	FVector2DParameterStringAndValue(FName InParameterName, FVector2D InValue)
	{
		ParameterName = InParameterName;
		Value = InValue;
	}

	/** The name of the vector2D parameter. */
	FName ParameterName;

	/** The animated value of the vector2D parameter. */
	FVector2D Value;
};

/**
 * Structure representing the animated value of a vector parameter.
 */
struct FVectorParameterStringAndValue
{
	/** Creates a new FVectorParameterAndValue with a parameter name and a value. */
	FVectorParameterStringAndValue(FName InParameterName, FVector InValue)
	{
		ParameterName = InParameterName;
		Value = InValue;
	}

	/** The name of the vector parameter. */
	FName ParameterName;

	/** The animated value of the vector parameter. */
	FVector Value;
};


/**
 * Structure representing the animated value of a color parameter.
 */
struct FColorParameterStringAndValue
{
	/** Creates a new FColorParameterAndValue with a parameter name and a value. */
	FColorParameterStringAndValue(FName InParameterName, FLinearColor InValue)
	{
		ParameterName = InParameterName;
		Value = InValue;
	}

	/** The name of the color parameter. */
	FName ParameterName;

	/** The animated value of the color parameter. */
	FLinearColor Value;
};

struct FTransformParameterStringAndValue
{

	/** The name of the transform  parameter. */
	FName ParameterName;
	/** Translation component */
	FVector Translation;
	/** Rotation component */
	FRotator Rotation;
	/** Scale component */
	FVector Scale;

	FTransformParameterStringAndValue(FName InParameterName, const FVector& InTranslation,
		const FRotator& InRotation, const FVector& InScale) : Translation(InTranslation),
		Rotation(InRotation), Scale(InScale)
	{
		ParameterName = InParameterName;
	}
};


struct FControlRigAnimTypeIDs;

/** Thread-safe because objects can be destroyed on background threads */
using FControlRigAnimTypeIDsPtr = TSharedPtr<FControlRigAnimTypeIDs, ESPMode::ThreadSafe>;

/**
 * Control rig anim type IDs are a little complex - they require a unique type ID for every bone
 * and they must be unique per-animating control rig. To efficiently support finding these each frame,
 * We store a cache of the type IDs in a container on an object annotation for each control rig.
 */
struct FControlRigAnimTypeIDs
{
	/** Get the anim type IDs for the specified section */
	static FControlRigAnimTypeIDsPtr Get(const UControlRig* ControlRig)
	{
		struct FControlRigAnimTypeIDsAnnotation
		{
			// IsDefault should really have been implemented as a trait rather than a function so that this type isn't necessary
			bool IsDefault() const
			{
				return Ptr == nullptr;
			}
			FControlRigAnimTypeIDsPtr Ptr;
		};

		// Function-local static so that this only gets created once it's actually required
		static FUObjectAnnotationSparse<FControlRigAnimTypeIDsAnnotation, true> AnimTypeIDAnnotation;

		FControlRigAnimTypeIDsAnnotation TypeIDs = AnimTypeIDAnnotation.GetAnnotation(ControlRig);
		if (TypeIDs.Ptr != nullptr)
		{
			return TypeIDs.Ptr;
		}

		FControlRigAnimTypeIDsPtr NewPtr = MakeShared<FControlRigAnimTypeIDs, ESPMode::ThreadSafe>();
		AnimTypeIDAnnotation.AddAnnotation(ControlRig, FControlRigAnimTypeIDsAnnotation{NewPtr});
		return NewPtr;
	}

	/** Find the anim-type ID for the specified scalar parameter */
	FMovieSceneAnimTypeID FindScalar(FName InParameterName)
	{
		return FindImpl(InParameterName, ScalarAnimTypeIDsByName);
	}
	/** Find the anim-type ID for the specified Vector2D parameter */
	FMovieSceneAnimTypeID FindVector2D(FName InParameterName)
	{
		return FindImpl(InParameterName, Vector2DAnimTypeIDsByName);
	}
	/** Find the anim-type ID for the specified vector parameter */
	FMovieSceneAnimTypeID FindVector(FName InParameterName)
	{
		return FindImpl(InParameterName, VectorAnimTypeIDsByName);
	}
	/** Find the anim-type ID for the specified transform parameter */
	FMovieSceneAnimTypeID FindTransform(FName InParameterName)
	{
		return FindImpl(InParameterName, TransformAnimTypeIDsByName);
	}
private:

	/** Sorted map should give the best trade-off for lookup speed with relatively small numbers of bones (O(log n)) */
	using MapType = TSortedMap<FName, FMovieSceneAnimTypeID, FDefaultAllocator, FNameFastLess>;

	static FMovieSceneAnimTypeID FindImpl(FName InParameterName, MapType& InOutMap)
	{
		if (const FMovieSceneAnimTypeID* Type = InOutMap.Find(InParameterName))
		{
			return *Type;
		}
		FMovieSceneAnimTypeID New = FMovieSceneAnimTypeID::Unique();
		InOutMap.Add(InParameterName, FMovieSceneAnimTypeID::Unique());
		return New;
	}
	/** Array of existing type identifiers */
	MapType ScalarAnimTypeIDsByName;
	MapType Vector2DAnimTypeIDsByName;
	MapType VectorAnimTypeIDsByName;
	MapType TransformAnimTypeIDsByName;
};

/**
 * Cache structure that is stored per-section that defines bitmasks for every
 * index within each curve type. Set bits denote that the curve should be 
 * evaluated. Only ever initialized once since the template will get re-created
 * whenever the control rig section changes
 */
struct FEvaluatedControlRigParameterSectionChannelMasks : IPersistentEvaluationData
{
	TBitArray<> ScalarCurveMask;
	TBitArray<> BoolCurveMask;
	TBitArray<> IntegerCurveMask;
	TBitArray<> EnumCurveMask;
	TBitArray<> Vector2DCurveMask;
	TBitArray<> VectorCurveMask;
	TBitArray<> ColorCurveMask;
	TBitArray<> TransformCurveMask;

	void Initialize(const UMovieSceneControlRigParameterSection* Section,
		TArrayView<const FScalarParameterNameAndCurve> Scalars,
		TArrayView<const FBoolParameterNameAndCurve> Bools,
		TArrayView<const FIntegerParameterNameAndCurve> Integers,
		TArrayView<const FEnumParameterNameAndCurve> Enums,
		TArrayView<const FVector2DParameterNameAndCurves> Vector2Ds,
		TArrayView<const FVectorParameterNameAndCurves> Vectors,
		TArrayView<const FColorParameterNameAndCurves> Colors,
		TArrayView<const FTransformParameterNameAndCurves> Transforms
		)
	{
		const TArray<bool>& ControlsMask = Section->GetControlsMask();

		const FChannelMapInfo* ChannelInfo = nullptr;

		ScalarCurveMask.Add(false, Scalars.Num());
		BoolCurveMask.Add(false, Bools.Num());
		IntegerCurveMask.Add(false, Integers.Num());
		EnumCurveMask.Add(false, Enums.Num());
		Vector2DCurveMask.Add(false, Vector2Ds.Num());
		VectorCurveMask.Add(false, Vectors.Num());
		ColorCurveMask.Add(false, Colors.Num());
		TransformCurveMask.Add(false, Transforms.Num());
		
		for (int32 Index = 0; Index < Scalars.Num(); ++Index)
		{
			const FScalarParameterNameAndCurve& Scalar = Scalars[Index];
			ChannelInfo = Section->ControlChannelMap.Find(Scalar.ParameterName);
			ScalarCurveMask[Index] = (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex]);
		}
		for (int32 Index = 0; Index < Bools.Num(); ++Index)
		{
			const FBoolParameterNameAndCurve& Bool = Bools[Index];
			ChannelInfo = Section->ControlChannelMap.Find(Bool.ParameterName);
			BoolCurveMask[Index] = (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex]);
		}
		for (int32 Index = 0; Index < Integers.Num(); ++Index)
		{
			const FIntegerParameterNameAndCurve& Integer = Integers[Index];
			ChannelInfo = Section->ControlChannelMap.Find(Integer.ParameterName);
			IntegerCurveMask[Index] = (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex]);
		}
		for (int32 Index = 0; Index < Enums.Num(); ++Index)
		{
			const FEnumParameterNameAndCurve& Enum = Enums[Index];
			ChannelInfo = Section->ControlChannelMap.Find(Enum.ParameterName);
			EnumCurveMask[Index] = (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex]);
		}
		for (int32 Index = 0; Index < Vector2Ds.Num(); ++Index)
		{
			const FVector2DParameterNameAndCurves& Vector2D = Vector2Ds[Index];
			ChannelInfo = Section->ControlChannelMap.Find(Vector2D.ParameterName);
			Vector2DCurveMask[Index] = (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex]);
		}
		for (int32 Index = 0; Index < Vectors.Num(); ++Index)
		{
			const FVectorParameterNameAndCurves& Vector = Vectors[Index];
			ChannelInfo = Section->ControlChannelMap.Find(Vector.ParameterName);
			VectorCurveMask[Index] = (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex]);
		}
		for (int32 Index = 0; Index < Colors.Num(); ++Index)
		{
			const FColorParameterNameAndCurves& Color = Colors[Index];
			ChannelInfo = Section->ControlChannelMap.Find(Color.ParameterName);
			ColorCurveMask[Index] = (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex]);
		}
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			const FTransformParameterNameAndCurves& Transform = Transforms[Index];
			ChannelInfo = Section->ControlChannelMap.Find(Transform.ParameterName);
			TransformCurveMask[Index] = (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex]);
		}
	}
};
// Static hack because we cannot add this to the function parameters for EvaluateCurvesWithMasks due to hotfix restrictions
static FEvaluatedControlRigParameterSectionChannelMasks* HACK_ChannelMasks = nullptr;

struct FEvaluatedControlRigParameterSectionValues
{
	FEvaluatedControlRigParameterSectionValues() = default;

	FEvaluatedControlRigParameterSectionValues(FEvaluatedControlRigParameterSectionValues&&) = default;
	FEvaluatedControlRigParameterSectionValues& operator=(FEvaluatedControlRigParameterSectionValues&&) = default;

	// Non-copyable
	FEvaluatedControlRigParameterSectionValues(const FEvaluatedControlRigParameterSectionValues&) = delete;
	FEvaluatedControlRigParameterSectionValues& operator=(const FEvaluatedControlRigParameterSectionValues&) = delete;

	/** Array of evaluated scalar values */
	TArray<FScalarParameterStringAndValue, TInlineAllocator<2>> ScalarValues;
	/** Array of evaluated bool values */
	TArray<FBoolParameterStringAndValue, TInlineAllocator<2>> BoolValues;
	/** Array of evaluated integer values */
	TArray<FIntegerParameterStringAndValue, TInlineAllocator<2>> IntegerValues;
	/** Array of evaluated vector2d values */
	TArray<FVector2DParameterStringAndValue, TInlineAllocator<2>> Vector2DValues;
	/** Array of evaluated vector values */
	TArray<FVectorParameterStringAndValue, TInlineAllocator<2>> VectorValues;
	/** Array of evaluated color values */
	TArray<FColorParameterStringAndValue, TInlineAllocator<2>> ColorValues;
	/** Array of evaluated transform values */
	TArray<FTransformParameterStringAndValue, TInlineAllocator<2>> TransformValues;
};

/** Token for control rig control parameters */
struct FControlRigTrackTokenFloat
{
	FControlRigTrackTokenFloat() {}
	
	FControlRigTrackTokenFloat(float InValue)
		:Value(InValue)
	{}

	float Value;

};

struct FControlRigTrackTokenBool
{
	FControlRigTrackTokenBool() {}

	FControlRigTrackTokenBool(bool InValue)
		:Value(InValue)
	{}

	bool Value;
};

struct FControlRigTrackTokenVector2D
{
	FControlRigTrackTokenVector2D() {}
	FControlRigTrackTokenVector2D(FVector2D InValue)
		:Value(InValue)
	{}

	FVector2D Value;
};

struct FControlRigTrackTokenVector
{
	FControlRigTrackTokenVector() {}
	FControlRigTrackTokenVector(FVector InValue)
		:Value(InValue)
	{}

	FVector Value;
};

struct FControlRigTrackTokenTransform
{
	FControlRigTrackTokenTransform() {}
	FControlRigTrackTokenTransform(FTransform InValue)
		: Value(InValue)
	{}
	FTransform Value;

};



// Specify a unique runtime type identifier for rig control track tokens
template<> FMovieSceneAnimTypeID GetBlendingDataType<FControlRigTrackTokenFloat>()
{
	static FMovieSceneAnimTypeID TypeId = FMovieSceneAnimTypeID::Unique();
	return TypeId;
}

template<> FMovieSceneAnimTypeID GetBlendingDataType<FControlRigTrackTokenBool>()
{
	static FMovieSceneAnimTypeID TypeId = FMovieSceneAnimTypeID::Unique();
	return TypeId;
}

template<> FMovieSceneAnimTypeID GetBlendingDataType<FControlRigTrackTokenVector2D>()
{
	static FMovieSceneAnimTypeID TypeId = FMovieSceneAnimTypeID::Unique();
	return TypeId;
}

template<> FMovieSceneAnimTypeID GetBlendingDataType<FControlRigTrackTokenVector>()
{
	static FMovieSceneAnimTypeID TypeId = FMovieSceneAnimTypeID::Unique();
	return TypeId;
}

template<> FMovieSceneAnimTypeID GetBlendingDataType<FControlRigTrackTokenTransform>()
{
	static FMovieSceneAnimTypeID TypeId = FMovieSceneAnimTypeID::Unique();
	return TypeId;
}



/** Define working data types for blending calculations  */
template<>  struct TBlendableTokenTraits<FControlRigTrackTokenFloat>
{
	typedef UE::MovieScene::TMaskedBlendable<float, 1> WorkingDataType;
};

template<>  struct TBlendableTokenTraits<FControlRigTrackTokenBool>
{
	typedef UE::MovieScene::TMaskedBlendable<bool, 1> WorkingDataType;
};

template<> struct TBlendableTokenTraits<FControlRigTrackTokenVector2D>
{
	typedef UE::MovieScene::TMaskedBlendable<float, 2> WorkingDataType;
};

template<> struct TBlendableTokenTraits<FControlRigTrackTokenVector>
{
	typedef UE::MovieScene::TMaskedBlendable<float, 3> WorkingDataType;
};

template<>  struct TBlendableTokenTraits<FControlRigTrackTokenTransform>
{
	typedef UE::MovieScene::TMaskedBlendable<float, 9> WorkingDataType;
};




namespace UE
{
namespace MovieScene
{

	void MultiChannelFromData(const FControlRigTrackTokenFloat& In, TMultiChannelValue<float, 1>& Out)
	{
		Out = { In.Value };
	}

	void ResolveChannelsToData(const TMultiChannelValue<float, 1>& In, FControlRigTrackTokenFloat& Out)
	{
		Out.Value = In[0];
	}

	void MultiChannelFromData(const FControlRigTrackTokenBool& In, TMultiChannelValue<bool, 1>& Out)
	{
		Out = { In.Value };
	}

	void ResolveChannelsToData(const TMultiChannelValue<bool, 1>& In, FControlRigTrackTokenBool& Out)
	{
		Out.Value = In[0];
	}

	void MultiChannelFromData(const FControlRigTrackTokenVector2D& In, TMultiChannelValue<float, 2>& Out)
	{
		Out = { In.Value.X, In.Value.Y };
	}

	void ResolveChannelsToData(const TMultiChannelValue<float, 2>& In, FControlRigTrackTokenVector2D& Out)
	{
		Out.Value = FVector2D(In[0], In[1]);
	}

	void MultiChannelFromData(const FControlRigTrackTokenVector& In, TMultiChannelValue<float, 3>& Out)
	{
		Out = { In.Value.X, In.Value.Y, In.Value.Z };
	}

	void ResolveChannelsToData(const TMultiChannelValue<float, 3>& In, FControlRigTrackTokenVector& Out)
	{
		Out.Value = FVector(In[0], In[1], In[2]);
	}

	void MultiChannelFromData(const FControlRigTrackTokenTransform& In, TMultiChannelValue<float, 9>& Out)
	{
		FVector Translation = In.Value.GetTranslation();
		FVector Rotation = In.Value.GetRotation().Rotator().Euler();
		FVector Scale = In.Value.GetScale3D();
		Out = { Translation.X, Translation.Y, Translation.Z, Rotation.X, Rotation.Y, Rotation.Z, Scale.X, Scale.Y, Scale.Z };

	}

	void ResolveChannelsToData(const TMultiChannelValue<float, 9>& In, FControlRigTrackTokenTransform& Out)
	{
		Out.Value = FTransform(
			FRotator::MakeFromEuler(FVector(In[3], In[4], In[5])),
			FVector(In[0], In[1], In[2]),
			FVector(In[6], In[7], In[8])
		);
	}

} // namespace MovieScene
} // namespace UE

void FControlRigBindingHelper::BindToSequencerInstance(UControlRig* ControlRig)
{
	check(ControlRig);
	if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
	{
		bool bWasCreated = false;
		if (UControlRigLayerInstance* AnimInstance = FAnimCustomInstanceHelper::BindToSkeletalMeshComponent<UControlRigLayerInstance>(SkeletalMeshComponent, bWasCreated))
		{
			if (bWasCreated || !AnimInstance->HasControlRigTrack(ControlRig->GetUniqueID()))
			{
				AnimInstance->RecalcRequiredBones();
				AnimInstance->AddControlRigTrack(ControlRig->GetUniqueID(), ControlRig);
				ControlRig->Initialize();

				ControlRig->SetBoneInitialTransformsFromSkeletalMesh(SkeletalMeshComponent->SkeletalMesh);
			}
		}
	}
	else if (UControlRigComponent* ControlRigComponent = Cast<UControlRigComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
	{
		if (ControlRigComponent->GetControlRig() != ControlRig)
		{
			ControlRigComponent->Initialize();
			ControlRigComponent->SetControlRig(ControlRig);
		}
	}
}

void FControlRigBindingHelper::UnBindFromSequencerInstance(UControlRig* ControlRig)
{
	check(ControlRig);

	if (!ControlRig->IsValidLowLevel() ||
	    ControlRig->HasAnyFlags(RF_BeginDestroyed) ||
		ControlRig->IsPendingKill())
	{
		return;
	}
	
	if (UControlRigComponent* ControlRigComponent = Cast<UControlRigComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
	{
		// todo: how do we reset the state?
		//ControlRig->Initialize();
	}
	else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
	{
		if (!SkeletalMeshComponent->IsValidLowLevel() ||
			SkeletalMeshComponent->HasAnyFlags(RF_BeginDestroyed) ||
			SkeletalMeshComponent->IsPendingKill())
		{
			return;
		}

		UControlRigLayerInstance* AnimInstance = Cast<UControlRigLayerInstance>(SkeletalMeshComponent->GetAnimInstance());
		if (AnimInstance)
		{
			if (!AnimInstance->IsValidLowLevel() ||
                AnimInstance->HasAnyFlags(RF_BeginDestroyed) ||
                AnimInstance->IsPendingKill())
			{
				return;
			}

			AnimInstance->ResetNodes();
			AnimInstance->RecalcRequiredBones();
			AnimInstance->RemoveControlRigTrack(ControlRig->GetUniqueID());
		}

		FAnimCustomInstanceHelper::UnbindFromSkeletalMeshComponent< UControlRigLayerInstance>(SkeletalMeshComponent);
	}
}


struct FControlRigParameterPreAnimatedTokenProducer : IMovieScenePreAnimatedTokenProducer
{
	FControlRigParameterPreAnimatedTokenProducer(FMovieSceneSequenceIDRef InSequenceID)
		: SequenceID(InSequenceID)
	{}

	virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const
	{

		struct FToken : IMovieScenePreAnimatedToken
		{
			FToken(FMovieSceneSequenceIDRef InSequenceID)
				: SequenceID(InSequenceID)
			{

			}
			void SetSkelMesh(USkeletalMeshComponent* InComponent)
			{
				SkeletalMeshRestoreState.SaveState(InComponent);
			}

			virtual void RestoreState(UObject& InObject, const UE::MovieScene::FRestoreStateParams& Params) override
			{

				if (UControlRig* ControlRig = Cast<UControlRig>(&InObject))
				{
					if (ControlRig->GetObjectBinding())
					{
						if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
						{
							SkeletalMeshRestoreState.RestoreState(SkeletalMeshComponent);
						}

						FControlRigBindingHelper::UnBindFromSequencerInstance(ControlRig);
						
						for (TNameAndValue<float>& Value : ScalarValues)
						{
							if (ControlRig->FindControl(Value.Name))
							{
								ControlRig->SetControlValue<float>(Value.Name, Value.Value, true, FRigControlModifiedContext(EControlRigSetKey::Never));
							}
						}

						for (TNameAndValue<bool>& Value : BoolValues)
						{
							if (ControlRig->FindControl(Value.Name))
							{
								ControlRig->SetControlValue<bool>(Value.Name, Value.Value, true, FRigControlModifiedContext(EControlRigSetKey::Never));
							}
						}

						for (TNameAndValue<int32>& Value : IntegerValues)
						{
							if (ControlRig->FindControl(Value.Name))
							{
								ControlRig->SetControlValue<int32>(Value.Name, Value.Value, true, FRigControlModifiedContext(EControlRigSetKey::Never));
							}
						}

						for (TNameAndValue<FVector2D>& Value : Vector2DValues)
						{
							if (ControlRig->FindControl(Value.Name))
							{
								ControlRig->SetControlValue<FVector2D>(Value.Name, Value.Value, true, FRigControlModifiedContext(EControlRigSetKey::Never));
							}
						}

						for (TNameAndValue<FVector>& Value : VectorValues)
						{
							if (ControlRig->FindControl(Value.Name))
							{
								ControlRig->SetControlValue<FVector>(Value.Name, Value.Value, true, FRigControlModifiedContext(EControlRigSetKey::Never));
							}
						}

						for (TNameAndValue<FTransform>& Value : TransformValues)
						{
							if (FRigControl* RigControl = ControlRig->FindControl(Value.Name))
							{
								switch (RigControl->ControlType)
								{
								case ERigControlType::Transform:
								{
									ControlRig->SetControlValue<FTransform>(Value.Name, Value.Value, true, FRigControlModifiedContext(EControlRigSetKey::Never));
									break;
								}
								case ERigControlType::TransformNoScale:
								{
									FTransformNoScale NoScale = Value.Value;
									ControlRig->SetControlValue<FTransformNoScale>(Value.Name, NoScale, true, FRigControlModifiedContext(EControlRigSetKey::Never));
									break;
								}
								case ERigControlType::EulerTransform:
								{
									FEulerTransform EulerTransform = Value.Value;
									ControlRig->SetControlValue<FEulerTransform>(Value.Name, EulerTransform, true, FRigControlModifiedContext(EControlRigSetKey::Never));
									break;
								}

								}
							}
						}
						//only unbind if not a component
						if (Cast<UControlRigComponent>(ControlRig->GetObjectBinding()->GetBoundObject()) == nullptr)
						{
							ControlRig->GetObjectBinding()->UnbindFromObject();
						}
					}
				}
			}

			FMovieSceneSequenceID SequenceID;
			TArray< TNameAndValue<float> > ScalarValues;
			TArray< TNameAndValue<bool> > BoolValues;
			TArray< TNameAndValue<int32> > IntegerValues;
			TArray< TNameAndValue<FVector> > VectorValues;
			TArray< TNameAndValue<FVector2D> > Vector2DValues;
			TArray< TNameAndValue<FTransform> > TransformValues;
			FSkeletalMeshRestoreState SkeletalMeshRestoreState;

		};


		FToken Token(SequenceID);

		if (UControlRig* ControlRig = Cast<UControlRig>(&Object))
		{
			if (ControlRig->GetObjectBinding())
			{
				if (UControlRigComponent* ControlRigComponent = Cast<UControlRigComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
				{
					if (ControlRigComponent->GetControlRig() != ControlRig)
					{
						ControlRigComponent->SetControlRig(ControlRig);
					}
					else
					{
						ControlRig->Initialize();
					}
				}
				else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
				{
					Token.SetSkelMesh(SkeletalMeshComponent);
				}
			}

			const TArray<FRigControl>& Controls = ControlRig->AvailableControls();
			FRigControlValue Value;
			for (const FRigControl& RigControl : Controls)
			{
				switch (RigControl.ControlType)
				{
				case ERigControlType::Bool:
				{
					bool Val = RigControl.Value.Get<bool>();
					Token.BoolValues.Add(TNameAndValue<bool>{ RigControl.Name, Val });
					break;
				}
				case ERigControlType::Integer:
				{
					int32 Val = RigControl.Value.Get<int32>();
					Token.IntegerValues.Add(TNameAndValue<int32>{ RigControl.Name, Val });
					break;
				}
				case ERigControlType::Float:
				{
					float Val = RigControl.Value.Get<float>();
					Token.ScalarValues.Add(TNameAndValue<float>{ RigControl.Name, Val });
					break;
				}
				case ERigControlType::Vector2D:
				{
					FVector2D Val = RigControl.Value.Get<FVector2D>();
					Token.Vector2DValues.Add(TNameAndValue<FVector2D>{ RigControl.Name, Val });
					break;
				}
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
				{
					FVector Val = RigControl.Value.Get<FVector>();
					Token.VectorValues.Add(TNameAndValue<FVector>{ RigControl.Name, Val });
					//mz todo specify rotator special so we can do quat interps
					break;
				}
				case ERigControlType::Transform:
				{
					FTransform Val = RigControl.Value.Get<FTransform>();
					Token.TransformValues.Add(TNameAndValue<FTransform>{ RigControl.Name, Val });
					break;
				}
				case ERigControlType::TransformNoScale:
				{
					FTransformNoScale NoScale = RigControl.Value.Get<FTransformNoScale>();
					FTransform Val = NoScale;
					Token.TransformValues.Add(TNameAndValue<FTransform>{ RigControl.Name, Val });
					break;
				}
				case ERigControlType::EulerTransform:
				{
					FEulerTransform Euler = RigControl.Value.Get<FEulerTransform>();
					FTransform Val = Euler.ToFTransform();
					Token.TransformValues.Add(TNameAndValue<FTransform>{ RigControl.Name, Val });
					break;
				}
				}
			}
		}

		return MoveTemp(Token);
	}

	FMovieSceneSequenceID SequenceID;
	TArray< TNameAndValue<bool> > BoolValues;
	TArray< TNameAndValue<int32> > IntegerValues;
	TArray< TNameAndValue<float> > ScalarValues;
	TArray< TNameAndValue<FVector2D> > Vector2DValues;
	TArray< TNameAndValue<FVector> > VectorValues;
	TArray< TNameAndValue<FTransform> > TransformValues;

};



/* Simple token used for non-blendables*/
struct FControlRigParameterExecutionToken : IMovieSceneExecutionToken
{
	FControlRigParameterExecutionToken(const UMovieSceneControlRigParameterSection* InSection,
		const FEvaluatedControlRigParameterSectionValues& Values)
	:	Section(InSection)
	{
		BoolValues = Values.BoolValues;
		IntegerValues = Values.IntegerValues;
	}
	FControlRigParameterExecutionToken(FControlRigParameterExecutionToken&&) = default;
	FControlRigParameterExecutionToken& operator=(FControlRigParameterExecutionToken&&) = default;

	// Non-copyable
	FControlRigParameterExecutionToken(const FControlRigParameterExecutionToken&) = delete;
	FControlRigParameterExecutionToken& operator=(const FControlRigParameterExecutionToken&) = delete;

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_ControlRigParameterTrack_TokenExecute)
		
		FMovieSceneSequenceID SequenceID = Operand.SequenceID;
		UControlRig* ControlRig = Section->GetControlRig();

		// Update the animation's state
		
		if (ControlRig)
		{
			const UMovieSceneSequence* Sequence = Player.State.FindSequence(Operand.SequenceID);
			TArrayView<TWeakObjectPtr<>> BoundObjects = Player.FindBoundObjects(Operand);

			if (Sequence && BoundObjects.Num() > 0 && BoundObjects[0].Get())
			{
				if (!ControlRig->GetObjectBinding())
				{
					ControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
				}

				if (!ControlRig->GetObjectBinding()->GetBoundObject())
				{
					ControlRig->GetObjectBinding()->BindToObject(BoundObjects[0].Get());
					ControlRig->Initialize();
					if (ControlRig->IsA<UFKControlRig>())
					{
						UMovieSceneControlRigParameterTrack* Track = Section->GetTypedOuter<UMovieSceneControlRigParameterTrack>();
						if (Track)
						{
							Track->ReplaceControlRig(ControlRig, true);
						}
					}
				}
				// ensure that pre animated state is saved, must be done before bind
				Player.SavePreAnimatedState(*ControlRig, FMovieSceneControlRigParameterTemplate::GetAnimTypeID(), FControlRigParameterPreAnimatedTokenProducer(Operand.SequenceID));

				FControlRigBindingHelper::BindToSequencerInstance(ControlRig);

				if (ControlRig->GetObjectBinding())
				{
					if (UControlRigComponent* ControlRigComponent = Cast<UControlRigComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
					{
						if (AActor* Actor = Cast<AActor>(BoundObjects[0].Get()))
						{
							if (UControlRigComponent* NewControlRigComponent = Actor->FindComponentByClass<UControlRigComponent>())
							{
								if (NewControlRigComponent != ControlRigComponent)
								{
									ControlRig->GetObjectBinding()->BindToObject(BoundObjects[0].Get());
									if (NewControlRigComponent->GetControlRig() != ControlRig)
									{
										NewControlRigComponent->SetControlRig(ControlRig);
									}
									else
									{
										ControlRig->Initialize();
									}
								}
							}
						}
						else if (UControlRigComponent* NewControlRigComponent = Cast<UControlRigComponent>(BoundObjects[0].Get()))
						{
							if (NewControlRigComponent != ControlRigComponent)
							{
								ControlRig->GetObjectBinding()->BindToObject(BoundObjects[0].Get());
								if (NewControlRigComponent->GetControlRig() != ControlRig)
								{
									NewControlRigComponent->SetControlRig(ControlRig);
								}
								else
								{
									ControlRig->Initialize();
								}
							}
						}
					}
					else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
					{
						if (UControlRigLayerInstance* AnimInstance = Cast<UControlRigLayerInstance>(SkeletalMeshComponent->GetAnimInstance()))
						{
							float Weight = 1.0f;
							FControlRigIOSettings InputSettings;
							InputSettings.bUpdateCurves = true;
							InputSettings.bUpdatePose = true;
							AnimInstance->UpdateControlRigTrack(ControlRig->GetUniqueID(), Weight, InputSettings, true);
						}
					}
				}
			}		
		}

		//Do Bool straight up no blending
		if (Section->GetBlendType().Get() != EMovieSceneBlendType::Additive)
		{
			bool bWasDoNotKey = false;

			bWasDoNotKey = Section->GetDoNotKey();
			Section->SetDoNotKey(true);

			if (Section->GetControlRig())
			{
				Section->GetControlRig()->SetAbsoluteTime((float)Context.GetFrameRate().AsSeconds(Context.GetTime()));
				for (const FBoolParameterStringAndValue& BoolNameAndValue : BoolValues)
				{
					if (Section->ControlsToSet.Num() == 0 || Section->ControlsToSet.Contains(BoolNameAndValue.ParameterName))
					{
						FRigControl* RigControl = Section->GetControlRig()->FindControl(BoolNameAndValue.ParameterName);
						if (RigControl && RigControl->ControlType == ERigControlType::Bool)
						{
							Section->GetControlRig()->SetControlValue<bool>(BoolNameAndValue.ParameterName, BoolNameAndValue.Value, true, EControlRigSetKey::Never);
						}
					}
				}

				for (const FIntegerParameterStringAndValue& IntegerNameAndValue : IntegerValues)
				{
					if (Section->ControlsToSet.Num() == 0 || Section->ControlsToSet.Contains(IntegerNameAndValue.ParameterName))
					{
						FRigControl* RigControl = Section->GetControlRig()->FindControl(IntegerNameAndValue.ParameterName);
						if (RigControl && RigControl->ControlType == ERigControlType::Integer)
						{
							Section->GetControlRig()->SetControlValue<int32>(IntegerNameAndValue.ParameterName, IntegerNameAndValue.Value, true, EControlRigSetKey::Never);
						}
					}
				}
			}
			Section->SetDoNotKey(bWasDoNotKey);
		}

	}

	const UMovieSceneControlRigParameterSection* Section;
	/** Array of evaluated bool values */
	TArray<FBoolParameterStringAndValue, TInlineAllocator<2>> BoolValues;
	/** Array of evaluated integer values */
	TArray<FIntegerParameterStringAndValue, TInlineAllocator<2>> IntegerValues;

};


FMovieSceneControlRigParameterTemplate::FMovieSceneControlRigParameterTemplate(const UMovieSceneControlRigParameterSection& Section, const UMovieSceneControlRigParameterTrack& Track)
	: FMovieSceneParameterSectionTemplate(Section) , Enums(Section.GetEnumParameterNamesAndCurves()), Integers(Section.GetIntegerParameterNamesAndCurves())

{

}



struct TControlRigParameterActuatorFloat : TMovieSceneBlendingActuator<FControlRigTrackTokenFloat>
{
	TControlRigParameterActuatorFloat(FMovieSceneAnimTypeID& InAnimID, const FName& InParameterName, const UMovieSceneControlRigParameterSection* InSection)
		: TMovieSceneBlendingActuator<FControlRigTrackTokenFloat>(FMovieSceneBlendingActuatorID(InAnimID))
		, ParameterName(InParameterName)
		, SectionData(InSection)
	{}


	FControlRigTrackTokenFloat RetrieveCurrentValue(UObject* InObject, IMovieScenePlayer* Player) const
	{
		const UMovieSceneControlRigParameterSection* Section = SectionData.Get();

		if (Section && Section->GetControlRig())
		{
			FRigControl* RigControl = Section->GetControlRig()->FindControl(ParameterName);
			if (RigControl && RigControl->ControlType == ERigControlType::Float)
			{
				float Val = RigControl->Value.Get<float>();
				return FControlRigTrackTokenFloat(Val);
			}

		}
		return FControlRigTrackTokenFloat();
	}

	void Actuate(UObject* InObject, const FControlRigTrackTokenFloat& InFinalValue, const TBlendableTokenStack<FControlRigTrackTokenFloat>& OriginalStack, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		const UMovieSceneControlRigParameterSection* Section = SectionData.Get();
		
		bool bWasDoNotKey = false;
		if (Section)
		{
			bWasDoNotKey = Section->GetDoNotKey();
			Section->SetDoNotKey(true);

			if (Section->GetControlRig() && (Section->ControlsToSet.Num() == 0 || Section->ControlsToSet.Contains(ParameterName)))
			{
				FRigControl* RigControl = Section->GetControlRig()->FindControl(ParameterName);
				if (RigControl && RigControl->ControlType == ERigControlType::Float)
				{
					Section->GetControlRig()->SetControlValue<float>(ParameterName, InFinalValue.Value, true, EControlRigSetKey::Never);
				}
			}

			Section->SetDoNotKey(bWasDoNotKey);
		}
	}


	virtual void Actuate(FMovieSceneInterrogationData& InterrogationData, const FControlRigTrackTokenFloat& InValue, const TBlendableTokenStack<FControlRigTrackTokenFloat>& OriginalStack, const FMovieSceneContext& Context) const override
	{
		FFloatInterrogationData Data;
		Data.Val = InValue.Value;
		Data.ParameterName = ParameterName;
		InterrogationData.Add(FFloatInterrogationData(Data), UMovieSceneControlRigParameterSection::GetFloatInterrogationKey());
	}

	FName ParameterName;
	TWeakObjectPtr<const UMovieSceneControlRigParameterSection> SectionData;

};


struct TControlRigParameterActuatorVector2D : TMovieSceneBlendingActuator<FControlRigTrackTokenVector2D>
{
	TControlRigParameterActuatorVector2D(FMovieSceneAnimTypeID& InAnimID,  const FName& InParameterName, const UMovieSceneControlRigParameterSection* InSection)
		: TMovieSceneBlendingActuator<FControlRigTrackTokenVector2D>(FMovieSceneBlendingActuatorID(InAnimID))
		, ParameterName(InParameterName)
		, SectionData(InSection)
	{}



	FControlRigTrackTokenVector2D RetrieveCurrentValue(UObject* InObject, IMovieScenePlayer* Player) const
	{
		const UMovieSceneControlRigParameterSection* Section = SectionData.Get();

		if (Section && Section->GetControlRig())
		{
			FRigControl* RigControl = Section->GetControlRig()->FindControl(ParameterName);
			if (RigControl && (RigControl->ControlType == ERigControlType::Vector2D))
			{
				FVector2D Val = RigControl->Value.Get<FVector2D>();
				return FControlRigTrackTokenVector2D(Val);
			}
		}
		return FControlRigTrackTokenVector2D();
	}

	void Actuate(UObject* InObject, const FControlRigTrackTokenVector2D& InFinalValue, const TBlendableTokenStack<FControlRigTrackTokenVector2D>& OriginalStack, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		const UMovieSceneControlRigParameterSection* Section = SectionData.Get();

		bool bWasDoNotKey = false;
		if (Section)
		{
			bWasDoNotKey = Section->GetDoNotKey();
			Section->SetDoNotKey(true);

			if (Section->GetControlRig() && (Section->ControlsToSet.Num() == 0 || Section->ControlsToSet.Contains(ParameterName)))
			{
				FRigControl* RigControl = Section->GetControlRig()->FindControl(ParameterName);
				if (RigControl && (RigControl->ControlType == ERigControlType::Vector2D))
				{
					Section->GetControlRig()->SetControlValue<FVector2D>(ParameterName, InFinalValue.Value, true, EControlRigSetKey::Never);
				}
			}

			Section->SetDoNotKey(bWasDoNotKey);
		}
	}
	virtual void Actuate(FMovieSceneInterrogationData& InterrogationData, const FControlRigTrackTokenVector2D& InValue, const TBlendableTokenStack<FControlRigTrackTokenVector2D>& OriginalStack, const FMovieSceneContext& Context) const override
	{
		FVector2DInterrogationData Data;
		Data.Val = InValue.Value;
		Data.ParameterName = ParameterName;
		InterrogationData.Add(FVector2DInterrogationData(Data), UMovieSceneControlRigParameterSection::GetVector2DInterrogationKey());
	}

	FName ParameterName;
	TWeakObjectPtr<const UMovieSceneControlRigParameterSection> SectionData;

};


struct TControlRigParameterActuatorVector : TMovieSceneBlendingActuator<FControlRigTrackTokenVector>
{
	TControlRigParameterActuatorVector(FMovieSceneAnimTypeID& InAnimID, const FName& InParameterName, const UMovieSceneControlRigParameterSection* InSection)
		: TMovieSceneBlendingActuator<FControlRigTrackTokenVector>(FMovieSceneBlendingActuatorID(InAnimID))
		, ParameterName(InParameterName)
		, SectionData(InSection)
	{}



	FControlRigTrackTokenVector RetrieveCurrentValue(UObject* InObject, IMovieScenePlayer* Player) const
	{
		const UMovieSceneControlRigParameterSection* Section = SectionData.Get();

		if (Section->GetControlRig())
		{
			FRigControl* RigControl = Section->GetControlRig()->FindControl(ParameterName);
			if (RigControl && (RigControl->ControlType == ERigControlType::Position || RigControl->ControlType == ERigControlType::Scale || RigControl->ControlType == ERigControlType::Rotator))
			{
				FVector Val = RigControl->Value.Get<FVector>();
				return FControlRigTrackTokenVector(Val);
			}
		}
		return FControlRigTrackTokenVector();
	}

	void Actuate(UObject* InObject, const FControlRigTrackTokenVector& InFinalValue, const TBlendableTokenStack<FControlRigTrackTokenVector>& OriginalStack, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		const UMovieSceneControlRigParameterSection* Section = SectionData.Get();
		
		bool bWasDoNotKey = false;
		if (Section)
		{
			bWasDoNotKey = Section->GetDoNotKey();
			Section->SetDoNotKey(true);
			if (Section->GetControlRig() && (Section->ControlsToSet.Num() == 0 || Section->ControlsToSet.Contains(ParameterName)))
			{
				FRigControl* RigControl = Section->GetControlRig()->FindControl(ParameterName);
				if (RigControl && (RigControl->ControlType == ERigControlType::Position || RigControl->ControlType == ERigControlType::Scale || RigControl->ControlType == ERigControlType::Rotator))
				{
						Section->GetControlRig()->SetControlValue<FVector>(ParameterName, InFinalValue.Value, true, EControlRigSetKey::Never);
				}
			}

			Section->SetDoNotKey(bWasDoNotKey);
		}
	}
	virtual void Actuate(FMovieSceneInterrogationData& InterrogationData, const FControlRigTrackTokenVector& InValue, const TBlendableTokenStack<FControlRigTrackTokenVector>& OriginalStack, const FMovieSceneContext& Context) const override
	{
		FVectorInterrogationData Data;
		Data.Val = InValue.Value;
		Data.ParameterName = ParameterName;		
		InterrogationData.Add(FVectorInterrogationData(Data), UMovieSceneControlRigParameterSection::GetVectorInterrogationKey());
	}


	FName ParameterName;
	TWeakObjectPtr<const UMovieSceneControlRigParameterSection> SectionData;

};



struct TControlRigParameterActuatorTransform : TMovieSceneBlendingActuator<FControlRigTrackTokenTransform>
{
	TControlRigParameterActuatorTransform(FMovieSceneAnimTypeID& InAnimID, const FName& InParameterName, const UMovieSceneControlRigParameterSection* InSection)
		: TMovieSceneBlendingActuator<FControlRigTrackTokenTransform>(FMovieSceneBlendingActuatorID(InAnimID))
		, ParameterName(InParameterName)
		, SectionData(InSection)
	{}

	FControlRigTrackTokenTransform RetrieveCurrentValue(UObject* InObject, IMovieScenePlayer* Player) const
	{
		const UMovieSceneControlRigParameterSection* Section = SectionData.Get();

		if (Section->GetControlRig() && (Section->ControlsToSet.Num() == 0 || Section->ControlsToSet.Contains(ParameterName)))
		{
			FRigControl* RigControl = Section->GetControlRig()->FindControl(ParameterName);
			if (RigControl && RigControl->ControlType == ERigControlType::Transform)
			{
				FTransform Val = RigControl->Value.Get<FTransform>();
				return FControlRigTrackTokenTransform(Val);
			}
			else if(RigControl && RigControl->ControlType == ERigControlType::TransformNoScale)
			{
				FTransformNoScale ValNoScale = RigControl->Value.Get<FTransformNoScale>();
				FTransform Val = ValNoScale;
				return FControlRigTrackTokenTransform(Val);
			}
			else if (RigControl && RigControl->ControlType == ERigControlType::EulerTransform)
			{
				FEulerTransform Euler = RigControl->Value.Get<FEulerTransform>();
				FTransform Val = Euler.ToFTransform();
				return FControlRigTrackTokenTransform(Val);
		}
		}
		return FControlRigTrackTokenTransform();
	}

	void Actuate(UObject* InObject, const FControlRigTrackTokenTransform& InFinalValue, const TBlendableTokenStack<FControlRigTrackTokenTransform>& OriginalStack, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		const UMovieSceneControlRigParameterSection* Section = SectionData.Get();
		if (Section)
		{
			UMovieSceneTrack * Track = Cast<UMovieSceneTrack>(Section->GetOuter());
			if (Track && Track->GetSectionToKey())
			{
				Section = Cast<const UMovieSceneControlRigParameterSection>(Track->GetSectionToKey());
			}
		}

		bool bWasDoNotKey = false;
		if (Section)
		{
			bWasDoNotKey = Section->GetDoNotKey();
			Section->SetDoNotKey(true);
		
			if (Section->GetControlRig() && (Section->ControlsToSet.Num() == 0 || Section->ControlsToSet.Contains(ParameterName)))
			{
				FRigControl* RigControl = Section->GetControlRig()->FindControl(ParameterName);
				if (RigControl && RigControl->ControlType == ERigControlType::Transform)
				{
						Section->GetControlRig()->SetControlValue<FTransform>(ParameterName, InFinalValue.Value,true, EControlRigSetKey::Never);
				}
				else if (RigControl && RigControl->ControlType == ERigControlType::TransformNoScale)
				{
					FTransformNoScale NoScale = InFinalValue.Value;
						Section->GetControlRig()->SetControlValue<FTransformNoScale>(ParameterName, NoScale, true, EControlRigSetKey::Never);
				}
				else if (RigControl && RigControl->ControlType == ERigControlType::EulerTransform)
				{
					FEulerTransform Euler = InFinalValue.Value;
					Section->GetControlRig()->SetControlValue<FEulerTransform>(ParameterName, Euler, true, EControlRigSetKey::Never);
				}
			}

			Section->SetDoNotKey(bWasDoNotKey);
		}
	}
	virtual void Actuate(FMovieSceneInterrogationData& InterrogationData, const FControlRigTrackTokenTransform& InValue, const TBlendableTokenStack<FControlRigTrackTokenTransform>& OriginalStack, const FMovieSceneContext& Context) const override
	{
		FTransformInterrogationData Data;
		Data.Val = InValue.Value;
		Data.ParameterName = ParameterName;
		InterrogationData.Add(FTransformInterrogationData(Data), UMovieSceneControlRigParameterSection::GetTransformInterrogationKey());
	}
	FName ParameterName;
	TWeakObjectPtr<const UMovieSceneControlRigParameterSection> SectionData;

};


void FMovieSceneControlRigParameterTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	const FFrameTime Time = Context.GetTime();

	const UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(GetSourceSection());
	if (Section && Section->GetControlRig())
	{
		FEvaluatedControlRigParameterSectionChannelMasks* ChannelMasks = PersistentData.FindSectionData<FEvaluatedControlRigParameterSectionChannelMasks>();
		if (!ChannelMasks)
		{
			// Naughty const_cast here, but we can't create this inside Initialize because of hotfix restrictions
			// The cast is ok because we actually do not have any threading involved
			ChannelMasks = &const_cast<FPersistentEvaluationData&>(PersistentData).GetOrAddSectionData<FEvaluatedControlRigParameterSectionChannelMasks>();
			ChannelMasks->Initialize(Section, Scalars, Bools, Integers, Enums, Vector2Ds, Vectors, Colors, Transforms);
		}

		UMovieSceneTrack* Track = Cast<UMovieSceneTrack>(Section->GetOuter());
		if (!Track)
		{
			return;
		}


		//Do blended tokens
		FEvaluatedControlRigParameterSectionValues Values;

		HACK_ChannelMasks = ChannelMasks;
		EvaluateCurvesWithMasks(Context, Values);
		HACK_ChannelMasks = nullptr;

		float Weight = EvaluateEasing(Context.GetTime());
		if (EnumHasAllFlags(Section->TransformMask.GetChannels(), EMovieSceneTransformChannel::Weight))
		{
			float ManualWeight = 1.f;
			Section->Weight.Evaluate(Context.GetTime(), ManualWeight);
			Weight *= ManualWeight;
		}

		//Do basic token
		FControlRigParameterExecutionToken ExecutionToken(Section,Values);
		ExecutionTokens.Add(MoveTemp(ExecutionToken));

		FControlRigAnimTypeIDsPtr TypeIDs = FControlRigAnimTypeIDs::Get(Section->GetControlRig());

		for (const FScalarParameterStringAndValue& ScalarNameAndValue : Values.ScalarValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = TypeIDs->FindScalar(ScalarNameAndValue.ParameterName);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!ExecutionTokens.GetBlendingAccumulator().FindActuator< FControlRigTrackTokenFloat>(ActuatorTypeID))
			{
				ExecutionTokens.GetBlendingAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorFloat>(AnimTypeID, ScalarNameAndValue.ParameterName, Section));
			}
			ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<FControlRigTrackTokenFloat>(ScalarNameAndValue.Value, Section->GetBlendType().Get(), Weight));
		}

		UE::MovieScene::TMultiChannelValue<float, 3> VectorData;
		for (const FVectorParameterStringAndValue& VectorNameAndValue : Values.VectorValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = TypeIDs->FindVector(VectorNameAndValue.ParameterName);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!ExecutionTokens.GetBlendingAccumulator().FindActuator< FControlRigTrackTokenVector>(ActuatorTypeID))
			{
				ExecutionTokens.GetBlendingAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorVector>(AnimTypeID,  VectorNameAndValue.ParameterName,Section));
			}
			VectorData.Set(0, VectorNameAndValue.Value.X);
			VectorData.Set(1, VectorNameAndValue.Value.Y);
			VectorData.Set(2, VectorNameAndValue.Value.Z);

			ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<FControlRigTrackTokenVector>(VectorData, Section->GetBlendType().Get(), Weight));
		}

		UE::MovieScene::TMultiChannelValue<float, 2> Vector2DData;
		for (const FVector2DParameterStringAndValue& Vector2DNameAndValue : Values.Vector2DValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = TypeIDs->FindVector2D(Vector2DNameAndValue.ParameterName);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!ExecutionTokens.GetBlendingAccumulator().FindActuator< FControlRigTrackTokenVector2D>(ActuatorTypeID))
			{
				ExecutionTokens.GetBlendingAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorVector2D>(AnimTypeID,  Vector2DNameAndValue.ParameterName, Section));
			}
			Vector2DData.Set(0, Vector2DNameAndValue.Value.X);
			Vector2DData.Set(1, Vector2DNameAndValue.Value.Y);

			ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<FControlRigTrackTokenVector2D>(Vector2DData, Section->GetBlendType().Get(), Weight));
		}

		UE::MovieScene::TMultiChannelValue<float, 9> TransformData;
		for (const FTransformParameterStringAndValue& TransformNameAndValue : Values.TransformValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = TypeIDs->FindTransform(TransformNameAndValue.ParameterName);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!ExecutionTokens.GetBlendingAccumulator().FindActuator< FControlRigTrackTokenTransform>(ActuatorTypeID))
			{
				ExecutionTokens.GetBlendingAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorTransform>(AnimTypeID,  TransformNameAndValue.ParameterName, Section));
			}

			FTransform Transform(TransformNameAndValue.Rotation, TransformNameAndValue.Translation, TransformNameAndValue.Scale);

			TransformData.Set(0, TransformNameAndValue.Translation.X);
			TransformData.Set(1, TransformNameAndValue.Translation.Y);
			TransformData.Set(2, TransformNameAndValue.Translation.Z);

			TransformData.Set(3, TransformNameAndValue.Rotation.Roll);
			TransformData.Set(4, TransformNameAndValue.Rotation.Pitch);
			TransformData.Set(5, TransformNameAndValue.Rotation.Yaw);

			TransformData.Set(6, TransformNameAndValue.Scale.X);
			TransformData.Set(7, TransformNameAndValue.Scale.Y);
			TransformData.Set(8, TransformNameAndValue.Scale.Z);
			ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<FControlRigTrackTokenTransform>(TransformData, Section->GetBlendType().Get(), Weight));
		}

	}
}


void FMovieSceneControlRigParameterTemplate::EvaluateCurvesWithMasks(const FMovieSceneContext& Context, FEvaluatedControlRigParameterSectionValues& Values) const
{
	const FFrameTime Time = Context.GetTime();

	check(HACK_ChannelMasks);


	const UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(GetSourceSection());
	if (Section)
	{
		// Reserve the value arrays to avoid re-allocation
		Values.ScalarValues.Reserve(Scalars.Num());
		Values.BoolValues.Reserve(Bools.Num());
		Values.IntegerValues.Reserve(Integers.Num() + Enums.Num()); // Both enums and integers output to the integer value array
		Values.Vector2DValues.Reserve(Vector2Ds.Num());
		Values.VectorValues.Reserve(Vectors.Num());
		Values.ColorValues.Reserve(Colors.Num());
		Values.TransformValues.Reserve(Transforms.Num());

		// Populate each of the output arrays in turn
		for (int32 Index = 0; Index < Scalars.Num(); ++Index)
		{
			float Value = 0;

			const FScalarParameterNameAndCurve& Scalar = this->Scalars[Index];
			if (HACK_ChannelMasks->ScalarCurveMask[Index])
			{
				Scalar.ParameterCurve.Evaluate(Time, Value);
			}
			
			Values.ScalarValues.Emplace(Scalar.ParameterName, Value);
		}

		for (int32 Index = 0; Index < Bools.Num(); ++Index)
		{
			bool Value = false;

			const FBoolParameterNameAndCurve& Bool = Bools[Index];
			if (HACK_ChannelMasks->BoolCurveMask[Index])
			{
				Bool.ParameterCurve.Evaluate(Time, Value);
			}

			Values.BoolValues.Emplace(Bool.ParameterName, Value);
		}
		for (int32 Index = 0; Index < Integers.Num(); ++Index)
		{
			int32 Value = 0;

			const FIntegerParameterNameAndCurve& Integer = Integers[Index];
			if (HACK_ChannelMasks->IntegerCurveMask[Index])
			{
				Integer.ParameterCurve.Evaluate(Time, Value);
			}

			Values.IntegerValues.Emplace(Integer.ParameterName, Value);
		}
		for (int32 Index = 0; Index < Enums.Num(); ++Index)
		{
			uint8 Value = 0;

			const FEnumParameterNameAndCurve& Enum = Enums[Index];
			if (HACK_ChannelMasks->EnumCurveMask[Index])
			{
				Enum.ParameterCurve.Evaluate(Time, Value);
			}
			Values.IntegerValues.Emplace(Enum.ParameterName, (int32)Value);

		}
		for (int32 Index = 0; Index < Vector2Ds.Num(); ++Index)
		{
			FVector2D Value(ForceInitToZero);

			const FVector2DParameterNameAndCurves& Vector2D = Vector2Ds[Index];
			if (HACK_ChannelMasks->Vector2DCurveMask[Index])
			{
				Vector2D.XCurve.Evaluate(Time, Value.X);
				Vector2D.YCurve.Evaluate(Time, Value.Y);
			}

			Values.Vector2DValues.Emplace(Vector2D.ParameterName, Value);
		}
		for (int32 Index = 0; Index < Vectors.Num(); ++Index)
		{
			FVector Value(ForceInitToZero);

			const FVectorParameterNameAndCurves& Vector = Vectors[Index];
			if (HACK_ChannelMasks->VectorCurveMask[Index])
			{
				Vector.XCurve.Evaluate(Time, Value.X);
				Vector.YCurve.Evaluate(Time, Value.Y);
				Vector.ZCurve.Evaluate(Time, Value.Z);
			}

			Values.VectorValues.Emplace(Vector.ParameterName, Value);
		}
		for (int32 Index = 0; Index < Colors.Num(); ++Index)
		{
			FLinearColor ColorValue = FLinearColor::White;

			const FColorParameterNameAndCurves& Color = Colors[Index];
			if (HACK_ChannelMasks->ColorCurveMask[Index])
			{
				Color.RedCurve.Evaluate(Time, ColorValue.R);
				Color.GreenCurve.Evaluate(Time, ColorValue.G);
				Color.BlueCurve.Evaluate(Time, ColorValue.B);
				Color.AlphaCurve.Evaluate(Time, ColorValue.A);
			}
		
			Values.ColorValues.Emplace(Color.ParameterName, ColorValue);
		}

		EMovieSceneTransformChannel ChannelMask = Section->GetTransformMask().GetChannels();
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			FVector Translation(ForceInitToZero), Scale(FVector::OneVector);
			FRotator Rotator(0.0f, 0.0f, 0.0f);

			const FTransformParameterNameAndCurves& Transform = Transforms[Index];
			if (HACK_ChannelMasks->TransformCurveMask[Index])
			{
				if (EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::TranslationX))
				{
					Transform.Translation[0].Evaluate(Time, Translation[0]);
				}
				if (EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::TranslationY))
				{
					Transform.Translation[1].Evaluate(Time, Translation[1]);
				}
				if (EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::TranslationZ))
				{
					Transform.Translation[2].Evaluate(Time, Translation[2]);
				}
				if (EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::RotationX))
				{
					Transform.Rotation[0].Evaluate(Time, Rotator.Roll);
				}
				if (EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::RotationY))
				{
					Transform.Rotation[1].Evaluate(Time, Rotator.Pitch);
				}
				if (EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::RotationZ))
				{
					Transform.Rotation[2].Evaluate(Time, Rotator.Yaw);
				}
				//mz todo quat interp...
				if (EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::ScaleX))
				{
					Transform.Scale[0].Evaluate(Time, Scale[0]);
				}
				if (EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::ScaleY))
				{
					Transform.Scale[1].Evaluate(Time, Scale[1]);
				}
				if (EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::ScaleZ))
				{
					Transform.Scale[2].Evaluate(Time, Scale[2]);
				}
			}
			FTransformParameterStringAndValue NameAndValue(Transform.ParameterName, Translation, Rotator, Scale);
			Values.TransformValues.Emplace(NameAndValue);
		}
	}
}


FMovieSceneAnimTypeID FMovieSceneControlRigParameterTemplate::GetAnimTypeID()
{
	return TMovieSceneAnimTypeID<FMovieSceneControlRigParameterTemplate>();
}


void FMovieSceneControlRigParameterTemplate::Interrogate(const FMovieSceneContext& Context, FMovieSceneInterrogationData& Container, UObject* BindingOverride) const
{
	MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_ControlRigTemplateParameter_Evaluate)

	const FFrameTime Time = Context.GetTime();

	const UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(GetSourceSection());
	if (Section && Section->GetControlRig())
	{
		FEvaluatedControlRigParameterSectionChannelMasks ChannelMasks;
		ChannelMasks.Initialize(Section, Scalars, Bools, Integers, Enums, Vector2Ds, Vectors, Colors, Transforms);

		//Do blended tokens
		FEvaluatedControlRigParameterSectionValues Values;

		HACK_ChannelMasks = &ChannelMasks;
		EvaluateCurvesWithMasks(Context, Values);
		HACK_ChannelMasks = nullptr;

		FControlRigAnimTypeIDsPtr TypeIDs = FControlRigAnimTypeIDs::Get(Section->GetControlRig());

		float Weight = 1.f;

		//float Weight = EvaluateEasing(Context.GetTime());
	//if (EnumHasAllFlags(TemplateData.Mask.GetChannels(), EMovieSceneTransformChannel::Weight))
	//{
	//	float ManualWeight = 1.f;
	//	TemplateData.ManualWeight.Evaluate(Context.GetTime(), ManualWeight);
	//	Weight *= ManualWeight;
	//}


		for (const FScalarParameterStringAndValue& ScalarNameAndValue : Values.ScalarValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = TypeIDs->FindScalar(ScalarNameAndValue.ParameterName);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!Container.GetAccumulator().FindActuator< FControlRigTrackTokenFloat>(ActuatorTypeID))
			{
				Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorFloat>(AnimTypeID, ScalarNameAndValue.ParameterName, Section));
			}
			Container.GetAccumulator().BlendToken(FMovieSceneEvaluationOperand(), ActuatorTypeID, FMovieSceneEvaluationScope(), Context,TBlendableToken<FControlRigTrackTokenFloat>(ScalarNameAndValue.Value, Section->GetBlendType().Get(), Weight));
		}

		UE::MovieScene::TMultiChannelValue<float, 2> Vector2DData;
		for (const FVector2DParameterStringAndValue& Vector2DNameAndValue : Values.Vector2DValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = TypeIDs->FindVector2D(Vector2DNameAndValue.ParameterName);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!Container.GetAccumulator().FindActuator< FControlRigTrackTokenVector>(ActuatorTypeID))
			{
				Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorVector2D>(AnimTypeID, Vector2DNameAndValue.ParameterName, Section));
			}
			Vector2DData.Set(0, Vector2DNameAndValue.Value.X);
			Vector2DData.Set(1, Vector2DNameAndValue.Value.Y);

			Container.GetAccumulator().BlendToken(FMovieSceneEvaluationOperand(), ActuatorTypeID, FMovieSceneEvaluationScope(), Context, TBlendableToken<FControlRigTrackTokenVector2D>(Vector2DData, Section->GetBlendType().Get(), Weight));
		}

		UE::MovieScene::TMultiChannelValue<float, 3> VectorData;
		for (const FVectorParameterStringAndValue& VectorNameAndValue : Values.VectorValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = TypeIDs->FindVector(VectorNameAndValue.ParameterName);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!Container.GetAccumulator().FindActuator< FControlRigTrackTokenVector>(ActuatorTypeID))
			{
				Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorVector>(AnimTypeID, VectorNameAndValue.ParameterName, Section));
			}
			VectorData.Set(0, VectorNameAndValue.Value.X);
			VectorData.Set(1, VectorNameAndValue.Value.Y);
			VectorData.Set(2, VectorNameAndValue.Value.Z);

			Container.GetAccumulator().BlendToken(FMovieSceneEvaluationOperand(), ActuatorTypeID, FMovieSceneEvaluationScope(),Context,TBlendableToken<FControlRigTrackTokenVector>(VectorData, Section->GetBlendType().Get(), Weight));
		}

		UE::MovieScene::TMultiChannelValue<float, 9> TransformData;
		for (const FTransformParameterStringAndValue& TransformNameAndValue : Values.TransformValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = TypeIDs->FindTransform(TransformNameAndValue.ParameterName);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!Container.GetAccumulator().FindActuator< FControlRigTrackTokenTransform>(ActuatorTypeID))
			{
				Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorTransform>(AnimTypeID, TransformNameAndValue.ParameterName, Section));
			}

			FTransform Transform(TransformNameAndValue.Rotation, TransformNameAndValue.Translation, TransformNameAndValue.Scale);

			TransformData.Set(0, TransformNameAndValue.Translation.X);
			TransformData.Set(1, TransformNameAndValue.Translation.Y);
			TransformData.Set(2, TransformNameAndValue.Translation.Z);

			TransformData.Set(3, TransformNameAndValue.Rotation.Roll);
			TransformData.Set(4, TransformNameAndValue.Rotation.Pitch);
			TransformData.Set(5, TransformNameAndValue.Rotation.Yaw);

			TransformData.Set(6, TransformNameAndValue.Scale.X);
			TransformData.Set(7, TransformNameAndValue.Scale.Y);
			TransformData.Set(8, TransformNameAndValue.Scale.Z);
			Container.GetAccumulator().BlendToken(FMovieSceneEvaluationOperand(),ActuatorTypeID, FMovieSceneEvaluationScope(), Context, TBlendableToken<FControlRigTrackTokenTransform>(TransformData, Section->GetBlendType().Get(), Weight));
		}

	}
}

