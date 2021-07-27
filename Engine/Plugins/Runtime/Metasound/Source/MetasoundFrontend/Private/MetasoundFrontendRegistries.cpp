// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendRegistries.h"

#include "Algo/ForEach.h"
#include "CoreMinimal.h"
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


			// Return the compatible literal with the most descriptive type.
			// TODO: Currently TIsParsable<> allows for implicit conversion of
			// constructor arguments of integral types which can cause some confusion
			// here when trying to match a literal type to a constructor. For example:
			//
			// struct FBoolConstructibleType
			// {
			// 	FBoolConstructibleType(bool InValue);
			// };
			//
			// static_assert(TIsParsable<FBoolConstructible, double>::Value); 
			//
			// Implicit conversions are currently allowed in TIsParsable because this
			// is perfectly legal syntax.
			//
			// double Value = 10.0;
			// FBoolConstructibleType BoolConstructible = Value;
			//
			// There are some tricks to possibly disable implicit conversions when
			// checking for specific constructors, but they are yet to be implemented 
			// and are untested. Here's the basic idea.
			//
			// template<DataType, DesiredIntegralArgType>
			// struct TOnlyConvertIfIsSame
			// {
			// 		// Implicit conversion only defined if types match.
			// 		template<typename SuppliedIntegralArgType, std::enable_if<std::is_same<std::decay<SuppliedIntegralArgType>::type, DesiredIntegralArgType>::value, int> = 0>
			// 		operator DesiredIntegralArgType()
			// 		{
			// 			return DesiredIntegralArgType{};
			// 		}
			// };
			//
			// static_assert(false == std::is_constructible<FBoolConstructibleType, TOnlyConvertIfSame<double>>::value);
			// static_assert(true == std::is_constructible<FBoolConstructibleType, TOnlyConvertIfSame<bool>>::value);
			ELiteralType GetMostDescriptiveLiteralForDataType(const FDataTypeRegistryInfo& InDataTypeInfo)
			{
				if (InDataTypeInfo.bIsProxyArrayParsable)
				{
					return ELiteralType::UObjectProxyArray;
				}
				else if (InDataTypeInfo.bIsProxyParsable)
				{
					return ELiteralType::UObjectProxy;
				}
				else if (InDataTypeInfo.bIsEnum && InDataTypeInfo.bIsIntParsable)
				{
					return ELiteralType::Integer;
				}
				else if (InDataTypeInfo.bIsStringArrayParsable)
				{
					return ELiteralType::StringArray;
				}
				else if (InDataTypeInfo.bIsFloatArrayParsable)
				{
					return ELiteralType::FloatArray;
				}
				else if (InDataTypeInfo.bIsIntArrayParsable)
				{
					return ELiteralType::IntegerArray;
				}
				else if (InDataTypeInfo.bIsBoolArrayParsable)
				{
					return ELiteralType::BooleanArray;
				}
				else if (InDataTypeInfo.bIsStringParsable)
				{
					return ELiteralType::String;
				}
				else if (InDataTypeInfo.bIsFloatParsable)
				{
					return ELiteralType::Float;
				}
				else if (InDataTypeInfo.bIsIntParsable)
				{
					return ELiteralType::Integer;
				}
				else if (InDataTypeInfo.bIsBoolParsable)
				{
					return ELiteralType::Boolean;
				}
				else if (InDataTypeInfo.bIsDefaultArrayParsable)
				{
					return ELiteralType::NoneArray; 
				}
				else if (InDataTypeInfo.bIsDefaultParsable)
				{
					return ELiteralType::None;
				}
				else
				{
					// if we ever hit this, something has gone wrong with the REGISTER_METASOUND_DATATYPE macro.
					// we should have failed to compile if any of these are false.
					checkNoEntry();
					return ELiteralType::Invalid;
				}
			}

			// Node registry entry for input nodes created from a data type registry entry.
			class FInputNodeRegistryEntry : public INodeRegistryEntry
			{
			public:
				FInputNodeRegistryEntry() = delete;

				FInputNodeRegistryEntry(TUniquePtr<IDataTypeRegistryEntry>&& InDataTypeEntry)
				: DataTypeEntry(MoveTemp(InDataTypeEntry))
				{
					if (DataTypeEntry.IsValid())
					{
						FrontendClass = DataTypeEntry->GetFrontendInputClass();
						ClassInfo = FNodeClassInfo(FrontendClass.Metadata);
					}
				}

				virtual ~FInputNodeRegistryEntry() = default;

				virtual const FNodeClassInfo& GetClassInfo() const override
				{
					return ClassInfo;
				}

				virtual TUniquePtr<INode> CreateNode(FDefaultNodeConstructorParams&&) const override
				{
					return nullptr;
				}

				virtual TUniquePtr<INode> CreateNode(FDefaultLiteralNodeConstructorParams&& InParams) const override
				{
					if (DataTypeEntry.IsValid())
					{
						return DataTypeEntry->CreateInputNode(MoveTemp(InParams));
					}
					return nullptr;
				}

				virtual TUniquePtr<INode> CreateNode(const FNodeInitData&) const override
				{
					return nullptr;
				}

				virtual const FMetasoundFrontendClass& GetFrontendClass() const override
				{
					return FrontendClass;
				}

				virtual TUniquePtr<INodeRegistryEntry> Clone() const override
				{
					if (DataTypeEntry.IsValid())
					{
						return MakeUnique<FInputNodeRegistryEntry>(DataTypeEntry->Clone());
					}
					return MakeUnique<FInputNodeRegistryEntry>(TUniquePtr<IDataTypeRegistryEntry>());
				}

				virtual bool IsNative() const override
				{
					return true;
				}

			private:
				
				TUniquePtr<IDataTypeRegistryEntry> DataTypeEntry;
				FNodeClassInfo ClassInfo;
				FMetasoundFrontendClass FrontendClass;
			};

			// Node registry entry for output nodes created from a data type registry entry.
			class FOutputNodeRegistryEntry : public INodeRegistryEntry
			{
			public:
				FOutputNodeRegistryEntry() = delete;

				FOutputNodeRegistryEntry(TUniquePtr<IDataTypeRegistryEntry>&& InDataTypeEntry)
				: DataTypeEntry(MoveTemp(InDataTypeEntry))
				{
					if (DataTypeEntry.IsValid())
					{
						FrontendClass = DataTypeEntry->GetFrontendOutputClass();
						ClassInfo = FNodeClassInfo(FrontendClass.Metadata);
					}
				}

				virtual ~FOutputNodeRegistryEntry() = default;

				virtual const FNodeClassInfo& GetClassInfo() const override
				{
					return ClassInfo;
				}

				virtual TUniquePtr<INode> CreateNode(FDefaultNodeConstructorParams&& InParams) const override
				{
					if (DataTypeEntry.IsValid())
					{
						return DataTypeEntry->CreateOutputNode(MoveTemp(InParams));
					}
					return nullptr;
				}

				virtual TUniquePtr<INode> CreateNode(FDefaultLiteralNodeConstructorParams&& InParams) const override
				{
					return nullptr;
				}

				virtual TUniquePtr<INode> CreateNode(const FNodeInitData&) const override
				{
					return nullptr;
				}

				virtual const FMetasoundFrontendClass& GetFrontendClass() const override
				{
					return FrontendClass;
				}

				virtual TUniquePtr<INodeRegistryEntry> Clone() const override
				{
					if (DataTypeEntry.IsValid())
					{
						return MakeUnique<FOutputNodeRegistryEntry>(DataTypeEntry->Clone());
					}
					return MakeUnique<FOutputNodeRegistryEntry>(TUniquePtr<IDataTypeRegistryEntry>());
				}

				virtual bool IsNative() const override
				{
					return true;
				}

			private:
				
				TUniquePtr<IDataTypeRegistryEntry> DataTypeEntry;
				FNodeClassInfo ClassInfo;
				FMetasoundFrontendClass FrontendClass;
			};

			// Node registry entry for variable nodes created from a data type registry entry.
			class FVariableNodeRegistryEntry : public INodeRegistryEntry
			{
			public:
				FVariableNodeRegistryEntry() = delete;

				FVariableNodeRegistryEntry(TUniquePtr<IDataTypeRegistryEntry>&& InDataTypeEntry)
				: DataTypeEntry(MoveTemp(InDataTypeEntry))
				{
					if (DataTypeEntry.IsValid())
					{
						FrontendClass = DataTypeEntry->GetFrontendVariableClass();
						ClassInfo = FNodeClassInfo(FrontendClass.Metadata);
					}
				}

				virtual ~FVariableNodeRegistryEntry() = default;

				virtual const FNodeClassInfo& GetClassInfo() const override
				{
					return ClassInfo;
				}

				virtual TUniquePtr<INode> CreateNode(FDefaultNodeConstructorParams&& InParams) const override
				{
					return nullptr;
				}

				virtual TUniquePtr<INode> CreateNode(FDefaultLiteralNodeConstructorParams&& InParams) const override
				{
					if (DataTypeEntry.IsValid())
					{
						return DataTypeEntry->CreateVariableNode(MoveTemp(InParams));
					}
					return nullptr;
				}

				virtual TUniquePtr<INode> CreateNode(const FNodeInitData&) const override
				{
					return nullptr;
				}

				virtual const FMetasoundFrontendClass& GetFrontendClass() const override
				{
					return FrontendClass;
				}

				virtual TUniquePtr<INodeRegistryEntry> Clone() const override
				{
					if (DataTypeEntry.IsValid())
					{
						return MakeUnique<FVariableNodeRegistryEntry>(DataTypeEntry->Clone());
					}
					return MakeUnique<FVariableNodeRegistryEntry>(TUniquePtr<IDataTypeRegistryEntry>());
				}

				virtual bool IsNative() const override
				{
					return true;
				}

			private:
				
				TUniquePtr<IDataTypeRegistryEntry> DataTypeEntry;
				FNodeClassInfo ClassInfo;
				FMetasoundFrontendClass FrontendClass;
			};


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

				// Return any data types that can be used as a metasound input type or output type.
				TArray<FName> GetAllValidDataTypes() override;

				void IterateRegistry(Metasound::FIterateMetasoundFrontendClassFunction InIterFunc, EMetasoundFrontendClassType InClassType = EMetasoundFrontendClassType::Invalid) const override;

				// Find Frontend Document data.
				bool FindFrontendClassFromRegistered(const FNodeRegistryKey& InKey, FMetasoundFrontendClass& OutClass) override;
				virtual bool FindNodeClassInfoFromRegistered(const Metasound::Frontend::FNodeRegistryKey& InKey, FNodeClassInfo& OutInfo) override;
				bool FindInputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey) override;
				bool FindVariableNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey) override;
				bool FindOutputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey) override;

				// Create a new instance of a C++ implemented node from the registry.
				TUniquePtr<Metasound::INode> CreateInputNode(const FName& InInputType, Metasound::FInputNodeConstructorParams&& InParams) override;
				TUniquePtr<Metasound::INode> CreateVariableNode(const FName& InVariableType, FVariableNodeConstructorParams&& InParams) override;
				TUniquePtr<Metasound::INode> CreateOutputNode(const FName& InOutputType, Metasound::FOutputNodeConstructorParams&& InParams) override;
				virtual TUniquePtr<INode> CreateNode(const FNodeRegistryKey& InKey, FDefaultNodeConstructorParams&&) const override;
				virtual TUniquePtr<INode> CreateNode(const FNodeRegistryKey& InKey, FDefaultLiteralNodeConstructorParams&&) const override;
				virtual TUniquePtr<Metasound::INode> CreateNode(const FNodeRegistryKey& InKey, const Metasound::FNodeInitData& InInitData) const override;

				// Returns a list of possible nodes to use to convert from FromDataType to ToDataType.
				// Returns an empty array if none are available.
				TArray<FConverterNodeInfo> GetPossibleConverterNodes(const FName& FromDataType, const FName& ToDataType) override;

				virtual bool RegisterDataType(const FName& InName, TUniquePtr<Metasound::Frontend::IDataTypeRegistryEntry>&&) override;

				// Get the desired kind of literal for a given data type. Returns EConstructorArgType::Invalid if the data type couldn't be found.
				Metasound::ELiteralType GetDesiredLiteralTypeForDataType(FName InDataType) const override;

				// Get whether we can build a literal of this specific type for InDataType.
				bool DoesDataTypeSupportLiteralType(FName InDataType, Metasound::ELiteralType InLiteralType) const override;

				// Handle uobjects and literals
				UClass* GetLiteralUClassForDataType(FName InDataType) const override;
				Metasound::FLiteral CreateLiteralFromUObject(const FName& InDataType, UObject* InObject) override;
				Metasound::FLiteral CreateLiteralFromUObjectArray(const FName& InDataType, TArray<UObject*> InObjectArray) override;

				// Get info about a specific data type (what kind of literals we can use, etc.)
				// @returns false if InDataType wasn't found in the registry. 
				bool GetInfoForDataType(FName InDataType, FDataTypeRegistryInfo& OutInfo) override;

				TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> GetEnumInterfaceForDataType(FName InDataType) const override;

				TSharedPtr<Metasound::IDataChannel, ESPMode::ThreadSafe> CreateDataChannelForDataType(const FName& InDataType, const Metasound::FOperatorSettings& InOperatorSettings) const override;
			private:

				const IDataTypeRegistryEntry* FindDataTypeEntry(const FName& InDataTypeName) const;
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
				TMap<FName, TUniquePtr<IDataTypeRegistryEntry>> RegisteredDataTypes;

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

			TUniquePtr<Metasound::INode> FRegistryContainerImpl::CreateInputNode(const FName& InDataType, Metasound::FInputNodeConstructorParams&& InParams)
			{
				const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType);

				if (ensureAlwaysMsgf(nullptr != Entry, TEXT("Could not find data type [Name:%s]"), *InDataType.ToString()))
				{
					return Entry->CreateInputNode(MoveTemp(InParams));
				}
				else
				{
					return nullptr;
				}
			}

			TUniquePtr<Metasound::INode> FRegistryContainerImpl::CreateVariableNode(const FName& InDataType, Metasound::FVariableNodeConstructorParams&& InParams)
			{
				const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType);

				if (ensureAlwaysMsgf(nullptr != Entry, TEXT("Could not find data type [Name:%s]"), *InDataType.ToString()))
				{
					return Entry->CreateVariableNode(MoveTemp(InParams));
				}
				else
				{
					return nullptr;
				}
			}

			TUniquePtr<Metasound::INode> FRegistryContainerImpl::CreateOutputNode(const FName& InDataType, Metasound::FOutputNodeConstructorParams&& InParams)
			{
				const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType);

				if (ensureAlwaysMsgf(nullptr != Entry, TEXT("Could not find data type [Name:%s]"), *InDataType.ToString()))
				{
					return Entry->CreateOutputNode(MoveTemp(InParams));
				}
				else
				{
					return nullptr;
				}
			}

			Metasound::FLiteral FRegistryContainerImpl::CreateLiteralFromUObject(const FName& InDataType, UObject* InObject)
			{
				const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType);

				if (ensureAlwaysMsgf(nullptr != Entry, TEXT("Could not find data type [Name:%s]"), *InDataType.ToString()))

				{
					if (Audio::IProxyDataPtr ProxyPtr = Entry->CreateProxy(InObject))
					{
						if (InObject)
						{
							ensureAlwaysMsgf(ProxyPtr.IsValid(), TEXT("UObject failed to create a valid proxy."));
						}
						return Metasound::FLiteral(MoveTemp(ProxyPtr));
					}
				}

				return Metasound::FLiteral();
			}

			Metasound::FLiteral FRegistryContainerImpl::CreateLiteralFromUObjectArray(const FName& InDataType, TArray<UObject*> InObjectArray)
			{
				const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType);

				if (ensureAlwaysMsgf(nullptr != Entry, TEXT("Could not find data type [Name:%s]"), *InDataType.ToString()))

				{
					TArray<Audio::IProxyDataPtr> ProxyArray;

					for (UObject* InObject : InObjectArray)
					{
						Audio::IProxyDataPtr ProxyPtr = Entry->CreateProxy(InObject);

						if (InObject)
						{
							ensureAlwaysMsgf(ProxyPtr.IsValid(), TEXT("UObject failed to create a valid proxy!"));
						}

						ProxyArray.Add(MoveTemp(ProxyPtr));
					}

					return Metasound::FLiteral(MoveTemp(ProxyArray));
				}
				else
				{
					return Metasound::FLiteral();
				}
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

			Metasound::ELiteralType FRegistryContainerImpl::GetDesiredLiteralTypeForDataType(FName InDataType) const
			{
				const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType);

				if (nullptr == Entry)
				{
					return Metasound::ELiteralType::Invalid;
				}

				const FDataTypeRegistryInfo& Info = Entry->GetDataTypeInfo();

				// If there's a designated preferred literal type for this datatype, use that.
				if (Info.PreferredLiteralType != Metasound::ELiteralType::Invalid)
				{
					return Info.PreferredLiteralType;
				}

				// Otherwise, we opt for the highest precision construction option available.
				return Metasound::Frontend::MetasoundFrontendRegistryPrivate::GetMostDescriptiveLiteralForDataType(Info);
			}

			UClass* FRegistryContainerImpl::GetLiteralUClassForDataType(FName InDataType) const
			{
				const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType);

				if (ensureAlwaysMsgf(nullptr != Entry, TEXT("Could not find data type [Name:%s]."), *InDataType.ToString()))
				{
					return Entry->GetDataTypeInfo().ProxyGeneratorClass;
				}

				return nullptr;
			}

			bool FRegistryContainerImpl::DoesDataTypeSupportLiteralType(FName InDataType, Metasound::ELiteralType InLiteralType) const
			{
				const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType);

				if (ensureAlwaysMsgf(nullptr != Entry, TEXT("Could not find data type [Name:%s]."), *InDataType.ToString()))
				{
					const FDataTypeRegistryInfo& Info = Entry->GetDataTypeInfo();

					switch (InLiteralType)
					{
						case Metasound::ELiteralType::Boolean:
						{
							return Info.bIsBoolParsable;
						}
						case Metasound::ELiteralType::BooleanArray:
						{
							return Info.bIsBoolArrayParsable;
						}

						case Metasound::ELiteralType::Integer:
						{
							return Info.bIsIntParsable;
						}
						case Metasound::ELiteralType::IntegerArray:
						{
							return Info.bIsIntArrayParsable;
						}

						case Metasound::ELiteralType::Float:
						{
							return Info.bIsFloatParsable;
						}
						case Metasound::ELiteralType::FloatArray:
						{
							return Info.bIsFloatArrayParsable;
						}

						case Metasound::ELiteralType::String:
						{
							return Info.bIsStringParsable;
						}
						case Metasound::ELiteralType::StringArray:
						{
							return Info.bIsStringArrayParsable;
						}

						case Metasound::ELiteralType::UObjectProxy:
						{
							return Info.bIsProxyParsable;
						}
						case Metasound::ELiteralType::UObjectProxyArray:
						{
							return Info.bIsProxyArrayParsable;
						}

						case Metasound::ELiteralType::None:
						{
							return Info.bIsDefaultParsable;
						}
						case Metasound::ELiteralType::NoneArray:
						{
							return Info.bIsDefaultArrayParsable;
						}

						case Metasound::ELiteralType::Invalid:
						default:
						{
							static_assert(static_cast<int32>(Metasound::ELiteralType::COUNT) == 13, "Possible missing case coverage for ELiteralType");
							return false;
						}
					}
				}

				return false;
			}

			bool FRegistryContainerImpl::RegisterDataType(const FName& InName, TUniquePtr<IDataTypeRegistryEntry>&& InRegistryEntry)
			{
				if (InRegistryEntry.IsValid())
				{
					if (!ensureAlwaysMsgf(!RegisteredDataTypes.Contains(InName),
						TEXT("Name collision when trying to register Metasound Data Type [Name:%s]. DataType must have "
							"unique name and REGISTER_METASOUND_DATATYPE cannot be called in a public header."),
							*InName.ToString()))
					{
						return false;
					}


					RegisteredDataTypes.Add(InName, InRegistryEntry->Clone());

					RegisterNode(MakeUnique<FInputNodeRegistryEntry>(InRegistryEntry->Clone()));
					RegisterNode(MakeUnique<FOutputNodeRegistryEntry>(InRegistryEntry->Clone()));
					RegisterNode(MakeUnique<FVariableNodeRegistryEntry>(MoveTemp(InRegistryEntry)));

					UE_LOG(LogMetaSound, Verbose, TEXT("Registered Metasound Datatype [Name:%s]."), *InName.ToString());
					return true;
				}

				return false;
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
						TEXT("Node with registry key already registered. "
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

			TArray<FName> FRegistryContainerImpl::GetAllValidDataTypes()
			{
				TArray<FName> OutDataTypes;

				for (auto& DataTypeTuple : RegisteredDataTypes)
				{
					OutDataTypes.Add(DataTypeTuple.Key);
				}

				return OutDataTypes;
			}

			bool FRegistryContainerImpl::GetInfoForDataType(FName InDataType, Metasound::Frontend::FDataTypeRegistryInfo& OutInfo)
			{
				if (const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType))
				{
					OutInfo = Entry->GetDataTypeInfo();
					return true;
				}
				return false;
			}

			TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> FRegistryContainerImpl::GetEnumInterfaceForDataType(FName InDataType) const
			{
				if (const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType))
				{
					return Entry->GetEnumInterface();
				}
				return nullptr;
			}

			TSharedPtr<Metasound::IDataChannel, ESPMode::ThreadSafe> FRegistryContainerImpl::CreateDataChannelForDataType(const FName& InDataType, const Metasound::FOperatorSettings& InSettings) const
			{
				if (const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataType))
				{
					return Entry->CreateDataChannel(InSettings);
				}
				return nullptr;
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
				if (const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataTypeName))
				{
					OutKey = NodeRegistryKey::CreateKey(Entry->GetFrontendInputClass().Metadata);
					return true;
				}
				return false;
			}

			bool FRegistryContainerImpl::FindVariableNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey)
			{
				if (const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataTypeName))
				{
					OutKey = NodeRegistryKey::CreateKey(Entry->GetFrontendVariableClass().Metadata);
					return true;
				}
				return false;
			}

			bool FRegistryContainerImpl::FindOutputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey)
			{
				if (const IDataTypeRegistryEntry* Entry = FindDataTypeEntry(InDataTypeName))
				{
					OutKey = NodeRegistryKey::CreateKey(Entry->GetFrontendOutputClass().Metadata);
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

			const IDataTypeRegistryEntry* FRegistryContainerImpl::FindDataTypeEntry(const FName& InDataTypeName) const
			{
				if (const TUniquePtr<IDataTypeRegistryEntry>* Entry = RegisteredDataTypes.Find(InDataTypeName))
				{
					if (Entry->IsValid())
					{
						return Entry->Get();
					}
				}

				return nullptr;
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

