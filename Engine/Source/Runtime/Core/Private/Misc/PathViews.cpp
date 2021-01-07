// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/PathViews.h"

#include "Algo/Find.h"
#include "Algo/FindLast.h"
#include "Containers/UnrealString.h"
#include "Containers/StringView.h"
#include "Misc/Char.h"
#include "Misc/StringBuilder.h"
#include "String/ParseTokens.h"
#include "Templates/Function.h"

namespace UE4PathViews_Private
{
	static bool IsSlashOrBackslash(TCHAR C) { return C == TEXT('/') || C == TEXT('\\'); }
	static bool IsNotSlashOrBackslash(TCHAR C) { return C != TEXT('/') && C != TEXT('\\'); }

	static bool IsSlashOrBackslashOrPeriod(TCHAR C) { return C == TEXT('/') || C == TEXT('\\') || C == TEXT('.'); }

	static bool HasRedundantTerminatingSeparator(FStringView A)
	{
		if (A.Len() <= 2)
		{
			if (A.Len() <= 1)
			{
				// "", "c", or "/", All of these are either not slash terminating or not redundant
				return false;
			}
			else if (!IsSlashOrBackslash(A[1]))
			{
				// "/c" or "cd"
				return false;
			}
			else if (IsSlashOrBackslash(A[0]))
			{
				// "//"
				return false;
			}
			else if (A[0] == ':')
			{
				// ":/", which is an invalid path, and we arbitrarily decide its terminating slash is not redundant
				return false;
			}
			else
			{
				// "c/"
				return true;
			}
		}
		else if (!IsSlashOrBackslash(A[A.Len() - 1]))
		{
			// "/Some/Path"
			return false;
		}
		else if (A[A.Len() - 2] == ':')
		{
			// "Volume:/",  "Volume:/Some/Path:/"
			// The first case is the root dir of the volume and is not redundant
			// The second case is an invalid path (at most one colon), and we arbitrarily decide its terminating slash is not redundant
			return false;
		}
		else
		{
			// /Some/Path/
			return true;
		}
	}

	static bool StringEqualsIgnoreCaseIgnoreSeparator(FStringView A, FStringView B)
	{
		if (A.Len() != B.Len())
		{
			return false;
		}
		const TCHAR* AData = A.GetData();
		const TCHAR* AEnd = AData + A.Len();
		const TCHAR* BData = B.GetData();
		for (; AData < AEnd; ++AData, ++BData)
		{
			TCHAR AChar = FChar::ToUpper(*AData);
			TCHAR BChar = FChar::ToUpper(*BData);
			if (IsSlashOrBackslash(AChar))
			{
				if (!IsSlashOrBackslash(BChar))
				{
					return false;
				}
			}
			else
			{
				if (AChar != BChar)
				{
					return false;
				}
			}
		}
		return true;
	}

	static bool StringLessIgnoreCaseIgnoreSeparator(FStringView A, FStringView B)
	{
		const TCHAR* AData = A.GetData();
		const TCHAR* AEnd = AData + A.Len();
		const TCHAR* BData = B.GetData();
		const TCHAR* BEnd = BData + B.Len();
		for (; AData < AEnd && BData < BEnd; ++AData, ++BData)
		{
			TCHAR AChar = FChar::ToUpper(*AData);
			TCHAR BChar = FChar::ToUpper(*BData);
			if (IsSlashOrBackslash(AChar))
			{
				if (!IsSlashOrBackslash(BChar))
				{
					return AChar < BChar;
				}
			}
			else
			{
				if (AChar != BChar)
				{
					return AChar < BChar;
				}
			}
		}
		if (BData < BEnd)
		{
			// B is longer than A, so A is a prefix of B. A string is greater than any of its prefixes
			return true;
		}
		return false;
	}
}

FStringView FPathViews::GetCleanFilename(const FStringView& InPath)
{
	if (const TCHAR* StartPos = Algo::FindLastByPredicate(InPath, UE4PathViews_Private::IsSlashOrBackslash))
	{
		return InPath.RightChop(UE_PTRDIFF_TO_INT32(StartPos - InPath.GetData() + 1));
	}
	return InPath;
}

