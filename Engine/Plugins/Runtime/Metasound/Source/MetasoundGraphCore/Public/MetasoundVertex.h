// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/SortedMap.h"
#include "MetasoundDataFactory.h"
#include "MetasoundDataReference.h"
#include "MetasoundEnvironment.h"
#include "MetasoundLiteral.h"

#include <type_traits>

namespace Metasound
{
	/** Name of a given vertex.  Only unique for a given node interface.
	 */
	using FVertexName = FName;

	// Forward declarations
	class FInputDataVertex;
	class FOutputDataVertex;
	class FEnvironmentVertex;

	// Vertex metadata
	struct FDataVertexMetadata
	{
		FText Description;
		FText DisplayName;
		bool bIsAdvancedDisplay = false;
	};

	/** FDataVertexModel
	 *
	 * An FDataVertexModel implements various vertex behaviors. 
	 *
	 * Essentially, an FDataVertexModel implements the underlying behavior of a
	 * FInputDataVertex or FOutputDataVertex.  
	 *
	 * The FInputDataVertex and FOutputDataVertex supply copy constructors and 
	 * assignment operators by utilizing the Clone() operator of the 
	 * FDataVertexModel. This allows FDataVertexModel behavior to be passed 
	 * to other objects without having to know the concrete implementation of the
	 * FDataVertexModel.
	 */
	struct FDataVertexModel
	{
		/** FDataVertexModel Constructor
		 *
		 * @InVertexName - Name of vertex.
		 * @InDataTypeName - Name of data type.
		 * @InMetadata - Metadata pertaining to given vertex.
		 */
		FDataVertexModel(const FVertexName& InVertexName, const FName& InDataTypeName, const FDataVertexMetadata& InMetadata)
		:	VertexName(InVertexName)
		,	DataTypeName(InDataTypeName)
		,	VertexMetadata(InMetadata)
		{
		}

		virtual ~FDataVertexModel() = default;

		/** Name of vertex. */
		const FVertexName VertexName;

		/** Type name of data. */
		const FName DataTypeName;

		/** Metadata associated with vertex. */
		const FDataVertexMetadata VertexMetadata;

		/** Test if a IDataReference matches the DataType of this vertex model.
		 *
		 * @param InReference - Data reference object.
		 *
		 * @return True if the types are equal, false otherwise.
		 */
		virtual bool IsReferenceOfSameType(const IDataReference& InReference) const = 0;

		/** Return the vertex type name (not to be confused with the data type name).
		 *
		 * @return Name of this vertex type.
		 */
		virtual const FName& GetVertexTypeName() const = 0;
	};


	/** FInputDataVertexModel
	 *
	 * Vertex model for inputs.
	 */
	struct FInputDataVertexModel : public FDataVertexModel
	{
	private:
		
		// Factory for creating a literal. 
		struct ILiteralFactory
		{
			virtual ~ILiteralFactory() = default;
			virtual FLiteral CreateLiteral() const = 0;
			virtual TUniquePtr<ILiteralFactory> Clone() const = 0;
		};

		// Create a literal with a copyable type.
		template<typename LiteralValueType>
		struct TLiteralFactory : ILiteralFactory
		{
			LiteralValueType LiteralValue;

			TLiteralFactory(const LiteralValueType& InValue)
			: LiteralValue(InValue)
			{
			}
			
			FLiteral CreateLiteral() const override
			{
				return FLiteral(LiteralValue);
			}

			TUniquePtr<ILiteralFactory> Clone() const override
			{
				return MakeUnique<TLiteralFactory<LiteralValueType>>(*this);
			}
		};

	public:

		FInputDataVertexModel(const FVertexName& InVertexName, const FName& InDataTypeName, const FText& InDescription)
			: FDataVertexModel(InVertexName, InDataTypeName, { InDescription })
			, LiteralFactory(MakeUnique<TLiteralFactory<FLiteral::FNone>>(FLiteral::FNone{}))
		{
		}

		FInputDataVertexModel(const FVertexName& InVertexName, const FName& InDataTypeName, const FDataVertexMetadata& InMetadata)
			: FDataVertexModel(InVertexName, InDataTypeName, InMetadata)
			, LiteralFactory(MakeUnique<TLiteralFactory<FLiteral::FNone>>(FLiteral::FNone{}))
		{
		}

