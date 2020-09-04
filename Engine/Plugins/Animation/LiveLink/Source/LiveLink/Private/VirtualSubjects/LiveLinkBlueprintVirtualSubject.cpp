// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualSubjects/LiveLinkBlueprintVirtualSubject.h"

void ULiveLinkBlueprintVirtualSubject::Initialize(FLiveLinkSubjectKey InSubjectKey, TSubclassOf<ULiveLinkRole> InRole, ILiveLinkClient* InLiveLinkClient)
{
	Super::Initialize(SubjectKey, Role, LiveLinkClient);

	CachedStaticData.InitializeWith(GetRoleStaticStruct(), nullptr);
	FrameSnapshot.StaticData.InitializeWith(GetRoleStaticStruct(), nullptr);
	FrameSnapshot.FrameData.InitializeWith(GetRoleFrameStruct(), nullptr);

	{
		FEditorScriptExecutionGuard ScriptGuard;

		OnInitialize();
	}
}

void ULiveLinkBlueprintVirtualSubject::Update()
{
	Super::Update();

	{
		FEditorScriptExecutionGuard ScriptGuard;

		OnUpdate();
	}

	if (FrameSnapshot.FrameData.IsValid() && !FrameSnapshot.StaticData.IsValid() && CachedStaticData.IsValid())
	{
		FrameSnapshot.StaticData.InitializeWith(CachedStaticData);
	}
}

void ULiveLinkBlueprintVirtualSubject::UpdateVirtualSubjectStaticData(const FLiveLinkBaseStaticData* InStaticData)
{
	CachedStaticData.InitializeWith(GetRoleStaticStruct(), InStaticData);
	FrameSnapshot.StaticData.InitializeWith(GetRoleStaticStruct(), InStaticData);
	
	// Invalidate any existing Frame Data
	FrameSnapshot.FrameData.Reset();
}

void ULiveLinkBlueprintVirtualSubject::UpdateVirtualSubjectFrameData(const FLiveLinkBaseFrameData* InFrameData)
{
	FrameSnapshot.FrameData.InitializeWith(GetRoleFrameStruct(), InFrameData);
}

bool ULiveLinkBlueprintVirtualSubject::UpdateVirtualSubjectStaticData_Internal(const FLiveLinkBaseStaticData& InStruct)
{
	// We should never hit this!  stubs to avoid NoExport on the class.
	check(0);
	return false;
}

bool ULiveLinkBlueprintVirtualSubject::UpdateVirtualSubjectFrameData_Internal(const FLiveLinkBaseFrameData& InStruct)
{
	// We should never hit this!  stubs to avoid NoExport on the class.
	check(0);
	return false;
}

UScriptStruct* ULiveLinkBlueprintVirtualSubject::GetRoleStaticStruct()
{
	check(Role);
	return Role->GetDefaultObject<ULiveLinkRole>()->GetStaticDataStruct();
}

UScriptStruct* ULiveLinkBlueprintVirtualSubject::GetRoleFrameStruct()
{
	check(Role);
	return Role->GetDefaultObject<ULiveLinkRole>()->GetFrameDataStruct();
}

DEFINE_FUNCTION(ULiveLinkBlueprintVirtualSubject::execUpdateVirtualSubjectStaticData_Internal)
{
	Stack.StepCompiledIn<FStructProperty>(NULL);
	void* PropertyAddress = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	bool bSuccess = false;

	if (StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FLiveLinkBaseStaticData::StaticStruct()))
	{
		FLiveLinkBaseStaticData* StaticData = reinterpret_cast<FLiveLinkBaseStaticData*>(PropertyAddress);

		P_THIS->UpdateVirtualSubjectStaticData(StaticData);

		bSuccess = true;
	}

	P_NATIVE_END;

	*(bool*)RESULT_PARAM = true;
}

DEFINE_FUNCTION(ULiveLinkBlueprintVirtualSubject::execUpdateVirtualSubjectFrameData_Internal)
{
	Stack.StepCompiledIn<FStructProperty>(NULL);
	void* PropertyAddress = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	bool bSuccess = false;

	if (StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FLiveLinkBaseFrameData::StaticStruct()))
	{
		FLiveLinkBaseFrameData* FrameData = reinterpret_cast<FLiveLinkBaseFrameData*>(PropertyAddress);

		P_THIS->UpdateVirtualSubjectFrameData(FrameData);

		bSuccess = true;
	}

	P_NATIVE_END;

	*(bool*)RESULT_PARAM = true;
}