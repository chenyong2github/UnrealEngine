// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildAction.h"

#include "Algo/AllOf.h"
#include "Containers/Map.h"
#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "DerivedDataBuildPrivate.h"
#include "Misc/Guid.h"
#include "Misc/TVariant.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Templates/Tuple.h"
#include <atomic>

namespace UE::DerivedData::Private
{

class FBuildActionBuilderInternal final : public IBuildActionBuilderInternal
{
public:
	inline FBuildActionBuilderInternal(
		FStringView InName,
		FStringView InFunction,
		const FGuid& InFunctionVersion,
		const FGuid& InBuildSystemVersion)
		: Name(InName)
		, Function(InFunction)
		, FunctionVersion(InFunctionVersion)
		, BuildSystemVersion(InBuildSystemVersion)
	{
		checkf(!Name.IsEmpty(), TEXT("A build action requires a non-empty name."));
		checkf(!Function.IsEmpty(), TEXT("A build action requires a non-empty function name for build of '%s'."), *Name);
		checkf(Algo::AllOf(Function, FChar::IsAlnum), TEXT("Action requires an alphanumeric function name for ")
			TEXT("build of '%s' by %s."), *Name, *Function);
	}

	~FBuildActionBuilderInternal() final = default;

	void AddConstant(FStringView Key, const FCbObject& Value) final
	{
		Add(Key, Value);
	}

	void AddInput(FStringView Key, const FIoHash& RawHash, uint64 RawSize) final
	{
		Add(Key, MakeTuple(RawHash, RawSize));
	}

	FBuildAction Build() final;

	using InputType = TVariant<FCbObject, TTuple<FIoHash, uint64>>;

