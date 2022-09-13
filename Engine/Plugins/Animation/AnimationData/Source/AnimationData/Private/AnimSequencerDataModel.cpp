// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimSequencerDataModel.h"
#include "AnimSequencerController.h"

#include "Animation/AnimSequence.h"
#include "AnimDataController.h"
#include "IAnimationEditor.h"
#include "ControlRig.h"
#include "ControlRigObjectBinding.h"
#include "Algo/Accumulate.h"
#include "Animation/AnimSequenceHelpers.h"
#include "Rigs/FKControlRig.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "UObject/ObjectSaveContext.h"
#include "Animation/AnimData/AnimDataNotifications.h"

#include "AnimSequencerHelpers.h"
#include "Animation/AnimationSettings.h"
#include "MovieScene/Private/Channels/MovieSceneCurveChannelImpl.h"
#include "Compilation/MovieSceneCompiledDataManager.h"

#define LOCTEXT_NAMESPACE "AnimSequencerDataModel"

int32 UAnimationSequencerDataModel::RetainFloatCurves = 0;
static FAutoConsoleVariableRef CVarRetainFloatCurves(
	TEXT("a.AnimSequencer.RetainFloatCurves"),
	UAnimationSequencerDataModel::RetainFloatCurves,
	TEXT("1 = Original FloatCurves are retained when generating transient curve data from Control Curves . 0 = FloatCurves are overriden with Control Curves"));

int32 UAnimationSequencerDataModel::ValidationMode = 0;
static FAutoConsoleVariableRef CValidationMode(
	TEXT("a.AnimSequencer.ValidationMode"),
	UAnimationSequencerDataModel::ValidationMode,
	TEXT("1 = Enables validation after operations to test data integrity against legacy version. 0 = validation disabled"));

int32 UAnimationSequencerDataModel::UseDirectFKControlRigMode = 1;
static FAutoConsoleVariableRef CVarDirectControlRigMode(
	TEXT("a.AnimSequencer.DirectControlRigMode"),
	UAnimationSequencerDataModel::UseDirectFKControlRigMode,
	TEXT("1 = FKControl rig uses Direct method for setting Control transforms. 0 = FKControl rig uses Replace method (transform offsets) for setting Control transforms"));

void UAnimationSequencerDataModel::RemoveOutOfDateControls() const
{
	if (UMovieSceneControlRigParameterSection* Section = GetFKControlRigSection())
	{
		if (UFKControlRig* ControlRig = Cast<UFKControlRig>(Section->GetControlRig()))
		{
			if (URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
			{
				if (URigHierarchyController* Controller = Hierarchy->GetController())
				{
					TArray<FRigElementKey> ElementKeysToRemove;
					Hierarchy->ForEach<FRigControlElement>([this, Section, &ElementKeysToRemove](const FRigControlElement* ControlElement) -> bool
					{
						const bool bContainsBone = Section->HasTransformParameter(ControlElement->GetName());
						const bool bContainsCurve = Section->HasScalarParameter(ControlElement->GetName());
						
						if (!bContainsBone && !bContainsCurve)
						{
							ElementKeysToRemove.Add(ControlElement->GetKey());
						}
						
						return true;
					});
						
					Hierarchy->ForEach<FRigCurveElement>([this, &ElementKeysToRemove](const FRigCurveElement* CurveElement) -> bool
					{
						const FName TargetCurveName = CurveElement->GetName();
						if(!LegacyCurveData.FloatCurves.ContainsByPredicate([TargetCurveName](const FFloatCurve& Curve) { return Curve.Name.DisplayName == TargetCurveName; }))
						{
							ElementKeysToRemove.Add(CurveElement->GetKey());	
						}
						return true;
					});

					for (const FRigElementKey& KeyToRemove : ElementKeysToRemove)
					{
						Controller->RemoveElement(KeyToRemove);
					}

					ControlRig->RefreshActiveControls();
				}
			}
		}
	}
}

USkeleton* UAnimationSequencerDataModel::GetSkeleton() const
{
	const UAnimationAsset* AnimationAsset = CastChecked<UAnimationAsset>(GetOuter());	
	checkf(AnimationAsset, TEXT("Unable to retrieve owning AnimationAsset"));

	USkeleton* Skeleton = AnimationAsset->GetSkeleton();
	if (Skeleton == nullptr)
	{
		IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("UnableToFindSkeleton", "Unable to retrieve target USkeleton for Animation Asset ({0})"), FText::FromString(*AnimationAsset->GetPathName()));
	} 

	return Skeleton;
}

void UAnimationSequencerDataModel::InitializeFKControlRig(UFKControlRig* FKControlRig, USkeleton* Skeleton) const
{
	checkf(FKControlRig, TEXT("Invalid FKControlRig provided"));
	if (Skeleton)
	{
		FKControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
		FKControlRig->GetObjectBinding()->BindToObject(Skeleton);

		UFKControlRig::FRigElementInitializationOptions InitOptions;
		InitOptions.bImportCurves = false;	
		if(UMovieSceneControlRigParameterSection* Section = GetFKControlRigSection())
		{
			for (const FScalarParameterNameAndCurve& AnimCurve : Section->GetScalarParameterNamesAndCurves())
			{
				InitOptions.CurveNames.Add(UFKControlRig::GetControlTargetName(AnimCurve.ParameterName, ERigElementType::Curve));
			}

			for (const FTransformParameterNameAndCurves& BoneCurve : Section->GetTransformParameterNamesAndCurves())
			{
				InitOptions.BoneNames.Add(UFKControlRig::GetControlTargetName(BoneCurve.ParameterName, ERigElementType::Bone));
			}
		}
		InitOptions.bGenerateBoneControls = InitOptions.BoneNames.Num() > 0;
		FKControlRig->SetInitializationOptions(InitOptions);

		FKControlRig->Initialize();

		FKControlRig->SetApplyMode(UseDirectFKControlRigMode == 1 ? EControlRigFKRigExecuteMode::Direct : EControlRigFKRigExecuteMode::Replace);
		FKControlRig->SetBoneInitialTransformsFromRefSkeleton(Skeleton->GetReferenceSkeleton());
		FKControlRig->Evaluate_AnyThread();
	}
}

UControlRig* UAnimationSequencerDataModel::GetControlRig() const
{
	if(const UMovieSceneControlRigParameterTrack* Track = GetControlRigTrack())
	{
		return Track->GetControlRig();
	}

	return nullptr;
}

void UAnimationSequencerDataModel::PostLoad()
{
	Super::PostLoad();

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Pre/post load any dependencies (Sequencer objects)
		TArray<UObject*> ObjectReferences;
		FReferenceFinder(ObjectReferences, this, false, true, true, true).FindReferences(this);	
		for (UObject* Dependency : ObjectReferences)
		{
			if (Dependency->HasAnyFlags(RF_NeedLoad))
			{
				Dependency->GetLinker()->Preload(Dependency);
			}

			if (Dependency->HasAnyFlags(RF_NeedPostLoad))
			{
				Dependency->ConditionalPostLoad();
			}
		}

		if (const UMovieSceneControlRigParameterSection* Section = GetFKControlRigSection())
		{
			if (UFKControlRig* ControlRig = Cast<UFKControlRig>(Section->GetControlRig()))
			{
				InitializeFKControlRig(ControlRig, GetSkeleton());
			}
		}

		RemoveOutOfDateControls();

		ValidateData();
	}
}

void UAnimationSequencerDataModel::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);
	OutDeps.Add(MovieScene);
}

void UAnimationSequencerDataModel::PostDuplicate(bool bDuplicateForPIE)
{
	UObject::PostDuplicate(bDuplicateForPIE);

	GetNotifier().Notify(EAnimDataModelNotifyType::Populated);
}

void UAnimationSequencerDataModel::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	// Forcefully skip UMovieSceneSequence::PreSave (as it generates cooked data which will never be included at the moment)
	UMovieSceneSignedObject::PreSave(ObjectSaveContext);
}

#if WITH_EDITOR
void UAnimationSequencerDataModel::WillNeverCacheCookedPlatformDataAgain()
{
	Super::WillNeverCacheCookedPlatformDataAgain();
	LegacyBoneAnimationTracks.Empty();
}
#endif

double UAnimationSequencerDataModel::GetPlayLength() const
{
	ValidateSequencerData();
	return MovieScene->GetDisplayRate().AsSeconds(GetNumberOfFrames());
}

