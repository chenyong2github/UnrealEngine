// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXFixtureTypeSharedData.h"

#include "DMXEditorUtils.h"
#include "Library/DMXEntityFixtureType.h"
#include "ScopedTransaction.h"
#include "PropertyHandle.h"


#define LOCTEXT_NAMESPACE "DMXFixtureTypeSharedData"

namespace 
{
	namespace FDMXFixtureTypeSharedDataDetails
	{
		// Helper to raise a mode Pre/PostEditChangeChainPropertyEvent with array index
		struct FDMXScopedModeEditChangeChainProperty
		{
			FDMXScopedModeEditChangeChainProperty(UDMXEntityFixtureType* InFixtureType)
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

			~FDMXScopedModeEditChangeChainProperty()
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

FDMXFixtureModeItem::FDMXFixtureModeItem(const TWeakPtr<FDMXFixtureTypeSharedData>& InSharedDataPtr, const TSharedPtr<IPropertyHandle>& InModeNameHandle)
	: SharedDataPtr(InSharedDataPtr)
	, ModeNameHandle(InModeNameHandle)
{
	check(ModeNameHandle.IsValid() && ModeNameHandle->IsValidHandle());
	check(ModeNameHandle->GetProperty() && ModeNameHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, ModeName));

}

FDMXFixtureModeItem::~FDMXFixtureModeItem()
{}

bool FDMXFixtureModeItem::EqualsMode(const TSharedPtr<FDMXFixtureModeItem>& Other) const
{
	check(Other->SharedDataPtr == SharedDataPtr);

	return Other->GetModeName() == GetModeName();
}

FString FDMXFixtureModeItem::GetModeName() const
{
	FString ModeName;
	if (ModeNameHandle.IsValid())
	{
		ModeNameHandle->GetValue(ModeName);
	}
	return ModeName;
}

void FDMXFixtureModeItem::SetModeName(const FString& InDesiredName, FString& OutNewName)
{
	SetName(InDesiredName, OutNewName);
}

bool FDMXFixtureModeItem::IsModeBeingEdited() const
{
	if (TSharedPtr<FDMXFixtureTypeSharedData> SharedData = SharedDataPtr.Pin())
	{
		const TSharedPtr<FDMXFixtureModeItem>* EditedModeItem = SharedData->GetModesBeingEdited().FindByPredicate([&](const TSharedPtr<FDMXFixtureModeItem>& Other) {
			return EqualsMode(Other);
			});
		return EditedModeItem != nullptr;
	}
	return false;
}

bool FDMXFixtureModeItem::IsModeSelected() const 
{
	if (TSharedPtr<FDMXFixtureTypeSharedData> SharedData = SharedDataPtr.Pin())
	{
		const TSharedPtr<FDMXFixtureModeItem>* SelectedModeItem = SharedData->GetSelectedModes().FindByPredicate([&](const TSharedPtr<FDMXFixtureModeItem>& Other) {
			return EqualsMode(Other);
			});
		return SelectedModeItem != nullptr;
	}
	return false;
}

void FDMXFixtureModeItem::SelectMode(bool bSelected)
{
	if (TSharedPtr<FDMXFixtureTypeSharedData> SharedData = SharedDataPtr.Pin())
	{
		SharedData->SelectModes(TArray<TSharedPtr<FDMXFixtureModeItem>>({ SharedThis(this) }));
	}
}

void FDMXFixtureModeItem::GetOuterFixtureTypes(TArray<TWeakObjectPtr<UDMXEntityFixtureType>>& OuterFixtureTypes) const
{
	OuterFixtureTypes.Reset();
	if (ModeNameHandle.IsValid())
	{
		TArray<UObject*> OuterObjects;
		ModeNameHandle->GetOuterObjects(OuterObjects);
		for (UObject* Object : OuterObjects)
		{
			TWeakObjectPtr<UDMXEntityFixtureType> OuterFixtureType = CastChecked<UDMXEntityFixtureType>(Object);
			OuterFixtureTypes.Add(OuterFixtureType);
		}
	}
}

void FDMXFixtureModeItem::GetName(FString& OutUniqueName) const
{
	if (ModeNameHandle.IsValid())
	{
		ModeNameHandle->GetValue(OutUniqueName);
	}
}

bool FDMXFixtureModeItem::IsNameUnique(const FString& TestedName) const
{
	if (ModeNameHandle.IsValid())
	{
		TArray<TWeakObjectPtr<UDMXEntityFixtureType>> OuterFixtureTypes;
		GetOuterFixtureTypes(OuterFixtureTypes);

		TSet<FString> ModeNames;
		for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : OuterFixtureTypes)
		{
			for (const FDMXFixtureMode& Mode : FixtureType->Modes)
			{
				ModeNames.Add(Mode.ModeName);
			}
		}

		return !ModeNames.Contains(TestedName);
	}
	return true;
}

void FDMXFixtureModeItem::SetName(const FString& InDesiredName, FString& OutUniqueName)
{
	if (TSharedPtr<FDMXFixtureTypeSharedData> SharedData = SharedDataPtr.Pin())
	{
		TSet<FString> ModeNames;
		for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : SharedData->GetFixtureTypesBeingEdited())
		{
			for (const FDMXFixtureMode& Mode : FixtureType->Modes)
			{
				ModeNames.Add(Mode.ModeName);
			}
		}

		OutUniqueName = FDMXEditorUtils::GenerateUniqueNameFromExisting(ModeNames, InDesiredName);
		ModeNameHandle->SetValue(OutUniqueName);
	}
}

