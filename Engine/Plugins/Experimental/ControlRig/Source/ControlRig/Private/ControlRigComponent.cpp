// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigComponent.h"
#include "Units/Execution/RigUnit_BeginExecution.h"

#include "SkeletalDebugRendering.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "AnimCustomInstanceHelper.h"

FControlRigAnimInstanceProxy* FControlRigComponentMappedElement::GetAnimProxyOnGameThread() const
{
	if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(SceneComponent))
	{
		UControlRigAnimInstance* AnimInstance = Cast<UControlRigAnimInstance>(SkeletalMeshComponent->GetAnimInstance());
		return AnimInstance->GetControlRigProxyOnGameThread();
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////

#if WITH_EDITOR
TMap<FString, TSharedPtr<SNotificationItem>> UControlRigComponent::EditorNotifications;
#endif

struct FSkeletalMeshToMap
{
	USkeletalMeshComponent* SkeletalMeshComponent;
	TArray<FControlRigComponentMappedBone> Bones;
	TArray<FControlRigComponentMappedCurve> Curves;
};

FCriticalSection gPendingSkeletalMeshesLock;
TMap<UControlRigComponent*, TArray<FSkeletalMeshToMap> > gPendingSkeletalMeshes;

UControlRigComponent::UControlRigComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	bTickInEditor = true;
	bAutoActivate = true;

	ControlRig = nullptr;
	bResetTransformBeforeTick = true;
	bResetInitialsBeforeSetup = true;
	bUpdateRigOnTick = true;
	bUpdateInEditor = true;
	bDrawBones = true;
	bShowDebugDrawing = false;
}

#if WITH_EDITOR
void UControlRigComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigComponent, ControlRigClass))
	{
		ControlRig = nullptr;
		SetupControlRigIfRequired();
	}
	else if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigComponent, MappedElements))
	{
		ValidateMappingData();
	}
}
#endif

void UControlRigComponent::OnRegister()
{
	Super::OnRegister();
	
	ControlRig = nullptr;

	{
		FScopeLock Lock(&gPendingSkeletalMeshesLock);
		gPendingSkeletalMeshes.FindOrAdd(this);
	}

	Initialize();

	if (AActor* Actor = GetOwner())
	{
		Actor->PrimaryActorTick.bStartWithTickEnabled = true;
		Actor->PrimaryActorTick.bCanEverTick = true;
		Actor->PrimaryActorTick.bTickEvenWhenPaused = true;
	}
}

void UControlRigComponent::OnUnregister()
{
	Super::OnUnregister();

	bool bBeginDestroyed = HasAnyFlags(RF_BeginDestroyed);
	if (!bBeginDestroyed)
	{
		if (AActor* Actor = GetOwner())
		{
			bBeginDestroyed = Actor->HasAnyFlags(RF_BeginDestroyed);
		}
	}

	if (!bBeginDestroyed)
	{
		for (TPair<USkeletalMeshComponent*, FCachedSkeletalMeshComponentSettings>& Pair : CachedSkeletalMeshComponentSettings)
		{
			if (Pair.Key)
			{
				if (Pair.Key->IsValidLowLevel() &&
					!Pair.Key->HasAnyFlags(RF_BeginDestroyed) &&
					!Pair.Key->IsPendingKill())
				{
					Pair.Value.Apply(Pair.Key);
				}
			}
		}
	}
	else
	{
		FScopeLock Lock(&gPendingSkeletalMeshesLock);
		gPendingSkeletalMeshes.Remove(this);
	}

	CachedSkeletalMeshComponentSettings.Reset();
}

void UControlRigComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if(!bUpdateRigOnTick)
	{
		return;
	}

	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		FScopeLock Lock(&gPendingSkeletalMeshesLock);
		TArray<FSkeletalMeshToMap>* PendingSkeletalMeshes = gPendingSkeletalMeshes.Find(this);

		if (PendingSkeletalMeshes != nullptr && PendingSkeletalMeshes->Num() > 0)
		{
			for (const FSkeletalMeshToMap& SkeletalMeshToMap : *PendingSkeletalMeshes)
			{
				AddMappedSkeletalMesh(
					SkeletalMeshToMap.SkeletalMeshComponent,
					SkeletalMeshToMap.Bones,
					SkeletalMeshToMap.Curves
				);
			}

			PendingSkeletalMeshes->Reset();
		}
	}

	Update(DeltaTime);
}

FPrimitiveSceneProxy* UControlRigComponent::CreateSceneProxy()
{
	return new FControlRigSceneProxy(this);
}

FBoxSphereBounds UControlRigComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox BBox(ForceInit);

	if (ControlRig)
	{
		// Get bounding box for the debug drawings if they are drawn 
		if (bShowDebugDrawing)
		{
			const FControlRigDrawInterface& DrawInterface = ControlRig->GetDrawInterface();
			for (int32 InstructionIndex = 0; InstructionIndex < DrawInterface.Num(); InstructionIndex++)
			{
				const FControlRigDrawInstruction& Instruction = DrawInterface[InstructionIndex];
				FTransform Transform = Instruction.Transform * GetComponentToWorld();
				for (const FVector& Position : Instruction.Positions)
				{
					BBox += Transform.TransformPosition(Position);
				}
			}
		}

		FTransform Transform = GetComponentToWorld();

		const FRigBoneHierarchy& BoneHierarchy = ControlRig->GetBoneHierarchy();
		for (const FRigBone& Bone : BoneHierarchy)
		{
			BBox += Transform.TransformPosition(Bone.GlobalTransform.GetLocation());
		}
	}

	if (BBox.IsValid)
	{
		// Points are in world space, so no need to transform.
		return FBoxSphereBounds(BBox);
	}
	else
	{
		const FVector BoxExtent(1.f);
		return FBoxSphereBounds(LocalToWorld.GetLocation(), BoxExtent, 1.f);
	}
}


UControlRig* UControlRigComponent::GetControlRig()
{
	return SetupControlRigIfRequired();
}

float UControlRigComponent::GetAbsoluteTime() const
{
	if(ControlRig)
	{
		return ControlRig->AbsoluteTime;
	}
	return 0.f;
}

void UControlRigComponent::OnPostInitialize_Implementation(UControlRigComponent* Component)
{
	ValidateMappingData();
	OnPostInitializeDelegate.Broadcast(Component);
}

void UControlRigComponent::OnPreSetup_Implementation(UControlRigComponent* Component)
{
	OnPreSetupDelegate.Broadcast(Component);
}

void UControlRigComponent::OnPostSetup_Implementation(UControlRigComponent* Component)
{
	OnPostSetupDelegate.Broadcast(Component);
}

void UControlRigComponent::OnPreUpdate_Implementation(UControlRigComponent* Component)
{
	TransferInputs();
	OnPreUpdateDelegate.Broadcast(Component);
}

void UControlRigComponent::OnPostUpdate_Implementation(UControlRigComponent* Component)
{
	TransferOutputs();
	OnPostUpdateDelegate.Broadcast(Component);
}

