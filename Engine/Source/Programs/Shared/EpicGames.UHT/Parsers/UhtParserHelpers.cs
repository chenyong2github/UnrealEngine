// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;

namespace EpicGames.UHT.Parsers
{
	public static class UhtParserHelpers
	{
		public static void ParseInheritance(IUhtTokenReader TokenReader, out UhtToken SuperIdentifier, out List<UhtToken[]>? BaseIdentifiers)
		{
			UhtToken SuperIdentifierTemp = new UhtToken();
			List<UhtToken[]>? BaseIdentifiersTemp = null;
			TokenReader.OptionalInheritance(
				(ref UhtToken Identifier) =>
				{
					UhtConfig.Instance.RedirectTypeIdentifier(ref Identifier);
					SuperIdentifierTemp = Identifier;
				},
				(UhtTokenList Identifier) =>
				{
					if (BaseIdentifiersTemp == null)
					{
						BaseIdentifiersTemp = new List<UhtToken[]>();
					}
					BaseIdentifiersTemp.Add(Identifier.ToArray());
				});
			SuperIdentifier = SuperIdentifierTemp;
			BaseIdentifiers = BaseIdentifiersTemp;
		}

		public static void ParseCompileVersionDeclaration(IUhtTokenReader TokenReader, UhtStruct Struct)
		{

			// Fetch the default generation code version. If supplied, then package code version overrides the default.
			EGeneratedCodeVersion Version = Struct.Package.Module.GeneratedCodeVersion;
			if (Version == EGeneratedCodeVersion.None)
			{
				Version = UhtConfig.Instance.DefaultGeneratedCodeVersion;
			}

			// Fetch the code version from header file
			TokenReader
				.Require('(')
				.OptionalIdentifier((ref UhtToken Identifier) =>
				{
					if (!Enum.TryParse(Identifier.Value.ToString(), true, out Version))
					{
						Version = EGeneratedCodeVersion.None;
					}
				})
				.Require(')');

			// Save the results
			Struct.GeneratedCodeVersion = Version;
		}
	}
}
