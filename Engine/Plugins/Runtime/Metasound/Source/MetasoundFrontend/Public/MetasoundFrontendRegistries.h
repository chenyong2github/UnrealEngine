// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioProxyInitializer.h"
#include "MetasoundDataReference.h"
#include "MetasoundEnum.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundLiteral.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundRouter.h"
#include "Templates/Function.h"


namespace Metasound
{
	// Base implementation for NodeConstructorCallbacks
	struct FDefaultNodeConstructorParams
	{
		// the instance name and name of the specific connection that should be used.
		FString NodeName;
		FGuid InstanceID;
		FString VertexName;
	};

	struct FDefaultLiteralNodeConstructorParams
	{
		// the instance name and name of the specific connection that should be used.
		FString NodeName;
		FGuid InstanceID;
		FString VertexName;
		FLiteral InitParam = FLiteral::CreateInvalid();
	};

	using FOutputNodeConstructorParams = FDefaultNodeConstructorParams;
	using FVariableNodeConstructorParams = FDefaultLiteralNodeConstructorParams;
	using FInputNodeConstructorParams = FDefaultLiteralNodeConstructorParams;
	using FReceiveNodeConstructorParams = FNodeInitData;

	using FIterateMetasoundFrontendClassFunction = TFunctionRef<void(const FMetasoundFrontendClass&)>;

	namespace Frontend
	{
		using FNodeRegistryKey = FString;


		// Returns true if the registry key is a valid key. 
		//
		// This does *not* connote that the registry key exists in the registry.
		METASOUNDFRONTEND_API bool IsValidNodeRegistryKey(const FNodeRegistryKey& InKey);

		/** FNodeClassInfo contains a minimal set of information needed to find
		 * and query node classes. 
		 */
		struct METASOUNDFRONTEND_API FNodeClassInfo
		{
			// ClassName of the given class
			FMetasoundFrontendClassName ClassName;

			// The type of this node class
			EMetasoundFrontendClassType Type = EMetasoundFrontendClassType::Invalid;

			// The ID used for the Asset Classes. If zero, class is natively defined.
			FGuid AssetClassID;

			// Path to asset containing graph if external type and references asset class.
			FName AssetPath;

			// Version of the registered class
			FMetasoundFrontendVersionNumber Version;

			// Types of class inputs
			TArray<FName> InputTypes;

			// Types of class outputs
			TArray<FName> OutputTypes;

			FNodeClassInfo() = default;

			// Constructor used to generate NodeClassInfo from a native class' Metadata.
			FNodeClassInfo(const FMetasoundFrontendClassMetadata& InMetadata);

			// Constructor used to generate NodeClassInfo from an asset
			FNodeClassInfo(const FMetasoundFrontendGraphClass& InClass, FName InAssetPath);

			// Loads the asset from the provided path, ensuring that the class is of type graph.
			UObject* LoadAsset() const
			{
				if (ensure(Type == EMetasoundFrontendClassType::External))
				{
					FSoftObjectPath SoftObjectPath(AssetPath);
					return SoftObjectPath.TryLoad();
				}

				return nullptr;
			}
		};

		/** INodeRegistryEntry declares the interface for a node registry entry.
		 * Each node class in the registry must satisfy this interface. 
		 */
		class INodeRegistryEntry
		{
		public:
			virtual ~INodeRegistryEntry() = default;

			/** Return FNodeClassInfo for the node class.
			 *
			 * Implementations of method should avoid any expensive operations 
			 * (e.g. loading from disk, allocating memory) as this method is called
			 * frequently when querying nodes.
			 */
			virtual const FNodeClassInfo& GetClassInfo() const = 0;

			/** Create a node given FDefaultNodeConstructorParams.
			 *
			 * If a node can be created with FDefaultNodeConstructorParams, this function
			 * should return a valid node pointer.
			 */
			virtual TUniquePtr<INode> CreateNode(FDefaultNodeConstructorParams&&) const = 0;

