// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCachePolicy.h"

#include "Algo/Accumulate.h"
#include "Algo/BinarySearch.h"
#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "String/ParseTokens.h"
#include "Templates/Function.h"

namespace UE::DerivedData::Private { class FCacheRecordPolicyShared; }

namespace UE::DerivedData::Private
{

constexpr ANSICHAR CachePolicyDelimiter = ',';

struct FCachePolicyToText
{
	ECachePolicy Policy;
	FAnsiStringView Text;
};

const FCachePolicyToText CachePolicyToText[]
{
	// Flags with multiple bits are ordered by bit count to minimize token count in the text format.
	{ ECachePolicy::Default,       ANSITEXTVIEW("Default") },
	{ ECachePolicy::Remote,        ANSITEXTVIEW("Remote") },
	{ ECachePolicy::Local,         ANSITEXTVIEW("Local") },
	{ ECachePolicy::Store,         ANSITEXTVIEW("Store") },
	{ ECachePolicy::Query,         ANSITEXTVIEW("Query") },
	// Flags with only one bit can be in any order. Match the order in ECachePolicy.
	{ ECachePolicy::QueryLocal,    ANSITEXTVIEW("QueryLocal") },
	{ ECachePolicy::QueryRemote,   ANSITEXTVIEW("QueryRemote") },
	{ ECachePolicy::StoreLocal,    ANSITEXTVIEW("StoreLocal") },
	{ ECachePolicy::StoreRemote,   ANSITEXTVIEW("StoreRemote") },
	{ ECachePolicy::SkipMeta,      ANSITEXTVIEW("SkipMeta") },
	{ ECachePolicy::SkipData,      ANSITEXTVIEW("SkipData") },
	{ ECachePolicy::PartialRecord, ANSITEXTVIEW("PartialRecord") },
	{ ECachePolicy::KeepAlive,     ANSITEXTVIEW("KeepAlive") },
	// None must be last because it matches every policy.
	{ ECachePolicy::None,          ANSITEXTVIEW("None") },
};

constexpr ECachePolicy CachePolicyKnownFlags
	= ECachePolicy::Default
	| ECachePolicy::SkipMeta
	| ECachePolicy::SkipData
	| ECachePolicy::PartialRecord
	| ECachePolicy::KeepAlive;

template <typename CharType>
TStringBuilderBase<CharType>& CachePolicyToString(TStringBuilderBase<CharType>& Builder, ECachePolicy Policy)
{
	// Mask out unknown flags. None will be written if no flags are known.
	Policy &= CachePolicyKnownFlags;
	for (const FCachePolicyToText& Pair : CachePolicyToText)
	{
		if (EnumHasAllFlags(Policy, Pair.Policy))
		{
			EnumRemoveFlags(Policy, Pair.Policy);
			Builder << Pair.Text << CachePolicyDelimiter;
			if (Policy == ECachePolicy::None)
			{
				break;
			}
		}
	}
	Builder.RemoveSuffix(1);
	return Builder;
}

template <typename CharType>
ECachePolicy ParseCachePolicy(const TStringView<CharType> Text)
{
	checkf(!Text.IsEmpty(), TEXT("ParseCachePolicy requires a non-empty string."));
	ECachePolicy Policy = ECachePolicy::None;
	String::ParseTokens(StringCast<UTF8CHAR, 128>(Text.GetData(), Text.Len()), UTF8CHAR(CachePolicyDelimiter),
		[&Policy, Index = int32(0)](FUtf8StringView Token) mutable
		{
			const int32 EndIndex = Index;
			for (; Index < UE_ARRAY_COUNT(CachePolicyToText); ++Index)
			{
				if (CachePolicyToText[Index].Text == Token)
				{
					Policy |= CachePolicyToText[Index].Policy;
					++Index;
					return;
				}
			}
			for (Index = 0; Index < EndIndex; ++Index)
			{
				if (CachePolicyToText[Index].Text == Token)
				{
					Policy |= CachePolicyToText[Index].Policy;
					++Index;
					return;
				}
			}
		});
	return Policy;
}

} // UE::DerivedData::Private

namespace UE::DerivedData
{

FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, ECachePolicy Policy) { return Private::CachePolicyToString(Builder, Policy); }
FWideStringBuilderBase& operator<<(FWideStringBuilderBase& Builder, ECachePolicy Policy) { return Private::CachePolicyToString(Builder, Policy); }
FUtf8StringBuilderBase& operator<<(FUtf8StringBuilderBase& Builder, ECachePolicy Policy) { return Private::CachePolicyToString(Builder, Policy); }

ECachePolicy ParseCachePolicy(FAnsiStringView Text) { return Private::ParseCachePolicy(Text); }
ECachePolicy ParseCachePolicy(FWideStringView Text) { return Private::ParseCachePolicy(Text); }
ECachePolicy ParseCachePolicy(FUtf8StringView Text) { return Private::ParseCachePolicy(Text); }

class Private::FCacheRecordPolicyShared final : public Private::ICacheRecordPolicyShared
{
public:
	inline void AddRef() const final
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	inline void Release() const final
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

