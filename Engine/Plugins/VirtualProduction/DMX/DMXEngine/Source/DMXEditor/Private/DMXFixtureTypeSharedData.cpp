// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXFixtureTypeSharedData.h"

#include "DMXEditorUtils.h"
#include "DMXRuntimeUtils.h"
#include "Library/DMXEntityFixtureType.h"

#include "ScopedTransaction.h"


#define LOCTEXT_NAMESPACE "DMXFixtureTypeSharedData"

namespace
{
	namespace FDMXFixtureTypeSharedDataDetails
	{
		// DEPRECATED 5.0, only here in to keep support for deprecated members of DMXFixtureTypeSharedData
		struct FDMXScopedModeEditChangeChainProperty_DEPRECATED
		{
			FDMXScopedModeEditChangeChainProperty_DEPRECATED(UDMXEntityFixtureType* InFixtureType)
			{
				check(InFixtureType);

				FixtureType = InFixtureType;

				FProperty* ModesProperty = UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes));
				check(ModesProperty);

				FEditPropertyChain PropertyChain;
				PropertyChain.AddHead(ModesProperty);
				PropertyChain.SetActivePropertyNode(ModesProperty);

				FixtureType->PreEditChange(PropertyChain);
				FixtureType->Modify();
			}

			~FDMXScopedModeEditChangeChainProperty_DEPRECATED()
			{
				if (FixtureType.IsValid())
				{
					FProperty* ModesProperty = UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes));
					check(ModesProperty);

					FEditPropertyChain PropertyChain;
					PropertyChain.AddHead(ModesProperty);
					PropertyChain.SetActivePropertyNode(ModesProperty);

					TMap<FString, int32> IndexMap;
					TArray<UObject*> ObjectsBeingEdited = { FixtureType.Get() };

					FPropertyChangedEvent PropertyChangedEvent(ModesProperty->GetOwnerProperty(), EPropertyChangeType::ArrayAdd, MakeArrayView(ObjectsBeingEdited));
					FPropertyChangedChainEvent PropertyChangedChainEvent(PropertyChain, PropertyChangedEvent);

					IndexMap.Add(FString(ModesProperty->GetName()), FixtureType->Modes.Num() - 1);

					TArray<TMap<FString, int32>> IndexPerObject;
					IndexPerObject.Add(IndexMap);

					PropertyChangedChainEvent.SetArrayIndexPerObject(IndexPerObject);
					PropertyChangedChainEvent.ObjectIteratorIndex = 0;

					FixtureType->PostEditChangeChainProperty(PropertyChangedChainEvent);
				}
			}

		private:
			TWeakObjectPtr<UDMXEntityFixtureType> FixtureType;
		};
	}
}

void FDMXFixtureTypeSharedData::SelectFixtureTypes(const TArray<TWeakObjectPtr<UDMXEntityFixtureType>>& InFixtureTypes)
{
	if (SelectedFixtureTypes != InFixtureTypes)
	{
		SelectedFixtureTypes = InFixtureTypes;

		// Selected Modes and Functions turn invalid
		SelectedModeIndices.Reset();
		SelectedFunctionIndices.Reset();
		bFixtureMatrixSelected = false;

		OnFixtureTypesSelected.Broadcast();
	}
}

void FDMXFixtureTypeSharedData::SelectModes(const TArray<int32>& InModeIndices)
{
	if (SelectedModeIndices != InModeIndices)
	{
		SelectedModeIndices = InModeIndices;

		// Selected Functions turn invalid
		SelectedFunctionIndices.Reset();
		bFixtureMatrixSelected = false;

		OnModesSelected.Broadcast();
	}
}

void FDMXFixtureTypeSharedData::SetFunctionAndMatrixSelection(const TArray<int32>& InFunctionIndices, bool bMatrixSelected)
{
	if (SelectedFunctionIndices != InFunctionIndices ||
		bFixtureMatrixSelected != bMatrixSelected)
	{
		SelectedFunctionIndices = InFunctionIndices;
		bFixtureMatrixSelected = bMatrixSelected;

		OnFunctionsSelected.Broadcast();
		OnMatrixSelectionChanged.Broadcast();
	}
}

