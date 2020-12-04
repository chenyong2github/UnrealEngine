// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendBaseClasses.h"
#include "MetasoundDataReference.h"

#include "MetasoundFrontendRegistries.h"



namespace Metasound
{
	namespace Frontend
	{
		

		TMap<FNodeRegistryKey, FNodeRegistryElement>& GetExternalNodeRegistry()
		{
			return FMetasoundFrontendRegistryContainer::Get()->GetExternalNodeRegistry();
		}

		TUniquePtr<INode> ConstructInputNode(const FName& InInputType, FInputNodeConstructorParams&& InParams)
		{
			return FMetasoundFrontendRegistryContainer::Get()->ConstructInputNode(InInputType, MoveTemp(InParams));
		}

		TUniquePtr<INode> ConstructOutputNode(const FName& InOutputType, const FOutputNodeConstrutorParams& InParams)
		{
			return FMetasoundFrontendRegistryContainer::Get()->ConstructOutputNode(InOutputType, InParams);
		}

		TUniquePtr<INode> ConstructExternalNode(const FName& InNodeType, uint32 InNodeHash, const FNodeInitData& InInitData)
		{
			return FMetasoundFrontendRegistryContainer::Get()->ConstructExternalNode(InNodeType, InNodeHash, InInitData);
		}

		void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, bool InValue)
		{
			OutDescription.LiteralType = EMetasoundLiteralType::Bool;
			OutDescription.AsBool = InValue;
			OutDescription.AsInteger = 0;
			OutDescription.AsFloat = 0.0f;
			OutDescription.AsString.Empty();
			OutDescription.AsUObject = nullptr;
			OutDescription.AsUObjectArray.Empty();
		}

