// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "MetasoundDataFactory.h"
#include "MetasoundDataReference.h"
#include "MetasoundEnvironment.h"
#include "MetasoundLiteral.h"

#include <type_traits>

namespace Metasound
{
	/** Name of a given vertex.  Only unique for a given node interface. */
	using FVertexName = FName;

	// Vertex metadata
	struct FDataVertexMetadata
	{
		FText Description;
		FText DisplayName;
		bool bIsAdvancedDisplay = false;
	};

	/** FDataVertex
	 *
	 * An FDataVertex is a named vertex of a MetaSound node which can contain data.
	 */
	class FDataVertex
	{

	public:

		FDataVertex() = default;

		/** FDataVertex Constructor
		 *
		 * @InVertexName - Name of vertex.
		 * @InDataTypeName - Name of data type.
		 * @InMetadata - Metadata pertaining to given vertex.
		 */
		FDataVertex(const FVertexName& InVertexName, const FName& InDataTypeName, const FDataVertexMetadata& InMetadata)
		:	VertexName(InVertexName)
		,	DataTypeName(InDataTypeName)
		,	Metadata(InMetadata)
		{
		}

		virtual ~FDataVertex() = default;

		/** Name of vertex. */
		FVertexName VertexName;

		/** Type name of data. */
		FName DataTypeName;

		/** Metadata associated with vertex. */
		FDataVertexMetadata Metadata;

		UE_DEPRECATED(5.1, "GetVertexName() is deprecated. Use VertexName")
		const FVertexName& GetVertexName() const
		{
			return VertexName;
		}

		UE_DEPRECATED(5.1, "GetDataTypeName() is deprecated. Use DataTypeName")
		const FName& GetDataTypeName() const
		{
			return DataTypeName;
		}

		UE_DEPRECATED(5.1, "GetMetadata() is deprecated. Use Metadata")
		const FDataVertexMetadata& GetMetadata() const
		{
			return Metadata;
		}
	};

	/** FInputDataVertex */
	class FInputDataVertex : public FDataVertex
	{
		// FLiteralFactory supports creation of an FLiteral for a vertex given a
		// copyable literal value type.
		//
		// An FLiteral cannot be assigned or copy-constructed because it can contain
		// a TUniquePtr<> which does not support the assignment operator and copy constructor.
		//
		// For factory can create a literal given a copyable type. The factory can
		// also be copied.
		struct FLiteralFactory
		{
			struct ILiteralFactoryImpl
			{
				virtual ~ILiteralFactoryImpl() = default;
				virtual FLiteral CreateLiteral() const = 0;
				virtual TUniquePtr<ILiteralFactoryImpl> Clone() const = 0;
			};

			// Create a literal with a copyable type.
			template<typename LiteralValueType>
			struct TLiteralFactoryImpl : ILiteralFactoryImpl
			{
				static_assert(std::is_copy_constructible<LiteralValueType>::value && std::is_copy_assignable<LiteralValueType>::value, "Literals can only be assigned for copyable types");

				TLiteralFactoryImpl(const LiteralValueType& InValue)
				: LiteralValue(InValue)
				{
				}
				
				FLiteral CreateLiteral() const override
				{
					return FLiteral(LiteralValue);
				}

				TUniquePtr<ILiteralFactoryImpl> Clone() const override
				{
					return MakeUnique<TLiteralFactoryImpl<LiteralValueType>>(*this);
				}
			private:
				LiteralValueType LiteralValue;
			};

			FLiteralFactory()
			: FactoryImpl(nullptr)
			{
			}

			template<typename ValueType>
			FLiteralFactory(const ValueType& InValue)
			: FactoryImpl(MakeUnique<TLiteralFactoryImpl<ValueType>>(InValue))
			{
			}

			FLiteralFactory(const FLiteralFactory& InOther)
			{
				*this = InOther;
			}

			FLiteralFactory& operator=(const FLiteralFactory& InOther)
			{
				FactoryImpl.Reset();
				if (InOther.FactoryImpl.IsValid())
				{
					FactoryImpl = InOther.FactoryImpl->Clone();
				}
				return *this;
			}

			FLiteral CreateLiteral() const
			{
				if (FactoryImpl.IsValid())
				{
					return FactoryImpl->CreateLiteral();
				}

				return FLiteral::CreateInvalid();
			}

		private:

			TUniquePtr<ILiteralFactoryImpl> FactoryImpl;
		};

	public:

		FInputDataVertex() = default;

		/** Construct an FInputDataVertex. */
		FInputDataVertex(const FVertexName& InVertexName, const FName& InDataTypeName, const FDataVertexMetadata& InMetadata)
			: FDataVertex(InVertexName, InDataTypeName, InMetadata)
		{
		}

