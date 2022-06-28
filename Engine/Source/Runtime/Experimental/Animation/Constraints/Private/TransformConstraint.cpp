// Copyright Epic Games, Inc. All Rights Reserved.

#include "TransformConstraint.h"
#include "Animation/Constraints/Public/TransformableHandle.h"
#include "ConstraintsManager.h"
#include "TransformableRegistry.h"
#include "GameFramework/Actor.h"

/** 
 * UTickableTransformConstraint
 **/

/** @todo remove to use something else. */
int64 UTickableTransformConstraint::GetType() const
{
	return static_cast<int64>(Type);
}

#if WITH_EDITOR
FName UTickableTransformConstraint::GetLabel() const
{
	return ParentTRSHandle->IsValid() ? ParentTRSHandle->GetName() : NAME_None;
}

void UTickableTransformConstraint::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UTickableTransformConstraint, bMaintainOffset))
	{
		Evaluate();
		return;
	}

	if (const FProperty* MemberProperty = PropertyChangedEvent.MemberProperty)
	{
		static const FString OffsetStr("Offset");
		if (MemberProperty->GetFName().ToString().Contains(OffsetStr) )
		{
			Evaluate();
		}
	}
}

#endif

void UTickableTransformConstraint::Setup()
{
	if (!ParentTRSHandle->IsValid() || !ChildTRSHandle->IsValid())
	{
		// handle error
		return;
	}
	
	ComputeOffset();
	SetupDependencies();
}

void UTickableTransformConstraint::SetupDependencies()
{
	FTickFunction* ParentTickFunction = ParentTRSHandle->IsValid() ? ParentTRSHandle->GetTickFunction() : nullptr;
	if (ParentTickFunction)
	{
		// manage dependencies
		// force ConstraintTickFunction to tick after InParent does.
		// Note that this might not register anything if the parent can't tick (static meshes for instance)
		ConstraintTick.AddPrerequisite(ParentTRSHandle->GetPrerequisiteObject(), *ParentTickFunction);
	}
	
	// TODO also check for cycle dependencies here
	FTickFunction* ChildTickFunction = ChildTRSHandle ? ChildTRSHandle->GetTickFunction() : nullptr;
	if (ChildTickFunction && ChildTickFunction != ParentTickFunction)
	{
		// force InParent to tick after ConstraintTickFunction does.
		// Note that this might not register anything if the child can't tick (static meshes for instance)
		ChildTickFunction->AddPrerequisite(this, ConstraintTick);
	}
}

void UTickableTransformConstraint::PostLoad()
{
	Super::PostLoad();
	ConstraintTick.RegisterFunction(GetFunction() );
}

void UTickableTransformConstraint::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	ConstraintTick.RegisterFunction(GetFunction());
}

uint32 UTickableTransformConstraint::GetTargetHash() const
{
	return ChildTRSHandle->IsValid() ? ChildTRSHandle->GetHash() : 0;
}

bool UTickableTransformConstraint::ReferencesObject(TWeakObjectPtr<UObject> InObject) const
{
	const TWeakObjectPtr<UObject> ChildTarget = ChildTRSHandle->IsValid() ? ChildTRSHandle->GetTarget() : nullptr;
	if (ChildTarget == InObject)
	{
		return true;	
	}

	const TWeakObjectPtr<UObject> ParentTarget = ParentTRSHandle->IsValid() ? ParentTRSHandle->GetTarget() : nullptr;
	if (ParentTarget == InObject)
	{
		return true;	
	}
	
	return false;
}

void UTickableTransformConstraint::SetChildGlobalTransform(const FTransform& InGlobal) const
{
	if(ChildTRSHandle->IsValid())
	{
		ChildTRSHandle->SetGlobalTransform(InGlobal);
	}
}

void UTickableTransformConstraint::SetChildLocalTransform(const FTransform& InLocal) const
{
	if(ChildTRSHandle->IsValid())
	{
		ChildTRSHandle->SetLocalTransform(InLocal);
	}
}

FTransform UTickableTransformConstraint::GetChildGlobalTransform() const
{
	return ChildTRSHandle->IsValid() ? ChildTRSHandle->GetGlobalTransform() : FTransform::Identity;
}

FTransform UTickableTransformConstraint::GetChildLocalTransform() const
{
	return ChildTRSHandle->IsValid() ? ChildTRSHandle->GetLocalTransform() : FTransform::Identity;
}

