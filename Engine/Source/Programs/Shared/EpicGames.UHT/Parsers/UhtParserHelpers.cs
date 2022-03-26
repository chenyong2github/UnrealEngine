// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;

namespace EpicGames.UHT.Parsers
{
	/// <summary>
	/// Collection of helper methods
	/// </summary>
	public static class UhtParserHelpers
	{

		/// <summary>
		/// Parse the inheritance 
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Config">Configuration</param>
		/// <param name="SuperIdentifier">Output super identifier</param>
		/// <param name="BaseIdentifiers">Output base identifiers</param>
		public static void ParseInheritance(IUhtTokenReader TokenReader, IUhtConfig Config, out UhtToken SuperIdentifier, out List<UhtToken[]>? BaseIdentifiers)
		{
			UhtToken SuperIdentifierTemp = new UhtToken();
			List<UhtToken[]>? BaseIdentifiersTemp = null;
			TokenReader.OptionalInheritance(
				(ref UhtToken Identifier) =>
				{
					Config.RedirectTypeIdentifier(ref Identifier);
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

		/// <summary>
		/// Parse compiler version declaration
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Config">Configuration</param>
		/// <param name="Struct">Struct being parsed</param>
		public static void ParseCompileVersionDeclaration(IUhtTokenReader TokenReader, IUhtConfig Config, UhtStruct Struct)
		{

			// Fetch the default generation code version. If supplied, then package code version overrides the default.
			EGeneratedCodeVersion Version = Struct.Package.Module.GeneratedCodeVersion;
			if (Version == EGeneratedCodeVersion.None)
			{
				Version = Config.DefaultGeneratedCodeVersion;
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
