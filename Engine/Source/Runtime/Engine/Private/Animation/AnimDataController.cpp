// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimData/AnimDataController.h"
#include "Animation/AnimData/AnimDataControllerActions.h"

#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimData/CurveIdentifier.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSequence.h"


#include "Algo/Transform.h"
#include "UObject/NameTypes.h"
#include "Animation/AnimCurveTypes.h"
#include "Math/UnrealMathUtility.h"

#define LOCTEXT_NAMESPACE "AnimDataController"

#if WITH_EDITOR

namespace UE {
namespace Anim {
	bool CanTransactChanges()
	{
		return GEngine && GEngine->CanTransact() && !GIsTransacting;
	}

	struct FScopedCompoundTransaction
	{
		FScopedCompoundTransaction(UE::FChangeTransactor& InTransactor, const FText& InDescription) : Transactor(InTransactor), bCreated(false)
		{
			if (CanTransactChanges() && !Transactor.IsTransactionPending())
			{
				Transactor.OpenTransaction(InDescription);
				bCreated = true;
			}
		}

		~FScopedCompoundTransaction()
		{
			if (bCreated)
			{
				Transactor.CloseTransaction();
			}
		}

		UE::FChangeTransactor& Transactor;
		bool bCreated;
	};
}}

#define CONDITIONAL_TRANSACTION(Text) \
	TUniquePtr<UE::Anim::FScopedCompoundTransaction> Transaction; \
	if (UE::Anim::CanTransactChanges() && bShouldTransact) \
	{ \
		Transaction = MakeUnique<UE::Anim::FScopedCompoundTransaction>(ChangeTransactor, Text); \
	}

#define CONDITIONAL_BRACKET(Text) \
	TUniquePtr<UAnimDataController::FScopedBracket> Transaction; \
	if (UE::Anim::CanTransactChanges() && bShouldTransact) \
	{ \
		Transaction = MakeUnique<UAnimDataController::FScopedBracket>(this, Text); \
	}

#define CONDITIONAL_ACTION(ActionClass, ...) \
	if (UE::Anim::CanTransactChanges() && bShouldTransact) \
	{ \
		ChangeTransactor.AddTransactionChange<ActionClass>(__VA_ARGS__); \
	}

void UAnimDataController::SetModel(UAnimDataModel* InModel)
{	
	if (Model != nullptr)
	{
		Model->GetModifiedEvent().RemoveAll(this);
	}

	Model = InModel;
	
	ChangeTransactor.SetTransactionObject(InModel);
}

const UAnimDataModel* const UAnimDataController::GetModel() const
{
	return Model;
}

void UAnimDataController::OpenBracket(const FText& InTitle, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	if (UE::Anim::CanTransactChanges() && !ChangeTransactor.IsTransactionPending())
	{
		ChangeTransactor.OpenTransaction(InTitle);

		CONDITIONAL_ACTION(UE::Anim::FCloseBracketAction, InTitle.ToString());
	}

	if (BracketDepth == 0)
	{
		FBracketPayload Payload;
		Payload.Description = InTitle.ToString();

		Model->Notify(EAnimDataModelNotifyType::BracketOpened, Payload);
	}

	++BracketDepth;
}

void UAnimDataController::CloseBracket(bool bShouldTransact /*= true*/)
{
	ValidateModel();

	if (BracketDepth == 0)
	{
		ReportError(LOCTEXT("NoExistingBracketError", "Attempt to close bracket that was not previously opened"));
		return;
	}

	--BracketDepth;

	if (BracketDepth == 0)
	{
		if (UE::Anim::CanTransactChanges())
		{
			ensure(ChangeTransactor.IsTransactionPending());

			CONDITIONAL_ACTION(UE::Anim::FOpenBracketAction, TEXT("Open Bracket"));

			ChangeTransactor.CloseTransaction();
		}
		
		Model->Notify(EAnimDataModelNotifyType::BracketClosed);
	}
}

void UAnimDataController::SetPlayLength(float Length, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	if (!FMath::IsNearlyZero(Length) && Length > 0.f)
	{
		if (Length != Model->GetPlayLength())
		{
			CONDITIONAL_TRANSACTION(LOCTEXT("SetPlayLength", "Setting Play Length"));
			SetPlayLength_Internal(Length, 0.f, Model->PlayLength, bShouldTransact);
		}
	}
	else
	{
		ReportWarningf(LOCTEXT("InvalidPlayLengthWarning", "Invalid play length value provided: {0} seconds"), FText::AsNumber(Length));
	}
}

void UAnimDataController::Resize(float Length, float T0, float T1, bool bShouldTransact /*= true*/)
{
	ValidateModel();
	
	const TRange<float> PlayRange(TRange<float>::BoundsType::Inclusive(0.f), TRange<float>::BoundsType::Inclusive(Model->PlayLength));
	if (!FMath::IsNearlyZero(Length) && Length > 0.f)
	{
		if (Length != Model->PlayLength)
		{
			// Ensure that T0 is within the curent play range
			if (PlayRange.Contains(T0))
			{
				// Ensure that the start and end length of either removal or insertion are valid
				if (T0 < T1)
				{
					CONDITIONAL_BRACKET(LOCTEXT("ResizeModel", "Resizing Animation Data"));
					const bool bInserted = Length > Model->PlayLength;
					SetPlayLength_Internal(Length, T0, T1, bShouldTransact);
					ResizeCurves(Length, bInserted, T0, T1, bShouldTransact);
				}
				else
				{
					ReportErrorf(LOCTEXT("InvalidEndTimeError", "Invalid T1, smaller that T0 value: T0 {0}, T1 {1}"), FText::AsNumber(T0), FText::AsNumber(T1));
				}
			}
			else
			{
				ReportErrorf(LOCTEXT("InvalidStartTimeError", "Invalid T0, not within existing play range: T0 {0}, Play Length {1}"), FText::AsNumber(T0), FText::AsNumber(Model->PlayLength));
			}			
		}
		else
		{
			ReportWarningf(LOCTEXT("SamePlayLengthWarning", "New play length is same as existing one: {0} seconds"), FText::AsNumber(Length));
		}
	}
	else
	{
		ReportErrorf(LOCTEXT("InvalidPlayLengthError", "Invalid play length value provided: {0} seconds"), FText::AsNumber(Length));
	}
}

void UAnimDataController::SetFrameRate(FFrameRate FrameRate, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	// Disallow invalid frame-rates, or 0.0 intervals
	const float FrameRateInterval = FrameRate.AsInterval();
	if ( FrameRate.IsValid() && !FMath::IsNearlyZero(FrameRateInterval) && FrameRateInterval > 0.f)
	{
		CONDITIONAL_TRANSACTION(LOCTEXT("SetFrameRate", "Setting Frame Rate"));

		CONDITIONAL_ACTION(UE::Anim::FSetFrameRateAction, Model);

		FFrameRateChangedPayload Payload;
		Payload.PreviousFrameRate = Model->FrameRate;

		Model->FrameRate = FrameRate;
		Model->NumberOfFrames = Model->FrameRate.AsFrameTime(Model->PlayLength).RoundToFrame().Value;
		Model->NumberOfKeys = Model->NumberOfFrames + 1;

		Model->Notify(EAnimDataModelNotifyType::FrameRateChanged, Payload);
	}
	else
	{
		ReportErrorf(LOCTEXT("InvalidFrameRateError", "Invalid frame rate provided: {0}"), FrameRate.ToPrettyText());
	}
}


