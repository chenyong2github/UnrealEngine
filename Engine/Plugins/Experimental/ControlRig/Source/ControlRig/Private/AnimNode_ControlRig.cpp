// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ControlRig.h"
#include "ControlRig.h"
#include "ControlRigComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "GameFramework/Actor.h"
#include "Animation/NodeMappingContainer.h"
#include "AnimationRuntime.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

FAnimNode_ControlRig::FAnimNode_ControlRig()
	: FAnimNode_ControlRigBase()
	, ControlRig(nullptr)
	, Alpha(1.f)
	, AlphaInputType(EAnimAlphaInputType::Float)
	, bAlphaBoolEnabled(true)
	, AlphaCurveName(NAME_None)
{
}

void FAnimNode_ControlRig::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (ControlRigClass)
	{
		ControlRig = NewObject<UControlRig>(InAnimInstance->GetOwningComponent(), ControlRigClass);
		ControlRig->Initialize(true);
		ControlRig->RequestInit();
	}

	FAnimNode_ControlRigBase::OnInitializeAnimInstance(InProxy, InAnimInstance);

#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->OnObjectsReplaced().AddRaw(this, &FAnimNode_ControlRig::OnObjectsReplaced);
	}
#endif // WITH_EDITOR

	InitializeProperties(InAnimInstance, GetTargetClass());
}

FAnimNode_ControlRig::~FAnimNode_ControlRig()
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->OnObjectsReplaced().RemoveAll(this);
	}
#endif // WITH_EDITOR
}

void FAnimNode_ControlRig::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("(%s)"), *GetNameSafe(ControlRigClass.Get()));
	DebugData.AddDebugItem(DebugLine);
	Source.GatherDebugData(DebugData.BranchFlow(1.f));
}

void FAnimNode_ControlRig::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	GetEvaluateGraphExposedInputs().Execute(Context);

	// alpha handlers
	InternalBlendAlpha = 0.f;
	switch (AlphaInputType)
	{
	case EAnimAlphaInputType::Float:
		InternalBlendAlpha = AlphaScaleBias.ApplyTo(AlphaScaleBiasClamp.ApplyTo(Alpha, Context.GetDeltaTime()));
		break;
	case EAnimAlphaInputType::Bool:
		InternalBlendAlpha = AlphaBoolBlend.ApplyTo(bAlphaBoolEnabled, Context.GetDeltaTime());
		break;
	case EAnimAlphaInputType::Curve:
		if (UAnimInstance* AnimInstance = Cast<UAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject()))
		{
			InternalBlendAlpha = AlphaScaleBiasClamp.ApplyTo(AnimInstance->GetCurveValue(AlphaCurveName), Context.GetDeltaTime());
		}
		break;
	};

	// Make sure Alpha is clamped between 0 and 1.
	InternalBlendAlpha = FMath::Clamp<float>(InternalBlendAlpha, 0.f, 1.f);

	PropagateInputProperties(Context.AnimInstanceProxy->GetAnimInstanceObject());

	FAnimNode_ControlRigBase::Update_AnyThread(Context);

	TRACE_ANIM_NODE_VALUE(Context, TEXT("Class"), *GetNameSafe(ControlRigClass.Get()));
}

void FAnimNode_ControlRig::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_ControlRigBase::Initialize_AnyThread(Context);

	AlphaBoolBlend.Reinitialize();
	AlphaScaleBiasClamp.Reinitialize();
}

void FAnimNode_ControlRig::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_ControlRigBase::CacheBones_AnyThread(Context);

	FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
	InputToCurveMappingUIDs.Reset();
	TArray<FName> const& UIDToNameLookUpTable = RequiredBones.GetUIDToNameLookupTable();

	auto CacheCurveMappingUIDs = [&](const TMap<FName, FName>& Mapping, TArray<FName> const& InUIDToNameLookUpTable, 
		const FAnimationCacheBonesContext& InContext)
	{
		for (auto Iter = Mapping.CreateConstIterator(); Iter; ++Iter)
		{
			// we need to have list of variables using pin
			const FName SourcePath = Iter.Key();
			const FName CurveName = Iter.Value();

			if (SourcePath != NAME_None && CurveName != NAME_None)
			{
				int32 Found = InUIDToNameLookUpTable.Find(CurveName);
				if (Found != INDEX_NONE)
				{
					// set value - sound should be UID
					InputToCurveMappingUIDs.Add(Iter.Value()) = Found;
				}
				else
				{
					UE_LOG(LogAnimation, Warning, TEXT("Curve %s Not Found from the Skeleton %s"), 
						*CurveName.ToString(), *GetNameSafe(InContext.AnimInstanceProxy->GetSkeleton()));
				}
			}

			// @todo: should we clear the item if not found?
		}
	};

	CacheCurveMappingUIDs(InputMapping, UIDToNameLookUpTable, Context);
	CacheCurveMappingUIDs(OutputMapping, UIDToNameLookUpTable, Context);
}