void UControlRigComponent::Initialize()
{
	if(UControlRig* CR = SetupControlRigIfRequired())
	{
		if (CR->IsInitializing())
		{
			ReportError(TEXT("Initialize is being called recursively."));
		}
		else
		{
			CR->DrawInterface.Reset();
			CR->GetHierarchy()->Initialize(true);
			CR->RequestInit();
		}
	}
}

void UControlRigComponent::Update(float DeltaTime)
{
	if(UControlRig* CR = SetupControlRigIfRequired())
	{
		if (CR->IsExecuting() || CR->IsInitializing())
		{
			ReportError(TEXT("Update is being called recursively."));
		}
		else
		{
			CR->SetDeltaTime(DeltaTime);
			CR->bResetInitialTransformsBeforeSetup = bResetInitialsBeforeSetup;

			// todo: set log
			// todo: set external data providers

			if (bResetTransformBeforeTick)
			{
				CR->GetBoneHierarchy().ResetTransforms();
			}

#if WITH_EDITOR
			if (bUpdateInEditor)
			{
				FEditorScriptExecutionGuard AllowScripts;
				OnPreUpdate(this);
			}
			else
#endif
			{
				OnPreUpdate(this);
			}

			CR->Evaluate_AnyThread();

			if (bShowDebugDrawing)
			{
				if (CR->DrawInterface.Instructions.Num() > 0)
				{
					MarkRenderStateDirty();
				}
			}
		}
	}
}

TArray<FName> UControlRigComponent::GetElementNames(ERigElementType ElementType)
{
	TArray<FName> Names;

	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		switch (ElementType)
		{
			case ERigElementType::Bone:
			{
				for (const FRigBone& Element : CR->GetBoneHierarchy())
				{
					Names.Add(Element.Name);
				}
				break;
			}
			case ERigElementType::Space:
			{
				for (const FRigSpace& Element : CR->GetSpaceHierarchy())
				{
					Names.Add(Element.Name);
				}
				break;
			}
			case ERigElementType::Control:
			{
				for (const FRigControl& Element : CR->GetControlHierarchy())
				{
					Names.Add(Element.Name);
				}
				break;
			}
			case ERigElementType::Curve:
			{
				for (const FRigCurve& Element : CR->GetCurveContainer())
				{
					Names.Add(Element.Name);
				}
				break;
			}
		}
	}

	return Names;
}

bool UControlRigComponent::DoesElementExist(FName Name, ERigElementType ElementType)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		return CR->GetHierarchy()->GetIndex(FRigElementKey(Name, ElementType)) != INDEX_NONE;
	}
	return false;
}

void UControlRigComponent::ClearMappedElements()
{
	if (!EnsureCalledOutsideOfBracket(TEXT("ClearMappedElements")))
	{
		return;
	}

	MappedElements.Reset();
	ValidateMappingData();
	Initialize();
}

void UControlRigComponent::SetMappedElements(TArray<FControlRigComponentMappedElement> NewMappedElements)
{
	if (!EnsureCalledOutsideOfBracket(TEXT("SetMappedElements")))
	{
		return;
	}

	MappedElements = NewMappedElements;
	ValidateMappingData();
	Initialize();
}

void UControlRigComponent::AddMappedElements(TArray<FControlRigComponentMappedElement> NewMappedElements)
{
	if (!EnsureCalledOutsideOfBracket(TEXT("AddMappedElements")))
	{
		return;
	}

	MappedElements.Append(NewMappedElements);
	ValidateMappingData();
	Initialize();
}

void UControlRigComponent::AddMappedComponents(TArray<FControlRigComponentMappedComponent> Components)
{
	if (!EnsureCalledOutsideOfBracket(TEXT("AddMappedComponents")))
	{
		return;
	}

	TArray<FControlRigComponentMappedElement> ElementsToMap;

	for (const FControlRigComponentMappedComponent& ComponentToMap : Components)
	{
		if (ComponentToMap.Component == nullptr ||
			ComponentToMap.ElementName.IsNone())
		{
			continue;
		}

		USceneComponent* Component = ComponentToMap.Component;

		FControlRigComponentMappedElement ElementToMap;
		ElementToMap.ComponentReference.OtherActor = Component->GetOwner() != GetOwner() ? Component->GetOwner() : nullptr;
		ElementToMap.ComponentReference.PathToComponent = Component->GetName();

		ElementToMap.ElementName = ComponentToMap.ElementName;
		ElementToMap.ElementType = ComponentToMap.ElementType;

		ElementsToMap.Add(ElementToMap);
	}

	AddMappedElements(ElementsToMap);
}