			/** Create a node given FDefaultLiteralNodeConstructorParams.
			 *
			 * If a node can be created with FDefaultLiteralNodeConstructorParams, this function
			 * should return a valid node pointer.
			 */
			virtual TUniquePtr<INode> CreateNode(FDefaultLiteralNodeConstructorParams&&) const = 0;

			/** Create a node given FNodeInitData.
			 *
			 * If a node can be created with FNodeInitData, this function
			 * should return a valid node pointer.
			 */
			virtual TUniquePtr<INode> CreateNode(const FNodeInitData&) const = 0;

			/** Return a FMetasoundFrontendClass which describes the node. */
			virtual const FMetasoundFrontendClass& GetFrontendClass() const = 0;

			/** Clone this registry entry. */
			virtual TUniquePtr<INodeRegistryEntry> Clone() const = 0;

			/** Whether or not the node is natively defined */
			virtual bool IsNative() const = 0;
		};

		/** FDataTypeRegsitryInfo contains runtime inspectable behavior of a registered
		 * MetaSound data type.
		 */
		struct FDataTypeRegistryInfo
		{
			// The name of the data type.
			FName DataTypeName;

			// The preferred constructor argument type for creating instances of the data type.
			ELiteralType PreferredLiteralType = ELiteralType::Invalid;

			// Constructor argument support in TDataTypeLiteralFactory<TDataType>;
			bool bIsDefaultParsable = false;
			bool bIsBoolParsable = false;
			bool bIsIntParsable = false;
			bool bIsFloatParsable = false;
			bool bIsStringParsable = false;
			bool bIsProxyParsable = false;
			bool bIsDefaultArrayParsable = false;
			bool bIsBoolArrayParsable = false;
			bool bIsIntArrayParsable = false;
			bool bIsFloatArrayParsable = false;
			bool bIsStringArrayParsable = false;
			bool bIsProxyArrayParsable = false;

			// Is an TEnum wrapped enum
			bool bIsEnum = false;

			// Determines whether the type can be used with send/receive transmitters
			bool bIsTransmittable = false;

			// If this datatype was registered with a specific UClass to use to filter with, that will be used here:
			UClass* ProxyGeneratorClass = nullptr;

			FORCEINLINE bool IsArrayType() const
			{
				return bIsDefaultArrayParsable
				|| bIsBoolArrayParsable
				|| bIsIntArrayParsable
				|| bIsFloatArrayParsable
				|| bIsStringArrayParsable
				|| bIsProxyArrayParsable;
			}
		};

		/** Interface for metadata of a registered MetaSound enum type. */
		struct METASOUNDFRONTEND_API IEnumDataTypeInterface
		{
			using FGenericInt32Entry = TEnumEntry<int32>;

			virtual ~IEnumDataTypeInterface() = default;

			virtual const TArray<FGenericInt32Entry>& GetAllEntries() const = 0;
			virtual FName GetNamespace() const = 0;
			virtual int32 GetDefaultValue() const = 0;

			template<typename Predicate>
			TOptional<FGenericInt32Entry> FindEntryBy(Predicate Pred) const
			{
				TArray<FGenericInt32Entry> Entries = GetAllEntries();
				if (FGenericInt32Entry* Found = Entries.FindByPredicate(Pred))
				{
					return *Found;
				}
				return {};
			}
			TOptional<FGenericInt32Entry> FindByValue(int32 InEnumValue) const
			{
				return FindEntryBy([InEnumValue](const FGenericInt32Entry& i) -> bool { return i.Value == InEnumValue; });
			}
			TOptional<FGenericInt32Entry> FindByName(FName InEnumName) const
			{
				return FindEntryBy([InEnumName](const FGenericInt32Entry& i) -> bool { return i.Name == InEnumName; });
			}
			TOptional<FName> ToName(int32 InEnumValue) const
			{
				if(TOptional<FGenericInt32Entry> Result = FindByValue(InEnumValue))
				{
					return Result->Name;
				}
				return {};
			}
			TOptional<int32> ToValue(FName InName) const
			{
				if (TOptional<FGenericInt32Entry> Result = FindByName(InName))
				{
					return Result->Value;
				}
				return {};
			}
		};

