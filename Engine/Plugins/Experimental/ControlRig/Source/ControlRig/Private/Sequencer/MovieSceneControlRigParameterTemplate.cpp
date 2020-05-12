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
#include "ControlRigSkeletalMeshBinding.h"
#include "Evaluation/Blending/BlendableTokenStack.h"
#include "Evaluation/Blending/MovieSceneBlendingActuatorID.h"
#include "TransformNoScale.h"

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
		ParameterString = InParameterName.ToString();		
		Value = InValue;
	}

	/** The name of the scalar parameter. */
	FString ParameterString;
	FName ParameterName;
	/** The animated value of the scalar parameter. */
	float Value;
};

/**
 * Structure representing the animated value of a scalar parameter.
 */
struct FBoolParameterStringAndValue
{
	/** Creates a new FBoolParameterAndValue with a parameter name and a value. */
	FBoolParameterStringAndValue(FName InParameterName, bool InValue)
	{
		ParameterName = InParameterName;
		ParameterString = InParameterName.ToString();		
		Value = InValue;
	}

	/** The name of the bool parameter. */
	FString ParameterString;
	FName ParameterName;
	/** The animated value of the bool parameter. */
	bool Value;
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
		ParameterString = InParameterName.ToString();
		Value = InValue;
	}

	/** The name of the vector2D parameter. */
	FString ParameterString;
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
		ParameterString = InParameterName.ToString();
		Value = InValue;
	}

	/** The name of the vector parameter. */
	FString ParameterString;
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
		ParameterString = InParameterName.ToString();
		Value = InValue;
	}

	/** The name of the color parameter. */
	FString ParameterString;
	FName ParameterName;

	/** The animated value of the color parameter. */
	FLinearColor Value;
};

struct FTransformParameterStringAndValue
{

	/** The name of the transform  parameter. */
	FString ParameterString;
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
		ParameterString = InParameterName.ToString();
	}
};

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
	/** Array of evaluated scalar values */
	TArray<FBoolParameterStringAndValue, TInlineAllocator<2>> BoolValues;
	/** Array of evaluated scalar values */
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
	typedef MovieScene::TMaskedBlendable<float, 1> WorkingDataType;
};

template<>  struct TBlendableTokenTraits<FControlRigTrackTokenBool>
{
	typedef MovieScene::TMaskedBlendable<bool, 1> WorkingDataType;
};

template<> struct TBlendableTokenTraits<FControlRigTrackTokenVector2D>
{
	typedef MovieScene::TMaskedBlendable<float, 2> WorkingDataType;
};

template<> struct TBlendableTokenTraits<FControlRigTrackTokenVector>
{
	typedef MovieScene::TMaskedBlendable<float, 3> WorkingDataType;
};

template<>  struct TBlendableTokenTraits<FControlRigTrackTokenTransform>
{
	typedef MovieScene::TMaskedBlendable<float, 9> WorkingDataType;
};




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
}

