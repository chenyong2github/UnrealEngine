// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendRegistries.h"

#include "Algo/ForEach.h"
#include "CoreMinimal.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendRegistryTransaction.h"
#include "MetasoundLog.h"
#include "MetasoundRouter.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"

#ifndef WITH_METASOUND_FRONTEND
#define WITH_METASOUND_FRONTEND 0
#endif


namespace Metasound
{
	namespace Frontend
	{
		namespace MetasoundFrontendRegistryPrivate
		{
			const FString& GetClassTypeString(EMetasoundFrontendClassType InType)
			{
				static const FString InputType(TEXT("Input"));
				static const FString OutputType(TEXT("Output"));
				static const FString ExternalType(TEXT("External"));
				static const FString VariableType(TEXT("Variable"));
				static const FString GraphType(TEXT("Graph"));
				static const FString InvalidType(TEXT("Invalid"));

				switch (InType)
				{
					case EMetasoundFrontendClassType::Input:
						return InputType;

					case EMetasoundFrontendClassType::Output:
						return OutputType;

					case EMetasoundFrontendClassType::External:
						return ExternalType;

					case EMetasoundFrontendClassType::Variable:
						return VariableType;

					case EMetasoundFrontendClassType::Graph:
						return GraphType;

					default:
						static_assert(static_cast<uint8>(EMetasoundFrontendClassType::Invalid) == 5, "Missing EMetasoundFrontendClassType case coverage");
						return InvalidType;
				}
			}



			// Registry container private implementation.
			class FRegistryContainerImpl : public FMetasoundFrontendRegistryContainer
			{

			public:
				using FConverterNodeRegistryKey = ::Metasound::Frontend::FConverterNodeRegistryKey;
				using FConverterNodeRegistryValue = ::Metasound::Frontend::FConverterNodeRegistryValue;
				using FConverterNodeInfo = ::Metasound::Frontend::FConverterNodeInfo;

				using FNodeRegistryKey = Metasound::Frontend::FNodeRegistryKey;

				using FDataTypeRegistryInfo = Metasound::Frontend::FDataTypeRegistryInfo;
				using FNodeClassMetadata = Metasound::FNodeClassMetadata;
				using IEnumDataTypeInterface = Metasound::Frontend::IEnumDataTypeInterface;

				FRegistryContainerImpl() = default;

				FRegistryContainerImpl(const FRegistryContainerImpl&) = delete;
				FRegistryContainerImpl& operator=(const FRegistryContainerImpl&) = delete;

				virtual ~FRegistryContainerImpl() = default;

				// Add a function to the init command array.
				bool EnqueueInitCommand(TUniqueFunction<void()>&& InFunc) override;

				// This is called on module startup. This invokes any registration commands enqueued by our registration macros.
				void RegisterPendingNodes() override;

				/** Register external node with the frontend.
				 *
				 * @param InCreateNode - Function for creating node from FNodeInitData.
				 * @param InCreateDescription - Function for creating a FMetasoundFrontendClass.
				 *
				 * @return True on success.
				 */
				virtual FNodeRegistryKey RegisterNode(TUniquePtr<Metasound::Frontend::INodeRegistryEntry>&&) override;
				virtual bool UnregisterNode(const FNodeRegistryKey& InKey) override;
				virtual bool IsNodeRegistered(const FNodeRegistryKey& InKey) const override;
				virtual bool IsNodeNative(const FNodeRegistryKey& InKey) const override;

				bool RegisterConversionNode(const FConverterNodeRegistryKey& InNodeKey, const FConverterNodeInfo& InNodeInfo) override;

				virtual void ForEachNodeRegistryTransactionSince(FRegistryTransactionID InSince, FRegistryTransactionID* OutCurrentRegistryTransactionID, TFunctionRef<void(const FNodeRegistryTransaction&)> InFunc) const override;

				void IterateRegistry(Metasound::FIterateMetasoundFrontendClassFunction InIterFunc, EMetasoundFrontendClassType InClassType = EMetasoundFrontendClassType::Invalid) const override;

