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

namespace Metasound
{
	namespace Frontend
	{
		using FRegistryTransactionID = int32;
	}

	// This struct is passed to FInputNodeConstructorCallback and FOutputNodeConstructorCallback.
	struct FInputNodeConstructorParams
	{
		// the instance name and name of the specific connection that we should use.
		const FString& InNodeName;
		const FGuid& InInstanceID;
		const FString& InVertexName;

		FLiteral InitParam;
	};

	struct FOutputNodeConstructorParams
	{
		// the instance name and name of the specific connection that we should use.
		const FString& InNodeName;
		const FGuid& InInstanceID;
		const FString& InVertexName;
	};

	/*
	class IMetasoundDataTypeRegistryEntry
	{
	public:
		virtual TUniquePtr<Metasound::INode> CreateInputNode(FInputNodeConstructorParams&&) const = 0;
		virtual TUniquePtr<Metasound::INode> CreateOutputNode(const FOutputNodeConstructorParams&) const = 0;
	};

	class IMetasoundProxyRegistryEntry
	{
	public:
		// This function is used to create a proxy from a datatype's base uclass.
		virtual Audio::IProxyDataPtr CreateAudioProxy(UObject*) const = 0;
	};

	class IMetasoundNodeRegistryEntry
	{
	public:
		virtual TUniquePtr<Metasound::INode> CreateNode(const Metasound::FNodeInitData&) const = 0;
		virtual FMetasoundFrontendClass CreateMetasoundFrontendClass() const = 0;
	};
	*/

	using FCreateInputNodeFunction = TFunction<TUniquePtr<Metasound::INode>(::Metasound::FInputNodeConstructorParams&&)>;
	using FCreateOutputNodeFunction = TFunction<TUniquePtr<Metasound::INode>(const ::Metasound::FOutputNodeConstructorParams&)>;

	// This function is used to create a proxy from a datatype's base uclass.
	using FCreateAudioProxyFunction = TFunction<Audio::IProxyDataPtr(UObject*)> ;

	// Creates a data channel for senders and receivers of this data type.
	using FCreateDataChannelFunction = TFunction<TSharedPtr<IDataChannel, ESPMode::ThreadSafe>(const FOperatorSettings&)>;

	using FCreateMetasoundNodeFunction = TFunction<TUniquePtr<Metasound::INode>(const Metasound::FNodeInitData&)>;
	using FCreateMetasoundFrontendClassFunction = TFunction<FMetasoundFrontendClass()>;
	using FIterateMetasoundFrontendClassFunction = TFunction<void(const FMetasoundFrontendClass&)>;

	// Various elements that we pass to the frontend registry based on templated type traits.
	struct FDataTypeRegistryInfo
	{
		// The name of the data type itself.
		FName DataTypeName;

		// What type we should default to using for literals.
		ELiteralType PreferredLiteralType = ELiteralType::Invalid;

		// This indicates the type can only be constructed with FOperatorSettings or the default constructor.
		bool bIsDefaultParsable = false;

		// These bools signify what basic literal types can be use to describe this data type.
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

	namespace Frontend
	{

		struct METASOUNDFRONTEND_API FNodeRegistryKey
		{
			// The class name for the node.
			FName NodeClassFullName;

			// A hash generated from the input types and output types for this node.
			uint32 NodeHash = 0;

			FORCEINLINE bool operator==(const FNodeRegistryKey& Other) const
			{
				return NodeHash == Other.NodeHash && NodeClassFullName == Other.NodeClassFullName;
			}

			friend uint32 GetTypeHash(const Metasound::Frontend::FNodeRegistryKey& InKey)
			{
				return InKey.NodeHash;
			}
		};

		// Struct with the basics of a node class' information,
		// used to look up that node from our node browser functions,
		// and also used in FGraphHandle::AddNewNode.
		struct METASOUNDFRONTEND_API FNodeClassInfo
		{
			// The type for this node.
			EMetasoundFrontendClassType NodeType = EMetasoundFrontendClassType::Invalid;

			// The lookup key used for the internal node registry.
			FNodeRegistryKey LookupKey;
		};