bool FDMXFixtureTypeSharedData::CanCopyModesToClipboard() const
{
	if (SelectedModeIndices.Num() == 0)
	{
		return false;
	}
	else if (SelectedFixtureTypes.Num() == 1)
	{
		if (UDMXEntityFixtureType* FixtureType = SelectedFixtureTypes[0].Get())
		{
			for (int32 ModeIndex : SelectedModeIndices)
			{
				if (!FixtureType->Modes.IsValidIndex(SelectedModeIndices[0]))
				{
					return false;
				}
			}
		}
	}

	return true;
}

void FDMXFixtureTypeSharedData::CopyModesToClipboard()
{
	if (ensureMsgf(CanCopyModesToClipboard(), TEXT("Cannot copy Modes to clipboard. Please test first with FDMXFixtureTypeSharedData::CanCopyModesToClipboard.")))
	{
		ModesClipboard.Reset();

		for (TWeakObjectPtr<UDMXEntityFixtureType> WeakFixtureType : SelectedFixtureTypes)
		{
			if (UDMXEntityFixtureType* FixtureType = WeakFixtureType.Get())
			{
				for (const int32 ModeIndex : SelectedModeIndices)
				{
					const FDMXFixtureMode& Mode = FixtureType->Modes[ModeIndex];
					if (FixtureType->Modes.IsValidIndex(ModeIndex))
					{
						const TOptional<FString> CopyStr = FDMXRuntimeUtils::SerializeStructToString<FDMXFixtureMode>(Mode);
						if (CopyStr.IsSet())
						{
							ModesClipboard.Add(CopyStr.GetValue());
						}
					}
				}
			}
		}
	}
}

bool FDMXFixtureTypeSharedData::CanPasteModesFromClipboard() const
{
	if (SelectedFixtureTypes.Num() == 1 && ModesClipboard.Num() > 0)
	{
		if (UDMXEntityFixtureType* FixtureType = SelectedFixtureTypes[0].Get())
		{
			return true;
		}
	}

	return true;
}

void FDMXFixtureTypeSharedData::PasteModesFromClipboard(TArray<int32>& OutNewlyAddedModeIndices)
{
	if (ensureMsgf(CanPasteModesFromClipboard(), TEXT("Cannot paste Modes from clipboard. Please test first with FDMXFixtureTypeSharedData::CanPasteModesFromClipboard.")))
	{
		if (SelectedFixtureTypes.Num() == 1)
		{
			if (UDMXEntityFixtureType* FixtureType = SelectedFixtureTypes[0].Get())
			{
				const FText TransactionText = FText::Format(LOCTEXT("PasteModesTransaction", "Paste Fixture {0}|plural(one=Mode, other=Modes)"), GetSelectedModeIndices().Num());
				const FScopedTransaction PasteModesTransaction(TransactionText);

				FixtureType->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes)));

				int32 NumPastedElements = 0;
				for (int32 ClipboardIndex = 0; ClipboardIndex < ModesClipboard.Num(); ClipboardIndex++)
				{
					const FString& ClipboardString = ModesClipboard[ClipboardIndex];

					const int32 PasteOntoIndex = [FixtureType, &ClipboardIndex, this]()
					{
						if (SelectedModeIndices.IsValidIndex(ClipboardIndex))
						{
							return SelectedModeIndices[ClipboardIndex];
						}
						else if (SelectedModeIndices.Num() > 0)
						{
							return SelectedModeIndices.Last();
						}
						else
						{
							return FixtureType->Modes.Num() - 1;
						}
					}();

					TSharedPtr<FJsonObject> RootJsonObject;
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ClipboardString);
					FJsonSerializer::Deserialize(Reader, RootJsonObject);

					if (RootJsonObject.IsValid())
					{
						FDMXFixtureMode ModeFromClipboard;
						if (FJsonObjectConverter::JsonObjectToUStruct(RootJsonObject.ToSharedRef(), FDMXFixtureMode::StaticStruct(), &ModeFromClipboard, 0, 0))
						{
							int32 IndexOfNewlyAddedMode = PasteOntoIndex + 1 + NumPastedElements;
							if (FixtureType->Modes.IsValidIndex(IndexOfNewlyAddedMode))
							{
								IndexOfNewlyAddedMode = FixtureType->Modes.Insert(ModeFromClipboard, IndexOfNewlyAddedMode);
							}
							else
							{
								IndexOfNewlyAddedMode = FixtureType->Modes.Add(ModeFromClipboard);
							}
							OutNewlyAddedModeIndices.Add(IndexOfNewlyAddedMode);

							// Make a uinque mode name by setting it
							FString UnusedString;
							FixtureType->SetModeName(IndexOfNewlyAddedMode, ModeFromClipboard.ModeName, UnusedString);

							NumPastedElements++;
						}
					}
				}

				FixtureType->PostEditChange();
			}
		}
	}
}

