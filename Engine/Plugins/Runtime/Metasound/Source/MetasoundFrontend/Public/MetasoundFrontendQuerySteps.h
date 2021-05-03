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

	class METASOUNDFRONTEND_API FMapClassNameToMajorVersion : public IFrontendQueryMapStep
	{
	public:
		FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const override;
	};

	class METASOUNDFRONTEND_API FReduceClassesToHighestVersion : public IFrontendQueryReduceStep
	{
	public:
		void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry*>& InEntries, FReduceOutputView& OutResult) const override;
	};

	class METASOUNDFRONTEND_API FReduceClassesToMajorVersion : public IFrontendQueryReduceStep
	{
	public:
		FReduceClassesToMajorVersion(int32 InMajorVersion);

		void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry*>& InEntries, FReduceOutputView& OutResult) const override;

	private:
		int32 MajorVersion = -1;
	};

	class METASOUNDFRONTEND_API FSortClassesByVersion : public IFrontendQuerySortStep
	{
	public:
		bool Sort(const FFrontendQueryEntry& InEntryLHS, const FFrontendQueryEntry& InEntryRHS) const override;
	};
}
