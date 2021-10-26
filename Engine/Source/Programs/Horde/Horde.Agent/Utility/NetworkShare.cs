// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using System.Text;

namespace HordeAgent.Utility
{
	/// <summary>
	/// Utility functions for mapping network shares
	/// </summary>
	static class NetworkShare
	{
		[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
		private struct NETRESOURCE
		{
			public uint dwScope;
			public uint dwType;
			public uint dwDisplayType;
			public uint dwUsage;
			public string? lpLocalName;
			public string? lpRemoteName;
			public string? lpComment;
			public string? lpProvider;
		};

		const uint RESOURCETYPE_DISK = 1;
		const uint CONNECT_TEMPORARY = 4;


		const int ERROR_ALREADY_ASSIGNED = 85;
		const int ERROR_MORE_DATA = 234;

		[DllImport("Mpr.dll", CharSet = CharSet.Unicode)]
		private static extern int WNetAddConnection2W(ref NETRESOURCE lpNetResource, string? lpPassword, string? lpUsername, System.UInt32 dwFlags );

		[DllImport("Mpr.dll", CharSet = CharSet.Unicode)]
		private static extern int WNetCancelConnectionW(string lpName, [MarshalAs(UnmanagedType.Bool)] bool fForce);

		[DllImport("Mpr.dll", CharSet = CharSet.Unicode)]
		private static extern int WNetGetConnectionW(string lpLocalName, IntPtr lpRemoteName, ref int lpnLength);

		/// <summary>
		/// Mounts a network share at the given location
		/// </summary>
		/// <param name="MountPoint">Mount point for the share</param>
		/// <param name="RemotePath">Path to the remote resource</param>
		public static void Mount(string MountPoint, string RemotePath)
		{
			NETRESOURCE NetResource = new NETRESOURCE();
			NetResource.dwType = RESOURCETYPE_DISK;
			NetResource.lpLocalName = MountPoint;
			NetResource.lpRemoteName = RemotePath;

			int Result = WNetAddConnection2W(ref NetResource, null, null, CONNECT_TEMPORARY);
			if(Result != 0)
			{
				if (Result == ERROR_ALREADY_ASSIGNED)
				{
					string? CurRemotePath;
					if (TryGetRemotePath(MountPoint, out CurRemotePath))
					{
						if (CurRemotePath.Equals(RemotePath, StringComparison.OrdinalIgnoreCase))
						{
							return;
						}
						else
						{
							throw new Win32Exception(Result, $"Unable to mount network share {RemotePath} as {MountPoint} ({Result}). Currently connected to {CurRemotePath}.");
						}
					}
				}
				throw new Win32Exception(Result, $"Unable to mount network share {RemotePath} as {MountPoint} ({Result}: {new Win32Exception(Result).Message})");
			}
		}

		/// <summary>
		/// Unmounts a network share
		/// </summary>
		/// <param name="MountPoint">The mount point to remove</param>
		public static void Unmount(string MountPoint)
		{
			int Result = WNetCancelConnectionW(MountPoint, true);
			if(Result != 0)
			{
				throw new Win32Exception(Result, $"Unable to unmount {MountPoint} ({Result})");
			}
		}

		/// <summary>
		/// Gets the currently mounted path for a network share
		/// </summary>
		/// <param name="MountPoint">The mount point</param>
		/// <param name="OutRemotePath">Receives the remote path</param>
		/// <returns>True if the remote path is returned</returns>
		public static bool TryGetRemotePath(string MountPoint, [NotNullWhen(true)] out string? OutRemotePath)
		{
			int Length = 260 * sizeof(char);
			for (; ; )
			{
				IntPtr RemotePath = Marshal.AllocHGlobal(Length);
				try
				{
					int PrevLength = Length;
					int Result = WNetGetConnectionW(MountPoint, RemotePath, ref Length);
					if (Result == 0)
					{
						OutRemotePath = Marshal.PtrToStringUni(RemotePath)!;
						return OutRemotePath != null;
					}
					else if(Result == ERROR_MORE_DATA && Length > PrevLength)
					{
						continue;
					}
					else
					{
						OutRemotePath = null!;
						return false;
					}
				}
				finally
				{
					Marshal.FreeHGlobal(RemotePath);
				}
			}
		}
	}
}