void UAnimDataController::UpdateCurveNamesFromSkeleton(const USkeleton* Skeleton, ERawCurveTrackTypes SupportedCurveType, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	if (Skeleton)
	{
		if (IsSupportedCurveType(SupportedCurveType))
		{
			CONDITIONAL_BRACKET(LOCTEXT("ValidateRawCurves", "Validating Animation Curve Names"));
			switch (SupportedCurveType)
			{
			case ERawCurveTrackTypes::RCT_Float:
			{
				const FSmartNameMapping* NameMapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
				for (FFloatCurve& FloatCurve : Model->CurveData.FloatCurves)
				{
					FSmartName NewSmartName = FloatCurve.Name;
					NameMapping->GetName(FloatCurve.Name.UID, NewSmartName.DisplayName);
					if (NewSmartName != FloatCurve.Name)
					{
						const FAnimationCurveIdentifier CurrentId(FloatCurve.Name, SupportedCurveType);
						const FAnimationCurveIdentifier NewId(NewSmartName, SupportedCurveType);
						RenameCurve(CurrentId, NewId, bShouldTransact);
					}
				}
				break;
			}
			case ERawCurveTrackTypes::RCT_Transform:
			{
				const FSmartNameMapping* NameMapping = Skeleton->GetSmartNameContainer(USkeleton::AnimTrackCurveMappingName);
				for (FTransformCurve& TransformCurve : Model->CurveData.TransformCurves)
				{
					FSmartName NewSmartName = TransformCurve.Name;
					NameMapping->GetName(TransformCurve.Name.UID, NewSmartName.DisplayName);
					if (NewSmartName != TransformCurve.Name)
					{
						const FAnimationCurveIdentifier CurrentId(TransformCurve.Name, SupportedCurveType);
						const FAnimationCurveIdentifier NewId(NewSmartName, SupportedCurveType);
						RenameCurve(CurrentId, NewId, bShouldTransact);
					}
				}
				break;
			}
			}
		}
		else
		{
			const FString CurveTypeAsString = GetCurveTypeValueName(SupportedCurveType);
			ReportWarningf(LOCTEXT("InvalidCurveTypeWarning", "Invalid curve type provided: {0} ({1})"), FText::FromString(CurveTypeAsString), FText::AsNumber((int32)SupportedCurveType));
		}
	}
	else
	{
		ReportError(LOCTEXT("InvalidSkeletonError", "Invalid USkeleton supplied"));
	}
}

void UAnimDataController::FindOrAddCurveNamesOnSkeleton(USkeleton* Skeleton, ERawCurveTrackTypes SupportedCurveType, bool bShouldTransact)
{
	ValidateModel();
	
	if (Skeleton)
	{
		if (IsSupportedCurveType(SupportedCurveType))
		{
			CONDITIONAL_BRACKET(LOCTEXT("FindOrAddRawCurveNames", "Updating Skeleton with Animation Curve Names"));
			switch (SupportedCurveType)
			{
			case ERawCurveTrackTypes::RCT_Float:
			{
				for (FFloatCurve& FloatCurve : Model->CurveData.FloatCurves)
				{
					FSmartName NewSmartName = FloatCurve.Name;
					Skeleton->VerifySmartName(USkeleton::AnimCurveMappingName, NewSmartName);
					if (NewSmartName != FloatCurve.Name)
					{
						const FAnimationCurveIdentifier CurrentId(FloatCurve.Name, SupportedCurveType);
						const FAnimationCurveIdentifier NewId(NewSmartName, SupportedCurveType);
						RenameCurve(CurrentId, NewId, bShouldTransact);
					}
				}
				break;
			}
			case ERawCurveTrackTypes::RCT_Transform:
			{
				for (FTransformCurve& TransformCurve : Model->CurveData.TransformCurves)
				{
					FSmartName NewSmartName = TransformCurve.Name;
					Skeleton->VerifySmartName(USkeleton::AnimTrackCurveMappingName, NewSmartName);
					if (NewSmartName != TransformCurve.Name)
					{
						const FAnimationCurveIdentifier CurrentId(TransformCurve.Name, SupportedCurveType);
						const FAnimationCurveIdentifier NewId(NewSmartName, SupportedCurveType);
						RenameCurve(CurrentId, NewId, bShouldTransact);
					}
				}
				break;
			}
			}
		}
		else
		{
			const FString CurveTypeAsString = GetCurveTypeValueName(SupportedCurveType);
			ReportWarningf(LOCTEXT("InvalidCurveTypeWarning", "Invalid curve type provided: {0} ({1})"), FText::FromString(CurveTypeAsString), FText::AsNumber((int32)SupportedCurveType));
		}
	}
	else
	{
		ReportError(LOCTEXT("InvalidSkeletonError", "Invalid USkeleton supplied "));
	}
}

bool UAnimDataController::RemoveBoneTracksMissingFromSkeleton(const USkeleton* Skeleton, bool bShouldTransact /*= true*/)
{
	if (!CheckOuterClass(UAnimSequence::StaticClass()))
	{
		return false;
	}

	if (Skeleton)
	{
		TArray<FName> TracksToBeRemoved;
		const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();

		for (FBoneAnimationTrack& Track : Model->BoneAnimationTracks)
		{
			if (ReferenceSkeleton.IsValidIndex(Track.BoneTreeIndex))
			{
				const FName BoneName = ReferenceSkeleton.GetBoneName(Track.BoneTreeIndex);
				if (BoneName != Track.Name)
				{
					// Rename track			
					Track.Name = BoneName;
				}
			}
			else
			{
				// Try find correct bone index
				const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(Track.Name);

				if (BoneIndex != INDEX_NONE)
				{
					// Update bone index
					Track.BoneTreeIndex = BoneIndex;
				}
				else
				{
					// Remove track
					TracksToBeRemoved.Add(Track.Name);
					ReportWarningf(LOCTEXT("InvalidBoneIndexWarning", "Unable to find bone index, animation track will be removed: {0}"), FText::FromName(Track.Name));
				}
			}
		}

		if (TracksToBeRemoved.Num())
		{
			CONDITIONAL_BRACKET(LOCTEXT("RemoveBoneTracksMissingFromSkeleton", "Validating Bone Animation Track Data against Skeleton"));
			for (const FName& TrackName : TracksToBeRemoved)
			{
				RemoveBoneTrack(TrackName);
			}
		}

		return TracksToBeRemoved.Num() > 0;
	}
	else
	{
		ReportError(LOCTEXT("InvalidSkeletonError", "Invalid USkeleton supplied"));
	}

	return false;
}


