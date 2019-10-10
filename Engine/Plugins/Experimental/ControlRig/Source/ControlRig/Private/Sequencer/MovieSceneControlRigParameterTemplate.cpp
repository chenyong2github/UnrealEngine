// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneControlRigParameterTemplate.h"
#include "Evaluation/Blending/MovieSceneMultiChannelBlending.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "ControlRig.h"
#include "Evaluation/MovieSceneEvaluation.h"
#include "IMovieScenePlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Sequencer/ControlRigSequencerAnimInstance.h"
#include "IControlRigObjectBinding.h"
#include "Animation/AnimData/BoneMaskFilter.h"
#include "ControlRigSkeletalMeshBinding.h"
#include "Evaluation/Blending/BlendableTokenStack.h"
#include "Evaluation/Blending/MovieSceneBlendingActuatorID.h"


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
		ParameterString = InParameterName.ToString();		Value = InValue;
	}

	/** The name of the scalar parameter. */
	FString ParameterString;
	FName ParameterName;
	/** The animated value of the scalar parameter. */
	float Value;
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
			{}

			virtual void RestoreState(UObject& InObject, IMovieScenePlayer& Player) override
			{
				if (UControlRig* ControlRig = Cast<UControlRig>(&InObject))
				{
					if (ControlRig->GetObjectBinding())
					{
						if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
						{
							if (UControlRigSequencerAnimInstance* AnimInstance = Cast<UControlRigSequencerAnimInstance>(SkeletalMeshComponent->GetAnimInstance()))
							{
								AnimInstance->ResetNodes();
								AnimInstance->RecalcRequiredBones();
							}
							UAnimSequencerInstance::UnbindFromSkeletalMeshComponent(SkeletalMeshComponent);
						}
						for (TNameAndValue<float>& Value : ScalarValues)
						{
							ControlRig->SetControlValue<float>(Value.Name, Value.Value);
						}

						for (TNameAndValue<FVector>& Value : VectorValues)
						{
							ControlRig->SetControlValue<FVector>(Value.Name, Value.Value);
						}

						for (TNameAndValue<FTransform>& Value : TransformValues)
						{
							ControlRig->SetControlValue<FTransform>(Value.Name, Value.Value);
						}
						ControlRig->GetObjectBinding()->UnbindFromObject();
					}
				}
			}

			FMovieSceneSequenceID SequenceID;
			TArray< TNameAndValue<float> > ScalarValues;
			TArray< TNameAndValue<FVector> > VectorValues;
			TArray< TNameAndValue<FTransform> > TransformValues;
		};


		FToken Token(SequenceID);

		if (UControlRig* ControlRig = Cast<UControlRig>(&Object))
		{

			/*
			//mz todo the other types

			UENUM()
			enum class ERigControlType : uint8
			{
				Bool,
				Float,
				Vector2D,
				Position,
				Scale,
				Quat,
				Rotator,
				Transform
			};
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
				}
			}
		}

		return MoveTemp(Token);
	}

	FMovieSceneSequenceID SequenceID;
	TArray< TNameAndValue<float> > ScalarValues;
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

	void BindToSequencerInstance(UControlRig* ControlRig)
	{
		check(ControlRig);
		if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
		{
			bool bWasCreated = false;
			if (UControlRigSequencerAnimInstance* AnimInstance = UAnimCustomInstance::BindToSkeletalMeshComponent<UControlRigSequencerAnimInstance>(SkeletalMeshComponent, bWasCreated))
			{
				AnimInstance->RecalcRequiredBones();
			}
		}
	}

	void UnBindFromSequencerInstance(UControlRig* ControlRig)
	{
		check(ControlRig);
		if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
		{
			UAnimCustomInstance::UnbindFromSkeletalMeshComponent(SkeletalMeshComponent);
		}
	}

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
				}
				BindToSequencerInstance(ControlRig);
				//MZ TODO HANDLE BOOLS AND OTHER NON BLENDABLES
				if (ControlRig->GetObjectBinding())
				{
					if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
					{
						if (UControlRigSequencerAnimInstance* AnimInstance = Cast<UControlRigSequencerAnimInstance>(SkeletalMeshComponent->GetAnimInstance()))
						{
							float Weight = 1.0f;
							bool bStructureChanged = AnimInstance->UpdateControlRig(ControlRig, Operand.SequenceID.GetInternalValue(), bAdditive, bApplyBoneFilter, *BoneFilter, Weight, true, true);
							if (bStructureChanged)
							{
								AnimInstance->RecalcRequiredBones();
							}
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
	TControlRigParameterActuatorFloat(FMovieSceneAnimTypeID& InAnimID, UControlRig* InControlRig, const FName& InParameterName, const UMovieSceneControlRigParameterSection* InSection)
		: TMovieSceneBlendingActuator<FControlRigTrackTokenFloat>(FMovieSceneBlendingActuatorID(InAnimID))
		, ControlRig(InControlRig)
		, ParameterName(InParameterName)
		, SectionData(InSection)
	{}


	FControlRigTrackTokenFloat RetrieveCurrentValue(UObject* InObject, IMovieScenePlayer* Player) const
	{
		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
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
		}
		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
			if (RigControl && RigControl->ControlType == ERigControlType::Float)
			{
				ControlRig->SetControlValue<float>(ParameterName, InFinalValue.Value);
			}
		}
		if (Section)
		{
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

	UControlRig *ControlRig;
	FName ParameterName;
	TWeakObjectPtr<const UMovieSceneControlRigParameterSection> SectionData;

};



struct TControlRigParameterActuatorVector : TMovieSceneBlendingActuator<FControlRigTrackTokenVector>
{
	TControlRigParameterActuatorVector(FMovieSceneAnimTypeID& InAnimID, UControlRig* InControlRig, const FName& InParameterName, const UMovieSceneControlRigParameterSection* InSection)
		: TMovieSceneBlendingActuator<FControlRigTrackTokenVector>(FMovieSceneBlendingActuatorID(InAnimID))
		, ControlRig(InControlRig)
		, ParameterName(InParameterName)
		, SectionData(InSection)
	{}



	FControlRigTrackTokenVector RetrieveCurrentValue(UObject* InObject, IMovieScenePlayer* Player) const
	{
		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
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
		}
		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
			if (RigControl && (RigControl->ControlType == ERigControlType::Position || RigControl->ControlType == ERigControlType::Scale || RigControl->ControlType == ERigControlType::Rotator))
			{
				ControlRig->SetControlValue<FVector>(ParameterName, InFinalValue.Value);
			}
		}
		if (Section)
		{
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

	UControlRig *ControlRig;
	FName ParameterName;
	TWeakObjectPtr<const UMovieSceneControlRigParameterSection> SectionData;

};



struct TControlRigParameterActuatorTransform : TMovieSceneBlendingActuator<FControlRigTrackTokenTransform>
{
	TControlRigParameterActuatorTransform(FMovieSceneAnimTypeID& InAnimID, UControlRig* InControlRig, const FName& InParameterName, const UMovieSceneControlRigParameterSection* InSection)
		: TMovieSceneBlendingActuator<FControlRigTrackTokenTransform>(FMovieSceneBlendingActuatorID(InAnimID))
		, ControlRig(InControlRig)
		, ParameterName(InParameterName)
		, SectionData(InSection)
	{}

	FControlRigTrackTokenTransform RetrieveCurrentValue(UObject* InObject, IMovieScenePlayer* Player) const
	{
		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
			if (RigControl && RigControl->ControlType == ERigControlType::Transform)
			{
				FTransform Val = RigControl->Value.Get<FTransform>();
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
		}
		if (ControlRig)
		{
			FRigControl* RigControl = ControlRig->FindControl(ParameterName);
			if (RigControl && RigControl->ControlType == ERigControlType::Transform)
			{
				ControlRig->SetControlValue<FTransform>(ParameterName, InFinalValue.Value);
			}
		}
		if (Section)
		{
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
	UControlRig *ControlRig;
	FName ParameterName;
	TWeakObjectPtr<const UMovieSceneControlRigParameterSection> SectionData;

};



void FMovieSceneControlRigParameterTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{

	const FFrameTime Time = Context.GetTime();

	const UMovieSceneControlRigParameterSection* Section = nullptr;
	if (GetSourceSection())
	{
		Section = Cast<UMovieSceneControlRigParameterSection>(GetSourceSection());

		//Do basic token
		FControlRigParameterExecutionToken ExecutionToken(Section);
		ExecutionTokens.Add(MoveTemp(ExecutionToken));

		//Do blended tokens
		FEvaluatedControlRigParameterSectionValues Values;
		EvaluateCurvesWithMasks(Context, Values);
		static TMovieSceneAnimTypeIDContainer<FString> ScalarAnimTypeIDsByName;
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
				ExecutionTokens.GetBlendingAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorFloat>(AnimTypeID, Section->ControlRig, ScalarNameAndValue.ParameterName,Section));
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
				ExecutionTokens.GetBlendingAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorVector>(AnimTypeID, Section->ControlRig, VectorNameAndValue.ParameterName,Section));
			}
			VectorData.Set(0, VectorNameAndValue.Value.X);
			VectorData.Set(1, VectorNameAndValue.Value.Y);
			VectorData.Set(2, VectorNameAndValue.Value.Z);

			ExecutionTokens.BlendToken(ActuatorTypeID, TBlendableToken<FControlRigTrackTokenVector>(VectorData, Section->GetBlendType().Get(), Weight));
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
				ExecutionTokens.GetBlendingAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorTransform>(AnimTypeID, Section->ControlRig, TransformNameAndValue.ParameterName, Section));
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

		int32 Index = 0;
		for (const FScalarParameterNameAndCurve& Scalar : Scalars)
		{
			float Value = 0;
			if (ControlsMask[Index])
			{
				Scalar.ParameterCurve.Evaluate(Time, Value);
			}
			
			Values.ScalarValues.Emplace(Scalar.ParameterName, Value);
			++Index;
		}

		for (const FVectorParameterNameAndCurves& Vector : Vectors)
		{
			FVector Value(ForceInitToZero);
			if (ControlsMask[Index])
			{
				Vector.XCurve.Evaluate(Time, Value.X);
				Vector.YCurve.Evaluate(Time, Value.Y);
				Vector.ZCurve.Evaluate(Time, Value.Z);
			}

			Values.VectorValues.Emplace(Vector.ParameterName, Value);
			++Index;
		}

		for (const FColorParameterNameAndCurves& Color : Colors)
		{
			FLinearColor ColorValue = FLinearColor::White;
			if (ControlsMask[Index])
			{
				Color.RedCurve.Evaluate(Time, ColorValue.R);
				Color.GreenCurve.Evaluate(Time, ColorValue.G);
				Color.BlueCurve.Evaluate(Time, ColorValue.B);
				Color.AlphaCurve.Evaluate(Time, ColorValue.A);
			}
		
			Values.ColorValues.Emplace(Color.ParameterName, ColorValue);
			++Index;
		}

		for (const FTransformParameterNameAndCurves& Transform : Transforms)
		{
			FVector Translation(ForceInitToZero), Scale(FVector::OneVector);
			FRotator Rotator(0.0f, 0.0f, 0.0f);
			EMovieSceneTransformChannel ChannelMask = Section->GetTransformMask().GetChannels();

			if (ControlsMask[Index])
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
			++Index;

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
				Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorFloat>(AnimTypeID, Section->ControlRig, ScalarNameAndValue.ParameterName, Section));
			}
			Container.GetAccumulator().BlendToken(FMovieSceneEvaluationOperand(), ActuatorTypeID, FMovieSceneEvaluationScope(), Context,TBlendableToken<FControlRigTrackTokenFloat>(ScalarNameAndValue.Value, Section->GetBlendType().Get(), Weight));
		}

		MovieScene::TMultiChannelValue<float, 3> VectorData;

		for (const FVectorParameterStringAndValue& VectorNameAndValue : Values.VectorValues)
		{
			FMovieSceneAnimTypeID AnimTypeID = VectorAnimTypeIDsByName.GetAnimTypeID(VectorNameAndValue.ParameterString);
			FMovieSceneBlendingActuatorID ActuatorTypeID(AnimTypeID);

			if (!Container.GetAccumulator().FindActuator< FControlRigTrackTokenVector>(ActuatorTypeID))
			{
				Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorVector>(AnimTypeID, Section->ControlRig, VectorNameAndValue.ParameterName, Section));
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
				Container.GetAccumulator().DefineActuator(ActuatorTypeID, MakeShared <TControlRigParameterActuatorTransform>(AnimTypeID, Section->ControlRig, TransformNameAndValue.ParameterName, Section));
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