namespace FControlRigBindingHelper
{
	void BindToSequencerInstance(UControlRig* ControlRig)
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
					ControlRig->CreateRigControlsForCurveContainer();
					ControlRig->Initialize();
				}
			}
		}
	}

	void UnBindFromSequencerInstance(UControlRig* ControlRig)
	{
		check(ControlRig);
		if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
		{
			UControlRigLayerInstance* AnimInstance = Cast<UControlRigLayerInstance>(SkeletalMeshComponent->GetAnimInstance());
			if (AnimInstance)
			{
				AnimInstance->ResetNodes();
				AnimInstance->RecalcRequiredBones();
				AnimInstance->RemoveControlRigTrack(ControlRig->GetUniqueID());
			}

			FAnimCustomInstanceHelper::UnbindFromSkeletalMeshComponent< UControlRigLayerInstance>(SkeletalMeshComponent);
		}
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
#if WITH_EDITOR
				,bUpdateAnimationInEditor(true)
#endif
			{

			}
			void SetSkelMesh(USkeletalMeshComponent* InComponent)
			{
#if WITH_EDITOR
				bUpdateAnimationInEditor = InComponent->GetUpdateAnimationInEditor();
#endif
			}

			virtual void RestoreState(UObject& InObject, IMovieScenePlayer& Player) override
			{

				if (UControlRig* ControlRig = Cast<UControlRig>(&InObject))
				{
					if (ControlRig->GetObjectBinding())
					{
						if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
						{
#if WITH_EDITOR
							SkeletalMeshComponent->SetUpdateAnimationInEditor(bUpdateAnimationInEditor);
#endif
						}
						FControlRigBindingHelper::UnBindFromSequencerInstance(ControlRig);
						for (TNameAndValue<float>& Value : ScalarValues)
						{
							if (ControlRig->FindControl(Value.Name))
							{
								ControlRig->SetControlValue<float>(Value.Name, Value.Value, true, EControlRigSetKey::Never);
							}
						}

						for (TNameAndValue<bool>& Value : BoolValues)
						{
							if (ControlRig->FindControl(Value.Name))
							{
								ControlRig->SetControlValue<bool>(Value.Name, Value.Value, true, EControlRigSetKey::Never);
							}
						}

						for (TNameAndValue<FVector2D>& Value : Vector2DValues)
						{
							if (ControlRig->FindControl(Value.Name))
							{
								ControlRig->SetControlValue<FVector2D>(Value.Name, Value.Value, true, EControlRigSetKey::Never);
							}
						}

						for (TNameAndValue<FVector>& Value : VectorValues)
						{
							if (ControlRig->FindControl(Value.Name))
							{
								ControlRig->SetControlValue<FVector>(Value.Name, Value.Value, true, EControlRigSetKey::Never);
							}
						}

						for (TNameAndValue<FTransform>& Value : TransformValues)
						{
							if (ControlRig->FindControl(Value.Name))
							{
								ControlRig->SetControlValue<FTransform>(Value.Name, Value.Value, true, EControlRigSetKey::Never);
							}
						}
						ControlRig->GetObjectBinding()->UnbindFromObject();
					}
				}
			}

			FMovieSceneSequenceID SequenceID;
			TArray< TNameAndValue<float> > ScalarValues;
			TArray< TNameAndValue<bool> > BoolValues;
			TArray< TNameAndValue<FVector> > VectorValues;
			TArray< TNameAndValue<FVector2D> > Vector2DValues;
			TArray< TNameAndValue<FTransform> > TransformValues;
#if WITH_EDITOR
			bool bUpdateAnimationInEditor;
#endif
		};


		FToken Token(SequenceID);

		if (UControlRig* ControlRig = Cast<UControlRig>(&Object))
		{
			if (ControlRig->GetObjectBinding())
			{
				if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
				{
					if (SkeletalMeshComponent)
					{
						Token.SetSkelMesh(SkeletalMeshComponent);
					}
				}
			}


			/*
			//mz todo the other types
			just left is bool
			*/

			const TArray<FRigControl>& Controls = ControlRig->AvailableControls();
			FRigControlValue Value;
			for (const FRigControl& RigControl : Controls)
			{
				switch (RigControl.ControlType)
				{
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
				}
			}
		}

		return MoveTemp(Token);
	}

	FMovieSceneSequenceID SequenceID;
	TArray< TNameAndValue<bool> > BoolValues;
	TArray< TNameAndValue<float> > ScalarValues;
	TArray< TNameAndValue<FVector2D> > Vector2DValues;
	TArray< TNameAndValue<FVector> > VectorValues;
	TArray< TNameAndValue<FTransform> > TransformValues;

};



/* Simple token used for non-blendables*/
struct FControlRigParameterExecutionToken : IMovieSceneExecutionToken
{
	FControlRigParameterExecutionToken(const UMovieSceneControlRigParameterSection* InSection)
	:	Section(InSection)
	{}
	FControlRigParameterExecutionToken(FControlRigParameterExecutionToken&&) = default;
	FControlRigParameterExecutionToken& operator=(FControlRigParameterExecutionToken&&) = default;