void UAnimDataController::ResetModel(bool bShouldTransact /*= true*/)
{
	ValidateModel();

	CONDITIONAL_BRACKET(LOCTEXT("ResetModel", "Clearing Animation Data"));

	RemoveAllBoneTracks();

	RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Float, bShouldTransact);
	RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Transform, bShouldTransact);

	SetPlayLength(MINIMUM_ANIMATION_LENGTH);
	SetFrameRate(FFrameRate(30,1));

	Model->Notify(EAnimDataModelNotifyType::Reset);
}

bool UAnimDataController::AddCurve(const FAnimationCurveIdentifier& CurveId, int32 CurveFlags /*= EAnimAssetCurveFlags::AACF_Editable*/, bool bShouldTransact /*= true*/)
{
	ValidateModel();
	if (CurveId.InternalName.IsValid())
	{		
		if (IsSupportedCurveType(CurveId.CurveType))
		{
			if (!Model->FindCurve(CurveId))
			{
				CONDITIONAL_TRANSACTION(LOCTEXT("AddRawCurve", "Adding Animation Curve"));

				FCurveAddedPayload Payload;
				Payload.Identifier = CurveId;
				
				auto AddNewCurve = [CurveName = CurveId.InternalName, CurveFlags](auto& CurveTypeArray)
				{
					CurveTypeArray.Add({ CurveName, CurveFlags});
				};
				
				switch (CurveId.CurveType)
				{
				case ERawCurveTrackTypes::RCT_Transform:
					AddNewCurve(Model->CurveData.TransformCurves);
					break;
				case ERawCurveTrackTypes::RCT_Float:
					AddNewCurve(Model->CurveData.FloatCurves);
					break;
				}

				CONDITIONAL_ACTION(UE::Anim::FRemoveCurveAction, CurveId);
				Model->Notify(EAnimDataModelNotifyType::CurveAdded, Payload);

				return true;
			}
			else
			{
				const FString CurveTypeAsString = GetCurveTypeValueName(CurveId.CurveType);
				ReportWarningf(LOCTEXT("ExistingCurveNameWarning", "Curve with name {0} and type {1} ({2}) already exists"), FText::FromName(CurveId.InternalName.DisplayName), FText::FromString(CurveTypeAsString), FText::AsNumber((int32)CurveId.CurveType));
			}			
		}
		else 
		{
			const FString CurveTypeAsString = GetCurveTypeValueName(CurveId.CurveType);
			ReportWarningf(LOCTEXT("InvalidCurveTypeWarning", "Invalid curve type provided: {0} ({1})"), FText::FromString(CurveTypeAsString), FText::AsNumber((int32)CurveId.CurveType));
		}		
	}
	else
	{
		const FString CurveTypeAsString = GetCurveTypeValueName(CurveId.CurveType);
		ReportWarningf(LOCTEXT("InvalidCurveIdentifierWarning", "Invalid curve identifier provided: name: {0}, UID: {1} type: {2}"), FText::FromName(CurveId.InternalName.DisplayName), FText::AsNumber(CurveId.InternalName.UID), FText::FromString(CurveTypeAsString));
	}

	return false;
}

bool UAnimDataController::DuplicateCurve(const FAnimationCurveIdentifier& CopyCurveId, const FAnimationCurveIdentifier& NewCurveId, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	ERawCurveTrackTypes SupportedCurveType = CopyCurveId.CurveType;

	if (CopyCurveId.InternalName.IsValid() && NewCurveId.InternalName.IsValid())
	{
		if (IsSupportedCurveType(SupportedCurveType))
		{
			if (CopyCurveId.CurveType == NewCurveId.CurveType)
			{
				if (Model->FindCurve(CopyCurveId))
				{
					if (!Model->FindCurve(NewCurveId))
					{
						CONDITIONAL_TRANSACTION(LOCTEXT("CopyRawCurve", "Duplicating Animation Curve"));

						auto DuplicateCurve = [NewCurveName = NewCurveId.InternalName](auto& CurveDataArray, const auto& SourceCurve)
						{
							auto& DuplicatedCurve = CurveDataArray.Add_GetRef( { NewCurveName, SourceCurve.GetCurveTypeFlags() });
							DuplicatedCurve.CopyCurve(SourceCurve);
						};
						
						switch (SupportedCurveType)
						{
						case ERawCurveTrackTypes::RCT_Transform:
							DuplicateCurve(Model->CurveData.TransformCurves, Model->GetTransformCurve(CopyCurveId));
							break;
						case ERawCurveTrackTypes::RCT_Float:
							DuplicateCurve(Model->CurveData.FloatCurves, Model->GetFloatCurve(CopyCurveId));
							break;
						}

						FCurveAddedPayload Payload;
						Payload.Identifier = NewCurveId;
						Model->Notify(EAnimDataModelNotifyType::CurveAdded, Payload);

						CONDITIONAL_ACTION(UE::Anim::FRemoveCurveAction, NewCurveId);

						return true;
					}
					else
					{
						const FString CurveTypeAsString = GetCurveTypeValueName(NewCurveId.CurveType);
						ReportWarningf(LOCTEXT("ExistingCurveNameWarning", "Curve with name {0} and type {1} ({2}) already exists"), FText::FromName(NewCurveId.InternalName.DisplayName), FText::FromString(CurveTypeAsString), FText::AsNumber((int32)NewCurveId.CurveType));
					}
				}
				else
				{
					const FString CurveTypeAsString = GetCurveTypeValueName(CopyCurveId.CurveType);
					ReportWarningf(LOCTEXT("CurveNameToDuplicateNotFoundWarning", "Could not find curve with name {0} and type {1} ({2}) for duplication"), FText::FromName(NewCurveId.InternalName.DisplayName), FText::FromString(CurveTypeAsString), FText::AsNumber((int32)NewCurveId.CurveType));
				}
			}
		}
		else
		{
			const FString CurveTypeAsString = GetCurveTypeValueName(SupportedCurveType);
			ReportWarningf(LOCTEXT("InvalidCurveTypeWarning", "Invalid curve type provided: {0} ({1})"), FText::FromString(CurveTypeAsString), FText::AsNumber((int32)SupportedCurveType));
		}
	}

	return false;
}