FStringView FPathViews::GetBaseFilename(const FStringView& InPath)
{
	const FStringView CleanPath = GetCleanFilename(InPath);
	return CleanPath.LeftChop(GetExtension(CleanPath, /*bIncludeDot*/ true).Len());
}

FStringView FPathViews::GetBaseFilenameWithPath(const FStringView& InPath)
{
	return InPath.LeftChop(GetExtension(InPath, /*bIncludeDot*/ true).Len());
}

FStringView FPathViews::GetBaseFilename(const FStringView& InPath, bool bRemovePath)
{
	return bRemovePath ? GetBaseFilename(InPath) : GetBaseFilenameWithPath(InPath);
}

FStringView FPathViews::GetPath(const FStringView& InPath)
{
	if (const TCHAR* EndPos = Algo::FindLastByPredicate(InPath, UE4PathViews_Private::IsSlashOrBackslash))
	{
		return InPath.Left((FStringView::SizeType)(EndPos - InPath.GetData()));
	}
	return FStringView();
}

FStringView FPathViews::GetExtension(const FStringView& InPath, bool bIncludeDot)
{
	if (const TCHAR* Dot = Algo::FindLast(GetCleanFilename(InPath), TEXT('.')))
	{
		const TCHAR* Extension = bIncludeDot ? Dot : Dot + 1;
		return FStringView(Extension, (FStringView::SizeType)(InPath.GetData() + InPath.Len() - Extension));
	}
	return FStringView();
}

FStringView FPathViews::GetPathLeaf(const FStringView& InPath)
{
	if (const TCHAR* EndPos = Algo::FindLastByPredicate(InPath, UE4PathViews_Private::IsNotSlashOrBackslash))
	{
		++EndPos;
		return GetCleanFilename(InPath.Left((FStringView::SizeType)(EndPos - InPath.GetData())));
	}
	return FStringView();
}

bool FPathViews::IsPathLeaf(FStringView InPath)
{
	using namespace UE4PathViews_Private;

	const TCHAR* FirstSlash = Algo::FindByPredicate(InPath, IsSlashOrBackslash);
	if (FirstSlash == nullptr)
	{
		return true;
	}
	if (Algo::FindByPredicate(InPath.RightChop((FStringView::SizeType)(FirstSlash - InPath.GetData())), IsNotSlashOrBackslash) == nullptr)
	{
		// The first slash is after the last non-slash
		// This means it is either required for e.g. // or D:/ or /, or it is a redundant terminating slash D:/Foo/
		// In all of those cases the token is still considered a (possibly unnormalized) leaf
		return true;
	}
	return false;

}

void FPathViews::IterateComponents(FStringView InPath, TFunctionRef<void(FStringView)> ComponentVisitor)
{
	UE::String::ParseTokensMultiple(InPath, { TEXT('/'), TEXT('\\') }, ComponentVisitor);
}

void FPathViews::Split(const FStringView& InPath, FStringView& OutPath, FStringView& OutName, FStringView& OutExt)
{
	const FStringView CleanName = GetCleanFilename(InPath);
	const TCHAR* DotPos = Algo::FindLast(CleanName, TEXT('.'));
	const FStringView::SizeType NameLen = DotPos ? (FStringView::SizeType)(DotPos - CleanName.GetData()) : CleanName.Len();
	OutPath = InPath.LeftChop(CleanName.Len() + 1);
	OutName = CleanName.Left(NameLen);
	OutExt = CleanName.RightChop(NameLen + 1);
}

void FPathViews::Append(FStringBuilderBase& Builder, const FStringView& Suffix)
{
	if (Builder.Len() > 0 && !UE4PathViews_Private::IsSlashOrBackslash(Builder.LastChar()))
	{
		Builder.Append(TEXT('/'));
	}
	Builder.Append(Suffix);
}

