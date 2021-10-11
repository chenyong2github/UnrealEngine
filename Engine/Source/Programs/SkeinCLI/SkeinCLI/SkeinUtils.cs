// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace SkeinCLI
{
	public static class SkeinUtils
	{
		/// <summary>
		/// Returns the root folder where all Skein data should be stored.
		/// </summary>
		public static string GetDataFolder()
		{
			// Windows: C:\Users\<username>\AppData\Roaming\skein
			// Linux  : /home/.config/skein

			return Path.Combine(System.Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "skein");
		}
	}
}