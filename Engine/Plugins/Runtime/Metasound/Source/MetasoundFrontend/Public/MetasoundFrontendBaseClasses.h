// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"
#include "MetasoundFrontendDataLayout.h"
#include "MetasoundFrontendRegistries.h"



namespace Metasound
{
	namespace Frontend
	{
		// This is called by FMetasoundFrontendModule, and flushes any node or datatype registration that was done prior to boot.
		void InitializeFrontend();

		/** Use this to get a reference to the static registry of all nodes implemented in C++. */
		METASOUNDFRONTEND_API TMap<FNodeRegistryKey, FNodeRegistryElement>& GetExternalNodeRegistry();

		// Convenience functions to create an INode corresponding to a specific input or output for a metasound graph.
		// @returns nullptr if the type given wasn't found.
		METASOUNDFRONTEND_API TUniquePtr<INode> ConstructInputNode(const FName& InInputType, FInputNodeConstructorParams&& InParams);
		METASOUNDFRONTEND_API TUniquePtr<INode> ConstructOutputNode(const FName& InOutputType, const FOutputNodeConstrutorParams& InParams);

		// Convenience functions to create an INodeB corresponding to a specific externally declared node type.
		// InNodeType and InNodeHash can be retrieved from the FNodeClassInfo generated from the node registry queries in the metasound frontend (GetAllAvailableNodeClasses, GetAllNodeClassesInNamespace, etc.)
		// @returns nullptr if the type given wasn't found.
		METASOUNDFRONTEND_API TUniquePtr<INode> ConstructExternalNode(const FName& InNodeType, uint32 InNodeHash, const FNodeInitData& InInitData);