				// Find Frontend Document data.
				bool FindFrontendClassFromRegistered(const FNodeRegistryKey& InKey, FMetasoundFrontendClass& OutClass) override;
				virtual bool FindNodeClassInfoFromRegistered(const Metasound::Frontend::FNodeRegistryKey& InKey, FNodeClassInfo& OutInfo) override;
				bool FindInputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey) override;
				bool FindVariableNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey) override;
				bool FindOutputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey) override;

				// Create a new instance of a C++ implemented node from the registry.
				virtual TUniquePtr<INode> CreateNode(const FNodeRegistryKey& InKey, FDefaultNodeConstructorParams&&) const override;
				virtual TUniquePtr<INode> CreateNode(const FNodeRegistryKey& InKey, FDefaultLiteralNodeConstructorParams&&) const override;
				virtual TUniquePtr<Metasound::INode> CreateNode(const FNodeRegistryKey& InKey, const Metasound::FNodeInitData& InInitData) const override;

				// Returns a list of possible nodes to use to convert from FromDataType to ToDataType.
				// Returns an empty array if none are available.
				TArray<FConverterNodeInfo> GetPossibleConverterNodes(const FName& FromDataType, const FName& ToDataType) override;

			private:

				const INodeRegistryEntry* FindNodeEntry(const FNodeRegistryKey& InKey) const;


				// This buffer is used to enqueue nodes and datatypes to register when the module has been initialized,
				// in order to avoid bad behavior with ensures, logs, etc. on static initialization.
				// The bad news is that TInlineAllocator is the safest allocator to use on static init.
				// The good news is that none of these lambdas typically have captures, so this should have low memory overhead.
				static constexpr int32 MaxNumNodesAndDatatypesToInitialize = 8192;
				TArray<TUniqueFunction<void()>, TInlineAllocator<MaxNumNodesAndDatatypesToInitialize>> LazyInitCommands;
				
				FCriticalSection LazyInitCommandCritSection;

				// Registry in which we keep all information about nodes implemented in C++.
				TMap<FNodeRegistryKey, TUniquePtr<INodeRegistryEntry>> RegisteredNodes;

				// Registry in which we keep lists of possible nodes to use to convert between two datatypes
				TMap<FConverterNodeRegistryKey, FConverterNodeRegistryValue> ConverterNodeRegistry;