FString FPathViews::ChangeExtension(const FStringView& InPath, const FStringView& InNewExtension)
{
	// Make sure the period we found was actually for a file extension and not part of the file path.
	const TCHAR* PathEndPos = Algo::FindLastByPredicate(InPath, UE4PathViews_Private::IsSlashOrBackslashOrPeriod);
	if (PathEndPos != nullptr && *PathEndPos == TEXT('.'))
	{
		const FStringView::SizeType Pos = FStringView::SizeType(PathEndPos - InPath.GetData());
		const FStringView FileWithoutExtension = InPath.Left(Pos);

		if (!InNewExtension.IsEmpty() && !InNewExtension.StartsWith('.'))
		{
			// The new extension lacks a period so we need to add it ourselves.
			FString Result(FileWithoutExtension, InNewExtension.Len() + 1);
			Result += '.';
			Result += InNewExtension;

			return Result;
		}
		else
		{
			FString Result(FileWithoutExtension, InNewExtension.Len());
			Result += InNewExtension;

			return Result;
		}
	}
	else
	{
		return FString(InPath);
	}
}

bool FPathViews::IsSeparator(TCHAR c)
{
	return UE4PathViews_Private::IsSlashOrBackslash(c);
}

bool FPathViews::Equals(FStringView A, FStringView B)
{
	using namespace UE4PathViews_Private;

	while (HasRedundantTerminatingSeparator(A))
	{
		A.LeftChopInline(1);
	}
	while (HasRedundantTerminatingSeparator(B))
	{
		B.LeftChopInline(1);
	}
	return StringEqualsIgnoreCaseIgnoreSeparator(A, B);
}

bool FPathViews::Less(FStringView A, FStringView B)
{
	using namespace UE4PathViews_Private;

	while (HasRedundantTerminatingSeparator(A))
	{
		A.LeftChopInline(1);
	}
	while (HasRedundantTerminatingSeparator(B))
	{
		B.LeftChopInline(1);
	}
	return StringLessIgnoreCaseIgnoreSeparator(A, B);
}

bool FPathViews::TryMakeChildPathRelativeTo(FStringView Child, FStringView Parent, FStringView& OutRelPath)
{
	using namespace UE4PathViews_Private;

	while (HasRedundantTerminatingSeparator(Parent))
	{
		Parent.LeftChopInline(1);
		// Note that Parent can not be the empty string, because HasRedundantTerminatingSeparator("/") is false
	}
	if (Parent.Len() == 0)
	{
		// We arbitrarily define an empty string as never the parent of any directory
		OutRelPath.Reset();
		return false;
	}
	if (Child.Len() < Parent.Len())
	{
		// A shorter path can not be a child path
		OutRelPath.Reset();
		return false;
	}

	check(Child.Len() > 0 && Parent.Len() > 0);

	if (!StringEqualsIgnoreCaseIgnoreSeparator(Parent, Child.SubStr(0, Parent.Len())))
	{
		// Child paths match their parent paths exactly up to the parent's length
		OutRelPath.Reset();
		return false;
	}
	if (Child.Len() == Parent.Len())
	{
		// The child is equal to the parent, which we define as being a child path
		OutRelPath.Reset();
		return true;
	}

	check(Child.Len() >= Parent.Len() + 1);

	if (IsSlashOrBackslash(Parent[Parent.Len() - 1]))
	{
		// Parent is the special case "//", "Volume:/", or "/" (these are the only cases that end in / after HasRedundantTerminatingSeparator)
		if (Parent.Len() == 1 && IsSlashOrBackslash(Child[1]))
		{
			// Parent is "/", Child is "//SomethingOrNothing". // is not a child of /, so this is not a child
			OutRelPath.Reset();
			return false;
		}
		// Since child starts with parent, including the terminating /, child is a child path of parent
		OutRelPath = Child.RightChop(Parent.Len()); // Chop all of the parent including the terminating slash
	}
	else
	{
		if (!IsSlashOrBackslash(Child[Parent.Len()]))
		{
			// Child is in a different directory that happens to have the parent's final directory component as a prefix
			// e.g. "Volume:/Some/Path", "Volume:/Some/PathOther"
			OutRelPath.Reset();
			return false;
		}
		// Parent   = Volume:/Some/Path
		// Child    = Volume:/Some/Path/Child1/Child2/etc
		// or Child = Volume:/Some/Path/
		// or Child = Volume:/Some/Path/////
		OutRelPath = Child.RightChop(Parent.Len() + 1); // Chop the Parent and the slash in the child that follows the parent
	}
	// If there were invalid duplicate slashes after the parent path, remove them since starting with a slash
	// is not a valid relpath
	while (OutRelPath.Len() > 0 && IsSlashOrBackslash(OutRelPath[0]))
	{
		OutRelPath.RightChopInline(1);
	}
	return true;
}