FDMXFixtureFunctionItem::FDMXFixtureFunctionItem(TWeakPtr<FDMXFixtureTypeSharedData> InOwnerPtr, const TSharedPtr<IPropertyHandle> InModeNameHandle, const TSharedPtr<IPropertyHandle> InFunctionHandle)
	: FDMXFixtureModeItem(InOwnerPtr, InModeNameHandle)
	, FunctionHandle(InFunctionHandle)
{
	check(FunctionHandle.IsValid());

	FunctionNameHandle = InFunctionHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, FunctionName));
	check(FunctionNameHandle.IsValid() && FunctionNameHandle->IsValidHandle());

	FunctionChannelHandle = InFunctionHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, Channel));
	check(FunctionChannelHandle.IsValid() && FunctionChannelHandle->IsValidHandle());

	check(FunctionNameHandle.IsValid() && FunctionNameHandle->IsValidHandle());
	check(FunctionNameHandle->GetProperty() && FunctionNameHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, FunctionName));
}

FDMXFixtureFunctionItem::~FDMXFixtureFunctionItem()
{}

bool FDMXFixtureFunctionItem::EqualsFunction(const TSharedPtr<FDMXFixtureFunctionItem>& Other) const
{
	check(Other->SharedDataPtr == SharedDataPtr);
	// Since we expect a common shared data instance, 
	// Edited objects being equal is implied

	if (!EqualsMode(Other))
	{
		return false;
	}

	return GetFunctionName() == Other->GetFunctionName();
}

FString FDMXFixtureFunctionItem::GetFunctionName() const
{
	FString FunctionName;	
	if (FunctionNameHandle.IsValid())
	{
		FunctionNameHandle->GetValue(FunctionName);
	}
	return FunctionName;
}

int32 FDMXFixtureFunctionItem::GetFunctionChannel() const
{
	int32 FunctionChannel = 0;
	if (FunctionChannelHandle.IsValid())
	{
		FunctionChannelHandle->GetValue(FunctionChannel);
	}
	return FunctionChannel;
}

void FDMXFixtureFunctionItem::SetFunctionName(const FString& InDesiredName, FString& OutNewName)
{
	if (FunctionNameHandle.IsValid())
	{
		FunctionNameHandle->GetValue(OutNewName);
	}
}