void UControlRigComponent::AddMappedSkeletalMesh(USkeletalMeshComponent* SkeletalMeshComponent, TArray<FControlRigComponentMappedBone> Bones, TArray<FControlRigComponentMappedCurve> Curves)
{
	if (SkeletalMeshComponent == nullptr)
	{
		return;
	}

	if (!EnsureCalledOutsideOfBracket(TEXT("AddMappedSkeletalMesh")))
	{
		return;
	}

	UControlRig* CR = SetupControlRigIfRequired();
	if (CR == nullptr)
	{
		// if we don't have a valid rig yet - delay it until tick component
		FSkeletalMeshToMap PendingMesh;
		PendingMesh.SkeletalMeshComponent = SkeletalMeshComponent;
		PendingMesh.Bones = Bones;
		PendingMesh.Curves = Curves;

		FScopeLock Lock(&gPendingSkeletalMeshesLock);
		TArray<FSkeletalMeshToMap>& PendingSkeletalMeshes = gPendingSkeletalMeshes.FindOrAdd(this);
		PendingSkeletalMeshes.Add(PendingMesh);
		return;
	}

	TArray<FControlRigComponentMappedElement> ElementsToMap;
	TArray<FControlRigComponentMappedBone> BonesToMap = Bones;
	if (BonesToMap.Num() == 0)
	{
		if (USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh)
		{
			if (USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
			{
				for (const FRigBone& RigBone : CR->GetBoneHierarchy())
				{
					if (Skeleton->GetReferenceSkeleton().FindBoneIndex(RigBone.Name) != INDEX_NONE)
					{
						FControlRigComponentMappedBone BoneToMap;
						BoneToMap.Source = RigBone.Name;
						BoneToMap.Target = RigBone.Name;
						BonesToMap.Add(BoneToMap);
					}
				}
			}
			else
			{
				ReportError(FString::Printf(TEXT("%s does not have a Skeleton set."), *SkeletalMesh->GetPathName()));
			}
		}
	}

	TArray<FControlRigComponentMappedCurve> CurvesToMap = Curves;
	if (CurvesToMap.Num() == 0)
	{
		if (USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh)
		{
			if (USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
			{
				for (const FRigCurve& RigCurve : CR->GetCurveContainer())
				{
					const FSmartNameMapping* CurveNameMapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
					if (CurveNameMapping)
					{
						FSmartName SmartName;
						if (CurveNameMapping->FindSmartName(RigCurve.Name, SmartName))
						{
							FControlRigComponentMappedCurve CurveToMap;
							CurveToMap.Source = RigCurve.Name;
							CurveToMap.Target = RigCurve.Name;
							CurvesToMap.Add(CurveToMap);
						}
					}
				}
			}
			else
			{
				ReportError(FString::Printf(TEXT("%s does not have a Skeleton set."), *SkeletalMesh->GetPathName()));
			}
		}
	}

	for (const FControlRigComponentMappedBone& BoneToMap : BonesToMap)
	{
		if (BoneToMap.Source.IsNone() ||
			BoneToMap.Target.IsNone())
		{
			continue;
		}

		FControlRigComponentMappedElement ElementToMap;
		ElementToMap.ComponentReference.OtherActor = SkeletalMeshComponent->GetOwner() != GetOwner() ? SkeletalMeshComponent->GetOwner() : nullptr;
		ElementToMap.ComponentReference.PathToComponent = SkeletalMeshComponent->GetName();

		ElementToMap.ElementName = BoneToMap.Source;
		ElementToMap.ElementType = ERigElementType::Bone;
		ElementToMap.TransformName = BoneToMap.Target;

		ElementsToMap.Add(ElementToMap);
	}

	for (const FControlRigComponentMappedCurve& CurveToMap : CurvesToMap)
	{
		if (CurveToMap.Source.IsNone() ||
			CurveToMap.Target.IsNone())
		{
			continue;
		}

		FControlRigComponentMappedElement ElementToMap;
		ElementToMap.ComponentReference.OtherActor = SkeletalMeshComponent->GetOwner() != GetOwner() ? SkeletalMeshComponent->GetOwner() : nullptr;
		ElementToMap.ComponentReference.PathToComponent = SkeletalMeshComponent->GetName();

		ElementToMap.ElementName = CurveToMap.Source;
		ElementToMap.ElementType = ERigElementType::Curve;
		ElementToMap.TransformName = CurveToMap.Target;

		ElementsToMap.Add(ElementToMap);
	}

	AddMappedElements(ElementsToMap);
}

void UControlRigComponent::AddMappedCompleteSkeletalMesh(USkeletalMeshComponent* SkeletalMeshComponent)
{
	AddMappedSkeletalMesh(SkeletalMeshComponent, TArray<FControlRigComponentMappedBone>(), TArray<FControlRigComponentMappedCurve>());
}

void UControlRigComponent::SetBoneInitialTransformsFromSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	if (InSkeletalMesh)
	{
		if (UControlRig* CR = SetupControlRigIfRequired())
		{
			CR->SetBoneInitialTransformsFromSkeletalMesh(InSkeletalMesh);
			bResetInitialsBeforeSetup = false;
		}
	}
}

FTransform UControlRigComponent::GetBoneTransform(FName BoneName, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		int32 BoneIndex = CR->GetBoneHierarchy().GetIndex(BoneName);
		if (BoneIndex != INDEX_NONE)
		{
			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				return CR->GetBoneHierarchy().GetLocalTransform(BoneIndex);
			}
			else
			{
				FTransform RootTransform = CR->GetBoneHierarchy().GetGlobalTransform(BoneIndex);
				ConvertTransformFromRigSpace(RootTransform, Space);
				return RootTransform;
			}
		}
	}

	return FTransform::Identity;
}

FTransform UControlRigComponent::GetInitialBoneTransform(FName BoneName, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		int32 BoneIndex = CR->GetBoneHierarchy().GetIndex(BoneName);
		if (BoneIndex != INDEX_NONE)
		{
			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				return CR->GetHierarchy()->GetInitialTransform(ERigElementType::Bone, BoneIndex);
			}
			else
			{
				FTransform RootTransform = CR->GetHierarchy()->GetInitialGlobalTransform(ERigElementType::Bone, BoneIndex);
				ConvertTransformFromRigSpace(RootTransform, Space);
				return RootTransform;
			}
		}
	}

	return FTransform::Identity;
}

void UControlRigComponent::SetBoneTransform(FName BoneName, FTransform Transform, EControlRigComponentSpace Space, float Weight, bool bPropagateToChildren)
{
	if (Weight <= SMALL_NUMBER)
	{
		return;
	}

	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		int32 BoneIndex = CR->GetBoneHierarchy().GetIndex(BoneName);
		if (BoneIndex != INDEX_NONE)
		{
			ConvertTransformToRigSpace(Transform, Space);

			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				if (Weight >= 1.f - SMALL_NUMBER)
				{
					CR->GetBoneHierarchy().SetLocalTransform(BoneIndex, Transform, bPropagateToChildren);
				}
				else
				{
					FTransform PreviousTransform = CR->GetBoneHierarchy().GetLocalTransform(BoneIndex);
					FTransform BlendedTransform = FControlRigMathLibrary::LerpTransform(PreviousTransform, Transform, Weight);
					CR->GetBoneHierarchy().SetLocalTransform(BoneIndex, Transform, bPropagateToChildren);
				}
			}
			else
			{
				if (Weight >= 1.f - SMALL_NUMBER)
				{
					CR->GetBoneHierarchy().SetGlobalTransform(BoneIndex, Transform, bPropagateToChildren);
				}
				else
				{
					FTransform PreviousTransform = CR->GetBoneHierarchy().GetGlobalTransform(BoneIndex);
					FTransform BlendedTransform = FControlRigMathLibrary::LerpTransform(PreviousTransform, Transform, Weight);
					CR->GetBoneHierarchy().SetGlobalTransform(BoneIndex, Transform, bPropagateToChildren);
				}
			}
		}
	}
}

void UControlRigComponent::SetInitialBoneTransform(FName BoneName, FTransform InitialTransform, EControlRigComponentSpace Space, bool bPropagateToChildren)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		FRigBoneHierarchy& HierarchyRef = CR->GetBoneHierarchy();
		int32 BoneIndex = HierarchyRef.GetIndex(BoneName);
		if (BoneIndex != INDEX_NONE)
		{
			if(!CR->IsRunningPreSetup() && !CR->IsRunningPostSetup())
			{
				ReportError(TEXT("SetInitialBoneTransform should only be called during OnPreSetup / OnPostSetup."));
				return;
			}

			ConvertTransformToRigSpace(InitialTransform, Space);

			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				int32 ParentIndex = HierarchyRef[BoneIndex].ParentIndex;
				if(ParentIndex != INDEX_NONE)
				{
					InitialTransform = InitialTransform * HierarchyRef[ParentIndex].InitialTransform;
				}
			}

			HierarchyRef.SetInitialGlobalTransform(BoneIndex, InitialTransform, bPropagateToChildren);
		}
	}	
}

bool UControlRigComponent::GetControlBool(FName ControlName)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Control->ControlType == ERigControlType::Bool)
			{
				return Control->GetValue().Get<bool>();
			}
		}
	}

	return false;
}

float UControlRigComponent::GetControlFloat(FName ControlName)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Control->ControlType == ERigControlType::Float)
			{
				return Control->GetValue().Get<float>();
			}
		}
	}

	return 0.f;
}

int32 UControlRigComponent::GetControlInt(FName ControlName)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Control->ControlType == ERigControlType::Integer)
			{
				return Control->GetValue().Get<int32>();
			}
		}
	}

	return 0.f;
}

FVector2D UControlRigComponent::GetControlVector2D(FName ControlName)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Control->ControlType == ERigControlType::Vector2D)
			{
				return Control->GetValue().Get<FVector2D>();
			}
		}
	}

	return FVector2D::ZeroVector;
}

