// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReplaceNodeReferencesHelper.h"
#include "ImaginaryBlueprintData.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "FReplaceNodeReferencesHelper"

FReplaceNodeReferencesHelper::FReplaceNodeReferencesHelper(const FMemberReference& Source, const FMemberReference& Replacement, UBlueprint* InBlueprint)
	: SourceReference(Source)
	, ReplacementReference(Replacement)
	, Blueprint(InBlueprint)
{
}

FReplaceNodeReferencesHelper::FReplaceNodeReferencesHelper(FMemberReference&& Source, FMemberReference&& Replacement, UBlueprint* InBlueprint)
	: SourceReference(MoveTemp(Source))
	, ReplacementReference(MoveTemp(Replacement))
	, Blueprint(InBlueprint)
{
}

FReplaceNodeReferencesHelper::~FReplaceNodeReferencesHelper()
{
}

void FReplaceNodeReferencesHelper::BeginFindAndReplace(const FSimpleDelegate& InOnCompleted /*=FSimpleDelegate()*/)
{
	bCompleted = false;
	OnCompleted = InOnCompleted;
	FFindInBlueprintCachingOptions CachingOptions;
	CachingOptions.MinimiumVersionRequirement = EFiBVersion::FIB_VER_VARIABLE_REFERENCE;
	CachingOptions.OnFinished.BindRaw(this, &FReplaceNodeReferencesHelper::OnSubmitSearchQuery);
	FFindInBlueprintSearchManager::Get().CacheAllAssets(nullptr, CachingOptions);
	SlowTask = MakeUnique<FScopedSlowTask>(3.f, LOCTEXT("Caching", "Caching Blueprints..."));
	SlowTask->MakeDialog();
}

void FReplaceNodeReferencesHelper::ReplaceReferences(TArray<FImaginaryFiBDataSharedPtr>& InRawDataList)
{
	ReplaceReferences(ReplacementReference, Blueprint, InRawDataList);
}

void FReplaceNodeReferencesHelper::ReplaceReferences(FMemberReference& InReplacement, UBlueprint* InBlueprint, TArray<FImaginaryFiBDataSharedPtr>& InRawDataList)
{
	const FScopedTransaction Transaction(FText::Format(LOCTEXT("ReplaceRefs", "Replace References with {0}"), FText::FromName(InReplacement.GetMemberName())));
	TArray< UBlueprint* > BlueprintsModified;
	for (FImaginaryFiBDataSharedPtr ImaginaryData : InRawDataList)
	{
		UBlueprint* Blueprint = ImaginaryData->GetBlueprint();
		BlueprintsModified.AddUnique(Blueprint);
		UObject* Node = ImaginaryData->GetObject(Blueprint);
		UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
		if (ensure(VariableNode))
		{
			VariableNode->Modify();
			if (VariableNode->VariableReference.IsLocalScope() || VariableNode->VariableReference.IsSelfContext())
			{
				VariableNode->VariableReference = InReplacement;
			}
			else
			{
				VariableNode->VariableReference.SetFromField<FProperty>(InReplacement.ResolveMember<FProperty>(InBlueprint), Blueprint->GeneratedClass);
			}
			VariableNode->ReconstructNode();
		}
	}

	for (UBlueprint* ModifiedBlueprint : BlueprintsModified)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ModifiedBlueprint);
		FFindInBlueprintSearchManager::Get().AddOrUpdateBlueprintSearchMetadata(ModifiedBlueprint);
	}
}

const void FReplaceNodeReferencesHelper::SetTransaction(TSharedPtr<FScopedTransaction> InTransaction)
{
	Transaction = InTransaction;
}

bool FReplaceNodeReferencesHelper::IsTickable() const
{
	return SlowTask.IsValid();
}

void FReplaceNodeReferencesHelper::Tick(float DeltaSeconds)
{
	if (StreamSearch.IsValid())
	{
		UpdateSearchQuery();
	}
	else
	{
		SlowTask->CompletedWork = FFindInBlueprintSearchManager::Get().GetCacheProgress();
	}
}

TStatId FReplaceNodeReferencesHelper::GetStatId() const
{
	return TStatId();
}

void FReplaceNodeReferencesHelper::OnSubmitSearchQuery()
{
	SlowTask->FrameMessage = LOCTEXT("Searching", "Searching Blueprints...");
	FString SearchTerm = SourceReference.GetReferenceSearchString(SourceReference.GetMemberParentClass());

	FStreamSearchOptions SearchOptions;
	SearchOptions.ImaginaryDataFilter = ESearchQueryFilter::NodesFilter;
	SearchOptions.MinimiumVersionRequirement = EFiBVersion::FIB_VER_VARIABLE_REFERENCE;

	StreamSearch = MakeShared<FStreamSearch>(SearchTerm, SearchOptions);
}

void FReplaceNodeReferencesHelper::UpdateSearchQuery()
{
	if (!StreamSearch->IsComplete())
	{
		SlowTask->CompletedWork = 1.f + FFindInBlueprintSearchManager::Get().GetPercentComplete(StreamSearch.Get());
	}
	else
	{
		TArray<FImaginaryFiBDataSharedPtr> ImaginaryData;
		StreamSearch->GetFilteredImaginaryResults(ImaginaryData);
		ReplaceReferences(ImaginaryData);
		
		StreamSearch->EnsureCompletion();

		// End the SlowTask
		SlowTask.Reset();

		OnCompleted.ExecuteIfBound();
		bCompleted = true;
	}
}

#undef LOCTEXT_NAMESPACE