int32 UAnimationSequencerDataModel::GetNumberOfFrames() const
{	
	ValidateSequencerData();
	const TRange<FFrameNumber> FrameRange = MovieScene->GetPlaybackRange();	
	const TRangeBound<FFrameNumber>& UpperRange = FrameRange.GetUpperBound();
	const bool bInclusive = UpperRange.IsInclusive();
	int32 Value = UpperRange.GetValue().Value;
	if (!bInclusive)
	{		
		Value = FMath::Max(Value - 1, 1);
	}

	return Value;
}

int32 UAnimationSequencerDataModel::GetNumberOfKeys() const
{
	return GetNumberOfFrames() + 1;
}

FFrameRate UAnimationSequencerDataModel::GetFrameRate() const
{	
	ValidateSequencerData();
	return MovieScene->GetDisplayRate();
}

const TArray<FBoneAnimationTrack>& UAnimationSequencerDataModel::GetBoneAnimationTracks() const
{
	return LegacyBoneAnimationTracks;
}

const FBoneAnimationTrack& UAnimationSequencerDataModel::GetBoneTrackByIndex(int32 TrackIndex) const
{
	checkf(LegacyBoneAnimationTracks.IsValidIndex(TrackIndex), TEXT("Unable to find animation track by index"));
	return LegacyBoneAnimationTracks[TrackIndex];
}

const FBoneAnimationTrack& UAnimationSequencerDataModel::GetBoneTrackByName(FName TrackName) const
{
	const FBoneAnimationTrack* TrackPtr = LegacyBoneAnimationTracks.FindByPredicate([TrackName](const FBoneAnimationTrack& Track)
	{
		return Track.Name == TrackName;
	});

	checkf(TrackPtr != nullptr, TEXT("Unable to find animation track by name"));

	return *TrackPtr;
}

const FBoneAnimationTrack* UAnimationSequencerDataModel::FindBoneTrackByName(FName Name) const
{
	return LegacyBoneAnimationTracks.FindByPredicate([Name](const FBoneAnimationTrack& Track)
	{
		return Track.Name == Name;
	});
}

FBoneAnimationTrack* UAnimationSequencerDataModel::FindMutableBoneTrackByName(FName Name)
{
	return LegacyBoneAnimationTracks.FindByPredicate([&Name](const FBoneAnimationTrack& Track)
	{
		return Track.Name == Name;
	});
}

const FBoneAnimationTrack* UAnimationSequencerDataModel::FindBoneTrackByIndex(int32 BoneIndex) const
{
	const FBoneAnimationTrack* TrackPtr = LegacyBoneAnimationTracks.FindByPredicate([BoneIndex](const FBoneAnimationTrack& Track)
	{
		return Track.BoneTreeIndex == BoneIndex;
	});

	return TrackPtr;
}

int32 UAnimationSequencerDataModel::GetBoneTrackIndex(const FBoneAnimationTrack& Track) const
{
	return LegacyBoneAnimationTracks.IndexOfByPredicate([&Track](const FBoneAnimationTrack& SearchTrack)
	{
		return SearchTrack.Name == Track.Name;
	});
}

int32 UAnimationSequencerDataModel::GetBoneTrackIndexByName(FName TrackName) const
{
	if (const FBoneAnimationTrack* TrackPtr = FindBoneTrackByName(TrackName))
	{
		return GetBoneTrackIndex(*TrackPtr);
	}

	return INDEX_NONE;
}

bool UAnimationSequencerDataModel::IsValidBoneTrackIndex(int32 TrackIndex) const
{
	return LegacyBoneAnimationTracks.IsValidIndex(TrackIndex);
}

int32 UAnimationSequencerDataModel::GetNumBoneTracks() const
{
	return LegacyBoneAnimationTracks.Num();
}

void UAnimationSequencerDataModel::GetBoneTrackNames(TArray<FName>& OutNames) const
{
	Algo::Transform(LegacyBoneAnimationTracks, OutNames, [](const FBoneAnimationTrack& Track)
	{
		return Track.Name; 
	});
}

const FAnimationCurveData& UAnimationSequencerDataModel::GetCurveData() const
{
	return LegacyCurveData;
}

int32 UAnimationSequencerDataModel::GetNumberOfTransformCurves() const
{
	return LegacyCurveData.TransformCurves.Num();
}

int32 UAnimationSequencerDataModel::GetNumberOfFloatCurves() const
{
	return LegacyCurveData.FloatCurves.Num();
}

const TArray<FFloatCurve>& UAnimationSequencerDataModel::GetFloatCurves() const
{
	return LegacyCurveData.FloatCurves;
}

const TArray<FTransformCurve>& UAnimationSequencerDataModel::GetTransformCurves() const
{
	return LegacyCurveData.TransformCurves;
}

const FAnimCurveBase* UAnimationSequencerDataModel::FindCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	switch (CurveIdentifier.CurveType)
	{
	case ERawCurveTrackTypes::RCT_Float:
		return FindFloatCurve(CurveIdentifier);
	case ERawCurveTrackTypes::RCT_Transform:
		return FindTransformCurve(CurveIdentifier);
	default:
		checkf(false, TEXT("Invalid curve identifier type"));
	}

	return nullptr;
}

const FFloatCurve* UAnimationSequencerDataModel::FindFloatCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	ensure(CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Float);
	for (const FFloatCurve& FloatCurve : GetCurveData().FloatCurves)
	{
		if (FloatCurve.Name == CurveIdentifier.InternalName || (FloatCurve.Name.UID == CurveIdentifier.InternalName.UID && FloatCurve.Name.UID != SmartName::MaxUID))
		{
			return &FloatCurve;
		}
	}

	return nullptr;
}

const FTransformCurve* UAnimationSequencerDataModel::FindTransformCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	ensure(CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Transform);
    for (const FTransformCurve& TransformCurve : GetCurveData().TransformCurves)
    {
    	if (TransformCurve.Name == CurveIdentifier.InternalName || TransformCurve.Name.UID == CurveIdentifier.InternalName.UID)
    	{
    		return &TransformCurve;
    	}
    }

    return nullptr;
}

const FRichCurve* UAnimationSequencerDataModel::FindRichCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	const FRichCurve* RichCurve = nullptr;

	if (CurveIdentifier.IsValid())
	{
		if (CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Float)
		{
			const FFloatCurve* Curve = FindFloatCurve(CurveIdentifier);
			if (Curve)
			{
				RichCurve = &Curve->FloatCurve;
			}
		}
		else if (CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Transform)
		{
			if (CurveIdentifier.Channel != ETransformCurveChannel::Invalid && CurveIdentifier.Axis != EVectorCurveChannel::Invalid)
			{
				// Dealing with transform curve
				if (const FTransformCurve* TransformCurve = FindTransformCurve(CurveIdentifier))
				{
					if (const FVectorCurve* VectorCurve = TransformCurve->GetVectorCurveByIndex(static_cast<int32>(CurveIdentifier.Channel)))
					{
						RichCurve = &VectorCurve->FloatCurves[static_cast<int32>(CurveIdentifier.Axis)];
					}
				}

			}
		}
	}

	return RichCurve;
}

const FAnimCurveBase& UAnimationSequencerDataModel::GetCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	const FAnimCurveBase* CurvePtr = FindCurve(CurveIdentifier);
	checkf(CurvePtr, TEXT("Tried to retrieve non-existing curve"));
	return *CurvePtr;
}

const FFloatCurve& UAnimationSequencerDataModel::GetFloatCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	const FFloatCurve* CurvePtr = FindFloatCurve(CurveIdentifier);
	checkf(CurvePtr, TEXT("Tried to retrieve non-existing curve"));
	return *CurvePtr;
}

const FTransformCurve& UAnimationSequencerDataModel::GetTransformCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	const FTransformCurve* CurvePtr = FindTransformCurve(CurveIdentifier);
	checkf(CurvePtr, TEXT("Tried to retrieve non-existing curve"));
	return *CurvePtr;
}

const FRichCurve& UAnimationSequencerDataModel::GetRichCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	const FRichCurve* CurvePtr = FindRichCurve(CurveIdentifier);
	checkf(CurvePtr, TEXT("Tried to retrieve non-existing curve"));
	return *CurvePtr;	
}

TArrayView<const FAnimatedBoneAttribute> UAnimationSequencerDataModel::GetAttributes() const
{
	return AnimatedBoneAttributes;
}