bool UAnimDataController::RemoveCurve(const FAnimationCurveIdentifier& CurveId, bool bShouldTransact /*= true*/)
{
	ValidateModel();
	ERawCurveTrackTypes SupportedCurveType = CurveId.CurveType;

	if (CurveId.InternalName.IsValid())
	{
		if (IsSupportedCurveType(CurveId.CurveType))
		{
			const FAnimCurveBase* Curve = Model->FindCurve(CurveId);
			if (Curve)
			{
				CONDITIONAL_TRANSACTION(LOCTEXT("RemoveCurve", "Removing Animation Curve"));

				switch (SupportedCurveType)
				{
					case ERawCurveTrackTypes::RCT_Transform:
					{
						const FTransformCurve& TransformCurve = Model->GetTransformCurve(CurveId);
						CONDITIONAL_ACTION(UE::Anim::FAddTransformCurveAction, CurveId, TransformCurve.GetCurveTypeFlags(), TransformCurve);
						Model->CurveData.TransformCurves.RemoveAll([Name = TransformCurve.Name](const FTransformCurve& ToRemoveCurve) { return ToRemoveCurve.Name == Name; });
						break;
					}
					case ERawCurveTrackTypes::RCT_Float:
					{
						const FFloatCurve& FloatCurve = Model->GetFloatCurve(CurveId);
						CONDITIONAL_ACTION(UE::Anim::FAddFloatCurveAction, CurveId, FloatCurve.GetCurveTypeFlags(), FloatCurve.FloatCurve.GetConstRefOfKeys(), FloatCurve.Color);
						Model->CurveData.FloatCurves.RemoveAll([Name = FloatCurve.Name](const FFloatCurve& ToRemoveCurve) { return ToRemoveCurve.Name == Name; });
						break;
					}
				}

				FCurveRemovedPayload Payload;
				Payload.Identifier = CurveId;
				Model->Notify(EAnimDataModelNotifyType::CurveRemoved, Payload);

				return true;
			}
			else
			{
				const FString CurveTypeAsString = GetCurveTypeValueName(SupportedCurveType);
				ReportWarningf(LOCTEXT("UnableToFindCurveForRemovalWarning", "Unable to find curve: {0} of type {1}"), FText::FromName(CurveId.InternalName.DisplayName), FText::FromString(CurveTypeAsString));
			}
		}
		else
		{
			const FString CurveTypeAsString = GetCurveTypeValueName(CurveId.CurveType);
			ReportWarningf(LOCTEXT("InvalidCurveTypeWarning", "Invalid curve type provided: {0} ({1})"), FText::FromString(CurveTypeAsString), FText::AsNumber((int32)CurveId.CurveType));
		}
	}

	return false;
}

void UAnimDataController::RemoveAllCurvesOfType(ERawCurveTrackTypes SupportedCurveType /*= ERawCurveTrackTypes::RCT_Float*/, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	CONDITIONAL_BRACKET(LOCTEXT("DeleteAllRawCurve", "Deleting All Animation Curve"));
	switch (SupportedCurveType)
	{
	case ERawCurveTrackTypes::RCT_Transform:
	{
		TArray<FTransformCurve> TransformCurves = Model->CurveData.TransformCurves;
		for (const FTransformCurve& Curve : TransformCurves)
		{
			RemoveCurve(FAnimationCurveIdentifier(Curve.Name, ERawCurveTrackTypes::RCT_Transform), bShouldTransact);
		}
		break;
	}
	case ERawCurveTrackTypes::RCT_Float:
	{
		TArray<FFloatCurve> FloatCurves = Model->CurveData.FloatCurves;
		for (const FFloatCurve& Curve : FloatCurves)
		{
			RemoveCurve(FAnimationCurveIdentifier(Curve.Name, ERawCurveTrackTypes::RCT_Float), bShouldTransact);
		}
		break;
	}
	case ERawCurveTrackTypes::RCT_Vector:
	default:
		const FString CurveTypeAsString = GetCurveTypeValueName(SupportedCurveType);
		ReportWarningf(LOCTEXT("InvalidCurveTypeWarning", "Invalid curve type provided: {0} ({1})"), FText::FromString(CurveTypeAsString), FText::AsNumber((int32)SupportedCurveType));
	}

}

bool UAnimDataController::SetCurveFlag(const FAnimationCurveIdentifier& CurveId, EAnimAssetCurveFlags Flag, bool bState /*= true*/, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	ERawCurveTrackTypes SupportedCurveType = CurveId.CurveType;

	FAnimCurveBase* Curve = nullptr;

	if (SupportedCurveType == ERawCurveTrackTypes::RCT_Float)
	{
		Curve = Model->FindMutableFloatCurveById(CurveId);
	}
	else if (SupportedCurveType == ERawCurveTrackTypes::RCT_Transform)
	{
		Curve = Model->FindMutableTransformCurveById(CurveId);
	}
	
	if (Curve)
	{
		CONDITIONAL_TRANSACTION(LOCTEXT("SetCurveFlag", "Setting Raw Curve Flag"));

		const int32 CurrentFlags = Curve->GetCurveTypeFlags();

		CONDITIONAL_ACTION(UE::Anim::FSetCurveFlagsAction, CurveId, CurrentFlags, SupportedCurveType);

		FCurveFlagsChangedPayload Payload;
		Payload.Identifier = CurveId;
		Payload.OldFlags = Curve->GetCurveTypeFlags();

		Curve->SetCurveTypeFlag(Flag, bState);

		Model->Notify(EAnimDataModelNotifyType::CurveFlagsChanged, Payload);

		return true;
	}
	else
	{
		const FString CurveTypeAsString = GetCurveTypeValueName(SupportedCurveType);
		ReportWarningf(LOCTEXT("UnableToFindCurveWarning", "Unable to find curve: {0} of type {1}"), FText::FromName(CurveId.InternalName.DisplayName), FText::FromString(CurveTypeAsString));
	}

	return false;
}

bool UAnimDataController::SetCurveFlags(const FAnimationCurveIdentifier& CurveId, int32 Flags, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	FAnimCurveBase* Curve = nullptr;

	ERawCurveTrackTypes SupportedCurveType = CurveId.CurveType;

	if (SupportedCurveType == ERawCurveTrackTypes::RCT_Float)
	{
		Curve = Model->FindMutableFloatCurveById(CurveId);
	}
	else if (SupportedCurveType == ERawCurveTrackTypes::RCT_Transform)
	{
		Curve = Model->FindMutableTransformCurveById(CurveId);
	}

	if (Curve)
	{
		CONDITIONAL_TRANSACTION(LOCTEXT("SetCurveFlag", "Setting Raw Curve Flags"));

		const int32 CurrentFlags = Curve->GetCurveTypeFlags();

		CONDITIONAL_ACTION(UE::Anim::FSetCurveFlagsAction, CurveId, CurrentFlags, SupportedCurveType);

		FCurveFlagsChangedPayload Payload;
		Payload.Identifier = CurveId;
		Payload.OldFlags = Curve->GetCurveTypeFlags();

		Curve->SetCurveTypeFlags(Flags);

		Model->Notify(EAnimDataModelNotifyType::CurveFlagsChanged, Payload);

		return true;
	}
	else
	{
		const FString CurveTypeAsString = GetCurveTypeValueName(SupportedCurveType);
		ReportWarningf(LOCTEXT("UnableToFindCurveForRemovalWarning", "Unable to find curve: {0} of type {1}"), FText::FromName(CurveId.InternalName.DisplayName), FText::FromString(CurveTypeAsString));
	}

	return false;
}

