// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Algo/AllOf.h"
#include "Containers/Array.h"
#include "MetasoundDataFactory.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundVertex.h"

#include <type_traits>

namespace Metasound
{
	namespace MetasoundVertexDataPrivate
	{
		template<typename V>
		using EnableIfIsInputDataVertex = std::enable_if_t<std::is_same_v<FInputDataVertex, V>>;

		// Binds a vertex to a data reference.
		template<typename VertexType>
		class TBinding
		{

		public:
			TBinding(VertexType&& InVertex)
			: Vertex(MoveTemp(InVertex))
			{
			}

			TBinding(const VertexType& InVertex)
			: Vertex(InVertex)
			{
			}

			template<typename DataReferenceType>
			void Bind(const DataReferenceType& InDataReference)
			{
				check(Vertex.DataTypeName == InDataReference.GetDataTypeName());
				Data.Emplace(InDataReference);
			}

			void Bind(FAnyDataReference&& InAnyDataReference)
			{
				check(Vertex.DataTypeName == InAnyDataReference.GetDataTypeName());
				Data.Emplace(MoveTemp(InAnyDataReference));
			}
			
			const VertexType& GetVertex() const
			{
				return Vertex;
			}

			bool IsBound() const
			{
				return Data.IsSet();
			}

			EDataReferenceAccessType GetAccessType() const
			{
				if (Data.IsSet())
				{
					Data->GetAccessType();
				}
				return EDataReferenceAccessType::None;
			}

			// Get data read reference assuming data is bound and read or write accessible.
			template<typename DataType>
			TDataReadReference<DataType> GetDataReadReference() const
			{
				check(Data.IsSet());
				return Data->GetDataReadReference<DataType>();
			}

			// Get the bound data read reference if it exists. Otherwise create and 
			// return a data read reference by constructing one using the Vertex's 
			// default literal.
			template<
				typename DataType,
				typename V = VertexType,
				typename = EnableIfIsInputDataVertex<V>
			> 
			TDataReadReference<DataType> GetOrCreateDefaultDataReadReference(const FOperatorSettings& InSettings) const
			{
				if (Data.IsSet())
				{
					return Data->GetDataReadReference<DataType>();
				}
				else
				{
					return TDataReadReferenceLiteralFactory<DataType>::CreateExplicitArgs(InSettings, Vertex.GetDefaultLiteral());
				}
			}

			// Get the bound data read reference if it exists. Otherwise create and 
			// return a data read reference by constructing one the supplied constructor 
			// arguments.
			template<typename DataType, typename ... ConstructorArgTypes>
			TDataReadReference<DataType> GetOrConstructDataReadReference(ConstructorArgTypes&&... ConstructorArgs) const
			{
				if (Data.IsSet())
				{
					return Data->GetDataReadReference<DataType>();
				}

				return TDataReadReference<DataType>::CreateNew(Forward<ConstructorArgTypes>(ConstructorArgs)...);
			}

			// Get data write reference assuming data is bound and write accessible.
			template<typename DataType> 
			TDataWriteReference<DataType> GetDataWriteReference() const
			{
				check(Data.IsSet());
				return Data->GetDataWriteReference<DataType>();
			}

			// Get the bound data write reference if it exists. Otherwise create and 
			// return a data write reference by constructing one using the Vertex's 
			// default literal.
			template<
				typename DataType,
				typename V = VertexType,
				typename = EnableIfIsInputDataVertex<V>
			> 
			TDataWriteReference<DataType> GetOrCreateDefaultDataWriteReference(const FOperatorSettings& InSettings) const
			{
				if (Data.IsSet())
				{
					return Data->GetDataWriteReference<DataType>();
				}
				return TDataWriteReferenceLiteralFactory<DataType>::CreateExplicitArgs(InSettings, Vertex.GetDefaultLiteral());
			}