bool FDMXFixtureFunctionItem::IsFunctionEdited() const
{
	if (TSharedPtr<FDMXFixtureTypeSharedData> SharedData = SharedDataPtr.Pin())
	{
		const TSharedPtr<FDMXFixtureFunctionItem>* EditedFunctionItem = SharedData->GetFunctionsBeingEdited().FindByPredicate([&](const TSharedPtr<FDMXFixtureFunctionItem>& Other) {
			return EqualsFunction(Other);
			});
		return EditedFunctionItem != nullptr;
	}
	return false;
}

bool FDMXFixtureFunctionItem::IsFunctionSelected() const
{
	if (TSharedPtr<FDMXFixtureTypeSharedData> SharedData = SharedDataPtr.Pin())
	{
		const TSharedPtr<FDMXFixtureFunctionItem>* SelectedFunctionItem = SharedData->GetSelectedFunctions().FindByPredicate([&](const TSharedPtr<FDMXFixtureFunctionItem>& Other) {
			return EqualsFunction(Other);
			});
		return SelectedFunctionItem != nullptr;
	}
	return false;
}

void FDMXFixtureFunctionItem::GetName(FString& OutUniqueName) const
{
	if (FunctionNameHandle.IsValid())
	{
		FunctionNameHandle->GetValue(OutUniqueName);
	}
}

bool FDMXFixtureFunctionItem::IsNameUnique(const FString& TestedName) const
{
	if (TSharedPtr<FDMXFixtureTypeSharedData> SharedData = SharedDataPtr.Pin())
	{
		TSet<FString> FunctionNames;
		for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : SharedData->GetFixtureTypesBeingEdited())
		{
			for (const FDMXFixtureMode& Mode : FixtureType->Modes)
			{
				for (const FDMXFixtureFunction& Function : Mode.Functions)
				{
					FunctionNames.Add(Function.FunctionName);
				}
			}
		}

		return !FunctionNames.Contains(TestedName);
	}
	return true;
}

void FDMXFixtureFunctionItem::SetName(const FString& InDesiredName, FString& OutUniqueName)
{
	if (TSharedPtr<FDMXFixtureTypeSharedData> SharedData = SharedDataPtr.Pin())
	{
		TSet<FString> FunctionNames;
		for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : SharedData->GetFixtureTypesBeingEdited())
		{
			for (const FDMXFixtureMode& Mode : FixtureType->Modes)
			{
				for (const FDMXFixtureFunction& Function : Mode.Functions)
				{
					FunctionNames.Add(Function.FunctionName);
				}
			}
		}

		OutUniqueName = FDMXEditorUtils::GenerateUniqueNameFromExisting(FunctionNames, InDesiredName);
		FunctionNameHandle->SetValue(OutUniqueName);
	}
}

void FDMXFixtureTypeSharedData::SetFixtureTypesBeingEdited(const TArray<TWeakObjectPtr<UDMXEntityFixtureType>>& InFixtureTypesBeingEdited)
{
	FixtureTypesBeingEdited = InFixtureTypesBeingEdited;

	// Selections turn invalid
	SelectedModes.Reset();
	SelectedFunctions.Reset();
}

int32 FDMXFixtureTypeSharedData::GetNumSelectedModes() const
{
	return SelectedModes.Num();
}

int32 FDMXFixtureTypeSharedData::GetNumSelectedFunctions() const
{
	return SelectedFunctions.Num();
}

void FDMXFixtureTypeSharedData::SelectModes(const TArray<TSharedPtr<FDMXFixtureModeItem>>& InModes)
{
	SelectedModes = InModes;

	// Remove functions that are not contained in selected modes from selection
	SelectedFunctions.RemoveAll([&](const TSharedPtr<FDMXFixtureFunctionItem>& FunctionItem) {
		TSharedPtr<FDMXFixtureModeItem>* SelectedParentMode = SelectedModes.FindByPredicate([&](const TSharedPtr<FDMXFixtureModeItem>& ModeItem) {
			return ModeItem->GetModeName() == FunctionItem->GetModeName();
			});
		return !SelectedParentMode;
		});

	OnModesSelected.Broadcast();
}