bool UAnimDataController::SetTransformCurveKeys(const FAnimationCurveIdentifier& CurveId, const TArray<FTransform>& TransformValues, const TArray<float>& TimeKeys, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	if (TransformValues.Num() == TimeKeys.Num())
	{
		FTransformCurve* Curve = Model->FindMutableTransformCurveById(CurveId);

		if (Curve)
		{
			CONDITIONAL_BRACKET(LOCTEXT("SetTransformCurveKeys_Bracket", "Setting Transform Curve Keys"));
			
			struct FKeys
			{
				FKeys(int32 NumKeys)
				{
					for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
					{
						ChannelKeys[ChannelIndex].SetNum(NumKeys);
					}
				}

				TArray<FRichCurveKey> ChannelKeys[3];
			};

			FKeys TranslationKeys(TransformValues.Num());
			FKeys RotationKeys(TransformValues.Num());
			FKeys ScaleKeys(TransformValues.Num());

			FKeys* SubCurveKeys[3] = { &TranslationKeys, &RotationKeys, &ScaleKeys };

			// Generate the curve keys
			for (int32 KeyIndex = 0; KeyIndex < TransformValues.Num(); ++KeyIndex)
			{
				const FTransform& Value = TransformValues[KeyIndex];
				const float& Time = TimeKeys[KeyIndex];

				const FVector Translation = Value.GetLocation();
				const FVector Rotation = Value.GetRotation().Euler();
				const FVector Scale = Value.GetScale3D();

				auto SetKey = [Time, KeyIndex](FKeys& Key, const FVector& Vector)
				{
					for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
					{
						Key.ChannelKeys[ChannelIndex][KeyIndex] = FRichCurveKey(Time, Vector[ChannelIndex]);
					}
				};

				SetKey(TranslationKeys, Translation);
				SetKey(RotationKeys, Rotation);
				SetKey(ScaleKeys, Scale);
			}
			
			for (int32 SubCurveIndex = 0; SubCurveIndex < 3; ++SubCurveIndex)
			{
				const ETransformCurveChannel Channel = (ETransformCurveChannel)SubCurveIndex;
				FKeys* CurveKeys = SubCurveKeys[SubCurveIndex];
				for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
				{
					const EVectorCurveChannel Axis = (EVectorCurveChannel)ChannelIndex;
					FAnimationCurveIdentifier TargetCurveIdentifier = CurveId;
					UAnimationCurveIdentifierExtensions::GetTransformChildCurveIdentifier(TargetCurveIdentifier, Channel, Axis);
					SetCurveKeys(TargetCurveIdentifier, CurveKeys->ChannelKeys[ChannelIndex], bShouldTransact);
				}
			}

			return true;
		}
		else
		{
			ReportWarningf(LOCTEXT("UnableToFindTransformCurveWarning", "Unable to find transform curve: {0}"), FText::FromName(CurveId.InternalName.DisplayName));
		}
	}
	else
	{
		// time value mismatch
		ReportWarningf(LOCTEXT("InvalidNumberOfTimeAndKeyEntriesWarning", "Number of times and key entries do not match: number of time values {0}, number of key values {1}"), FText::AsNumber(TimeKeys.Num()), FText::AsNumber(TransformValues.Num()));
	}

	return false;	
}


bool UAnimDataController::SetTransformCurveKey(const FAnimationCurveIdentifier& CurveId, float Time, const FTransform& Value, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	FTransformCurve* Curve = Model->FindMutableTransformCurveById(CurveId);

	if (Curve)
	{
		CONDITIONAL_BRACKET(LOCTEXT("AddTransformCurveKey_Bracket", "Setting Transform Curve Key"));
		struct FKeys
		{
			FRichCurveKey ChannelKeys[3];
		};

		FKeys VectorKeys[3];
		
		// Generate the rich curve keys		
		const FVector Translation = Value.GetLocation();
		const FVector Rotation = Value.GetRotation().Euler();
		const FVector Scale = Value.GetScale3D();

		auto SetKey = [Time](FKeys& Key, const FVector& Vector)
		{
			for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
			{
				Key.ChannelKeys[ChannelIndex] = FRichCurveKey(Time, Vector[ChannelIndex]);
			}
		};

		SetKey(VectorKeys[0], Translation);
		SetKey(VectorKeys[1], Rotation);
		SetKey(VectorKeys[2], Scale);
		
		for (int32 SubCurveIndex = 0; SubCurveIndex < 3; ++SubCurveIndex)
		{
			const ETransformCurveChannel Channel = (ETransformCurveChannel)SubCurveIndex;
			const FKeys& VectorCurveKeys = VectorKeys[SubCurveIndex];
			for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
			{
				const EVectorCurveChannel Axis = (EVectorCurveChannel)ChannelIndex;
				FAnimationCurveIdentifier TargetCurveIdentifier = CurveId;
				UAnimationCurveIdentifierExtensions::GetTransformChildCurveIdentifier(TargetCurveIdentifier, Channel, Axis);
				SetCurveKey(TargetCurveIdentifier, VectorCurveKeys.ChannelKeys[ChannelIndex], bShouldTransact);
			}
		}

		return true;
	}
	else
	{
		ReportWarningf(LOCTEXT("UnableToFindTransformCurveWarning", "Unable to find transform curve: {0}"), FText::FromName(CurveId.InternalName.DisplayName));
	}

	return false;
}


bool UAnimDataController::RemoveTransformCurveKey(const FAnimationCurveIdentifier& CurveId, float Time, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	FTransformCurve* TransformCurve = Model->FindMutableTransformCurveById(CurveId);
	if (TransformCurve)
	{
		const FString BaseCurveName = CurveId.InternalName.DisplayName.ToString();
		const TArray<FString> SubCurveNames = { TEXT( "Translation"), TEXT( "Rotation"), TEXT( "Scale") };
		const TArray<FString> ChannelCurveNames = { TEXT("X"), TEXT("Y"), TEXT("Z") };

		CONDITIONAL_BRACKET(LOCTEXT("RemoveTransformCurveKey_Bracket", "Deleting Animation Transform Curve Key"));
		
		for (int32 SubCurveIndex = 0; SubCurveIndex < 3; ++SubCurveIndex)
		{
			const ETransformCurveChannel Channel = (ETransformCurveChannel)SubCurveIndex;
			for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
			{
				const EVectorCurveChannel Axis = (EVectorCurveChannel)ChannelIndex;
				FAnimationCurveIdentifier TargetCurveIdentifier = CurveId;
				UAnimationCurveIdentifierExtensions::GetTransformChildCurveIdentifier(TargetCurveIdentifier, Channel, Axis);
				RemoveCurveKey(TargetCurveIdentifier, Time, bShouldTransact);
			}
		}


		return true;

	}
	else
	{
		ReportWarningf(LOCTEXT("UnableToFindTransformCurveWarning", "Unable to find transform curve: {0}"), FText::FromName(CurveId.InternalName.DisplayName));
	}

	return false;
}