		struct METASOUNDFRONTEND_API FNodeRegistryElement
		{
			// This lambda can be used to get an INodeBase for this specific node class.
			FCreateMetasoundNodeFunction CreateNode;

			FCreateMetasoundFrontendClassFunction CreateFrontendClass;

			FNodeRegistryElement(FCreateMetasoundNodeFunction&& InCreateNodeFunction, FCreateMetasoundFrontendClassFunction&& InCreateDescriptionFunction)
				: CreateNode(MoveTemp(InCreateNodeFunction))
				, CreateFrontendClass(InCreateDescriptionFunction)
			{
			}
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
	}

	struct FDataTypeConstructorCallbacks
	{
		// This constructs a TInputNode<> with the corresponding datatype.
		FCreateInputNodeFunction CreateInputNode;

		FCreateMetasoundFrontendClassFunction CreateFrontendInputClass;

		// This constructs a TOutputNode<> with the corresponding datatype.
		FCreateOutputNodeFunction CreateOutputNode;

		FCreateMetasoundFrontendClassFunction CreateFrontendOutputClass;

		// For datatypes that use a UObject literal or a UObject literal array, this lambda generates a literal from the corresponding UObject.
		FCreateAudioProxyFunction CreateAudioProxy;

		FCreateDataChannelFunction CreateDataChannel;
	};
} // namespace Frontend

/**
 * Singleton registry for all types and nodes.
 */
class METASOUNDFRONTEND_API FMetasoundFrontendRegistryContainer
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


	static FMetasoundFrontendRegistryContainer* Get();
	static void ShutdownMetasoundFrontend();