FVector UControlRigComponent::GetControlPosition(FName ControlName, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				if (Control->ControlType == ERigControlType::Position)
				{
					return Control->GetValue().Get<FVector>();
				}
				else if (Control->ControlType == ERigControlType::TransformNoScale)
				{
					return Control->GetValue().Get<FTransformNoScale>().Location;
				}
				else if (Control->ControlType == ERigControlType::Transform)
				{
					return Control->GetValue().Get<FTransform>().GetLocation();
				}
			}
			else
			{
				FTransform RootTransform = CR->GetHierarchy()->GetGlobalTransform(ERigElementType::Control, Control->Index);
				ConvertTransformFromRigSpace(RootTransform, Space);
				return RootTransform.GetLocation();
			}
		}
	}

	return FVector::ZeroVector;
}

FRotator UControlRigComponent::GetControlRotator(FName ControlName, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				if (Control->ControlType == ERigControlType::Rotator)
				{
					return Control->GetValue().Get<FRotator>();
				}
				else if (Control->ControlType == ERigControlType::TransformNoScale)
				{
					return Control->GetValue().Get<FTransformNoScale>().Rotation.Rotator();
				}
				else if (Control->ControlType == ERigControlType::Transform)
				{
					return Control->GetValue().Get<FTransform>().GetRotation().Rotator();
				}
			}
			else
			{
				FTransform RootTransform = CR->GetHierarchy()->GetGlobalTransform(ERigElementType::Control, Control->Index);
				ConvertTransformFromRigSpace(RootTransform, Space);
				return RootTransform.GetRotation().Rotator();
			}
		}
	}

	return FRotator::ZeroRotator;
}

FVector UControlRigComponent::GetControlScale(FName ControlName, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				if (Control->ControlType == ERigControlType::Scale)
				{
					return Control->GetValue().Get<FVector>();
				}
				else if (Control->ControlType == ERigControlType::Transform)
				{
					return Control->GetValue().Get<FTransform>().GetScale3D();
				}
			}
			else
			{
				FTransform RootTransform = CR->GetHierarchy()->GetGlobalTransform(ERigElementType::Control, Control->Index);
				ConvertTransformFromRigSpace(RootTransform, Space);
				return RootTransform.GetScale3D();
			}
		}
	}

	return FVector::OneVector;
}

FTransform UControlRigComponent::GetControlTransform(FName ControlName, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				return CR->GetHierarchy()->GetLocalTransform(ERigElementType::Control, Control->Index);
			}
			else
			{
				FTransform RootTransform = CR->GetHierarchy()->GetGlobalTransform(ERigElementType::Control, Control->Index);
				ConvertTransformFromRigSpace(RootTransform, Space);
				return RootTransform;
			}
		}
	}

	return FTransform::Identity;
}

void UControlRigComponent::SetControlBool(FName ControlName, bool Value)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		CR->SetControlValue<bool>(ControlName, Value);
	}
}

void UControlRigComponent::SetControlFloat(FName ControlName, float Value)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		CR->SetControlValue<float>(ControlName, Value);
	}
}

void UControlRigComponent::SetControlInt(FName ControlName, int32 Value)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		CR->SetControlValue<int32>(ControlName, Value);
	}
}

void UControlRigComponent::SetControlVector2D(FName ControlName, FVector2D Value)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		CR->SetControlValue<FVector2D>(ControlName, Value);
	}
}

void UControlRigComponent::SetControlPosition(FName ControlName, FVector Value, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (Space == EControlRigComponentSpace::LocalSpace)
		{
			if (FRigControl* Control = CR->FindControl(ControlName))
			{
				if (Control->ControlType == ERigControlType::Position)
				{
					CR->SetControlValue<FVector>(ControlName, Value);
				}
				else if (Control->ControlType == ERigControlType::TransformNoScale)
				{
					FTransformNoScale Previous = Control->GetValue().Get<FTransformNoScale>();
					Previous.Location = Value;
					CR->SetControlValue<FTransformNoScale>(ControlName, Previous);
				}
				else if (Control->ControlType == ERigControlType::Transform)
				{
					FTransform Previous = Control->GetValue().Get<FTransform>();
					Previous.SetLocation(Value);
					CR->SetControlValue<FTransform>(ControlName, Previous);
				}
			}
		}
		else
		{
			FTransform Transform = FTransform::Identity;
			Transform.SetLocation(Value);
			ConvertTransformToRigSpace(Transform, Space);
			CR->SetControlGlobalTransform(ControlName, Transform);
		}
	}
}

void UControlRigComponent::SetControlRotator(FName ControlName, FRotator Value, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (Space == EControlRigComponentSpace::LocalSpace)
		{
			if (FRigControl* Control = CR->FindControl(ControlName))
			{
				if (Control->ControlType == ERigControlType::Rotator)
				{
					CR->SetControlValue<FRotator>(ControlName, Value);
				}
				else if (Control->ControlType == ERigControlType::TransformNoScale)
				{
					FTransformNoScale Previous = Control->GetValue().Get<FTransformNoScale>();
					Previous.Rotation = FQuat(Value);
					CR->SetControlValue<FTransformNoScale>(ControlName, Previous);
				}
				else if (Control->ControlType == ERigControlType::Transform)
				{
					FTransform Previous = Control->GetValue().Get<FTransform>();
					Previous.SetRotation(FQuat(Value));
					CR->SetControlValue<FTransform>(ControlName, Previous);
				}
			}
		}
		else
		{
			FTransform Transform = FTransform::Identity;
			Transform.SetRotation(FQuat(Value));
			ConvertTransformToRigSpace(Transform, Space);
			CR->SetControlGlobalTransform(ControlName, Transform);
		}
	}
}

void UControlRigComponent::SetControlScale(FName ControlName, FVector Value, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (Space == EControlRigComponentSpace::LocalSpace)
		{
			if (FRigControl* Control = CR->FindControl(ControlName))
			{
				if (Control->ControlType == ERigControlType::Scale)
				{
					CR->SetControlValue<FVector>(ControlName, Value);
				}
				else if (Control->ControlType == ERigControlType::Transform)
				{
					FTransform Previous = Control->GetValue().Get<FTransform>();
					Previous.SetScale3D(Value);
					CR->SetControlValue<FTransform>(ControlName, Previous);
				}
			}
		}
		else
		{
			FTransform Transform = FTransform::Identity;
			Transform.SetScale3D(Value);
			ConvertTransformToRigSpace(Transform, Space);
			CR->SetControlGlobalTransform(ControlName, Transform);
		}
	}
}

void UControlRigComponent::SetControlTransform(FName ControlName, FTransform Value, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				CR->GetHierarchy()->SetLocalTransform(ERigElementType::Control, Control->Index, Value);
			}
			else
			{
				ConvertTransformToRigSpace(Value, Space);
				CR->GetHierarchy()->SetGlobalTransform(ERigElementType::Control, Control->Index, Value);
			}
		}
	}
}