bool UAnimDataController::RenameCurve(const FAnimationCurveIdentifier& CurveToRenameId, const FAnimationCurveIdentifier& NewCurveId, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	if (NewCurveId.IsValid())
	{
		if (CurveToRenameId != NewCurveId)
		{
			if (CurveToRenameId.CurveType == NewCurveId.CurveType)
			{
				FAnimCurveBase* Curve = Model->FindMutableCurveById(CurveToRenameId);
				if (Curve)
				{
					CONDITIONAL_TRANSACTION(LOCTEXT("RenameCurve", "Renaming Curve"));

					FCurveRenamedPayload Payload;
					Payload.Identifier = FAnimationCurveIdentifier(Curve->Name, CurveToRenameId.CurveType);

					Curve->Name = NewCurveId.InternalName;
					Payload.NewIdentifier = NewCurveId;

					CONDITIONAL_ACTION(UE::Anim::FRenameCurveAction, NewCurveId, CurveToRenameId);

					Model->Notify(EAnimDataModelNotifyType::CurveRenamed, Payload);

					return true;
				}
				else
				{
					const FString CurveTypeAsString = GetCurveTypeValueName(CurveToRenameId.CurveType);
					ReportWarningf(LOCTEXT("UnableToFindCurveWarning", "Unable to find curve: {0} of type {1}"), FText::FromName(CurveToRenameId.InternalName.DisplayName), FText::FromString(CurveTypeAsString));
				}
			}
			else
			{
				const FString CurrentCurveTypeAsString = GetCurveTypeValueName(CurveToRenameId.CurveType);
				const FString NewCurveTypeAsString = GetCurveTypeValueName(NewCurveId.CurveType);
				ReportWarningf(LOCTEXT("MismatchOfCurveTypesWarning", "Different curve types provided between current and new curve names: {0} ({1}) and {2} ({3})"), FText::FromName(CurveToRenameId.InternalName.DisplayName), FText::FromString(CurrentCurveTypeAsString),
					FText::FromName(NewCurveId.InternalName.DisplayName), FText::FromString(NewCurveTypeAsString));
			}
		}
		else
		{
			ReportWarningf(LOCTEXT("MatchingCurveNamesWarning", "Provided curve names are the same: {0}"), FText::FromName(CurveToRenameId.InternalName.DisplayName));
		}
		
	}
	else
	{
		ReportWarningf(LOCTEXT("InvalidCurveIdentiferProvidedWarning", "Invalid new curve identifier provided: {2} ({3})"), FText::FromName(NewCurveId.InternalName.DisplayName), FText::AsNumber(NewCurveId.InternalName.UID));
	}

	return false;
}

bool UAnimDataController::SetCurveColor(const FAnimationCurveIdentifier& CurveId, FLinearColor Color, bool bShouldTransact)
{
	ValidateModel();

	if (CurveId.IsValid())
	{
		if (CurveId.CurveType == ERawCurveTrackTypes::RCT_Float)
		{
			FFloatCurve* Curve = Model->FindMutableFloatCurveById(CurveId);
			if (Curve)
			{
				CONDITIONAL_TRANSACTION(LOCTEXT("ChangingCurveColor", "Changing Curve Color"));

				CONDITIONAL_ACTION(UE::Anim::FSetCurveColorAction, CurveId, Curve->Color);

				Curve->Color = Color;

				FCurveChangedPayload Payload;
				Payload.Identifier = CurveId;
				Model->Notify(EAnimDataModelNotifyType::CurveColorChanged, Payload);

				return true;				
			}
			else
			{
				const FString CurveTypeAsString = GetCurveTypeValueName(CurveId.CurveType);
				ReportWarningf(LOCTEXT("UnableToFindCurveWarning", "Unable to find curve: {0} of type {1}"), FText::FromName(CurveId.InternalName.DisplayName), FText::FromString(CurveTypeAsString));
			}
		}
		else
		{
			ReportWarning(LOCTEXT("NonSupportedCurveColorSetWarning", "Changing curve color is currently only supported for float curves"));
		}
	}
	else
	{
		ReportWarningf(LOCTEXT("InvalidCurveIdentifier", "Invalid Curve Identifier : {0} ({1})"), FText::FromName(CurveId.InternalName.DisplayName), FText::AsNumber(CurveId.InternalName.UID));
	}	

	return false;
}

bool UAnimDataController::ScaleCurve(const FAnimationCurveIdentifier& CurveId, float Origin, float Factor, bool bShouldTransact /*= true*/)
{
	ValidateModel();

	ERawCurveTrackTypes SupportedCurveType = CurveId.CurveType;
	if (SupportedCurveType == ERawCurveTrackTypes::RCT_Float)
	{
		FFloatCurve* Curve = Model->FindMutableFloatCurveById(CurveId);
		if (Curve)
		{
			CONDITIONAL_TRANSACTION(LOCTEXT("ScalingCurve", "Scaling Curve"));

			Curve->FloatCurve.ScaleCurve(Origin, Factor);

			FCurveScaledPayload Payload;
			Payload.Identifier = CurveId;
			Payload.Factor = Factor;
			Payload.Origin = Origin;
			
			CONDITIONAL_ACTION(UE::Anim::FScaleCurveAction, CurveId, Origin, 1.0f / Factor, SupportedCurveType);

			Model->Notify(EAnimDataModelNotifyType::CurveScaled, Payload);

			return true;
		}
		else
		{
			ReportWarningf(LOCTEXT("UnableToFindFloatCurveWarning", "Unable to find float curve: {0}"), FText::FromName(CurveId.InternalName.DisplayName));
		}
	}
	else
	{
		ReportWarning(LOCTEXT("NonSupportedCurveScalingWarning", "Scaling curves is currently only supported for float curves"));
	}

	return false;
}

bool UAnimDataController::SetCurveKey(const FAnimationCurveIdentifier& CurveId, const FRichCurveKey& Key, bool bShouldTransact)
{
	ValidateModel();

	FRichCurve* RichCurve = Model->GetMutableRichCurve(CurveId);
	ERawCurveTrackTypes SupportedCurveType = CurveId.CurveType;
	if (RichCurve)
	{
		FCurveChangedPayload Payload;
		Payload.Identifier = CurveId;

		// Set or add rich curve value
		const FKeyHandle Handle = RichCurve->FindKey(Key.Time, 0.f);
		if (Handle != FKeyHandle::Invalid())
		{
			CONDITIONAL_TRANSACTION(LOCTEXT("SetNamedCurveKey", "Setting Curve Key"));
			// Cache old value for action
			const FRichCurveKey CurrentKey = RichCurve->GetKey(Handle);
			CONDITIONAL_ACTION(UE::Anim::FSetRichCurveKeyAction, CurveId, CurrentKey);

			// Set the new value
			RichCurve->SetKeyValue(Handle, Key.Value);

			Model->Notify(EAnimDataModelNotifyType::CurveChanged, Payload);
		}
		else
		{
			CONDITIONAL_TRANSACTION(LOCTEXT("AddNamedCurveKey", "Adding Curve Key"));
			CONDITIONAL_ACTION(UE::Anim::FRemoveRichCurveKeyAction, CurveId, Key.Time);

			// Add the new key
			RichCurve->AddKey(Key.Time, Key.Value);

			Model->Notify(EAnimDataModelNotifyType::CurveChanged, Payload);
		}

		return true;
	}

	return false;
}