bool FDMXFixtureTypeSharedData::CanCopyFunctionsToClipboard() const
{
	if (SelectedFunctionIndices.Num() == 0 || bFixtureMatrixSelected)
	{
		return false;
	}
	else if (SelectedFixtureTypes.Num() == 1 && SelectedModeIndices.Num() == 1)
	{
		if (UDMXEntityFixtureType* FixtureType = SelectedFixtureTypes[0].Get())
		{
			if(FixtureType->Modes.IsValidIndex(SelectedModeIndices[0]))
			{
				const FDMXFixtureMode& Mode = FixtureType->Modes[SelectedModeIndices[0]];

				for (const int32 FunctionIndex : SelectedFunctionIndices)
				{
					if (!Mode.Functions.IsValidIndex(FunctionIndex))
					{
						return false;
					}
				}
			}
		}
	}

	return true;
}

void FDMXFixtureTypeSharedData::CopyFunctionsToClipboard()
{
	if (ensureMsgf(CanCopyFunctionsToClipboard(), TEXT("Cannot copy Functions to clipboard. Please test first with FDMXFixtureTypeSharedData::CopyFunctionsToClipboard.")))
	{
		FunctionsClipboard.Reset();

		if (SelectedFixtureTypes.Num() == 1 && SelectedModeIndices.Num() == 1)
		{
			if (UDMXEntityFixtureType* FixtureType = SelectedFixtureTypes[0].Get())
			{
				if (FixtureType->Modes.IsValidIndex(SelectedModeIndices[0]))
				{
					const FDMXFixtureMode& Mode = FixtureType->Modes[SelectedModeIndices[0]];

					// Sort selected function indices by starting channel, so elements are copied in visual right order, not in order of how things were selected
					SelectedFunctionIndices.Sort([Mode](const int32 FunctionIndexA, const int32 FunctionIndexB)
						{
							if (Mode.Functions.IsValidIndex(FunctionIndexA) && Mode.Functions.IsValidIndex(FunctionIndexB))
							{
								return Mode.Functions[FunctionIndexA].Channel <= Mode.Functions[FunctionIndexB].Channel;
							}
							return false;
						});

					for (const int32 FunctionIndex : SelectedFunctionIndices)
					{
						if (Mode.Functions.IsValidIndex(FunctionIndex))
						{
							const TOptional<FString> CopyStr = FDMXRuntimeUtils::SerializeStructToString<FDMXFixtureFunction>(Mode.Functions[FunctionIndex]);
							if (CopyStr.IsSet())
							{
								FunctionsClipboard.Add(CopyStr.GetValue());
							}
						}
					}
				}
			}
		}
	}
}