int32 UAnimationSequencerDataModel::GetNumberOfAttributes() const
{
	return AnimatedBoneAttributes.Num();
}

int32 UAnimationSequencerDataModel::GetNumberOfAttributesForBoneIndex(const int32 BoneIndex) const
{
	// Sum up total number of attributes with provided bone index
	const int32 NumberOfBoneAttributes = Algo::Accumulate<int32>(AnimatedBoneAttributes, 0, [BoneIndex](int32 Sum, const FAnimatedBoneAttribute& Attribute) -> int32
	{
		Sum += Attribute.Identifier.GetBoneIndex() == BoneIndex ? 1 : 0;
		return Sum;
	});
	return NumberOfBoneAttributes;
}

void UAnimationSequencerDataModel::GetAttributesForBone(const FName& BoneName, TArray<const FAnimatedBoneAttribute*>& OutBoneAttributes) const
{
	Algo::TransformIf(AnimatedBoneAttributes, OutBoneAttributes, [BoneName](const FAnimatedBoneAttribute& Attribute) -> bool
	{
		return Attribute.Identifier.GetBoneName() == BoneName;
	},
	[](const FAnimatedBoneAttribute& Attribute) 
	{
		return &Attribute;
	});
}

const FAnimatedBoneAttribute& UAnimationSequencerDataModel::GetAttribute(const FAnimationAttributeIdentifier& AttributeIdentifier) const
{
	const FAnimatedBoneAttribute* AttributePtr = FindAttribute(AttributeIdentifier);
	checkf(AttributePtr, TEXT("Unable to find attribute for provided identifier"));

	return *AttributePtr;
}

const FAnimatedBoneAttribute* UAnimationSequencerDataModel::FindAttribute(const FAnimationAttributeIdentifier& AttributeIdentifier) const
{
	return AnimatedBoneAttributes.FindByPredicate([AttributeIdentifier](const FAnimatedBoneAttribute& Attribute)
	{
		return Attribute.Identifier == AttributeIdentifier;
	});
}

UAnimSequence* UAnimationSequencerDataModel::GetAnimationSequence() const
{
	return Cast<UAnimSequence>(GetOuter());
}

FGuid UAnimationSequencerDataModel::GenerateGuid() const
{	
	if (CachedRawDataGUID.IsValid())
	{
		return CachedRawDataGUID;
	}
	else
	{
		FSHA1 Sha;
		const FString ClassName = GetClass()->GetName();
		Sha.UpdateWithString(*ClassName, ClassName.Len());
		
		auto UpdateSHAWithArray = [&Sha](const auto& Array)
		{
			if (Array.Num())
			{
				Sha.Update(reinterpret_cast<const uint8*>(Array.GetData()), Array.Num() * Array.GetTypeSize());			
			}
		};

		auto UpdateWithChannel = [UpdateSHAWithArray, &Sha](const auto& Channel)
		{
			UpdateSHAWithArray(Channel.GetData().GetTimes());
			UpdateSHAWithArray(Channel.GetData().GetValues());
			if (Channel.GetDefault().IsSet())
			{
				Sha.Update(reinterpret_cast<const uint8*>(&Channel.GetDefault().GetValue()), sizeof(Channel.GetDefault().GetValue()));
			}
		};

		if (const UMovieSceneControlRigParameterSection* RigSection = GetFKControlRigSection())
		{
			UpdateWithChannel(RigSection->Weight);

			for (const FTransformParameterNameAndCurves& TransformParameter : RigSection->GetTransformParameterNamesAndCurves())
			{
				const FString ParameterString = TransformParameter.ParameterName.ToString();
				Sha.UpdateWithString(*ParameterString, ParameterString.Len());
				for (int32 Index = 0; Index < 3; ++Index)
				{							
					UpdateWithChannel(TransformParameter.Translation[Index]);
					UpdateWithChannel(TransformParameter.Rotation[Index]);
					UpdateWithChannel(TransformParameter.Scale[Index]);
				}
			}

			for (const FScalarParameterNameAndCurve& ScalarCurve : RigSection->GetScalarParameterNamesAndCurves())
			{
				const FString ParameterString = ScalarCurve.ParameterName.ToString();
				Sha.UpdateWithString(*ParameterString, ParameterString.Len());
				UpdateWithChannel(ScalarCurve.ParameterCurve);
			}
		}
		
		auto UpdateWithData = [&Sha](const auto& Data)
		{
			Sha.Update(reinterpret_cast<const uint8*>(&Data), sizeof(Data));
		};
		
		for (const FAnimatedBoneAttribute& Attribute : AnimatedBoneAttributes)
		{
			UpdateWithData(Attribute.Identifier);
			UpdateSHAWithArray(Attribute.Curve.GetConstRefOfKeys());
		}

		Sha.Final();

		uint32 Hash[5];
		Sha.GetHash(reinterpret_cast<uint8*>(Hash));
		const FGuid Guid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
		
		return Guid;
	}
}

TScriptInterface<IAnimationDataController> UAnimationSequencerDataModel::GetController()
{
	TScriptInterface<IAnimationDataController> Controller = nullptr;
#if WITH_EDITOR
	Controller = NewObject<UAnimSequencerController>();
	Controller->SetModel(this);
#endif // WITH_EDITOR

	return Controller;
}

IAnimationDataModel::FModelNotifier& UAnimationSequencerDataModel::GetNotifier()
{
	if (!Notifier)
	{
		Notifier.Reset(new IAnimationDataModel::FModelNotifier(this));
	}
	
	return *Notifier.Get();
}

void UAnimationSequencerDataModel::Evaluate(FAnimationPoseData& InOutPoseData, const UE::Anim::DataModel::FEvaluationContext& EvaluationContext) const
{
	QUICK_SCOPE_CYCLE_COUNTER(AnimationDataSequence_Evaluate);

	if(!!ValidationMode)
	{
		ValidateSequencerData();
	}

	if (UMovieSceneControlRigParameterTrack* Track = GetControlRigTrack())
	{
		FScopeLock Lock(&EvaluationLock);
		// Evaluates and applies control curves from track to ControlRig
		EvaluateTrack(Track, EvaluationContext);

		// Generate/populate the output animation pose data
		UControlRig* ControlRig = Track->GetControlRig();
		GeneratePoseData(ControlRig, InOutPoseData, EvaluationContext);
	}
}

void UAnimationSequencerDataModel::OnNotify(const EAnimDataModelNotifyType& NotifyType, const FAnimDataModelNotifPayload& Payload)
{
	Collector.Handle(NotifyType);

	if (Collector.IsNotWithinBracket() && bPopulated)
	{
		// Once the model has been populated and a modification is made - invalidate the cached GUID
		auto ResetCachedGUID = [this]()
		{
			if (CachedRawDataGUID.IsValid() && !Collector.Contains(EAnimDataModelNotifyType::Populated))
			{
				CachedRawDataGUID.Invalidate();			
			}
		};

		bool bRefreshed = false;
		auto RefreshControlsAndProxy = [this, &bRefreshed]()
        {
			if (!bRefreshed)
			{
				if (UMovieSceneControlRigParameterSection* Section = GetFKControlRigSection())
				{
					if (!IsRunningCookCommandlet())
					{
						Section->ReconstructChannelProxy();
					}
			    
					if (UFKControlRig* FKRig = Cast<UFKControlRig>(Section->GetControlRig()))
					{
						FKRig->RefreshActiveControls();
					}
				}
				bRefreshed = true;
			}
        };	
		
		const TArray<EAnimDataModelNotifyType> CurveNotifyTypes = {EAnimDataModelNotifyType::CurveAdded, EAnimDataModelNotifyType::CurveChanged, EAnimDataModelNotifyType::CurveRenamed, EAnimDataModelNotifyType::CurveRemoved,
			EAnimDataModelNotifyType::CurveFlagsChanged, EAnimDataModelNotifyType::CurveScaled, EAnimDataModelNotifyType::CurveColorChanged, EAnimDataModelNotifyType::Populated, EAnimDataModelNotifyType::Reset };
		if(Collector.Contains(CurveNotifyTypes))
		{
			if(!ValidationMode)
			{
				GenerateLegacyCurveData();
			}
			RefreshControlsAndProxy();
			ResetCachedGUID();
		}

		const TArray<EAnimDataModelNotifyType> BonesNotifyTypes = {EAnimDataModelNotifyType::TrackAdded, EAnimDataModelNotifyType::TrackChanged, EAnimDataModelNotifyType::TrackRemoved, EAnimDataModelNotifyType::Populated, EAnimDataModelNotifyType::Reset };
		if(Collector.Contains(BonesNotifyTypes))
		{
			if(!ValidationMode)
			{
				GenerateLegacyBoneData();
			}
			RefreshControlsAndProxy();
			ResetCachedGUID();
		}

		if (Collector.Contains(EAnimDataModelNotifyType::Populated))
		{
			RefreshControlsAndProxy();
		}
				
		ValidateData();
	}
}