void FDMXFixtureTypeSharedData::SelectFunctions(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& InFunctions)
{
	SelectedFunctions = InFunctions;
	OnFunctionsSelected.Broadcast();
}

bool FDMXFixtureTypeSharedData::CanAddMode() const
{
	return GetFixtureTypesBeingEdited().Num() == 1;
}

void FDMXFixtureTypeSharedData::AddMode(const FScopedTransaction& TransactionScope)
{
	check(CanAddMode());
	if (!CanAddMode())
	{
		return;
	}

	for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : GetFixtureTypesBeingEdited())
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
		NewMode.ModeName = FDMXEditorUtils::GenerateUniqueNameFromExisting(ModeNames, LOCTEXT("DMXFixtureTypeSharedData.DefaultModeName", "Mode").ToString());

		FProperty* ModesProperty = UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes));
		check(ModesProperty);

		const FDMXFixtureTypeSharedDataDetails::FDMXScopedModeEditChangeChainProperty ScopedChange = FDMXFixtureTypeSharedDataDetails::FDMXScopedModeEditChangeChainProperty(FixtureType.Get());

		FixtureType->Modes.Add(NewMode);	
	}
}

/** Helper struct to keep references to modes in arrays while operating on it */
struct FModeRef
{
	FModeRef(TWeakObjectPtr<UDMXEntityFixtureType> InFixtureType, const FString& InModeName)
		: FixtureType(InFixtureType)
		, ModeName(InModeName)
	{}

	TWeakObjectPtr<UDMXEntityFixtureType> FixtureType;
	FString ModeName;

	static TArray<FModeRef> GetModeReferences(const TArray<TSharedPtr<FDMXFixtureModeItem>>& Modes)
	{
		TArray<FModeRef> Result;
		for (const TSharedPtr<FDMXFixtureModeItem>& Mode : Modes)
		{
			TArray<TWeakObjectPtr<UDMXEntityFixtureType>> OuterFixtureTypes;
			Mode->GetOuterFixtureTypes(OuterFixtureTypes);
			for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : OuterFixtureTypes)
			{
				Result.Add(FModeRef(FixtureType, Mode->GetModeName()));
			}
		}
		return Result;
	}
};

void FDMXFixtureTypeSharedData::DuplicateModes(const TArray<TSharedPtr<FDMXFixtureModeItem>>& ModesToDuplicate)
{
	TArray<FModeRef> ModeReferences = FModeRef::GetModeReferences(ModesToDuplicate);

	for (const FModeRef& ModeRef : ModeReferences)
	{
		if (!ModeRef.FixtureType.IsValid())
		{
			continue;
		}

		int32 ModeIndex = ModeRef.FixtureType->Modes.IndexOfByPredicate([&](const FDMXFixtureMode& Mode) {
			return Mode.ModeName == ModeRef.ModeName;
			});
		check(ModeIndex != INDEX_NONE);

		// Copy
		FDMXFixtureMode NewMode = ModeRef.FixtureType->Modes[ModeIndex];

		// Get a unique name
		TSet<FString> ModeNames;
		for (const FDMXFixtureMode& Mode : ModeRef.FixtureType->Modes)
		{
			ModeNames.Add(Mode.ModeName);
		}
		NewMode.ModeName = FDMXEditorUtils::GenerateUniqueNameFromExisting(ModeNames, ModeRef.ModeName);

		// Transaction
		const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("DMXFixtureTypeSharedData.ModeDuplicated", "DMX Editor: Duplicated Fixture Type Mode"));

		const FDMXFixtureTypeSharedDataDetails::FDMXScopedModeEditChangeChainProperty ScopedChange = FDMXFixtureTypeSharedDataDetails::FDMXScopedModeEditChangeChainProperty(ModeRef.FixtureType.Get());

		if (ModeRef.FixtureType->Modes.IsValidIndex(ModeIndex + 1))
		{
			ModeRef.FixtureType->Modes.Insert(NewMode, ModeIndex + 1);
		}
		else
		{
			ModeRef.FixtureType->Modes.Add(NewMode);
		}
	}
}

