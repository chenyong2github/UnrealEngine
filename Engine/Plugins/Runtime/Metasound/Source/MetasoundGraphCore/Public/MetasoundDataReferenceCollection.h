// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundVertex.h"

namespace Metasound
{
	class METASOUNDGRAPHCORE_API FDataReferenceCollection
	{
		// Convenience class to handle copying and moving of data refs
		// without knowing data refs derived type.  
		//
		// Uses Clone() function as a virtual copy constructor.
		class FDataRefWrapper 
		{
			public:
				// Anything that implements a IDataReference interface
				template<typename DataRefType>
				FDataRefWrapper(const DataRefType& InDataReference)
				:	DataRefPtr(InDataReference.Clone())
				{
				}

				// Override copy constructor to use underlying Clone method.
				FDataRefWrapper(const FDataRefWrapper& Other)
				:	DataRefPtr(Other.CloneDataReference())
				{
				}

				// Override equal operator to use underlying Clone method.
				FDataRefWrapper& operator=(const FDataRefWrapper& Other)
				{
					DataRefPtr = Other.CloneDataReference();
					return *this;
				}

				bool IsValid() const
				{
					return DataRefPtr.IsValid();
				}

				// Get underlying data ref
				const IDataReference* GetDataReference() const
				{
					return DataRefPtr.Get();
				}

				// Clone underlying data ref
				TUniquePtr<IDataReference> CloneDataReference() const
				{
					if (DataRefPtr.IsValid())
					{
						return DataRefPtr->Clone();
					}
				
					return TUniquePtr<IDataReference>();
				}

			private:

				TUniquePtr<IDataReference> DataRefPtr;
		};


		using FDataReferenceMap = TMap<FString, FDataRefWrapper>;

		public:

			/** Add a readable data reference to the collection.
			 *
			 * @param InName - Name of data reference.
			 * @InDataReference - Readable data reference. 
			 */
			template<typename DataType>
			void AddDataReadReference(const FString& InName, const TDataReadReference<DataType>& InDataReference)
			{
				FDataRefWrapper Wrapper(InDataReference);

				AddDataReference(DataReadRefMap, InName, MoveTemp(Wrapper));
			}

			/** Add a readable data reference to the collection.
			 *
			 * @param InName - Name of data reference.
			 * @InDataReference - Writable data reference. 
			 */
			template<typename DataType>
			void AddDataReadReference(const FString& InName, const TDataWriteReference<DataType>& InDataReference)
			{
				TDataReadReference<DataType> Readable(InDataReference);

				AddDataReadReference(InName, Readable);
			}

			/** Add a writable data reference to the collection.
			 *
			 * @param InName - Name of data reference.
			 * @InDataReference - Writable data reference. 
			 */
			template<typename DataType>
			void AddDataWriteReference(const FString& InName, const TDataWriteReference<DataType>& InDataReference)
			{
				FDataRefWrapper Wrapper(InDataReference);

				AddDataReference(DataWriteRefMap, InName, MoveTemp(Wrapper));
			}

			/** Add a readable data reference from another collection.
			 *
			 * @param InLocalName - Name to give data added to this collection.
			 * @param InOtherCollection - Collection which contains the desired data reference. 
			 * @param InOtherName - Name of desired data in other collection.
			 * @param InOtherType - TypeName of desired data.
			 *
			 * @return True if the data was successfully added. False otherwise. 
			 */
			bool AddDataReadReferenceFrom(const FString& InLocalName, const FDataReferenceCollection& InOtherCollection, const FString& InOtherName, const FName& InOtherTypeName);

			/** Add a writable data reference from another collection.
			 *
			 * @param InLocalName - Name to give data added to this collection.
			 * @param InOtherCollection - Collection which contains the desired data reference. 
			 * @param InOtherName - Name of desired data in other collection.
			 * @param InOtherType - TypeName of desired data.
			 *
			 * @return True if the data was successfully added. False otherwise. 
			 */
			bool AddDataWriteReferenceFrom(const FString& InLocalName, const FDataReferenceCollection& InOtherCollection, const FString& InOtherName, const FName& InOtherTypeName);

			/** Query whether a readable data reference is within the collection. 
			 *
			 * @param InName - Name of the data.
			 * @param InTypeName - TypeName of teh data.
			 *
			 * @return True if the data exists in the collection. False otherwise. 
			 */
			bool ContainsDataReadReference(const FString& InName, const FName& InTypeName) const;

			/** Query whether a writable data reference is within the collection. 
			 *
			 * @param InName - Name of the data.
			 * @param InTypeName - TypeName of teh data.
			 *
			 * @return True if the data exists in the collection. False otherwise. 
			 */
			bool ContainsDataWriteReference(const FString& InName, const FName& InTypeName) const;


			/** Query whether a readable data reference is within the collection. 
			 *
			 * @param InName - Name of the data.
			 *
			 * @return True if the data exists in the collection. False otherwise. 
			 */
			template<typename DataType>
			bool ContainsDataReadReference(const FString& InName) const
			{
				return Contains<DataType>(DataReadRefMap, InName);
			}

			/** Query whether a writable data reference is within the collection. 
			 *
			 * @param InName - Name of the data.
			 *
			 * @return True if the data exists in the collection. False otherwise. 
			 */
			template<typename DataType>
			bool ContainsDataWriteReference(const FString& InName) const
			{
				return Contains<DataType>(DataWriteRefMap, InName);
			}

			/** Returns a readable data ref from the collection.
			 *
			 * @param InName - Name of readable data.
			 *
			 * @return A readable data reference.
			 */
			template<typename DataType>
			TDataReadReference<DataType> GetDataReadReference(const FString& InName) const
			{
				typedef TDataReadReference<DataType> FDataRefType;

				const IDataReference* DataRefPtr = GetDataReference(DataReadRefMap, InName);

				check(nullptr != DataRefPtr);

				check(IsDataReferenceOfType<DataType>(*DataRefPtr));

				return FDataRefType(*static_cast<const FDataRefType*>(DataRefPtr));
			}

