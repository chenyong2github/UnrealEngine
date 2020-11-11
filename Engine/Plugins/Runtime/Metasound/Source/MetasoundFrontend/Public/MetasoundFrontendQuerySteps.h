// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendQuery.h"

namespace Metasound
{
	class METASOUNDFRONTEND_API FGenerateAllAvailableNodeClasses : public IFrontendQueryGenerateStep
	{
	public:

		void Generate(TArray<FFrontendQueryEntry>& OutEntries) const override;
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
}