void FDMXFixtureTypeSharedData::DeleteModes(const TArray<TSharedPtr<FDMXFixtureModeItem>>& ModesToDelete)
{
	TArray<FModeRef> ModeReferences = FModeRef::GetModeReferences(ModesToDelete);

	for (const FModeRef& ModeRef : ModeReferences)
	{
		if (!ModeRef.FixtureType.IsValid())
		{
			continue;
		}

		int32 ModeIndex = ModeRef.FixtureType->Modes.IndexOfByPredicate([&](const FDMXFixtureMode& Mode) {
			return Mode.ModeName == ModeRef.ModeName;
			});
		check(ModeIndex != INDEX_NONE);

		// Transaction
		const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("DMXFixtureTypeSharedData.ModeDeleted", "DMX Editor: Deleted Fixture Type Mode"));

		const FDMXFixtureTypeSharedDataDetails::FDMXScopedModeEditChangeChainProperty ScopedChange = FDMXFixtureTypeSharedDataDetails::FDMXScopedModeEditChangeChainProperty(ModeRef.FixtureType.Get());

		ModeRef.FixtureType->Modes.RemoveAt(ModeIndex);
	}
}


void FDMXFixtureTypeSharedData::CopyModesToClipboard(const TArray<TSharedPtr<FDMXFixtureModeItem>>& ModeItems)
{
	ModesClipboard.Reset();
	for (TSharedPtr<FDMXFixtureModeItem> ModeItem : ModeItems)
	{
		for(const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : GetFixtureTypesBeingEdited())
		{ 
			if (!FixtureType.IsValid())
			{
				continue;
			}
			FDMXFixtureMode* ModeInAssetPtr = FixtureType->Modes.FindByPredicate([&](const FDMXFixtureMode& ModeInAsset) {
				return ModeInAsset.ModeName == ModeItem->GetModeName();
				});
			check(ModeInAssetPtr);

			FString CopyStr;
			if (SerializeStruct<FDMXFixtureMode>(*ModeInAssetPtr, CopyStr))
			{
				ModesClipboard.Add(CopyStr);
			}
		}
	}
}

void FDMXFixtureTypeSharedData::PasteClipboardToModes(const TArray<TSharedPtr<FDMXFixtureModeItem>>& ModeItems)
{
	for (int32 IndexOfMode = 0; IndexOfMode < ModeItems.Num(); IndexOfMode++)
	{
		if (!ModesClipboard.IsValidIndex(IndexOfMode))
		{
			return;
		}

		const FString& ClipboardElement = ModesClipboard[IndexOfMode];

		TSharedPtr<FJsonObject> RootJsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ClipboardElement);
		FJsonSerializer::Deserialize(Reader, RootJsonObject);

		if (RootJsonObject.IsValid())
		{
			FDMXFixtureMode ModeFromClipboard;

			if (FJsonObjectConverter::JsonObjectToUStruct(RootJsonObject.ToSharedRef(), FDMXFixtureMode::StaticStruct(), &ModeFromClipboard, 0, 0))
			{
				for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : GetFixtureTypesBeingEdited())
				{
					if (!FixtureType.IsValid())
					{
						continue;
					}

					FDMXFixtureMode* ModePtr = FixtureType->Modes.FindByPredicate([&](const FDMXFixtureMode& ModeInAsset) {
						return ModeInAsset.ModeName == ModeItems[IndexOfMode]->GetModeName();
						});
					if (!ModePtr)
					{
						continue;
					}

					FString OldModeName = ModePtr->ModeName;
					FDMXFixtureMode& Mode = *ModePtr;
					
					FixtureType->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes)));
					const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("DMXFixtureTypeSharedData.FixtureTypeModePasted", "DMX Editor: Pasted Fixture Type Mode"));
					FixtureType->Modify();

					Mode = ModeFromClipboard;

					// Keep the original mode name
					Mode.ModeName = OldModeName;

					FixtureType->PostEditChange();
				}
			}
		}
	}
}