	static FNodeRegistryKey GetRegistryKey(const FNodeClassMetadata& InNodeMetadata);
	static FNodeRegistryKey GetRegistryKey(const FMetasoundFrontendClassMetadata& InNodeMetadata);
	static bool GetRegistryKey(const FNodeRegistryElement& InElement, FNodeRegistryKey& OutKey);
	static bool GetFrontendClassFromRegistered(const FMetasoundFrontendClassMetadata& InMetadata, FMetasoundFrontendClass& OutClass);
	static bool GetInputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata);
	static bool GetOutputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata);


	FMetasoundFrontendRegistryContainer() = default;
	virtual ~FMetasoundFrontendRegistryContainer() = default;

	FMetasoundFrontendRegistryContainer(const FMetasoundFrontendRegistryContainer&) = delete;
	FMetasoundFrontendRegistryContainer& operator=(const FMetasoundFrontendRegistryContainer&) = delete;

	// Enqueu and command for registering a node or data type.
	// The command queue will be processed on module init or when calling `RegisterPendingNodes()`
	virtual bool EnqueueInitCommand(TUniqueFunction<void()>&& InFunc) = 0;

	// This is called on module startup. This invokes any registration commands enqueued by our registration macros.
	virtual void RegisterPendingNodes() = 0;

	/** Register external node with the frontend.
	 *
	 * @param InCreateNode - Function for creating node from FNodeInitData.
	 * @param InCreateDescription - Function for creating a FMetasoundFrontendClass.
	 *
	 * @return True on success.
	 */
	virtual bool RegisterExternalNode(Metasound::FCreateMetasoundNodeFunction&& InCreateNode, Metasound::FCreateMetasoundFrontendClassFunction&& InCreateDescription) = 0;
	virtual bool RegisterConversionNode(const FConverterNodeRegistryKey& InNodeKey, const FConverterNodeInfo& InNodeInfo) = 0;
	virtual bool IsNodeRegistered(const FNodeRegistryKey& InKey) const = 0;

	/** Return all node classes registered. */
	virtual TArray<Metasound::Frontend::FNodeClassInfo> GetAllAvailableNodeClasses(Metasound::Frontend::FRegistryTransactionID* OutCurrentRegistryTransactionID) const = 0;

	/** Return node classes registered since a given transaction id. */
	virtual TArray<Metasound::Frontend::FNodeClassInfo> GetNodeClassesRegisteredSince(Metasound::Frontend::FRegistryTransactionID InSince, Metasound::Frontend::FRegistryTransactionID* OutCurrentRegistryTransactionID) const = 0;

	// Return any data types that can be used as a metasound input type or output type.
	virtual TArray<FName> GetAllValidDataTypes() = 0;

	// Iterates class types in registry.  If InClassType is set to a valid class type (optional), only iterates classes of the given type
	virtual void IterateRegistry(Metasound::FIterateMetasoundFrontendClassFunction InIterFunc, EMetasoundFrontendClassType InClassType = EMetasoundFrontendClassType::Invalid) const = 0;

	// Query for MetaSound Frontend document objects.
	virtual bool FindFrontendClassFromRegistered(const Metasound::Frontend::FNodeClassInfo& InClassInfo, FMetasoundFrontendClass& OutClass) = 0;
	virtual bool FindFrontendClassFromRegistered(const FMetasoundFrontendClassMetadata& InMetadata, FMetasoundFrontendClass& OutClass) = 0;
	virtual bool FindInputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata) = 0;
	virtual bool FindOutputNodeClassMetadataForDataType(const FName& InDataTypeName, FMetasoundFrontendClassMetadata& OutMetadata) = 0;

	// Create a new instance of a C++ implemented node from the registry.
	virtual TUniquePtr<Metasound::INode> ConstructInputNode(const FName& InInputType, Metasound::FInputNodeConstructorParams&& InParams) = 0;
	virtual TUniquePtr<Metasound::INode> ConstructOutputNode(const FName& InOutputType, const Metasound::FOutputNodeConstructorParams& InParams) = 0;
	virtual TUniquePtr<Metasound::INode> ConstructExternalNode(const Metasound::Frontend::FNodeRegistryKey& InRegistryKey, const Metasound::FNodeInitData& InInitData) = 0;

	// Returns a list of possible nodes to use to convert from FromDataType to ToDataType.
	// Returns an empty array if none are available.
	virtual TArray<FConverterNodeInfo> GetPossibleConverterNodes(const FName& FromDataType, const FName& ToDataType) = 0;

	virtual bool RegisterDataType(const FDataTypeRegistryInfo& InDataInfo, const FDataTypeConstructorCallbacks& InCallbacks) = 0;

	virtual bool RegisterEnumDataInterface(FName InDataType, TSharedPtr<IEnumDataTypeInterface>&& InInterface) = 0;

	// Get the desired kind of literal for a given data type. Returns EConstructorArgType::Invalid if the data type couldn't be found.
	virtual Metasound::ELiteralType GetDesiredLiteralTypeForDataType(FName InDataType) const = 0;

	template<typename ArgType>
	bool DoesDataTypeSupportLiteralType(FName InDataType) const
	{
		return DoesDataTypeSupportLiteralType(InDataType, Metasound::TLiteralTypeInfo<ArgType>::GetLiteralArgTypeEnum());
	}

	virtual UClass* GetLiteralUClassForDataType(FName InDataType) const = 0;
	virtual Metasound::FLiteral GenerateLiteralForUObject(const FName& InDataType, UObject* InObject) = 0;
	virtual Metasound::FLiteral GenerateLiteralForUObjectArray(const FName& InDataType, TArray<UObject*> InObjectArray) = 0;

	// Get whether we can build a literal of this specific type for InDataType.
	virtual bool DoesDataTypeSupportLiteralType(FName InDataType, Metasound::ELiteralType InLiteralType) const = 0;


	// Get info about a specific data type (what kind of literals we can use, etc.)
	// @returns false if InDataType wasn't found in the registry. 
	virtual bool GetInfoForDataType(FName InDataType, FDataTypeRegistryInfo& OutInfo) = 0;

	virtual TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> GetEnumInterfaceForDataType(FName InDataType) const = 0;

	virtual TSharedPtr<Metasound::IDataChannel, ESPMode::ThreadSafe> CreateDataChannelForDataType(const FName& InDataType, const Metasound::FOperatorSettings& InOperatorSettings) const = 0;

private:
	static FMetasoundFrontendRegistryContainer* LazySingleton;
};