void FAnimNode_ControlRig::Evaluate_AnyThread(FPoseContext & Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// evaluate 
	FAnimNode_ControlRigBase::Evaluate_AnyThread(Output);
}

void FAnimNode_ControlRig::PostSerialize(const FArchive& Ar)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// after compile, we have to reinitialize
	// because it needs new execution code
	// since memory has changed
	if (Ar.IsObjectReferenceCollector())
	{
		if (ControlRig)
		{
			ControlRig->Initialize();
		}
	}
}

void FAnimNode_ControlRig::UpdateInput(UControlRig* InControlRig, const FPoseContext& InOutput)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_ControlRigBase::UpdateInput(InControlRig, InOutput);

	// now go through variable mapping table and see if anything is mapping through input
	if (InputMapping.Num() > 0 && InControlRig)
	{
		for (auto Iter = InputMapping.CreateConstIterator(); Iter; ++Iter)
		{
			// we need to have list of variables using pin
			const FName SourcePath = Iter.Key();
			if (SourcePath != NAME_None)
			{
				const FName CurveName = Iter.Value();

				SmartName::UID_Type UID = *InputToCurveMappingUIDs.Find(CurveName);
				if (UID != SmartName::MaxUID)
				{
					const float Value = InOutput.Curve.Get(UID);

					FRigVMParameter Parameter = InControlRig->GetVM()->GetParameterByName(SourcePath);
					if (Parameter.GetType() == ERigVMParameterType::Input && Parameter.GetCPPType() == TEXT("float"))
					{
						InControlRig->GetVM()->SetParameterValue<float>(Parameter, Value);
					}
					else
					{
						UE_LOG(LogAnimation, Warning, TEXT("[%s] Missing Input Parameter [%s]"), *GetNameSafe(InControlRig->GetClass()), *SourcePath.ToString());
					}
				}
			}
		} 
	}
}

void FAnimNode_ControlRig::UpdateOutput(UControlRig* InControlRig, FPoseContext& InOutput)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_ControlRigBase::UpdateOutput(InControlRig, InOutput);

	if (OutputMapping.Num() > 0 && InControlRig)
	{
		for (auto Iter = OutputMapping.CreateConstIterator(); Iter; ++Iter)
		{
			// we need to have list of variables using pin
			const FName SourcePath = Iter.Key();
			const FName CurveName = Iter.Value();

			if (SourcePath != NAME_None)
			{
				FRigVMParameter Parameter = InControlRig->GetVM()->GetParameterByName(SourcePath);
				if (Parameter.GetType() == ERigVMParameterType::Output && Parameter.GetCPPType() == TEXT("float"))
				{
					float Value = InControlRig->GetVM()->GetParameterValue<float>(Parameter);
					SmartName::UID_Type* UID = InputToCurveMappingUIDs.Find(Iter.Value());
					if (UID)
					{
						InOutput.Curve.Set(*UID, Value);
					}
				}
				else
				{
					UE_LOG(LogAnimation, Warning, TEXT("[%s] Missing Output Parameter [%s]"), *GetNameSafe(ControlRig->GetClass()), *SourcePath.ToString());
				}
			}
		}
	}
}

void FAnimNode_ControlRig::SetIOMapping(bool bInput, const FName& SourceProperty, const FName& TargetCurve)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UClass* TargetClass = GetTargetClass();
	if (TargetClass)
	{
		UControlRig* CDO = TargetClass->GetDefaultObject<UControlRig>();
		if (CDO)
		{
			TMap<FName, FName>& MappingData = (bInput) ? InputMapping : OutputMapping;

			// if it's valid as of now, we add it
			ERigVMParameterType ParameterType = CDO->GetVM()->GetParameterByName(SourceProperty).GetType();
			if ((bInput && (ParameterType == ERigVMParameterType::Input)) ||
				(!bInput && (ParameterType == ERigVMParameterType::Output)))
			{
				if (TargetCurve == NAME_None)
				{
					MappingData.Remove(SourceProperty);
				}
				else
				{
					MappingData.FindOrAdd(SourceProperty) = TargetCurve;
				}
			}
		}
	}
}