bool FDMXFixtureTypeSharedData::CanPasteFunctionsFromClipboard() const
{
	if (SelectedFixtureTypes.Num() == 1 && SelectedModeIndices.Num() == 1 && ModesClipboard.Num() > 0)
	{
		if (UDMXEntityFixtureType* FixtureType = SelectedFixtureTypes[0].Get())
		{
			if (FixtureType->Modes.IsValidIndex(SelectedModeIndices[0]))
			{
				return true;
			}
		}
	}

	return true;
}

void FDMXFixtureTypeSharedData::PasteFunctionsFromClipboard(TArray<int32>& OutNewlyAddedFunctionIndices)
{
	if (ensureMsgf(CanPasteFunctionsFromClipboard(), TEXT("Cannot paste Functions from clipboard. Please test first with FDMXFixtureTypeSharedData::CanPasteFunctionsFromClipboard.")))
	{
		if (SelectedFixtureTypes.Num() == 1 && SelectedModeIndices.Num() == 1)
		{
			if (UDMXEntityFixtureType* FixtureType = SelectedFixtureTypes[0].Get())
			{
				const int32 SelectedModeIndex = SelectedModeIndices[0];
				if (FixtureType->Modes.IsValidIndex(SelectedModeIndex))
				{
					FDMXFixtureMode& Mode = FixtureType->Modes[SelectedModeIndex];

					// Sort selected function indices by starting channel, so elements are pasted in visual right order, not in order of how things were selected
					SelectedFunctionIndices.Sort([Mode](const int32 FunctionIndexA, const int32 FunctionIndexB)
						{
							if (Mode.Functions.IsValidIndex(FunctionIndexA) && Mode.Functions.IsValidIndex(FunctionIndexB))
							{
								return Mode.Functions[FunctionIndexA].Channel <= Mode.Functions[FunctionIndexB].Channel;
							}
							return false;
						});

					const FText TransactionText = FText::Format(LOCTEXT("PasteModesTransaction", "Paste Fixture {0}|plural(one=Mode, other=Modes)"), GetSelectedModeIndices().Num());
					const FScopedTransaction PasteModesTransaction(TransactionText);

					FixtureType->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions)));

					int32 NumPastedElements = 0;
					for (int32 ClipboardIndex = 0; ClipboardIndex < FunctionsClipboard.Num(); ClipboardIndex++)
					{
						TSharedPtr<FJsonObject> RootJsonObject;
						TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FunctionsClipboard[ClipboardIndex]);
						FJsonSerializer::Deserialize(Reader, RootJsonObject);

						if (RootJsonObject.IsValid())
						{
							FDMXFixtureFunction FunctionFromClipboard;
							if (FJsonObjectConverter::JsonObjectToUStruct(RootJsonObject.ToSharedRef(), FDMXFixtureFunction::StaticStruct(), &FunctionFromClipboard, 0, 0))
							{
								// Find an index to paste onto
								const int32 PasteOntoIndex = [Mode, ClipboardIndex, this]()
								{
									if (SelectedFunctionIndices.IsValidIndex(ClipboardIndex))
									{
										return SelectedFunctionIndices[ClipboardIndex];
									}
									else if (SelectedFunctionIndices.Num() > 0)
									{
										return SelectedFunctionIndices.Last();
									}
									else
									{
										// Paste at the end of the Functions array
										return Mode.Functions.Num() - 1;
									}
								}();

								FDMXFixtureFunction NewFunction = FunctionFromClipboard;

								// Insert the Function
								const int32 DesiredIndex = PasteOntoIndex + 1 + NumPastedElements;
								const int32 ResultingIndex = FixtureType->InsertFunction(SelectedModeIndex, DesiredIndex, NewFunction);
								OutNewlyAddedFunctionIndices.Add(ResultingIndex);

								NumPastedElements++;
							}
						}
					}

					if (NumPastedElements > 0)
					{
						FixtureType->UpdateChannelSpan(SelectedModeIndex);
					}

					FixtureType->PostEditChange();
				}
			}
		}
	}
}