UMovieSceneControlRigParameterTrack* UAnimationSequencerDataModel::GetControlRigTrack() const
{
	return MovieScene->FindMasterTrack<UMovieSceneControlRigParameterTrack>();
}

UMovieSceneControlRigParameterSection* UAnimationSequencerDataModel::GetFKControlRigSection() const
{
	if (MovieScene)
	{
		const UMovieSceneControlRigParameterTrack* Track = GetControlRigTrack();
		for (UMovieSceneSection* TrackSection : Track->GetAllSections())
		{
			if (UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(TrackSection))
			{
				if (const UControlRig* ControlRig = Section->GetControlRig())
				{
					if (ControlRig->IsA<UFKControlRig>())
					{
						return Section;
					}
				}
			}
		}
	}

	return nullptr;
}

void UAnimationSequencerDataModel::GenerateLegacyCurveData()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GenerateLegacyCurveData);
	ValidateSequencerData();
	
	if (const UMovieSceneControlRigParameterTrack* Track = GetControlRigTrack())
	{
		for (const UMovieSceneSection* TrackSection : Track->GetAllSections())
		{
			if (const UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(TrackSection))
			{
				if (const UControlRig* ControlRig = Section->GetControlRig())
				{
					if (USkeleton* Skeleton = GetSkeleton())
					{
						if(URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
						{
							const FString SequencerSuffix(TEXT("_Sequencer"));
							const TArray<FScalarParameterNameAndCurve>& ScalarCurves = Section->GetScalarParameterNamesAndCurves();				
							if (RetainFloatCurves)
							{
								LegacyCurveData.FloatCurves.RemoveAll([SequencerSuffix](const FFloatCurve& FloatCurve)
								{
									return FloatCurve.Name.DisplayName.ToString().EndsWith(SequencerSuffix);
								});
							}
							else
							{
								LegacyCurveData.FloatCurves.Empty();
							}				

							Hierarchy->ForEach<FRigCurveElement>([Hierarchy,Skeleton, this, ScalarCurves, FrameRate = GetFrameRate(), SequencerSuffix](const FRigCurveElement* CurveElement) -> bool
							{
								const FRigElementKey ControlKey(UFKControlRig::GetControlName(CurveElement->GetName(), ERigElementType::Curve), ERigElementType::Control);
								if (const FRigControlElement* Element = Hierarchy->Find<FRigControlElement>(ControlKey))
								{
									FFloatCurve& FloatCurve = LegacyCurveData.FloatCurves.AddDefaulted_GetRef();
									if (RetainFloatCurves)
									{
										FloatCurve.Name.DisplayName = FName(*(CurveElement->GetName().ToString() + TEXT("_Sequencer")));
									}
									else
									{
										FloatCurve.Name.DisplayName = CurveElement->GetName();	
									}						
								
									Skeleton->VerifySmartName(USkeleton::AnimCurveMappingName, FloatCurve.Name);
									FloatCurve.Color = Element->Settings.ShapeColor;
									
									const FAnimationCurveIdentifier CurveId(FloatCurve.Name, ERawCurveTrackTypes::RCT_Float);
									if (!RetainFloatCurves || !FloatCurve.Name.DisplayName.ToString().Contains(SequencerSuffix))
									{
										const FAnimationCurveMetaData& CurveMetaData = CurveIdentifierToMetaData.FindChecked(CurveId);
										FloatCurve.SetCurveTypeFlags(CurveMetaData.Flags);
										FloatCurve.Color = CurveMetaData.Color;
									}							

									if (const FScalarParameterNameAndCurve* ScalarCurve = ScalarCurves.FindByPredicate([Element](FScalarParameterNameAndCurve Curve)
									{
										return Curve.ParameterName == Element->GetName();
									}))
									{							
										AnimSequencerHelpers::ConvertFloatChannelToRichCurve(ScalarCurve->ParameterCurve, FloatCurve.FloatCurve, FrameRate);
									}
								}
								return true;
							});	
						}
						else
						{						
							IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("UnableToFindRigHierarchy", "Unable to retrieve RigHierarchy for ControlRig ({0})"), FText::FromString(ControlRig->GetPathName()));	      
						}
					}								
				}
			}
		}
	}
}

void UAnimationSequencerDataModel::GenerateLegacyBoneData()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GenerateLegacyBoneData);
	// Reset current track-data
	LegacyBoneAnimationTracks.Reset();

	if(const USkeleton* TargetSkeleton = GetSkeleton())
	{
		const FReferenceSkeleton& ReferenceSkeleton = TargetSkeleton->GetReferenceSkeleton();
		ValidateSequencerData();
		
		if(const UMovieSceneControlRigParameterSection* Section = GetFKControlRigSection())
		{
			if (const UControlRig* ControlRig = Section->GetControlRig())
			{
				if (const URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
				{
					TArray<FTransform> Transforms;
					TArray<FFrameNumber> FrameNumbers;

					const TArray<FTransformParameterNameAndCurves>& TransformCurves = Section->GetTransformParameterNamesAndCurves();
					LegacyBoneAnimationTracks.SetNumZeroed(TransformCurves.Num());
					ParallelFor(TransformCurves.Num(), [this, &TransformCurves, Hierarchy, &ReferenceSkeleton](int32 CurveIndex)
					{
						const FTransformParameterNameAndCurves& TransformParameterCurve = TransformCurves[CurveIndex];

						const FName TargetBoneName = UFKControlRig::GetControlTargetName(TransformParameterCurve.ParameterName, ERigElementType::Bone);
						const FRigElementKey BoneElementKey(TargetBoneName, ERigElementType::Bone);
						if (Hierarchy->Contains(BoneElementKey))
						{							
							// Only populate the track if any curve keys were set
							const bool bContainsAnyKeys = [&TransformParameterCurve]()
							{
								for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
								{
									if (TransformParameterCurve.Translation[ChannelIndex].HasAnyData() || TransformParameterCurve.Rotation[ChannelIndex].HasAnyData() || TransformParameterCurve.Scale[ChannelIndex].HasAnyData())
									{
										return true;
									}
								}

								return false;
							}();
							
							FBoneAnimationTrack& BoneTrack = LegacyBoneAnimationTracks[CurveIndex];
							BoneTrack.Name = TargetBoneName;
							BoneTrack.BoneTreeIndex = ReferenceSkeleton.FindBoneIndex(BoneTrack.Name);

							if (bContainsAnyKeys)
							{								
								const int32 NumKeys = GetNumberOfKeys();
								BoneTrack.InternalTrackData.PosKeys.SetNumUninitialized(NumKeys);
								BoneTrack.InternalTrackData.RotKeys.SetNumUninitialized(NumKeys);
								BoneTrack.InternalTrackData.ScaleKeys.SetNumUninitialized(NumKeys);

								FVector3f EulerAngles;
								for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
								{
									for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
									{
										BoneTrack.InternalTrackData.PosKeys[FrameIndex][ChannelIndex] = TransformParameterCurve.Translation[ChannelIndex].GetValues().Num() == 0 ? TransformParameterCurve.Translation[ChannelIndex].GetDefault().GetValue() : TransformParameterCurve.Translation[ChannelIndex].GetValues()[FrameIndex].Value;
										EulerAngles[ChannelIndex] = TransformParameterCurve.Rotation[ChannelIndex].GetValues().Num() == 0 ? TransformParameterCurve.Rotation[ChannelIndex].GetDefault().GetValue() : TransformParameterCurve.Rotation[ChannelIndex].GetValues()[FrameIndex].Value;
										BoneTrack.InternalTrackData.ScaleKeys[FrameIndex][ChannelIndex] = TransformParameterCurve.Scale[ChannelIndex].GetValues().Num() == 0 ? TransformParameterCurve.Scale[ChannelIndex].GetDefault().GetValue() : TransformParameterCurve.Scale[ChannelIndex].GetValues()[FrameIndex].Value;
									}
									
									BoneTrack.InternalTrackData.RotKeys[FrameIndex] = FQuat4f::MakeFromEuler(EulerAngles);
								}								
							}
						}
					});

					// Remove any empty non-populate tracks
					//LegacyBoneAnimationTracks.RemoveAll([](const FBoneAnimationTrack& Track) { return Track.InternalTrackData.PosKeys.Num() == 0; } );
				}
				else
				{						
					IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("UnableToFindRigHierarchy", "Unable to retrieve RigHierarchy for ControlRig ({0})"), FText::FromString(ControlRig->GetPathName()));	      
				}
			}
			else
			{				
				IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("UnableToFindControlRig", "Unable to retrieve ControlRig for Model ({0})"), FText::FromString(GetPathName()));
			}
		}					
	}
	else
	{
		IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("UnableToFindSkeleton", "Unable to retrieve target USkeleton for Animation Asset ({0})"), FText::FromString(GetOuter()->GetPathName()));	
	}
}