			// Get the bound data write reference if it exists. Otherwise create and 
			// return a data write reference by constructing one the supplied constructor 
			// arguments.
			template<typename DataType, typename ... ConstructorArgTypes>
			TDataWriteReference<DataType> GetOrConstructDataWriteReference(ConstructorArgTypes&&... ConstructorArgs) const
			{
				if (Data.IsSet())
				{
					return Data->GetDataWriteReference<DataType>();
				}
				return TDataWriteReference<DataType>::CreateNew(Forward<ConstructorArgTypes>(ConstructorArgs)...);
			}

		private:

			VertexType Vertex;
			TOptional<FAnyDataReference> Data;
		};
	}

	/** An input vertex interface with optionally bound data references. */
	class METASOUNDGRAPHCORE_API FInputVertexInterfaceData
	{
		using FBinding = MetasoundVertexDataPrivate::TBinding<FInputDataVertex>;

	public:

		using FRangedForIteratorType = typename TArray<FBinding>::RangedForIteratorType;
		using FRangedForConstIteratorType = typename TArray<FBinding>::RangedForConstIteratorType;

		/** Construct with an FInputVertexInterface. */
		FInputVertexInterfaceData(const FInputVertexInterface& InVertexInterface);

		/** Bind a data reference to a vertex. */
		template<typename DataReferenceType>
		void BindVertex(const FVertexName& InVertexName, const DataReferenceType& InDataReference)
		{
			BindVertex(InVertexName, FAnyDataReference{InDataReference});
		}

		/** Bind a data reference to a vertex. */
		void BindVertex(const FVertexName& InVertexName, FAnyDataReference&& InDataReference);

		/** Bind a data references to vertices with matching vertex names. */
		void Bind(const FDataReferenceCollection& InCollection);

		/** Returns true if a vertex with the given vertex name exists and is bound
		 * to a data reference. */
		bool IsVertexBound(const FVertexName& InVertexName) const;

		/** Returns the access type of a bound vertex. If the vertex does not exist
		 * or if it is unbound, this will return EDataReferenceAccessType::None
		 */
		EDataReferenceAccessType GetVertexDataAccessType(const FVertexName& InVertexName) const;

		/** Returns true if all vertices in the FInputVertexInterface are bound to 
		 * data references. */
		bool AreAllVerticesBound() const;

		FRangedForIteratorType begin()
		{
			return Bindings.begin();
		}

		FRangedForIteratorType end()
		{
			return Bindings.end();
		}

		FRangedForConstIteratorType begin() const
		{
			return Bindings.begin();
		}

		FRangedForConstIteratorType end() const
		{
			return Bindings.end();
		}


		/** Get data read reference assuming data is bound and read or write accessible. */
		template<typename DataType> 
		TDataReadReference<DataType> GetDataReadReference(const FVertexName& InVertexName) const
		{
			const FBinding* Binding = FindChecked(InVertexName);
			return Binding->GetDataReadReference<DataType>();
		}

		/** Get the bound data read reference if it exists. Otherwise create and 
		 * return a data read reference by constructing one using the Vertex's 
		 * default literal.
		 */
		template<typename DataType> 
		TDataReadReference<DataType> GetOrCreateDefaultDataReadReference(const FVertexName& InVertexName, const FOperatorSettings& InSettings) const
		{
			if (const FBinding* Binding = Find(InVertexName))
			{
				return Binding->GetOrCreateDefaultDataReadReference<DataType>(InSettings);
			}
			else
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed find vertex with name '%s'. Cannot check for existing bound data."), *InVertexName.ToString());
			}

			return TDataReadReferenceLiteralFactory<DataType>::CreateAny(InSettings, FLiteral::CreateInvalid());
		}

		/** Get the bound data read reference if it exists. Otherwise create and 
		 * return a data read reference by constructing one the supplied constructor 
		 * arguments.
		 */
		template<typename DataType, typename ... ConstructorArgTypes>
		TDataReadReference<DataType> GetOrConstructDataReadReference(const FVertexName& InVertexName, ConstructorArgTypes&&... ConstructorArgs) const
		{
			if (const FBinding* Binding = Find(InVertexName))
			{
				return Binding->GetOrConstructDataReadReference<DataType>(Forward<ConstructorArgTypes>(ConstructorArgs)...);
			}
			else
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed find vertex with name '%s'. Cannot check for existing bound data."), *InVertexName.ToString());
			}

