// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;


namespace nDisplayLauncher.Helpers
{
	// String helpers
	public static class StringHelper
	{
		// Extended version of string.Contains with case checking
		public static bool Contains(string Source, string ToCheck, StringComparison Comp)
		{
			return Source?.IndexOf(ToCheck, Comp) >= 0;
		}
	}
}