void UAnimationSequencerDataModel::ValidateData() const
{		
	ValidateSequencerData();
	ValidateControlRigData();

	if (!!ValidationMode)
	{
		ValidateLegacyAgainstControlRigData();
	}
}

void UAnimationSequencerDataModel::ValidateSequencerData() const
{
	checkf(MovieScene, TEXT("No Movie Scene found for SequencerDataModel"));

	const int32 NumberOfMasterTracks = MovieScene->GetMasterTracks().Num();
	checkf(NumberOfMasterTracks == 1, TEXT("Invalid number of Tracks in Movie Scene expected 1 but found %i"), NumberOfMasterTracks);
		
	const UMovieSceneControlRigParameterTrack* Track = MovieScene->FindMasterTrack<UMovieSceneControlRigParameterTrack>();
	checkf(Track, TEXT("Unable to find Control Rig Track"));

	const int32 NumberOfSections = Track->GetAllSections().Num();
	checkf(NumberOfSections == 1, TEXT("Invalid number of Sections found for Control Rig Track expected 1 but found %i"), NumberOfSections);

	const UMovieSceneControlRigParameterSection* Section = GetFKControlRigSection();
	checkf(Section, TEXT("Unable to find Control Rig Section"));
}

void UAnimationSequencerDataModel::ValidateControlRigData() const
{
	const UMovieSceneControlRigParameterSection* Section = GetFKControlRigSection();
	checkf(Section, TEXT("Unable to find Control Rig Section"));

	UControlRig* ControlRig = Section->GetControlRig();
	checkf(ControlRig, TEXT("Unable to find Control Rig instance for Section"));

	checkf(ControlRig->IsA<UFKControlRig>(), TEXT("Invalid class for Control Rig expected UFKControlRig"));

	const URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
	if (Hierarchy && ValidationMode)
	{
		// Validate Rig Hierarchy against the outer Animation Sequence its (reference) Skeleton		
		if (const USkeleton* Skeleton = GetSkeleton())
		{
			const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
			const int32 NumberOfBones = ReferenceSkeleton.GetNum();

			// Validating the bone elements against the reference skeleton bones
			for (int32 BoneIndex = 0; BoneIndex < NumberOfBones; ++BoneIndex)
			{
				const FName ExpectedBoneName = ReferenceSkeleton.GetBoneName(BoneIndex);
				const bool bIsVirtualBone = ExpectedBoneName.ToString().StartsWith(VirtualBoneNameHelpers::VirtualBonePrefix);
				if (!bIsVirtualBone)
				{
					const FRigElementKey BoneKey(ExpectedBoneName, ERigElementType::Bone);
					const FRigBoneElement* BoneElement = Hierarchy->Find<FRigBoneElement>(BoneKey);
					checkf(BoneElement, TEXT("Unable to find FRigBoneElement in RigHierarchy for Bone with name: %s"), *ExpectedBoneName.ToString());
		
					const int32 ParentBoneIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
					if (BoneElement && ParentBoneIndex != INDEX_NONE)
					{
						const FName ExpectedParentBoneName = ReferenceSkeleton.GetBoneName(ParentBoneIndex);
						const FRigElementKey ParentBoneKey(ExpectedParentBoneName, ERigElementType::Bone);
            
						const FRigBoneElement* ParentBoneElement = Hierarchy->Find<FRigBoneElement>(ParentBoneKey);
						checkf(BoneElement->ParentElement == ParentBoneElement, TEXT("Unexpected Parent Element for Bone %s. Expected %s but found %s"), *ExpectedBoneName.ToString(), *ExpectedParentBoneName.ToString(), *ParentBoneElement->GetDisplayName().ToString());
					}	
				}
			}
		}
	}
}

