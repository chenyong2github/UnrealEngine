// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using EpicGames.Core;
using Horde.Build.Users;

namespace Horde.Build.Perforce
{
	/// <summary>
	/// Flags identifying content of a changelist
	/// </summary>
	[Flags]
	public enum ChangeContentFlags
	{
		/// <summary>
		/// The change contains code
		/// </summary>
		ContainsCode = 1,

		/// <summary>
		/// The change contains content
		/// </summary>
		ContainsContent = 2,
	}
}