				TRegistryTransactionHistory<FNodeRegistryTransaction> RegistryTransactionHistory;
			};

			void FRegistryContainerImpl::RegisterPendingNodes()
			{
				FScopeLock ScopeLock(&LazyInitCommandCritSection);

				UE_LOG(LogMetaSound, Display, TEXT("Processing %i Metasounds Frontend Registration Requests."), LazyInitCommands.Num());
				uint64 CurrentTime = FPlatformTime::Cycles64();

				for (TUniqueFunction<void()>& Command : LazyInitCommands)
				{
					Command();
				}

				LazyInitCommands.Empty();

				uint64 CyclesUsed = FPlatformTime::Cycles64() - CurrentTime;
				UE_LOG(LogMetaSound, Display, TEXT("Initializing Metasounds Frontend took %f seconds."), FPlatformTime::ToSeconds64(CyclesUsed));
			}

			bool FRegistryContainerImpl::EnqueueInitCommand(TUniqueFunction<void()>&& InFunc)
			{
				FScopeLock ScopeLock(&LazyInitCommandCritSection);
				if (LazyInitCommands.Num() >= MaxNumNodesAndDatatypesToInitialize)
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Registering more that %d nodes and datatypes for metasounds! Consider increasing MetasoundFrontendRegistryContainer::MaxNumNodesAndDatatypesToInitialize."));
				}

				LazyInitCommands.Add(MoveTemp(InFunc));
				return true;
			}

			void FRegistryContainerImpl::ForEachNodeRegistryTransactionSince(FRegistryTransactionID InSince, FRegistryTransactionID* OutCurrentRegistryTransactionID, TFunctionRef<void(const FNodeRegistryTransaction&)> InFunc) const 
			{
				return RegistryTransactionHistory.ForEachTransactionSince(InSince, OutCurrentRegistryTransactionID, InFunc);
			}


			TUniquePtr<Metasound::INode> FRegistryContainerImpl::CreateNode(const FNodeRegistryKey& InKey, FDefaultNodeConstructorParams&& InParams) const
			{
				const INodeRegistryEntry* Entry = FindNodeEntry(InKey);

				if (ensureAlwaysMsgf(nullptr != Entry, TEXT("Could not find node [RegistryKey:%s]"), *InKey))
				{
					return Entry->CreateNode(MoveTemp(InParams));
				}

				return nullptr;
			}

			TUniquePtr<Metasound::INode> FRegistryContainerImpl::CreateNode(const FNodeRegistryKey& InKey, FDefaultLiteralNodeConstructorParams&& InParams) const
			{
				const INodeRegistryEntry* Entry = FindNodeEntry(InKey);

				if (ensureAlwaysMsgf(nullptr != Entry, TEXT("Could not find node [RegistryKey:%s]"), *InKey))
				{
					return Entry->CreateNode(MoveTemp(InParams));
				}

				return nullptr;
			}

			TUniquePtr<Metasound::INode> FRegistryContainerImpl::CreateNode(const FNodeRegistryKey& InKey, const Metasound::FNodeInitData& InInitData) const
			{
				const INodeRegistryEntry* Entry = FindNodeEntry(InKey);

				if (ensureAlwaysMsgf(nullptr != Entry, TEXT("Could not find node [RegistryKey:%s]"), *InKey))
				{
					return Entry->CreateNode(InInitData);
				}

				return nullptr;
			}

			TArray<::Metasound::Frontend::FConverterNodeInfo> FRegistryContainerImpl::GetPossibleConverterNodes(const FName& FromDataType, const FName& ToDataType)
			{
				FConverterNodeRegistryKey InKey = { FromDataType, ToDataType };
				if (!ConverterNodeRegistry.Contains(InKey))
				{
					return TArray<FConverterNodeInfo>();
				}
				else
				{
					return ConverterNodeRegistry[InKey].PotentialConverterNodes;
				}
			}

			FNodeRegistryKey FRegistryContainerImpl::RegisterNode(TUniquePtr<INodeRegistryEntry>&& InEntry)
			{
				FNodeRegistryKey Key;

				if (InEntry.IsValid())
				{
					Key = NodeRegistryKey::CreateKey(InEntry->GetClassInfo());

					// check to see if an identical node was already registered, and log
					ensureAlwaysMsgf(
						!RegisteredNodes.Contains(Key),
						TEXT("Node with registry key '%s' already registered. "
							"The previously registered node will be overwritten. "
							"This can happen if two classes share the same name or if METASOUND_REGISTER_NODE is defined in a public header."),
						*Key);

					// Store update to newly registered node in history so nodes
					// can be queried by transaction ID
					RegistryTransactionHistory.Add(FNodeRegistryTransaction(FNodeRegistryTransaction::ETransactionType::NodeRegistration, Key, InEntry->GetClassInfo()));

					// Store registry elements in map so nodes can be queried using registry key.
					RegisteredNodes.Add(Key, MoveTemp(InEntry));
				}

				return Key;
			}

			bool FRegistryContainerImpl::UnregisterNode(const FNodeRegistryKey& InKey)
			{
				if (IsValidNodeRegistryKey(InKey))
				{
					if (const INodeRegistryEntry* Entry = FindNodeEntry(InKey))
					{
						RegistryTransactionHistory.Add(FNodeRegistryTransaction(FNodeRegistryTransaction::ETransactionType::NodeUnregistration, InKey, Entry->GetClassInfo()));

						RegisteredNodes.Remove(InKey);
						return true;
					}
				}

				return false;
			}

			bool FRegistryContainerImpl::RegisterConversionNode(const FConverterNodeRegistryKey& InNodeKey, const FConverterNodeInfo& InNodeInfo)
			{
				if (!ConverterNodeRegistry.Contains(InNodeKey))
				{
					ConverterNodeRegistry.Add(InNodeKey);
				}

				FConverterNodeRegistryValue& ConverterNodeList = ConverterNodeRegistry[InNodeKey];

				if (ensureAlways(!ConverterNodeList.PotentialConverterNodes.Contains(InNodeInfo)))
				{
					ConverterNodeList.PotentialConverterNodes.Add(InNodeInfo);
					return true;
				}
				else
				{
					// If we hit this, someone attempted to add the same converter node to our list multiple times.
					return false;
				}
			}

			bool FRegistryContainerImpl::IsNodeRegistered(const FNodeRegistryKey& InKey) const
			{
				return RegisteredNodes.Contains(InKey);
			}

			bool FRegistryContainerImpl::IsNodeNative(const FNodeRegistryKey& InKey) const
			{
				if (const INodeRegistryEntry* Entry = FindNodeEntry(InKey))
				{
					return Entry->IsNative();
				}

				return false;
			}


			bool FRegistryContainerImpl::FindFrontendClassFromRegistered(const FNodeRegistryKey& InKey, FMetasoundFrontendClass& OutClass)
			{
				if (const INodeRegistryEntry* Entry = FindNodeEntry(InKey))
				{
					OutClass = Entry->GetFrontendClass();
					return true;
				}
				return false;
			}

			bool FRegistryContainerImpl::FindNodeClassInfoFromRegistered(const Metasound::Frontend::FNodeRegistryKey& InKey, FNodeClassInfo& OutInfo)
			{
				if (const INodeRegistryEntry* Entry = FindNodeEntry(InKey))
				{
					OutInfo = Entry->GetClassInfo();
					return true;
				}

				return false;
			}

			bool FRegistryContainerImpl::FindInputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey)
			{
				FMetasoundFrontendClass Class;
				if (IDataTypeRegistry::Get().GetFrontendInputClass(InDataTypeName, Class))
				{
					OutKey = NodeRegistryKey::CreateKey(Class.Metadata);
					return true;
				}
				return false;
			}

			bool FRegistryContainerImpl::FindVariableNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey)
			{
				FMetasoundFrontendClass Class;
				if (IDataTypeRegistry::Get().GetFrontendVariableClass(InDataTypeName, Class))
				{
					OutKey = NodeRegistryKey::CreateKey(Class.Metadata);
					return true;
				}
				return false;
			}

			bool FRegistryContainerImpl::FindOutputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey)
			{
				FMetasoundFrontendClass Class;
				if (IDataTypeRegistry::Get().GetFrontendOutputClass(InDataTypeName, Class))
				{
					OutKey = NodeRegistryKey::CreateKey(Class.Metadata);
					return true;
				}
				return false;
			}

			void FRegistryContainerImpl::IterateRegistry(Metasound::FIterateMetasoundFrontendClassFunction InIterFunc, EMetasoundFrontendClassType InClassType) const
			{
				auto WrappedFunc = [&](const TPair<FNodeRegistryKey, TUniquePtr<INodeRegistryEntry>>& Pair)
				{
					InIterFunc(Pair.Value->GetFrontendClass());
				};

				if (EMetasoundFrontendClassType::Invalid == InClassType)
				{
					// Iterate through all classes. 
					Algo::ForEach(RegisteredNodes, WrappedFunc);
				}
				else
				{
					// Only call function on classes of certain type.
					auto IsMatchingClassType = [&](const TPair<FNodeRegistryKey, TUniquePtr<INodeRegistryEntry>>& Pair)
					{
						return Pair.Value->GetClassInfo().Type == InClassType;
					};
					Algo::ForEachIf(RegisteredNodes, IsMatchingClassType, WrappedFunc);
				}
			}


			const INodeRegistryEntry* FRegistryContainerImpl::FindNodeEntry(const FNodeRegistryKey& InKey) const
			{
				if (const TUniquePtr<INodeRegistryEntry>* Entry = RegisteredNodes.Find(InKey))
				{
					if (Entry->IsValid())
					{
						return Entry->Get();
					}
				}

				return nullptr;
			}
		} // namespace MetasoundFrontendRegistriesPrivate

		FNodeRegistryTransaction::FNodeRegistryTransaction(ETransactionType InType, const FNodeRegistryKey& InKey, const FNodeClassInfo& InNodeClassInfo)
		: Type(InType)
		, Key(InKey)
		, NodeClassInfo(InNodeClassInfo)
		{
		}

		FNodeRegistryTransaction::ETransactionType FNodeRegistryTransaction::GetTransactionType() const
		{
			return Type;
		}

		const FNodeClassInfo& FNodeRegistryTransaction::GetNodeClassInfo() const
		{
			return NodeClassInfo;
		}

		const FNodeRegistryKey& FNodeRegistryTransaction::GetNodeRegistryKey() const
		{
			return Key;
		}

		namespace NodeRegistryKey
		{
			// All registry keys should be created through this function to ensure consistency.
			FNodeRegistryKey CreateKey(EMetasoundFrontendClassType InType, const FString& InFullClassName, int32 InMajorVersion, int32 InMinorVersion)
			{
				using namespace MetasoundFrontendRegistryPrivate;

				FString RegistryKey = FString::Format(TEXT("{0}_{1}_{2}.{3}"), {*GetClassTypeString(InType), *InFullClassName, InMajorVersion, InMinorVersion});

				return RegistryKey;
			}

			bool IsValid(const FNodeRegistryKey& InKey)
			{
				return !InKey.IsEmpty();
			}

			bool IsEqual(const FNodeRegistryKey& InLHS, const FNodeRegistryKey& InRHS)
			{
				return InLHS == InRHS;
			}

			bool IsEqual(const FMetasoundFrontendClassMetadata& InLHS, const FMetasoundFrontendClassMetadata& InRHS)
			{
				if (InLHS.GetClassName() == InRHS.GetClassName())
				{
					if (InLHS.GetType() == InRHS.GetType())
					{
						if (InLHS.GetVersion() == InRHS.GetVersion())
						{
							return true;
						}
					}
				}
				return false;
			}

			bool IsEqual(const FNodeClassInfo& InLHS, const FMetasoundFrontendClassMetadata& InRHS)
			{
				if (InLHS.ClassName == InRHS.GetClassName())
				{
					if (InLHS.Type == InRHS.GetType())
					{
						if (InLHS.Version == InRHS.GetVersion())
						{
							return true;
						}
					}
				}
				return false;
			}

			FNodeRegistryKey CreateKey(const FNodeClassMetadata& InNodeMetadata)
			{
				return CreateKey(EMetasoundFrontendClassType::External, InNodeMetadata.ClassName.GetFullName().ToString(), InNodeMetadata.MajorVersion, InNodeMetadata.MinorVersion);
			}

			FNodeRegistryKey CreateKey(const FMetasoundFrontendClassMetadata& InNodeMetadata)
			{
				return CreateKey(InNodeMetadata.GetType(), InNodeMetadata.GetClassName().GetFullName().ToString(), InNodeMetadata.GetVersion().Major, InNodeMetadata.GetVersion().Minor);
			}

			FNodeRegistryKey CreateKey(const FNodeClassInfo& InClassInfo)
			{
				return CreateKey(InClassInfo.Type, InClassInfo.ClassName.GetFullName().ToString(), InClassInfo.Version.Major, InClassInfo.Version.Minor);
			}
		}

		bool IsValidNodeRegistryKey(const FNodeRegistryKey& InKey)
		{
			return NodeRegistryKey::IsValid(InKey);
		}

		FNodeClassInfo::FNodeClassInfo(const FMetasoundFrontendClassMetadata& InMetadata)
			: ClassName(InMetadata.GetClassName())
			, Type(InMetadata.GetType())
			, Version(InMetadata.GetVersion())
		{
		}

		FNodeClassInfo::FNodeClassInfo(const FMetasoundFrontendGraphClass& InClass, FName InAssetPath)
			: ClassName(InClass.Metadata.GetClassName())
			, Type(EMetasoundFrontendClassType::External) // Overridden as it is considered the same as an external class in the registry
			, AssetClassID(FGuid(ClassName.Name.ToString()))
			, AssetPath(InAssetPath)
			, Version(InClass.Metadata.GetVersion())
		{
			ensure(!AssetPath.IsNone());

			for (const FMetasoundFrontendClassInput& Input : InClass.Interface.Inputs)
			{
				InputTypes.Add(Input.TypeName);
			}

			for (const FMetasoundFrontendClassOutput& Output : InClass.Interface.Outputs)
			{
				OutputTypes.Add(Output.TypeName);
			}
		}
	} // namespace Frontend
} // namespace Metasound

