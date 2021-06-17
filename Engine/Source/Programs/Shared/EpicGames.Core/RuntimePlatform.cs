// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace EpicGames.Core
{
	public class RuntimePlatform
	{
		/// <summary>
		/// Whether we are currently running on Linux.
		/// </summary>
		public static readonly bool IsLinux = RuntimeInformation.IsOSPlatform(OSPlatform.Linux);

		/// <summary>
		/// Whether we are currently running on a MacOS platform.
		/// </summary>
		public static readonly bool IsMac = RuntimeInformation.IsOSPlatform(OSPlatform.OSX);

		/// <summary>
		/// Whether we are currently running a Windows platform.
		/// </summary>
		public static readonly bool IsWindows = RuntimeInformation.IsOSPlatform(OSPlatform.Windows);

		public enum Type
		{
			Windows, Linux, Mac
		};

		/// <summary>
		/// The current runtime platform
		/// </summary>
		public static readonly Type Current = IsWindows ? Type.Windows : IsMac ? Type.Mac : Type.Linux;

		/// <summary>
		/// The extension executables have on the current platform
		/// </summary>
		public static readonly string ExeExtension = IsWindows ? ".exe" : "";
	}
}