FTransform UControlRigComponent::GetControlOffset(FName ControlName, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				return Control->OffsetTransform;
			}
			else
			{
				FTransform RootTransform = CR->GetControlHierarchy().GetParentInitialTransform(Control->Index, true);
				ConvertTransformFromRigSpace(RootTransform, Space);
				return RootTransform;
			}
		}
	}

	return FTransform::Identity;
}

void UControlRigComponent::SetControlOffset(FName ControlName, FTransform OffsetTransform, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (FRigControl* Control = CR->FindControl(ControlName))
		{
			if (Space != EControlRigComponentSpace::LocalSpace)
			{
				ConvertTransformToRigSpace(OffsetTransform, Space);

				FTransform ParentTransform = CR->GetControlHierarchy().GetParentInitialTransform(Control->Index, false);
				OffsetTransform = OffsetTransform.GetRelativeTransform(ParentTransform);
			}

			CR->GetControlHierarchy().SetControlOffset(Control->Index, OffsetTransform);
		}
	}
}

FTransform UControlRigComponent::GetSpaceTransform(FName SpaceName, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		int32 SpaceIndex = CR->GetSpaceHierarchy().GetIndex(SpaceName);
		if (SpaceIndex != INDEX_NONE)
		{
			const FRigSpace& SpaceElement = CR->GetSpaceHierarchy()[SpaceIndex];
			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				return CR->GetHierarchy()->GetLocalTransform(ERigElementType::Space, SpaceElement.Index);
			}
			else
			{
				FTransform RootTransform = CR->GetHierarchy()->GetGlobalTransform(ERigElementType::Space, SpaceElement.Index);
				ConvertTransformFromRigSpace(RootTransform, Space);
				return RootTransform;
			}
		}
	}

	return FTransform::Identity;
}

FTransform UControlRigComponent::GetInitialSpaceTransform(FName SpaceName, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		int32 SpaceIndex = CR->GetSpaceHierarchy().GetIndex(SpaceName);
		if (SpaceIndex != INDEX_NONE)
		{
			const FRigSpace& SpaceElement = CR->GetSpaceHierarchy()[SpaceIndex];
			if (Space == EControlRigComponentSpace::LocalSpace)
			{
				return CR->GetHierarchy()->GetInitialTransform(ERigElementType::Space, SpaceElement.Index);
			}
			else
			{
				FTransform RootTransform = CR->GetHierarchy()->GetInitialGlobalTransform(ERigElementType::Space, SpaceElement.Index);
				ConvertTransformFromRigSpace(RootTransform, Space);
				return RootTransform;
			}
		}
	}

	return FTransform::Identity;
}

void UControlRigComponent::SetInitialSpaceTransform(FName SpaceName, FTransform InitialTransform, EControlRigComponentSpace Space)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		int32 SpaceIndex = CR->GetSpaceHierarchy().GetIndex(SpaceName);
		if (SpaceIndex != INDEX_NONE)
		{
			const FRigSpace& SpaceElement = CR->GetSpaceHierarchy()[SpaceIndex];

			if (Space != EControlRigComponentSpace::LocalSpace)
			{
				ConvertTransformToRigSpace(InitialTransform, Space);

				FTransform ParentTransform = CR->GetHierarchy()->GetInitialGlobalTransform(SpaceElement.GetParentElementKey());
				InitialTransform = InitialTransform.GetRelativeTransform(ParentTransform);
			}

			CR->GetHierarchy()->SetInitialTransform(SpaceElement.GetElementKey(), InitialTransform);
		}
	}
}

UControlRig* UControlRigComponent::SetupControlRigIfRequired()
{
	if(ControlRig != nullptr)
	{
		if (ControlRig->GetClass() != ControlRigClass)
		{
			ControlRig->OnInitialized_AnyThread().RemoveAll(this);
			ControlRig->OnPreSetup_AnyThread().RemoveAll(this);
			ControlRig->OnPostSetup_AnyThread().RemoveAll(this);
			ControlRig->OnExecuted_AnyThread().RemoveAll(this);
			ControlRig = nullptr;
		}
		else
		{
			return ControlRig;
		}
	}

	if(ControlRigClass)
	{
		ControlRig = NewObject<UControlRig>(this, ControlRigClass);
		ControlRig->VM = NewObject<URigVM>(ControlRig);

		SetControlRig(ControlRig);

		if (ControlRigCreatedEvent.IsBound())
		{
			ControlRigCreatedEvent.Broadcast(this);
		}

		ValidateMappingData();
	}

	return ControlRig;
}
void UControlRigComponent::SetControlRig(UControlRig* InControlRig)
{
	ControlRig = InControlRig;
	ControlRig->OnInitialized_AnyThread().AddUObject(this, &UControlRigComponent::HandleControlRigInitializedEvent);
	ControlRig->OnPreSetup_AnyThread().AddUObject(this, &UControlRigComponent::HandleControlRigPreSetupEvent);
	ControlRig->OnPostSetup_AnyThread().AddUObject(this, &UControlRigComponent::HandleControlRigPostSetupEvent);
	ControlRig->OnExecuted_AnyThread().AddUObject(this, &UControlRigComponent::HandleControlRigExecutedEvent);

	ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, this);

	ControlRig->Initialize();
}

void UControlRigComponent::ValidateMappingData()
{
	TMap<USkeletalMeshComponent*, FCachedSkeletalMeshComponentSettings> NewCachedSettings;

	if(ControlRig)
	{
		for (FControlRigComponentMappedElement& MappedElement : MappedElements)
		{
			MappedElement.ElementIndex = INDEX_NONE;
			MappedElement.SubIndex = INDEX_NONE;

			AActor* MappedOwner = MappedElement.ComponentReference.OtherActor == nullptr ? GetOwner() : MappedElement.ComponentReference.OtherActor;
			MappedElement.SceneComponent = Cast<USceneComponent>(MappedElement.ComponentReference.GetComponent(MappedOwner));

			if (MappedElement.SceneComponent == nullptr ||
				MappedElement.SceneComponent == this ||
				MappedElement.ElementName.IsNone())
			{
				continue;
			}

			if (MappedElement.Direction == EControlRigComponentMapDirection::Output && MappedElement.Weight <= SMALL_NUMBER)
			{
				continue;
			}

			FRigElementKey Key(MappedElement.ElementName, MappedElement.ElementType);
			MappedElement.ElementIndex = ControlRig->GetHierarchy()->GetIndex(Key);
			MappedElement.SubIndex = MappedElement.TransformIndex;

			if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MappedElement.SceneComponent))
			{
				MappedElement.Space = EControlRigComponentSpace::ComponentSpace;

				MappedElement.SubIndex = INDEX_NONE;
				if (MappedElement.TransformIndex >= 0 && MappedElement.TransformIndex < SkeletalMeshComponent->GetNumBones())
				{
					MappedElement.SubIndex = MappedElement.TransformIndex;
				}
				else if (!MappedElement.TransformName.IsNone())
				{
					if (USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh)
					{
						if (USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
						{
							if (MappedElement.ElementType == ERigElementType::Curve)
							{
								const FSmartNameMapping* CurveNameMapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
								if (CurveNameMapping)
								{
									FSmartName SmartName;
									if (CurveNameMapping->FindSmartName(MappedElement.TransformName, SmartName))
									{
										MappedElement.SubIndex = (int32)SmartName.UID;
									}
								}
							} 
							else
							{
								MappedElement.SubIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(MappedElement.TransformName);
							}
						}
						else
						{
							ReportError(FString::Printf(TEXT("%s does not have a Skeleton set."), *SkeletalMesh->GetPathName()));
						}
					}
				}

				// if we didn't find the bone, disable this mapped element
				if (MappedElement.SubIndex == INDEX_NONE)
				{
					MappedElement.ElementIndex = INDEX_NONE;
					continue;
				}

				if (MappedElement.Direction == EControlRigComponentMapDirection::Output)
				{
					if (!NewCachedSettings.Contains(SkeletalMeshComponent))
					{
						FCachedSkeletalMeshComponentSettings PreviousSettings(SkeletalMeshComponent);
						NewCachedSettings.Add(SkeletalMeshComponent, PreviousSettings);
					}

					//If the animinstance is a sequencer instance don't replace it that means we are already running an animation on the skeleton
					//and don't want to replace the anim instance.
					if (Cast<ISequencerAnimationSupport>(SkeletalMeshComponent->GetAnimInstance()) == nullptr)
					{
						SkeletalMeshComponent->SetAnimInstanceClass(UControlRigAnimInstance::StaticClass());
					}
				}
			}
		}
	}

	// for the skeletal mesh components we no longer map, let's remove it
	for (TPair<USkeletalMeshComponent*, FCachedSkeletalMeshComponentSettings>& Pair : CachedSkeletalMeshComponentSettings)
	{
		FCachedSkeletalMeshComponentSettings* NewCachedSetting = NewCachedSettings.Find(Pair.Key);
		if (NewCachedSetting)
		{
			*NewCachedSetting = Pair.Value;
		}
		else
		{
			Pair.Value.Apply(Pair.Key);
		}
	}

	CachedSkeletalMeshComponentSettings = NewCachedSettings;
}