void FDMXFixtureTypeSharedData::SelectFunctions(const TArray<int32>& InFunctionIndices)
{
	// DEPRECATED 5.0
	if (SelectedFunctionIndices != InFunctionIndices)
	{
		SelectedFunctionIndices = InFunctionIndices;
		OnFunctionsSelected.Broadcast();
	}
}

bool FDMXFixtureTypeSharedData::CanAddMode() const
{
	// DEPRECATED 5.0

	return SelectedFixtureTypes.Num() == 1;
}

void FDMXFixtureTypeSharedData::AddMode()
{
	// DEPRECATED 5.0

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const bool bCanAddMode = CanAddMode();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (ensureMsgf(bCanAddMode, TEXT("Call to FDMXFixtureTypeSharedData::AddMode when no mode can be added")))
	{
		for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : SelectedFixtureTypes)
		{
			if (!FixtureType.IsValid())
			{
				continue;
			}

			FDMXFixtureMode NewMode;

			// Make a unique name for the new mode
			TSet<FString> ModeNames;
			for (const FDMXFixtureMode& Mode : FixtureType->Modes)
			{
				ModeNames.Add(Mode.ModeName);
			}
			NewMode.ModeName = FDMXRuntimeUtils::GenerateUniqueNameFromExisting(ModeNames, LOCTEXT("DefaultModeName", "Mode").ToString());

			const FDMXFixtureTypeSharedDataDetails::FDMXScopedModeEditChangeChainProperty_DEPRECATED ScopedChange(FixtureType.Get());

			FixtureType->Modes.Add(NewMode);
		}
	}
}

void FDMXFixtureTypeSharedData::DuplicateModes(const TArray<int32>& ModeÎndiciesToDuplicate)
{
	// DEPRECATED 5.0

	for (const TWeakObjectPtr<UDMXEntityFixtureType>& WeakFixtureType : SelectedFixtureTypes)
	{ 
		if (UDMXEntityFixtureType* FixtureType = WeakFixtureType.Get())
		{
			for (int32 ModeIndex : ModeÎndiciesToDuplicate)
			{
				if (FixtureType->Modes.IsValidIndex(ModeIndex))
				{
					// Copy
					FDMXFixtureMode NewMode = FixtureType->Modes[ModeIndex];

					// Get a unique name
					TSet<FString> ModeNames;
					for (const FDMXFixtureMode& Mode : FixtureType->Modes)
					{
						ModeNames.Add(Mode.ModeName);
					}
					NewMode.ModeName = FDMXRuntimeUtils::GenerateUniqueNameFromExisting(ModeNames, NewMode.ModeName);

					const FDMXFixtureTypeSharedDataDetails::FDMXScopedModeEditChangeChainProperty_DEPRECATED ScopedChange(FixtureType);

					if (FixtureType->Modes.IsValidIndex(ModeIndex + 1))
					{
						FixtureType->Modes.Insert(NewMode, ModeIndex + 1);
					}
					else
					{
						FixtureType->Modes.Add(NewMode);
					}
				}
			}
		}
	}
}

void FDMXFixtureTypeSharedData::DeleteModes(const TArray<int32>& ModeIndicesToDelete)
{
	// DEPRECATED 5.0

	for (const TWeakObjectPtr<UDMXEntityFixtureType>& WeakFixtureType : SelectedFixtureTypes)
	{
		if (UDMXEntityFixtureType* FixtureType = WeakFixtureType.Get())
		{
			for (int32 ModeIndex : ModeIndicesToDelete)
			{
				if (FixtureType->Modes.IsValidIndex(ModeIndex))
				{
					// Transaction
					const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("DeleteModeTransaction", "Delete Fixture Type Mode"));

					const FDMXFixtureTypeSharedDataDetails::FDMXScopedModeEditChangeChainProperty_DEPRECATED ScopedChange(FixtureType);

					FixtureType->Modes.RemoveAt(ModeIndex);
				}
			}
		}
	}
}