		/** Construct an FInputDataVertex with a default literal. */
		template<typename LiteralValueType>
		FInputDataVertex(const FVertexName& InVertexName, const FName& InDataTypeName, const FDataVertexMetadata& InMetadata, const LiteralValueType& InLiteralValue)
			: FDataVertex(InVertexName, InDataTypeName, InMetadata)
			, LiteralFactory(InLiteralValue)
		{
		}

		/** Returns the default literal associated with this input. */
		FLiteral GetDefaultLiteral() const 
		{
			return LiteralFactory.CreateLiteral();
		}

		friend bool METASOUNDGRAPHCORE_API operator==(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator<(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS);

	private:

		FLiteralFactory LiteralFactory;
	};

	/** Create a FInputDataVertex with a templated MetaSound data type. */
	template<typename DataType>
	class TInputDataVertex : public FInputDataVertex
	{
	public:
		TInputDataVertex() = default;

		template<typename... ArgTypes>
		TInputDataVertex(const FVertexName& InVertexName, const FDataVertexMetadata& InMetadata, ArgTypes&&... Args)
		: FInputDataVertex(InVertexName, GetMetasoundDataTypeName<DataType>(), InMetadata, Forward<ArgTypes>(Args)...)
		{
		}

		template<typename... ArgTypes>
 		UE_DEPRECATED(5.1, "TInputDataVertex constructor without explicit FDataVertexMatadata is deprecated. Use constructor which accepts TDataVertexMetadata")
		TInputDataVertex(const FVertexName& InVertexName, const FText& InDescription, ArgTypes&&... Args)
		: FInputDataVertex(InVertexName, GetMetasoundDataTypeName<DataType>(), { InDescription }, Forward<ArgTypes>(Args)...)
		{
		}
	};

	template<typename DataType>
	using TInputDataVertexModel UE_DEPRECATED(5.1, "TInputDataVertexModel<DataType> is deprecated. Replace with TInputDataVertex<DataType>") = TInputDataVertex<DataType>;
	
	/** FOutputDataVertex
	 *
	 * Vertex for outputs.
	 */
	class FOutputDataVertex : public FDataVertex
	{
	public:
		using FDataVertex::FDataVertex;

		friend bool METASOUNDGRAPHCORE_API operator==(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator<(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS);
	};

	/** Create a FOutputDataVertex with a templated MetaSound data type. */
	template<typename DataType>
	class TOutputDataVertex : public FOutputDataVertex
	{
	public:

		TOutputDataVertex() = default;

		template<typename... ArgTypes>
		TOutputDataVertex(const FVertexName& InVertexName, const FDataVertexMetadata& InMetadata, ArgTypes&&... Args)
		: FOutputDataVertex(InVertexName, GetMetasoundDataTypeName<DataType>(), InMetadata, Forward<ArgTypes>(Args)...)
		{
		}

		template<typename... ArgTypes>
 		UE_DEPRECATED(5.1, "TOutputDataVertex constructor without explicit FDataVertexMatadata is deprecated. Use constructor which accepts TDataVertexMetadata")
		TOutputDataVertex(const FVertexName& InVertexName, const FText& InDescription, ArgTypes&&... Args)
		: FOutputDataVertex(InVertexName, GetMetasoundDataTypeName<DataType>(), { InDescription }, Forward<ArgTypes>(Args)...)
		{
		}
	};

	template<typename DataType>
	using TOutputDataVertexModel UE_DEPRECATED(5.1, "TOutputDataVertexModel<DataType> is deprecated. Replace with TOutputDataVertex<DataType>") = TOutputDataVertex<DataType>;


	/** FEnvironmentVertex
	 *
	 * A vertex for environment variables. 
	 */
	class FEnvironmentVertex
	{
	public:
		/** FEnvironmentVertex Construtor
		 *
		 * @InVertexName - Name of vertex.
		 * @InDescription - Human readable vertex description.
		 */
		FEnvironmentVertex(const FVertexName& InVertexName, const FText& InDescription)
		:	VertexName(InVertexName)
		,	Description(InDescription)
		{
		}

		virtual ~FEnvironmentVertex() = default;

		/** Name of vertex. */
		FVertexName VertexName;

		/** Description of the vertex. */
		FText Description;

		/** Name of vertex. */
		UE_DEPRECATED(5.1, "GetVertexName() is deprecated. Use VertexName")
		const FVertexName& GetVertexName() const
		{
			return VertexName;
		}

