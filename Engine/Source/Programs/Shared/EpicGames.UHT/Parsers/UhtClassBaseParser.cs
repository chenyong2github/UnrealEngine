// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using System.Collections.Generic;

namespace EpicGames.UHT.Parsers
{

	/// <summary>
	/// Base parser class for all UClass types
	/// </summary>
	public class UhtClassBaseParser : UhtClass
	{

		/// <summary>
		/// The super class identifier
		/// </summary>
		public UhtToken SuperIdentifier;

		/// <summary>
		/// List of base identifiers
		/// </summary>
		public List<UhtToken[]>? BaseIdentifiers = null;

		/// <summary>
		/// Construct a new base class parser
		/// </summary>
		/// <param name="Outer">Outer type</param>
		/// <param name="LineNumber">Line number of class</param>
		public UhtClassBaseParser(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}

		/// <inheritdoc/>
		protected override void ResolveSuper(UhtResolvePhase ResolvePhase)
		{
			base.ResolveSuper(ResolvePhase);
			switch (ResolvePhase)
			{
				case UhtResolvePhase.Bases:
					BindAndResolveSuper(ref this.SuperIdentifier, UhtFindOptions.Class);
					BindAndResolveBases(this.BaseIdentifiers, UhtFindOptions.Class);

					// Force the MatchedSerializers on for anything being exported
					if (!this.ClassFlags.HasAnyFlags(EClassFlags.NoExport))
					{
						this.ClassFlags |= EClassFlags.MatchedSerializers;
					}
					break;
			}
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase ResolvePhase)
		{
			bool bResult = base.ResolveSelf(ResolvePhase);

			switch (ResolvePhase)
			{
				case UhtResolvePhase.Properties:
					UhtPropertyParser.ResolveChildren(this, UhtPropertyParseOptions.AddModuleRelativePath);
					break;
			}
			return bResult;
		}
	}
}