FName FAnimNode_ControlRig::GetIOMapping(bool bInput, const FName& SourceProperty) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TMap<FName, FName>& MappingData = (bInput) ? InputMapping : OutputMapping;
	if (const FName* NameFound = MappingData.Find(SourceProperty))
	{
		return *NameFound;
	}

	return NAME_None;
}

void FAnimNode_ControlRig::InitializeProperties(const UObject* InSourceInstance, UClass* InTargetClass)
{
	// Build property lists
	SourceProperties.Reset(SourcePropertyNames.Num());
	DestProperties.Reset(SourcePropertyNames.Num());

	check(SourcePropertyNames.Num() == DestPropertyNames.Num());

	for (int32 Idx = 0; Idx < SourcePropertyNames.Num(); ++Idx)
	{
		FName& SourceName = SourcePropertyNames[Idx];
		UClass* SourceClass = InSourceInstance->GetClass();

		FProperty* SourceProperty = FindFProperty<FProperty>(SourceClass, SourceName);
		SourceProperties.Add(SourceProperty);
		DestProperties.Add(nullptr);
	}
}

void FAnimNode_ControlRig::PropagateInputProperties(const UObject* InSourceInstance)
{
	if (TargetInstance)
	{
		UControlRig* TargetControlRig = Cast<UControlRig>((UObject*)TargetInstance);

		// First copy properties
		check(SourceProperties.Num() == DestProperties.Num());
		for (int32 PropIdx = 0; PropIdx < SourceProperties.Num(); ++PropIdx)
		{
			FProperty* CallerProperty = SourceProperties[PropIdx];

			FRigVMParameter DestParameter = TargetControlRig->GetVM()->GetParameterByName(DestPropertyNames[PropIdx]);
			if (DestParameter.GetType() != ERigVMParameterType::Input)
			{
				continue;
			}

			const uint8* SrcPtr = CallerProperty->ContainerPtrToValuePtr<uint8>(InSourceInstance);

			if (CastField<FBoolProperty>(CallerProperty) != nullptr && DestParameter.GetCPPType() == TEXT("bool"))
			{
				const bool Value = *(const bool*)SrcPtr;
				TargetControlRig->GetVM()->SetParameterValue<bool>(DestParameter, Value);
			}
			else if (CastField<FFloatProperty>(CallerProperty) != nullptr && DestParameter.GetCPPType() == TEXT("float"))
			{
				const float Value = *(const float*)SrcPtr;
				TargetControlRig->GetVM()->SetParameterValue<float>(DestParameter, Value);
			}
			else if (CastField<FIntProperty>(CallerProperty) != nullptr && DestParameter.GetCPPType() == TEXT("int32"))
			{
				const int32 Value = *(const int32*)SrcPtr;
				TargetControlRig->GetVM()->SetParameterValue<int32>(DestParameter, Value);
			}
			else if (CastField<FNameProperty>(CallerProperty) != nullptr && DestParameter.GetCPPType() == TEXT("FName"))
			{
				const FName Value = *(const FName*)SrcPtr;
				TargetControlRig->GetVM()->SetParameterValue<FName>(DestParameter, Value);
			}
			else if (FStructProperty* StructProperty = CastField<FStructProperty>(CallerProperty))
			{
				if (StructProperty->Struct == DestParameter.GetScriptStruct())
				{
					uint8* DestPtr = TargetControlRig->GetVM()->WorkMemory.GetData(DestParameter.GetRegisterIndex());
					StructProperty->Struct->CopyScriptStruct(DestPtr, SrcPtr, 1);
				}
			}
		}
	}
}

#if WITH_EDITOR
void FAnimNode_ControlRig::OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap)
{
	if (ControlRig)
	{
		UObject* const* NewFound = OldToNewInstanceMap.Find(ControlRig);

		if (NewFound)
		{
			// recache the properties
			bReinitializeProperties = true;
		}
	}
}
#endif	// #if WITH_EDITOR