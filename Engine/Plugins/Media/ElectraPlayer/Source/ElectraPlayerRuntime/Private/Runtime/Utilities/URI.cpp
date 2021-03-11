// Copyright Epic Games, Inc. All Rights Reserved.

// Unittests are currently not present within perforce but can be obtained from:
// https://github.ol.epicgames.net/Eric-Schneller/urlparser/blob/master/parser_test.cpp

#include "Utilities/URI.h"
#include "Utilities/StringHelpers.h"

namespace Electra
{
    namespace Utilities
    {

        const FString FURI::Format() const
        {
			static const FString kTextColon(TEXT(":"));
			static const FString kTextQuestionMark(TEXT("?"));
			static const FString kTextHash(TEXT("#"));
			static const FString kTextDoubleSlash(TEXT("//"));
            FString Result;

            if (!Scheme.IsEmpty())
            {
				Result += Scheme + kTextColon;
            }

            if (HasAuthority())
            {
                Result += kTextDoubleSlash;
                if (HasUserInfo())
                {
                    Result += UserInfo.Username;
                    if (!UserInfo.Password.IsEmpty())
                    {
                        Result += kTextColon + UserInfo.Password;
                    }
					Result.AppendChar(TCHAR('@'));
                }

                Result += Host;
            }

            Result += Opaque;
            Result += Path;

            if (HasQuery)
            {
                Result += kTextQuestionMark + QueryString;
            }

            if (HasFragment)
            {
                Result += kTextHash + Fragment;
            }

            return Result;
        }

        const FURI FURI::Resolve(const FURI& InUri) const
        {
            FURI Target = *this;

            if (!InUri.Scheme.IsEmpty())
            {
                Target.Host = InUri.Host;
                Target.Scheme = InUri.Scheme;
                Target.UserInfo = InUri.UserInfo;
                Target.Path = CleanPath(InUri.Path);
                Target.Opaque = InUri.Opaque;
                Target.QueryString = InUri.QueryString;
                Target.HasQuery = InUri.HasQuery;
            }
            else
            {
                if (InUri.HasAuthority())
                {
                    Target.UserInfo = InUri.UserInfo;
                    Target.Host = InUri.Host;
                    Target.QueryString = InUri.QueryString;
                    Target.HasQuery = InUri.HasQuery;
                    Target.Path = CleanPath(InUri.Path);
                }
                else
                {
                    if (InUri.Path.IsEmpty())
                    {
                        if (InUri.HasQuery)
                        {
                            Target.QueryString = InUri.QueryString;
                            Target.HasQuery = true;
                        }
                    }
                    else
                    {
						int32 FoundIndex;
						InUri.Path.FindChar(TCHAR('/'), FoundIndex);
                        if (FoundIndex == 0)
                        {
                            Target.Path = CleanPath(InUri.Path);
                        }
                        else
                        {
                            if (HasAuthority() && Path.IsEmpty())
                            {
								static const FString kTextSlash(TEXT("/"));
                                Target.Path = CleanPath(kTextSlash + InUri.Path);
                            }
                            else
                            {
                                Target.Path = CleanPath(BasePath(Path) + InUri.Path);
                            }
                        }
                        Target.QueryString = InUri.QueryString;
                        Target.HasQuery = InUri.HasQuery;
                    }
                }
            }

            Target.HasFragment = InUri.HasFragment;
            Target.Fragment = InUri.Fragment;

            return Target;
        }


        FString parseScheme(StringHelpers::FStringIterator& it, FURIParseError& OutError)
        {
            FString scheme;
            int start = 0;

            while (it)
            {
                if ((TCHAR('a') <= *it && *it <= TCHAR('z')) || (TCHAR('A') <= *it && *it <= TCHAR('Z')))
                {
                }
                else if ((TCHAR('0') <= *it && *it <= TCHAR('9')) || *it == TCHAR('-') || *it == TCHAR('+') || *it == TCHAR('.'))
                {
                    if (start == 0)
                    {
                        break;
                    }
                }
                else if (*it == TCHAR(':'))
                {
                    if (start == 0)
                    {
                        OutError = FURIParseError::InvalidScheme;
                    }

                    ++it;
                    return scheme;
                }
                else
                {
                    break;
                }
				scheme.AppendChar(FChar::ToLower(*it));
                ++start;
                ++it;
            }

            // Scheme is only found if there's a ":" and it's valid.
            it -= start;
            return TEXT("");
        }

        /**
         * Splits a string using it's iterators based on the provided Split character at the first time it occurs. The
         * first part of the string is passed into OutBefore, the second into OutAfter. The Split character doesn't
         * occur in neither.
         * @param it Will be iterated until end is reached.
         * @param end
         * @param OutBefore
         * @param OutAfter
         * @param Split The character used for splitting.
         * @return Identifies if the Split character was found. This is necessary in case OutAfter is empty.
         */
        bool splitChar(StringHelpers::FStringIterator& it, FString& OutBefore, FString& OutAfter, const TCHAR Split)
        {
            bool bSplitFound = false;

            while (it)
            {
                if (*it == Split && !bSplitFound)
                {
                    bSplitFound = true;
                    ++it;
                    continue;
                }

                if (bSplitFound)
                {
                    OutAfter += *it;
                }
                else
                {
                    OutBefore += *it;
                }

                ++it;
            }

            return bSplitFound;
        }