FTransform UTickableTransformConstraint::GetParentGlobalTransform() const
{
	return ParentTRSHandle->IsValid() ? ParentTRSHandle->GetGlobalTransform() : FTransform::Identity;
}

FTransform UTickableTransformConstraint::GetParentLocalTransform() const
{
	return ParentTRSHandle->IsValid() ? ParentTRSHandle->GetLocalTransform() : FTransform::Identity;
}

/** 
 * UTickableTranslationConstraint
 **/

UTickableTranslationConstraint::UTickableTranslationConstraint()
{
	Type = ETransformConstraintType::Translation;
}

void UTickableTranslationConstraint::ComputeOffset()
{
	UClass* StaticClass = UTickableTranslationConstraint::StaticClass();
	
	const FTransform InitParentTransform = GetParentGlobalTransform();
	const FTransform InitChildTransform = GetChildGlobalTransform();
	
	OffsetTranslation = FVector::ZeroVector;
	if (bMaintainOffset)
	{
		OffsetTranslation = InitChildTransform.GetLocation() - InitParentTransform.GetLocation();
	}	
}

FConstraintTickFunction::ConstraintFunction UTickableTranslationConstraint::GetFunction() const
{
	return [this]()
	{
		if (!Active)
		{
			return;
		}
		
		const float ClampedWeight = FMath::Clamp<float>(Weight, 0.f, 1.f);
		if (ClampedWeight < KINDA_SMALL_NUMBER)
		{
			return;
		}

		const FVector ParentTranslation = GetParentGlobalTransform().GetLocation();
		FTransform Transform = GetChildGlobalTransform();
		FVector NewTranslation = bMaintainOffset ? ParentTranslation + OffsetTranslation : ParentTranslation;
		if (ClampedWeight < 1.0f - KINDA_SMALL_NUMBER)
		{
			NewTranslation = FMath::Lerp<FVector>(Transform.GetLocation(), NewTranslation, ClampedWeight);
		}
		Transform.SetLocation(NewTranslation);
			
		SetChildGlobalTransform(Transform);
	};
}

/** 
 * UTickableRotationConstraint
 **/

UTickableRotationConstraint::UTickableRotationConstraint()
{
	Type = ETransformConstraintType::Rotation;
}

void UTickableRotationConstraint::ComputeOffset()
{
	const FTransform InitParentTransform = GetParentGlobalTransform();
	const FTransform InitChildTransform = GetChildGlobalTransform();
	
	OffsetRotation = FQuat::Identity;
	if (bMaintainOffset)
	{
		OffsetRotation = InitParentTransform.GetRotation().Inverse() * InitChildTransform.GetRotation();
		OffsetRotation.Normalize();
	}
}

FConstraintTickFunction::ConstraintFunction UTickableRotationConstraint::GetFunction() const
{
	return [this]()
	{
		if (!Active)
		{
			return;
		}
		
		const float ClampedWeight = FMath::Clamp<float>(Weight, 0.f, 1.f);
		if (ClampedWeight < KINDA_SMALL_NUMBER)
		{
			return;
		}
		
		const FQuat ParentRotation = GetParentGlobalTransform().GetRotation();
		FTransform Transform = GetChildGlobalTransform();

		FQuat NewRotation = bMaintainOffset ? ParentRotation * OffsetRotation : ParentRotation;
		if (ClampedWeight < 1.0f - KINDA_SMALL_NUMBER)
		{
			NewRotation = FQuat::Slerp(Transform.GetRotation(), NewRotation, ClampedWeight);
		}
		Transform.SetRotation(NewRotation);
		
		SetChildGlobalTransform(Transform);
	};
}

/** 
 * UTickableScaleConstraint
 **/

UTickableScaleConstraint::UTickableScaleConstraint()
{
	Type = ETransformConstraintType::Scale;
}

void UTickableScaleConstraint::ComputeOffset()
{
	const FTransform InitParentTransform = GetParentGlobalTransform();
	const FTransform InitChildTransform = GetChildGlobalTransform();
	
	OffsetScale = FVector::OneVector;
	if (bMaintainOffset)
	{
		const FVector InitParentScale = InitParentTransform.GetScale3D();
		OffsetScale = InitChildTransform.GetScale3D();
		OffsetScale[0] = FMath::Abs(InitParentScale[0]) > KINDA_SMALL_NUMBER ? OffsetScale[0] / InitParentScale[0] : 0.f;
		OffsetScale[1] = FMath::Abs(InitParentScale[1]) > KINDA_SMALL_NUMBER ? OffsetScale[1] / InitParentScale[1] : 0.f;
		OffsetScale[2] = FMath::Abs(InitParentScale[2]) > KINDA_SMALL_NUMBER ? OffsetScale[2] / InitParentScale[2] : 0.f;
	}
}

