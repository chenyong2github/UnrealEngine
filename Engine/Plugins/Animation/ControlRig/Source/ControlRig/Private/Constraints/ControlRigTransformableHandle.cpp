// Copyright Epic Games, Inc. All Rights Reserved.


#include "Constraints/ControlRigTransformableHandle.h"

#include "ControlRig.h"
#include "IControlRigObjectBinding.h"
#include "Rigs/RigHierarchyElements.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sections/MovieScene3DTransformSection.h"

/**
 * UTransformableControlHandle
 */

UTransformableControlHandle::~UTransformableControlHandle()
{
	UnregisterDelegates();
}

void UTransformableControlHandle::PostLoad()
{
	Super::PostLoad();
	RegisterDelegates();
}

bool UTransformableControlHandle::IsValid() const
{
	if (!ControlRig.IsValid() || ControlName == NAME_None)
	{
		return false;
	}

	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMesh();
	if (!SkeletalMeshComponent)
	{
		return false;
	}

	const FRigControlElement* ControlElement = ControlRig->FindControl(ControlName);
	if (!ControlElement)
	{
		return false;
	}
	
	return true;
}

// NOTE should we cache the skeletal mesh and the CtrlIndex to avoid looking for if every time
// probably not for handling runtime changes
void UTransformableControlHandle::SetGlobalTransform(const FTransform& InGlobal) const
{
	const FRigControlElement* ControlElement = GetControlElement();
	if (!ControlElement)
	{
		return;
	}

	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMesh();
	if (!SkeletalMeshComponent)
	{
		return;
	}
	
	URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
	const FRigElementKey& ControlKey = ControlElement->GetKey();
	const int32 CtrlIndex = Hierarchy->GetIndex(ControlKey);
	
	const FTransform& ComponentTransform = SkeletalMeshComponent->GetComponentTransform();
	Hierarchy->SetGlobalTransform(CtrlIndex, InGlobal.GetRelativeTransform(ComponentTransform));
}

void UTransformableControlHandle::SetLocalTransform(const FTransform& InLocal) const
{
	const FRigControlElement* ControlElement = GetControlElement();
	if (!ControlElement)
	{
		return;
	}
	
	URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
	const FRigElementKey& ControlKey = ControlElement->GetKey();
	const int32 CtrlIndex = Hierarchy->GetIndex(ControlKey);
	
	Hierarchy->SetLocalTransform(CtrlIndex, InLocal);
}

// NOTE should we cache the skeletal mesh and the CtrlIndex to avoid looking for if every time
// probably not for handling runtime changes
FTransform UTransformableControlHandle::GetGlobalTransform() const
{
	const FRigControlElement* ControlElement = GetControlElement();
	if (!ControlElement)
	{
		return FTransform::Identity;
	}
	
	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMesh();
	if (!SkeletalMeshComponent)
	{
		return FTransform::Identity;
	}

	const FRigElementKey& ControlKey = ControlElement->GetKey();
	const URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
	const int32 CtrlIndex = Hierarchy->GetIndex(ControlKey);

	const FTransform& ComponentTransform = SkeletalMeshComponent->GetComponentTransform();
	return Hierarchy->GetGlobalTransform(CtrlIndex) * ComponentTransform;
}

FTransform UTransformableControlHandle::GetLocalTransform() const
{
	const FRigControlElement* ControlElement = GetControlElement();
	if (!ControlElement)
	{
		return FTransform::Identity;
	}
	
	const FRigElementKey& ControlKey = ControlElement->GetKey();
	const URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
	const int32 CtrlIndex = Hierarchy->GetIndex(ControlKey);

	return Hierarchy->GetLocalTransform(CtrlIndex);
}

UObject* UTransformableControlHandle::GetPrerequisiteObject() const
{
	return GetSkeletalMesh(); 
}

FTickFunction* UTransformableControlHandle::GetTickFunction() const
{
	USkeletalMeshComponent* SkelMeshComponent = GetSkeletalMesh();
	return SkelMeshComponent ? &SkelMeshComponent->PrimaryComponentTick : nullptr;
}

