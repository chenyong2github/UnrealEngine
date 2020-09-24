// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Rigs/RigCurveContainer.h"
#include "ControlRigLog.h"
#include "AnimationDataSource.h"
#include "Drawing/ControlRigDrawInterface.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

/** Current state of rig
*	What  state Control Rig currently is
*/
UENUM()
enum class EControlRigState : uint8
{
	Init,
	Update,
	Invalid,
};

/** Execution context that rig units use */
struct FRigUnitContext
{
	/** default constructor */
	FRigUnitContext()
		: DrawInterface(nullptr)
		, DrawContainer(nullptr)
		, DataSourceRegistry(nullptr)
		, DeltaTime(0.f)
		, AbsoluteTime(0.f)
		, State(EControlRigState::Invalid)
		, Hierarchy(nullptr)
		, bDuringInteraction(false)
		, ToWorldSpaceTransform(FTransform::Identity)
		, OwningComponent(nullptr)
		, OwningActor(nullptr)
		, World(nullptr)
#if WITH_EDITOR
		, Log(nullptr)
#endif
	{
	}

	/** The draw interface for the units to use */
	FControlRigDrawInterface* DrawInterface;

	/** The auxiliary draw container for the units to use */
	FControlRigDrawContainer* DrawContainer;

	/** The registry to access data source */
	const UAnimationDataSourceRegistry* DataSourceRegistry;

	/** The current delta time */
	float DeltaTime;

	/** The current delta time */
	float AbsoluteTime;

	/** The current frames per second */
	float FramesPerSecond;

	/** Current execution context */
	EControlRigState State;

	/** The current hierarchy being executed */
	const FRigHierarchyContainer* Hierarchy;

	/** True if the rig is executing during an interaction */
	bool bDuringInteraction;

	/** The current transform going from rig (global) space to world space */
	FTransform ToWorldSpaceTransform;

	/** The current component this rig is owned by */
	USceneComponent* OwningComponent;

	/** The current actor this rig is owned by */
	const AActor* OwningActor;

	/** The world this rig is running in */
	const UWorld* World;

#if WITH_EDITOR
	/** A handle to the compiler log */
	FControlRigLog* Log;
#endif

	FORCEINLINE const FRigBoneHierarchy* GetBones() const
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->BoneHierarchy;
		}
		return nullptr;
	}

	FORCEINLINE const FRigSpaceHierarchy* GetSpaces() const
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->SpaceHierarchy;
		}
		return nullptr;
	}

	FORCEINLINE const FRigControlHierarchy* GetControls() const
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->ControlHierarchy;
		}
		return nullptr;
	}

	FORCEINLINE const FRigCurveContainer* GetCurves() const
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->CurveContainer;
		}
		return nullptr;
	}

	/**
	 * Returns a given data source and cast it to the expected class.
	 *
	 * @param InName The name of the data source to look up.
	 * @return The requested data source
	 */
	template<class T>
	FORCEINLINE T* RequestDataSource(const FName& InName) const
	{
		if (DataSourceRegistry == nullptr)
		{
			return nullptr;
		}
		return DataSourceRegistry->RequestSource<T>(InName);
	}

	/**
	 * Converts a transform from rig (global) space to world space
	 */
	FORCEINLINE FTransform ToWorldSpace(const FTransform& InTransform) const
	{
		return InTransform * ToWorldSpaceTransform;
	}

	/**
	 * Converts a transform from world space to rig (global) space
	 */
	FORCEINLINE FTransform ToRigSpace(const FTransform& InTransform) const
	{
		return InTransform.GetRelativeTransform(ToWorldSpaceTransform);
	}

	/**
	 * Converts a location from rig (global) space to world space
	 */
	FORCEINLINE FVector ToWorldSpace(const FVector& InLocation) const
	{
		return ToWorldSpaceTransform.TransformPosition(InLocation);
	}

	/**
	 * Converts a location from world space to rig (global) space
	 */
	FORCEINLINE FVector ToRigSpace(const FVector& InLocation) const
	{
		return ToWorldSpaceTransform.InverseTransformPosition(InLocation);
	}

	/**
	 * Converts a rotation from rig (global) space to world space
	 */
	FORCEINLINE FQuat ToWorldSpace(const FQuat& InRotation) const
	{
		return ToWorldSpaceTransform.TransformRotation(InRotation);
	}

	/**
	 * Converts a rotation from world space to rig (global) space
	 */
	FORCEINLINE FQuat ToRigSpace(const FQuat& InRotation) const
	{
		return ToWorldSpaceTransform.InverseTransformRotation(InRotation);
	}
};

#if WITH_EDITOR
#define UE_CONTROLRIG_RIGUNIT_REPORT(Severity, Format, ...) \
if(Context.Log != nullptr) \
{ \
	Context.Log->Report(EMessageSeverity::Severity, RigVMExecuteContext.FunctionName, RigVMExecuteContext.InstructionIndex, FString::Printf((Format), ##__VA_ARGS__)); \
}
#define UE_CONTROLRIG_RIGUNIT_LOG_MESSAGE(Format, ...) UE_CONTROLRIG_RIGUNIT_REPORT(Info, (Format), ##__VA_ARGS__)
#define UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(Format, ...) UE_CONTROLRIG_RIGUNIT_REPORT(Warning, (Format), ##__VA_ARGS__)
#define UE_CONTROLRIG_RIGUNIT_REPORT_ERROR(Format, ...) UE_CONTROLRIG_RIGUNIT_REPORT(Error, (Format), ##__VA_ARGS__)
#else
#define UE_CONTROLRIG_RIGUNIT_REPORT(Severity, Format, ...)
#define UE_CONTROLRIG_RIGUNIT_LOG_MESSAGE(Format, ...)
#define UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(Format, ...)
#define UE_CONTROLRIG_RIGUNIT_REPORT_ERROR(Format, ...)
#endif