	FString Name;
	FString Function;
	FGuid FunctionVersion;
	FGuid BuildSystemVersion;
	TMap<FString, InputType> Inputs;

private:
	template <typename ValueType>
	inline void Add(FStringView Key, const ValueType& Value)
	{
		const uint32 KeyHash = GetTypeHash(Key);
		checkf(!Key.IsEmpty(), TEXT("Empty key used in action for build of '%s' by %s."), *Name, *Function);
		checkf(!Inputs.ContainsByHash(KeyHash, Key), TEXT("Duplicate key '%.*s' used in action for "),
			TEXT("build of '%s' by %s."), Key.Len(), Key.GetData(), *Name, *Function);
		Inputs.EmplaceByHash(KeyHash, Key, InputType(TInPlaceType<ValueType>(), Value));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FBuildActionInternal final : public IBuildActionInternal
{
public:
	explicit FBuildActionInternal(FBuildActionBuilderInternal&& ActionBuilder);
	explicit FBuildActionInternal(FStringView Name, FCbObject&& Action);

	~FBuildActionInternal() final = default;

	const FBuildActionKey& GetKey() const final { return Key; }

	FStringView GetName() const final { return Name; }
	FStringView GetFunction() const final { return Function; }
	const FGuid& GetFunctionVersion() const final { return FunctionVersion; }
	const FGuid& GetBuildSystemVersion() const final { return BuildSystemVersion; }

	void IterateConstants(TFunctionRef<void (FStringView Key, FCbObject&& Value)> Visitor) const final;
	void IterateInputs(TFunctionRef<void (FStringView Key, const FIoHash& RawHash, uint64 RawSize)> Visitor) const final;

	void Save(FCbWriter& Writer) const final;

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

private:
	mutable std::atomic<uint32> ReferenceCount{0};
	FString Name;
	FString Function;
	FGuid FunctionVersion;
	FGuid BuildSystemVersion;
	FBuildActionKey Key;
	FCbObject Action;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildActionInternal::FBuildActionInternal(FBuildActionBuilderInternal&& ActionBuilder)
	: Name(MoveTemp(ActionBuilder.Name))
	, Function(MoveTemp(ActionBuilder.Function))
	, FunctionVersion(MoveTemp(ActionBuilder.FunctionVersion))
	, BuildSystemVersion(MoveTemp(ActionBuilder.BuildSystemVersion))
{
	ActionBuilder.Inputs.KeySort(TLess<>());

	bool bHasConstants = false;
	bool bHasInputs = false;

	for (const TPair<FString, FBuildActionBuilderInternal::InputType>& Pair : ActionBuilder.Inputs)
	{
		if (Pair.Value.IsType<FCbObject>())
		{
			bHasConstants = true;
		}
		else if (Pair.Value.IsType<TTuple<FIoHash, uint64>>())
		{
			bHasInputs = true;
		}
	}

	TCbWriter<2048> Writer;
	Writer.BeginObject();
	Writer.AddString("Function"_ASV, Function);
	Writer.AddUuid("FunctionVersion"_ASV, FunctionVersion);
	Writer.AddUuid("BuildSystemVersion"_ASV, BuildSystemVersion);

	if (bHasConstants)
	{
		Writer.BeginObject("Constants"_ASV);
		for (const TPair<FString, FBuildActionBuilderInternal::InputType>& Pair : ActionBuilder.Inputs)
		{
			if (Pair.Value.IsType<FCbObject>())
			{
				Writer.AddObject(FTCHARToUTF8(Pair.Key), Pair.Value.Get<FCbObject>());
			}
		}
		Writer.EndObject();
	}

	if (bHasInputs)
	{
		Writer.BeginObject("Inputs"_ASV);
		for (const TPair<FString, FBuildActionBuilderInternal::InputType>& Pair : ActionBuilder.Inputs)
		{
			if (Pair.Value.IsType<TTuple<FIoHash, uint64>>())
			{
				const TTuple<FIoHash, uint64>& Input = Pair.Value.Get<TTuple<FIoHash, uint64>>();
				Writer.BeginObject(FTCHARToUTF8(Pair.Key));
				Writer.AddBinaryAttachment("RawHash"_ASV, Input.Get<FIoHash>());
				Writer.AddInteger("RawSize"_ASV, Input.Get<uint64>());
				Writer.EndObject();
			}
		}
		Writer.EndObject();
	}

	Writer.EndObject();
	Action = Writer.Save().AsObject();
	Key = FBuildActionKey{Action.GetHash()};
}

FBuildActionInternal::FBuildActionInternal(FStringView InName, FCbObject&& InAction)
	: Name(InName)
	, Function(InAction.FindView("Function"_ASV).AsString())
	, FunctionVersion(InAction.FindView("FunctionVersion"_ASV).AsUuid())
	, BuildSystemVersion(InAction.FindView("BuildSystemVersion"_ASV).AsUuid())
	, Action(MoveTemp(InAction))
{
	checkf(!InName.IsEmpty(), TEXT("A build action requires a non-empty name."));
	Action.MakeOwned();
	if (const bool bIsValid = Action && Algo::AllOf(Function, FChar::IsAlnum))
	{
		Key = FBuildActionKey{Action.GetHash()};
	}
}

void FBuildActionInternal::IterateConstants(TFunctionRef<void (FStringView Key, FCbObject&& Value)> Visitor) const
{
	for (FCbField Field : Action["Constants"_ASV])
	{
		Visitor(FUTF8ToTCHAR(Field.GetName()), Field.AsObject());
	}
}

void FBuildActionInternal::IterateInputs(TFunctionRef<void (FStringView Key, const FIoHash& RawHash, uint64 RawSize)> Visitor) const
{
	for (FCbFieldView Field : Action.FindView("Inputs"_ASV))
	{
		FCbObjectView Input = Field.AsObjectView();
		Visitor(FUTF8ToTCHAR(Field.GetName()), Input["RawHash"_ASV].AsHash(), Input["RawSize"_ASV].AsUInt64());
	}
}

void FBuildActionInternal::Save(FCbWriter& Writer) const
{
	Writer.AddObject(Action);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildAction FBuildActionBuilderInternal::Build()
{
	return CreateBuildAction(new FBuildActionInternal(MoveTemp(*this)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildAction CreateBuildAction(IBuildActionInternal* Action)
{
	return FBuildAction(Action);
}

FBuildActionBuilder CreateBuildActionBuilder(IBuildActionBuilderInternal* ActionBuilder)
{
	return FBuildActionBuilder(ActionBuilder);
}

FBuildActionBuilder CreateBuildAction(FStringView Name, FStringView Function, const FGuid& FunctionVersion, const FGuid& BuildSystemVersion)
{
	return CreateBuildActionBuilder(new FBuildActionBuilderInternal(Name, Function, FunctionVersion, BuildSystemVersion));
}

FBuildAction LoadBuildAction(FStringView Name, FCbObject&& Action)
{
	return CreateBuildAction(new FBuildActionInternal(Name, MoveTemp(Action)));
}

} // UE::DerivedData::Private