uint32 UTransformableControlHandle::GetHash() const
{
	if (ControlRig.IsValid() && ControlName != NAME_None)
	{
		return HashCombine(GetTypeHash(ControlRig.Get()), GetTypeHash(ControlName));
	}
	return 0;
}

TWeakObjectPtr<UObject> UTransformableControlHandle::GetTarget() const
{
	return GetSkeletalMesh();
}

USkeletalMeshComponent* UTransformableControlHandle::GetSkeletalMesh() const
{
	const TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig.IsValid() ? ControlRig->GetObjectBinding() : nullptr;
	return ObjectBinding ? Cast<USkeletalMeshComponent>(ObjectBinding->GetBoundObject()) : nullptr;
}

FRigControlElement* UTransformableControlHandle::GetControlElement() const
{
	if (!ControlRig.IsValid() || ControlName == NAME_None)
	{
		return nullptr;
	}

	return ControlRig->FindControl(ControlName);
}

void UTransformableControlHandle::UnregisterDelegates() const
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);
#endif
	
	if (ControlRig.IsValid())
	{
		if (URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
		{
			Hierarchy->OnModified().RemoveAll(this);
		}
		ControlRig->ControlModified().RemoveAll(this);
	}
}

void UTransformableControlHandle::RegisterDelegates()
{
	UnregisterDelegates();

#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this, &UTransformableControlHandle::OnObjectsReplaced);
#endif

	if (ControlRig.IsValid())
	{
		if (URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
		{
			Hierarchy->OnModified().AddUObject(this, &UTransformableControlHandle::OnHierarchyModified);
		}
		
		ControlRig->ControlModified().AddUObject(this, &UTransformableControlHandle::OnControlModified);
	}
}

void UTransformableControlHandle::OnHierarchyModified(
	ERigHierarchyNotification InNotif,
	URigHierarchy* InHierarchy,
	const FRigBaseElement* InElement)
{
	if (!ControlRig.IsValid())
	{
	 	return;
	}

	const URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
	if (!Hierarchy || InHierarchy != Hierarchy)
	{
		return;
	}

	switch (InNotif)
	{
		case ERigHierarchyNotification::ElementRemoved:
		{
			// FIXME this leaves the constraint invalid as the element won't exist anymore
			// find a way to remove this from the constraints list 
			break;
		}
		case ERigHierarchyNotification::ElementRenamed:
		{
			const FName OldName = Hierarchy->GetPreviousName(InElement->GetKey());
			if (OldName == ControlName)
			{
				ControlName = InElement->GetName();
			}
			break;
		}
		default:
			break;
	}
}

void UTransformableControlHandle::OnControlModified(
	UControlRig* InControlRig,
	FRigControlElement* InControl,
	const FRigControlModifiedContext& InContext)
{
	if (!InControlRig || !InControl)
	{
		return;
	}

	if (!ControlRig.IsValid() || ControlName == NAME_None)
	{
		return;
	}

	if (ControlRig == InControlRig && InControl->GetName() == ControlName)
	{
		if(OnHandleModified.IsBound())
		{
			OnHandleModified.Broadcast(this, InContext.bConstraintUpdate);
		}
	}
}

static TPair<const FChannelMapInfo*, int32> GetInfoAndNumFloatChannels(
	const UControlRig* InControlRig,
	const FName& InControlName,
	const UMovieSceneControlRigParameterSection* InSection)
{
	const FRigControlElement* ControlElement = InControlRig ? InControlRig->FindControl(InControlName) : nullptr;
	auto GetNumFloatChannels = [](const ERigControlType& InControlType)
	{
		switch (InControlType)
		{
		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
			return 3;
		case ERigControlType::TransformNoScale:
			return 6;
		case ERigControlType::Transform:
		case ERigControlType::EulerTransform:
			return 9;
		default:
			break;
		}
		return 0;
	};

	const int32 NumFloatChannels = ControlElement ? GetNumFloatChannels(ControlElement->Settings.ControlType) : 0;
	const FChannelMapInfo* ChannelInfo = InSection ? InSection->ControlChannelMap.Find(InControlName) : nullptr;

	return { ChannelInfo, NumFloatChannels };
}
TArrayView<FMovieSceneFloatChannel*>  UTransformableControlHandle::GetFloatChannels(const UMovieSceneSection* InSection) const
{
	// no floats for transform sections
	static const TArrayView<FMovieSceneFloatChannel*> EmptyChannelsView;

	const FChannelMapInfo* ChannelInfo = nullptr;
	int32 NumChannels = 0;
	const UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(InSection);
	if (CRSection == nullptr)
	{
		return EmptyChannelsView;
	}

	Tie(ChannelInfo, NumChannels) = GetInfoAndNumFloatChannels(ControlRig.Get(),ControlName, CRSection);

	if (ChannelInfo == nullptr || NumChannels == 0)
	{
		return EmptyChannelsView;
	}

	// return a sub view that just represents the control's channels
	const TArrayView<FMovieSceneFloatChannel*> FloatChannels = InSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	const int32 ChannelStartIndex = ChannelInfo->ChannelIndex;
	return FloatChannels.Slice(ChannelStartIndex, NumChannels);
}

TArrayView<FMovieSceneDoubleChannel*>  UTransformableControlHandle::GetDoubleChannels(const UMovieSceneSection* InSection) const
{
	static const TArrayView<FMovieSceneDoubleChannel*> EmptyChannelsView;
	return EmptyChannelsView;
}

bool UTransformableControlHandle::AddTransformKeys(const TArray<FFrameNumber>& InFrames,
	const TArray<FTransform>& InTransforms,
	const EMovieSceneTransformChannel& InChannels,
	const FFrameRate& InTickResolution,
	UMovieSceneSection*,
	const bool bLocal) const
{
	if (!ControlRig.IsValid() || ControlName == NAME_None || InFrames.IsEmpty() || InFrames.Num() != InTransforms.Num())
	{
		return false;
	}
	auto KeyframeFunc = [this, bLocal](const FTransform& InTransform, const FRigControlModifiedContext& InKeyframeContext)
	{
		UControlRig* InControlRig = ControlRig.Get();
		static constexpr bool bNotify = true;
		static constexpr bool bUndo = false;
		static constexpr bool bFixEuler = true;

		if (bLocal)
		{
			return InControlRig->SetControlLocalTransform(ControlName, InTransform, bNotify, InKeyframeContext, bUndo, bFixEuler);
		}
		InControlRig->SetControlGlobalTransform(ControlName, InTransform, bNotify, InKeyframeContext, bUndo, bFixEuler);
	};

	FRigControlModifiedContext KeyframeContext;
	KeyframeContext.SetKey = EControlRigSetKey::Always;
	KeyframeContext.KeyMask = static_cast<uint32>(InChannels);

	for (int32 Index = 0; Index < InFrames.Num(); ++Index)
	{
		const FFrameNumber& Frame = InFrames[Index];
		KeyframeContext.LocalTime = InTickResolution.AsSeconds(FFrameTime(Frame));

		KeyframeFunc(InTransforms[Index], KeyframeContext);
	}

	return true;
}




#if WITH_EDITOR

FString UTransformableControlHandle::GetLabel() const
{
	return ControlName.ToString();
}

FString UTransformableControlHandle::GetFullLabel() const
{
	const USkeletalMeshComponent* SkeletalMesh = GetSkeletalMesh();
	if (!SkeletalMesh)
	{
		static const FString DummyLabel;
		return DummyLabel;
	}
	
	const AActor* Actor = SkeletalMesh->GetOwner();
	const FString ControlRigLabel = Actor ? Actor->GetActorLabel() : SkeletalMesh->GetName();
	return FString::Printf(TEXT("%s/%s"), *ControlRigLabel, *ControlName.ToString() );
}

void UTransformableControlHandle::OnObjectsReplaced(const TMap<UObject*, UObject*>& InOldToNewInstances)
{
	if (UObject* NewObject = InOldToNewInstances.FindRef(ControlRig.Get()))
	{
		if (UControlRig* NewControlRig = Cast<UControlRig>(NewObject))
		{
			if (URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
			{
				Hierarchy->OnModified().RemoveAll(this);
			}
			
			ControlRig = NewControlRig;
			
			if (URigHierarchy* Hierarchy = ControlRig->GetHierarchy())
			{
				Hierarchy->OnModified().AddUObject(this, &UTransformableControlHandle::OnHierarchyModified);
			}
		}
	}
}

#endif