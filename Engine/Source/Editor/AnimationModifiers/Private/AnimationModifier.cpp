// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationModifier.h"
#include "AssetViewUtils.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "ModifierOutputFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor/Transactor.h"
#include "UObject/UObjectIterator.h"

#include "UObject/ReleaseObjectVersion.h"
#include "Misc/MessageDialog.h"
#include "Editor/Transactor.h"
#include "UObject/UObjectIterator.h"
#include "UObject/AnimObjectVersion.h"

#define LOCTEXT_NAMESPACE "AnimationModifier"

UAnimationModifier::UAnimationModifier()
	: PreviouslyAppliedModifier(nullptr)
{
}

void UAnimationModifier::ApplyToAnimationSequence(class UAnimSequence* InAnimationSequence)
{
	FEditorScriptExecutionGuard ScriptGuard;

	checkf(InAnimationSequence, TEXT("Invalid Animation Sequence supplied"));
	CurrentAnimSequence = InAnimationSequence;
	CurrentSkeleton = InAnimationSequence->GetSkeleton();

	// Filter to check for warnings / errors thrown from animation blueprint library (rudimentary approach for now)
	FCategoryLogOutputFilter OutputLog;
	OutputLog.SetAutoEmitLineTerminator(true);
	OutputLog.AddCategoryName("LogAnimationBlueprintLibrary");

	GLog->AddOutputDevice(&OutputLog);
		
	// Transact the modifier to prevent instance variables/data to change during applying
	FTransaction ModifierTransaction;
	ModifierTransaction.SaveObject(this);

	FTransaction AnimationDataTransaction;
	AnimationDataTransaction.SaveObject(CurrentAnimSequence);
	AnimationDataTransaction.SaveObject(CurrentSkeleton);

	/** In case this modifier has been previously applied, revert it using the serialised out version at the time */	
	if (PreviouslyAppliedModifier)
	{
		PreviouslyAppliedModifier->Modify();
		PreviouslyAppliedModifier->OnRevert(CurrentAnimSequence);
	}

	UAnimDataController* Controller = CurrentAnimSequence->GetController();

	{
		UAnimDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("ApplyModifierBracket", "Applying Animation Modifier"));
		/** Reverting and applying, populates the log with possible warnings and or errors to notify the user about */
		OnApply(CurrentAnimSequence);
	}

	// Apply transaction
	ModifierTransaction.BeginOperation();
	ModifierTransaction.Apply();
	ModifierTransaction.EndOperation();

	GLog->RemoveOutputDevice(&OutputLog);

	// Check if warnings or errors have occurred and show dialog to user to inform her about this
	const bool bWarningsOrErrors = OutputLog.ContainsWarnings() || OutputLog.ContainsErrors();
	bool bShouldRevert = false;
	if (bWarningsOrErrors)
	{
		static const FText WarningMessageFormat = FText::FromString("Modifier has generated warnings during a test run:\n\n{0}\nAre you sure you want to Apply it?");
		static const FText ErrorMessageFormat = FText::FromString("Modifier has generated errors (and warnings) during a test run:\n\n{0}\nResolve the Errors before trying to Apply!");

		EAppMsgType::Type MessageType = OutputLog.ContainsErrors() ? EAppMsgType::Ok : EAppMsgType::YesNo;
		const FText& MessageFormat = OutputLog.ContainsErrors() ? ErrorMessageFormat : WarningMessageFormat;
		const FText MessageTitle = FText::FromString("Modifier has Generated Warnings/Errors");
		bShouldRevert = (FMessageDialog::Open(MessageType, FText::FormatOrdered(MessageFormat, FText::FromString(OutputLog)), &MessageTitle) != EAppReturnType::Yes);
	}

	// Revert changes if necessary, otherwise post edit and refresh animation data
	if (bShouldRevert)
	{
		AnimationDataTransaction.BeginOperation();
		AnimationDataTransaction.Apply();
		AnimationDataTransaction.EndOperation();
		CurrentAnimSequence->RefreshCacheData();
	}
	else
	{
		/** Mark the previous modifier pending kill, as it will be replaced with the current modifier state */
		if (PreviouslyAppliedModifier)
		{
			PreviouslyAppliedModifier->MarkPendingKill();
		}

		PreviouslyAppliedModifier = DuplicateObject(this, GetOuter());

		CurrentAnimSequence->PostEditChange();
		CurrentSkeleton->PostEditChange();
		CurrentAnimSequence->RefreshCacheData();

		UpdateStoredRevisions();
	}
		
	// Finished
	CurrentAnimSequence = nullptr;
	CurrentSkeleton = nullptr;
}

void UAnimationModifier::UpdateCompressedAnimationData()
{
	if (CurrentAnimSequence->DoesNeedRecompress())
	{
		CurrentAnimSequence->RequestSyncAnimRecompression(false);
	}
}