		// Utility functions for setting serialized literals.
		METASOUNDFRONTEND_API void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, bool InValue);
		METASOUNDFRONTEND_API void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, int32 InValue);
		METASOUNDFRONTEND_API void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, float InValue);
		METASOUNDFRONTEND_API void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, const FString& InValue);
		METASOUNDFRONTEND_API void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, UObject* InValue);
		METASOUNDFRONTEND_API void SetLiteralDescription(FMetasoundLiteralDescription& OutDescription, const TArray<UObject*>& InValue);
		
		METASOUNDFRONTEND_API void ClearLiteralDescription(FMetasoundLiteralDescription& OutDescription);

		// Utility functions for building a ::Metasound::FDataInitParam corresponding to a literal.

		// Return the literal description parsed into a init param. 
		// @Returns an invalid init param if the data type couldn't be found, or if the literal type was incompatible with the data type.
		METASOUNDFRONTEND_API FDataTypeLiteralParam GetLiteralParamForDataType(FName InDataType, const FMetasoundLiteralDescription& InDescription);

		METASOUNDFRONTEND_API bool DoesDataTypeSupportLiteralType(FName InDataType, EMetasoundLiteralType InLiteralType);
		METASOUNDFRONTEND_API bool DoesDataTypeSupportLiteralType(FName InDataType, ELiteralArgType InLiteralType);
		
		// Returns a literal param without any type checking.
		METASOUNDFRONTEND_API FDataTypeLiteralParam GetLiteralParam(const FMetasoundLiteralDescription& InDescription);

		// Returns the defaulted version of a literal param for the given data type.
		// @Returns an invalid init param if the data type couldn't be found.
		METASOUNDFRONTEND_API FDataTypeLiteralParam GetDefaultParamForDataType(FName InDataType);

		// Utility base class for classes that want to support naive multi-level undo/redo.
		class METASOUNDFRONTEND_API ITransactable : public TSharedFromThis<ITransactable>
		{
		public:

			ITransactable() = delete;
			ITransactable(uint32 InUndoLimit, TWeakObjectPtr<UObject> InOwningAsset = nullptr);

			/*
			 * Undo a single operation for this or any of it's owned transactables.
			 * @returns true on success.
			 */
			bool Undo();

			/*
			 * Redo a single operation for this or any of it's owned transactables.
			 * @returns true on success.
			 */
			bool Redo();

		protected:
			struct FReversibleTransaction
			{
				// Function that will be executed when Undo is called.
				// This should return true on success.
				TFunction<bool()> UndoTransaction;

				// Function that will be executed when Redo is called.
				// This should return true on success.
				TFunction<bool()> RedoTransaction;

				FReversibleTransaction(TFunction<bool()>&& InUndo, TFunction<bool()>&& InRedo)
					: UndoTransaction(MoveTemp(InUndo))
					, RedoTransaction(MoveTemp(InRedo))
				{
				}
			};

			// Commits an undoable/redoable transaction for this object.
			void CommitTransaction(FReversibleTransaction&& InTransactionDescription);
			
			// Use this function to denote an ITransactable that should consider this ITransactable part of it's undo roll.
			// For example, if an undoable transaction is committed to a FNodeHandle,
			// and you want FGraphHandle::Undo to be able to undo that transaction,
			// you would call FInputHandle::RegisterOwningTransactable(OwningGraphHandle). 
			// 
			// @returns true on success, false if registering this as an owning transaction would create a cycle.
			bool RegisterOwningTransactable(ITransactable& InOwningTransactable);

		private:

			// Execute the topmost transaction on the undo stack,
			// push it to redo stack,
			// and notify the OnwingTransactable.
			bool PerformLocalUndo();

			// Execute the topmost transaction on the redo stack,
			// push it to the undo stack,
			// and notify the OwningTransactable.
			bool PerformLocalRedo();

			// Called from a owned transactable to denote that it is popping an undo or redo action.
			// When this occurs, we discard the topmost call to the owned transactable from the UndoTransactableStack.
			bool DiscardUndoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable);
			bool DiscardRedoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable);

			bool PushUndoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable);
			bool PushRedoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable);

			// TODO: Make these ring buffers to avoid constantly moving elements around.
			TArray<TWeakPtr<ITransactable>> UndoTransactableStack;
			TArray<TWeakPtr<ITransactable>> RedoTransactableStack;
			TArray<FReversibleTransaction> LocalUndoTransactionStack;
			TArray<FReversibleTransaction> LocalRedoTransactionStack;

			// When CommitTransaction is called,
			// this ITransactable is pushed to the ITransactable::TransactableStack of this ITransactable object.
			TWeakPtr<ITransactable> OwningTransactable;

		protected:
			uint32 UndoLimit;

			// If this transactable is operating on a UAsset, this can be filled to mark it's corresponding package as dirty.
			TWeakObjectPtr<UObject> OwningAsset;
		};

		namespace Path
		{
			// This series of enums is used to denote 
			// the path to a specific element in the class description.

			// This enum is every possible Description struct we have.
			enum class EDescType : uint8
			{
				Document, // FMetasoundDocument
				Class, // FMetasoundClassDescription
				Metadata, // FMetasoundClassMetadata
				DocDependencies, // TArray<FMetasoundClassDescription>
				ClassDependencies,
				Graph, // 
				Nodes,
				Node,
				Inputs,
				Input,
				Outputs,
				Output,
				Invalid
			};


			// This series of enums is used to delineate which description types are direct children or other descripiton types.
			enum class EFromDocument : uint32
			{
				ToRootClass,
				ToDependencies
			};

			
			enum class EFromClass : uint32
			{
				ToMetadata,
				ToInputs,
				ToOutputs,
				ToDependencies,
				ToGraph,
				ToInvalid
			};

			enum class EFromGraph : uint32
			{
				ToNodes,
				Invalid
			};

			struct FElement
			{
				EDescType CurrentDescType;
				
				// This is only used if EDescType is Inputs or Outputs
				FString LookupName;

				// This is only used if EDescType == Node or Dependencies
				int32 LookupID;

				FElement()
					: CurrentDescType(EDescType::Invalid)
					, LookupID(FMetasoundNodeDescription::InvalidID)
				{}
			};
		}

		// This struct represents a selector for a specific element in a FMetasoundClassDescription.
		// Typically these will be used with FDescriptionAccessPoint::GetElementAtPath.
		struct FDescPath
		{
			TArray<Path::FElement> Path;

			FDescPath()
			{
				Path::FElement RootElement;
				RootElement.CurrentDescType = Path::EDescType::Document;
				Path.Add(RootElement);
			}

			int32 Num() const
			{
				return Path.Num();
			}

			bool IsValid() const
			{
				return Path.Num() > 0 &&
					Path[0].CurrentDescType == Path::EDescType::Document;
			}

			/** 
			 * Bracket operator used for setting up a path into a metasound class description.
			 * These can be chained, for example:
			 * FDescPath PathToNode = RootClass[Path::EFromClass::ToGraph][Path::EFromGraph::ToNodes][12];
			*/
			FDescPath operator[](Path::EFromDocument InElement) const
			{
				if (!ensureAlwaysMsgf(Path.Num() > 0, TEXT("Tried to append to a path that had no root.")))
				{
					return FDescPath();
				}

				// Copy the old path, and based on the last element in that path, append a new child element.
				FDescPath NewPath = *this;

				const Path::FElement& LastElementInPath = Path.Last();

				if(!ensureAlwaysMsgf(LastElementInPath.CurrentDescType == Path::EDescType::Document, TEXT("Tried to build an invalid path.")))
				{
					return FDescPath();
				}

				Path::FElement NewElement = Path::FElement();

				switch (InElement)
				{
					case Path::EFromDocument::ToRootClass:
					{
						NewElement.CurrentDescType = Path::EDescType::Class;
						break;
					}
					case Path::EFromDocument::ToDependencies:
					{
						NewElement.CurrentDescType = Path::EDescType::DocDependencies;
						break;
					}
				}

				NewPath.Path.Add(NewElement);
				return NewPath;
			}

			FDescPath operator[](Path::EFromClass InElement) const
			{
				if (!ensureAlwaysMsgf(Path.Num() > 0, TEXT("Tried to append to a path that had no root.")))
				{
					return FDescPath();
				}

				// Copy the old path, and based on the last element in that path, append a new child element.
				FDescPath NewPath = *this;

				const Path::FElement& LastElementInPath = Path.Last();

				if (!ensureAlwaysMsgf(LastElementInPath.CurrentDescType == Path::EDescType::Class, TEXT("Tried to build an invalid path.")))
				{
					return FDescPath();
				}

				Path::FElement NewElement = Path::FElement();

				switch (InElement)
				{
					case Path::EFromClass::ToDependencies:
					{
						NewElement.CurrentDescType = Path::EDescType::ClassDependencies;
						break;
					}
					case Path::EFromClass::ToGraph:
					{
						NewElement.CurrentDescType = Path::EDescType::Graph;
						break;
					}
					case Path::EFromClass::ToInputs:
					{
						NewElement.CurrentDescType = Path::EDescType::Inputs;
						break;
					}
					case Path::EFromClass::ToOutputs:
					{
						NewElement.CurrentDescType = Path::EDescType::Outputs;
						break;
					}
					case Path::EFromClass::ToInvalid:
					default:
					{
						ensureAlwaysMsgf(false, TEXT("Invalid path set up. Returning an empty path."));
						return FDescPath();
						break;
					}
				}

				NewPath.Path.Add(NewElement);
				return NewPath;
			}

			FDescPath operator[](Path::EFromGraph InElement) const
			{
				if (!ensureAlwaysMsgf(Path.Num() > 0, TEXT("Tried to append to a path that had no root.")))
				{
					return FDescPath();
				}

				// Copy the old path, and based on the last element in that path, append a new child element.
				FDescPath NewPath = *this;

				const Path::FElement& LastElementInPath = Path.Last();

				if (!ensureAlwaysMsgf(LastElementInPath.CurrentDescType == Path::EDescType::Graph, TEXT("Tried to build an invalid path.")))
				{
					return FDescPath();
				}

				Path::FElement NewElement = Path::FElement();

				switch (InElement)
				{
					case Path::EFromGraph::ToNodes:
					{
						NewElement.CurrentDescType = Path::EDescType::Nodes;
						break;
					}
					case Path::EFromGraph::Invalid:
					default:
					{
						ensureAlwaysMsgf(false, TEXT("Invalid path set up. Returning an empty path."));
						return FDescPath();
						break;
					}
				}

				NewPath.Path.Add(NewElement);
				return NewPath;
			}

			FDescPath operator[](int32 InElement) const
			{
				if (!ensureAlwaysMsgf(Path.Num() > 0, TEXT("Tried to append to a path that had no root.")))
				{
					return FDescPath();
				}

				// Copy the old path, and based on the last element in that path, append a new child element.
				FDescPath NewPath = *this;

				const Path::FElement& LastElementInPath = Path.Last();

				if (!ensureAlwaysMsgf(LastElementInPath.CurrentDescType == Path::EDescType::Nodes || LastElementInPath.CurrentDescType == Path::EDescType::DocDependencies, TEXT("Tried to build an invalid path.")))
				{
					return FDescPath();
				}

				Path::FElement NewElement = Path::FElement();
				NewElement.LookupID = InElement;

				if (LastElementInPath.CurrentDescType == Path::EDescType::Nodes)
				{
					NewElement.CurrentDescType = Path::EDescType::Node;
				}
				else if (LastElementInPath.CurrentDescType == Path::EDescType::ClassDependencies)
				{
					NewElement.CurrentDescType = Path::EDescType::DocDependencies;
				}
				else if (LastElementInPath.CurrentDescType == Path::EDescType::DocDependencies)
				{
					NewElement.CurrentDescType = Path::EDescType::Class;
				}

				NewPath.Path.Add(NewElement);
				return NewPath;
			}

			FDescPath operator [](const TCHAR* InElementName) const
			{
				if (!ensureMsgf(Path.Num() > 0, TEXT("Tried to append to a path that had no root.")))
				{
					return FDescPath();
				}

				// Copy the old path, and based on the last element in that path, append a new child element.
				FDescPath NewPath = *this;

				const Path::FElement& LastElementInPath = Path.Last();
				Path::FElement NewElement = Path::FElement();

				switch (LastElementInPath.CurrentDescType)
				{
					case Path::EDescType::DocDependencies:
					{
						ensureAlwaysMsgf(false, TEXT("Invalid path set up. DocDependencies for are accessed by using a int32."));
						return FDescPath();
						break;
					}
					case Path::EDescType::Inputs:
					{
						NewElement.CurrentDescType = Path::EDescType::Input;
						NewElement.LookupName = FString(InElementName);
						break;
					}
					case Path::EDescType::Outputs:
					{
						NewElement.CurrentDescType = Path::EDescType::Output;
						NewElement.LookupName = FString(InElementName);
						break;
					}
					/** Class descriptions and graph descriptions are navigated via enums. */
					case Path::EDescType::Class:
					case Path::EDescType::Graph:
					{
						ensureAlwaysMsgf(false, TEXT("Invalid path set up. Classes and Graphs are accessed by using the DescriptionPath::EClass and DesciptionPath::EGraph enums, respectively."));
						return FDescPath();
						break;
					}
					/** Dependencies for individual classes are navigated to via a int32. */
					case Path::EDescType::ClassDependencies:
					{
						ensureAlwaysMsgf(false, TEXT("Invalid path set up. Dependencies for individual classes are accessed by using a int32."));
						return FDescPath();
						break;;
					}
					/** Node list descriptions are navigated via a int32. */
					case Path::EDescType::Nodes:
					{
						ensureAlwaysMsgf(false, TEXT("Invalid path set up. Node lists are accessed by using a int32."));
						return FDescPath();
						break;
					}
					/** Individual inputs, outputs and nodes, as well as metadata, do not have any child elements. */
					case Path::EDescType::Input:
					case Path::EDescType::Output:
					case Path::EDescType::Node:
					case Path::EDescType::Metadata:
					default:
					{
						ensureAlwaysMsgf(false, TEXT("Invalid path. Tried to add more pathing off of a description type that has no children."));
						return FDescPath();
						break;
					}
				}

				NewPath.Path.Add(NewElement);
				return NewPath;
			}

			// This can be used to go up multiple levels in a path. Effectively removes the last UnwindCount elements from the path.
			FDescPath& operator<< (uint32 UnwindCount)
			{
				if (!ensureAlwaysMsgf(((uint32)Path.Num()) > UnwindCount, TEXT("Tried to unwind past the root of an FDescPath!")))
				{
					return *this;
				}

				while (UnwindCount-- > 0)
				{
					Path.RemoveAt(Path.Num() - 1);
				}

				return *this;
			}
		};

		// Series of utility functions for description paths.
		namespace Path
		{
			// When given a path for a node, finds that node's declaration in the Dependencies list.
			FDescPath GetPathToClassForNode(FDescPath InPathForNode, FString& InNodeName);
			
			// Returns a path for whatever class description encapsulates the given graph.
			FDescPath GetOwningClassDescription(FDescPath InPathForGraph);
			
			FDescPath GetDependencyPath(int32 InDependencyID);

			// Given the path to an input node, returns the path to the corresponding  input description.
			FDescPath GetInputDescriptionPath(FDescPath InPathForInputNode, const FString& InputName);

			FDescPath GetOutputDescriptionPath(FDescPath InPathForOutputNode, const FString& OutputName);

			FDescPath GetOuterGraphPath(FDescPath InPath);

			// Generates a human readable string for a path into a Metasound description. Used for debugging.
			FString GetPrintableString(FDescPath InPath);
		}

		using FClassDependencyIDs = TArray<int32>;

		/**
		 * Utility class that lives alongside a FMetasoundClassDescription to allow it's individual elements to be safely read and edited.
		 * Note: Could be moved to a private header.
		 *       It's also only necessary because we can't wrap a FMetasoundClassDescription in a shared ptr and also use it as a UPROPERTY.
		 */
		class METASOUNDFRONTEND_API FDescriptionAccessPoint
		{
		public:
			FDescriptionAccessPoint() = delete;
			FDescriptionAccessPoint(FMetasoundDocument& InRootDocument);

			// Returns the root class description associated with this FDescriptionAccessPoint.
			FMetasoundDocument& GetRoot();

			// Get a descriptor element at a specific path from the root of a metasound.
			// @see FDescPath for more details.
			// @returns a pointer to a descriptor struct, or an invalid ptr if the path is invalid.
			template <typename MetasoundDescriptionType>
			MetasoundDescriptionType* GetElementAtPath(const FDescPath& InPathFromRoot)
			{
				if (TIsSame<MetasoundDescriptionType, FMetasoundClassDescription>::Value)
				{
					return (MetasoundDescriptionType*) GetClassFromPath(InPathFromRoot);
				}
				else if (TIsSame<MetasoundDescriptionType, FMetasoundNodeDescription>::Value)
				{
					return (MetasoundDescriptionType*) GetNodeFromPath(InPathFromRoot);
				}
				else if (TIsSame<MetasoundDescriptionType, FMetasoundGraphDescription>::Value)
				{
					return (MetasoundDescriptionType*) GetGraphFromPath(InPathFromRoot);
				}
				else if (TIsSame<MetasoundDescriptionType, FMetasoundInputDescription>::Value)
				{
					return (MetasoundDescriptionType*) GetInputFromPath(InPathFromRoot);
				}
				else if (TIsSame<MetasoundDescriptionType, FMetasoundOutputDescription>::Value)
				{
					return (MetasoundDescriptionType*) GetOutputFromPath(InPathFromRoot);
				}
				else if (TIsSame<MetasoundDescriptionType, FMetasoundClassMetadata>::Value)
				{
					return (MetasoundDescriptionType*) GetMetadataFromPath(InPathFromRoot);
				}
				else if (TIsSame<MetasoundDescriptionType, FClassDependencyIDs>::Value)
				{
					return (MetasoundDescriptionType*) GetClassDependencyIDsFromPath(InPathFromRoot);
				}
				else
				{
					ensureAlwaysMsgf(false, TEXT("Tried to call GetElementAtPath with an invalid type."));
					return nullptr;
				}
			}

		private:
			// The root class description for all description structs
			// accessed via this class.
			FMetasoundDocument& RootDocument;

			// These functions result in some duplicate code, but allow us to hide
			// the implementation for GetElementAtPath.
			FMetasoundClassDescription* GetClassFromPath(const FDescPath& InPathFromRoot);
			FMetasoundNodeDescription* GetNodeFromPath(const FDescPath& InPathFromRoot);
			FMetasoundGraphDescription* GetGraphFromPath(const FDescPath& InPathFromRoot);
			FMetasoundInputDescription* GetInputFromPath(const FDescPath& InPathFromRoot);
			FMetasoundOutputDescription* GetOutputFromPath(const FDescPath& InPathFromRoot);
			FMetasoundClassMetadata* GetMetadataFromPath(const FDescPath& InPathFromRoot);
			FClassDependencyIDs* GetClassDependencyIDsFromPath(const FDescPath& InPathFromRoot);

			using FMetasoundDescriptionPtr = TVariant<
				FMetasoundDocument*,
				FMetasoundClassDescription*,
				FMetasoundNodeDescription*,
				FMetasoundGraphDescription*,
				FMetasoundInputDescription*,
				FMetasoundOutputDescription*,
				FMetasoundClassMetadata*,
				FClassDependencyIDs*,
				void*>
				;

			struct FDescriptionUnwindStep
			{
				FMetasoundDescriptionPtr DescriptionStructPtr;
				Path::EDescType Type;

				// TODO: make some simple constructors for this so we don't have
				// to manually fill out info each time. 

				static FDescriptionUnwindStep CreateInvalid()
				{
					FDescriptionUnwindStep InvalidStep;
					InvalidStep.DescriptionStructPtr.Set<void*>(nullptr);
					InvalidStep.Type = Path::EDescType::Invalid;
					return InvalidStep;
				}
			};

			FDescriptionUnwindStep GoToNextFromDocument(FMetasoundDocument& InDocument, FDescPath& InPath, const Path::FElement& InNext);
			FDescriptionUnwindStep GoToNextFromClass(FMetasoundClassDescription& InClass, FDescPath& InPath, const Path::FElement& InNext);

			// This function pops off elements of InPath to go to the next level down in a Metasound description.
			FDescriptionUnwindStep GoToNext(FDescPath& InPath, FDescriptionUnwindStep InElement);
		};

		// Utility class used by the frontend handle classes to access the underlying Description data they are modifying.
		// Similar to a TSoftObjectPtr: 
		// this class caches a FDescPath to a specific element in a Metasound Class Description, and a weak pointer to a FDescriptionAccessPoint
		// to use it with.
		template <typename MetasoundDescriptionType>
		class TDescriptionPtr
		{
			static constexpr bool bIsValidMetasoundDescriptionType =
				TIsSame< MetasoundDescriptionType, FMetasoundDocument>::Value ||
				TIsSame< MetasoundDescriptionType, FMetasoundGraphDescription>::Value ||
				TIsSame< MetasoundDescriptionType, FMetasoundNodeDescription>::Value ||
				TIsSame< MetasoundDescriptionType, FMetasoundInputDescription>::Value ||
				TIsSame< MetasoundDescriptionType, FMetasoundOutputDescription>::Value ||
				TIsSame< MetasoundDescriptionType, FMetasoundClassMetadata>::Value ||
				TIsSame< MetasoundDescriptionType, FMetasoundClassDescription>::Value;

			static_assert(bIsValidMetasoundDescriptionType, R"(Tried to use a Metasound::Frontend::FDescriptionAccessor with an invalid type. Supported types are: 
			FMetasoundDocument			
			FMetasoundGraphDescription, 
			FMetasoundNodeDescription,
			FMetasoundInputDescription,
			FMetasoundOutputDescription,
			FMetasoundClassMetadata,
			FMetasoundClassDescription)");

			TWeakPtr<FDescriptionAccessPoint> AccessPoint;
			FDescPath PathFromRoot;
			bool bAccessFailure = false;

		public:
			TDescriptionPtr() = delete;

			TDescriptionPtr(TWeakPtr<FDescriptionAccessPoint> InAccessPoint, const FDescPath& InPathFromRoot)
				: AccessPoint(InAccessPoint)
				, PathFromRoot(InPathFromRoot)
			{
				// Test accessor
				bAccessFailure = Get() == nullptr;
			}

			TDescriptionPtr(TDescriptionPtr&& InDescriptionPtr) = default;
			TDescriptionPtr(const TDescriptionPtr& InDescriptionPtr) = default;

			TDescriptionPtr& operator =(TDescriptionPtr&& InDescriptionPtr) = default;
			TDescriptionPtr& operator =(const TDescriptionPtr & InDescriptionPtr) = default;

			// @returns false if the description this accessor was referencing has been destroyed,
			// or if this was created with bad arguments.
			bool IsValid() const
			{
				return AccessPoint.IsValid() && PathFromRoot.IsValid() && !bAccessFailure;
			}

			FDescPath GetPath() const
			{
				return PathFromRoot;
			}

			TWeakPtr<FDescriptionAccessPoint> GetAccessPoint() const
			{
				return AccessPoint;
			}

			// returns a pointer or reference to the description data struct.
			// Note that this is NOT thread safe- this should be called on the same thread as whomever owns the lifecycle of the underlying MetasoundDescriptionType.
			MetasoundDescriptionType* Get() const
			{
				if (TSharedPtr<FDescriptionAccessPoint> PinnedAccessPoint = AccessPoint.Pin())
				{
					if (TIsSame<MetasoundDescriptionType, FMetasoundDocument>::Value)
					{
						return (MetasoundDescriptionType*) &(PinnedAccessPoint->GetRoot());
					}
					else
					{
						return PinnedAccessPoint->GetElementAtPath<MetasoundDescriptionType>(PathFromRoot);
					}
				}
				else
				{
					return nullptr;
				}
			}

			MetasoundDescriptionType& GetChecked() const
			{
				MetasoundDescriptionType* ElementPtr = Get();
				check(ElementPtr);
				return *ElementPtr;
			}

			MetasoundDescriptionType& operator*() const
			{
				return GetChecked();
			}

			MetasoundDescriptionType* operator->() const
			{
				return Get();
			}

			FORCEINLINE explicit operator bool() const
			{
				return IsValid();
			}
		};
	}
}