		/** Registry entry interface for a MetaSound data type. */
		class IDataTypeRegistryEntry
		{
		public:
			virtual ~IDataTypeRegistryEntry() = default;

			/** Return the FDataTypeRegistryInfo for the data type */
			virtual const FDataTypeRegistryInfo& GetDataTypeInfo() const = 0;

			/** Return the enum interface for the data type. If the data type does
			 * not support an enum interface, the returned pointer should be invalid.
			 */
			virtual TSharedPtr<const IEnumDataTypeInterface> GetEnumInterface() const = 0;

			/** Return an FMetasoundFrontenClass representing an input node of the data type. */
			virtual const FMetasoundFrontendClass& GetFrontendInputClass() const = 0;

			/** Return an FMetasoundFrontenClass representing a variable node of the data type. */
			virtual const FMetasoundFrontendClass& GetFrontendVariableClass() const = 0;

			/** Return an FMetasoundFrontenClass representing an output node of the data type. */
			virtual const FMetasoundFrontendClass& GetFrontendOutputClass() const = 0;

			/** Create an input node */
			virtual TUniquePtr<INode> CreateInputNode(FInputNodeConstructorParams&&) const = 0;

			/** Create an output node */
			virtual TUniquePtr<INode> CreateOutputNode(FOutputNodeConstructorParams&&) const = 0;
			
			/** Create a variable node */
			virtual TUniquePtr<INode> CreateVariableNode(FVariableNodeConstructorParams&&) const = 0;

			/** Create a recevie node. */
			virtual TUniquePtr<INode> CreateReceiveNode(FReceiveNodeConstructorParams&&) const = 0;

			/** Create a proxy from a UObject. If this data type does not support
			 * UObject proxies, return a nullptr. */
			virtual Audio::IProxyDataPtr CreateProxy(UObject*) const = 0;

			/** Create a data channel for transmission. If this data type does not
			 * support transmission, return a nullptr. */
			virtual TSharedPtr<IDataChannel, ESPMode::ThreadSafe> CreateDataChannel(const FOperatorSettings&) const = 0;

			/** Clone this registry entry. */
			virtual TUniquePtr<IDataTypeRegistryEntry> Clone() const = 0;
		};

		struct METASOUNDFRONTEND_API FConverterNodeRegistryKey
		{
			// The datatype one would like to convert from.
			FName FromDataType;

			// The datatype one would like to convert to.
			FName ToDataType;

			FORCEINLINE bool operator==(const FConverterNodeRegistryKey& Other) const
			{
				return FromDataType == Other.FromDataType && ToDataType == Other.ToDataType;
			}

			friend uint32 GetTypeHash(const ::Metasound::Frontend::FConverterNodeRegistryKey& InKey)
			{
				return HashCombine(GetTypeHash(InKey.FromDataType), GetTypeHash(InKey.ToDataType));
			}
		};

		struct METASOUNDFRONTEND_API FConverterNodeInfo
		{
			// If this node has multiple input pins, we use this to designate which pin should be used.
			FVertexKey PreferredConverterInputPin;

			// If this node has multiple output pins, we use this to designate which pin should be used.
			FVertexKey PreferredConverterOutputPin;

			// The key for this node in the node registry.
			FNodeRegistryKey NodeKey;

			FORCEINLINE bool operator==(const FConverterNodeInfo& Other) const
			{
				return NodeKey == Other.NodeKey;
			}
		};

		struct METASOUNDFRONTEND_API FConverterNodeRegistryValue
		{
			// A list of nodes that can perform a conversion between the two datatypes described in the FConverterNodeRegistryKey for this map element.
			TArray<FConverterNodeInfo> PotentialConverterNodes;
		};