void UControlRigComponent::TransferInputs()
{
	if (ControlRig)
	{
		for (FControlRigComponentMappedElement& MappedElement : MappedElements)
		{
			if (MappedElement.ElementIndex == INDEX_NONE || MappedElement.Direction == EControlRigComponentMapDirection::Output)
			{
				continue;
			}

			FTransform Transform = FTransform::Identity;
			if (MappedElement.SubIndex >= 0)
			{
				if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MappedElement.SceneComponent))
				{
					// todo: optimize
					Transform = SkeletalMeshComponent->GetBoneTransform(MappedElement.SubIndex, FTransform::Identity);
				}
				else if (UInstancedStaticMeshComponent* InstancingComponent = Cast<UInstancedStaticMeshComponent>(MappedElement.SceneComponent))
				{
					if (MappedElement.SubIndex < InstancingComponent->GetNumRenderInstances())
					{
						InstancingComponent->GetInstanceTransform(MappedElement.SubIndex, Transform, true);
					}
					else
					{
						continue;
					}
				}
			}
			else
			{
				Transform = MappedElement.SceneComponent->GetComponentToWorld();
			}

			Transform = MappedElement.Offset * Transform;

			ConvertTransformToRigSpace(Transform, MappedElement.Space);

			if (MappedElement.ElementType == ERigElementType::Control)
			{
				ControlRig->SetControlGlobalTransform(MappedElement.ElementName, Transform);
			}
			else
			{
				ControlRig->GetHierarchy()->SetGlobalTransform(MappedElement.ElementType, MappedElement.ElementIndex, Transform);
			}
		}
	}
}

void UControlRigComponent::TransferOutputs()
{
	if (ControlRig)
	{
		USceneComponent* LastComponent = nullptr;
		FControlRigAnimInstanceProxy* Proxy = nullptr;

		for (FControlRigComponentMappedElement& MappedElement : MappedElements)
		{
			if (LastComponent != MappedElement.SceneComponent || Proxy == nullptr)
			{
				Proxy = MappedElement.GetAnimProxyOnGameThread();
				if (Proxy)
				{
					Proxy->StoredTransforms.Reset();
					Proxy->StoredCurves.Reset();
					LastComponent = MappedElement.SceneComponent;
				}
			}
		}

		TArray<USkeletalMeshComponent*> ComponentsToTick;

		for (FControlRigComponentMappedElement& MappedElement : MappedElements)
		{
			if (MappedElement.ElementIndex == INDEX_NONE || MappedElement.Direction == EControlRigComponentMapDirection::Input)
			{
				continue;
			}

			if (MappedElement.ElementType == ERigElementType::Bone ||
				MappedElement.ElementType == ERigElementType::Control ||
				MappedElement.ElementType == ERigElementType::Space)
			{
				FTransform Transform = ControlRig->GetHierarchy()->GetGlobalTransform(MappedElement.ElementType, MappedElement.ElementIndex);
				ConvertTransformFromRigSpace(Transform, MappedElement.Space);

				Transform = MappedElement.Offset * Transform;

				if (MappedElement.SubIndex >= 0)
				{
					if (LastComponent != MappedElement.SceneComponent || Proxy == nullptr)
					{
						Proxy = MappedElement.GetAnimProxyOnGameThread();
						if (Proxy)
						{
							LastComponent = MappedElement.SceneComponent;
						}
					}

					if (Proxy)
					{
						ComponentsToTick.AddUnique(Cast<USkeletalMeshComponent>(MappedElement.SceneComponent));
						Proxy->StoredTransforms.FindOrAdd(MappedElement.SubIndex) = Transform;
					}
					else if (UInstancedStaticMeshComponent* InstancingComponent = Cast<UInstancedStaticMeshComponent>(MappedElement.SceneComponent))
					{
						if (MappedElement.SubIndex < InstancingComponent->GetNumRenderInstances())
						{
							if (MappedElement.Weight < 1.f - SMALL_NUMBER)
							{
								FTransform Previous = FTransform::Identity;
								InstancingComponent->GetInstanceTransform(MappedElement.SubIndex, Previous, true);
								Transform = FControlRigMathLibrary::LerpTransform(Previous, Transform, FMath::Clamp<float>(MappedElement.Weight, 0.f, 1.f));
							}
							InstancingComponent->UpdateInstanceTransform(MappedElement.SubIndex, Transform, true, true, true);
						}
					}
				}
				else
				{
					if (MappedElement.Weight < 1.f - SMALL_NUMBER)
					{
						FTransform Previous = MappedElement.SceneComponent->GetComponentToWorld();
						Transform = FControlRigMathLibrary::LerpTransform(Previous, Transform, FMath::Clamp<float>(MappedElement.Weight, 0.f, 1.f));
					}
					MappedElement.SceneComponent->SetWorldTransform(Transform);
				}
			}
			else if (MappedElement.ElementType == ERigElementType::Curve)
			{
				if (MappedElement.SubIndex >= 0)
				{
					if (LastComponent != MappedElement.SceneComponent || Proxy == nullptr)
					{
						Proxy = MappedElement.GetAnimProxyOnGameThread();
						if (Proxy)
						{
							LastComponent = MappedElement.SceneComponent;
						}
					}

					if (Proxy)
					{
						ComponentsToTick.AddUnique(Cast<USkeletalMeshComponent>(MappedElement.SceneComponent));
						Proxy->StoredCurves.FindOrAdd((SmartName::UID_Type)MappedElement.SubIndex) = ControlRig->GetCurveContainer()[MappedElement.ElementIndex].Value;
					}
				}
			}
		}

		for (USkeletalMeshComponent* SkeletalMeshComponent : ComponentsToTick)
		{
			if (SkeletalMeshComponent)
			{
				if (SkeletalMeshComponent->IsValidLowLevel() &&
					!SkeletalMeshComponent->HasAnyFlags(RF_BeginDestroyed) &&
					!SkeletalMeshComponent->IsPendingKill())
				{
					SkeletalMeshComponent->TickAnimation(0.f, false);
					SkeletalMeshComponent->RefreshBoneTransforms();
					SkeletalMeshComponent->RefreshSlaveComponents();
					SkeletalMeshComponent->UpdateComponentToWorld();
					SkeletalMeshComponent->FinalizeBoneTransform();
					SkeletalMeshComponent->MarkRenderTransformDirty();
					SkeletalMeshComponent->MarkRenderDynamicDataDirty();
				}
			}
		}
	}
}

