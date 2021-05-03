// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendRegistries.h"

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
			// All registry keys should be created through this function to ensure consistency.
			Frontend::FNodeRegistryKey GetRegistryKey(const FName& InClassName, int32 InMajorVersion)
			{
				Frontend::FNodeRegistryKey Key;

				Key.NodeClassFullName = InClassName;

				// NodeHash is hash of node name and major version.
				Key.NodeHash = FCrc::StrCrc32(*Key.NodeClassFullName.ToString());
				Key.NodeHash = HashCombine(Key.NodeHash, FCrc::TypeCrc32(InMajorVersion));

				return Key;
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

			// Registry container private implementation.
			class FRegistryContainerImpl : public FMetasoundFrontendRegistryContainer
			{

			public:
				using FConverterNodeRegistryKey = ::Metasound::Frontend::FConverterNodeRegistryKey;
				using FConverterNodeRegistryValue = ::Metasound::Frontend::FConverterNodeRegistryValue;
				using FConverterNodeInfo = ::Metasound::Frontend::FConverterNodeInfo;

				using FNodeRegistryKey = Metasound::Frontend::FNodeRegistryKey;
				using FNodeRegistryElement = Metasound::Frontend::FNodeRegistryElement;

				using FDataTypeRegistryInfo = Metasound::FDataTypeRegistryInfo;
				using FDataTypeConstructorCallbacks = ::Metasound::FDataTypeConstructorCallbacks;
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
				bool RegisterExternalNode(Metasound::FCreateMetasoundNodeFunction&& InCreateNode, Metasound::FCreateMetasoundFrontendClassFunction&& InCreateDescription) override;
				bool RegisterConversionNode(const FConverterNodeRegistryKey& InNodeKey, const FConverterNodeInfo& InNodeInfo) override;
				bool IsNodeRegistered(const FNodeRegistryKey& InKey) const override;

				// Get all available nodes
				TArray<Frontend::FNodeClassInfo> GetAllAvailableNodeClasses(Frontend::FRegistryTransactionID* OutCurrentRegistryTransactionID) const override;
				TArray<Metasound::Frontend::FNodeClassInfo> GetNodeClassesRegisteredSince(Metasound::Frontend::FRegistryTransactionID InSince, Metasound::Frontend::FRegistryTransactionID* OutCurrentRegistryTransactionID) const override;

				// Return any data types that can be used as a metasound input type or output type.
				TArray<FName> GetAllValidDataTypes() override;

				void IterateRegistry(Metasound::FIterateMetasoundFrontendClassFunction InIterFunc, EMetasoundFrontendClassType InClassType = EMetasoundFrontendClassType::Invalid) const override;

				// Find Frontend Document data.
				bool FindFrontendClassFromRegistered(const FNodeClassInfo& InClassInfo, FMetasoundFrontendClass& OutClass) override;
				bool FindFrontendClassFromRegistered(const FMetasoundFrontendClassMetadata& InMetadata, FMetasoundFrontendClass& OutClass) override;
				bool FindInputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata) override;
				bool FindOutputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata) override;

				// Create a new instance of a C++ implemented node from the registry.
				TUniquePtr<Metasound::INode> ConstructInputNode(const FName& InInputType, Metasound::FInputNodeConstructorParams&& InParams) override;
				TUniquePtr<Metasound::INode> ConstructOutputNode(const FName& InOutputType, const Metasound::FOutputNodeConstructorParams& InParams) override;
				TUniquePtr<Metasound::INode> ConstructExternalNode(const FNodeRegistryKey& InKey, const Metasound::FNodeInitData& InInitData) override;

				// Returns a list of possible nodes to use to convert from FromDataType to ToDataType.
				// Returns an empty array if none are available.
				TArray<FConverterNodeInfo> GetPossibleConverterNodes(const FName& FromDataType, const FName& ToDataType) override;

				bool RegisterDataType(const FDataTypeRegistryInfo& InDataInfo, const FDataTypeConstructorCallbacks& InCallbacks) override;

				bool RegisterEnumDataInterface(FName InDataType, TSharedPtr<IEnumDataTypeInterface>&& InInterface) override;

				// Get the desired kind of literal for a given data type. Returns EConstructorArgType::Invalid if the data type couldn't be found.
				Metasound::ELiteralType GetDesiredLiteralTypeForDataType(FName InDataType) const override;

				// Get whether we can build a literal of this specific type for InDataType.
				bool DoesDataTypeSupportLiteralType(FName InDataType, Metasound::ELiteralType InLiteralType) const override;

				// Handle uobjects and literals
				UClass* GetLiteralUClassForDataType(FName InDataType) const override;
				Metasound::FLiteral GenerateLiteralForUObject(const FName& InDataType, UObject* InObject) override;
				Metasound::FLiteral GenerateLiteralForUObjectArray(const FName& InDataType, TArray<UObject*> InObjectArray) override;

				// Get info about a specific data type (what kind of literals we can use, etc.)
				// @returns false if InDataType wasn't found in the registry. 
				bool GetInfoForDataType(FName InDataType, FDataTypeRegistryInfo& OutInfo) override;

				TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> GetEnumInterfaceForDataType(FName InDataType) const override;

				TSharedPtr<Metasound::IDataChannel, ESPMode::ThreadSafe> CreateDataChannelForDataType(const FName& InDataType, const Metasound::FOperatorSettings& InOperatorSettings) const override;
			private:


				// This buffer is used to enqueue nodes and datatypes to register when the module has been initialized,
				// in order to avoid bad behavior with ensures, logs, etc. on static initialization.
				// The bad news is that TInlineAllocator is the safest allocator to use on static init.
				// The good news is that none of these lambdas typically have captures, so this should have low memory overhead.
				static constexpr int32 MaxNumNodesAndDatatypesToInitialize = 8192;
				TArray<TUniqueFunction<void()>, TInlineAllocator<MaxNumNodesAndDatatypesToInitialize>> LazyInitCommands;
				
				FCriticalSection LazyInitCommandCritSection;

				// Registry in which we keep all information about nodes implemented in C++.
				TMap<FNodeRegistryKey, FNodeRegistryElement> ExternalNodeRegistry;

				// Registry in which we keep lists of possible nodes to use to convert between two datatypes
				TMap<FConverterNodeRegistryKey, FConverterNodeRegistryValue> ConverterNodeRegistry;

				struct FDataTypeRegistryElement
				{
					Metasound::FDataTypeConstructorCallbacks Callbacks;

					Metasound::FDataTypeRegistryInfo Info;

					TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> EnumInterface;
				};

				TMap<FName, FDataTypeRegistryElement> DataTypeRegistry;
				TMap<FNodeRegistryKey, FDataTypeRegistryElement> DataTypeNodeRegistry;
				
				FRegistryTransactionHistory RegistryTransactionHistory;
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

			TArray<FNodeClassInfo> FRegistryContainerImpl::GetAllAvailableNodeClasses(FRegistryTransactionID* OutCurrentRegistryTransactionID) const 
			{
				TArray<FNodeClassInfo> OutClasses;

				TArray<const IRegistryTransaction*> Transactions = RegistryTransactionHistory.GetTransactions(GetOriginRegistryTransactionID(), OutCurrentRegistryTransactionID);

				for (const IRegistryTransaction* Transaction : Transactions)
				{
					if (const FNodeClassInfo* Info = Transaction->GetNodeClassInfo())
					{
						OutClasses.Add(*Info);
					}
				}

				return OutClasses;
			}

			TArray<Metasound::Frontend::FNodeClassInfo> FRegistryContainerImpl::GetNodeClassesRegisteredSince(Metasound::Frontend::FRegistryTransactionID InSince, Metasound::Frontend::FRegistryTransactionID* OutCurrentRegistryTransactionID) const 
			{
				TArray<FNodeClassInfo> OutClasses;

				TArray<const IRegistryTransaction*> Transactions = RegistryTransactionHistory.GetTransactions(InSince, OutCurrentRegistryTransactionID);

				for (const IRegistryTransaction* Transaction : Transactions)
				{
					if (const FNodeClassInfo* Info = Transaction->GetNodeClassInfo())
					{
						OutClasses.Add(*Info);
					}
				}

				return OutClasses;
			}


			TUniquePtr<Metasound::INode> FRegistryContainerImpl::ConstructInputNode(const FName& InInputType, Metasound::FInputNodeConstructorParams&& InParams)
			{
				if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InInputType), TEXT("Couldn't find data type %s!"), *InInputType.ToString()))
				{
					return DataTypeRegistry[InInputType].Callbacks.CreateInputNode(MoveTemp(InParams));
				}
				else
				{
					return nullptr;
				}
			}

			TUniquePtr<Metasound::INode> FRegistryContainerImpl::ConstructOutputNode(const FName& InOutputType, const Metasound::FOutputNodeConstructorParams& InParams)
			{
				if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InOutputType), TEXT("Couldn't find data type %s!"), *InOutputType.ToString()))
				{
					return DataTypeRegistry[InOutputType].Callbacks.CreateOutputNode(InParams);
				}
				else
				{
					return nullptr;
				}
			}

			Metasound::FLiteral FRegistryContainerImpl::GenerateLiteralForUObject(const FName& InDataType, UObject* InObject)
			{
				if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InDataType), TEXT("Couldn't find data type %s!"), *InDataType.ToString()))
				{
					if (Audio::IProxyDataPtr ProxyPtr = DataTypeRegistry[InDataType].Callbacks.CreateAudioProxy(InObject))
					{
						if (InObject)
						{
							ensureAlwaysMsgf(ProxyPtr.IsValid(), TEXT("UObject failed to create a valid proxy!"));
						}
						return Metasound::FLiteral(MoveTemp(ProxyPtr));
					}
				}

				return Metasound::FLiteral();
			}

			Metasound::FLiteral FRegistryContainerImpl::GenerateLiteralForUObjectArray(const FName& InDataType, TArray<UObject*> InObjectArray)
			{
				if (ensureAlwaysMsgf(DataTypeRegistry.Contains(InDataType), TEXT("Couldn't find data type %s!"), *InDataType.ToString()))
				{
					TArray<Audio::IProxyDataPtr> ProxyArray;

					for (UObject* InObject : InObjectArray)
					{
						Audio::IProxyDataPtr ProxyPtr = DataTypeRegistry[InDataType].Callbacks.CreateAudioProxy(InObject);

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

			TUniquePtr<Metasound::INode> FRegistryContainerImpl::ConstructExternalNode(const FNodeRegistryKey& InKey, const Metasound::FNodeInitData& InInitData)
			{
				if (!ExternalNodeRegistry.Contains(InKey))
				{
					return nullptr;
				}
				else
				{
					return ExternalNodeRegistry[InKey].CreateNode(InInitData);
				}
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
				if (!DataTypeRegistry.Contains(InDataType))
				{
					return Metasound::ELiteralType::Invalid;
				}

				const FDataTypeRegistryElement& DataTypeInfo = DataTypeRegistry[InDataType];

				// If there's a designated preferred literal type for this datatype, use that.
				if (DataTypeInfo.Info.PreferredLiteralType != Metasound::ELiteralType::Invalid)
				{
					return DataTypeInfo.Info.PreferredLiteralType;
				}

				// Otherwise, we opt for the highest precision construction option available.
				return Metasound::Frontend::MetasoundFrontendRegistryPrivate::GetMostDescriptiveLiteralForDataType(DataTypeInfo.Info);
			}

			UClass* FRegistryContainerImpl::GetLiteralUClassForDataType(FName InDataType) const
			{
				if (!DataTypeRegistry.Contains(InDataType))
				{
					ensureAlwaysMsgf(false, TEXT("DataType %s not registered."), *InDataType.ToString());
					return nullptr;
				}
				else
				{
					return DataTypeRegistry[InDataType].Info.ProxyGeneratorClass;
				}
			}

			bool FRegistryContainerImpl::DoesDataTypeSupportLiteralType(FName InDataType, Metasound::ELiteralType InLiteralType) const
			{
				if (!DataTypeRegistry.Contains(InDataType))
				{
					ensureAlwaysMsgf(false, TEXT("DataType %s not registered."), *InDataType.ToString());
					return false;
				}

				const FDataTypeRegistryElement& DataTypeInfo = DataTypeRegistry[InDataType];
				
				switch (InLiteralType)
				{
					case Metasound::ELiteralType::Boolean:
					{
						return DataTypeInfo.Info.bIsBoolParsable;
					}
					case Metasound::ELiteralType::BooleanArray:
					{
						return DataTypeInfo.Info.bIsBoolArrayParsable;
					}

					case Metasound::ELiteralType::Integer:
					{
						return DataTypeInfo.Info.bIsIntParsable;
					}
					case Metasound::ELiteralType::IntegerArray:
					{
						return DataTypeInfo.Info.bIsIntArrayParsable;
					}

					case Metasound::ELiteralType::Float:
					{
						return DataTypeInfo.Info.bIsFloatParsable;
					}
					case Metasound::ELiteralType::FloatArray:
					{
						return DataTypeInfo.Info.bIsFloatArrayParsable;
					}

					case Metasound::ELiteralType::String:
					{
						return DataTypeInfo.Info.bIsStringParsable;
					}
					case Metasound::ELiteralType::StringArray:
					{
						return DataTypeInfo.Info.bIsStringArrayParsable;
					}

					case Metasound::ELiteralType::UObjectProxy:
					{
						return DataTypeInfo.Info.bIsProxyParsable;
					}
					case Metasound::ELiteralType::UObjectProxyArray:
					{
						return DataTypeInfo.Info.bIsProxyArrayParsable;
					}

					case Metasound::ELiteralType::None:
					{
						return DataTypeInfo.Info.bIsDefaultParsable;
					}
					case Metasound::ELiteralType::NoneArray:
					{
						return DataTypeInfo.Info.bIsDefaultArrayParsable;
					}

					case Metasound::ELiteralType::Invalid:
					default:
					{
						static_assert(static_cast<int32>(Metasound::ELiteralType::COUNT) == 13, "Possible missing case coverage for ELiteralType");
						return false;
					}
				}
			}

			bool FRegistryContainerImpl::RegisterDataType(const ::Metasound::FDataTypeRegistryInfo& InDataInfo, const ::Metasound::FDataTypeConstructorCallbacks& InCallbacks)
			{
				if (!ensureAlwaysMsgf(!DataTypeRegistry.Contains(InDataInfo.DataTypeName),
					TEXT("Name collision when trying to register Metasound Data Type %s! DataType must have "
						"unique name and REGISTER_METASOUND_DATATYPE cannot be called in a public header."),
						*InDataInfo.DataTypeName.ToString()))
				{
					// todo: capture callstack for previous declaration for non-shipping builds to help clarify who already registered this name for a type.
					return false;
				}
				else
				{
					FDataTypeRegistryElement InElement = { InCallbacks, InDataInfo };

					DataTypeRegistry.Add(InDataInfo.DataTypeName, InElement);

					Metasound::Frontend::FNodeRegistryKey InputNodeRegistryKey = GetRegistryKey(InCallbacks.CreateFrontendInputClass().Metadata);
					DataTypeNodeRegistry.Add(InputNodeRegistryKey, InElement);

					Metasound::Frontend::FNodeRegistryKey OutputNodeRegistryKey = GetRegistryKey(InCallbacks.CreateFrontendOutputClass().Metadata);
					DataTypeNodeRegistry.Add(OutputNodeRegistryKey, InElement);

					UE_LOG(LogMetaSound, Display, TEXT("Registered Metasound Datatype %s."), *InDataInfo.DataTypeName.ToString());
					return true;
				}
			}

			bool FRegistryContainerImpl::RegisterEnumDataInterface(FName InDataType, TSharedPtr<IEnumDataTypeInterface>&& InInterface)
			{
				if (FDataTypeRegistryElement* Element = DataTypeRegistry.Find(InDataType))
				{
					Element->EnumInterface = MoveTemp(InInterface);
					return true;
				}
				return false;
			}

			bool FRegistryContainerImpl::RegisterExternalNode(Metasound::FCreateMetasoundNodeFunction&& InCreateNode, Metasound::FCreateMetasoundFrontendClassFunction&& InCreateDescription)
			{
				FNodeRegistryElement RegistryElement = FNodeRegistryElement(MoveTemp(InCreateNode), MoveTemp(InCreateDescription));

				FNodeRegistryKey Key;

				if (ensure(FMetasoundFrontendRegistryContainer::GetRegistryKey(RegistryElement, Key)))
				{
					// check to see if an identical node was already registered, and log
					ensureAlwaysMsgf(
						!ExternalNodeRegistry.Contains(Key),
						TEXT("Node with identical name '%s' & Major Version already registered. "
							"The previously registered node will be overwritten. "
							"This can happen if two classes share the same name or if METASOUND_REGISTER_NODE is defined in a public header."),
						*Key.NodeClassFullName.ToString());

					// Store update to newly registered node in history so nodes
					// can be queried by transaction ID
					FNodeClassInfo ClassInfo = { EMetasoundFrontendClassType::External, Key };
					RegistryTransactionHistory.Add(MakeNodeRegistrationTransaction(ClassInfo));

					// Store registry elements in map so nodes can be queried using registry key.
					ExternalNodeRegistry.Add(MoveTemp(Key), MoveTemp(RegistryElement));

					return true;
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
				return ExternalNodeRegistry.Contains(InKey);
			}

			TArray<FName> FRegistryContainerImpl::GetAllValidDataTypes()
			{
				TArray<FName> OutDataTypes;

				for (auto& DataTypeTuple : DataTypeRegistry)
				{
					OutDataTypes.Add(DataTypeTuple.Key);
				}

				return OutDataTypes;
			}

			bool FRegistryContainerImpl::GetInfoForDataType(FName InDataType, Metasound::FDataTypeRegistryInfo& OutInfo)
			{
				if (!DataTypeRegistry.Contains(InDataType))
				{
					return false;
				}
				else
				{
					OutInfo = DataTypeRegistry[InDataType].Info;
					return true;
				}
			}


			TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface>
			FRegistryContainerImpl::GetEnumInterfaceForDataType(FName InDataType) const
			{
				if (const FDataTypeRegistryElement* Element = DataTypeRegistry.Find(InDataType))
				{
					return Element->EnumInterface;
				}
				return nullptr;
			}

			TSharedPtr<Metasound::IDataChannel, ESPMode::ThreadSafe> FRegistryContainerImpl::CreateDataChannelForDataType(const FName& InDataType, const Metasound::FOperatorSettings& InSettings) const
			{
				if (const FDataTypeRegistryElement* Element = DataTypeRegistry.Find(InDataType))
				{
					if (Element->Callbacks.CreateDataChannel)
					{
						return Element->Callbacks.CreateDataChannel(InSettings);
					}
				}
				return nullptr;
			}

			bool FRegistryContainerImpl::FindFrontendClassFromRegistered(const FNodeClassInfo& InClassInfo, FMetasoundFrontendClass& OutClass)
			{
				if (InClassInfo.NodeType == EMetasoundFrontendClassType::External)
				{
					if (ExternalNodeRegistry.Contains(InClassInfo.LookupKey))
					{
						OutClass = ExternalNodeRegistry[InClassInfo.LookupKey].CreateFrontendClass();
						return true;
					}
				}
				else if (InClassInfo.NodeType == EMetasoundFrontendClassType::Input)
				{
					if (DataTypeNodeRegistry.Contains(InClassInfo.LookupKey))
					{
						OutClass = DataTypeNodeRegistry[InClassInfo.LookupKey].Callbacks.CreateFrontendInputClass();
						return true;
					}
				}
				else if (InClassInfo.NodeType == EMetasoundFrontendClassType::Output)
				{
					if (DataTypeNodeRegistry.Contains(InClassInfo.LookupKey))
					{
						OutClass = DataTypeNodeRegistry[InClassInfo.LookupKey].Callbacks.CreateFrontendOutputClass();
						return true;
					}
				}

				return false;
			}

			bool FRegistryContainerImpl::FindFrontendClassFromRegistered(const FMetasoundFrontendClassMetadata& InMetadata, FMetasoundFrontendClass& OutClass)
			{
				FNodeClassInfo Info;
				Info.NodeType = InMetadata.Type;
				Info.LookupKey = GetRegistryKey(InMetadata);

				return FindFrontendClassFromRegistered(Info, OutClass);
			}

			bool FRegistryContainerImpl::FindInputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata)
			{
				if (DataTypeRegistry.Contains(InDataTypeName))
				{
					OutMetadata = DataTypeRegistry[InDataTypeName].Callbacks.CreateFrontendInputClass().Metadata;
					return true;
				}
				return false;
			}

			bool FRegistryContainerImpl::FindOutputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata)
			{
				if (DataTypeRegistry.Contains(InDataTypeName))
				{
					OutMetadata = DataTypeRegistry[InDataTypeName].Callbacks.CreateFrontendOutputClass().Metadata;
					return true;
				}
				return false;
			}

			void FRegistryContainerImpl::IterateRegistry(Metasound::FIterateMetasoundFrontendClassFunction InIterFunc, EMetasoundFrontendClassType InClassType) const
			{
				auto IterateExternalClasses = [=]
				{
					for (const TPair<FNodeRegistryKey, FNodeRegistryElement>& Pair : ExternalNodeRegistry)
					{
						InIterFunc(Pair.Value.CreateFrontendClass());
					}
				};

				auto IterateInputClasses = [=]
				{
					for (const TPair<FNodeRegistryKey, FDataTypeRegistryElement>& Pair : DataTypeNodeRegistry)
					{
						InIterFunc(Pair.Value.Callbacks.CreateFrontendInputClass());
					}
				};

				auto IterateOutputClasses = [=]
				{
					for (const TPair<FNodeRegistryKey, FDataTypeRegistryElement>& Pair : DataTypeNodeRegistry)
					{
						InIterFunc(Pair.Value.Callbacks.CreateFrontendOutputClass());
					}
				};

				switch (InClassType)
				{
					case EMetasoundFrontendClassType::External:
					{
						IterateExternalClasses();
					}
					break;

					// TODO: Implement
					case EMetasoundFrontendClassType::Graph:
					{
						checkNoEntry();
					}
					break;

					case EMetasoundFrontendClassType::Input:
					{
						IterateInputClasses();
					}
					break;

					case EMetasoundFrontendClassType::Output:
					{
						IterateOutputClasses();
					}
					break;

					default:
					case EMetasoundFrontendClassType::Invalid:
					{
						IterateExternalClasses();
						IterateInputClasses();
						IterateOutputClasses();
					}
					break;
				}
				static_assert(static_cast<uint32>(EMetasoundFrontendClassType::Invalid) == 4, "Possible missing switch case coverage for EMetasoundFrontendClassType");
			}
		}
	}
}

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

