// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomationTestExcludelist.h"

static const FString FunctionalTestsPreFix = TEXT("Project.Functional Tests.");

void UAutomationTestExcludelist::OverrideConfigSection(FString& SectionName)
{
	SectionName = TEXT("AutomationTestExcludelist");
}

UAutomationTestExcludelist* UAutomationTestExcludelist::Get()
{
	return GetMutableDefault<UAutomationTestExcludelist>();
}

void UAutomationTestExcludelist::PostInitProperties()
{
	Super::PostInitProperties();

	for (auto& Entry : ExcludeTest)
	{
		if (Entry.IsEmpty())
		{
			Entry.FullTestName = GetFullTestName(Entry);
		}
	}
}

FString UAutomationTestExcludelist::GetFullTestName(const FAutomationTestExcludelistEntry& ExcludelistEntry)
{
	if (!ExcludelistEntry.IsEmpty())
	{
		return ExcludelistEntry.FullTestName;
	}

	FString Map = ExcludelistEntry.Map.ToString().TrimStartAndEnd();
	FString ListName = ExcludelistEntry.Test.ToString().TrimStartAndEnd();

	if (Map.StartsWith(TEXT("/")) && !ListName.StartsWith(FunctionalTestsPreFix))
	{
		ListName = FunctionalTestsPreFix + Map + TEXT(".") + ListName;
	}

	return ListName.ToLower();
}

void UAutomationTestExcludelist::AddToExcludeTest(const FString& TestName, const FAutomationTestExcludelistEntry& ExcludelistEntry)
{
	auto NewEntry = FAutomationTestExcludelistEntry(ExcludelistEntry);
	NewEntry.Test = *TestName;
	if (!NewEntry.Map.IsNone())
	{
		NewEntry.Map = TEXT("");
	}

	NewEntry.FullTestName = GetFullTestName(NewEntry);

	ExcludeTest.Add(NewEntry);
}

void UAutomationTestExcludelist::RemoveFromExcludeTest(const FString& TestName)
{
	if (TestName.IsEmpty())
		return;

	const FString NameToCompare = TestName.ToLower();

	for (int i = 0; i < ExcludeTest.Num(); ++i)
	{
		if (ExcludeTest[i].FullTestName == NameToCompare)
		{
			ExcludeTest.RemoveAt(i);
			return;
		}
	}
}

bool UAutomationTestExcludelist::IsTestExcluded(const FString& TestName, const FString& RHI, FName* OutReason, bool* OutWarn)
{
	if (auto Entry = GetExcludeTestEntry(TestName))
	{
		if (!Entry->ShouldExcludeForRHI(RHI))
		{
			return false;
		}

		if (OutReason != nullptr)
		{
			*OutReason = Entry->Reason;
		}

		if (OutWarn != nullptr)
		{
			*OutWarn = Entry->Warn;
		}

		return true;
	}

	return false;
}

FAutomationTestExcludelistEntry* UAutomationTestExcludelist::GetExcludeTestEntry(const FString& TestName)
{
	if (TestName.IsEmpty())
		return nullptr;

	const FString NameToCompare = TestName.ToLower();

	for (auto& Entry : ExcludeTest)
	{
		if(NameToCompare.StartsWith(Entry.FullTestName))
		{
			if (NameToCompare.Len() == Entry.FullTestName.Len() || NameToCompare.Mid(Entry.FullTestName.Len(), 1) == TEXT("."))
			{
				return &Entry;
			}
		}
	}

	return nullptr;
}