void UControlRigComponent::HandleControlRigInitializedEvent(UControlRig* InControlRig, const EControlRigState InState, const FName& InEventName)
{
#if WITH_EDITOR
	if (bUpdateInEditor)
	{
		FEditorScriptExecutionGuard AllowScripts;
		OnPostInitialize(this);
	}
	else
#endif
	{
		OnPostInitialize(this);
	}
}

void UControlRigComponent::HandleControlRigPreSetupEvent(UControlRig* InControlRig, const EControlRigState InState, const FName& InEventName)
{
	TArray<USkeletalMeshComponent*> ComponentsToTick;

	USceneComponent* LastComponent = nullptr;
	FControlRigAnimInstanceProxy* Proxy = nullptr;

	for (FControlRigComponentMappedElement& MappedElement : MappedElements)
	{
		if (LastComponent != MappedElement.SceneComponent || Proxy == nullptr)
		{
			Proxy = MappedElement.GetAnimProxyOnGameThread();
			if (Proxy)
			{
				Proxy->StoredTransforms.Reset();
				Proxy->StoredCurves.Reset();
				LastComponent = MappedElement.SceneComponent;
			}
		}

		if (USkeletalMeshComponent* Component = Cast< USkeletalMeshComponent>(MappedElement.SceneComponent))
		{
			ComponentsToTick.Add(Component);
		}
	}

	for (USkeletalMeshComponent* SkeletalMeshComponent : ComponentsToTick)
	{
		SkeletalMeshComponent->TickAnimation(0.f, false);
		SkeletalMeshComponent->RefreshBoneTransforms();
		SkeletalMeshComponent->RefreshSlaveComponents();
		SkeletalMeshComponent->UpdateComponentToWorld();
		SkeletalMeshComponent->FinalizeBoneTransform();
		SkeletalMeshComponent->MarkRenderTransformDirty();
		SkeletalMeshComponent->MarkRenderDynamicDataDirty();
	}

#if WITH_EDITOR
	if (bUpdateInEditor)
	{
		FEditorScriptExecutionGuard AllowScripts;
		OnPreSetup(this);
	}
	else
#endif
	{
		OnPreSetup(this);
	}
}

void UControlRigComponent::HandleControlRigPostSetupEvent(UControlRig* InControlRig, const EControlRigState InState, const FName& InEventName)
{
#if WITH_EDITOR
	if (bUpdateInEditor)
	{
		FEditorScriptExecutionGuard AllowScripts;
		OnPostSetup(this);
	}
	else
#endif
	{
		OnPostSetup(this);
	}
}

void UControlRigComponent::HandleControlRigExecutedEvent(UControlRig* InControlRig, const EControlRigState InState, const FName& InEventName)
{
	if (InEventName == FRigUnit_BeginExecution::EventName)
	{
#if WITH_EDITOR
		if (bUpdateInEditor)
		{
			FEditorScriptExecutionGuard AllowScripts;
			OnPostUpdate(this);
		}
		else
#endif
		{
			OnPostUpdate(this);
		}
	}
}

void UControlRigComponent::ConvertTransformToRigSpace(FTransform& InOutTransform, EControlRigComponentSpace FromSpace)
{
	switch (FromSpace)
	{
		case EControlRigComponentSpace::WorldSpace:
		{
			InOutTransform = InOutTransform.GetRelativeTransform(GetComponentToWorld());
			break;
		}
		case EControlRigComponentSpace::ActorSpace:
		{
			InOutTransform = InOutTransform.GetRelativeTransform(GetRelativeTransform());
			break;
		}
		case EControlRigComponentSpace::ComponentSpace:
		case EControlRigComponentSpace::RigSpace:
		case EControlRigComponentSpace::LocalSpace:
		default:
		{
			// nothing to do
			break;
		}
	}
}

void UControlRigComponent::ConvertTransformFromRigSpace(FTransform& InOutTransform, EControlRigComponentSpace ToSpace)
{
	switch (ToSpace)
	{
		case EControlRigComponentSpace::WorldSpace:
		{
			InOutTransform = InOutTransform * GetComponentToWorld();
			break;
		}
		case EControlRigComponentSpace::ActorSpace:
		{
			InOutTransform = InOutTransform * GetRelativeTransform();
			break;
		}
		case EControlRigComponentSpace::ComponentSpace:
		case EControlRigComponentSpace::RigSpace:
		case EControlRigComponentSpace::LocalSpace:
		default:
		{
			// nothing to do
			break;
		}
	}
}

bool UControlRigComponent::EnsureCalledOutsideOfBracket(const TCHAR* InCallingFunctionName)
{
	if (UControlRig* CR = SetupControlRigIfRequired())
	{
		if (CR->IsRunningPreSetup())
		{
			if (InCallingFunctionName)
			{
				ReportError(FString::Printf(TEXT("%s cannot be called during the PreSetupEvent - use ConstructionScript instead."), InCallingFunctionName));
				return false;
			}
			else
			{
				ReportError(FString::Printf(TEXT("Cannot be called during the PreSetupEvent - use ConstructionScript instead."), InCallingFunctionName));
				return false;
			}
		}

		if (CR->IsRunningPostSetup())
		{
			if (InCallingFunctionName)
			{
				ReportError(FString::Printf(TEXT("%s cannot be called during the PostSetupEvent - use ConstructionScript instead."), InCallingFunctionName));
				return false;
			}
			else
			{
				ReportError(FString::Printf(TEXT("Cannot be called during the PostSetupEvent - use ConstructionScript instead."), InCallingFunctionName));
				return false;
			}
		}

		if (CR->IsInitializing())
		{
			if (InCallingFunctionName)
			{
				ReportError(FString::Printf(TEXT("%s cannot be called during the InitEvent - use ConstructionScript instead."), InCallingFunctionName));
				return false;
			}
			else
			{
				ReportError(FString::Printf(TEXT("Cannot be called during the InitEvent - use ConstructionScript instead."), InCallingFunctionName));
				return false;
			}
		}

		if (CR->IsExecuting())
		{
			if (InCallingFunctionName)
			{
				ReportError(FString::Printf(TEXT("%s cannot be called during the UpdateEvent - use ConstructionScript instead."), InCallingFunctionName));
				return false;
			}
			else
			{
				ReportError(FString::Printf(TEXT("Cannot be called during the UpdateEvent - use ConstructionScript instead."), InCallingFunctionName));
				return false;
			}
		}
	}
	
	return true;
}

