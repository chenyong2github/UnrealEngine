// Copyright Epic Games, Inc. All Rights Reserved.

FDebugName::FDebugName()
	: Name()
	, Number(NAME_NO_NUMBER_INTERNAL)
{
}

FDebugName::FDebugName(FName InName)
	: Name(InName)
	, Number(NAME_NO_NUMBER_INTERNAL)
{
}

FDebugName::FDebugName(FName InName, int32 InNumber)
	: Name(InName)
	, Number(InNumber)
{
}

FDebugName::FDebugName(FMemoryImageName InName, int32 InNumber)
	: Name(InName)
	, Number(InNumber)
{
}

FDebugName& FDebugName::operator=(FName Other)
{
	Name = FMemoryImageName(Other);
	Number = NAME_NO_NUMBER_INTERNAL;
	return *this;
}

FString FDebugName::ToString() const
{
	FString Out;
	FName(Name).AppendString(Out);
	if (Number != NAME_NO_NUMBER_INTERNAL)
	{
		Out.Appendf(TEXT("_%u"), Number);
	}
	return Out;
}

void FDebugName::AppendString(FStringBuilderBase& Builder) const
{
	FName(Name).AppendString(Builder);
	if (Number != NAME_NO_NUMBER_INTERNAL)
	{
		Builder << '_' << Number;
	}
}
