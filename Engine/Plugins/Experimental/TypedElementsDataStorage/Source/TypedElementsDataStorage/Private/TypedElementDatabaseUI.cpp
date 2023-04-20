// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementDatabaseUI.h"

#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "GenericPlatform/GenericPlatformMemory.h"
#include "Widgets/SlateControlledConstruction.h"

DEFINE_LOG_CATEGORY(LogTypedElementDatabaseUI);

namespace Internal
{
	// Source: https://en.cppreference.com/w/cpp/utility/variant/visit
	template<class... Ts>
	struct TOverloaded : Ts...
	{ 
		using Ts::operator()...; 
	};

	template<class... Ts> TOverloaded(Ts...) -> TOverloaded<Ts...>;
}

void UTypedElementDatabaseUi::Initialize(
	ITypedElementDataStorageInterface* StorageInterface,
	ITypedElementDataStorageCompatibilityInterface* StorageCompatibilityInterface)
{
	checkf(StorageInterface, TEXT("Typed Element's Database compatibility manager is being initialized with an invalid storage target."));

	Storage = StorageInterface;
	StorageCompatibility = StorageCompatibilityInterface;
	CreateStandardArchetypes();
}

void UTypedElementDatabaseUi::Deinitialize()
{
}

void UTypedElementDatabaseUi::RegisterWidgetPurpose(FName Purpose, EPurposeType Type, FText Description)
{
	FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose);
	if (!PurposeInfo)
	{
		FPurposeInfo& NewInfo = WidgetPurposes.Add(Purpose);
		NewInfo.Type = Type;
		NewInfo.Description = MoveTemp(Description);
	}
}