void UControlRigComponent::ReportError(const FString& InMessage)
{
	UE_LOG(LogControlRig, Warning, TEXT("%s: %s"), *GetPathName(), *InMessage);

#if WITH_EDITOR

	if (GetWorld()->IsEditorWorld())
	{
		const TSharedPtr<SNotificationItem>* ExistingItemPtr = EditorNotifications.Find(InMessage);
		if (ExistingItemPtr)
		{
			const TSharedPtr<SNotificationItem>& ExistingItem = *ExistingItemPtr;
			if (ExistingItem.IsValid())
			{
				if (ExistingItem->HasActiveTimers())
				{
					return;
				}
				else
				{
					EditorNotifications.Remove(InMessage);
				}
			}
		}

		FNotificationInfo Info(FText::FromString(InMessage));
		Info.bUseSuccessFailIcons = true;
		Info.Image = FEditorStyle::GetBrush(TEXT("MessageLog.Warning"));
		Info.bFireAndForget = true;
		Info.bUseThrobber = true;
		Info.FadeOutDuration = 8.0f;
		Info.ExpireDuration = Info.FadeOutDuration;
		TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
		NotificationPtr->SetCompletionState(SNotificationItem::CS_Fail);

		EditorNotifications.Add(InMessage, NotificationPtr);
	}
#endif
}

FControlRigSceneProxy::FControlRigSceneProxy(const UControlRigComponent* InComponent)
: FPrimitiveSceneProxy(InComponent)
, ControlRigComponent(InComponent)
{
	bWillEverBeLit = false;
}

SIZE_T FControlRigSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FControlRigSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	if (ControlRigComponent->ControlRig == nullptr)
	{
		return;
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

			bool bShouldDrawBones = ControlRigComponent->bDrawBones && ControlRigComponent->ControlRig != nullptr;

			// make sure to check if we are within a preview / editor world
			// or the console variable draw bones is turned on
			if (bShouldDrawBones)
			{
				if (UWorld* World = ControlRigComponent->GetWorld())
				{
					if (!World->IsPreviewWorld())
					{
						const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;
						bShouldDrawBones = EngineShowFlags.Bones != 0;
					}
				}
			}

			if (bShouldDrawBones)
			{
				FTransform Transform = ControlRigComponent->GetComponentToWorld();
				const float MaxDrawRadius = ControlRigComponent->Bounds.SphereRadius * 0.02f;
				const FRigBoneHierarchy& Hierarchy = ControlRigComponent->ControlRig->GetBoneHierarchy();

				for (const FRigBone& Bone : Hierarchy)
				{
					const int32 ParentIndex = Bone.ParentIndex;
					const FLinearColor LineColor = FLinearColor::White;

					FVector Start, End;
					if (ParentIndex >= 0)
					{
						Start = Hierarchy[ParentIndex].GlobalTransform.GetLocation();
						End = Bone.GlobalTransform.GetLocation();
					}
					else
					{
						Start = FVector::ZeroVector;
						End = Bone.GlobalTransform.GetLocation();
					}

					Start = Transform.TransformPosition(Start);
					End = Transform.TransformPosition(End);

					const float BoneLength = (End - Start).Size();
					// clamp by bound, we don't want too long or big
					const float Radius = FMath::Clamp(BoneLength * 0.05f, 0.1f, MaxDrawRadius);

					//Render Sphere for bone end point and a cone between it and its parent.
					SkeletalDebugRendering::DrawWireBone(PDI, Start, End, LineColor, SDPG_Foreground, Radius);
				}
			}

			if (ControlRigComponent->bShowDebugDrawing)
			{
				const FControlRigDrawInterface& DrawInterface = ControlRigComponent->ControlRig->GetDrawInterface();

				for (int32 InstructionIndex = 0; InstructionIndex < DrawInterface.Num(); InstructionIndex++)
				{
					const FControlRigDrawInstruction& Instruction = DrawInterface[InstructionIndex];
					if (Instruction.Positions.Num() == 0)
					{
						continue;
					}

					FTransform InstructionTransform = Instruction.Transform * ControlRigComponent->GetComponentToWorld();
					switch (Instruction.PrimitiveType)
					{
						case EControlRigDrawSettings::Points:
						{
							for (const FVector& Point : Instruction.Positions)
							{
								PDI->DrawPoint(InstructionTransform.TransformPosition(Point), Instruction.Color, Instruction.Thickness, SDPG_Foreground);
							}
							break;
						}
						case EControlRigDrawSettings::Lines:
						{
							const TArray<FVector>& Points = Instruction.Positions;
							PDI->AddReserveLines(SDPG_Foreground, Points.Num() / 2, false, Instruction.Thickness > SMALL_NUMBER);
							for (int32 PointIndex = 0; PointIndex < Points.Num() - 1; PointIndex += 2)
							{
								PDI->DrawLine(InstructionTransform.TransformPosition(Points[PointIndex]), InstructionTransform.TransformPosition(Points[PointIndex + 1]), Instruction.Color, SDPG_Foreground, Instruction.Thickness);
							}
							break;
						}
						case EControlRigDrawSettings::LineStrip:
						{
							const TArray<FVector>& Points = Instruction.Positions;
							PDI->AddReserveLines(SDPG_Foreground, Points.Num() - 1, false, Instruction.Thickness > SMALL_NUMBER);
							for (int32 PointIndex = 0; PointIndex < Points.Num() - 1; PointIndex++)
							{
								PDI->DrawLine(InstructionTransform.TransformPosition(Points[PointIndex]), InstructionTransform.TransformPosition(Points[PointIndex + 1]), Instruction.Color, SDPG_Foreground, Instruction.Thickness);
							}
							break;
						}
					}
				}
			}
		}
	}
}

/**
*  Returns a struct that describes to the renderer when to draw this proxy.
*	@param		Scene view to use to determine our relevence.
*  @return		View relevance struct
*/
FPrimitiveViewRelevance FControlRigSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance ViewRelevance;
	ViewRelevance.bDrawRelevance = IsShown(View);
	ViewRelevance.bDynamicRelevance = true;
	// ideally the TranslucencyRelevance should be filled out by the material, here we do it conservative
	ViewRelevance.bSeparateTranslucency = ViewRelevance.bNormalTranslucency = true;
	return ViewRelevance;
}

uint32 FControlRigSceneProxy::GetMemoryFootprint(void) const
{
	return(sizeof(*this) + GetAllocatedSize());
}

uint32 FControlRigSceneProxy::GetAllocatedSize(void) const
{
	return FPrimitiveSceneProxy::GetAllocatedSize();
}
