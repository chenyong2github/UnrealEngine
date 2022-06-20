// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace UnrealGameSync
{
	internal class GlobalPerforceSettings
	{
		public static void ReadGlobalPerforceSettings(ref string? ServerAndPort, ref string? UserName, ref string? DepotPath, ref bool bPreview)
		{
			using (RegistryKey? Key = Registry.CurrentUser.OpenSubKey("SOFTWARE\\Epic Games\\UnrealGameSync", false))
			{
				if (Key != null)
				{
					ServerAndPort = Key.GetValue("ServerAndPort", ServerAndPort) as string;
					UserName = Key.GetValue("UserName", UserName) as string;
					DepotPath = Key.GetValue("DepotPath", DepotPath) as string;
					bPreview = ((Key.GetValue("Preview", bPreview? 1 : 0) as int?) ?? 0) != 0;

					// Fix corrupted depot path string
					if (DepotPath != null)
					{
						Match Match = Regex.Match(DepotPath, "^(.*)/(Release|UnstableRelease)/\\.\\.\\.@.*$");
						if (Match.Success)
						{
							DepotPath = Match.Groups[1].Value;
							SaveGlobalPerforceSettings(ServerAndPort, UserName, DepotPath, bPreview);
						}
					}
				}
			}
		}

		public static void DeleteRegistryKey(RegistryKey RootKey, string KeyName, string ValueName)
		{
			using (RegistryKey? Key = RootKey.OpenSubKey(KeyName, true))
			{
				if (Key != null)
				{
					DeleteRegistryKey(Key, ValueName);
				}
			}
		}

		public static void DeleteRegistryKey(RegistryKey Key, string Name)
		{
			string[] ValueNames = Key.GetValueNames();
			if (ValueNames.Any(x => String.Compare(x, Name, StringComparison.OrdinalIgnoreCase) == 0))
			{
				try
				{
					Key.DeleteValue(Name);
				}
				catch
				{
				}
			}
		}

		public static void SaveGlobalPerforceSettings(string? ServerAndPort, string? UserName, string? DepotPath, bool bPreview)
		{
			try
			{
				using (RegistryKey Key = Registry.CurrentUser.CreateSubKey("SOFTWARE\\Epic Games\\UnrealGameSync"))
				{
					// Delete this legacy setting
					DeleteRegistryKey(Key, "Server");

					if (String.IsNullOrEmpty(ServerAndPort))
					{
						try { Key.DeleteValue("ServerAndPort"); } catch (Exception) { }
					}
					else
					{
						Key.SetValue("ServerAndPort", ServerAndPort);
					}

					if (String.IsNullOrEmpty(UserName))
					{
						try { Key.DeleteValue("UserName"); } catch (Exception) { }
					}
					else
					{
						Key.SetValue("UserName", UserName);
					}

					if (String.IsNullOrEmpty(DepotPath) || (DeploymentSettings.DefaultDepotPath != null && String.Equals(DepotPath, DeploymentSettings.DefaultDepotPath, StringComparison.InvariantCultureIgnoreCase)))
					{
						DeleteRegistryKey(Key, "DepotPath");
					}
					else
					{
						Key.SetValue("DepotPath", DepotPath);
					}

					if (bPreview)
					{
						Key.SetValue("Preview", 1);
					}
					else
					{
						try { Key.DeleteValue("Preview"); } catch (Exception) { }
					}
				}
			}
			catch (Exception Ex)
			{
				MessageBox.Show("Unable to save settings.\n\n" + Ex.ToString());
			}
		}
	}
}