FConstraintTickFunction::ConstraintFunction UTickableScaleConstraint::GetFunction() const
{
	return [this]()
	{
		if (!Active)
		{
			return;
		}
		
		const float ClampedWeight = FMath::Clamp<float>(Weight, 0.f, 1.f);
		if (ClampedWeight < KINDA_SMALL_NUMBER)
		{
			return;
		}
		
		const FVector ParentScale = GetParentGlobalTransform().GetScale3D();
		FTransform Transform = GetChildGlobalTransform();
		FVector NewScale = bMaintainOffset ? ParentScale * OffsetScale : ParentScale;
		if (ClampedWeight < 1.0f - KINDA_SMALL_NUMBER)
		{
			NewScale = FMath::Lerp<FVector>(Transform.GetScale3D(), NewScale, ClampedWeight);
		}
		Transform.SetScale3D(NewScale);
			
		SetChildGlobalTransform(Transform);
	};
}

/** 
 * UTickableParentConstraint
 **/

UTickableParentConstraint::UTickableParentConstraint()
{
	Type = ETransformConstraintType::Parent;
}

void UTickableParentConstraint::ComputeOffset()
{
	const FTransform InitParentTransform = GetParentGlobalTransform();
	const FTransform InitChildTransform = GetChildGlobalTransform();
	
	OffsetTransform = FTransform::Identity;
	if (bMaintainOffset)
	{
		OffsetTransform = InitChildTransform.GetRelativeTransform(InitParentTransform);
	}
}

uint32 UTickableParentConstraint::CalculateDependenciesHash() const
{
	uint32 Hash = 0;
	
	Hash = HashCombine(Hash, GetTypeHash(Active));
	Hash = HashCombine(Hash, GetTypeHash(Weight));

	const FTransform ParentGlobal = GetParentGlobalTransform();
	Hash = HashCombine(Hash, GetTypeHash(ParentGlobal.GetTranslation()));
	Hash = HashCombine(Hash, GetTypeHash(ParentGlobal.GetRotation().Euler() ));
	Hash = HashCombine(Hash, GetTypeHash(ParentGlobal.GetScale3D()));
	
	return Hash;
}

FConstraintTickFunction::ConstraintFunction UTickableParentConstraint::GetFunction() const
{
	return [this]()
	{
		if (!Active)
		{
			return;
		}
		
		const float ClampedWeight = FMath::Clamp<float>(Weight, 0.f, 1.f);
		if (ClampedWeight < KINDA_SMALL_NUMBER)
		{
			return;
		}

		auto LerpTransform = [ClampedWeight](const FTransform& InTransform, FTransform& InTransformToBeSet)
		{
			if (ClampedWeight < 1.0f - KINDA_SMALL_NUMBER)
			{
				InTransformToBeSet.SetLocation(
					FMath::Lerp<FVector>(InTransform.GetLocation(), InTransformToBeSet.GetLocation(), ClampedWeight));
				InTransformToBeSet.SetRotation(
					FQuat::Slerp(InTransform.GetRotation(), InTransformToBeSet.GetRotation(), ClampedWeight));
				InTransformToBeSet.SetScale3D(
					FMath::Lerp<FVector>(InTransform.GetScale3D(), InTransformToBeSet.GetScale3D(), ClampedWeight));
			}
		};

		const FTransform ParentTransform = GetParentGlobalTransform();
		
		// if bDynamicOffset is on, means we assume that the incoming local child transform is coming from the child
		// TRS animation (for instance) which is local to the constraint's parent
		if (bDynamicOffset)
		{
			const uint32 DependenciesHash = CalculateDependenciesHash();

			const FTransform ChildGlobalTransform = GetChildGlobalTransform();
			const bool bIsGlobalDirty = !ChildGlobalTransform.Equals(Cache.LastGlobalSet);
			const bool bIsInputHashDirty = DependenciesHash != Cache.CachedInputHash;

			// we avoid computation if nothing has changed
			if (!bIsGlobalDirty && !bIsInputHashDirty)
			{
				return;
			}

			FTransform LocalTransform = Cache.LastLocalSet;
			if (bIsGlobalDirty)
			{
				LocalTransform = GetChildLocalTransform();
			}
			
			FTransform TargetTransform = LocalTransform * ParentTransform;
			//apply weight if needed
			LerpTransform(ChildGlobalTransform, TargetTransform);
			
			SetChildGlobalTransform(TargetTransform);

			// update cache
			Cache.CachedInputHash = DependenciesHash;
			Cache.LastGlobalSet = TargetTransform;
			Cache.LastLocalSet = LocalTransform;
			
			return;
		}

		// static behavior
		FTransform TargetTransform = bMaintainOffset ? OffsetTransform * ParentTransform : ParentTransform;
		//apply weight if needed
		LerpTransform(GetChildGlobalTransform(), TargetTransform);
		SetChildGlobalTransform(TargetTransform);
	};
}