bool FMetasoundFrontendRegistryContainer::GetRegistryKey(const Metasound::Frontend::FNodeRegistryElement& InElement, Metasound::Frontend::FNodeRegistryKey& OutKey)
{
	if (InElement.CreateFrontendClass)
	{
		FMetasoundFrontendClass FrontendClass = InElement.CreateFrontendClass();

		OutKey = GetRegistryKey(FrontendClass.Metadata);

		return true;
	}

	return false;
}

Metasound::Frontend::FNodeRegistryKey FMetasoundFrontendRegistryContainer::GetRegistryKey(const FNodeClassMetadata& InNodeMetadata)
{
	return Metasound::Frontend::MetasoundFrontendRegistryPrivate::GetRegistryKey(InNodeMetadata.ClassName.GetFullName(), InNodeMetadata.MajorVersion);
}

Metasound::Frontend::FNodeRegistryKey FMetasoundFrontendRegistryContainer::GetRegistryKey(const FMetasoundFrontendClassMetadata& InNodeMetadata)
{
	return Metasound::Frontend::MetasoundFrontendRegistryPrivate::GetRegistryKey(InNodeMetadata.ClassName.GetFullName(), InNodeMetadata.Version.Major);
}

bool FMetasoundFrontendRegistryContainer::GetFrontendClassFromRegistered(const FMetasoundFrontendClassMetadata& InMetadata, FMetasoundFrontendClass& OutClass)
{
	FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();

	if (ensure(nullptr != Registry))
	{
		return Registry->FindFrontendClassFromRegistered(InMetadata, OutClass);
	}

	return false;
}

bool FMetasoundFrontendRegistryContainer::GetInputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata)
{
	if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
	{
		return Registry->FindInputNodeClassMetadataForDataType(InDataTypeName, OutMetadata);
	}
	return false;
}

bool FMetasoundFrontendRegistryContainer::GetOutputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata)
{
	if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
	{
		return Registry->FindOutputNodeClassMetadataForDataType(InDataTypeName, OutMetadata);
	}
	return false;
}