void UAnimationModifier::RevertFromAnimationSequence(class UAnimSequence* InAnimationSequence)
{
	FEditorScriptExecutionGuard ScriptGuard;

	/** Can only revert if previously applied, which means there should be a previous modifier */
	if (PreviouslyAppliedModifier)
	{
		checkf(InAnimationSequence, TEXT("Invalid Animation Sequence supplied"));
		CurrentAnimSequence = InAnimationSequence;
		CurrentSkeleton = InAnimationSequence->GetSkeleton();

		// Transact the modifier to prevent instance variables/data to change during reverting
		FTransaction Transaction;
		Transaction.SaveObject(this);

		PreviouslyAppliedModifier->Modify();

		UAnimDataController* Controller = CurrentAnimSequence->GetController();

		{
			UAnimDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("RevertModifierBracket", "Reverting Animation Modifier"));
			PreviouslyAppliedModifier->OnRevert(CurrentAnimSequence);
		}

		// Apply transaction
		Transaction.BeginOperation();
		Transaction.Apply();
		Transaction.EndOperation();

	    CurrentAnimSequence->PostEditChange();
	    CurrentSkeleton->PostEditChange();
	    CurrentAnimSequence->RefreshCacheData();

		ResetStoredRevisions();

		// Finished
		CurrentAnimSequence = nullptr;
		CurrentSkeleton = nullptr;

		PreviouslyAppliedModifier->MarkPendingKill();
		PreviouslyAppliedModifier = nullptr;
	}
}

bool UAnimationModifier::IsLatestRevisionApplied() const
{
	return (AppliedGuid == RevisionGuid);
}

void UAnimationModifier::PostInitProperties()
{
	Super::PostInitProperties();
	UpdateNativeRevisionGuid();
}

void UAnimationModifier::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	/** Back-wards compatibility, assume the current modifier as previously applied */
	if (Ar.CustomVer(FReleaseObjectVersion::GUID) < FReleaseObjectVersion::SerializeAnimModifierState)
	{
		PreviouslyAppliedModifier = DuplicateObject(this, GetOuter());
	}
}

void UAnimationModifier::PostLoad()
{
	Super::PostLoad();

	UClass* Class = GetClass();
	UObject* DefaultObject = Class->GetDefaultObject();

	// CDO, set GUID if invalid
	if(DefaultObject == this)
	{
		// Ensure we always have a valid guid
		if (!RevisionGuid.IsValid())
		{
			UpdateRevisionGuid(GetClass());
			MarkPackageDirty();
		}
	}
	// Non CDO, update revision GUID
	else if(UAnimationModifier* TypedDefaultObject = Cast<UAnimationModifier>(DefaultObject))
	{
		RevisionGuid = TypedDefaultObject->RevisionGuid;
	}
}

const USkeleton* UAnimationModifier::GetSkeleton()
{
	return CurrentSkeleton;
}

void UAnimationModifier::UpdateRevisionGuid(UClass* ModifierClass)
{
	if (ModifierClass)
	{
		RevisionGuid = FGuid::NewGuid();

		// Apply to any currently loaded instances of this class
		for (TObjectIterator<UAnimationModifier> It; It; ++It)
		{
			if (*It != this && It->GetClass() == ModifierClass)
			{
				It->SetInstanceRevisionGuid(RevisionGuid);
			}
		}
	}
}

void UAnimationModifier::UpdateNativeRevisionGuid()
{
	UClass* Class = GetClass();
	// Check if this is the class default object
	if (this == GetDefault<UAnimationModifier>(Class))
	{
		// If so check whether or not the config stored revision matches the natively defined one
		if (StoredNativeRevision != GetNativeClassRevision())
		{
			// If not update the blueprint revision GUID
			UpdateRevisionGuid(Class);
			StoredNativeRevision = GetNativeClassRevision();

			MarkPackageDirty();

			// Save the new native revision to config files
			SaveConfig();
			UpdateDefaultConfigFile();
		}
	}
}

void UAnimationModifier::ApplyToAll(TSubclassOf<UAnimationModifier> ModifierSubClass)
{
	if (UClass* ModifierClass = ModifierSubClass.Get())
	{
		// Make sure all packages (in this case UAnimSequences) are loaded to ensure the TObjectIterator has any instances to iterate over
		LoadModifierReferencers(ModifierSubClass);
		
		const FScopedTransaction Transaction(LOCTEXT("UndoAction_ApplyModifiers", "Applying Animation Modifier to Animation Sequence(s)"));		
		for (TObjectIterator<UAnimationModifier> It; It; ++It)
		{
			if (*It && It->GetClass() == ModifierClass)
			{
				// Go through outer chain to find AnimSequence
				UObject* Outer = It->GetOuter();
				while(Outer && !Outer->IsA<UAnimSequence>())
				{
					Outer = Outer->GetOuter();
				}

				if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(Outer))
				{
					AnimSequence->Modify();
					It->ApplyToAnimationSequence(AnimSequence);
				}			
			}
		}
	}	
}

void UAnimationModifier::LoadModifierReferencers(TSubclassOf<UAnimationModifier> ModifierSubClass)
{
	if (UClass* ModifierClass = ModifierSubClass.Get())
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FName> PackageDependencies;
		AssetRegistryModule.GetRegistry().GetReferencers(ModifierClass->GetPackage()->GetFName(), PackageDependencies);

		TArray<FString> PackageNames;
		Algo::Transform(PackageDependencies, PackageNames, [](FName Name) { return Name.ToString(); });
		TArray<UPackage*> Packages = AssetViewUtils::LoadPackages(PackageNames);
	}
}

int32 UAnimationModifier::GetNativeClassRevision() const
{
	// Overriden in derrived classes to perform native revisioning
	return 0;
}

const UAnimSequence* UAnimationModifier::GetAnimationSequence()
{
	return CurrentAnimSequence;
}

void UAnimationModifier::UpdateStoredRevisions()
{
	AppliedGuid = RevisionGuid;
}

void UAnimationModifier::ResetStoredRevisions()
{
	AppliedGuid.Invalidate();
}

void UAnimationModifier::SetInstanceRevisionGuid(FGuid Guid)
{
	RevisionGuid = Guid;
}

#undef LOCTEXT_NAMESPACE