bool FDMXFixtureTypeSharedData::CanAddFunction() const
{
	return GetNumSelectedModes() == 1;
}

void FDMXFixtureTypeSharedData::AddFunctionToSelectedMode()
{
	check(CanAddFunction());
	if (!CanAddFunction())
	{
		return;
	}
	TArray<FModeRef> ModeReferences = FModeRef::GetModeReferences(TArray<TSharedPtr<FDMXFixtureModeItem>>({ SelectedModes[0] }));

	for (const FModeRef& ModeRef : ModeReferences)
	{
		if (!ModeRef.FixtureType.IsValid())
		{
			continue;
		}

		FDMXFixtureMode* ModePtr = ModeRef.FixtureType->Modes.FindByPredicate([&](const FDMXFixtureMode& ModeInAsset) {
			return ModeInAsset.ModeName == SelectedModes[0]->GetModeName();
			});
		if (!ModePtr)
		{
			continue;
		}
		FDMXFixtureMode& Mode = *ModePtr;

		// Get a unique name
		TSet<FString> FunctionNames;
		for (const FDMXFixtureFunction& Function : Mode.Functions)
		{
			FunctionNames.Add(Function.FunctionName);
		}
		FDMXFixtureFunction NewFunction;
		NewFunction.FunctionName = FDMXEditorUtils::GenerateUniqueNameFromExisting(FunctionNames, LOCTEXT("DMXFixtureTypeSharedData.DefaultFunctionName", "Function").ToString());

		const FScopedTransaction ModeNameTransaction = FScopedTransaction(LOCTEXT("DMXFixtureTypeSharedData.AddedFunction", "DMX Editor: Added Fixture Type Function"));

		ModeRef.FixtureType->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes)));
		ModeRef.FixtureType->Modify();

		Mode.Functions.Add(NewFunction);
		ModeRef.FixtureType->UpdateChannelSpan(Mode);

		ModeRef.FixtureType->PostEditChange();

		FDMXEditorUtils::AutoAssignedAddresses(ModeRef.FixtureType.Get());
	}
}

/** Helper struct to keep references to functions in arrays while operating on it */
struct FFunctionRef
{
	FFunctionRef(TWeakObjectPtr<UDMXEntityFixtureType> InFixtureType, const FString& InModeName, const FString& InFunctionName)
		: FixtureType(InFixtureType)
		, ModeName(InModeName)
		, FunctionName(InFunctionName)	
	{}

	TWeakObjectPtr<UDMXEntityFixtureType> FixtureType;
	FString ModeName;
	FString FunctionName;

	static TArray<FFunctionRef> GetFunctionReferences(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& Functions)
	{
		TArray<FFunctionRef> Result;
		for (const TSharedPtr<FDMXFixtureFunctionItem>& Function : Functions)
		{
			TArray<TWeakObjectPtr<UDMXEntityFixtureType>> OuterFixtureTypes;
			Function->GetOuterFixtureTypes(OuterFixtureTypes);
			for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : OuterFixtureTypes)
			{
				Result.Add(FFunctionRef(FixtureType, Function->GetModeName(), Function->GetFunctionName()));
			}
		}
		return Result;
	}
};