		void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, int32 InValue)
		{
			OutDescription.LiteralType = EMetasoundLiteralType::Integer;
			OutDescription.AsBool = false;
			OutDescription.AsInteger = InValue;
			OutDescription.AsFloat = 0.0f;
			OutDescription.AsString.Empty();
			OutDescription.AsUObject = nullptr;
			OutDescription.AsUObjectArray.Empty();
		}

		void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, float InValue)
		{
			OutDescription.LiteralType = EMetasoundLiteralType::Float;
			OutDescription.AsBool = false;
			OutDescription.AsInteger = 0;
			OutDescription.AsFloat = InValue;
			OutDescription.AsString.Empty();
			OutDescription.AsUObject = nullptr;
			OutDescription.AsUObjectArray.Empty();
		}

		void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, const FString& InValue)
		{
			OutDescription.LiteralType = EMetasoundLiteralType::String;
			OutDescription.AsBool = false;
			OutDescription.AsInteger = 0;
			OutDescription.AsFloat = 0.0f;
			OutDescription.AsString = InValue;
			OutDescription.AsUObject = nullptr;
			OutDescription.AsUObjectArray.Empty();
		}

		void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, UObject* InValue)
		{
			OutDescription.LiteralType = EMetasoundLiteralType::UObject;
			OutDescription.AsBool = false;
			OutDescription.AsInteger = 0;
			OutDescription.AsFloat = 0.0f;
			OutDescription.AsString.Empty();
			OutDescription.AsUObject = InValue;
			OutDescription.AsUObjectArray.Empty();

		}

		void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, const TArray<UObject*>& InValue)
		{
			OutDescription.LiteralType = EMetasoundLiteralType::UObjectArray;
			OutDescription.AsBool = false;
			OutDescription.AsInteger = 0;
			OutDescription.AsFloat = 0.0f;
			OutDescription.AsString.Empty();
			OutDescription.AsUObject = nullptr;
			OutDescription.AsUObjectArray = InValue;
		}

		void ClearLiteralDescription(FMetasoundLiteralDescription& OutDescription)
		{
			OutDescription.LiteralType = EMetasoundLiteralType::None;
			OutDescription.AsBool = false;
			OutDescription.AsInteger = 0;
			OutDescription.AsFloat = 0.0f;
			OutDescription.AsString.Empty();
			OutDescription.AsUObject = nullptr;
			OutDescription.AsUObjectArray.Empty();
		}

		FDataTypeLiteralParam GetLiteralParamForDataType(FName InDataType, const FMetasoundLiteralDescription& InDescription)
		{
			EMetasoundLiteralType LiteralType = InDescription.LiteralType;
			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();

			switch (LiteralType)
			{
				case EMetasoundLiteralType::None:
				{
					return GetDefaultParamForDataType(InDataType);
				}
				case EMetasoundLiteralType::Bool:
				{
					if (!Registry->DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::Boolean))
					{
						return FDataTypeLiteralParam::InvalidParam();
					}
					else
					{
						return FDataTypeLiteralParam(InDescription.AsBool);
					}
				}
				case EMetasoundLiteralType::Float:
				{
					if (!Registry->DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::Float))
					{
						return FDataTypeLiteralParam::InvalidParam();
					}
					else
					{
						return FDataTypeLiteralParam(InDescription.AsFloat);
					}
				}
				case EMetasoundLiteralType::Integer:
				{
					if (!Registry->DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::Integer))
					{
						return FDataTypeLiteralParam::InvalidParam();
					}
					else
					{
						return FDataTypeLiteralParam(InDescription.AsInteger);
					}
				}
				case EMetasoundLiteralType::String:
				{
					if (!Registry->DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::String))
					{
						return FDataTypeLiteralParam::InvalidParam();
					}
					else
					{
						return FDataTypeLiteralParam(InDescription.AsString);
					}
				}
				case EMetasoundLiteralType::UObject:
				{
					if (!Registry->DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::UObjectProxy))
					{
						return FDataTypeLiteralParam::InvalidParam();
					}
					else
					{
						return Registry->GenerateLiteralForUObject(InDataType, InDescription.AsUObject);
					}
				}
				case EMetasoundLiteralType::UObjectArray:
				{
					if (!Registry->DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::UObjectProxyArray))
					{
						return FDataTypeLiteralParam::InvalidParam();
					}
					else
					{
						return Registry->GenerateLiteralForUObjectArray(InDataType, InDescription.AsUObjectArray);
					}
				}
				case EMetasoundLiteralType::Invalid:
				default:
				{
					return FDataTypeLiteralParam::InvalidParam();
				}
			}
		}

		bool DoesDataTypeSupportLiteralType(FName InDataType, EMetasoundLiteralType InLiteralType)
		{
			switch (InLiteralType)
			{
			case EMetasoundLiteralType::None:
				return DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::None);
			case EMetasoundLiteralType::Bool:
				return DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::Boolean);
			case EMetasoundLiteralType::Float:
				return DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::Float);
			case EMetasoundLiteralType::Integer:
				return DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::Integer);
			case EMetasoundLiteralType::String:
				return DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::String);
			case EMetasoundLiteralType::UObject:
				return DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::UObjectProxy);
			case EMetasoundLiteralType::UObjectArray:
				return DoesDataTypeSupportLiteralType(InDataType, ELiteralArgType::UObjectProxyArray);
			case EMetasoundLiteralType::Invalid:
			default:
				return false;
			}
		}

		bool DoesDataTypeSupportLiteralType(FName InDataType, ELiteralArgType InLiteralType)
		{
			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();

			return Registry->DoesDataTypeSupportLiteralType(InDataType, InLiteralType);
		}

		Metasound::FDataTypeLiteralParam GetLiteralParam(const FMetasoundLiteralDescription& InDescription)
		{
			EMetasoundLiteralType LiteralType = InDescription.LiteralType;

			switch (LiteralType)
			{
				case EMetasoundLiteralType::Bool:
				{
					return FDataTypeLiteralParam(InDescription.AsBool);
				}
				case EMetasoundLiteralType::Float:
				{
					return FDataTypeLiteralParam(InDescription.AsFloat);
				}
				case EMetasoundLiteralType::Integer:
				{
					return FDataTypeLiteralParam(InDescription.AsInteger);
				}
				case EMetasoundLiteralType::String:
				{
					return FDataTypeLiteralParam(InDescription.AsString);
				}
				case EMetasoundLiteralType::UObject:
				case EMetasoundLiteralType::UObjectArray:
				case EMetasoundLiteralType::None:
				case EMetasoundLiteralType::Invalid:
				default:
				{
					return FDataTypeLiteralParam::InvalidParam();
				}
			}
		}

		Metasound::FDataTypeLiteralParam GetDefaultParamForDataType(FName InDataType)
		{
			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();

			ELiteralArgType DesiredArgType = Registry->GetDesiredLiteralTypeForDataType(InDataType);

			switch (DesiredArgType)
			{
				case Metasound::ELiteralArgType::Boolean:
				{
					return FDataTypeLiteralParam(false);
				}
				case Metasound::ELiteralArgType::Integer:
				{
					return FDataTypeLiteralParam(0);
				}
				case Metasound::ELiteralArgType::Float:
				{
					return FDataTypeLiteralParam(0.0f);
				}
				case Metasound::ELiteralArgType::String:
				{
					return FDataTypeLiteralParam(FString());
				}
				case Metasound::ELiteralArgType::UObjectProxy:
				case Metasound::ELiteralArgType::UObjectProxyArray:
				case Metasound::ELiteralArgType::None:
				{
					return FDataTypeLiteralParam();
				}
				case Metasound::ELiteralArgType::Invalid:
				default:
				{
					return FDataTypeLiteralParam::InvalidParam();
				}
			}

		}

		ITransactable::ITransactable(uint32 InUndoLimit, TWeakObjectPtr<UObject> InOwningAsset /* = nullptr */)
			: UndoLimit(InUndoLimit)
			, OwningAsset(InOwningAsset)
		{
		}

		bool ITransactable::Undo()
		{
			if (UndoTransactableStack.Num() == 0)
			{
				return false;
			}

			if (TSharedPtr<ITransactable> Owner = OwningTransactable.Pin())
			{
				Owner->DiscardUndoFromOwnedTransactable(AsShared());
			}

			TSharedPtr<ITransactable> Transactable = UndoTransactableStack.Pop().Pin();
			TSharedPtr<ITransactable> PreviousTransactable = AsShared();

			RedoTransactableStack.Push(Transactable);

			while(Transactable)
			{
				if (Transactable == PreviousTransactable)
				{
					bool bUndoSucceeded = Transactable->PerformLocalUndo();
					if (bUndoSucceeded && OwningAsset.IsValid())
					{
						OwningAsset->MarkPackageDirty();
					}

					return bUndoSucceeded;
				}

				PreviousTransactable = Transactable;
				Transactable = Transactable->UndoTransactableStack.Pop().Pin();
				PreviousTransactable->RedoTransactableStack.Push(Transactable);
			}

			return false;
		}

		bool ITransactable::Redo()
		{
			if (RedoTransactableStack.Num() == 0)
			{
				return false;
			}

			if (TSharedPtr<ITransactable> Owner = OwningTransactable.Pin())
			{
				Owner->DiscardRedoFromOwnedTransactable(AsShared());
			}

			TSharedPtr<ITransactable> Transactable = RedoTransactableStack.Pop().Pin();
			TSharedPtr<ITransactable> PreviousTransactable = AsShared();

			UndoTransactableStack.Push(Transactable);

			while (Transactable)
			{
				if (Transactable == PreviousTransactable)
				{
					const bool bRedoSucceeded = Transactable->PerformLocalRedo();
					if (bRedoSucceeded && OwningAsset.IsValid())
					{
						OwningAsset->MarkPackageDirty();
					}

					return bRedoSucceeded;
				}

				PreviousTransactable = Transactable;
				Transactable = Transactable->RedoTransactableStack.Pop().Pin();
				PreviousTransactable->UndoTransactableStack.Push(Transactable);
			}

			return false;
		}

		void ITransactable::CommitTransaction(FReversibleTransaction&& InTransactionDescription)
		{
			if (((uint32)LocalUndoTransactionStack.Num()) >= UndoLimit)
			{
				// Discard the oldest undo action.
				LocalUndoTransactionStack.RemoveAt(0);
			}

			LocalUndoTransactionStack.Push(MoveTemp(InTransactionDescription));
			RedoTransactableStack.Reset();

			TWeakPtr<ITransactable> WeakThisPtr = AsShared();
			UndoTransactableStack.Push(WeakThisPtr);

			if (TSharedPtr<ITransactable> Owner = OwningTransactable.Pin())
			{
				Owner->PushUndoFromOwnedTransactable(WeakThisPtr);
			}

			if (OwningAsset.IsValid())
			{
				OwningAsset->MarkPackageDirty();
			}
		}

		bool ITransactable::RegisterOwningTransactable(ITransactable& InOwningTransactable)
		{
			// Sanity check that this will not create a cycle.
			while (TSharedPtr<ITransactable> Owner = InOwningTransactable.OwningTransactable.Pin())
			{
				if (Owner.Get() == this)
				{
					return false;
				}
			}

			OwningTransactable = InOwningTransactable.AsShared();
			return true;
		}


		bool ITransactable::PerformLocalUndo()
		{
			if (LocalUndoTransactionStack.Num() == 0)
			{
				return false;
			}

			FReversibleTransaction Transaction = LocalUndoTransactionStack.Pop();
			bool bResult = Transaction.UndoTransaction();
			if (bResult)
			{
				LocalRedoTransactionStack.Push(MoveTemp(Transaction));
			}

			return bResult;
		}

		bool ITransactable::PerformLocalRedo()
		{
			if (LocalRedoTransactionStack.Num() == 0)
			{
				return false;
			}

			FReversibleTransaction Transaction = LocalRedoTransactionStack.Pop();
			bool bResult = Transaction.RedoTransaction();
			if (bResult)
			{
				LocalUndoTransactionStack.Push(MoveTemp(Transaction));
			}

			return bResult;
		}

		bool ITransactable::DiscardUndoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable)
		{
			// NOTE: This relies on TArray.Push adding an element to the end of the array.
			// Start at the end of the array and work backwards until we find the most recent undo operation for this transactable.
			for (int32 Index = UndoTransactableStack.Num() - 1; Index >= 0; Index--)
			{
				if (UndoTransactableStack[Index] == InOwnedTransactable)
				{
					// Discard, but preserve the stack order.
					UndoTransactableStack.RemoveAt(Index);
					return true;
				}
			}

			return false;
		}

		bool ITransactable::DiscardRedoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable)
		{
			// NOTE: This relies on TArray.Push adding an element to the end of the array.
			// Start at the end of the array and work backwards until we find the most recent undo operation for this transactable.
			for (int32 Index = RedoTransactableStack.Num() - 1; Index >= 0; Index--)
			{
				if (RedoTransactableStack[Index] == InOwnedTransactable)
				{
					// Discard, but preserve the stack order.
					RedoTransactableStack.RemoveAt(Index);
					return true;
				}
			}

			return false;
		}

		bool ITransactable::PushUndoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable)
		{
			UndoTransactableStack.Push(MoveTemp(InOwnedTransactable));
			RedoTransactableStack.Reset();

			return true;
		}



		bool ITransactable::PushRedoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable)
		{
			RedoTransactableStack.Push(MoveTemp(InOwnedTransactable));
			DiscardUndoFromOwnedTransactable(InOwnedTransactable);

			return true;
		}

		FDescriptionAccessPoint::FDescriptionAccessPoint(FMetasoundDocument& InRootDocument)
			: RootDocument(InRootDocument)
		{
		}

		FMetasoundDocument& FDescriptionAccessPoint::GetRoot()
		{
			return RootDocument;
		}

		FMetasoundClassDescription* FDescriptionAccessPoint::GetClassFromPath(const FDescPath& InPathFromRoot)
		{
			FDescPath CurrentPath = InPathFromRoot;

			FMetasoundDescriptionPtr Ptr;
			Ptr.Set<FMetasoundDocument*>(&RootDocument);
			FDescriptionUnwindStep CurrentStep = { Ptr, Path::EDescType::Document };

			while (CurrentPath.Num() != 0 && CurrentStep.Type != Path::EDescType::Invalid)
			{
				CurrentStep = GoToNext(CurrentPath, CurrentStep);
			}

			if (ensureAlwaysMsgf(CurrentStep.Type == Path::EDescType::Class && CurrentPath.Num() == 0, TEXT("Couldn't resolve part of the path.")))
			{
				return CurrentStep.DescriptionStructPtr.Get<FMetasoundClassDescription*>();
			}
			else
			{
				return nullptr;
			}
		}

		FMetasoundNodeDescription* FDescriptionAccessPoint::GetNodeFromPath(const FDescPath& InPathFromRoot)
		{
			FDescPath CurrentPath = InPathFromRoot;
			
			FMetasoundDescriptionPtr Ptr;
			Ptr.Set<FMetasoundDocument*>(&RootDocument);
			FDescriptionUnwindStep CurrentStep = { Ptr, Path::EDescType::Document };

			while (CurrentPath.Num() != 0 && CurrentStep.Type != Path::EDescType::Invalid)
			{
				CurrentStep = GoToNext(CurrentPath, CurrentStep);
			}

			if (ensureAlwaysMsgf(CurrentPath.Num() == 0, TEXT("Couldn't resolve part of the path.")))
			{
				return CurrentStep.DescriptionStructPtr.Get<FMetasoundNodeDescription*>();
			}
			else
			{
				return nullptr;
			}
		}

		FMetasoundGraphDescription* FDescriptionAccessPoint::GetGraphFromPath(const FDescPath& InPathFromRoot)
		{
			FDescPath CurrentPath = InPathFromRoot;
			
			FMetasoundDescriptionPtr Ptr;
			Ptr.Set<FMetasoundDocument*>(&RootDocument);
			FDescriptionUnwindStep CurrentStep = { Ptr, Path::EDescType::Document };

			while (CurrentPath.Num() != 0 && CurrentStep.Type != Path::EDescType::Invalid)
			{
				CurrentStep = GoToNext(CurrentPath, CurrentStep);
			}

			if (ensureAlwaysMsgf(CurrentPath.Num() == 0, TEXT("Couldn't resolve part of the path.")))
			{
				return CurrentStep.DescriptionStructPtr.Get<FMetasoundGraphDescription*>();
			}
			else
			{
				return nullptr;
			}
		}

		FMetasoundInputDescription* FDescriptionAccessPoint::GetInputFromPath(const FDescPath& InPathFromRoot)
		{
			FDescPath CurrentPath = InPathFromRoot;
			
			FMetasoundDescriptionPtr Ptr;
			Ptr.Set<FMetasoundDocument*>(&RootDocument);
			FDescriptionUnwindStep CurrentStep = { Ptr, Path::EDescType::Document };

			while (CurrentPath.Num() != 0 && CurrentStep.Type != Path::EDescType::Invalid)
			{
				CurrentStep = GoToNext(CurrentPath, CurrentStep);
			}

			if (ensureAlwaysMsgf(CurrentPath.Num() == 0, TEXT("Couldn't resolve part of the path.")))
			{
				return CurrentStep.DescriptionStructPtr.Get<FMetasoundInputDescription*>();
			}
			else
			{
				return nullptr;
			}
		}

		FMetasoundOutputDescription* FDescriptionAccessPoint::GetOutputFromPath(const FDescPath& InPathFromRoot)
		{
			FDescPath CurrentPath = InPathFromRoot;
			
			FMetasoundDescriptionPtr Ptr;
			Ptr.Set<FMetasoundDocument*>(&RootDocument);
			FDescriptionUnwindStep CurrentStep = { Ptr, Path::EDescType::Document };

			while (CurrentPath.Num() != 0 && CurrentStep.Type != Path::EDescType::Invalid)
			{
				CurrentStep = GoToNext(CurrentPath, CurrentStep);
			}

			if (ensureAlwaysMsgf(CurrentPath.Num() == 0, TEXT("Couldn't resolve part of the path.")))
			{
				return CurrentStep.DescriptionStructPtr.Get<FMetasoundOutputDescription*>();
			}
			else
			{
				return nullptr;
			}
		}

		FMetasoundClassMetadata* FDescriptionAccessPoint::GetMetadataFromPath(const FDescPath& InPathFromRoot)
		{
			FDescPath CurrentPath = InPathFromRoot;
			
			FMetasoundDescriptionPtr Ptr;
			Ptr.Set<FMetasoundDocument*>(&RootDocument);
			FDescriptionUnwindStep CurrentStep = { Ptr, Path::EDescType::Document };

			while (CurrentPath.Num() != 0 && CurrentStep.Type != Path::EDescType::Invalid)
			{
				CurrentStep = GoToNext(CurrentPath, CurrentStep);
			}

			if (ensureAlwaysMsgf(CurrentPath.Num() == 0, TEXT("Couldn't resolve part of the path.")))
			{
				return CurrentStep.DescriptionStructPtr.Get<FMetasoundClassMetadata*>();
			}
			else
			{
				return nullptr;
			}
		}

		Metasound::Frontend::FClassDependencyIDs* FDescriptionAccessPoint::GetClassDependencyIDsFromPath(const FDescPath& InPathFromRoot)
		{
			FDescPath CurrentPath = InPathFromRoot;
			
			FMetasoundDescriptionPtr Ptr;
			Ptr.Set<FMetasoundDocument*>(&RootDocument);
			FDescriptionUnwindStep CurrentStep = { Ptr, Path::EDescType::Document };

			while (CurrentPath.Num() != 0 && CurrentStep.Type != Path::EDescType::Invalid)
			{
				CurrentStep = GoToNext(CurrentPath, CurrentStep);
			}

			if (ensureAlwaysMsgf(CurrentStep.Type != Path::EDescType::Invalid && CurrentPath.Num() == 0, TEXT("Couldn't resolve part of the path.")))
			{
				return CurrentStep.DescriptionStructPtr.Get<FClassDependencyIDs*>();
			}
			else
			{
				return nullptr;
			}
		}

		FDescriptionAccessPoint::FDescriptionUnwindStep FDescriptionAccessPoint::GoToNextFromDocument(FMetasoundDocument& InDocument, FDescPath& InPath, const Path::FElement& InNext)
		{
			switch (InNext.CurrentDescType)
			{
				case Path::EDescType::Document:
				{
					FDescriptionUnwindStep UnwindStep;
					UnwindStep.DescriptionStructPtr.Set<FMetasoundDocument*>(&InDocument);
					UnwindStep.Type = Path::EDescType::Document;
					return UnwindStep;
				}
				case Path::EDescType::Class:
				{
					FDescriptionUnwindStep UnwindStep;
					UnwindStep.DescriptionStructPtr.Set<FMetasoundClassDescription*>(&(InDocument.RootClass));
					UnwindStep.Type = Path::EDescType::Class;
					return UnwindStep;
				}
				case Path::EDescType::DocDependencies:
				{
					// The next element in a path after Dependencies will always be the name of the dependency.
					Path::FElement DependencyElement = InPath.Path[0];
					InPath.Path.RemoveAt(0);
					if (!ensureAlwaysMsgf(DependencyElement.CurrentDescType == Path::EDescType::Class, TEXT("Invalid path set up.")))
					{
						return FDescriptionUnwindStep::CreateInvalid();
					}

					FString& DependencyName = DependencyElement.LookupName;

					int32 DependencyID = DependencyElement.LookupID;
					
					if (!ensureAlwaysMsgf(DependencyID != INDEX_NONE || DependencyName.Len() > 0, TEXT("Path to a dependency did not include a valid ID or dependency name.")))
					{
						return FDescriptionUnwindStep::CreateInvalid();
					}

					if (DependencyID == FMetasoundClassDescription::RootClassID)
					{
						FDescriptionUnwindStep UnwindStep;
						UnwindStep.DescriptionStructPtr.Set<FMetasoundClassDescription*>(&(InDocument.RootClass));
						UnwindStep.Type = Path::EDescType::Class;
						return UnwindStep;
					}

					TArray<FMetasoundClassDescription>& DependenciesList = InDocument.Dependencies;

					// Dependencies can be looked up by ID or by name.
					if (DependencyID != INDEX_NONE)
					{
						// Scan the dependencies list for the matching lookup ID.
						for (FMetasoundClassDescription& Dependency : DependenciesList)
						{
							if (Dependency.UniqueID == DependencyID)
							{
								FDescriptionUnwindStep UnwindStep;
								UnwindStep.DescriptionStructPtr.Set<FMetasoundClassDescription*>(&Dependency);
								UnwindStep.Type = Path::EDescType::Class;
								return UnwindStep;
							}
						}
					}
					else
					{
						// TODO: remove this chunk of code in the "else{}" block. Not sure if it has to be here. 
						checkNoEntry();
						// fall back to scanning the dependencies list for the matching lookup name.
						for (FMetasoundClassDescription& Dependency : DependenciesList)
						{
							if (Dependency.Metadata.NodeName == DependencyName)
							{
								FDescriptionUnwindStep UnwindStep;
								UnwindStep.DescriptionStructPtr.Set<FMetasoundClassDescription*>(&Dependency);
								UnwindStep.Type = Path::EDescType::Class;
								return UnwindStep;
							}
						}
					}
					

					// If we reached the end of the Dependencies list and didn't find a match, ensure.
					ensureAlwaysMsgf(false, TEXT("Couldn't find dependency %s in path."), *DependencyName);
					return FDescriptionUnwindStep::CreateInvalid();
				}
			}

			checkNoEntry();
			return FDescriptionUnwindStep::CreateInvalid();
		}

		FDescriptionAccessPoint::FDescriptionUnwindStep FDescriptionAccessPoint::GoToNextFromClass(FMetasoundClassDescription& InClassDescription, FDescPath& InPath, const Path::FElement& InNext)
		{
			switch (InNext.CurrentDescType)
			{
				case Path::EDescType::Graph:
				{
					FDescriptionUnwindStep UnwindStep;
					UnwindStep.DescriptionStructPtr.Set<FMetasoundGraphDescription*>(&(InClassDescription.Graph));
					UnwindStep.Type = Path::EDescType::Graph;
					return UnwindStep;
				}
				case Path::EDescType::ClassDependencies:
				{
					FDescriptionUnwindStep UnwindStep;
					UnwindStep.DescriptionStructPtr.Set<FClassDependencyIDs*>(&(InClassDescription.DependencyIDs));
					UnwindStep.Type = Path::EDescType::ClassDependencies;
					return UnwindStep;
				}
				case Path::EDescType::Inputs:
				{
					// The next element after an Inputs element should always be the name of an input.
					Path::FElement InputElement = InPath.Path[0];
					InPath.Path.RemoveAt(0);
					if (!ensureAlwaysMsgf(InputElement.CurrentDescType == Path::EDescType::Input, TEXT("Invalid path set up.")))
					{
						return FDescriptionUnwindStep::CreateInvalid();
					}

					FString& InputName = InputElement.LookupName;

					// Scan the inputs list for the lookup name.
					TArray<FMetasoundInputDescription>& InputsList = InClassDescription.Inputs;
					for (FMetasoundInputDescription& Input : InputsList)
					{
						if (Input.Name == InputName)
						{
							FDescriptionUnwindStep UnwindStep;
							UnwindStep.DescriptionStructPtr.Set<FMetasoundInputDescription*>(&Input);
							UnwindStep.Type = Path::EDescType::Input;
							return UnwindStep;
						}
					}

					// If we reached the end of the Inputs list and didn't find a match, ensure.
					ensureAlwaysMsgf(false, TEXT("Couldn't find input %s in path."), *InputName);
					return FDescriptionUnwindStep::CreateInvalid();
				}
				case Path::EDescType::Outputs:
				{
					// The next element after an Outputs element should always be the name of an output.
					Path::FElement OutputElement = InPath.Path[0];
					InPath.Path.RemoveAt(0);
					if (!ensureAlwaysMsgf(OutputElement.CurrentDescType == Path::EDescType::Output, TEXT("Invalid path set up.")))
					{
						return FDescriptionUnwindStep::CreateInvalid();
					}
				
					FString& OutputName = OutputElement.LookupName;

					// Scan the outputs list for the lookup name.
					TArray<FMetasoundOutputDescription>& OutputsList = InClassDescription.Outputs;
					for (FMetasoundOutputDescription& Output : OutputsList)
					{
						if (Output.Name == OutputElement.LookupName)
						{
							FDescriptionUnwindStep UnwindStep;
							UnwindStep.DescriptionStructPtr.Set<FMetasoundOutputDescription*>(&Output);
							UnwindStep.Type = Path::EDescType::Output;
							return UnwindStep;
						}
					}

					// If we reached the end of the Inputs list and didn't find a match, ensure.
					ensureAlwaysMsgf(false, TEXT("Couldn't find output %s in path."), *InNext.LookupName);
					return FDescriptionUnwindStep::CreateInvalid();
				}
				case Path::EDescType::Metadata:
				{
					FDescriptionUnwindStep UnwindStep;
					UnwindStep.DescriptionStructPtr.Set<FMetasoundClassMetadata*>(&(InClassDescription.Metadata));
					UnwindStep.Type = Path::EDescType::Metadata;

					return UnwindStep;
				}
				default:
				{
					ensureAlwaysMsgf(false, TEXT("Invalid path- Tried to path directly from a Class Description to a type that wasn't a direct memember of the Class"));
					return FDescriptionUnwindStep::CreateInvalid();
				}
			}
		}


		FDescriptionAccessPoint::FDescriptionUnwindStep FDescriptionAccessPoint::GoToNext(FDescPath& InPath, FDescriptionUnwindStep InElement)
		{
			if (!ensureMsgf(InPath.Path.Num() != 0, TEXT("Attempted to unwind an empty path.")))
			{
				return FDescriptionUnwindStep::CreateInvalid();
			}

			Path::FElement NextStep = InPath.Path[0];

			InPath.Path.RemoveAt(0);

			switch (InElement.Type)
			{
				case Path::EDescType::Document:
				{
					FMetasoundDocument* Document = InElement.DescriptionStructPtr.Get<FMetasoundDocument*>();
					return GoToNextFromDocument(*Document, InPath, NextStep);
					break;
				}
				case Path::EDescType::Class:
				{
					FMetasoundClassDescription* ClassDescription = InElement.DescriptionStructPtr.Get<FMetasoundClassDescription*>();
					return GoToNextFromClass(*ClassDescription, InPath, NextStep);
					break;
				}
				case Path::EDescType::Graph:
				{
					if (!ensureAlwaysMsgf(NextStep.CurrentDescType == Path::EDescType::Nodes, TEXT("Invalid path. the Graph description only contains the Nodes list.")))
					{
						return FDescriptionUnwindStep::CreateInvalid();
					}

					FMetasoundGraphDescription* GraphDescription = InElement.DescriptionStructPtr.Get<FMetasoundGraphDescription*>();
				
					if (!ensureAlwaysMsgf(InPath.Path.Num() != 0, TEXT("Incomplete path! path stopped at Nodes list without specifying a node ID.")))
					{
						return FDescriptionUnwindStep::CreateInvalid();
					}

					Path::FElement NodeElement = InPath.Path[0];
					InPath.Path.RemoveAt(0);

					if (!ensureAlwaysMsgf(NodeElement.CurrentDescType == Path::EDescType::Node, TEXT("Invalid path! a Nodes element must always be followed by a Node ID.")))
					{
						return FDescriptionUnwindStep::CreateInvalid();
					}
				
					int32 NodeID = NodeElement.LookupID;

					TArray<FMetasoundNodeDescription>& NodeList = GraphDescription->Nodes;
					for (FMetasoundNodeDescription& Node : NodeList)
					{
						if (Node.UniqueID == NodeID)
						{
							FDescriptionUnwindStep UnwindStep;
							UnwindStep.DescriptionStructPtr.Set<FMetasoundNodeDescription*>(&Node);
							UnwindStep.Type = Path::EDescType::Node;
							return UnwindStep;
						}
					}
					break;
				}
				case Path::EDescType::Inputs:
				{
					ensureAlwaysMsgf(false, TEXT("Invalid path. Inputs will always be pathed directly after a Class."));
					return FDescriptionUnwindStep::CreateInvalid();
				}
				case Path::EDescType::DocDependencies:
				{
					ensureAlwaysMsgf(false, TEXT("Invalid path. Document dependencies should always follow after a Document and should always list a specific dependency by ID or name."));
					return FDescriptionUnwindStep::CreateInvalid();
				}
				case Path::EDescType::Input:
				case Path::EDescType::Output:
				case Path::EDescType::Node:
				case Path::EDescType::Metadata:
				default:
				{
					ensureAlwaysMsgf(false, TEXT("Invalid path. Input, Output, Node, and Metadata don't have any child elements."));
					return FDescriptionUnwindStep::CreateInvalid();
				}
			}

			// if we ever hit this, we likely missed a return on one of these branches.
			checkNoEntry();
			return FDescriptionUnwindStep::CreateInvalid();
		}

		FDescPath Path::GetPathToClassForNode(FDescPath InPathForNode, FString& InNodeName)
		{
			return FDescPath()[EFromDocument::ToDependencies][*InNodeName];
		}


		FDescPath Path::GetOwningClassDescription(FDescPath InPathForGraph)
		{
			// Backtrack from the end of the path until we find a Class element.
			for (int32 Level = InPathForGraph.Path.Num() - 1; Level >= 0; Level--)
			{
				if (InPathForGraph.Path[Level].CurrentDescType == Path::EDescType::Class)
				{
					return InPathForGraph;
				}
				else
				{
					InPathForGraph.Path.RemoveAt(InPathForGraph.Path.Num() - 1);
				}
			}

			return InPathForGraph;
		}

		FDescPath Path::GetDependencyPath(int32 InDependencyID)
		{
			return FDescPath()[Path::EFromDocument::ToDependencies][InDependencyID];
		}

		FDescPath Path::GetInputDescriptionPath(FDescPath InPathForInputNode, const FString& InputName)
		{
			return (InPathForInputNode << 3)[Path::EFromClass::ToInputs][*InputName];
		}

		FDescPath Path::GetOutputDescriptionPath(FDescPath InPathForOutputNode, const FString& OutputName)
		{
			return (InPathForOutputNode << 3)[Path::EFromClass::ToOutputs][*OutputName];
		}

		FDescPath Path::GetOuterGraphPath(FDescPath InPath)
		{
			// Unwind element by element until we hit a graph.
			while (InPath.Path.Num() > 1 && InPath.Path.Last().CurrentDescType != Path::EDescType::Graph)
			{
				InPath.Path.Pop();
			}

			return InPath;
		}

		FString Path::GetPrintableString(FDescPath InPath)
		{
			FString OutString = FString(TEXT("//"));
			for (FElement& PathElement : InPath.Path)
			{
				switch (PathElement.CurrentDescType)
				{
					case EDescType::Document:
					{
						OutString += TEXT("Document/");
						break;
					}
					case EDescType::Class:
					{
						OutString += TEXT("Class/");
						break;
					}
					case EDescType::DocDependencies:
					{
						OutString += TEXT("Dependencies(");

						if (PathElement.LookupID != INDEX_NONE)
						{
							OutString.AppendInt(PathElement.LookupID);
						}
						else
						{
							OutString += PathElement.LookupName;
						}
						
						OutString += TEXT(")/");
						break;
					}
					case EDescType::ClassDependencies:
					{
						OutString += TEXT("Dependencies(");
						OutString.AppendInt(PathElement.LookupID);
						OutString += TEXT(")/");
						break;
					}
					case EDescType::Graph:
					{
						OutString += TEXT("Graph/");
						break;
					}
					case EDescType::Inputs:
					{
						OutString += TEXT("Inputs/");
						break;
					}
					case EDescType::Input:
					{
						OutString += TEXT("Input(");
						OutString += PathElement.LookupName;
						OutString += TEXT(")/");
						break;
					}
					case EDescType::Metadata:
					{
						OutString += TEXT("Metadata/");
						break;
					}
					case EDescType::Nodes:
					{
						OutString += TEXT("Nodes/");
						break;
					}
					case EDescType::Node:
					{
						OutString += TEXT("Node(");
						OutString.AppendInt(PathElement.LookupID);
						OutString += TEXT(")/");
						break;
					}
					case EDescType::Outputs:
					{
						OutString += TEXT("Outputs/");
						break;
					}
					case EDescType::Output:
					{
						OutString += TEXT("Output(");
						OutString += PathElement.LookupName;
						OutString += TEXT(")/");
						break;
					}
					default:
					{
						OutString += TEXT("Unknown/");
						break;
					}
				}
			}

			return OutString;
		}

		

		void InitializeFrontend()
		{
			FMetasoundFrontendRegistryContainer::Get()->InitializeFrontend();
		}
	}
}