FMetasoundFrontendRegistryContainer* FMetasoundFrontendRegistryContainer::LazySingleton = nullptr;

FMetasoundFrontendRegistryContainer* FMetasoundFrontendRegistryContainer::Get()
{
	if (!LazySingleton)
	{
		LazySingleton = new Metasound::Frontend::MetasoundFrontendRegistryPrivate::FRegistryContainerImpl();
	}

	return LazySingleton;
}

void FMetasoundFrontendRegistryContainer::ShutdownMetasoundFrontend()
{
	if (LazySingleton)
	{
		delete LazySingleton;
		LazySingleton = nullptr;
	}
}

Metasound::Frontend::FNodeRegistryKey FMetasoundFrontendRegistryContainer::GetRegistryKey(const FNodeClassMetadata& InNodeMetadata)
{
	return Metasound::Frontend::NodeRegistryKey::CreateKey(InNodeMetadata);
}

Metasound::Frontend::FNodeRegistryKey FMetasoundFrontendRegistryContainer::GetRegistryKey(const FMetasoundFrontendClassMetadata& InNodeMetadata)
{
	return Metasound::Frontend::NodeRegistryKey::CreateKey(InNodeMetadata);
}

Metasound::Frontend::FNodeRegistryKey FMetasoundFrontendRegistryContainer::GetRegistryKey(const FNodeClassInfo& InClassInfo)
{
	return Metasound::Frontend::NodeRegistryKey::CreateKey(InClassInfo);
}