void FDMXFixtureTypeSharedData::DuplicateFunctions(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& FunctionsToDuplicate)
{
	TArray<FFunctionRef> FunctionReferences = FFunctionRef::GetFunctionReferences(FunctionsToDuplicate);

	for (const FFunctionRef& FuncRef : FunctionReferences)
	{
		if (!FuncRef.FixtureType.IsValid())
		{
			continue;
		}

		FDMXFixtureMode* ModePtr = FuncRef.FixtureType->Modes.FindByPredicate([&](const FDMXFixtureMode& Mode) {
			return Mode.ModeName == FuncRef.ModeName;
			});
		check(ModePtr);

		int32 IndexOfFunction = ModePtr->Functions.IndexOfByPredicate([&](const FDMXFixtureFunction& Function) {
			return Function.FunctionName == FuncRef.FunctionName;
			});
		check(IndexOfFunction != INDEX_NONE);

		// Copy
		FDMXFixtureFunction NewFunction = ModePtr->Functions[IndexOfFunction];

		// Get a unique name
		TSet<FString> FunctionNames;
		for (const FDMXFixtureFunction& Function : ModePtr->Functions)
		{
			FunctionNames.Add(Function.FunctionName);
		}
		NewFunction.FunctionName = FDMXEditorUtils::GenerateUniqueNameFromExisting(FunctionNames, FuncRef.FunctionName);

		// Transaction
		const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("DMXFixtureTypeSharedData.DuplicatedFunction", "DMX Editor: Duplicated Fixture Type Function"));

		FuncRef.FixtureType->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes)));
		FuncRef.FixtureType->Modify();

		ModePtr->AddOrInsertFunction(IndexOfFunction, NewFunction);

		FuncRef.FixtureType->PostEditChange();
	}
}

void FDMXFixtureTypeSharedData::DeleteFunctions(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& FunctionsToDelete)
{
	FArrayProperty* ModesProperty = FindFProperty<FArrayProperty>(UDMXEntityFixtureType::StaticClass(), GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
	FArrayProperty* FunctionsProperty = FindFProperty<FArrayProperty>(FDMXFixtureMode::StaticStruct(), GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions));
	FEditPropertyChain PropertyChain;
	PropertyChain.AddHead(ModesProperty);
	PropertyChain.AddTail(FunctionsProperty);
	PropertyChain.SetActiveMemberPropertyNode(ModesProperty);
	PropertyChain.SetActivePropertyNode(FunctionsProperty);

	TArray<FFunctionRef> FunctionReferences = FFunctionRef::GetFunctionReferences(FunctionsToDelete);
	if (FunctionReferences.Num() == 0)
	{
		return;
	}

	UDMXEntityFixtureType* FixtureType = FunctionReferences[0].FixtureType.Get();
	if (!FixtureType)
	{
		return;
	}

	const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("DMXFixtureTypeSharedData.DeletedFunction", "DMX Editor: Deleted Fixture Type Function"));

	FixtureType->PreEditChange(PropertyChain);
	FixtureType->Modify();

	for (const FFunctionRef& FuncRef : FunctionReferences)
	{
		check(FuncRef.FixtureType == FixtureType);

		FDMXFixtureMode* ModePtr = FuncRef.FixtureType->Modes.FindByPredicate([&](const FDMXFixtureMode& Mode) {
			return Mode.ModeName == FuncRef.ModeName;
			});
		check(ModePtr);

		int32 IndexOfFunction = ModePtr->Functions.IndexOfByPredicate([&](const FDMXFixtureFunction& Function) {
			return Function.FunctionName == FuncRef.FunctionName;
			});
		check(IndexOfFunction != INDEX_NONE);

		ModePtr->Functions.RemoveAt(IndexOfFunction);
	}

	FPropertyChangedEvent PropertyChangedEvent(FunctionsProperty, EPropertyChangeType::ArrayRemove);
	FPropertyChangedChainEvent PropertyChangedChainEvent(PropertyChain, PropertyChangedEvent);

	FixtureType->PostEditChangeChainProperty(PropertyChangedChainEvent);

	FDMXEditorUtils::AutoAssignedAddresses(FixtureType);
}

