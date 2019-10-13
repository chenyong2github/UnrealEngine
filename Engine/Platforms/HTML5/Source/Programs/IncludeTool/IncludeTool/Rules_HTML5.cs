// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using IncludeTool.Support;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace IncludeTool
{
	/// <summary>
	/// Contains callback functions to disambiguate and provide metadata for files in this branch
	/// </summary>
	static class Rules_HTML5
	{
		/// <summary>
		/// List of include tokens which are external files, and do not need to be resolved
		/// </summary>
		static readonly string[] ExternalFileIncludePaths =
		{
			// HTML5
			"html5.h",
		};

		/// <summary>
		/// Determine whether the given include token is to an external file. If so, it doesn't need to be resolved.
		/// </summary>
		/// <param name="IncludeToken">The include token</param>
		/// <returns>True if the include is to an external file</returns>
		public static bool IsExternalInclude(string IncludePath)
		{
			return ExternalFileIncludePaths.Any(x => IncludePath.StartsWith(x));
		}

	}
}