bool UTypedElementDatabaseUi::RegisterWidgetFactory(FName Purpose, const UScriptStruct* Constructor)
{
	checkf(Constructor->IsChildOf(FTypedElementWidgetConstructor::StaticStruct()),
		TEXT("Attempting to register a Typed Elements widget constructor '%s' that isn't derived from FTypedElementWidgetConstructor."),
		*Constructor->GetFullName());
	
	if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
	{
		switch (PurposeInfo->Type)
		{
		case ITypedElementDataStorageUiInterface::EPurposeType::Generic:
			PurposeInfo->Factories.Emplace(Constructor);
			PurposeInfo->bIsSorted = false;
			return true;
		case ITypedElementDataStorageUiInterface::EPurposeType::UniqueByName:
			if (PurposeInfo->Factories.IsEmpty())
			{
				PurposeInfo->Factories.Emplace(Constructor);
				PurposeInfo->bIsSorted = false;
			}
			else
			{
				PurposeInfo->Factories.EmplaceAt(0, Constructor);
			}
			return true;
		case ITypedElementDataStorageUiInterface::EPurposeType::UniqueByNameAndColumn:
			UE_LOG(LogTypedElementDatabaseUI, Warning,
				TEXT("Unable to register widget factory '%s' as purpose '%s' requires at least one column for matching."), 
				*Constructor->GetName(), *Purpose.ToString());
			return false;
		default:
			checkf(false, TEXT("Unexpected ITypedElementDataStorageUiInterface::EPurposeType found provided when registering widget factory."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogTypedElementDatabaseUI, Warning, 
			TEXT("Unable to register widget factory '%s' as purpose '%s' isn't registered."), *Constructor->GetName(), *Purpose.ToString());
		return false;
	}
}

bool UTypedElementDatabaseUi::RegisterWidgetFactory(
	FName Purpose, const UScriptStruct* Constructor, TArray<TWeakObjectPtr<const UScriptStruct>> Columns)
{
	if (!Columns.IsEmpty())
	{
		checkf(Constructor->IsChildOf(FTypedElementWidgetConstructor::StaticStruct()),
			TEXT("Attempting to register a Typed Elements widget constructor '%s' that isn't deriving from FTypedElementWidgetConstructor."),
			*Constructor->GetFullName());

		if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
		{
			switch (PurposeInfo->Type)
			{
			case ITypedElementDataStorageUiInterface::EPurposeType::Generic:
				PurposeInfo->Factories.Emplace(Constructor);
				PurposeInfo->bIsSorted = false;
				return true;
			case ITypedElementDataStorageUiInterface::EPurposeType::UniqueByName:
				if (PurposeInfo->Factories.IsEmpty())
				{
					PurposeInfo->Factories.Emplace(Constructor);
					PurposeInfo->bIsSorted = false;
				}
				else
				{
					PurposeInfo->Factories.EmplaceAt(0, Constructor);
				}
				return true;
			case ITypedElementDataStorageUiInterface::EPurposeType::UniqueByNameAndColumn:
				PrepareColumnsList(Columns);
				PurposeInfo->Factories.Emplace(Constructor, MoveTemp(Columns));
				PurposeInfo->bIsSorted = false;
				return true;
			default:
				checkf(false, TEXT("Unexpected ITypedElementDataStorageUiInterface::EPurposeType found provided when registering widget factory."));
				return false;
			}
		}
		else
		{
			UE_LOG(LogTypedElementDatabaseUI, Warning,
				TEXT("Unable to register widget factory '%s' as purpose '%s' isn't registered."), *Constructor->GetName(), *Purpose.ToString());
			return false;
		}
	}
	else
	{
		return RegisterWidgetFactory(Purpose, Constructor);
	}
}

bool UTypedElementDatabaseUi::RegisterWidgetFactory(FName Purpose, TUniquePtr<FTypedElementWidgetConstructor>&& Constructor)
{
	checkf(Constructor->GetTypeInfo(), TEXT("Widget constructor being registered that doesn't have valid type information."));
	
	if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
	{
		switch (PurposeInfo->Type)
		{
		case ITypedElementDataStorageUiInterface::EPurposeType::Generic:
			PurposeInfo->Factories.Emplace(MoveTemp(Constructor));
			PurposeInfo->bIsSorted = false;
			return true;
		case ITypedElementDataStorageUiInterface::EPurposeType::UniqueByName:
			if (PurposeInfo->Factories.IsEmpty())
			{
				PurposeInfo->Factories.Emplace(MoveTemp(Constructor));
				PurposeInfo->bIsSorted = false;
			}
			else
			{
				PurposeInfo->Factories.EmplaceAt(0, MoveTemp(Constructor));
			}
			return true;
		case ITypedElementDataStorageUiInterface::EPurposeType::UniqueByNameAndColumn:
			UE_LOG(LogTypedElementDatabaseUI, Warning,
				TEXT("Unable to register widget factory '%s' as purpose '%s' requires at least one column for matching."),
				*Constructor->GetTypeInfo()->GetName(), *Purpose.ToString());
			return false;
		default:
			checkf(false, TEXT("Unexpected ITypedElementDataStorageUiInterface::EPurposeType found provided when registering widget factory."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogTypedElementDatabaseUI, Warning, 
			TEXT("Unable to register widget factory as purpose '%s' isn't registered."), *Purpose.ToString());
		return false;
	}
}

bool UTypedElementDatabaseUi::RegisterWidgetFactory(FName Purpose, TUniquePtr<FTypedElementWidgetConstructor>&& Constructor, 
	TArray<TWeakObjectPtr<const UScriptStruct>> Columns)
{
	if (!Columns.IsEmpty())
	{
		checkf(Constructor->GetTypeInfo(), TEXT("Widget constructor being registered that doesn't have valid type information."));

		if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
		{
			switch (PurposeInfo->Type)
			{
			case ITypedElementDataStorageUiInterface::EPurposeType::Generic:
				PurposeInfo->Factories.Emplace(MoveTemp(Constructor));
				PurposeInfo->bIsSorted = false;
				return true;
			case ITypedElementDataStorageUiInterface::EPurposeType::UniqueByName:
				if (PurposeInfo->Factories.IsEmpty())
				{
					PurposeInfo->Factories.Emplace(MoveTemp(Constructor));
					PurposeInfo->bIsSorted = false;
				}
				else
				{
					PurposeInfo->Factories.EmplaceAt(0, MoveTemp(Constructor));
				}
				return true;
			case ITypedElementDataStorageUiInterface::EPurposeType::UniqueByNameAndColumn:
				PrepareColumnsList(Columns);
				PurposeInfo->Factories.Emplace(MoveTemp(Constructor), MoveTemp(Columns));
				PurposeInfo->bIsSorted = false;
				return true;
			default:
				checkf(false, TEXT("Unexpected ITypedElementDataStorageUiInterface::EPurposeType found provided when registering widget factory."));
				return false;
			}
		}
		else
		{
			UE_LOG(LogTypedElementDatabaseUI, Warning, TEXT("Unable to register widget factory '%s' as purpose '%s' isn't registered."), 
				*Constructor->GetTypeInfo()->GetName(), *Purpose.ToString());
			return false;
		}
	}
	else
	{
		return RegisterWidgetFactory(Purpose, MoveTemp(Constructor));
	}
}

void UTypedElementDatabaseUi::CreateWidgetConstructors(FName Purpose,
	TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments, const WidgetConstructorCallback& Callback)
{
	if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
	{
		for (const FWidgetFactory& Factory : PurposeInfo->Factories)
		{
			if (!CreateSingleWidgetConstructor(Factory.Constructor, Arguments, {}, Callback))
			{
				return;
			}
		}
	}
}

void UTypedElementDatabaseUi::CreateWidgetConstructors(FName Purpose, TArray<TWeakObjectPtr<const UScriptStruct>>& Columns,
	TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments, const WidgetConstructorCallback& Callback)
{
	using ColumnView = TConstArrayView<TWeakObjectPtr<const UScriptStruct>>;
	
	if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
	{
		// Sort so searching can be done in a single pass. This would also allow for binary searching, but the number of columns
		// is typically small enough for a binary search to end up being more expensive than a linear search. This may change
		// if/when there are a sufficient enough number of widgets that are bound to a large number of columns.
		Columns.Sort(
			[](const TWeakObjectPtr<const UScriptStruct>& Lhs, const TWeakObjectPtr<const UScriptStruct>& Rhs)
			{
				return Lhs.GetWeakPtrTypeHash() < Rhs.GetWeakPtrTypeHash();
			});

		if (!PurposeInfo->bIsSorted)
		{
			// This is the only call that requires the array of factories to be sorted from from largest to smallest number
			// of columns, so lazily sort only when needed.
			PurposeInfo->Factories.StableSort(
				[](const FWidgetFactory& Lhs, const FWidgetFactory& Rhs)
				{
					int32 LeftSize = Lhs.Columns.Num();
					int32 RightSize = Rhs.Columns.Num();
					if (LeftSize == RightSize)
					{
						for (int32 Index = 0; Index < LeftSize; ++Index)
						{
							uint32 LeftHash = Lhs.Columns[Index].GetWeakPtrTypeHash();
							uint32 RightHash = Lhs.Columns[Index].GetWeakPtrTypeHash();
							if (LeftHash != RightHash)
							{
								return LeftHash < RightHash;
							}
						}
						return true;
					}
					else
					{
						return LeftSize > RightSize;
					}
				});
			PurposeInfo->bIsSorted = true;
		}

		for (auto FactoryIt = PurposeInfo->Factories.CreateConstIterator();  FactoryIt && !Columns.IsEmpty();  ++FactoryIt)
		{
			int32 MatchingIndex = INDEX_NONE;
			if (FactoryIt->Columns.Num() > Columns.Num())
			{
				// There are more columns required for this factory than there are in the requested columns list so skip this
				// factory.
				continue;
			}
			
			ColumnView ColumnsRange = Columns;
			for (auto ColumnIt = FactoryIt->Columns.CreateConstIterator(); ColumnIt && !ColumnsRange.IsEmpty(); ++ColumnIt)
			{
				int32 Index = INDEX_NONE;
				if (ColumnsRange.Find(*ColumnIt, Index))
				{
					MatchingIndex = MatchingIndex == INDEX_NONE ? Index : MatchingIndex;
					ColumnsRange = ColumnsRange.RightChop(Index + 1);
				}
				else
				{
					// Reset as there is at least one column that didn't match.
					MatchingIndex = INDEX_NONE;
					break;
				}
			}

			// This assumes that the column arrays are sorted from longest to shortest.
			if (MatchingIndex != INDEX_NONE)
			{
				// Delete in reverse order to reduce the number of objects that need to be moved up as the order needs to remain stable.
				ColumnsRange = ColumnView(Columns);
				for (auto It = FactoryIt->Columns.rbegin(); It != FactoryIt->Columns.rend(); ++It)
				{
					int32 Index = INDEX_NONE;
					ensureMsgf(ColumnsRange.Find(*It, Index), TEXT("A previous found matching column can't be found in the original array."));
					Columns.RemoveAt(Index);
					ColumnsRange = ColumnsRange.Left(Index);
				}

				if (!CreateSingleWidgetConstructor(FactoryIt->Constructor, Arguments, FactoryIt->Columns, Callback))
				{
					return;
				}
			}
		}
	}
}

void UTypedElementDatabaseUi::ConstructWidgets(FName Purpose, TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments,
	const WidgetCreatedCallback& ConstructionCallback)
{
	if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
	{
		for (const FWidgetFactory& Factory : PurposeInfo->Factories)
		{
			std::visit(Internal::TOverloaded
				{
					[this, &Arguments, &ConstructionCallback](const UScriptStruct* Constructor)
					{ 
						CreateWidgetInstanceFromDescription(Constructor, Arguments, ConstructionCallback); 
					},
					[this, &Arguments, &ConstructionCallback](const TUniquePtr<FTypedElementWidgetConstructor>& Constructor)
					{
						CreateWidgetInstanceFromInstance(Constructor.Get(), Arguments, ConstructionCallback);
					}
				}, Factory.Constructor);
		}
	}
}

bool UTypedElementDatabaseUi::CreateSingleWidgetConstructor(
	const FWidgetFactory::ConstructorType& Constructor,
	TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments, 
	TConstArrayView<TWeakObjectPtr<const UScriptStruct>> MatchedColumnTypes, 
	const WidgetConstructorCallback& Callback)
{
	return std::visit(Internal::TOverloaded
		{
			[this, &Arguments, &MatchedColumnTypes, &Callback](const UScriptStruct* Target)
			{
				TUniquePtr<FTypedElementWidgetConstructor> Result(reinterpret_cast<FTypedElementWidgetConstructor*>(
					FMemory::Malloc(Target->GetStructureSize(), Target->GetMinAlignment())));
				if (Result)
				{
					Target->InitializeStruct(Result.Get());
					return Callback(MoveTemp(Result), MatchedColumnTypes);
				}
				return true;
			},
			[this, &Arguments, &MatchedColumnTypes, &Callback](const TUniquePtr<FTypedElementWidgetConstructor>& Target)
			{
				const UScriptStruct* TargetType = Target->GetTypeInfo();
				checkf(TargetType, TEXT("Expected valid type information from a widget constructor."));
				TUniquePtr<FTypedElementWidgetConstructor> Result(reinterpret_cast<FTypedElementWidgetConstructor*>(
					FMemory::Malloc(TargetType->GetStructureSize(), TargetType->GetMinAlignment())));
				if (Result)
				{
					TargetType->InitializeStruct(Result.Get());
					TargetType->CopyScriptStruct(Result.Get(), Target.Get());
					return Callback(MoveTemp(Result), MatchedColumnTypes);
				}
				return true;
			}
		}, Constructor);
}

void UTypedElementDatabaseUi::CreateWidgetInstanceFromDescription(
	const UScriptStruct* Target,
	TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments,
	const WidgetCreatedCallback& ConstructionCallback)
{
	FTypedElementWidgetConstructor* Constructor = reinterpret_cast<FTypedElementWidgetConstructor*>(
		FMemory_Alloca_Aligned(Target->GetStructureSize(), Target->GetMinAlignment()));
	if (Constructor)
	{
		Target->InitializeStruct(Constructor);
		CreateWidgetInstance(*Constructor, Arguments, ConstructionCallback);
		Target->DestroyStruct(&Constructor);
	}
	else
	{
		checkf(false, TEXT("Remaining stack space is too small to create a Typed Elements widget constructor from a description."));
	}
}

void UTypedElementDatabaseUi::CreateWidgetInstanceFromInstance(
	FTypedElementWidgetConstructor* SourceConstructor,
	TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments,
	const WidgetCreatedCallback& ConstructionCallback)
{
	if (Arguments.IsEmpty())
	{
		CreateWidgetInstance(*SourceConstructor, Arguments, ConstructionCallback);
	}
	else
	{
		const UScriptStruct* Target = SourceConstructor->GetTypeInfo();
		checkf(Target, TEXT("Expected valid type information from a widget constructor."));
		FTypedElementWidgetConstructor* Constructor = reinterpret_cast<FTypedElementWidgetConstructor*>(
			FMemory_Alloca_Aligned(Target->GetStructureSize(), Target->GetMinAlignment()));
		if (Constructor)
		{
			Target->InitializeStruct(Constructor);
			Target->CopyScriptStruct(Constructor, SourceConstructor);
			CreateWidgetInstance(*Constructor, Arguments, ConstructionCallback);
			Target->DestroyStruct(&Constructor);
		}
		else
		{
			checkf(false, TEXT("Remaining stack space is too small to create a Typed Elements widget constructor from an instance."));
		}
	}
}

void UTypedElementDatabaseUi::CreateWidgetInstance(
	FTypedElementWidgetConstructor& Constructor, 
	TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments, 
	const WidgetCreatedCallback& ConstructionCallback)
{
	TypedElementRowHandle Row = Storage->AddRow(WidgetTable);
	TSharedPtr<SWidget> Widget = Constructor.Construct(Row, Storage, this, Arguments);
	if (Widget)
	{
		ConstructionCallback(Widget.ToSharedRef(), Row);
	}
	else
	{
		Storage->RemoveRow(Row);
	}
}

TSharedPtr<SWidget> UTypedElementDatabaseUi::ConstructWidget(TypedElementRowHandle Row, FTypedElementWidgetConstructor& Constructor,
	TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments)
{
	if (Arguments.IsEmpty())
	{
		return Constructor.Construct(Row, Storage, this, Arguments);
	}
	else
	{
		const UScriptStruct* Target = Constructor.GetTypeInfo();
		FTypedElementWidgetConstructor* ConstructorCopy = reinterpret_cast<FTypedElementWidgetConstructor*>(
			FMemory_Alloca_Aligned(Target->GetStructureSize(), Target->GetMinAlignment()));
		if (ConstructorCopy)
		{
			Target->InitializeStruct(ConstructorCopy);
			Target->CopyScriptStruct(ConstructorCopy, &Constructor);
			TSharedPtr<SWidget> Widget = ConstructorCopy->Construct(Row, Storage, this, Arguments);
			Target->DestroyStruct(&ConstructorCopy);
			return Widget;
		}
		else
		{
			checkf(false, TEXT("Remaining stack space is too small to create a Typed Elements widget constructor from an instance."));
			return nullptr;
		}
		return nullptr;
	}
}

void UTypedElementDatabaseUi::ListWidgetPurposes(const WidgetPurposeCallback& Callback) const
{
	for (auto&& It : WidgetPurposes)
	{
		Callback(It.Key, It.Value.Type, It.Value.Description);
	}
}

void UTypedElementDatabaseUi::CreateStandardArchetypes()
{
	WidgetTable = Storage->RegisterTable(MakeArrayView(
		{
			FTypedElementSlateWidgetReferenceColumn::StaticStruct(),
			FTypedElementSlateWidgetReferenceDeletesRowTag::StaticStruct()
		}), FName("Editor_WidgetTable"));
}

void UTypedElementDatabaseUi::PrepareColumnsList(TArray<TWeakObjectPtr<const UScriptStruct>>& Columns)
{
	Columns.Sort(
		[](const TWeakObjectPtr<const UScriptStruct>& Lhs, const TWeakObjectPtr<const UScriptStruct>& Rhs)
		{
			return Lhs.GetWeakPtrTypeHash() < Rhs.GetWeakPtrTypeHash();
		});

	// Remove duplicates.
	for (int32 Index = 1; Index < Columns.Num(); )
	{
		if (Columns[Index - 1].GetWeakPtrTypeHash() != Columns[Index].GetWeakPtrTypeHash())
		{
			++Index;
		}
		else
		{
			Columns.RemoveAt(Index);
		}
	}
}



//
// FWidgetFactory
//

UTypedElementDatabaseUi::FWidgetFactory::FWidgetFactory(const UScriptStruct* InConstructor)
	: Constructor(InConstructor)
{
}

UTypedElementDatabaseUi::FWidgetFactory::FWidgetFactory(TUniquePtr<FTypedElementWidgetConstructor>&& InConstructor)
	: Constructor(MoveTemp(InConstructor))
{
	checkf(std::get<TUniquePtr<FTypedElementWidgetConstructor>>(Constructor)->GetTypeInfo(), 
		TEXT("Widget constructor registered that didn't contain valid type information."));
}

UTypedElementDatabaseUi::FWidgetFactory::FWidgetFactory(const UScriptStruct* InConstructor, 
	TArray<TWeakObjectPtr<const UScriptStruct>>&& InColumns)
	: Columns(MoveTemp(InColumns))
	, Constructor(InConstructor)
{
}

UTypedElementDatabaseUi::FWidgetFactory::FWidgetFactory(TUniquePtr<FTypedElementWidgetConstructor>&& InConstructor, 
	TArray<TWeakObjectPtr<const UScriptStruct>>&& InColumns)
	: Columns(MoveTemp(InColumns))
	, Constructor(MoveTemp(InConstructor))
{
	checkf(std::get<TUniquePtr<FTypedElementWidgetConstructor>>(Constructor)->GetTypeInfo(),
		TEXT("Widget constructor registered that didn't contain valid type information."));
}