void FDMXFixtureTypeSharedData::CopyFunctionsToClipboard(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& FunctionItems)
{
	FunctionsClipboard.Reset();
	for (TSharedPtr<FDMXFixtureFunctionItem> Item : FunctionItems)
	{
		for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : GetFixtureTypesBeingEdited())
		{
			if (!FixtureType.IsValid())
			{
				continue;
			}

			const FDMXFixtureMode* ModePtr = FixtureType->Modes.FindByPredicate([&](const FDMXFixtureMode& ModeInAsset) {
				return ModeInAsset.ModeName == Item->GetModeName();
				});
			if (!ModePtr)
			{
				continue;
			}

			const FDMXFixtureFunction* FunctionPtr = ModePtr->Functions.FindByPredicate([&](const FDMXFixtureFunction& FunctionInAsset) {
				return FunctionInAsset.FunctionName == Item->GetFunctionName();
				});
			if (!FunctionPtr)
			{
				continue;
			}

			FString CopyStr;
			if (SerializeStruct<FDMXFixtureFunction>(*FunctionPtr, CopyStr))
			{
				FunctionsClipboard.Add(CopyStr);
			}
		}
	}
}

void FDMXFixtureTypeSharedData::PasteClipboardToFunctions(const TArray<TSharedPtr<FDMXFixtureFunctionItem>>& FunctionItems)
{
	for (int32 IndexOfFunction = 0; IndexOfFunction < FunctionItems.Num(); IndexOfFunction++)
	{
		if (!FunctionsClipboard.IsValidIndex(IndexOfFunction))
		{
			return;
		}

		const FString& ClipboardElement = FunctionsClipboard[IndexOfFunction];

		TSharedPtr<FJsonObject> RootJsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ClipboardElement);
		FJsonSerializer::Deserialize(Reader, RootJsonObject);

		if (RootJsonObject.IsValid())
		{
			FDMXFixtureFunction FunctionFromClipboard;
			if (FJsonObjectConverter::JsonObjectToUStruct(RootJsonObject.ToSharedRef(), FDMXFixtureFunction::StaticStruct(), &FunctionFromClipboard, 0, 0))
			{
				for (const TWeakObjectPtr<UDMXEntityFixtureType>& FixtureType : GetFixtureTypesBeingEdited())
				{
					if (!FixtureType.IsValid())
					{
						continue;
					}
					FDMXFixtureMode* ModeInAssetPtr = FixtureType->Modes.FindByPredicate([&](const FDMXFixtureMode& ModeInAsset) {
						return ModeInAsset.ModeName == FunctionItems[IndexOfFunction]->GetModeName();
						});
					if (!ModeInAssetPtr)
					{
						continue;
					}
					FDMXFixtureFunction* FunctionPtr = ModeInAssetPtr->Functions.FindByPredicate([&](const FDMXFixtureFunction& FunctionInAsset) {
						return FunctionInAsset.FunctionName == FunctionItems[IndexOfFunction]->GetFunctionName();
						});
					if (!FunctionPtr)
					{
						continue;
					}

					FString OldFunctionName = FunctionPtr->FunctionName;
					FDMXFixtureFunction& Function = *FunctionPtr;					

					FixtureType->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UDMXEntityFixtureType, Modes)));
					const FScopedTransaction ModeNameTransaction = FScopedTransaction(LOCTEXT("DMXFixtureTypeSharedData.FixtureTypeModePasted", "DMX Editor: Pasted Fixture Type Mode"));
					FixtureType->Modify();

					Function = FunctionFromClipboard;

					// Keep the original function name
					Function.FunctionName = OldFunctionName;

					FixtureType->PostEditChange();
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