void FDMXFixtureTypeSharedData::PasteClipboardToModes(const TArray<int32>& ModeIndices)
{	
	// DEPRECATED 5.0

	for (int32 PasteOntoElementIndex = 0; PasteOntoElementIndex < ModeIndices.Num(); PasteOntoElementIndex++)
	{
		const int32 PasteOntoIndex = ModeIndices[PasteOntoElementIndex];

		const FString& ClipboardString = [PasteOntoElementIndex, this]()
		{
			if (ModesClipboard.IsValidIndex(PasteOntoElementIndex))
			{
				return ModesClipboard[PasteOntoElementIndex];
			}
			else if (ModesClipboard.Num() > 0)
			{
				return ModesClipboard.Last();
			}

			return FString();
		}();

		TSharedPtr<FJsonObject> RootJsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ClipboardString);
		FJsonSerializer::Deserialize(Reader, RootJsonObject);

		if (RootJsonObject.IsValid())
		{
			FDMXFixtureMode ModeFromClipboard;
			if (FJsonObjectConverter::JsonObjectToUStruct(RootJsonObject.ToSharedRef(), FDMXFixtureMode::StaticStruct(), &ModeFromClipboard, 0, 0))
			{
				for (const TWeakObjectPtr<UDMXEntityFixtureType>& WeakFixtureType : SelectedFixtureTypes)
				{
					if (UDMXEntityFixtureType* FixtureType = WeakFixtureType.Get())
					{
						if (FixtureType->Modes.IsValidIndex(PasteOntoIndex))
						{
							FDMXFixtureMode& PasteOntoMode = FixtureType->Modes[PasteOntoIndex];

							FString OldModeName = PasteOntoMode.ModeName;

							FixtureType->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes)));
							const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("PasteModeTransaction", "Paste Fixture Type Mode"));
							FixtureType->Modify();

							PasteOntoMode = ModeFromClipboard;

							// Keep the original mode name
							PasteOntoMode.ModeName = OldModeName;

							FixtureType->PostEditChange();
						}
					}
				}
			}
		}
	}
}

bool FDMXFixtureTypeSharedData::CanAddFunction() const
{
	// DEPRECATED 5.0

	return SelectedModeIndices.Num() == 1;
}

void FDMXFixtureTypeSharedData::AddFunctionToSelectedMode()
{
	// DEPRECATED 5.0

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const bool bCanAddFunction = CanAddFunction();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (ensureMsgf(bCanAddFunction, TEXT("Call to FDMXFixtureTypeSharedData::AddFunctionToSelectedMode, when no function can be added")))
	{
		for (const TWeakObjectPtr<UDMXEntityFixtureType>& WeakFixtureType : SelectedFixtureTypes)
		{
			if (UDMXEntityFixtureType* FixtureType = WeakFixtureType.Get())
			{
				const FScopedTransaction AddFunctionTransaction = FScopedTransaction(LOCTEXT("AddFunctionTransaction", "Add Fixture Type Function"));

				FixtureType->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes)));
				FixtureType->Modify();

				int32 ModeIndex = SelectedModeIndices[0]; // Implicitly valid via CanAddFunctions tested before
				if (FixtureType->Modes.IsValidIndex(ModeIndex))
				{
					FDMXFixtureMode& Mode = FixtureType->Modes[ModeIndex];

					// Get a unique name
					TSet<FString> FunctionNames;
					for (const FDMXFixtureFunction& Function : Mode.Functions)
					{
						FunctionNames.Add(Function.FunctionName);
					}
					FDMXFixtureFunction NewFunction;
					NewFunction.FunctionName = FDMXRuntimeUtils::GenerateUniqueNameFromExisting(FunctionNames, LOCTEXT("DMXFixtureTypeSharedData.DefaultFunctionName", "Function").ToString());

					Mode.Functions.Add(NewFunction);
					FixtureType->UpdateChannelSpan(ModeIndex);
				}

				FDMXEditorUtils::AutoAssignedAddresses(FixtureType);

				FixtureType->PostEditChange();
			}
		}
	}
}