		using FRegistryTransactionID = int32;

		class METASOUNDFRONTEND_API FNodeRegistryTransaction 
		{
		public:
			/** Describes the type of transaction. */
			enum class ETransactionType : uint8
			{
				NodeRegistration,     //< Something was added to the registry.
				NodeUnregistration,  //< Something was removed from the registry.
				Invalid
			};

			FNodeRegistryTransaction(ETransactionType InType, const FNodeRegistryKey& InKey, const FNodeClassInfo& InNodeClassInfo);

			ETransactionType GetTransactionType() const;
			const FNodeClassInfo& GetNodeClassInfo() const;
			const FNodeRegistryKey& GetNodeRegistryKey() const;

		private:

			ETransactionType Type;
			FNodeRegistryKey Key;
			FNodeClassInfo NodeClassInfo;
		};

		namespace NodeRegistryKey
		{
			// Returns true if the registry key is a valid key. 
			//
			// This does *not* connote that the registry key exists in the registry.
			METASOUNDFRONTEND_API bool IsValid(const FNodeRegistryKey& InKey);

			// Returns true if both keys represent the same entry in the node registry.
			METASOUNDFRONTEND_API bool IsEqual(const FNodeRegistryKey& InLHS, const FNodeRegistryKey& InRHS);

			// Returns true if the class metadata and key represent the same entry in the node registry.
			METASOUNDFRONTEND_API bool IsEqual(const FMetasoundFrontendClassMetadata& InLHS, const FNodeRegistryKey& InRHS);

			// Returns true if the class info and key represent the same entry in the node registry.
			METASOUNDFRONTEND_API bool IsEqual(const FNodeClassInfo& InLHS, const FNodeRegistryKey& InRHS);

			// Returns true if the class metadatas represent the same entry in the node registry.
			METASOUNDFRONTEND_API bool IsEqual(const FMetasoundFrontendClassMetadata& InLHS, const FMetasoundFrontendClassMetadata& InRHS);

			// Returns true if the class info and class metadata represent the same entry in the node registry.
			METASOUNDFRONTEND_API bool IsEqual(const FNodeClassInfo& InLHS, const FMetasoundFrontendClassMetadata& InRHS);

			METASOUNDFRONTEND_API FNodeRegistryKey CreateKey(EMetasoundFrontendClassType InType, const FString& InFullClassName, int32 InMajorVersion, int32 InMinorVersion);
			METASOUNDFRONTEND_API FNodeRegistryKey CreateKey(const FNodeClassMetadata& InNodeMetadata);
			METASOUNDFRONTEND_API FNodeRegistryKey CreateKey(const FMetasoundFrontendClassMetadata& InNodeMetadata);
			METASOUNDFRONTEND_API FNodeRegistryKey CreateKey(const FNodeClassInfo& ClassInfo);
		}
	} // namespace Frontend
} // namespace Metasound

/**
 * Singleton registry for all types and nodes.
 */
class METASOUNDFRONTEND_API FMetasoundFrontendRegistryContainer
{

public:
	using FNodeClassInfo = Metasound::Frontend::FNodeClassInfo;
	using FConverterNodeRegistryKey = ::Metasound::Frontend::FConverterNodeRegistryKey;
	using FConverterNodeRegistryValue = ::Metasound::Frontend::FConverterNodeRegistryValue;
	using FConverterNodeInfo = ::Metasound::Frontend::FConverterNodeInfo;

	using FNodeRegistryKey = Metasound::Frontend::FNodeRegistryKey;
	using FNodeClassMetadata = Metasound::FNodeClassMetadata;

	using FDataTypeRegistryInfo = Metasound::Frontend::FDataTypeRegistryInfo;
	using IEnumDataTypeInterface = Metasound::Frontend::IEnumDataTypeInterface;

	static FMetasoundFrontendRegistryContainer* Get();
	static void ShutdownMetasoundFrontend();