		template<typename LiteralValueType>
		FInputDataVertexModel(const FVertexName& InVertexName, const FName& InDataTypeName, const FText& InDescription, const LiteralValueType& InLiteralValue)
			: FDataVertexModel(InVertexName, InDataTypeName, { InDescription })
			, LiteralFactory(MakeUnique<TLiteralFactory<LiteralValueType>>(InLiteralValue))
		{
		}

		template<typename LiteralValueType>
		FInputDataVertexModel(const FVertexName& InVertexName, const FName& InDataTypeName, const FDataVertexMetadata& InMetadata, const LiteralValueType& InLiteralValue)
			: FDataVertexModel(InVertexName, InDataTypeName, InMetadata)
			, LiteralFactory(MakeUnique<TLiteralFactory<LiteralValueType>>(InLiteralValue))
		{
		}

		FInputDataVertexModel(const FInputDataVertexModel& InOther)
		: FDataVertexModel(InOther)
		{
			if (InOther.LiteralFactory)
			{
				LiteralFactory = InOther.LiteralFactory->Clone();
			}
		}

		/** Create a clone of this FInputDataVertexModel */
		virtual TUniquePtr<FInputDataVertexModel> Clone() const = 0;

		/** Creates the default literal for this vertex. If a default does not exist,
		 * then an invalid literal is returned. 
		 */
		FLiteral CreateDefaultLiteral() const 
		{
			if (LiteralFactory.IsValid())
			{
				return LiteralFactory->CreateLiteral();
			}

			return FLiteral::CreateInvalid();
		}

		friend bool METASOUNDGRAPHCORE_API operator==(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS);

		private:

		/** Compare another FInputDataVertexModel is equal to this
		 *
		 * @return True if models are equal, false otherwise.
		 */
		virtual bool IsEqual(const FInputDataVertexModel& InOther) const = 0;

		TUniquePtr<ILiteralFactory> LiteralFactory;
	};

	/** FOutputDataVertexModel
	 *
	 * Vertex model for outputs.
	 */
	struct FOutputDataVertexModel : public FDataVertexModel
	{
		using FDataVertexModel::FDataVertexModel;

		/** Create a clone of this FOutputDataVertexModel */
		virtual TUniquePtr<FOutputDataVertexModel> Clone() const = 0;

		friend bool METASOUNDGRAPHCORE_API operator==(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS);

		private:

		/** Compare another FOutputDataVertexModel is equal to this
		 *
		 * @return True if models are equal, false otherwise.
		 */
		virtual bool IsEqual(const FOutputDataVertexModel& InOther) const = 0;

	};

	/** TBaseVertexModel provides basic functionality of vertex models. */
	template<typename DataType, typename VertexModelType>
	struct TBaseVertexModel : public VertexModelType
	{
		/** TBaseVertexModel Constructor
		 *
		 * @InVertexName - Name of vertex.
		 * @InDescription - Human readable vertex description.
		 */
		template<typename... ModelArgTypes>
		TBaseVertexModel(const FVertexName& InVertexName, const FText& InDescription, ModelArgTypes&&... ModelArgs)
		:	VertexModelType(InVertexName, GetMetasoundDataTypeName<DataType>(), { InDescription }, Forward<ModelArgTypes>(ModelArgs)...)
		{
		}

		/** TBaseVertexModel Constructor
		 *
		 * @InVertexName - Name of vertex.
		 * @InVertexMetadata - Vertex metadata, used primarily for debugging or live edit.
		 */
		template<typename... ModelArgTypes>
		TBaseVertexModel(const FVertexName& InVertexName, const FDataVertexMetadata& InMetadata, ModelArgTypes&&... ModelArgs)
		:	VertexModelType(InVertexName, GetMetasoundDataTypeName<DataType>(), InMetadata, Forward<ModelArgTypes>(ModelArgs)...)
		{
		}

		/** Test if a IDataReference matches the DataType of this vertex model.
		 *
		 * @param InReference - Data reference object.
		 *
		 * @return True if the types are equal, false otherwise.
		 */
		virtual bool IsReferenceOfSameType(const IDataReference& InReference) const override
		{
			return IsDataReferenceOfType<DataType>(InReference);
		}