	inline void AddValuePolicy(const FCacheValuePolicy& Value) final
	{
		checkf(Value.Id.IsValid(), TEXT("Failed to add value policy because the ID is null."));
		const int32 Index = Algo::LowerBoundBy(Values, Value.Id, &FCacheValuePolicy::Id);
		checkf(!(Values.IsValidIndex(Index) && Values[Index].Id == Value.Id),
			TEXT("Failed to add value policy with ID %s because it has an existing value policy with that ID. ")
			TEXT("New: %s. Existing: %s."),
			*WriteToString<32>(Value.Id), *WriteToString<128>(Value.Policy), *WriteToString<128>(Values[Index].Policy));
		Values.Insert(Value, Index);
	}

	inline TConstArrayView<FCacheValuePolicy> GetValuePolicies() const final
	{
		return Values;
	}

private:
	TArray<FCacheValuePolicy, TInlineAllocator<14>> Values;
	mutable std::atomic<uint32> ReferenceCount{0};
};

ECachePolicy FCacheRecordPolicy::GetValuePolicy(const FValueId& Id) const
{
	if (Shared)
	{
		const TConstArrayView<FCacheValuePolicy> Values = Shared->GetValuePolicies();
		if (const int32 Index = Algo::BinarySearchBy(Values, Id, &FCacheValuePolicy::Id); Index != INDEX_NONE)
		{
			return Values[Index].Policy;
		}
	}
	return DefaultValuePolicy;
}

FCacheRecordPolicy FCacheRecordPolicy::Transform(const TFunctionRef<ECachePolicy (ECachePolicy)> Op) const
{
	if (IsUniform())
	{
		return Op(RecordPolicy);
	}

	FCacheRecordPolicyBuilder Builder(Op(GetBasePolicy()));
	for (const FCacheValuePolicy& Value : GetValuePolicies())
	{
		Builder.AddValuePolicy({Value.Id, Op(Value.Policy) & FCacheValuePolicy::PolicyMask});
	}
	return Builder.Build();
}

void FCacheRecordPolicy::Save(FCbWriter& Writer) const
{
	Writer.BeginObject();
	Writer.AddString("BasePolicy"_ASV, WriteToUtf8String<128>(GetBasePolicy()));
	if (!IsUniform())
	{
		Writer.BeginArray("ValuePolicies"_ASV);
		for (const FCacheValuePolicy& Value : GetValuePolicies())
		{
			Writer.BeginObject();
			Writer.AddObjectId("Id"_ASV, Value.Id);
			Writer.AddString("Policy"_ASV, WriteToUtf8String<128>(Value.Policy));
			Writer.EndObject();
		}
		Writer.EndArray();
	}
	Writer.EndObject();
}

FOptionalCacheRecordPolicy FCacheRecordPolicy::Load(const FCbObjectView Object)
{
	const FUtf8StringView BasePolicyText = Object["BasePolicy"_ASV].AsString();
	if (BasePolicyText.IsEmpty())
	{
		return {};
	}

	FCacheRecordPolicyBuilder Builder(ParseCachePolicy(BasePolicyText));
	for (const FCbFieldView Value : Object["ValuePolicies"_ASV])
	{
		const FValueId Id = Value["Id"_ASV].AsObjectId();
		const FUtf8StringView PolicyText = Value["Policy"_ASV].AsString();
		if (Id.IsNull() || PolicyText.IsEmpty())
		{
			return {};
		}
		const ECachePolicy Policy = ParseCachePolicy(PolicyText);
		if (EnumHasAnyFlags(Policy, ~FCacheValuePolicy::PolicyMask))
		{
			return {};
		}
		Builder.AddValuePolicy(Id, Policy);
	}
	return Builder.Build();
}

void FCacheRecordPolicyBuilder::AddValuePolicy(const FCacheValuePolicy& Value)
{
	checkf(!EnumHasAnyFlags(Value.Policy, ~Value.PolicyMask),
		TEXT("Value policy contains flags that only make sense on the record policy. Policy: %s"),
		*WriteToString<128>(Value.Policy));
	if (Value.Policy == (BasePolicy & Value.PolicyMask))
	{
		return;
	}
	if (!Shared)
	{
		Shared = new Private::FCacheRecordPolicyShared;
	}
	Shared->AddValuePolicy(Value);
}

FCacheRecordPolicy FCacheRecordPolicyBuilder::Build()
{
	FCacheRecordPolicy Policy(BasePolicy);
	if (Shared)
	{
		const auto Add = [](const ECachePolicy A, const ECachePolicy B)
		{
			return ((A | B) & ~ECachePolicy::SkipData) | ((A & B) & ECachePolicy::SkipData);
		};
		const TConstArrayView<FCacheValuePolicy> Values = Shared->GetValuePolicies();
		Policy.RecordPolicy = Algo::TransformAccumulate(Values, &FCacheValuePolicy::Policy, BasePolicy, Add);
		Policy.Shared = MoveTemp(Shared);
	}
	return Policy;
}

} // UE::DerivedData