void UAnimationSequencerDataModel::ValidateLegacyAgainstControlRigData() const
{
	UMovieSceneControlRigParameterSection* Section = GetFKControlRigSection();

	UControlRig* ControlRig = Section->GetControlRig();
	URigHierarchy* Hierarchy = ControlRig->GetHierarchy();

	// Validate bone tracks against controls
	const UAnimSequence* OuterSequence = GetAnimationSequence();
	if (const USkeleton* Skeleton = OuterSequence->GetSkeleton())
	{
		const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();

		TArray<FTransform> Transforms;
		TArray<FFrameNumber> FrameNumbers;
		
		for (const FBoneAnimationTrack& Track : LegacyBoneAnimationTracks)
		{
			const FName ExpectedBoneName = ReferenceSkeleton.GetBoneName(Track.BoneTreeIndex);
			
			const FRigElementKey BoneKey(ExpectedBoneName, ERigElementType::Bone);
			const FRigBoneElement* BoneElement = Hierarchy->Find<FRigBoneElement>(BoneKey);
			if(!BoneElement)
			{
				IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("BoneElementNotFound", "Unable to find FRigBoneElement in RigHierarchy for Bone with name: {0}"), FText::FromString(ExpectedBoneName.ToString()));
			}

			const FRigElementKey BoneControlKey(UFKControlRig::GetControlName(ExpectedBoneName, ERigElementType::Bone), ERigElementType::Control);
			const FRigControlElement* BoneControlElement = Hierarchy->Find<FRigControlElement>(BoneControlKey);

			if (!BoneControlElement)
			{
				IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("ControlElementNotFound", "Unable to find FRigControlElement in RigHierarchy for Bone with name: {0}"), FText::FromString(ExpectedBoneName.ToString()));
			}		
		
			const FTransformParameterNameAndCurves* BoneCurveParameter = Section->GetTransformParameterNamesAndCurves().FindByPredicate([BoneControlKey](const FTransformParameterNameAndCurves& ParameterPair)
			{
				return ParameterPair.ParameterName == BoneControlKey.Name;
			});
			if (!(BoneCurveParameter || Track.InternalTrackData.PosKeys.Num() == 0))
			{
				IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("ControlCurveNotFound", "Unable to find FTransformParameterNameAndCurves in RigHierarchy for Bone Control with name: {0}"), FText::FromName(BoneControlKey.Name));
			}

			GenerateTransformKeysForControl(ExpectedBoneName, Transforms, FrameNumbers);

			const int32 NumExpectedKeys = Track.InternalTrackData.PosKeys.Num();
			if (NumExpectedKeys != Transforms.Num())
			{
				IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("UnexpectedNumberOfControlKeys",	"Unexpected number of Bone Control Curve keys for {0}, expected {1} but found {2}"), FText::FromName(ExpectedBoneName), FText::AsNumber(NumExpectedKeys), FText::AsNumber(Transforms.Num()));
			}			

			if (NumExpectedKeys == Transforms.Num())
			{
				for (int32 KeyIndex = 0; KeyIndex < NumExpectedKeys; ++KeyIndex)
				{
					const FTransform& TransformKey = Transforms[KeyIndex];

					checkf(TransformKey.GetLocation().Equals(FVector(Track.InternalTrackData.PosKeys[KeyIndex])), TEXT("Unexpected positional key (%i) for bone %s, expected %s but found %s"), KeyIndex,
						*ExpectedBoneName.ToString(),* Track.InternalTrackData.PosKeys[KeyIndex].ToCompactString(), *TransformKey.GetLocation().ToCompactString());

					if(!TransformKey.GetLocation().Equals(FVector(Track.InternalTrackData.PosKeys[KeyIndex])))
					{
						IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("UnexpectedPositionalKey", "Unexpected positional key ({0}) for bone {1}, expected {2} but found {3}"),							
							FText::AsNumber(KeyIndex), FText::FromName(ExpectedBoneName), FText::FromString(Track.InternalTrackData.PosKeys[KeyIndex].ToCompactString()), FText::FromString(TransformKey.GetLocation().ToCompactString()));
					}

					const FQuat LegacyRotation = FQuat(Track.InternalTrackData.RotKeys[KeyIndex]).GetNormalized();
					const double RotationDeltaDegrees = FMath::RadiansToDegrees(TransformKey.GetRotation().AngularDistance(LegacyRotation));
			
					if(RotationDeltaDegrees > 0.5f)
					{
						IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("UnexpectedRotationalKey", "Unexpected rotational key ({0} degrees delta) ({1}) for bone {2}, expected {3} but found {4}"), FText::AsNumber(RotationDeltaDegrees), FText::AsNumber(KeyIndex), FText::FromName(ExpectedBoneName), FText::FromString(Track.InternalTrackData.RotKeys[KeyIndex].ToString()), FText::FromString(TransformKey.GetRotation().ToString()));
					}

					if (!TransformKey.GetScale3D().Equals(FVector(Track.InternalTrackData.ScaleKeys[KeyIndex])))
					{
						IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("UnexpectedScalingKey", "Unexpected scaling key ({0}) for bone {1}, expected {2} but found {3}"), FText::AsNumber(KeyIndex),
							FText::FromName(ExpectedBoneName), FText::FromString(Track.InternalTrackData.ScaleKeys[KeyIndex].ToCompactString()), FText::FromString(TransformKey.GetScale3D().ToCompactString()));
					}
				}
			
				Transforms.Reset();
				FrameNumbers.Reset();
			}
		}
		
		// Validate curve data against controls
		for (const FFloatCurve& FloatCurve : LegacyCurveData.FloatCurves)
		{
			const FName CurveName = FloatCurve.Name.DisplayName;					
			const FRigElementKey CurveKey(CurveName, ERigElementType::Curve);
			const FRigCurveElement* CurveElement = Hierarchy->Find<FRigCurveElement>(CurveKey);
			if (!CurveElement)
			{
				IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("CurveElementNotFound", "Unable to find FRigCurve in RigHierarchy for Curve with name: {0}"), FText::FromName(CurveName));
			}
	        
	        const FRigElementKey CurveControlKey(UFKControlRig::GetControlName(CurveName, ERigElementType::Curve), ERigElementType::Control);
	        const FRigControlElement* CurveControlElement = Hierarchy->Find<FRigControlElement>(CurveControlKey);
			if (!CurveControlElement)
			{
				IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("CurveControlElementNotFound", "Unable to find FRigControlElement in RigHierarchy for Curve with name: {0}"), FText::FromName(CurveName));
			}
			
			const FScalarParameterNameAndCurve* CurveControlParameter = Section->GetScalarParameterNamesAndCurves().FindByPredicate([CurveControlKey](const FScalarParameterNameAndCurve& ParameterPair)
			{
				return ParameterPair.ParameterName == CurveControlKey.Name;
			});
			
			if (CurveControlParameter)
			{
				for (const FRichCurveKey& Key : FloatCurve.FloatCurve.GetConstRefOfKeys())
				{
					float ParameterValue = 0.f;
					const FFrameTime FrameTime = CurveControlParameter->ParameterCurve.GetTickResolution().AsFrameTime(Key.Time);

					if (!CurveControlParameter->ParameterCurve.Evaluate(FrameTime, ParameterValue))
					{
						IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("FailedToEvaluateCurveControl", "Unable to evaluate Control Curve ({0}) at interval {1}"), FText::FromName(CurveName), FText::AsNumber(FrameTime.AsDecimal()));
					}

					const float RichCurveValue = FloatCurve.FloatCurve.Eval(Key.Time);
					// QQ threshold
					if (!(FMath::IsNearlyEqual(ParameterValue, Key.Value, 0.001f) || FMath::IsNearlyEqual(ParameterValue, RichCurveValue, 0.001f)))
					{
						IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("CurveDeviationError", "Unexpected Control Curve ({0}) evaluation value {1} at {2}, expected {3} ({4})"), FText::FromName(CurveName), FText::AsNumber(ParameterValue), FText::AsNumber(FrameTime.AsDecimal()), FText::AsNumber(Key.Value), FText::AsNumber(RichCurveValue));
					}
				}
			}
			else
			{
				IAnimationDataController::ReportObjectErrorf(this, LOCTEXT("ParameterNotFound", "Unable to find FScalarParameterNameAndCurve in RigHierarchy for Curve Control with name: {0}"), FText::FromName(CurveName));
			}
		}	
	}	
}

void UAnimationSequencerDataModel::IterateTransformControlCurve(const FName& BoneName, TFunction<void(const FTransform&, const FFrameNumber&)> IterationFunction) const
{
	ValidateSequencerData();
	ValidateControlRigData();

	const UMovieSceneControlRigParameterSection* Section = GetFKControlRigSection();
	UControlRig* ControlRig = Section->GetControlRig();
	const URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
	
	const FRigElementKey BoneControlKey(UFKControlRig::GetControlName(BoneName, ERigElementType::Bone), ERigElementType::Control);
	if (Hierarchy->Contains(BoneControlKey))
	{
		if (const FTransformParameterNameAndCurves* ControlCurvePtr = Section->GetTransformParameterNamesAndCurves().FindByPredicate([CurveName = BoneControlKey.Name](const FTransformParameterNameAndCurves& TransformParameter)
		{
			return TransformParameter.ParameterName == CurveName;
		}))
		{
			const FTransformParameterNameAndCurves& ControlCurve = *ControlCurvePtr;

			FTransform Transform;
			FVector3f Location;
			FVector3f EulerAngles;
			FVector3f Scale;

			for (int32 KeyIndex = 0; KeyIndex < GetNumberOfKeys(); ++KeyIndex)
			{
				const FFrameNumber Frame(KeyIndex);
				for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
				{
					ControlCurve.Translation[ChannelIndex].Evaluate(Frame, Location[ChannelIndex]);
					ControlCurve.Rotation[ChannelIndex].Evaluate(Frame, EulerAngles[ChannelIndex]);
					ControlCurve.Scale[ChannelIndex].Evaluate(Frame, Scale[ChannelIndex]);
				}

				Transform.SetLocation(FVector(Location));
				Transform.SetRotation(FQuat::MakeFromEuler(FVector(EulerAngles)));
				Transform.SetScale3D(FVector(Scale));

				Transform.NormalizeRotation();

				IterationFunction(Transform, Frame);
			}
		}
	}
}

void UAnimationSequencerDataModel::GenerateTransformKeysForControl(const FName& BoneName, TArray<FTransform>& InOutTransforms, TArray<FFrameNumber>& InOutFrameNumbers) const
{
	IterateTransformControlCurve(BoneName, [&InOutTransforms, &InOutFrameNumbers](const FTransform& Transform, const FFrameNumber& FrameNumber) -> void
	{
		InOutTransforms.Add(Transform);
		InOutFrameNumbers.Add(FrameNumber);
	});
}

UMovieScene* UAnimationSequencerDataModel::GetMovieScene() const
{
	return MovieScene;
}

UObject* UAnimationSequencerDataModel::GetParentObject(UObject* MovieSceneBlends) const
{
	return GetOuter();
}