bool UAnimDataController::RemoveCurveKey(const FAnimationCurveIdentifier& CurveId, float Time, bool bShouldTransact)
{
	ValidateModel();

	FRichCurve* RichCurve = Model->GetMutableRichCurve(CurveId);
	ERawCurveTrackTypes SupportedCurveType = CurveId.CurveType;
	if (RichCurve)
	{
		FCurveChangedPayload Payload;
		Payload.Identifier = CurveId;

		// Remove key at time value		
		const FKeyHandle Handle = RichCurve->FindKey(Time, 0.f);
		if (Handle != FKeyHandle::Invalid())
		{
			CONDITIONAL_TRANSACTION(LOCTEXT("RemoveNamedCurveKey", "Removing Curve Key"));

			// Cached current value for action
			const FRichCurveKey CurrentKey = RichCurve->GetKey(Handle);
			CONDITIONAL_ACTION(UE::Anim::FAddRichCurveKeyAction, CurveId, CurrentKey);

			RichCurve->DeleteKey(Handle);

			Model->Notify(EAnimDataModelNotifyType::CurveChanged, Payload);

			return true;
		}
		else
		{
			ReportErrorf(LOCTEXT("RichCurveKeyNotFoundError", "Unable to find rich curve key: curve name {0}, time {1}"), FText::FromName(CurveId.InternalName.DisplayName), FText::AsNumber(Time));
		}
	}

	return false;
}


bool UAnimDataController::SetCurveKeys(const FAnimationCurveIdentifier& CurveId, const TArray<FRichCurveKey>& CurveKeys, bool bShouldTransact)
{
	ValidateModel();

	FRichCurve* RichCurve = Model->GetMutableRichCurve(CurveId);
	ERawCurveTrackTypes SupportedCurveType = CurveId.CurveType;
	if (RichCurve)
	{
		CONDITIONAL_TRANSACTION(LOCTEXT("SettingNamedCurveKeys", "Setting Curve Keys"));
		CONDITIONAL_ACTION(UE::Anim::FSetRichCurveKeysAction, CurveId, RichCurve->GetConstRefOfKeys());

		// Set rich curve values
		RichCurve->SetKeys(CurveKeys);

		FCurveChangedPayload Payload;
		Payload.Identifier = CurveId;
		Model->Notify(EAnimDataModelNotifyType::CurveChanged, Payload);

		return true;
	}

	return false;
}

void UAnimDataController::NotifyPopulated()
{
	ValidateModel();
	Model->Notify(EAnimDataModelNotifyType::Populated);
}

void UAnimDataController::NotifyBracketOpen()
{
	ValidateModel();
	Model->Notify(EAnimDataModelNotifyType::BracketOpened);
}

void UAnimDataController::NotifyBracketClosed()
{
	ValidateModel();
	Model->Notify(EAnimDataModelNotifyType::BracketClosed);
}

const bool UAnimDataController::IsSupportedCurveType(ERawCurveTrackTypes CurveType) const
{
	const TArray<ERawCurveTrackTypes> SupportedTypes = { ERawCurveTrackTypes::RCT_Float, ERawCurveTrackTypes::RCT_Transform };
	return SupportedTypes.Contains(CurveType);
}

void UAnimDataController::ValidateModel() const
{
	checkf(Model != nullptr, TEXT("Invalid Model"));
}

void UAnimDataController::SetPlayLength_Internal(float NewLength, float T0, float T1, bool bShouldTransact)
{
	FSequenceLengthChangedPayload Payload;
	Payload.T0 = T0;
	Payload.T1 = T1;
	Payload.PreviousLength = Model->PlayLength;

	CONDITIONAL_ACTION(UE::Anim::FSetSequenceLengthAction, Model);

	Model->PlayLength = NewLength;

	Model->NumberOfFrames = Model->FrameRate.AsFrameTime(Model->PlayLength).RoundToFrame().Value;
	Model->NumberOfKeys = Model->NumberOfFrames + 1;
	
	Model->Notify<FSequenceLengthChangedPayload>(EAnimDataModelNotifyType::SequenceLengthChanged, Payload);
}

void UAnimDataController::ReportWarning(const FText& InMessage) const
{
	FString Message = InMessage.ToString();
	if (Model != nullptr)
	{
		if (UPackage* Package = Cast<UPackage>(Model->GetOutermost()))
		{
			Message = FString::Printf(TEXT("%s : %s"), *Package->GetPathName(), *Message);
		}
	}

	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Warning, *Message, *FString());
}

void UAnimDataController::ReportError(const FText& InMessage) const
{
	FString Message = InMessage.ToString();
	if (Model != nullptr)
	{
		if (UPackage* Package = Cast<UPackage>(Model->GetOutermost()))
		{
			Message = FString::Printf(TEXT("%s : %s"), *Package->GetPathName(), *Message);
		}
	}

	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Error, *Message, *FString());
}

FString UAnimDataController::GetCurveTypeValueName(ERawCurveTrackTypes InType) const
{
	FString ValueString;

	const UEnum* Enum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ERawCurveTrackTypes"));
	if (Enum)
	{
		ValueString = Enum->GetNameStringByValue((int64)InType);
	}

	return ValueString;
}

bool UAnimDataController::CheckOuterClass(UClass* InClass) const
{
	ValidateModel();
	
	const UObject* ModelOuter = Model->GetOuter();
	if (ModelOuter)
	{
		const UClass* OuterClass = ModelOuter->GetClass();
		if (OuterClass)
		{
			if (OuterClass == InClass || OuterClass->IsChildOf(InClass))
			{
				return true;
			}
			else
			{
				ReportErrorf(LOCTEXT("NoValidOuterClassError", "Incorrect outer object class found for Animation Data Model {0}, expected {1} actual {2}"), FText::FromString(Model->GetName()), FText::FromString(InClass->GetName()), FText::FromString(OuterClass->GetName()));
			}
		}
	}
	else
	{
		ReportErrorf(LOCTEXT("NoValidOuterObjectFoundError", "No valid outer object found for Animation Data Model {0}"), FText::FromString(Model->GetName()));
	}

	return false;
}

int32 UAnimDataController::AddBoneTrack(FName BoneName, bool bShouldTransact /*= true*/)
{
	if (!CheckOuterClass(UAnimSequence::StaticClass()))
	{
		return INDEX_NONE;
	}

	CONDITIONAL_TRANSACTION(LOCTEXT("AddBoneTrack", "Adding Animation Data Track"));
	return InsertBoneTrack(BoneName, INDEX_NONE, bShouldTransact);
}

