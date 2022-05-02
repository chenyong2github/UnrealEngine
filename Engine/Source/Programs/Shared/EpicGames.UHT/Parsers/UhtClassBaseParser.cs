// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Text.Json.Serialization;
using EpicGames.Core;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;

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
		[JsonIgnore]
		public UhtToken SuperIdentifier { get; set; }

		/// <summary>
		/// List of base identifiers
		/// </summary>
		[JsonIgnore]
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2227:Collection properties should be read only", Justification = "")]
		public List<UhtToken[]>? BaseIdentifiers { get; set; } = null;

		/// <summary>
		/// Construct a new base class parser
		/// </summary>
		/// <param name="outer">Outer type</param>
		/// <param name="lineNumber">Line number of class</param>
		public UhtClassBaseParser(UhtType outer, int lineNumber) : base(outer, lineNumber)
		{
		}

		/// <inheritdoc/>
		protected override void ResolveSuper(UhtResolvePhase resolvePhase)
		{
			base.ResolveSuper(resolvePhase);
			switch (resolvePhase)
			{
				case UhtResolvePhase.Bases:
					BindAndResolveSuper(this.SuperIdentifier, UhtFindOptions.Class);
					BindAndResolveBases(this.BaseIdentifiers, UhtFindOptions.Class);

					// Force the MatchedSerializers on for anything being exported
					if (!this.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.NoExport))
					{
						this.ClassFlags |= EClassFlags.MatchedSerializers;
					}
					break;
			}
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase resolvePhase)
		{
			bool result = base.ResolveSelf(resolvePhase);

			switch (resolvePhase)
			{
				case UhtResolvePhase.Properties:
					UhtPropertyParser.ResolveChildren(this, UhtPropertyParseOptions.AddModuleRelativePath);
					break;
			}
			return result;
		}
	}
}
