// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"

namespace Electra
{

    namespace Utilities
    {

        enum class FURIParseError
        {
            None,
            InvalidScheme,
            PathSegmentContainsColon,
        };

        struct FURIUserInfo
        {
            FString Username;
            FString Password;
        };

        /**
         * Implementation of a Uniform Resource Identifier implementation based on RFC 3986 which currently doesn't cover
         * escaping / unescaping of paths, username, password and query variables.
         */
        class FURI
        {
        private:
            FURIParseError ParseError = FURIParseError::None;
        public:
            FString Scheme;

            /**
             * Host which contains a port if present.
             */
            FString Host;
            FString Path;

            /**
             * Identifies that a fragment is added to the URI even if it's empty.
             */
            bool HasFragment;

            /**
             * Encoded fragment which will be added after the #.
             */
            FString Fragment;

            /**
             * Opaque as of RFC 3986 is used for URI schemes where no hierarchy is visible. For example:
             *
             *   mailto:test@epicgames.com
             *   tel:+1-123-123-1234
             *
             * This is set if no authority or path was detected and can never be set together with Path.
             */
            FString Opaque;
            FURIUserInfo UserInfo;

            /**
             * Identifies that a query is added to the URI even if it's empty.
             */
            bool HasQuery;

            /**
             * The query as an encoded string.
             */
            FString QueryString;

            /**
             * After using FURI::Parse this returns if the URI was successfully parsed.
             * @return
             */
            inline const bool IsValid()
            {
                return ParseError == FURIParseError::None;
            }

            inline const FURIParseError GetParseError()
            {
                return ParseError;
            }

            /**
             * Returns true if either Username or Password is available.
             * @return
             */
            inline bool HasUserInfo() const
            {
                return !UserInfo.Password.IsEmpty() || !UserInfo.Username.IsEmpty();
            }

            /**
             * Returns true if either a Host or Username/Password is available.
             * @return
             */
            inline bool HasAuthority() const
            {
                return HasUserInfo() || !Host.IsEmpty();
            }

            /**
             * Turns the URI into a string.
             *
             *   scheme ":" [ "//" authority ] ( path | opaque ) [ "?" query ] [ "#" fragment ]
             *
             * @return
             */
            const FString Format() const;

            /**
             * Resolves and returns a new absolute target url based on this FUrl instance using the provided InUrl as
             * reference. Note that it's using the strict format of transformation, see
             * (https://tools.ietf.org/html/rfc3986#section-5.2.2) for details.
             * @param InUri The URI used for resolving the new URI, which can be relative.
             * @return A new instance which contains the absolute resolved URI.
             */
            const FURI Resolve(const FURI& InUri) const;

            /**
             * Parses a string into a FURI. If the string doesn't represent a valid URL the resulting FURI.IsValid()
             * will return false. To receive the proper error use FURI.GetParseError().
             * @param InUri
             * @return
             */
            static FURI Parse(const FString& InUri);

            /**
             * Returns the base path of an URI path.
             *
             * Example:
             *  "/some/path" -> "/some/"
             *  "/test/path/" -> "/test/path/"
             *  "/" -> "/"
             *  "test" -> ""
             *  "relative/path" -> "relative"
             *
             * @param InPath
             * @return
             */
            static FString BasePath(const FString& InPath);

            /**
             * Cleans up a path by removing "." and "..".
             *
             * Example:
             *  "test/../path" -> "path"
             *  "test/./path" -> "test/path"
             *
             * @param InPath
             * @return
             */
            static FString CleanPath(const FString& InPath);
        };
    }
} // namespace Electra