		private:

		/** Compare if another VertexModelType is equal to this.
		 *
		 * @return True if models are equal, false otherwise.
		 */
		virtual bool IsEqual(const VertexModelType& InOther) const override
		{
			bool bIsEqual = this->GetVertexTypeName() == InOther.GetVertexTypeName();

			bIsEqual &= this->VertexName == InOther.VertexName;
			bIsEqual &= this->DataTypeName == InOther.DataTypeName;

			return bIsEqual;
		}
	};

	/** TOuputDataVertexModel creates a simple, unchanging, input vertex. */
	template<typename DataType>
	struct TInputDataVertexModel : public TBaseVertexModel<DataType, FInputDataVertexModel>
	{

		using TBaseVertexModel<DataType, FInputDataVertexModel>::TBaseVertexModel;

		/** TInputDataVertexModel Constructor
		 *
		 * @InVertexName - Name of vertex.
		 * @InDescription - Human readable vertex description.
		 */
		TInputDataVertexModel(const FVertexName& InVertexName, const FText& InDescription)
		:	TBaseVertexModel<DataType, FInputDataVertexModel>(InVertexName, InDescription)
		{
		}

		/** TInputDataVertexModel Constructor
		 *
		 * @InVertexName - Name of vertex.
		 * @InDescription - Human readable vertex description.
		 * @InDefaultValue - Default Value of vertex
		 */
		template<typename LiteralValueType>
		TInputDataVertexModel(const FVertexName& InVertexName, const FText& InDescription, const LiteralValueType& InDefaultValue)
		:	TBaseVertexModel<DataType, FInputDataVertexModel>(InVertexName, InDescription, InDefaultValue)
		{
		}

		/** TInputDataVertexModel Constructor
		 *
		 * @InVertexName - Name of vertex.
		 * @InDescription - Human readable vertex description.
		 * @InDefaultValue - Default Value of vertex
		 */
		template<typename LiteralValueType>
		TInputDataVertexModel(const FVertexName& InVertexName, const FDataVertexMetadata& InMetadata)
		:	TBaseVertexModel<DataType, FInputDataVertexModel>(InVertexName, InMetadata)
		{
		}

		TInputDataVertexModel(const TInputDataVertexModel& InOther) = default;

		/** Return the vertex type name (not to be confused with the data type name).
		 *
		 * @return Name of this vertex type.
		 */
		virtual const FName& GetVertexTypeName() const override
		{
			static const FString VertexTypeString = TEXT("InputDataVertex_") + GetMetasoundDataTypeName<DataType>().ToString();
			static const FName VertexTypeName(*VertexTypeString);

			return VertexTypeName;
		}

		/** Create a clone of this VertexModelType */
		virtual TUniquePtr<FInputDataVertexModel> Clone() const override
		{
			return MakeUnique<TInputDataVertexModel<DataType>>(*this);
		}
	};


	/** FInputDataVertex represents an input vertex to an interface. It uses
	 * a FInputDataVertexModel to determine its behavior.
	 */
	class METASOUNDGRAPHCORE_API FInputDataVertex
	{
			using FEmptyVertexModel = TInputDataVertexModel<void>;

		public:

			/** Construct an FInputDataVertex with a given vertex model.
			 *
			 * @param InVertexModel - A model subclass of FInputDataVertexModel.
			 */
			template<typename VertexModelType, typename = typename TEnableIf<TIsDerivedFrom<VertexModelType, FInputDataVertexModel>::Value>::Type>
			FInputDataVertex(const VertexModelType& InVertexModel)
			:	VertexModel(InVertexModel.Clone())
			{
				static_assert(TIsDerivedFrom<VertexModelType, FInputDataVertexModel>::Value, "Vertex implementation must be a subclass of FInputDataVertexModel");

				if (!VertexModel.IsValid())
				{
					VertexModel = MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty());
				}
			}

			/** Construct an empty FInputDataVertex. */
			FInputDataVertex();

			/** Copy constructor */
			FInputDataVertex(const FInputDataVertex& InOther);

			/** Assignment operator. */
			FInputDataVertex& operator=(const FInputDataVertex& InOther);