int32 UAnimDataController::InsertBoneTrack(FName BoneName, int32 DesiredIndex, bool bShouldTransact /*= true*/)
{
	if (!CheckOuterClass(UAnimSequence::StaticClass()))
	{
		return INDEX_NONE;
	}
	
	const int32 TrackIndex = Model->GetBoneTrackIndexByName(BoneName);

	if (TrackIndex == INDEX_NONE)
	{
		if (Model->GetNumBoneTracks() >= MAX_ANIMATION_TRACKS)
		{
			ReportWarningf(LOCTEXT("MaxNumberOfTracksReachedWarning", "Cannot add track with name {0}. An animation sequence cannot contain more than 65535 tracks"), FText::FromName(BoneName));
		}
		else
		{
			CONDITIONAL_TRANSACTION(LOCTEXT("InsertBoneTrack", "Inserting Animation Data Track"));

			// Determine correct index to do insertion at
			const int32 InsertIndex = Model->BoneAnimationTracks.IsValidIndex(DesiredIndex) ? DesiredIndex : Model->BoneAnimationTracks.Num();

			FBoneAnimationTrack& NewTrack = Model->BoneAnimationTracks.InsertDefaulted_GetRef(InsertIndex);
			NewTrack.Name = BoneName;

			if (const UAnimSequence* AnimationSequence = Model->GetAnimationSequence())
			{
				if (const USkeleton* Skeleton = AnimationSequence->GetSkeleton())
				{
					const int32 BoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

					if (BoneIndex == INDEX_NONE)
					{
						ReportWarningf(LOCTEXT("UnableToFindBoneIndexWarning", "Unable to retrieve bone index for track: {0}"), FText::FromName(BoneName));
					}

					NewTrack.BoneTreeIndex = BoneIndex;
				}
				else
				{
					ReportError(LOCTEXT("UnableToGetOuterSkeletonError", "Unable to retrieve Skeleton for outer Animation Sequence"));
				}
			}
			else
			{
				ReportError(LOCTEXT("UnableToGetOuterAnimSequenceError", "Unable to retrieve outer Animation Sequence"));
			}

			FAnimationTrackAddedPayload Payload;
			Payload.Name = BoneName;
			Payload.TrackIndex = InsertIndex;

			Model->Notify<FAnimationTrackAddedPayload>(EAnimDataModelNotifyType::TrackAdded, Payload);
			CONDITIONAL_ACTION(UE::Anim::FRemoveTrackAction, NewTrack, InsertIndex);

			return InsertIndex;
		}
	}
	else
	{
		ReportWarningf(LOCTEXT("TrackNameAlreadyExistsWarning", "Track with name {0} already exists"), FText::FromName(BoneName));
	}
	
	return TrackIndex;
}

bool UAnimDataController::RemoveBoneTrack(FName BoneName, bool bShouldTransact /*= true*/)
{
	if (!CheckOuterClass(UAnimSequence::StaticClass()))
	{
		return false;
	}

	const FBoneAnimationTrack* ExistingTrackPtr = Model->FindBoneTrackByName(BoneName);

	if (ExistingTrackPtr != nullptr)
	{
		CONDITIONAL_TRANSACTION(LOCTEXT("RemoveBoneTrack", "Removing Animation Data Track"));
		const int32 TrackIndex = Model->BoneAnimationTracks.IndexOfByPredicate([ExistingTrackPtr](const FBoneAnimationTrack& Track)
		{
			return Track.Name == ExistingTrackPtr->Name;
		});

		ensure(TrackIndex != INDEX_NONE);

		CONDITIONAL_ACTION(UE::Anim::FAddTrackAction, *ExistingTrackPtr, TrackIndex);
		Model->BoneAnimationTracks.RemoveAt(TrackIndex);

		FAnimationTrackRemovedPayload Payload;
		Payload.Name = BoneName;

		Model->Notify(EAnimDataModelNotifyType::TrackRemoved, Payload);

		return true;
	}
	else
	{
		ReportWarningf(LOCTEXT("UnableToFindTrackWarning", "Could not find track with name {0}"), FText::FromName(BoneName));
	}

	return false;
}

void UAnimDataController::RemoveAllBoneTracks(bool bShouldTransact /*= true*/)
{
	if (!CheckOuterClass(UAnimSequence::StaticClass()))
	{
		return;
	}
	
	TArray<FName> TrackNames;
	Model->GetBoneTrackNames(TrackNames);

	if (TrackNames.Num())
	{
		CONDITIONAL_BRACKET(LOCTEXT("RemoveAllBoneTracks", "Removing all Animation Data Tracks"));
		for (const FName& TrackName : TrackNames)
		{
			RemoveBoneTrack(TrackName, bShouldTransact);
		}
	}	
}

bool UAnimDataController::SetBoneTrackKeys(FName BoneName, const TArray<FVector>& PositionalKeys, const TArray<FQuat>& RotationalKeys, const TArray<FVector>& ScalingKeys, bool bShouldTransact /*= true*/)
{
	if (!CheckOuterClass(UAnimSequence::StaticClass()))
	{
		return false;
	}

	CONDITIONAL_TRANSACTION(LOCTEXT("SetTrackKeysTransaction", "Setting Animation Data Track keys"));

	// Validate key format
	const int32 MaxNumKeys = FMath::Max(FMath::Max(PositionalKeys.Num(), RotationalKeys.Num()), ScalingKeys.Num());

	if (MaxNumKeys > 0)
	{
		const bool bValidPosKeys = PositionalKeys.Num() == MaxNumKeys;
		const bool bValidRotKeys = RotationalKeys.Num() == MaxNumKeys;
		const bool bValidScaleKeys = ScalingKeys.Num() == MaxNumKeys;

		if (bValidPosKeys && bValidRotKeys && bValidScaleKeys)
		{
			if (FBoneAnimationTrack* TrackPtr = Model->FindMutableBoneTrackByName(BoneName))
			{
				CONDITIONAL_ACTION(UE::Anim::FSetTrackKeysAction, *TrackPtr);

				TrackPtr->InternalTrackData.PosKeys = PositionalKeys;
				TrackPtr->InternalTrackData.RotKeys = RotationalKeys;
				TrackPtr->InternalTrackData.ScaleKeys = ScalingKeys;


				FAnimationTrackChangedPayload Payload;
				Payload.Name = BoneName;

				Model->Notify(EAnimDataModelNotifyType::TrackChanged, Payload);

				return true;
			}
			else
			{
				ReportWarningf(LOCTEXT("InvalidTrackNameWarning", "Track with name {0} does not exist"), FText::FromName(BoneName));
			}
		}
		else
		{
			ReportErrorf(LOCTEXT("InvalidTrackKeyDataError", "Invalid track key data, expected uniform data: number of positional keys {0}, number of rotational keys {1}, number of scaling keys {2}"), FText::AsNumber(PositionalKeys.Num()), FText::AsNumber(RotationalKeys.Num()), FText::AsNumber(ScalingKeys.Num()));
		}
	}
	else
	{
		ReportErrorf(LOCTEXT("MissingTrackKeyDataError", "Missing track key data, expected uniform data: number of positional keys {0}, number of rotational keys {1}, number of scaling keys {2}"), FText::AsNumber(PositionalKeys.Num()), FText::AsNumber(RotationalKeys.Num()), FText::AsNumber(ScalingKeys.Num()));
	}

	return false;
}

void UAnimDataController::ResizeCurves(float NewLength, bool bInserted, float T0, float T1, bool bShouldTransact /*= true*/)
{
	CONDITIONAL_BRACKET(LOCTEXT("ResizeCurves", "Resizing all Curves"));

	for (FFloatCurve& Curve : Model->CurveData.FloatCurves)
	{
		FFloatCurve ResizedCurve = Curve;
		ResizedCurve.Resize(NewLength, bInserted, T0, T1);
		SetCurveKeys(FAnimationCurveIdentifier(Curve.Name, ERawCurveTrackTypes::RCT_Float), ResizedCurve.FloatCurve.GetConstRefOfKeys(), bShouldTransact);
	}

	for (FTransformCurve& Curve : Model->CurveData.TransformCurves)
	{
		Curve.Resize(NewLength, bInserted, T0, T1);
	}
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE // "AnimDataController"

