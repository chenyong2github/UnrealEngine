// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Rigs/RigCurveContainer.h"
#include "ControlRigLog.h"
#include "AnimationDataSource.h"
#include "Drawing/ControlRigDrawInterface.h"

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
		, DataSourceRegistry(nullptr)
		, DeltaTime(0.f)
		, State(EControlRigState::Invalid)
		, Hierarchy(nullptr)
#if WITH_EDITOR
		, Log(nullptr)
#endif
	{

	}

	/** The draw interface for the units to use */
	FControlRigDrawInterface* DrawInterface;

	/** The registry to access data source */
	const UAnimationDataSourceRegistry* DataSourceRegistry;

	/** The current delta time */
	float DeltaTime;

	/** Current execution context */
	EControlRigState State;

	/** The current hierarchy being executed */
	const FRigHierarchyContainer* Hierarchy;

#if WITH_EDITOR
	/** A handle to the compiler log */
	FControlRigLog* Log;
#endif

	const FRigBoneHierarchy* GetBones() const
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->BoneHierarchy;
		}
		return nullptr;
	}

	const FRigSpaceHierarchy* GetSpaces() const
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->SpaceHierarchy;
		}
		return nullptr;
	}

	const FRigControlHierarchy* GetControls() const
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->ControlHierarchy;
		}
		return nullptr;
	}

	const FRigCurveContainer* GetCurves() const
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
	T* RequestDataSource(const FName& InName) const
	{
		if (DataSourceRegistry == nullptr)
		{
			return nullptr;
		}
		return DataSourceRegistry->RequestSource<T>(InName);
	}
};

#if WITH_EDITOR
#define UE_CONTROLRIG_RIGUNIT_REPORT(Severity, Format, ...) \
if(Context.Log != nullptr) \
{ \
	Context.Log->Report(EMessageSeverity::Severity, RigUnitName, FString::Printf((Format), ##__VA_ARGS__)); \
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