void FDMXFixtureTypeSharedData::DuplicateFunctions(const TArray<int32>& FunctionIndicesToDuplicate)
{
	// DEPRECATED 5.0

	for (const TWeakObjectPtr<UDMXEntityFixtureType>& WeakFixtureType : SelectedFixtureTypes)
	{
		if (UDMXEntityFixtureType* FixtureType = WeakFixtureType.Get())
		{
			for (int32 ModeIndex : SelectedModeIndices)
			{
				if (FixtureType->Modes.IsValidIndex(ModeIndex))
				{
					FDMXFixtureMode& Mode = FixtureType->Modes[ModeIndex];

					// Transaction
					const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("DuplicatFunctionTransaction", "Duplicate Fixture Type Function"));

					FixtureType->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes)));
					FixtureType->Modify();

					int32 NumDuplicates = 0;
					for (int32 FunctionIndex : FunctionIndicesToDuplicate)
					{
						const TArray<int32> DuplicateAtIndices({ FunctionIndex + NumDuplicates });
						TArray<int32> NewlyAddedFunctionIndices;
						FixtureType->DuplicateFunctions(ModeIndex, DuplicateAtIndices, NewlyAddedFunctionIndices);

						NumDuplicates++;
					}

					FixtureType->UpdateChannelSpan(ModeIndex);

					FixtureType->PostEditChange();
				}
			}
		}
	}
}

void FDMXFixtureTypeSharedData::DeleteFunctions(const TArray<int32>& FunctionIndicesToDelete)
{
	// DEPRECATED 5.0

	for (const TWeakObjectPtr<UDMXEntityFixtureType>& WeakFixtureType : SelectedFixtureTypes)
	{
		if (UDMXEntityFixtureType* FixtureType = WeakFixtureType.Get())
		{
			for (int32 ModeIndex : SelectedModeIndices)
			{
				if (FixtureType->Modes.IsValidIndex(ModeIndex))
				{
					FDMXFixtureMode& Mode = FixtureType->Modes[ModeIndex];

					const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("DeleteFunctionsTransaction", "Deleted Fixture Type Functions"));

					// Required this way for the 5.0 Deprecated UDMXEntityFixtureType::OnDataTypeChanged event
					FArrayProperty* ModesProperty = FindFProperty<FArrayProperty>(UDMXEntityFixtureType::StaticClass(), GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
					FArrayProperty* FunctionsProperty = FindFProperty<FArrayProperty>(FDMXFixtureMode::StaticStruct(), GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions));
					FEditPropertyChain PropertyChain;
					PropertyChain.AddHead(ModesProperty);
					PropertyChain.AddTail(FunctionsProperty);
					PropertyChain.SetActiveMemberPropertyNode(ModesProperty);
					PropertyChain.SetActivePropertyNode(FunctionsProperty);

					FixtureType->PreEditChange(FunctionsProperty);
					FixtureType->Modify();

					for (const int32 FunctionIndex : FunctionIndicesToDelete)
					{
						if (Mode.Functions.IsValidIndex(FunctionIndex))
						{
							const TArray<int32> RemoveAtIndices({ FunctionIndex });
							FixtureType->RemoveFunctions(ModeIndex, RemoveAtIndices);
						}
					}

					FPropertyChangedEvent PropertyChangedEvent(FunctionsProperty, EPropertyChangeType::ArrayRemove);
					FPropertyChangedChainEvent PropertyChangedChainEvent(PropertyChain, PropertyChangedEvent);

					FixtureType->PostEditChangeChainProperty(PropertyChangedChainEvent);
				}
			}
		}
	}
}

