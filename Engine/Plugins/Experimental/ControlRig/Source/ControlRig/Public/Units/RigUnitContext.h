// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Hierarchy.h"
#include "CurveContainer.h"
#include "ControlRigLog.h"
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
#if WITH_EDITOR
		, Log(nullptr)
#endif
	{

	}

	/** The draw interface for the units to use */
	FControlRigDrawInterface* DrawInterface;

	/** The current delta time */
	float DeltaTime;

	/** Current execution context */
	EControlRigState State;

	/** The current hierarchy being executed */
	FRigHierarchyRef HierarchyReference;

	/** The current curve being executed */
	FRigCurveContainerRef CurveReference;

#if WITH_EDITOR
	/** A handle to the compiler log */
	FControlRigLog* Log;
#endif
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