bool FPathViews::IsParentPathOf(FStringView Parent, FStringView Child)
{
	FStringView RelPath;
	return TryMakeChildPathRelativeTo(Child, Parent, RelPath);
}

bool FPathViews::IsRelativePath(FStringView InPath)
{
	using namespace UE4PathViews_Private;

	if (const TCHAR* EndPos = Algo::FindByPredicate(InPath, IsSlashOrBackslash))
	{
		FStringView::SizeType FirstLen = static_cast<FStringView::SizeType>(EndPos - InPath.GetData());
		if (FirstLen == 0)
		{
			// Path starts with /; it may be either /Foo or //Foo
			return false;
		}
		else if (InPath[FirstLen - 1] == ':')
		{
			// InPath == Volume:/SomethingOrNothing
			return false;
		}
		else
		{
			// InPath == RelativeComponent/SomethingOrNothing
			return true;
		}
	}
	else
	{
		// InPath == SomethingOrNothing
		return true;
	}
}

void FPathViews::SplitFirstComponent(FStringView InPath, FStringView& OutFirstComponent, FStringView& OutRemainder)
{
	using namespace UE4PathViews_Private;

	if (const TCHAR* EndPos = Algo::FindByPredicate(InPath, IsSlashOrBackslash))
	{
		FStringView::SizeType FirstLen = static_cast<FStringView::SizeType>(EndPos - InPath.GetData());
		if (FirstLen == 0)
		{
			// Path starts with /; it may be either /Foo or //Foo
			if (InPath.Len() == 1)
			{
				// InPath == /
				// FirstComponent = /
				// Remainder = <Empty>
				OutFirstComponent = InPath;
				OutRemainder.Reset();
			}
			else if (IsSlashOrBackslash(InPath[1]))
			{
				// InPath == //SomethingOrNothing
				// FirstComponent = //
				// Remainder = SomethingOrNothing
				OutFirstComponent = InPath.Left(2);
				OutRemainder = InPath.RightChop(2);
			}
			else
			{
				// InPath == /SomethingOrNothing
				// FirstComponent = /
				// Remainder = SomethingOrNothing
				OutFirstComponent = InPath.Left(1);
				OutRemainder = InPath.RightChop(1);
			}
		}
		else if (InPath[FirstLen - 1] == ':')
		{
			// InPath == Volume:/SomethingOrNothing
			// FirstComponent = Volume:/
			// Remainder = SomethingOrNothing
			OutFirstComponent = InPath.Left(FirstLen + 1);
			OutRemainder = InPath.RightChop(FirstLen + 1);
		}
		else
		{
			// InPath == RelativeComponent/SomethingOrNothing
			// FirstComponent = RelativeComponent
			// Remainder = SomethingOrNothing
			OutFirstComponent = InPath.Left(FirstLen);
			OutRemainder = InPath.RightChop(FirstLen + 1);
		}
	}
	else
	{
		// InPath == SomethingOrNothing
		// FirstComponent = SomethingOrNothing
		// Remainder = <Empty>
		OutFirstComponent = InPath;
		OutRemainder.Reset();
	}
	// If there were invalid duplicate slashes after the first component, remove them since starting with a slash
	// is not a valid relpath
	while (OutRemainder.Len() > 0 && IsSlashOrBackslash(OutRemainder[0]))
	{
		OutRemainder.RightChopInline(1);
	}
}

void FPathViews::AppendPath(FStringBuilderBase& InOutPath, FStringView AppendPath)
{
	using namespace UE4PathViews_Private;

	if (AppendPath.Len() == 0)
	{
	}
	else if (IsRelativePath(AppendPath))
	{
		if (InOutPath.Len() > 0 && !IsSlashOrBackslash(InOutPath.LastChar()))
		{
			InOutPath << TEXT('/');
		}
		InOutPath << AppendPath;
	}
	else
	{
		InOutPath.Reset();
		InOutPath << AppendPath;
	}
}