/** 
 * UTickableLookAtConstraint
 **/

UTickableLookAtConstraint::UTickableLookAtConstraint()
{
	Type = ETransformConstraintType::LookAt;
}

void UTickableLookAtConstraint::ComputeOffset()
{
	// TODO compute offset
}

FConstraintTickFunction::ConstraintFunction UTickableLookAtConstraint::GetFunction() const
{
	// @todo handle weight here
	return [this]()
	{
		if (!Active)
		{
			return;
		}
		
		const FTransform ParentTransform = GetParentGlobalTransform();
		const FTransform ChildTransform = GetChildGlobalTransform();
		
		const FVector LookAtDir = ParentTransform.GetLocation() - ChildTransform.GetLocation();
		const FRotator LookAtRotation = LookAtDir.Rotation();

		FTransform Transform = GetChildGlobalTransform();
		Transform.SetRotation(LookAtRotation.Quaternion());
		
		SetChildGlobalTransform(Transform);
	};
}

/** 
 * FTransformConstraintUtils
 **/

namespace
{

UTransformableHandle* GetHandle(AActor* InActor, UObject* Outer)
{
	// look for customized transform handle
	const FTransformableRegistry& Registry = FTransformableRegistry::Get();
	if (const FTransformableRegistry::CreateHandleFuncT CreateFunction = Registry.GetCreateFunction(InActor->GetClass()))
	{
		return CreateFunction(InActor, Outer);
	}

	// need to make sure it's moveable
	if (InActor->GetRootComponent())
	{
		return FTransformConstraintUtils::CreateHandleForSceneComponent(InActor->GetRootComponent(), Outer);

	}
	return nullptr;
}
	
uint32 GetConstrainableHash(const AActor* InActor)
{
	// look for customized hash function
	const FTransformableRegistry& Registry = FTransformableRegistry::Get();
	if (const FTransformableRegistry::GetHashFuncT HashFunction = Registry.GetHashFunction(InActor->GetClass()))
	{
		return HashFunction(InActor);
	}

	// scene component hash
	const uint32 ComponentHash = GetTypeHash(InActor->GetRootComponent());
	return ComponentHash;
}
	
}

UTransformableComponentHandle* FTransformConstraintUtils::CreateHandleForSceneComponent(USceneComponent* InSceneComponent, UObject* Outer)
{
	UTransformableComponentHandle* ComponentHandle = nullptr;
	if (InSceneComponent)
	{
		ComponentHandle = NewObject<UTransformableComponentHandle>(Outer);
		ComponentHandle->Component = InSceneComponent;
		InSceneComponent->SetMobility(EComponentMobility::Movable);
	}
	return ComponentHandle;
}

void FTransformConstraintUtils::GetParentConstraints(
	UWorld* InWorld,
	const AActor* InChild,
	TArray< TObjectPtr<UTickableConstraint> >& OutConstraints)
{
	if (!InWorld || !InChild)
	{
		return;
	}

	const uint32 ChildHash = GetConstrainableHash(InChild);
	if (ChildHash == 0)
	{
		return;
	}
	
	static constexpr bool bSorted = true;
	const FConstraintsManagerController& Controller = FConstraintsManagerController::Get(InWorld);
	OutConstraints.Append(Controller.GetParentConstraints(ChildHash, bSorted));
}

