// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/SortedMap.h"
#include "MetasoundDataReference.h"
#include "MetasoundEnvironment.h"

#include <type_traits>

namespace Metasound
{
	/** Key type for an FInputDataVertexColletion or 
	 * FOutputDataVertexCollection. 
	 */
	typedef FString FVertexKey;
	typedef FString FDataVertexKey; // TODO: Remove

	// Forward declare.
	class FInputDataVertex;

	// Forward declare.
	class FOutputDataVertex;

	// Forward declare
	class FEnvironmentVertex;

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
		/** FDataVertexModel Construtor
		 *
		 * @InVertexName - Name of vertex.
		 * @InDataTypeName - Name of data type.
		 * @InDescription - Human readable vertex description.
		 */
		FDataVertexModel(const FString& InVertexName, const FName& InDataTypeName, const FText& InDescription)
		:	VertexName(InVertexName)
		,	DataTypeName(InDataTypeName)
		,	Description(InDescription)
		{
		}

		virtual ~FDataVertexModel() = default;

		/** Name of vertex. */
		const FString VertexName;

		/** Type name of data. */
		const FName DataTypeName;

		/** Description of the vertex. */
		const FText Description;

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
	struct FInputDataVertexModel: FDataVertexModel
	{
		// Inherit constructor
		using FDataVertexModel::FDataVertexModel;

		/** Create a clone of this FInputDataVertexModel */
		virtual TUniquePtr<FInputDataVertexModel> Clone() const = 0;

		friend bool METASOUNDGRAPHCORE_API operator==(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS);
		friend bool METASOUNDGRAPHCORE_API operator!=(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS);

		private:

		/** Compare another FInputDataVertexModel is equal to this
		 *
		 * @return True if models are equal, false otherwise.
		 */
		virtual bool IsEqual(const FInputDataVertexModel& InOther) const = 0;
	};

	/** FOutputDataVertexModel
	 *
	 * Vertex model for outputs.
	 */
	struct FOutputDataVertexModel : FDataVertexModel
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
	struct TBaseVertexModel : VertexModelType
	{
		/** TBaseVertexModel Construtor
		 *
		 * @InVertexName - Name of vertex.
		 * @InDescription - Human readable vertex description.
		 */
		TBaseVertexModel(const FString& InVertexName, const FText& InDescription)
		:	VertexModelType(InVertexName, GetMetasoundDataTypeName<DataType>(), InDescription)
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
	struct TInputDataVertexModel : TBaseVertexModel<DataType, FInputDataVertexModel>
	{
		using TBaseVertexModel<DataType, FInputDataVertexModel>::TBaseVertexModel;

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
			return MakeUnique<TInputDataVertexModel<DataType>>(this->VertexName, this->Description);
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
			const FString& GetVertexName() const;

			/** Type name of data reference. */
			const FName& GetDataTypeName() const;

			/** Description of the vertex. */
			const FText& GetDescription() const;

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
			return MakeUnique<TOutputDataVertexModel<DataType>>(this->VertexName, this->Description);
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
			const FString& GetVertexName() const;

			/** Type name of data reference. */
			const FName& GetDataTypeName() const;

			/** Description of the vertex. */
			const FText& GetDescription() const;

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
		FEnvironmentVertexModel(const FString& InVertexName, const FText& InDescription)
		:	VertexName(InVertexName)
		,	Description(InDescription)
		{
		}

		virtual ~FEnvironmentVertexModel() = default;

		/** Name of vertex. */
		const FString VertexName;

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
			const FString& GetVertexName() const;

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


	/** Create an FVertexKey from a FDataVertex. */
	FORCEINLINE FVertexKey MakeDataVertexKey(const FInputDataVertex& InVertex)
	{
		return InVertex.GetVertexName();
	}

	/** Create an FVertexKey from a FDataVertex. */
	FORCEINLINE FVertexKey MakeDataVertexKey(const FOutputDataVertex& InVertex)
	{
		return InVertex.GetVertexName();
	}

	/** Create an FVertexKey from a FEnvironmentVariableVertex. */
	FORCEINLINE FVertexKey MakeEnvironmentVertexKey(const FEnvironmentVertex& InVertex)
	{
		return InVertex.GetVertexName();
	}