	// Non-copyable
	FControlRigParameterExecutionToken(const FControlRigParameterExecutionToken&) = delete;
	FControlRigParameterExecutionToken& operator=(const FControlRigParameterExecutionToken&) = delete;

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_ControlRigParameterTrack_TokenExecute)
		
		FInputBlendPose DefaultBoneFilter;

		bool bAdditive = false;
		bool bApplyBoneFilter = false;
		const FInputBlendPose* BoneFilter = &DefaultBoneFilter;

		FMovieSceneSequenceID SequenceID = Operand.SequenceID;
		UControlRig* ControlRig = Section->ControlRig;

		// Update the animation's state
		
		if (ControlRig)
		{
			const UMovieSceneSequence* Sequence = Player.State.FindSequence(Operand.SequenceID);
			TArrayView<TWeakObjectPtr<>> BoundSkelMeshes = Player.FindBoundObjects(Operand);

			if (Sequence && BoundSkelMeshes.Num() > 0 && BoundSkelMeshes[0].Get())
			{
				if (!ControlRig->GetObjectBinding())
				{
					ControlRig->SetObjectBinding(MakeShared<FControlRigSkeletalMeshBinding>());
				}
				if (!ControlRig->GetObjectBinding()->GetBoundObject())
				{
					ControlRig->GetObjectBinding()->BindToObject(BoundSkelMeshes[0].Get());
					ControlRig->Initialize();
				}
				FControlRigBindingHelper::BindToSequencerInstance(ControlRig);
				//MZ TODO HANDLE BOOLS AND OTHER NON BLENDABLES
				if (ControlRig->GetObjectBinding())
				{
					if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
					{
						if (UControlRigLayerInstance* AnimInstance = Cast<UControlRigLayerInstance>(SkeletalMeshComponent->GetAnimInstance()))
						{
							float Weight = 1.0f;
							FControlRigIOSettings InputSettings;
							InputSettings.bUpdateCurves = false;
							InputSettings.bUpdatePose = true;
							AnimInstance->UpdateControlRigTrack(ControlRig->GetUniqueID(), Weight, InputSettings, true);
						}
					}
				}
			}
		}

		// ensure that pre animated state is saved
		Player.SavePreAnimatedState(*ControlRig, FMovieSceneControlRigParameterTemplate::GetAnimTypeID(), FControlRigParameterPreAnimatedTokenProducer(Operand.SequenceID));
		
	}

	const UMovieSceneControlRigParameterSection* Section;

};


