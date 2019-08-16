// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"
#include "RigUnit.h"
#include "RigUnitContext.h"
#include "Hierarchy.h"
#include "CurveContainer.h"

class FControlRigUnitTestBase : public FAutomationTestBase
{
public:
	FControlRigUnitTestBase(const FString& InName, bool bIsComplex)
		: FAutomationTestBase(InName, bIsComplex)
		, HierarchyContainer(FRigHierarchyContainer())
		, HierarchyRef(FRigHierarchyRef())
		, Hierarchy(HierarchyContainer.BaseHierarchy)
		, CurveContainer(FRigCurveContainer())
		, CurveContainerRef(&CurveContainer)
	{
		HierarchyRef.Container = &HierarchyContainer;
		HierarchyRef.bUseBaseHierarchy = true;
		Context.HierarchyReference = HierarchyRef;
		ExecuteContext.HierarchyReference = HierarchyRef;
		ExecuteContext.CurveReference = CurveContainerRef;
	}

	FRigHierarchyContainer HierarchyContainer;
	FRigHierarchyRef HierarchyRef;
	FRigHierarchy& Hierarchy;
	FRigCurveContainer CurveContainer;
	FRigCurveContainerRef CurveContainerRef;
	FControlRigExecuteContext ExecuteContext;
	FRigUnitContext Context;
};

#define CONTROLRIG_RIGUNIT_STRINGIFY(Content) #Content
#define IMPLEMENT_RIGUNIT_AUTOMATION_TEST(TUnitStruct) \
	class TUnitStruct##Test : public FControlRigUnitTestBase \
	{ \
	public: \
		TUnitStruct##Test( const FString& InName ) \
		:FControlRigUnitTestBase( InName, false ) {\
		} \
		virtual uint32 GetTestFlags() const override { return EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter; } \
		virtual bool IsStressTest() const { return false; } \
		virtual uint32 GetRequiredDeviceNum() const override { return 1; } \
		virtual FString GetTestSourceFileName() const override { return __FILE__; } \
		virtual int32 GetTestSourceFileLine() const override { return __LINE__; } \
	protected: \
		virtual void GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const override \
		{ \
			OutBeautifiedNames.Add(TEXT(CONTROLRIG_RIGUNIT_STRINGIFY(ControlRig.Units.TUnitStruct))); \
			OutTestCommands.Add(FString()); \
		} \
		TUnitStruct Unit; \
		virtual bool RunTest(const FString& Parameters) override \
		{ \
			Hierarchy.Reset(); \
			CurveContainer.Reset(); \
			Unit = TUnitStruct(); \
			return RunControlRigUnitTest(Parameters); \
		} \
		virtual bool RunControlRigUnitTest(const FString& Parameters); \
		virtual FString GetBeautifiedTestName() const override { return TEXT(CONTROLRIG_RIGUNIT_STRINGIFY(ControlRig.Units.TUnitStruct)); } \
		void Init() \
		{ \
			Context.State = EControlRigState::Init; \
			Unit.Execute(Context); \
		} \
		void Execute() \
		{ \
			Context.State = EControlRigState::Update; \
			Unit.Execute(Context); \
		} \
		void InitAndExecute() { Init(); Execute(); } \
	}; \
	namespace\
	{\
		TUnitStruct##Test TUnitStruct##AutomationTestInstance(TEXT(CONTROLRIG_RIGUNIT_STRINGIFY(TUnitStruct##Test))); \
	} \
	bool TUnitStruct##Test::RunControlRigUnitTest(const FString& Parameters)