void FDMXFixtureTypeSharedData::PasteClipboardToFunctions(const TArray<int32>& FunctionIndices)
{
	// DEPRECATED 5.0

	for (int32 PasteOntoElementIndex = 0; PasteOntoElementIndex < FunctionIndices.Num(); PasteOntoElementIndex++)
	{
		const int32 PasteOntoIndex = FunctionIndices[PasteOntoElementIndex];

		const FString& ClipboardString = [PasteOntoElementIndex, this]()
		{
			if (FunctionsClipboard.IsValidIndex(PasteOntoElementIndex))
			{
				return FunctionsClipboard[PasteOntoElementIndex];
			}
			else if (FunctionsClipboard.Num() > 0)
			{
				return FunctionsClipboard.Last();
			}

			return FString();
		}();

		TSharedPtr<FJsonObject> RootJsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ClipboardString);
		FJsonSerializer::Deserialize(Reader, RootJsonObject);

		if (RootJsonObject.IsValid())
		{
			FDMXFixtureFunction FunctionFromClipboard;
			if (FJsonObjectConverter::JsonObjectToUStruct(RootJsonObject.ToSharedRef(), FDMXFixtureFunction::StaticStruct(), &FunctionFromClipboard, 0, 0))
			{
				for (const TWeakObjectPtr<UDMXEntityFixtureType>& WeakFixtureType : SelectedFixtureTypes)
				{
					if (UDMXEntityFixtureType* FixtureType = WeakFixtureType.Get())
					{
						const FScopedTransaction ModeNameTransaction = FScopedTransaction(LOCTEXT("PasteModeTransaction", "Paste Fixture Type Mode"));

						FixtureType->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes)));
						FixtureType->Modify();

						for (int32 ModeIndex : SelectedModeIndices)
						{
							if (FixtureType->Modes.IsValidIndex(ModeIndex))
							{
								FDMXFixtureMode& Mode = FixtureType->Modes[ModeIndex];

								if (Mode.Functions.IsValidIndex(PasteOntoIndex))
								{
									FDMXFixtureFunction& PasteOntoFunction = Mode.Functions[PasteOntoIndex];

									FString OldFunctionName = PasteOntoFunction.FunctionName;

									PasteOntoFunction = FunctionFromClipboard;

									// Keep the original function name
									PasteOntoFunction.FunctionName = OldFunctionName;
								}
							}
						}

						FixtureType->PostEditChange();
					}
				}
			}
		}
	}
}

bool FDMXFixtureTypeSharedData::CanAddCellAttribute() const
{
	// DEPRECATED 5.0

	return 
		SelectedFixtureTypes.Num() == 1 && 
		SelectedModeIndices.Num() == 1;
}

void FDMXFixtureTypeSharedData::AddCellAttributeToSelectedMode()
{
	// DEPRECATED 5.0

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const bool bCanAddCellAttribute = CanAddCellAttribute();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (ensureMsgf(bCanAddCellAttribute, TEXT("Call to FDMXFixtureTypeSharedData::AddCellAttributeToSelectedMode, when no Cell Attribute can be added.")))
	{
		if (UDMXEntityFixtureType* FixtureType = SelectedFixtureTypes[0].Get())
		{
			const int32 SelectedModeIndex = SelectedModeIndices[0];
			if (FixtureType->Modes.IsValidIndex(SelectedModeIndex) && FixtureType->Modes[SelectedModeIndex].bFixtureMatrixEnabled)
			{
				FDMXFixtureMode& Mode = FixtureType->Modes[SelectedModeIndices[0]];

				FDMXFixtureCellAttribute NewAttribute;
				TArray<FName> AvailableAttributes = FDMXAttributeName::GetPossibleValues();
				NewAttribute.Attribute = AvailableAttributes.Num() > 0 ? FDMXAttributeName(AvailableAttributes[0]) : FDMXAttributeName();

				const FDMXFixtureTypeSharedDataDetails::FDMXScopedModeEditChangeChainProperty_DEPRECATED ScopedChange(FixtureType);

				Mode.FixtureMatrixConfig.CellAttributes.Add(NewAttribute);

				// Update affected properties
				FixtureType->UpdateChannelSpan(SelectedModeIndex);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