			return TDataReadReference<DataType>::CreateNew(Forward<ConstructorArgTypes>(ConstructorArgs)...);
		}


		/** Get data write reference assuming data is bound and write accessible. */
		template<typename DataType> 
		TDataWriteReference<DataType> GetDataWriteReference(const FVertexName& InVertexName) const
		{
			const FBinding* Binding = FindChecked(InVertexName);
			return Binding->GetDataWriteReference<DataType>();
		}

		/** Get the bound data write reference if it exists. Otherwise create and 
		 * return a data write reference by constructing one using the Vertex's 
		 * default literal.
		 */
		template<typename DataType> 
		TDataWriteReference<DataType> GetOrCreateDefaultDataWriteReference(const FVertexName& InVertexName, const FOperatorSettings& InSettings) const
		{
			if (const FBinding* Binding = Find(InVertexName))
			{
				return Binding->GetOrCreateDefaultDataWriteReference<DataType>(InSettings);
			}
			else
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed find vertex with name '%s'. Cannot check for existing bound data."), *InVertexName.ToString());
			}

			return TDataWriteReferenceLiteralFactory<DataType>::CreateAny(InSettings, FLiteral::CreateInvalid());
		}

		/** Get the bound data write reference if it exists. Otherwise create and 
		 * return a data write reference by constructing one the supplied constructor 
		 * arguments.
		 */
		template<typename DataType, typename ... ConstructorArgTypes>
		TDataWriteReference<DataType> GetOrConstructDataWriteReference(const FVertexName& InVertexName, ConstructorArgTypes&&... ConstructorArgs) const
		{
			if (const FBinding* Binding = Find(InVertexName))
			{
				return Binding->GetOrConstructDataWriteReference<DataType>(Forward<ConstructorArgTypes>(ConstructorArgs)...);
			}
			else
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed find vertex with name '%s'. Cannot check for existing bound data."), *InVertexName.ToString());
			}

			return TDataWriteReference<DataType>::CreateNew(Forward<ConstructorArgTypes>(ConstructorArgs)...);
		}
	
	private:

		FBinding* Find(const FVertexName& InVertexName);
		const FBinding* Find(const FVertexName& InVertexName) const;

		FBinding* FindChecked(const FVertexName& InVertexName);
		const FBinding* FindChecked(const FVertexName& InVertexName) const;

		TArray<FBinding> Bindings;
	};

	/** An output vertex interface with optionally bound data references. */
	class METASOUNDGRAPHCORE_API FOutputVertexInterfaceData
	{
		using FBinding = MetasoundVertexDataPrivate::TBinding<FOutputDataVertex>;

	public:

		using FRangedForIteratorType = typename TArray<FBinding>::RangedForIteratorType;
		using FRangedForConstIteratorType = typename TArray<FBinding>::RangedForConstIteratorType;

		/* Construct using an FOutputVertexInterface. */
		FOutputVertexInterfaceData(const FOutputVertexInterface& InVertexInterface);

		/** Bind a data reference to a vertex. */
		template<typename DataReferenceType>
		void BindVertex(const FVertexName& InVertexName, const DataReferenceType& InDataReference)
		{
			BindVertex(InVertexName, FAnyDataReference{InDataReference});
		}

		/** Bind a data reference to a vertex. */
		void BindVertex(const FVertexName& InVertexName, FAnyDataReference&& InDataReference);

		/** Bind a data references to vertices with matching vertex names. */
		void Bind(const FDataReferenceCollection& InCollection);

		/** Returns true if a vertex with the given vertex name exists and is bound
		 * to a data reference. */
		bool IsVertexBound(const FVertexName& InVertexName) const;

		/** Returns the access type of a bound vertex. If the vertex does not exist
		 * or if it is unbound, this will return EDataReferenceAccessType::None
		 */
		EDataReferenceAccessType GetVertexDataAccessType(const FVertexName& InVertexName) const;

		/** Returns true if all vertices in the FInputVertexInterface are bound to 
		 * data references. */
		bool AreAllVerticesBound() const;

		FRangedForIteratorType begin()
		{
			return Bindings.begin();
		}

		FRangedForIteratorType end()
		{
			return Bindings.end();
		}

		FRangedForConstIteratorType begin() const
		{
			return Bindings.begin();
		}

		FRangedForConstIteratorType end() const
		{
			return Bindings.end();
		}


		/** Get data read reference assuming data is bound and read or write accessible. */
		template<typename DataType> 
		TDataReadReference<DataType> GetDataReadReference(const FVertexName& InVertexName) const
		{
			const FBinding* Binding = FindChecked(InVertexName);
			return Binding->GetDataReadReference<DataType>();
		}

		/** Get the bound data read reference if it exists. Otherwise create and 
		 * return a data read reference by constructing one the supplied constructor 
		 * arguments.
		 */
		template<typename DataType, typename ... ConstructorArgTypes>
		TDataReadReference<DataType> GetOrConstructDataReadReference(const FVertexName& InVertexName, ConstructorArgTypes&&... ConstructorArgs) const
		{
			if (const FBinding* Binding = Find(InVertexName))
			{
				return Binding->GetOrConstructDataReadReference<DataType>(Forward<ConstructorArgTypes>(ConstructorArgs)...);
			}
			else
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed find vertex with name '%s'. Cannot check for existing bound data."), *InVertexName.ToString());
			}

			return TDataReadReference<DataType>::CreateNew(Forward<ConstructorArgTypes>(ConstructorArgs)...);
		}


		/** Get data write reference assuming data is bound and write accessible. */
		template<typename DataType> 
		TDataWriteReference<DataType> GetDataWriteReference(const FVertexName& InVertexName) const
		{
			const FBinding* Binding = FindChecked(InVertexName);
			return Binding->GetDataWriteReference<DataType>();
		}

		/** Get the bound data write reference if it exists. Otherwise create and 
		 * return a data write reference by constructing one the supplied constructor 
		 * arguments.
		 */
		template<typename DataType, typename ... ConstructorArgTypes>
		TDataWriteReference<DataType> GetOrConstructDataWriteReference(const FVertexName& InVertexName, ConstructorArgTypes&&... ConstructorArgs) const
		{
			if (const FBinding* Binding = Find(InVertexName))
			{
				return Binding->GetOrConstructDataWriteReference<DataType>(Forward<ConstructorArgTypes>(ConstructorArgs)...);
			}
			else
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed find vertex with name '%s'. Cannot check for existing bound data."), *InVertexName.ToString());
			}

			return TDataWriteReference<DataType>::CreateNew(Forward<ConstructorArgTypes>(ConstructorArgs)...);
		}
	
	private:

		FBinding* Find(const FVertexName& InVertexName);
		const FBinding* Find(const FVertexName& InVertexName) const;

		FBinding* FindChecked(const FVertexName& InVertexName);
		const FBinding* FindChecked(const FVertexName& InVertexName) const;

		TArray<FBinding> Bindings;
	};


	/** A vertex interface with optionally bound data. */
	class FVertexInterfaceData
	{
	public:

		/** Construct using an FVertexInterface. */
		FVertexInterfaceData(const FVertexInterface& InVertexInterface)
		: InputVertexInterfaceData(InVertexInterface.GetInputInterface())
		, OutputVertexInterfaceData(InVertexInterface.GetOutputInterface())
		{
		}

		/** Get input vertex interface data. */
		FInputVertexInterfaceData& GetInputs()
		{
			return InputVertexInterfaceData;
		}

		/** Get input vertex interface data. */
		const FInputVertexInterfaceData& GetInputs() const
		{
			return InputVertexInterfaceData;
		}

		/** Get output vertex interface data. */
		FOutputVertexInterfaceData& GetOutputs()
		{
			return OutputVertexInterfaceData;
		}

		/** Get output vertex interface data. */
		const FOutputVertexInterfaceData& GetOutputs() const
		{
			return OutputVertexInterfaceData;
		}

	private:

		FInputVertexInterfaceData InputVertexInterfaceData;
		FOutputVertexInterfaceData OutputVertexInterfaceData;
	};
}