			/** Name of vertex. */
			const FVertexName& GetVertexName() const;

			/** Type name of data reference. */
			const FName& GetDataTypeName() const;

			/** Description of the vertex. */
			const FDataVertexMetadata& GetMetadata() const;

			/** Default value of the vertex. */
			FLiteral GetDefaultLiteral() const;

			/** Determine if vertex refers to same data type. 
			 *
			 * @param InReference - Data reference object.
			 *
			 * @return True if the types are equal, false otherwise.
			 */
			bool IsReferenceOfSameType(const IDataReference& InReference) const;

			/** Return the vertex type name (not to be confused with the data type name).
			 *
			 * @return Name of this vertex type.
			 */
			const FName& GetVertexTypeName() const;

			friend bool METASOUNDGRAPHCORE_API operator==(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS);
			friend bool METASOUNDGRAPHCORE_API operator!=(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS);
			friend bool METASOUNDGRAPHCORE_API operator<(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS);

		private:

			TUniquePtr<FInputDataVertexModel> VertexModel;
	};

	/** TOuputDataVertexModel creates a simple, unchanging, output vertex. */
	template<typename DataType>
	struct TOutputDataVertexModel : TBaseVertexModel<DataType, FOutputDataVertexModel>
	{
		using TBaseVertexModel<DataType, FOutputDataVertexModel>::TBaseVertexModel;

		/** Return the vertex type name (not to be confused with the data type name).
		 *
		 * @return Name of this vertex type.
		 */
		virtual const FName& GetVertexTypeName() const override
		{
			static const FString VertexTypeString = TEXT("OutputDataVertex_") + GetMetasoundDataTypeName<DataType>().ToString();
			static const FName VertexTypeName(*VertexTypeString);

			return VertexTypeName;
		}

		/** Create a clone of this TOutputDataVertexModel*/
		virtual TUniquePtr<FOutputDataVertexModel> Clone() const override
		{
			return MakeUnique<TOutputDataVertexModel<DataType>>(this->VertexName, this->VertexMetadata);
		}
	};

	/** FOutputDataVertex represents an input vertex to an interface. It uses
	 * a FOutputDataVertexModel to determine its behavior.
	 */
	class METASOUNDGRAPHCORE_API FOutputDataVertex
	{
		using FEmptyVertexModel = TOutputDataVertexModel<void>;

		public:

			/** Construct an FOutputDataVertex with a given vertex model.
			 *
			 * @param InVertexModel - A model subclass of FOutputDataVertexModel.
			 */
			template<typename VertexModelType>
			FOutputDataVertex(const VertexModelType& InVertexModel)
			:	VertexModel(InVertexModel.Clone())
			{
				static_assert(TIsDerivedFrom<VertexModelType, FOutputDataVertexModel>::Value, "Vertex implementation must be a subclass of FOutputDataVertexModel");

				if (!VertexModel.IsValid())
				{
					VertexModel = MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty());
				}
			}

			/** Construct an empty FInputDataVertex. */
			FOutputDataVertex();

			/** Copy constructor */
			FOutputDataVertex(const FOutputDataVertex& InOther);

			/** Assignment operator. */
			FOutputDataVertex& operator=(const FOutputDataVertex& InOther);

			/** Name of vertex. */
			FVertexName GetVertexName() const;

			/** Type name of data reference. */
			const FName& GetDataTypeName() const;

			/** Metadata of the vertex. */
			const FDataVertexMetadata& GetMetadata() const;

			/** Test if a IDataReference matches the DataType of this vertex.
			 *
			 * @param InReference - Data reference object.
			 *
			 * @return True if the types are equal, false otherwise.
			 */
			bool IsReferenceOfSameType(const IDataReference& InReference) const;

			/** Return the vertex type name (not to be confused with the data type name).
			 *
			 * @return Name of this vertex type.
			 */
			const FName& GetVertexTypeName() const;

			friend bool METASOUNDGRAPHCORE_API operator==(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS);
			friend bool METASOUNDGRAPHCORE_API operator!=(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS);
			friend bool METASOUNDGRAPHCORE_API operator<(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS);

		private:

			TUniquePtr<FOutputDataVertexModel> VertexModel;
	};

	/** FEnvironmentVertexModel
	 *
	 * An FEnvironmentVertexModel implements various vertex behaviors. 
	 *
	 * The FEnvironmentVertex supplies copy constructors and assignment operators 
	 * by utilizing the Clone() operator of the FEnvironmentVertexModel. 
	 * This allows FEnvironmentVertexModel behavior to be passed to other objects 
	 * without having to know the concrete implementation of the FEnvironmentVertexModel.
	 */
	struct FEnvironmentVertexModel
	{
		/** FEnvironmentVertexModel Construtor
		 *
		 * @InVertexName - Name of vertex.
		 * @InDescription - Human readable vertex description.
		 */
		FEnvironmentVertexModel(FVertexName InVertexName, const FText& InDescription)
		:	VertexName(InVertexName)
		,	Description(InDescription)
		{
		}

		virtual ~FEnvironmentVertexModel() = default;

		/** Name of vertex. */
		const FVertexName VertexName;

		/** Description of the vertex. */
		const FText Description;

		/** Test if a IMetasoundEnvironmentVariable matches the DataType of this vertex model.
		 *
		 * @param InVariable - Environment variable object.
		 *
		 * @return True if the types are equal, false otherwise.
		 */
		virtual bool IsVariableOfSameType(const IMetasoundEnvironmentVariable& InVariable) const = 0;

		virtual FMetasoundEnvironmentVariableTypeId GetVariableTypeId() const = 0;

		/** Create a clone of this FEnvironmentVertexModel */
		virtual TUniquePtr<FEnvironmentVertexModel> Clone() const = 0;

		friend bool METASOUNDGRAPHCORE_API operator==(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS);

		private:

		/** Compare another FEnvironmentVertexModel is equal to this
		 *
		 * @return True if models are equal, false otherwise.
		 */
		virtual bool IsEqual(const FEnvironmentVertexModel& InOther) const = 0;
	};

	/** TEnvironmentVertexModel creates a simple, unchanging, environment vertex. */
	template<typename VarType>
	struct TEnvironmentVertexModel : FEnvironmentVertexModel
	{
		/** Inherited constructor. */
		using FEnvironmentVertexModel::FEnvironmentVertexModel;

		/** Test if a IMetasoundEnvironmentVariable matches the DataType of this vertex model.
		 *
		 * @param InVariable - Environment variable object.
		 *
		 * @return True if the types are equal, false otherwise.
		 */
		bool IsVariableOfSameType(const IMetasoundEnvironmentVariable& InVariable) const override
		{
			return IsEnvironmentVariableOfType<VarType>(InVariable);
		}
		
		/** Return the type id of the vertex. */
		FMetasoundEnvironmentVariableTypeId GetVariableTypeId() const override
		{
			return GetMetasoundEnvironmentVariableTypeId<VarType>();
		}

		/** Create a clone of this TEnvironmentVertexModel*/
		virtual TUniquePtr<FEnvironmentVertexModel> Clone() const override
		{
			return MakeUnique<TEnvironmentVertexModel<VarType>>(this->VertexName, this->Description);
		}

		friend bool METASOUNDGRAPHCORE_API operator==(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS);
	private:

		bool IsEqual(const FEnvironmentVertexModel& InOther) const override
		{
			return (GetVariableTypeId() == InOther.GetVariableTypeId()) && (VertexName == InOther.VertexName);
		}
	};

	/** FEnvironmentVertex represents an input vertex to an interface. It uses
	 * a FEnvironmentVertexModel to determine its behavior.
	 */
	class METASOUNDGRAPHCORE_API FEnvironmentVertex
	{
		using FEmptyVertexModel = TEnvironmentVertexModel<void>;

		public:

			/** Construct an FEnvironmentVertex with a given vertex model.
			 *
			 * @param InVertexModel - A model subclass of FEnvironmentVertexModel.
			 */
			template<typename VertexModelType>
			FEnvironmentVertex(const VertexModelType& InVertexModel)
			:	VertexModel(InVertexModel.Clone())
			{
				static_assert(TIsDerivedFrom<VertexModelType, FEnvironmentVertexModel>::Value, "Vertex implementation must be a subclass of FEnvironmentVertexModel");

				if (!VertexModel.IsValid())
				{
					VertexModel = MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty());
				}
			}

			/** Construct an empty FInputDataVertex. */
			FEnvironmentVertex();

			/** Copy constructor */
			FEnvironmentVertex(const FEnvironmentVertex& InOther);

			/** Assignment operator. */
			FEnvironmentVertex& operator=(const FEnvironmentVertex& InOther);

			/** Name of vertex. */
			const FVertexName& GetVertexName() const;

			/** Description of the vertex. */
			const FText& GetDescription() const;

			/** Test if a IMetasoundEnvironmentVariable matches the DataType of this vertex.
			 *
			 * @param InVariable - Environment variable object.
			 *
			 * @return True if the types are equal, false otherwise.
			 */
			bool IsVariableOfSameType(const IMetasoundEnvironmentVariable& InVariable) const;

			friend bool METASOUNDGRAPHCORE_API operator==(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS);
			friend bool METASOUNDGRAPHCORE_API operator!=(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS);
			friend bool METASOUNDGRAPHCORE_API operator<(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS);

		private:

			TUniquePtr<FEnvironmentVertexModel> VertexModel;
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

			using FContainerType = TSortedMap<FVertexName, VertexType, FDefaultAllocator, FNameFastLess>;
			using FOrderContainerType = TArray<FVertexName>;

			// Required for end of recursion.
			static void CopyInputs(FContainerType& InStorage, FOrderContainerType& InOrder)
			{
			}

			// Recursive call used to unpack a template parameter pack. This stores
			// each object in the template parameter pack into the groups internal
			// storage.
			//
			// Assume that InputType is either a sublcass of FDataVertexModel
			// and that VertexType is either FInputDataVertex or FOutputDataVertex
			template<typename VertexModelType, typename... RemainingVertexModelTypes>
			static void CopyInputs(FContainerType& InStorage, FOrderContainerType& InKeyOrder, const VertexModelType& InInput, const RemainingVertexModelTypes&... InRemainingInputs)
			{
				// Create vertex out of vertex model
				VertexType Vertex(InInput);

				InStorage.Add(Vertex.GetVertexName(), Vertex);
				InKeyOrder.Add(Vertex.GetVertexName());
				CopyInputs(InStorage, InKeyOrder, InRemainingInputs...);
			}

		public:

			using RangedForConstIteratorType = typename FContainerType::RangedForConstIteratorType;

			/** TVertexInterfaceGroup constructor with variadic list of vertex
			 * models.
			 */
			template<typename... VertexModelTypes>
			TVertexInterfaceGroup(VertexModelTypes&&... InVertexModels)
			{
				CopyInputs(Vertices, OrderedKeys, Forward<VertexModelTypes>(InVertexModels)...);
			}

			/** Add a vertex model to the group. */
			template<typename VertexModelType, typename = typename TEnableIf<TIsDerivedFrom<VertexModelType, FDataVertexModel>::Value>::Type>
			void Add(const VertexModelType& InVertexModel)
			{
				VertexType Vertex(InVertexModel);
				Add(Vertex);
			}

			/** Add a vertex to the group. */
			void Add(const VertexType& InVertex)
			{
				Vertices.Add(InVertex.GetVertexName(), InVertex);
				OrderedKeys.Add(InVertex.GetVertexName());
			}

			/** Remove a vertex by key. */
			bool Remove(const FVertexName& InKey)
			{
				int32 NumRemoved = Vertices.Remove(InKey);
				OrderedKeys.Remove(InKey);
				return (NumRemoved > 0);
			}

			/** Returns true if the group contains a vertex with a matching key. */
			bool Contains(const FVertexName& InKey) const
			{
				return Vertices.Contains(InKey);
			}

			const VertexType* Find(const FVertexName& InKey) const
			{
				return Vertices.Find(InKey);
			}

			int32 GetSortOrderIndex(const FVertexName& InKey) const
			{
				int32 OutIndex = INDEX_NONE;
				OrderedKeys.Find(InKey, OutIndex);
				return OutIndex;
			}

			/** Return the vertex for a given vertex key. */
			const VertexType& operator[](const FVertexName& InName) const
			{
				return Vertices[InName];
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
			FOrderContainerType OrderedKeys;
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