void UAnimationSequencerDataModel::GeneratePoseData(UControlRig* ControlRig, FAnimationPoseData& InOutPoseData, const UE::Anim::DataModel::FEvaluationContext& EvaluationContext) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GeneratePoseData);
	
	if (ControlRig)
	{
		if (const URigHierarchy* RigHierarchy = ControlRig->GetHierarchy())
		{
			// Evaluate Control rig to update bone and curve elements according to controls
			ControlRig->Evaluate_AnyThread();

			// Start with ref-pose
			FCompactPose& RigPose = InOutPoseData.GetPose();
			RigPose.ResetToRefPose();
			const FBoneContainer& RequiredBones = RigPose.GetBoneContainer();

			FBlendedCurve& Curve = InOutPoseData.GetCurve();
			UE::Anim::Retargeting::FRetargetingScope RetargetingScope(RigPose, EvaluationContext);
			
			// Populate bone/curve elements to Pose/Curve indices
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_GetMappings);
				const FReferenceSkeleton& RefSkeleton = RequiredBones.GetReferenceSkeleton();

				RigHierarchy->ForEach<FRigControlElement>([this, &RefSkeleton, &Curve, &RequiredBones, &RigHierarchy, &RetargetingScope, &RigPose, bValidCurve = Curve.IsValid(), SmartNameContainer=RequiredBones.GetSkeletonAsset()->GetSmartNameContainer(USkeleton::AnimCurveMappingName)](const FRigControlElement* ControlElement) -> bool
				{
					if (ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
					{
						const FName ExpectedBoneName = UFKControlRig::GetControlTargetName(ControlElement->GetName(), ERigElementType::Bone);
						const int32 BoneIndex = RefSkeleton.FindBoneIndex(ExpectedBoneName);
						if (BoneIndex != INDEX_NONE)
						{
							const FName& BoneName = ExpectedBoneName;
							const FRigElementKey Key(BoneName, ERigElementType::Bone);

							const bool bMatchingLegacyBone = !ValidationMode || LegacyBoneAnimationTracks.ContainsByPredicate([BoneName](const FBoneAnimationTrack& Track)
							{
								return Track.Name == BoneName;
							});
							ensureMsgf(bMatchingLegacyBone, TEXT("Non-matching bone vs legacy data %s"), *BoneName.ToString());
						
							const int32 SkeletonBoneIndex = RequiredBones.GetSkeletonAsset()->GetReferenceSkeleton().FindBoneIndex(BoneName);
							const FCompactPoseBoneIndex CompactPoseBoneIndex = RequiredBones.GetCompactPoseIndexFromSkeletonIndex(SkeletonBoneIndex);
							if (CompactPoseBoneIndex != INDEX_NONE)
							{
								RetargetingScope.AddTrackedBone(CompactPoseBoneIndex, SkeletonBoneIndex);
				        
								// Retrieve evaluated bone transform from Hierarchy
								RigPose[CompactPoseBoneIndex] = RigHierarchy->GetLocalTransform(Key);
							}
						}
					}
					else if (ControlElement->Settings.ControlType == ERigControlType::Float && bValidCurve)
					{
						const FName ExpectedCurveName = UFKControlRig::GetControlTargetName(ControlElement->GetName(), ERigElementType::Curve);
						const SmartName::UID_Type CurveIndex = SmartNameContainer->FindUID(ExpectedCurveName);
						if (CurveIndex != INDEX_NONE)
						{
							const FRigElementKey Key(ExpectedCurveName, ERigElementType::Curve);	
							if (Curve.IsEnabled(CurveIndex))
							{
								Curve.Set(CurveIndex, RigHierarchy->GetCurveValue(Key));
							}
							
						}
					}

					return true;
				});
			}			

			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_NormalizeRotations);
				RigPose.NormalizeRotations();
			}

			// Apply any additive transform curves - if requested and any are set
			if (!RigPose.GetBoneContainer().ShouldUseSourceData())
			{
				for (const FTransformCurve& TransformCurve : GetTransformCurves())
				{
					// if disabled, do not handle
					if (TransformCurve.GetCurveTypeFlag(AACF_Disabled))
					{
						continue;
					}
			
					// Add or retrieve curve
					const FName& CurveName = TransformCurve.Name.DisplayName;
					// note we're not checking Curve.GetCurveTypeFlags() yet
					FTransform Value = TransformCurve.Evaluate(EvaluationContext.SampleFrameRate.AsSeconds(EvaluationContext.SampleTime), 1.f);

					const FCompactPoseBoneIndex BoneIndex(RigPose.GetBoneContainer().GetPoseBoneIndexForBoneName(CurveName));
					if(ensure(BoneIndex != INDEX_NONE))
					{
						const FTransform LocalTransform = RigPose[BoneIndex];
						RigPose[BoneIndex].SetRotation(LocalTransform.GetRotation() * Value.GetRotation());
						RigPose[BoneIndex].SetTranslation(LocalTransform.TransformPosition(Value.GetTranslation()));
						RigPose[BoneIndex].SetScale3D(LocalTransform.GetScale3D() * Value.GetScale3D());
					}					
				}
			}

			// Generate relative transform for VirtualBones according to source/target
			{					
				QUICK_SCOPE_CYCLE_COUNTER(STAT_GenerateVirtualBones);
				
				TArray<FVirtualBoneCompactPoseData>& VBCompactPoseData = UE::Anim::FBuildRawPoseScratchArea::Get().VirtualBoneCompactPoseData;
				VBCompactPoseData = RequiredBones.GetVirtualBoneCompactPoseData();
				if (VBCompactPoseData.Num() > 0)
				{
					FCSPose<FCompactPose> CSPose1;
					CSPose1.InitPose(RigPose);

					for (const FVirtualBoneCompactPoseData& VB : VBCompactPoseData)
					{
						const FTransform Source = CSPose1.GetComponentSpaceTransform(VB.SourceIndex);
						const FTransform Target = CSPose1.GetComponentSpaceTransform(VB.TargetIndex);
						RigPose[VB.VBIndex] = Target.GetRelativeTransform(Source);
					}
				}
			}

			{				
				QUICK_SCOPE_CYCLE_COUNTER(STAT_SetAttributes);
				// Evaluate attributes at requested time interval
				for (const FAnimatedBoneAttribute& Attribute : AnimatedBoneAttributes)
				{
					const FCompactPoseBoneIndex PoseBoneIndex = RequiredBones.GetCompactPoseIndexFromSkeletonIndex(Attribute.Identifier.GetBoneIndex());
					// Only add attribute if the bone its tied to exists in the currently evaluated set of bones
					if(PoseBoneIndex.IsValid())
					{
						UE::Anim::Attributes::GetAttributeValue(InOutPoseData.GetAttributes(), PoseBoneIndex, Attribute, EvaluationContext.SampleFrameRate.AsSeconds(EvaluationContext.SampleTime));
					}
				}				
			}			
		}
	}
}