        FURIParseError parseAuthority(StringHelpers::FStringIterator& it, FString& OutHost, FURIUserInfo& OutInfo)
        {
            FString Part;
            FString Info;

            while (it)
            {
                if (*it == TCHAR('/'))
                {
                    break;
                }

                if (*it == TCHAR('@'))
                {
                    Info = Part;
                    Part = TEXT("");
                    ++it;
                    continue;
                }

                Part += *it;
                ++it;
            }

            OutHost = Part;

            if (!Info.IsEmpty())
            {
                StringHelpers::FStringIterator InfoIt(Info);
                splitChar(InfoIt, OutInfo.Username, OutInfo.Password, TCHAR(':'));
            }

            return FURIParseError::None;
        }

        FURI FURI::Parse(const FString& InUri)
        {
			StringHelpers::FStringIterator it(InUri);
            FURI OutUrl;

            FString beforeFragment;
            OutUrl.HasFragment = splitChar(it, beforeFragment, OutUrl.Fragment, TCHAR('#'));
            
			StringHelpers::FStringIterator it2(beforeFragment);
            OutUrl.Scheme = parseScheme(it2, OutUrl.ParseError);
            if (OutUrl.ParseError != FURIParseError::None)
            {
                return OutUrl;
            }

            FString rest;
            OutUrl.HasQuery = splitChar(it2, rest, OutUrl.QueryString, TCHAR('?'));

			StringHelpers::FStringIterator it3(rest);
            if (it3 && *it3 != TCHAR('/'))
            {
                if (!OutUrl.Scheme.IsEmpty())
                {
                    OutUrl.Opaque = rest;
                    return OutUrl;
                }

				int32 colon, slash;
				rest.FindChar(TCHAR(':'), colon);
				rest.FindChar(TCHAR('/'), slash);
                if (colon != INDEX_NONE && (slash == INDEX_NONE || colon < slash))
                {
                    OutUrl.ParseError = FURIParseError::PathSegmentContainsColon;
                    return OutUrl;
                }
            }

            if ((!OutUrl.Scheme.IsEmpty() || rest.Find(TEXT("///")) != 0) && rest.Find(TEXT("//")) == 0)
            {
                it3 += 2;
                OutUrl.ParseError = parseAuthority(it3, OutUrl.Host, OutUrl.UserInfo);

                if (OutUrl.ParseError != FURIParseError::None)
                {
                    return OutUrl;
                }
            }

            while (it3)
            {
                OutUrl.Path += *it3;
                ++it3;
            }

            return OutUrl;
        }

        FString FURI::BasePath(const FString& InPath)
        {
            FString Result;
            FString Part;

            StringHelpers::FStringIterator PathIt(InPath);

            while (PathIt)
            {
                Part += *PathIt;

                if (*PathIt == TCHAR('/'))
                {
                    Result += Part;
                    Part = TEXT("");
                }

                ++PathIt;
            }

            return Result;
        }

        FString FURI::CleanPath(const FString& InPath)
        {
			TArray<FString> PathParts;
            FString CurrentPath;

            unsigned int Size = (unsigned int)InPath.Len();
            unsigned int Position = 0;
            bool bRooted = false;

            if (Size > 0 && InPath[0] == TCHAR('/'))
            {
                bRooted = true;
                ++Position;
            }

            for (; Position < Size; ++Position)
            {
                if (InPath[Position] == TCHAR('.') && (Position + 1 == Size || InPath[Position + 1] == TCHAR('/')) &&
                    (Position == 0 || InPath[Position - 1] == TCHAR('/')))
                {
                    ++Position;
                }
                else if (InPath[Position] == TCHAR('.') && (Position + 1 < Size && InPath[Position + 1] == TCHAR('.') &&
                                                     (Position + 2 == Size || InPath[Position + 2] == TCHAR('/')) &&
                                                     (Position == 0 || InPath[Position - 1] == TCHAR('/'))))
                {
                    if (PathParts.Num())
                    {
                        PathParts.Pop(false);
                    }
                    Position += 2;
                }
                else if (InPath[Position] != TCHAR('/'))
                {
                    CurrentPath += InPath[Position];
                }
                else if (!CurrentPath.IsEmpty())
                {
                    PathParts.Push(CurrentPath);
                    CurrentPath = TEXT("");
                }
            }

            if (!CurrentPath.IsEmpty())
            {
                PathParts.Push(CurrentPath);
            }

            FString Result;
            if (bRooted)
            {
                Result.AppendChar('/');
            }

            for (int32 i = 0; i < PathParts.Num(); i++)
            {
                if (i != 0)
                {
                    Result.AppendChar(TCHAR('/'));
                }
                Result += PathParts[i];
            }

            // In case the last element was a positional element.
            if (!Result.IsEmpty() && Result[Result.Len() - 1] != TCHAR('/'))
            {
                if ((Size > 2 && InPath[Size - 2] == TCHAR('/') && InPath[Size - 1] == TCHAR('.')) ||
                    (Size > 3 && InPath[Size - 3] == TCHAR('/') && InPath[Size - 2] == TCHAR('.') && InPath[Size - 1] == TCHAR('.')))
                {
					Result.AppendChar(TCHAR('/'));
                }
                else if (Size > 2 && InPath[Size - 1] == TCHAR('/'))
                {
					Result.AppendChar(TCHAR('/'));
                }
            }

            return Result;
        }

    }
} // namespace Electra


