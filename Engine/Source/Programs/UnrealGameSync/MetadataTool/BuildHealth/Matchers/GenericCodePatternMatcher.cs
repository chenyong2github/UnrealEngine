// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon.Perforce;

namespace MetadataTool
{
	/// <summary>
	/// Base class for pattern matchers that match errors in source code
	/// </summary>
	abstract class GenericCodePatternMatcher : PatternMatcher
	{
		/// <summary>
		/// Set of extensions to treat as code
		/// </summary>
		static readonly string[] CodeExtensions =
		{
			".c",
			".cc",
			".cpp",
			".m",
			".mm",
			".rc",
			".cs",
			".h",
			".hpp",
			".inl",
			".uproject",
			".uplugin",
			".ini",
		};

		/// <summary>
		/// Finds any suspected causers for a particular failure. Excludes any changes that don't contain code.
		/// </summary>
		/// <param name="Perforce">The perforce connection</param>
		/// <param name="Issue">The build issue</param>
		/// <param name="Changes">List of changes since the issue first occurred.</param>
		/// <returns>List of changes which are causers for the issue</returns>
		public override List<ChangeInfo> FindCausers(PerforceConnection Perforce, BuildHealthIssue Issue, IReadOnlyList<ChangeInfo> Changes)
		{
			List<ChangeInfo> Causers = base.FindCausers(Perforce, Issue, Changes);
			Causers.RemoveAll(x => !ContainsAnyFileWithExtension(Perforce, x, CodeExtensions));
			return Causers;
		}
	}
}