			/** Returns a readable data ref from the collection or construct one
			 * if one is not there.
			 *
			 * @param InName - Name of readable data.
			 * @param ConstructorArgs - Arguments to pass to constructor of TDataReadReference<DataType>
			 *
			 * @return A readable data reference.
			 */
			template<typename DataType, typename... ConstructorArgTypes>
			TDataReadReference<DataType> GetDataReadReferenceOrConstruct(const FString& InName, ConstructorArgTypes&&... ConstructorArgs) const
			{
				typedef TDataReadReference<DataType> FDataRefType;

				if (ContainsDataReadReference<DataType>(InName))
				{
					return GetDataReadReference<DataType>(InName);
				}
				else
				{
					return FDataRefType::CreateNew(Forward<ConstructorArgTypes>(ConstructorArgs)...);
				}
			}

			/** Returns a readable data ref from the collection or construct one
			 * if one is not there with the default provided from the given input vertex collection.
			 *
			 * @param InName - Name of readable data.
			 * @param InputVertices - Collection of input vertices to retrieve default from if reference is not available
			 * @param ConstructorArgs - Arguments to pass to constructor of TDataReadReference<DataType>
			 *
			 * @return A readable data reference.
			 */
			// TODO: FIXME
			// 1. Remove extra template parameters "DefaultDataType", "ConstructorArgTypes"
			// 2. Use data factory instead of read reference class to construct
			// 3. Pipe in FOperatorSettings to the factory call.
			// 4. Build from literal, not from DefaultDataType.
			template<typename DataType, typename DefaultDataType = DataType, typename... ConstructorArgTypes>
			TDataReadReference<DataType> GetDataReadReferenceOrConstructWithVertexDefault(const FInputVertexInterface& InputVertices, const FString& InName) const
			{
				using FDataRef = TDataReadReference<DataType>;

				if (ContainsDataReadReference<DataType>(InName))
				{
					return GetDataReadReference<DataType>(InName);
				}
				else
				{
					const DefaultDataType DefaultValue = InputVertices[InName].GetDefaultValue().Value.Get<DefaultDataType>();
					return FDataRef::CreateNew(DefaultValue);
				}
			}

			/** Returns a readable data ref from the collection or construct one
			 * if one is not there with the default provided from the given input vertex collection.
			 *
			 * @param InName - Name of readable data.
			 * @param InputVertices - Collection of input vertices to retrieve default from if reference is not available
			 * @param ConstructorArgs - Arguments to pass to constructor of TDataReadReference<DataType>
			 *
			 * @return A readable data reference.
			 */
			template<typename DataType, typename DefaultDataType = DataType, typename... ConstructorArgTypes>
			TDataReadReference<DataType> GetDataReadReferenceOrConstructWithVertexDefault(const FInputVertexInterface& InputVertices, const FString& InName, const FOperatorSettings& InOperatorSettings) const
			{
				using FDataRefType = TDataReadReference<DataType>;

				if (ContainsDataReadReference<DataType>(InName))
				{
					return GetDataReadReference<DataType>(InName);
				}
				else
				{
					const DefaultDataType DefaultValue = InputVertices[InName].GetDefaultValue().Value.Get<DefaultDataType>();
					return FDataRefType::CreateNew(InOperatorSettings, DefaultValue);
				}
			}

			/** Returns a writable data ref from the collection.
			 *
			 * @param InName - Name of readable data.
			 *
			 * @return A readable data reference.
			 */
			template<typename DataType>
			TDataWriteReference<DataType> GetDataWriteReference(const FString& InName) const
			{
				typedef TDataWriteReference<DataType> FDataRefType;

				const IDataReference* DataRefPtr = GetDataReference(DataWriteRefMap, InName);

				check(nullptr != DataRefPtr);

				check(IsDataReferenceOfType<DataType>(*DataRefPtr));

				return FDataRefType(*static_cast<const FDataRefType*>(DataRefPtr));
			}

			/** Returns a writable data ref from the collection or construct
			 * one if one is not there.
			 *
			 * @param InName - Name of readable data.
			 * @param ConstructorArgs - Arguments to pass to constructor of TDataWriteReference<DataType>
			 *
			 * @return A readable data reference.
			 */
			template<typename DataType, typename... ConstructorArgTypes>
			TDataWriteReference<DataType> GetDataWriteReferenceOrConstruct(const FString& InName, ConstructorArgTypes&&... ConstructorArgs) const
			{
				typedef TDataWriteReference<DataType> FDataRefType;

				if (ContainsDataWriteReference<DataType>(InName))
				{
					return GetDataWriteReference<DataType>(InName);
				}
				else
				{
					return FDataRefType::CreateNew(Forward<ConstructorArgTypes>(ConstructorArgs)...);
				}
			}

		private:

			template<typename DataType>
			bool Contains(const FDataReferenceMap& InMap, const FString& InName) const
			{
				if (InMap.Contains(InName))
				{
					const IDataReference* DataRefPtr = InMap[InName].GetDataReference();

					if (nullptr != DataRefPtr)
					{
						return IsDataReferenceOfType<DataType>(*DataRefPtr);
					}
				}

				return false;
			}

			const IDataReference* GetDataReference(const FDataReferenceMap& InMap, const FString& InName) const;

			void AddDataReference(FDataReferenceMap& InMap, const FString& InName, FDataRefWrapper&& InDataReference);

			FDataReferenceMap DataReadRefMap;
			FDataReferenceMap DataWriteRefMap;
	};
}
