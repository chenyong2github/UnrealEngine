// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using Microsoft.Win32.SafeHandles;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Net.NetworkInformation;
using System.Runtime.InteropServices;
using System.Text;

namespace HordeAgent.Utility
{
	static class Shutdown
	{
		public const UInt32 TOKEN_QUERY = 0x0008;
		public const UInt32 TOKEN_ADJUST_PRIVILEGES = 0x0020;

		[DllImport("advapi32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool OpenProcessToken(IntPtr ProcessHandle, UInt32 DesiredAccess, out IntPtr TokenHandle);

		[StructLayout(LayoutKind.Sequential)]
		public struct LUID
		{
			public UInt32 LowPart;
			public Int32 HighPart;
		}

		[DllImport("advapi32.dll")]
		static extern bool LookupPrivilegeValue(string? lpSystemName, string lpName, ref LUID lpLuid);

		[StructLayout(LayoutKind.Sequential, Pack = 4)]
		public struct LUID_AND_ATTRIBUTES
		{
			public LUID Luid;
			public UInt32 Attributes;
		}

		struct TOKEN_PRIVILEGES
		{
			public int PrivilegeCount;
			[MarshalAs(UnmanagedType.ByValArray, SizeConst = 1)]
			public LUID_AND_ATTRIBUTES[] Privileges;
		}

		// Use this signature if you do not want the previous state
		[DllImport("advapi32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool AdjustTokenPrivileges(IntPtr TokenHandle,
		   [MarshalAs(UnmanagedType.Bool)]bool DisableAllPrivileges,
		   ref TOKEN_PRIVILEGES NewState,
		   UInt32 Zero,
		   IntPtr Null1,
		   IntPtr Null2);

		const uint SHTDN_REASON_MAJOR_APPLICATION = 0x00040000;
		const uint SHTDN_REASON_MINOR_MAINTENANCE = 0x00000001;

		[DllImport("advapi32.dll", CharSet = CharSet.Auto, SetLastError = true)]
		static extern bool InitiateSystemShutdownEx(string? lpMachineName, string? lpMessage, uint dwTimeout, bool bForceAppsClosed, bool bRebootAfterShutdown, uint dwReason);

		const string SE_SHUTDOWN_NAME = "SeShutdownPrivilege";

		const int SE_PRIVILEGE_ENABLED = 0x00000002;

		/// <summary>
		/// Initiate a shutdown operation
		/// </summary>
		/// <param name="bRestartAfterShutdown">Whether to restart after the shutdown</param>
		/// <param name="Logger">Logger for the operation</param>
		/// <returns></returns>
		public static bool InitiateShutdown(bool bRestartAfterShutdown, ILogger Logger)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				if (bRestartAfterShutdown)
				{
					Logger.LogInformation("Triggering restart");
				}
				else
				{
					Logger.LogInformation("Triggering shutdown");
				}

				IntPtr TokenHandle;
				if (!OpenProcessToken(Process.GetCurrentProcess().Handle, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, out TokenHandle))
				{
					Logger.LogError("OpenProcessToken() failed (code 0x{Code:x8})", Marshal.GetLastWin32Error());
					return false;
				}

				// Get the LUID for the shutdown privilege. 
				LUID Luid = new LUID();
				if (!LookupPrivilegeValue(null, SE_SHUTDOWN_NAME, ref Luid))
				{
					Logger.LogError("LookupPrivilegeValue() failed (code 0x{Code:x8})", Marshal.GetLastWin32Error());
					return false;
				}

				TOKEN_PRIVILEGES Privileges = new TOKEN_PRIVILEGES();
				Privileges.PrivilegeCount = 1;
				Privileges.Privileges = new LUID_AND_ATTRIBUTES[1];
				Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
				Privileges.Privileges[0].Luid = Luid;

				if (!AdjustTokenPrivileges(TokenHandle, false, ref Privileges, 0, IntPtr.Zero, IntPtr.Zero))
				{
					Logger.LogError("AdjustTokenPrivileges() failed (code 0x{Code:x8})", Marshal.GetLastWin32Error());
					return false;
				}

				if (!InitiateSystemShutdownEx(null, "HordeAgent has initiated shutdown", 10, true, bRestartAfterShutdown, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_MAINTENANCE))
				{
					Logger.LogError("Shutdown failed (0x{Code:x8})", Marshal.GetLastWin32Error());
					return false;
				}
				return true;
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				String ShutdownArgs;
				if (bRestartAfterShutdown)
                {
					ShutdownArgs = "sudo shutdown -r +0 \"Horde Agent is restarting\"";
				}
				else
                {
					ShutdownArgs = "sudo shutdown +0 \"Horde Agent is shutting down\"";
				}
 
				Process ShutdownProcess = new Process()
				{
					StartInfo = new ProcessStartInfo
					{
						FileName = "/bin/sh",
						Arguments = String.Format("-c \"{0}\"", ShutdownArgs),
						UseShellExecute = false,
						CreateNoWindow = true
					}
				};
 
				ShutdownProcess.Start();
				ShutdownProcess.WaitForExit();
				int ExitCode = ShutdownProcess.ExitCode;
				if (ExitCode != 0)
				{
					Logger.LogError("Shutdown failed ({0})", ExitCode);
					return false;
				}
				return true;
			}
			else
			{
				Logger.LogError("Shutdown is not implemented on this platform");
				return false;
			}
		}
	}
}
