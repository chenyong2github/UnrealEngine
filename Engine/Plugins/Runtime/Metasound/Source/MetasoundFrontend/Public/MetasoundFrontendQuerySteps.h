// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendQuery.h"

namespace Metasound
{
	/** Streams node classes that have been newly registered or unregistered since last call to Stream()
	 */
	class METASOUNDFRONTEND_API FNodeClassRegistrationEvents : public IFrontendQuerySource
	{
	public:
		FNodeClassRegistrationEvents();
		void Stream(TArray<FFrontendQueryEntry>& OutEntries) override;
		void Reset() override;

	private:
		Frontend::FRegistryTransactionID CurrentTransactionID;
	};

	/** Partitions node registration events by their node registration keys. */
	class METASOUNDFRONTEND_API FMapRegistrationEventsToNodeRegistryKeys : public IFrontendQueryMapStep
	{
	public:
		FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const override;
	};

	/** Reduces registration events mapped to the same key by inspecting their add/remove state in
	 * order to determine their final state. If an item has been added more than it has been removed,
	 * then it is added to the output. Otherwise, it is omitted. */
	class METASOUNDFRONTEND_API FReduceRegistrationEventsToCurrentStatus: public IFrontendQueryReduceStep
	{
	public:
		void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry * const>& InEntries, FReduceOutputView& OutResult) const override;
	};

	/** Transforms a registration event into a FMetasoundFrontendClass. */
	class METASOUNDFRONTEND_API FTransformRegistrationEventsToClasses : public IFrontendQueryTransformStep
	{
	public:
		virtual void Transform(FFrontendQueryEntry::FValue& InValue) const override;
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

	class METASOUNDFRONTEND_API FMapToFullClassName : public IFrontendQueryMapStep
	{
	public:
		FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const override;
	};

	class METASOUNDFRONTEND_API FReduceClassesToHighestVersion : public IFrontendQueryReduceStep
	{
	public:
		void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry * const>& InEntries, FReduceOutputView& OutResult) const override;
	};

	class METASOUNDFRONTEND_API FReduceClassesToMajorVersion : public IFrontendQueryReduceStep
	{
	public:
		FReduceClassesToMajorVersion(int32 InMajorVersion);

		void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry * const>& InEntries, FReduceOutputView& OutResult) const override;

	private:
		int32 MajorVersion = -1;
	};

	class METASOUNDFRONTEND_API FSortClassesByVersion : public IFrontendQuerySortStep
	{
	public:
		bool Sort(const FFrontendQueryEntry& InEntryLHS, const FFrontendQueryEntry& InEntryRHS) const override;
	};
}
