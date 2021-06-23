// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;
using System.Xml.Linq;
using Microsoft.Deployment.WindowsInstaller;
using Microsoft.Win32;

namespace CustomActionManaged
{
	/// <summary>
	/// Helper that exposes misc environment info.
	/// </summary>
	public static class CustomEnvironmentInfo
	{
		[DllImport("kernel32.dll", SetLastError = true, CallingConvention = CallingConvention.Winapi)]
		[return: MarshalAs(UnmanagedType.Bool)]
		private static extern bool IsWow64Process(
			[In] IntPtr hProcess,
			[Out] out bool wow64Process
		);

		private static bool InternalIs64BitOperatingSystem()
		{
			bool RetVal = false;

			try
			{
				using (Process p = Process.GetCurrentProcess())
				{
					if (!IsWow64Process(p.Handle, out RetVal))
					{
						return false;
					}
				}
			}
			catch (System.Exception)
			{

			}
			return RetVal;
		}

		// Normally we would use Environment.Is64BitOperatingSystem but this is a .NET 4.0 property and we want to stick to 2.0 to cover
		//  all potential release platforms.
		public static bool Is64BitOperatingSystem = (IntPtr.Size == 8) || InternalIs64BitOperatingSystem();
	}

	public static class RegistryHelper
	{
		[DllImport("Advapi32.dll")]
		static extern uint RegOpenKeyEx(
			UIntPtr hKey,
			string lpSubKey,
			uint ulOptions,
			int samDesired,
			out int phkResult);

		[DllImport("Advapi32.dll")]
		static extern uint RegCloseKey(int hKey);

		[DllImport("advapi32.dll", EntryPoint = "RegQueryValueEx")]
		public static extern int RegQueryValueEx(
			int hKey, string lpValueName,
			int lpReserved,
			ref uint lpType,
			System.Text.StringBuilder lpData,
			ref uint lpcbData);

		public static UIntPtr HKEY_LOCAL_MACHINE = new UIntPtr(0x80000002u);
		public static UIntPtr HKEY_CURRENT_USER = new UIntPtr(0x80000001u);

		public static int QueryValue = 0x0001;
		public static int WOW64_64Key = 0x0100;

		/// <summary>
		/// Helper function that allows the retrieval of 64bit registry values from 32bit processes.  This
		/// is a required workaround because we are limited to using .NET 2.0 and because we run the custom
		/// action in 32bit mode which results in registry redirection(virtualization) when using Registry.GetValue().
		/// </summary>
		static public string GetRegKey64(UIntPtr KeySection, String KeyName, String ValueName)
		{
			int Key = 0;
			string Data = null;

			try
			{
				if (RegOpenKeyEx(KeySection, KeyName, 0, QueryValue | WOW64_64Key, out Key) == 0)
				{
					uint Type = 0;
					uint DataSize = 1024;
					StringBuilder DataSB = new StringBuilder(1024);
					RegQueryValueEx(Key, ValueName, 0, ref Type, DataSB, ref DataSize);
					Data = DataSB.ToString();
				}
			}
			finally
			{
				if (Key != 0)
				{
					RegCloseKey(Key);
				}
			}

			return Data;
		}
	}

	public class CustomActions
	{
		/// <summary>
		/// Helper function for to get session properties in a safe way.  Returns the empty string if the property is not found.
		/// </summary>
		private static string GetSessionProperty(Session session, string PropertyName)
		{
			string PropertyValue = string.Empty;
			try
			{
				PropertyValue = session[PropertyName];
			}
			catch (System.Exception ex)
			{
				PropertyValue = string.Empty;
				session.Log("Failed to get session property {0}: {1}", PropertyName, ex.Message);
			}
			return PropertyValue;
		}

		/// <summary>
		/// Helper function for setting session properties.
		/// </summary>
		private static void SetSessionProperty(Session session, string PropertyName, string Value)
		{
			try
			{
				session[PropertyName] = Value;
			}
			catch (System.Exception ex)
			{
				session.Log("Failed to set session property {0}: {1}", PropertyName, ex.Message);
			}
		}

		[CustomAction]
		public static ActionResult SearchInstallationPaths(Session session)
		{
			try
			{
				string[] PropertiesToUpdate = new string[] {
					"ARCHICAD25DIR",
					"ARCHICAD24DIR",
					"ARCHICAD23DIR",
				};

				string[] RegistryKeyPaths = new string[] {
					"SOFTWARE\\GRAPHISOFT SE\\ARCHICAD 25 RC1",
					"SOFTWARE\\GRAPHISOFT SE\\ARCHICAD 24",
					"SOFTWARE\\GRAPHISOFT SE\\ARCHICAD 23",
				};

				for (int i = 0; i < RegistryKeyPaths.Length; i++)
				{
					string Value = RegistryHelper.GetRegKey64(RegistryHelper.HKEY_LOCAL_MACHINE, RegistryKeyPaths[i], "Location");
					if (Value != string.Empty)
					{
						SetSessionProperty(session, PropertiesToUpdate[i], Value);
						session.Log("Updating property {0} to value {1}.", PropertiesToUpdate[i], Value);
					}
				}

				session.Log("Successfully update option's values.");
			}
			catch (System.Exception)
			{
				session.Log("Failed to update option's values.");
			}

			// We always return success even if the procedure to set engine start tab failed.  This will prevent
			//  the installer from failing and rolling back.
			return ActionResult.Success;
		}

	}
}