		/** Description of the vertex. */
		UE_DEPRECATED(5.1, "GetDescription() is deprecated. Use Description")
		const FText& GetDescription() const
		{
			return Description;
		}

		friend bool METASOUNDGRAPHCORE_API operator==(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator<(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS);
	};

	/** TVertexInterfaceGroups encapsulates multiple related data vertices. It 
	 * requires that each vertex in the group have a unique FVertexName.
	 */
	template<typename VertexType>
	class TVertexInterfaceGroup
	{
			static constexpr bool bIsDerivedFromFInputDataVertex = std::is_base_of<FInputDataVertex, VertexType>::value;
			static constexpr bool bIsDerivedFromFOutputDataVertex = std::is_base_of<FOutputDataVertex, VertexType>::value;
			static constexpr bool bIsDerivedFromFEnvironmentVertex = std::is_base_of<FEnvironmentVertex, VertexType>::value;
			static constexpr bool bIsSupportedVertexType = bIsDerivedFromFInputDataVertex || bIsDerivedFromFOutputDataVertex || bIsDerivedFromFEnvironmentVertex;

			static_assert(bIsSupportedVertexType, "VertexType must be derived from FInputDataVertex, FOutputDataVertex, or FEnvironmentVertex");

			using FContainerType = TArray<VertexType>;

			struct FEqualVertexNamePredicate
			{
				const FVertexName& NameRef;

				FEqualVertexNamePredicate(const FVertexName& InName)
				: NameRef(InName)
				{
				}

				FEqualVertexNamePredicate(const VertexType& InVertex)
				: NameRef(InVertex.VertexName)
				{
				}

				bool operator()(const VertexType& InOther)
				{
					return InOther.VertexName == NameRef;
				}
			};

			void AddOrUpdateVertex(VertexType&& InVertex)
			{
				if (VertexType* Vertex = Find(InVertex.VertexName))
				{
					*Vertex = MoveTemp(InVertex);
				}
				else
				{
					Vertices.Add(MoveTemp(InVertex));
				}
			}

			// Required for end of recursion.
			void CopyInputs()
			{
			}

			// Recursive call used to unpack a template parameter pack. This stores
			// each object in the template parameter pack into the groups internal
			// storage.
			//
			// Assume that InputType is either a sublcass of FDataVertex
			// and that VertexType is either FInputDataVertex or FOutputDataVertex
			template<typename CurrentVertexType, typename... RemainingVertexTypes>
			void CopyInputs(CurrentVertexType&& InInput, RemainingVertexTypes&&... InRemainingInputs)
			{
				// Create vertex out of vertex model
				AddOrUpdateVertex(MoveTemp(InInput));
				CopyInputs(InRemainingInputs...);
			}


		public:

			using RangedForConstIteratorType = typename FContainerType::RangedForConstIteratorType;

			/** TVertexInterfaceGroup constructor with variadic list of vertex
			 * models.
			 */
			template<typename... VertexTypes>
			TVertexInterfaceGroup(VertexTypes&&... InVertexs)
			{
				CopyInputs(Forward<VertexTypes>(InVertexs)...);
			}

			/** Add a vertex to the group. */
			void Add(const VertexType& InVertex)
			{
				AddOrUpdateVertex(VertexType(InVertex));
			}

			void Add(VertexType&& InVertex)
			{
				AddOrUpdateVertex(MoveTemp(InVertex));
			}

			/** Remove a vertex by key. */
			bool Remove(const FVertexName& InKey)
			{
				int32 NumRemoved = Vertices.RemoveAll(FEqualVertexNamePredicate(InKey));
				return (NumRemoved > 0);
			}

			/** Returns true if the group contains a vertex with a matching key. */
			bool Contains(const FVertexName& InKey) const
			{
				return Vertices.ContainsByPredicate(FEqualVertexNamePredicate(InKey));
			}

			/** Find a vertex with a given VertexName */
			VertexType* Find(const FVertexName& InKey)
			{
				return Vertices.FindByPredicate(FEqualVertexNamePredicate(InKey));
			}

			/** Find a vertex with a given VertexName */
			const VertexType* Find(const FVertexName& InKey) const
			{
				return Vertices.FindByPredicate(FEqualVertexNamePredicate(InKey));
			}

			/** Return the sort order index of a vertex with the given name.
			 *
			 * @param InKey - FVertexName of vertex of interest.
			 *
			 * @return The index of the vertex. INDEX_NONE if the vertex does not exist. 
			 */
			int32 GetSortOrderIndex(const FVertexName& InKey) const
			{
				return Vertices.IndexOfByPredicate(FEqualVertexNamePredicate(InKey));
			}

