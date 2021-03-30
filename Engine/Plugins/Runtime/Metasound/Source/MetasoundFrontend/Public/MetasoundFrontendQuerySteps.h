// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendQuery.h"

namespace Metasound
{
	/** Generates all registered node classes */
	class METASOUNDFRONTEND_API FGenerateAllAvailableNodeClasses : public IFrontendQueryGenerateStep
	{
	public:

		void Generate(TArray<FFrontendQueryEntry>& OutEntries) const override;
	};

	/** Generates node classes that have been newly registered since last call
	 * to this FGenerateNewlyAvailableClasses::Generate()
	 */
	class METASOUNDFRONTEND_API FGenerateNewlyAvailableNodeClasses : public IFrontendQueryGenerateStep
	{
	public:
		FGenerateNewlyAvailableNodeClasses();
		void Generate(TArray<FFrontendQueryEntry>& OutEntries) const override;

	private:
		mutable Frontend::FRegistryTransactionID CurrentTransactionID;
	};

	class METASOUNDFRONTEND_API FFilterClassesByInputVertexDataType : public IFrontendQueryFilterStep
	{
	public: 
		template<typename DataType>
		FFilterClassesByInputVertexDataType()
		:	FFilterClassesByInputVertexDataType(GetMetasoundDataTypeName<DataType>())
		{
		}

		FFilterClassesByInputVertexDataType(const FName& InTypeName);

		bool Filter(const FFrontendQueryEntry& InEntry) const override;

	private:
		FName InputVertexTypeName;
	};

	class METASOUNDFRONTEND_API FFilterClassesByOutputVertexDataType : public IFrontendQueryFilterStep
	{
	public: 
		template<typename DataType>
		FFilterClassesByOutputVertexDataType()
		:	FFilterClassesByOutputVertexDataType(GetMetasoundDataTypeName<DataType>())
		{
		}

		FFilterClassesByOutputVertexDataType(const FName& InTypeName);

		bool Filter(const FFrontendQueryEntry& InEntry) const override;

	private:
		FName OutputVertexTypeName;
	};

	class METASOUNDFRONTEND_API FFilterClassesByClassName : public IFrontendQueryFilterStep
	{
	public: 
		FFilterClassesByClassName(const FMetasoundFrontendClassName& InClassName);

		bool Filter(const FFrontendQueryEntry& InEntry) const override;

	private:
		FMetasoundFrontendClassName ClassName;
	};

	class METASOUNDFRONTEND_API FFilterClassesByClassID : public IFrontendQueryFilterStep
	{
	public:
		FFilterClassesByClassID(const FGuid InClassID);

		bool Filter(const FFrontendQueryEntry& InEntry) const override;

	private:
		FGuid ClassID;
	};
}