	static FNodeRegistryKey GetRegistryKey(const FNodeClassMetadata& InNodeMetadata);
	static FNodeRegistryKey GetRegistryKey(const FMetasoundFrontendClassMetadata& InNodeMetadata);
	static FNodeRegistryKey GetRegistryKey(const FNodeClassInfo& ClassInfo);

	static bool GetFrontendClassFromRegistered(const FNodeRegistryKey& InKey, FMetasoundFrontendClass& OutClass);
	static bool GetNodeClassInfoFromRegistered(const FNodeRegistryKey& InKey, FNodeClassInfo& OutInfo);
	static bool GetInputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey);
	static bool GetVariableNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey);
	static bool GetOutputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey);


	FMetasoundFrontendRegistryContainer() = default;
	virtual ~FMetasoundFrontendRegistryContainer() = default;

	FMetasoundFrontendRegistryContainer(const FMetasoundFrontendRegistryContainer&) = delete;
	FMetasoundFrontendRegistryContainer& operator=(const FMetasoundFrontendRegistryContainer&) = delete;

	// Enqueu and command for registering a node or data type.
	// The command queue will be processed on module init or when calling `RegisterPendingNodes()`
	virtual bool EnqueueInitCommand(TUniqueFunction<void()>&& InFunc) = 0;

	// This is called on module startup. This invokes any registration commands enqueued by our registration macros.
	virtual void RegisterPendingNodes() = 0;

	/** Perform function for each registry transaction since a given transaction ID. */
	virtual void ForEachNodeRegistryTransactionSince(Metasound::Frontend::FRegistryTransactionID InSince, Metasound::Frontend::FRegistryTransactionID* OutCurrentRegistryTransactionID, TFunctionRef<void(const Metasound::Frontend::FNodeRegistryTransaction&)> InFunc) const = 0;

	/** Register a data type
	 * @param InName - Name of data type.
	 * @param InEntry - TUniquePtr to data type registry entry.
	 *
	 * @return True on success, false on failure.
	 */
	virtual bool RegisterDataType(const FName& InName, TUniquePtr<Metasound::Frontend::IDataTypeRegistryEntry>&& InEntry) = 0;

	// Return any data types that can be used as a metasound input type or output type.
	virtual TArray<FName> GetAllValidDataTypes() = 0;

	// Get info about a specific data type (what kind of literals we can use, etc.)
	// @returns false if InDataType wasn't found in the registry. 
	virtual bool GetInfoForDataType(FName InDataType, FDataTypeRegistryInfo& OutInfo) = 0;

	// Get the desired kind of literal for a given data type. Returns EConstructorArgType::Invalid if the data type couldn't be found.
	virtual Metasound::ELiteralType GetDesiredLiteralTypeForDataType(FName InDataType) const = 0;

	// Get whether we can build a literal of this specific type for InDataType.
	virtual bool DoesDataTypeSupportLiteralType(FName InDataType, Metasound::ELiteralType InLiteralType) const = 0;

	template<typename ArgType>
	bool DoesDataTypeSupportLiteralType(FName InDataType) const
	{
		return DoesDataTypeSupportLiteralType(InDataType, Metasound::TLiteralTypeInfo<ArgType>::GetLiteralArgTypeEnum());
	}

	virtual UClass* GetLiteralUClassForDataType(FName InDataType) const = 0;
	virtual Metasound::FLiteral CreateLiteralFromUObject(const FName& InDataType, UObject* InObject) = 0;
	virtual Metasound::FLiteral CreateLiteralFromUObjectArray(const FName& InDataType, TArray<UObject*> InObjectArray) = 0;

	// Return the enum interface for a data type. If the data type does not have 
	// an enum interface, returns a nullptr.
	virtual TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> GetEnumInterfaceForDataType(FName InDataType) const = 0;

	// Create a data channel for a data type. If the data type cannot be used in
	// a data channel, return a nullptr.
	virtual TSharedPtr<Metasound::IDataChannel, ESPMode::ThreadSafe> CreateDataChannelForDataType(const FName& InDataType, const Metasound::FOperatorSettings& InOperatorSettings) const = 0;


	/** Register an external node with the frontend.
	 *
	 * @param InCreateNode - Function for creating node from FNodeInitData.
	 * @param InCreateDescription - Function for creating a FMetasoundFrontendClass.
	 *
	 * @return A node registration key. If the registration failed, then the registry 
	 *         key will be invalid.
	 */
	virtual FNodeRegistryKey RegisterNode(TUniquePtr<Metasound::Frontend::INodeRegistryEntry>&&) = 0;

	/** Unregister an external node from the frontend.
	 *
	 * @param InKey - The registration key for the node.
	 *
	 * @return True on success, false on failure.
	 */
	virtual bool UnregisterNode(const FNodeRegistryKey& InKey) = 0;

	/** Returns true if the provided registry key corresponds to a valid registered node. */
	virtual bool IsNodeRegistered(const FNodeRegistryKey& InKey) const = 0;

	/** Returns true if the provided registry key corresponds to a valid registered node that is natively defined. */
	virtual bool IsNodeNative(const FNodeRegistryKey& InKey) const = 0;

	// Iterates class types in registry.  If InClassType is set to a valid class type (optional), only iterates classes of the given type
	virtual void IterateRegistry(Metasound::FIterateMetasoundFrontendClassFunction InIterFunc, EMetasoundFrontendClassType InClassType = EMetasoundFrontendClassType::Invalid) const = 0;

	// Query for MetaSound Frontend document objects.
	virtual bool FindFrontendClassFromRegistered(const Metasound::Frontend::FNodeRegistryKey& InKey, FMetasoundFrontendClass& OutClass) = 0;
	virtual bool FindNodeClassInfoFromRegistered(const Metasound::Frontend::FNodeRegistryKey& InKey, FNodeClassInfo& OutInfo) = 0;

	virtual bool FindInputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey) = 0;
	virtual bool FindVariableNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey) = 0;
	virtual bool FindOutputNodeRegistryKeyForDataType(const FName& InDataTypeName, FNodeRegistryKey& OutKey) = 0;

	// Create a new instance of a C++ implemented node from the registry.
	virtual TUniquePtr<Metasound::INode> CreateInputNode(const FName& InInputType, Metasound::FInputNodeConstructorParams&& InParams) = 0;
	virtual TUniquePtr<Metasound::INode> CreateVariableNode(const FName& InVariableType, Metasound::FVariableNodeConstructorParams&& InParams) = 0;
	virtual TUniquePtr<Metasound::INode> CreateOutputNode(const FName& InOutputType, Metasound::FOutputNodeConstructorParams&& InParams) = 0;
	virtual TUniquePtr<Metasound::INode> CreateReceiveNode(const FName& InVariableType, Metasound::FReceiveNodeConstructorParams&& InParams) = 0;

	virtual TUniquePtr<Metasound::INode> CreateNode(const FNodeRegistryKey& InKey, Metasound::FDefaultNodeConstructorParams&&) const = 0;
	virtual TUniquePtr<Metasound::INode> CreateNode(const FNodeRegistryKey& InKey, Metasound::FDefaultLiteralNodeConstructorParams&&) const = 0;
	virtual TUniquePtr<Metasound::INode> CreateNode(const FNodeRegistryKey& InKey, const Metasound::FNodeInitData&) const = 0;

	virtual bool RegisterConversionNode(const FConverterNodeRegistryKey& InNodeKey, const FConverterNodeInfo& InNodeInfo) = 0;

	// Returns a list of possible nodes to use to convert from FromDataType to ToDataType.
	// Returns an empty array if none are available.
	virtual TArray<FConverterNodeInfo> GetPossibleConverterNodes(const FName& FromDataType, const FName& ToDataType) = 0;

private:
	static FMetasoundFrontendRegistryContainer* LazySingleton;
};