UTickableTransformConstraint* FTransformConstraintUtils::CreateFromType(
	UWorld* InWorld,
	const ETransformConstraintType InType)
{
	if (!InWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("FTransformConstraintUtils::CreateFromType sanity check failed."));
		return nullptr;
	}
	const UEnum* ETransformConstraintTypeEnum = StaticEnum<ETransformConstraintType>();
	if (!ETransformConstraintTypeEnum->IsValidEnumValue(static_cast<int64>(InType)))
	{
		UE_LOG(LogTemp, Error, TEXT("Constraint Type %d not recognized"), InType);
		return nullptr;
	}


	// unique name (we may want to use another approach here to manage uniqueness)
	const FString ConstraintTypeStr = ETransformConstraintTypeEnum->GetNameStringByValue((uint8)InType);
	const FName BaseName(*FString::Printf(TEXT("%sConstraint"), *ConstraintTypeStr));

	const FConstraintsManagerController& Controller = FConstraintsManagerController::Get(InWorld);
	UTickableTransformConstraint* Constraint = nullptr;

	switch (InType)
	{
	case ETransformConstraintType::Translation:
		Constraint = Controller.AllocateConstraintT<UTickableTranslationConstraint>(BaseName);
		break;
	case ETransformConstraintType::Rotation:
		Constraint = Controller.AllocateConstraintT<UTickableRotationConstraint>(BaseName);
		break;
	case ETransformConstraintType::Scale:
		Constraint = Controller.AllocateConstraintT<UTickableScaleConstraint>(BaseName);
		break;
	case ETransformConstraintType::Parent:
		Constraint = Controller.AllocateConstraintT<UTickableParentConstraint>(BaseName);
		break;
	case ETransformConstraintType::LookAt:
		Constraint = Controller.AllocateConstraintT<UTickableLookAtConstraint>(BaseName);
		break;
	default:
		ensure(false);
		break;
	}
	return Constraint;
}

UTickableTransformConstraint* FTransformConstraintUtils::CreateAndAddFromActors(
	UWorld* InWorld,
	AActor* InParent,
	AActor* InChild,
	const ETransformConstraintType InType,
	const bool bMaintainOffset)
{
	// SANITY CHECK
	if (!InWorld || !InParent || !InChild)
	{
		UE_LOG(LogTemp, Error, TEXT("FTransformConstraintUtils::CreateAndAddFromActors sanity check failed."));
		return nullptr;
	}
	
	UConstraintsManager* ConstraintsManager = UConstraintsManager::Get(InWorld);
	if (!ConstraintsManager)
	{
		UE_LOG(LogTemp, Error, TEXT("FTransformConstraintUtils::CreateAndAddFromActors constraint manager is null."));
		return nullptr;
	}

	UTransformableHandle* ParentHandle = GetHandle(InParent, ConstraintsManager);
	UTransformableHandle* ChildHandle = GetHandle(InChild, ConstraintsManager);
	
	UTickableTransformConstraint* Constraint = FTransformConstraintUtils::CreateFromType(InWorld, InType);


	if (Constraint && (ParentHandle->IsValid() && ChildHandle->IsValid()))
	{
		if (AddConstraint(InWorld, ParentHandle, ChildHandle, Constraint, bMaintainOffset) == false)
		{
			Constraint->MarkAsGarbage();
			Constraint = nullptr;
		}
	}
	return Constraint;
}

bool FTransformConstraintUtils::AddConstraint(
	UWorld* InWorld,
	UTransformableHandle* InParentHandle,
	UTransformableHandle* InChildHandle,
	UTickableTransformConstraint* Constraint,
	const bool bMaintainOffset)
{
	const bool bIsValidParent = InParentHandle && InParentHandle->IsValid();
	const bool bIsValidChild = InChildHandle && InChildHandle->IsValid();
	if (!bIsValidParent || !bIsValidChild)
	{
		UE_LOG(LogTemp, Error, TEXT("FTransformConstraintUtils::AddConst error adding constraint"));
		return false;
	}

	if (Constraint == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("FTransformConstraintUtils::AddConst error creating constraint"));
		return false;
	}


	auto SetupConstraint = [InParentHandle, InChildHandle, bMaintainOffset](UTickableTransformConstraint* InConstraint)
	{
		InConstraint->ParentTRSHandle = InParentHandle;
		InConstraint->ChildTRSHandle = InChildHandle;
		InConstraint->bMaintainOffset = bMaintainOffset;
		InConstraint->Setup();
	};
	
	const FConstraintsManagerController& Controller = FConstraintsManagerController::Get(InWorld);
	
	Controller.AddConstraint(Constraint);

	SetupConstraint(Constraint);

	return true;
}