	/** TVertexInterfaceGroups encapsulates multiple related data vertices. It 
	 * requires that each vertex in the group have a unique FVertexKey.
	 */
	template<typename VertexType>
	class TVertexInterfaceGroup
	{
			static constexpr bool bIsDerivedFromFInputDataVertex = std::is_base_of<FInputDataVertex, VertexType>::value;
			static constexpr bool bIsDerivedFromFOutputDataVertex = std::is_base_of<FOutputDataVertex, VertexType>::value;
			static constexpr bool bIsDerivedFromFEnvironmentVertex = std::is_base_of<FEnvironmentVertex, VertexType>::value;
			static constexpr bool bIsSupportedVertexType = bIsDerivedFromFInputDataVertex || bIsDerivedFromFOutputDataVertex || bIsDerivedFromFEnvironmentVertex;

			static_assert(bIsSupportedVertexType, "VertexType must be derived from FInputDataVertex, FOutputDataVertex, or FEnvironmentVertex");

			using FContainerType = TSortedMap<FVertexKey, VertexType>;

			// Required for end of recursion.
			static void CopyInputs(FContainerType& InStorage)
			{
			}

			// Recursive call used to unpack a template parameter pack. This stores
			// each object in the template parameter pack into the groups internal
			// storage.
			//
			// Assume that InputType is either a sublcass of FDataVertexModel
			// and that VertexType is either FInputDataVertex or FOutputDataVertex
			template<typename VertexModelType, typename... RemainingVertexModelTypes>
			static void CopyInputs(FContainerType& InStorage, const VertexModelType& InInput, const RemainingVertexModelTypes&... InRemainingInputs)
			{
				// Create vertex out of vertex model
				VertexType Vertex(InInput);

				InStorage.Add(MakeDataVertexKey(Vertex), Vertex);
				CopyInputs(InStorage, InRemainingInputs...);
			}

		public:

			using RangedForConstIteratorType = typename TSortedMap<FVertexKey, VertexType>::RangedForConstIteratorType;

			/** TVertexInterfaceGroup constructor with variadic list of vertex
			 * models.
			 */
			template<typename... VertexModelTypes>
			TVertexInterfaceGroup(VertexModelTypes&&... InVertexModels)
			{
				CopyInputs(Vertices, Forward<VertexModelTypes>(InVertexModels)...);
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
				Vertices.Add(MakeDataVertexKey(InVertex), InVertex);
			}

			/** Remove a vertex by key. */
			bool Remove(const FVertexKey& InKey)
			{
				int32 NumRemoved = Vertices.Remove();
				return (NumRemoved > 0);
			}

			/** Returns true if the group contains a vertex with a matching key. */
			bool Contains(const FVertexKey& InKey) const
			{
				return Vertices.Contains(InKey);
			}

			/** Return the vertex for a given vertex key. */
			const VertexType& operator[](const FVertexKey& InKey) const
			{
				return Vertices[InKey];
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
			const FInputDataVertex& GetInputVertex(const FVertexKey& InKey) const;

			/** Returns true if an input vertex with the given key exists. */
			bool ContainsInputVertex(const FVertexKey& InKey) const;

			/** Return the output interface. */
			const FOutputVertexInterface& GetOutputInterface() const;

			/** Return the output interface. */
			FOutputVertexInterface& GetOutputInterface();

			/** Return an output vertex. */
			const FOutputDataVertex& GetOutputVertex(const FVertexKey& InKey) const;

			/** Returns true if an outptu vertex with the given key exists. */
			bool ContainsOutputVertex(const FVertexKey& InKey) const;

			/** Return the output interface. */
			const FEnvironmentVertexInterface& GetEnvironmentInterface() const;

			/** Return the output interface. */
			FEnvironmentVertexInterface& GetEnvironmentInterface();

			/** Return an output vertex. */
			const FEnvironmentVertex& GetEnvironmentVertex(const FVertexKey& InKey) const;

			/** Returns true if an outptu vertex with the given key exists. */
			bool ContainsEnvironmentVertex(const FVertexKey& InKey) const;

			/** Test for equality between two interfaces. */
			friend bool METASOUNDGRAPHCORE_API operator==(const FVertexInterface& InLHS, const FVertexInterface& InRHS);

			/** Test for inequality between two interfaces. */
			friend bool METASOUNDGRAPHCORE_API operator!=(const FVertexInterface& InLHS, const FVertexInterface& InRHS);

		private:

			FInputVertexInterface InputInterface;
			FOutputVertexInterface OutputInterface;
			FEnvironmentVertexInterface EnvironmentInterface;
	};
}
