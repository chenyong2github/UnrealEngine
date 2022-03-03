// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using System.Collections.Generic;

namespace EpicGames.UHT.Parsers
{
	public class UhtClassBaseParser : UhtClass
	{
		public UhtToken SuperIdentifier;
		public List<UhtToken[]>? BaseIdentifiers = null;

		public UhtClassBaseParser(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}

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