			/** Return the vertex for a given vertex key. */
			const VertexType& operator[](const FVertexName& InName) const
			{
				const VertexType* Vertex = Find(InName);
				checkf(nullptr != Vertex, TEXT("Vertex with name '%s' does not exist"), *InName.ToString());
				return *Vertex;
			}

			/** Iterator for ranged for loops. */
			RangedForConstIteratorType begin() const
			{
				return Vertices.begin();
			}

			/** Iterator for ranged for loops. */
			RangedForConstIteratorType end() const
			{
				return Vertices.end();
			}

			/** Returns the number of vertices in the group. */
			int32 Num() const
			{
				return Vertices.Num();
			}

			/** Compare whether two vertex groups are equal. */
			friend bool operator==(const TVertexInterfaceGroup<VertexType>& InLHS, const TVertexInterfaceGroup<VertexType>& InRHS)
			{
				return InLHS.Vertices == InRHS.Vertices;
			}

			/** Compare whether two vertex groups are unequal. */
			friend bool operator!=(const TVertexInterfaceGroup<VertexType>& InLHS, const TVertexInterfaceGroup<VertexType>& InRHS)
			{
				return !(InLHS == InRHS);
			}

		private:

			FContainerType Vertices;
	};

	
	/** FInputVertexInterface is a TVertexInterfaceGroup which holds FInputDataVertexes. */
	typedef TVertexInterfaceGroup<FInputDataVertex> FInputVertexInterface;

	/** FOutputVertexInterface is a TVertexInterfaceGroup which holds FOutputDataVertexes. */
	typedef TVertexInterfaceGroup<FOutputDataVertex> FOutputVertexInterface;

	/** FEnvironmentVertexInterface is a TVertexInterfaceGroup which holds FEnvironmentVertexes. */
	typedef TVertexInterfaceGroup<FEnvironmentVertex> FEnvironmentVertexInterface;

	/** FVertexInterface provides access to a collection of input and output vertex
	 * interfaces. 
	 */
	class METASOUNDGRAPHCORE_API FVertexInterface
	{
		public:

			/** Default constructor. */
			FVertexInterface() = default;

			/** Construct with an input and output interface. */
			FVertexInterface(const FInputVertexInterface& InInputs, const FOutputVertexInterface& InOutputs);

			/** Construct with input, output and environment interface. */
			FVertexInterface(const FInputVertexInterface& InInputs, const FOutputVertexInterface& InOutputs, const FEnvironmentVertexInterface& InEnvironmentVariables);

			/** Return the input interface. */
			const FInputVertexInterface& GetInputInterface() const;

			/** Return the input interface. */
			FInputVertexInterface& GetInputInterface();

			/** Return an input vertex. */
			const FInputDataVertex& GetInputVertex(const FVertexName& InKey) const;

			/** Returns true if an input vertex with the given key exists. */
			bool ContainsInputVertex(const FVertexName& InKey) const;

			/** Return the output interface. */
			const FOutputVertexInterface& GetOutputInterface() const;

			/** Return the output interface. */
			FOutputVertexInterface& GetOutputInterface();

			/** Return an output vertex. */
			const FOutputDataVertex& GetOutputVertex(const FVertexName& InName) const;

			/** Returns true if an output vertex with the given name exists. */
			bool ContainsOutputVertex(const FVertexName& InName) const;

			/** Return the output interface. */
			const FEnvironmentVertexInterface& GetEnvironmentInterface() const;

			/** Return the output interface. */
			FEnvironmentVertexInterface& GetEnvironmentInterface();

			/** Return an output vertex. */
			const FEnvironmentVertex& GetEnvironmentVertex(const FVertexName& InKey) const;

			/** Returns true if an output vertex with the given key exists. */
			bool ContainsEnvironmentVertex(const FVertexName& InKey) const;

			/** Test for equality between two interfaces. */
			friend bool METASOUNDGRAPHCORE_API operator==(const FVertexInterface& InLHS, const FVertexInterface& InRHS);

			/** Test for inequality between two interfaces. */
			friend bool METASOUNDGRAPHCORE_API operator!=(const FVertexInterface& InLHS, const FVertexInterface& InRHS);

		private:

			FInputVertexInterface InputInterface;
			FOutputVertexInterface OutputInterface;
			FEnvironmentVertexInterface EnvironmentInterface;
	};

	/**
	 * This struct is used to pass in any arguments required for constructing a single node instance.
	 * because of this, all FNode implementations have to implement a constructor that takes an FNodeInitData instance.
	 */
	struct FNodeInitData
	{
		FVertexName InstanceName;
		FGuid InstanceID;
	};
}