void UAnimationSequencerDataModel::EvaluateTrack(UMovieSceneControlRigParameterTrack* CR_Track, const UE::Anim::DataModel::FEvaluationContext& EvaluationContext) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_EvaluateTrack);
	
	// Determine frame-time to sample according to the interpolation type (floor to frame for step interpolation)
	const FFrameTime InterpolationTime = EvaluationContext.InterpolationType == EAnimInterpolationType::Step ? EvaluationContext.SampleTime.FloorToFrame() : EvaluationContext.SampleTime;
	const FFrameTime BoneSampleTime = FFrameRate::TransformTime(InterpolationTime, EvaluationContext.SampleFrameRate, MovieScene->GetTickResolution());	

	// Retrieve section withing range of requested evaluation frame 
	const TArray<UMovieSceneSection*, TInlineAllocator<4>> SectionsInRange = CR_Track->FindAllSections(BoneSampleTime.FrameNumber);
	//ensureMsgf(SectionsInRange.Num() == 1, TEXT("Unable to retrieve section within range of request evaluation frame %i for %s"), BoneSampleTime.FrameNumber.Value, *GetAnimationSequence()->GetPathName());
	if (SectionsInRange.Num())
	{
		const UMovieSceneControlRigParameterSection* FKRigSection = CastChecked<UMovieSceneControlRigParameterSection>(SectionsInRange[0]);
		checkf(FKRigSection->ControlRigClass->GetDefaultObject()->IsA<UFKControlRig>(), TEXT("Unexpected class %s on ControlRig, expecting FKControlRig"), *FKRigSection->ControlRigClass->GetPathName());
		
		bool bWasDoNotKey = false;
		bWasDoNotKey = FKRigSection->GetDoNotKey();
		FKRigSection->SetDoNotKey(true);

		UControlRig* ControlRig = FKRigSection->GetControlRig();
		check(ControlRig);

		// Reset to ref-pose
		ControlRig->GetHierarchy()->ResetPoseToInitial(ERigElementType::Bone);

		const TArray<FScalarParameterNameAndCurve>& ScalarParameters = FKRigSection->GetScalarParameterNamesAndCurves();
		for (const FScalarParameterNameAndCurve& TypedParameter : ScalarParameters)
		{
			const FName& Name = TypedParameter.ParameterName;
			float Value = 0.f;

			const FFrameTime CurveSampleTime = FFrameRate::TransformTime(EvaluationContext.SampleTime, EvaluationContext.SampleFrameRate, TypedParameter.ParameterCurve.GetTickResolution());
			if(TypedParameter.ParameterCurve.Evaluate(CurveSampleTime, Value))
			{					
				const FRigControlElement* ControlElement = ControlRig->FindControl(Name);
				if (ControlElement && ControlElement->Settings.ControlType == ERigControlType::Float)
				{
					ControlRig->SetControlValue<float>(Name, Value, false, EControlRigSetKey::Never, false);
				}
			}
		}

		const TArray<FTransformParameterNameAndCurves>& TransformParameters = FKRigSection->GetTransformParameterNamesAndCurves();
		if (TransformParameters.Num())
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_EvaluateTransformParameters);
			
			struct FEvaluationInfo
			{
				float Interp = 0.f;
				int32 Index1 = INDEX_NONE, Index2 = INDEX_NONE;
			};

			TMovieSceneCurveChannelImpl<FMovieSceneFloatChannel>::FTimeEvaluationCache FromFrameTimeEvaluationCache;
			TMovieSceneCurveChannelImpl<FMovieSceneFloatChannel>::FTimeEvaluationCache ToFrameTimeEvaluationCache;
			for (const FTransformParameterNameAndCurves& TypedParameter : TransformParameters)
			{
				const FName& Name = TypedParameter.ParameterName;
				const FRigControlElement* ControlElement = ControlRig->FindControl(Name);
				if (ControlElement && ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
				{
					FEulerTransform EulerTransform;
					
					const double Alpha = BoneSampleTime.GetSubFrame();			
					auto EvaluateToTransform = [&TypedParameter](const FFrameNumber& Frame, FTransform& InOutTransform, TMovieSceneCurveChannelImpl<FMovieSceneFloatChannel>::FTimeEvaluationCache& Cache)
					{
						auto EvaluateValue = [Frame, &Cache](const auto& Channel, auto& Target)
						{
							auto Value = 0.f;
							TMovieSceneCurveChannelImpl<FMovieSceneFloatChannel>::EvaluateWithCache(&Channel, &Cache, Frame, Value);
							Target = Value;
						};

						auto EvaluateVector = [EvaluateValue](const auto& VectorChannels, auto& TargetVector)
						{
							EvaluateValue(VectorChannels[0], TargetVector[0]);
							EvaluateValue(VectorChannels[1], TargetVector[1]);
							EvaluateValue(VectorChannels[2], TargetVector[2]);
						};

						FVector Location, Scale;
						EvaluateVector(TypedParameter.Translation, Location);
						InOutTransform.SetTranslation(Location);
						EvaluateVector(TypedParameter.Scale, Scale);
						InOutTransform.SetScale3D(Scale);

						FRotator Rotator;
						EvaluateValue(TypedParameter.Rotation[0], Rotator.Roll);
						EvaluateValue(TypedParameter.Rotation[1], Rotator.Pitch);
						EvaluateValue(TypedParameter.Rotation[2], Rotator.Yaw);
						InOutTransform.SetRotation(Rotator.Quaternion());
					};

					auto ExtractTransform = [&TypedParameter](const FFrameNumber& Frame, FEulerTransform& InOutEulerTransform)
					{
						auto ExtractValue = [&TypedParameter, Frame](const auto& Channel, auto& Target)
						{
							if (Channel.GetDefault().IsSet())
							{
								Target = Channel.GetDefault().GetValue();
							}
							else
							{
								Target = Channel.GetValues()[Frame.Value].Value;
							}
						};

						auto ExtractVector = [ExtractValue](const auto& VectorChannels, auto& TargetVector)
						{
							ExtractValue(VectorChannels[0], TargetVector[0]);
							ExtractValue(VectorChannels[1], TargetVector[1]);
							ExtractValue(VectorChannels[2], TargetVector[2]);
						};

						ExtractVector(TypedParameter.Translation, InOutEulerTransform.Location);
						ExtractVector(TypedParameter.Scale, InOutEulerTransform.Scale);

						ExtractValue(TypedParameter.Rotation[0], InOutEulerTransform.Rotation.Roll);
						ExtractValue(TypedParameter.Rotation[1], InOutEulerTransform.Rotation.Pitch);
						ExtractValue(TypedParameter.Rotation[2], InOutEulerTransform.Rotation.Yaw);
					};
					
					// Assume no interpolation due to uniform keys
					if (FMath::IsNearlyZero(Alpha))
					{
						if (EvaluationContext.InterpolationType == EAnimInterpolationType::Linear)
						{
							FTransform FinalTransform;
							EvaluateToTransform(BoneSampleTime.FrameNumber, FinalTransform, FromFrameTimeEvaluationCache);
							EulerTransform = FEulerTransform(FinalTransform);
						}
						else if (EvaluationContext.InterpolationType == EAnimInterpolationType::Step)
						{
							ExtractTransform(BoneSampleTime.FrameNumber, EulerTransform);
						}
					}
					// Interpolate between two uniform keys
					else
					{
						const FFrameNumber FromFrame = BoneSampleTime.FloorToFrame();
						const FFrameNumber ToFrame = BoneSampleTime.CeilToFrame();

						FTransform FromBoneTransform;
						EvaluateToTransform(FromFrame, FromBoneTransform, FromFrameTimeEvaluationCache);
						FTransform ToBoneTransform;
						EvaluateToTransform(ToFrame, ToBoneTransform, ToFrameTimeEvaluationCache);

						FTransform FinalTransform;
						FinalTransform.Blend(FromBoneTransform, ToBoneTransform, Alpha);
						
						EulerTransform = FEulerTransform(FinalTransform);
					}
					
					ControlRig->SetControlValue<FRigControlValue::FEulerTransform_Float>(Name, EulerTransform, false, EControlRigSetKey::Never, false);
				}
			}
		}

		FKRigSection->SetDoNotKey(bWasDoNotKey);
	}
}

FTransformCurve* UAnimationSequencerDataModel::FindMutableTransformCurveById(const FAnimationCurveIdentifier& CurveIdentifier)
{
	for (FTransformCurve& TransformCurve : LegacyCurveData.TransformCurves)
	{
		if (TransformCurve.Name.UID == CurveIdentifier.InternalName.UID)
		{
			return &TransformCurve;
		}
	}

	return nullptr;
}

FFloatCurve* UAnimationSequencerDataModel::FindMutableFloatCurveById(const FAnimationCurveIdentifier& CurveIdentifier)
{
	for (FFloatCurve& FloatCurve : LegacyCurveData.FloatCurves)
	{
		if (FloatCurve.Name.UID == CurveIdentifier.InternalName.UID)
		{
			return &FloatCurve;
		}
	}

	return nullptr;
}

FAnimCurveBase* UAnimationSequencerDataModel::FindMutableCurveById(const FAnimationCurveIdentifier& CurveIdentifier)
{
	switch (CurveIdentifier.CurveType)
	{
	case ERawCurveTrackTypes::RCT_Float:
		return FindMutableFloatCurveById(CurveIdentifier);
	case ERawCurveTrackTypes::RCT_Transform:
		return FindMutableTransformCurveById(CurveIdentifier);
	default:
		checkf(false, TEXT("Invalid curve identifier type"));
	}

	return nullptr;
}

FRichCurve* UAnimationSequencerDataModel::GetMutableRichCurve(const FAnimationCurveIdentifier& CurveIdentifier)
{
	FRichCurve* RichCurve = nullptr;

	if (CurveIdentifier.IsValid())
	{
		if (CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Float)
		{
			FFloatCurve* Curve = FindMutableFloatCurveById(CurveIdentifier);
			if (Curve)
			{
				RichCurve = &Curve->FloatCurve;
			}
		}
		else if (CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Transform)
		{
			if (CurveIdentifier.Channel != ETransformCurveChannel::Invalid && CurveIdentifier.Axis != EVectorCurveChannel::Invalid)
			{
				// Dealing with transform curve
				if (FTransformCurve* TransformCurve = FindMutableTransformCurveById(CurveIdentifier))
				{
					if (FVectorCurve* VectorCurve = TransformCurve->GetVectorCurveByIndex(static_cast<int32>(CurveIdentifier.Channel)))
					{
						RichCurve = &VectorCurve->FloatCurves[static_cast<int32>(CurveIdentifier.Axis)];
					}
				}

			}
		}
	}

	return RichCurve;
}

#undef LOCTEXT_NAMESPACE //"AnimSequencerDataModel"