FMovieSceneControlRigParameterTemplate::FMovieSceneControlRigParameterTemplate(const UMovieSceneControlRigParameterSection& Section, const UMovieSceneControlRigParameterTrack& Track)
	: FMovieSceneParameterSectionTemplate(Section) 
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
		const UMovieSceneControlRigParameterSection* Section = nullptr;
		Section = SectionData.Get();
		if (Section && Section->ControlRig)
		{
			FRigControl* RigControl = Section->ControlRig->FindControl(ParameterName);
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
		const UMovieSceneControlRigParameterSection* Section = nullptr;
		Section = SectionData.Get();
		
		bool bWasDoNotKey = false;
		if (Section)
		{
			bWasDoNotKey = Section->GetDoNotKey();
			Section->SetDoNotKey(true);
		
			if (Section->ControlRig)
			{
				FRigControl* RigControl = Section->ControlRig->FindControl(ParameterName);
				if (RigControl && RigControl->ControlType == ERigControlType::Float)
				{
					Section->ControlRig->SetControlValue<float>(ParameterName, InFinalValue.Value, true, EControlRigSetKey::Never);
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
		const UMovieSceneControlRigParameterSection* Section = nullptr;
		Section = SectionData.Get();
		{
			FRigControl* RigControl = Section->ControlRig->FindControl(ParameterName);
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
		const UMovieSceneControlRigParameterSection* Section = nullptr;
		Section = SectionData.Get();

		bool bWasDoNotKey = false;
		if (Section)
		{
			bWasDoNotKey = Section->GetDoNotKey();
			Section->SetDoNotKey(true);
	
			if (Section->ControlRig)
			{
				FRigControl* RigControl = Section->ControlRig->FindControl(ParameterName);
				if (RigControl && (RigControl->ControlType == ERigControlType::Vector2D))
				{
					Section->ControlRig->SetControlValue<FVector2D>(ParameterName, InFinalValue.Value, true, EControlRigSetKey::Never);
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
		const UMovieSceneControlRigParameterSection* Section = nullptr;
		Section = SectionData.Get();
		if (Section->ControlRig)
		{
			FRigControl* RigControl = Section->ControlRig->FindControl(ParameterName);
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
		const UMovieSceneControlRigParameterSection* Section = nullptr;
		Section = SectionData.Get();
		
		bool bWasDoNotKey = false;
		if (Section)
		{
			bWasDoNotKey = Section->GetDoNotKey();
			Section->SetDoNotKey(true);

			if (Section->ControlRig)
			{
				FRigControl* RigControl = Section->ControlRig->FindControl(ParameterName);
				if (RigControl && (RigControl->ControlType == ERigControlType::Position || RigControl->ControlType == ERigControlType::Scale || RigControl->ControlType == ERigControlType::Rotator))
				{
					Section->ControlRig->SetControlValue<FVector>(ParameterName, InFinalValue.Value, true, EControlRigSetKey::Never);
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
		const UMovieSceneControlRigParameterSection* Section = nullptr;

		Section = SectionData.Get();
		if (Section->ControlRig)
		{
			FRigControl* RigControl = Section->ControlRig->FindControl(ParameterName);
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
		}
		return FControlRigTrackTokenTransform();
	}

	void Actuate(UObject* InObject, const FControlRigTrackTokenTransform& InFinalValue, const TBlendableTokenStack<FControlRigTrackTokenTransform>& OriginalStack, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		const UMovieSceneControlRigParameterSection* Section = nullptr;
		
		Section = SectionData.Get();
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
		
			if (Section->ControlRig)
			{
				FRigControl* RigControl = Section->ControlRig->FindControl(ParameterName);
				if (RigControl && RigControl->ControlType == ERigControlType::Transform)
				{
					Section->ControlRig->SetControlValue<FTransform>(ParameterName, InFinalValue.Value,true, EControlRigSetKey::Never);
				}
				else if (RigControl && RigControl->ControlType == ERigControlType::TransformNoScale)
				{
					FTransformNoScale NoScale = InFinalValue.Value;
					Section->ControlRig->SetControlValue<FTransformNoScale>(ParameterName, NoScale, true, EControlRigSetKey::Never);
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
	if (Section)
	{
		//Do basic token
		FControlRigParameterExecutionToken ExecutionToken(Section);
		ExecutionTokens.Add(MoveTemp(ExecutionToken));

		//Do blended tokens
		FEvaluatedControlRigParameterSectionValues Values;
		EvaluateCurvesWithMasks(Context, Values);
		static TMovieSceneAnimTypeIDContainer<FString> ScalarAnimTypeIDsByName;
		static TMovieSceneAnimTypeIDContainer<FString> BoolAnimTypeIDsByName;
		static TMovieSceneAnimTypeIDContainer<FString> VectorAnimTypeIDsByName;
		static TMovieSceneAnimTypeIDContainer<FString> Vector2DAnimTypeIDsByName;
		static TMovieSceneAnimTypeIDContainer<FString> TransformAnimTypeIDsByName;

		float Weight = EvaluateEasing(Context.GetTime());
		if (EnumHasAllFlags(Section->TransformMask.GetChannels(), EMovieSceneTransformChannel::Weight))
		{
			float ManualWeight = 1.f;
			Section->Weight.Evaluate(Context.GetTime(), ManualWeight);
			Weight *= ManualWeight;
		}

		//Do Bool straight up no blending
		bool bWasDoNotKey = false;
	
		bWasDoNotKey = Section->GetDoNotKey();
		Section->SetDoNotKey(true);
		
		if (Section->ControlRig)
		{
			for (const FBoolParameterStringAndValue& BoolNameAndValue : Values.BoolValues)
			{
				FRigControl* RigControl = Section->ControlRig->FindControl(BoolNameAndValue.ParameterName);
				if (RigControl && RigControl->ControlType == ERigControlType::Bool)
				{
					Section->ControlRig->SetControlValue<bool>(BoolNameAndValue.ParameterName, BoolNameAndValue.Value, true, EControlRigSetKey::Never);
				}
			}
		}
		Section->SetDoNotKey(bWasDoNotKey);
		
		
		uint32 OperandHash = GetTypeHash(Operand);
		FString UniqueActuator(FString::FromInt((int32)OperandHash));

		for (const FScalarParameterStringAndValue& ScalarNameAndValue : Values.ScalarValues)
		{
			FString NewString(ScalarNameAndValue.ParameterString);
			NewString.Append(UniqueActuator);
			FMovieSceneAnimTypeID AnimTypeID = ScalarAnimTypeIDsByName.GetAnimTypeID(NewString);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!ExecutionTokens.GetBlendingAccumulator().FindActuator< FControlRigTrackTokenFloat>(ActuatorTypeID))
			{
				ExecutionTokens.GetBlendingAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorFloat>(AnimTypeID, ScalarNameAndValue.ParameterName,Section));
			}
			ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<FControlRigTrackTokenFloat>(ScalarNameAndValue.Value, Section->GetBlendType().Get(), Weight));
		}

		MovieScene::TMultiChannelValue<float, 3> VectorData;
		for (const FVectorParameterStringAndValue& VectorNameAndValue : Values.VectorValues)
		{
			FString NewString(VectorNameAndValue.ParameterString);
			NewString.Append(UniqueActuator);
			FMovieSceneAnimTypeID AnimTypeID = VectorAnimTypeIDsByName.GetAnimTypeID(NewString);
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

		MovieScene::TMultiChannelValue<float, 2> Vector2DData;
		for (const FVector2DParameterStringAndValue& Vector2DNameAndValue : Values.Vector2DValues)
		{
			FString NewString(Vector2DNameAndValue.ParameterString);
			NewString.Append(UniqueActuator);
			FMovieSceneAnimTypeID AnimTypeID = Vector2DAnimTypeIDsByName.GetAnimTypeID(NewString);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!ExecutionTokens.GetBlendingAccumulator().FindActuator< FControlRigTrackTokenVector2D>(ActuatorTypeID))
			{
				ExecutionTokens.GetBlendingAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorVector2D>(AnimTypeID,  Vector2DNameAndValue.ParameterName, Section));
			}
			Vector2DData.Set(0, Vector2DNameAndValue.Value.X);
			Vector2DData.Set(1, Vector2DNameAndValue.Value.Y);

			ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<FControlRigTrackTokenVector2D>(Vector2DData, Section->GetBlendType().Get(), Weight));
		}

		MovieScene::TMultiChannelValue<float, 9> TransformData;
		for (const FTransformParameterStringAndValue& TransformNameAndValue : Values.TransformValues)
		{
			FString NewString(TransformNameAndValue.ParameterString);
			NewString.Append(UniqueActuator);
			FMovieSceneAnimTypeID AnimTypeID = TransformAnimTypeIDsByName.GetAnimTypeID(NewString);
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


	const UMovieSceneControlRigParameterSection* Section = nullptr;
	if (GetSourceSection())
	{
		Section = Cast<UMovieSceneControlRigParameterSection>(GetSourceSection());
		TArray<bool> ControlsMask = Section->GetControlsMask();

		//mz todo optimize this, don't want to do this map search every tick, will cache on the NameAndCurve objects
		const FChannelMapInfo* ChannelInfo = nullptr;
		for (const FScalarParameterNameAndCurve& Scalar : Scalars)
		{
			float Value = 0;
			ChannelInfo = Section->ControlChannelMap.Find(Scalar.ParameterName);
			if (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex])
			{
				Scalar.ParameterCurve.Evaluate(Time, Value);
			}
			
			Values.ScalarValues.Emplace(Scalar.ParameterName, Value);
		}

		for (const FBoolParameterNameAndCurve& Bool : Bools)
		{
			bool Value = false;
			ChannelInfo = Section->ControlChannelMap.Find(Bool.ParameterName);
			if (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex])
			{
				Bool.ParameterCurve.Evaluate(Time, Value);
			}

			Values.BoolValues.Emplace(Bool.ParameterName, Value);
		}

		for (const FVector2DParameterNameAndCurves& Vector2D : Vector2Ds)
		{
			FVector2D Value(ForceInitToZero);
			ChannelInfo = Section->ControlChannelMap.Find(Vector2D.ParameterName);
			if (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex])
			{
				Vector2D.XCurve.Evaluate(Time, Value.X);
				Vector2D.YCurve.Evaluate(Time, Value.Y);
			}

			Values.Vector2DValues.Emplace(Vector2D.ParameterName, Value);
		}

		for (const FVectorParameterNameAndCurves& Vector : Vectors)
		{
			FVector Value(ForceInitToZero);
			ChannelInfo = Section->ControlChannelMap.Find(Vector.ParameterName);
			if (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex])
			{
				Vector.XCurve.Evaluate(Time, Value.X);
				Vector.YCurve.Evaluate(Time, Value.Y);
				Vector.ZCurve.Evaluate(Time, Value.Z);
			}

			Values.VectorValues.Emplace(Vector.ParameterName, Value);
		}

		for (const FColorParameterNameAndCurves& Color : Colors)
		{
			FLinearColor ColorValue = FLinearColor::White;
			ChannelInfo = Section->ControlChannelMap.Find(Color.ParameterName);
			if (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex])
			{
				Color.RedCurve.Evaluate(Time, ColorValue.R);
				Color.GreenCurve.Evaluate(Time, ColorValue.G);
				Color.BlueCurve.Evaluate(Time, ColorValue.B);
				Color.AlphaCurve.Evaluate(Time, ColorValue.A);
			}
		
			Values.ColorValues.Emplace(Color.ParameterName, ColorValue);
		}

		for (const FTransformParameterNameAndCurves& Transform : Transforms)
		{
			FVector Translation(ForceInitToZero), Scale(FVector::OneVector);
			FRotator Rotator(0.0f, 0.0f, 0.0f);
			EMovieSceneTransformChannel ChannelMask = Section->GetTransformMask().GetChannels();
			ChannelInfo = Section->ControlChannelMap.Find(Transform.ParameterName);

			if (!ChannelInfo || ControlsMask[ChannelInfo->ControlIndex])
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

	const UMovieSceneControlRigParameterSection* Section = nullptr;
	if (GetSourceSection())
	{
		Section = Cast<UMovieSceneControlRigParameterSection>(GetSourceSection());

		//Do blended tokens
		FEvaluatedControlRigParameterSectionValues Values;
		EvaluateCurvesWithMasks(Context, Values);
		static TMovieSceneAnimTypeIDContainer<FString> ScalarAnimTypeIDsByName;
		static TMovieSceneAnimTypeIDContainer<FString> BoolAnimTypeIDsByName;
		static TMovieSceneAnimTypeIDContainer<FString> Vector2DAnimTypeIDsByName;
		static TMovieSceneAnimTypeIDContainer<FString> VectorAnimTypeIDsByName;
		static TMovieSceneAnimTypeIDContainer<FString> TransformAnimTypeIDsByName;


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
			FMovieSceneAnimTypeID AnimTypeID = ScalarAnimTypeIDsByName.GetAnimTypeID(ScalarNameAndValue.ParameterString);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!Container.GetAccumulator().FindActuator< FControlRigTrackTokenFloat>(ActuatorTypeID))
			{
				Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorFloat>(AnimTypeID, ScalarNameAndValue.ParameterName, Section));
			}
			Container.GetAccumulator().BlendToken(FMovieSceneEvaluationOperand(), ActuatorTypeID, FMovieSceneEvaluationScope(), Context,TBlendableToken<FControlRigTrackTokenFloat>(ScalarNameAndValue.Value, Section->GetBlendType().Get(), Weight));
		}

		MovieScene::TMultiChannelValue<float, 2> Vector2DData;
		for (const FVector2DParameterStringAndValue& Vector2DNameAndValue : Values.Vector2DValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = Vector2DAnimTypeIDsByName.GetAnimTypeID(Vector2DNameAndValue.ParameterString);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!Container.GetAccumulator().FindActuator< FControlRigTrackTokenVector>(ActuatorTypeID))
			{
				Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorVector2D>(AnimTypeID, Vector2DNameAndValue.ParameterName, Section));
			}
			Vector2DData.Set(0, Vector2DNameAndValue.Value.X);
			Vector2DData.Set(1, Vector2DNameAndValue.Value.Y);

			Container.GetAccumulator().BlendToken(FMovieSceneEvaluationOperand(), ActuatorTypeID, FMovieSceneEvaluationScope(), Context, TBlendableToken<FControlRigTrackTokenVector2D>(Vector2DData, Section->GetBlendType().Get(), Weight));
		}

		MovieScene::TMultiChannelValue<float, 3> VectorData;
		for (const FVectorParameterStringAndValue& VectorNameAndValue : Values.VectorValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = VectorAnimTypeIDsByName.GetAnimTypeID(VectorNameAndValue.ParameterString);
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

		MovieScene::TMultiChannelValue<float, 9> TransformData;
		for (const FTransformParameterStringAndValue& TransformNameAndValue : Values.TransformValues)
		{
			uint32 ID = BindingOverride->GetUniqueID();


			FMovieSceneAnimTypeID AnimTypeID = TransformAnimTypeIDsByName.GetAnimTypeID(TransformNameAndValue.ParameterString);
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