bool FMetasoundFrontendRegistryContainer::GetFrontendClassFromRegistered(const FNodeRegistryKey& InKey, FMetasoundFrontendClass& OutClass)
{
	FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();

	if (ensure(nullptr != Registry))
	{
		return Registry->FindFrontendClassFromRegistered(InKey, OutClass);
	}

	return false;
}

bool FMetasoundFrontendRegistryContainer::GetNodeClassInfoFromRegistered(const FNodeRegistryKey& InKey, FNodeClassInfo& OutInfo)
{
	if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
	{
		return Registry->FindNodeClassInfoFromRegistered(InKey, OutInfo);
	}
	return false;
}


bool FMetasoundFrontendRegistryContainer::GetInputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey)
{
	if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
	{
		return Registry->FindInputNodeRegistryKeyForDataType(InDataTypeName, OutKey);
	}
	return false;
}

bool FMetasoundFrontendRegistryContainer::GetVariableNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey)
{
	if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
	{
		return Registry->FindVariableNodeRegistryKeyForDataType(InDataTypeName, OutKey);
	}
	return false;
}

bool FMetasoundFrontendRegistryContainer::GetOutputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey)
{
	if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
	{
		return Registry->FindOutputNodeRegistryKeyForDataType(InDataTypeName, OutKey);
	}
	return false;
}

