// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class INiagaraMergeManager
{
public:
	enum class EMergeEmitterResult
	{
		SucceededNoDifferences,
		SucceededDifferencesApplied,
		FailedToDiff,
		FailedToMerge,
		None
	};

	struct FMergeEmitterResults
	{
		FMergeEmitterResults()
			: MergeResult(EMergeEmitterResult::None)
			, bModifiedGraph(false)
			, MergedInstance(nullptr)
		{
		}

		EMergeEmitterResult MergeResult;
		TArray<FText> ErrorMessages;
		bool bModifiedGraph;
		UNiagaraEmitter* MergedInstance;

		FString GetErrorMessagesString() const
		{
			TArray<FString> ErrorMessageStrings;
			for (FText ErrorMessage : ErrorMessages)
			{
				ErrorMessageStrings.Add(ErrorMessage.ToString());
			}
			return FString::Join(ErrorMessageStrings, TEXT("\n"));
		}
	};

	virtual FMergeEmitterResults MergeEmitter(UNiagaraEmitter& Parent, UNiagaraEmitter* ParentAtLastMerge, UNiagaraEmitter& Instance) const = 0;

	virtual void DiffEditableProperties(const void* BaseDataAddress, const void* OtherDataAddress, UStruct& Struct, TArray<FProperty*>& OutDifferentProperties) const = 0;

	virtual void CopyPropertiesToBase(void* BaseDataAddress, const void* OtherDataAddress, TArray<FProperty*> PropertiesToCopy) const = 0;

	virtual ~INiagaraMergeManager() { }
};