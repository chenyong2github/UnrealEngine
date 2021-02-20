// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using MySql.Data.MySqlClient;
using AutomationTool;

namespace Gauntlet
{
	/// <summary>
	/// Interface to drive Database submission
	/// </summary>
	public interface IDatabaseDriver<Data> where Data : class
	{
		/// <summary>
		/// Submit collection of object to target Database, use TestContext to complete data modeling.
		/// </summary>
		/// <param name="DataItems"></param>
		/// <param name="Context"></param>
		/// <returns></returns>
		bool SubmitDataItems(IEnumerable<Data> DataItems, ITestContext Context);
	}

	/// <summary>
	/// Interface for Database Configuration
	/// </summary>
	public interface IDatabaseConfig<Data> where Data : class 
	{
		void LoadConfig(string ConfigFilePath);
		IDatabaseDriver<Data> GetDriver();
	}

	/// <summary>
	/// Interface for data type MySQL Configuration
	/// </summary>
	public interface IMySQLConfig<Data> where Data : class
	{
		/// <summary>
		/// Get Target Table name based on data type
		/// </summary>
		string GetTableName();

		/// <summary>
		/// Get Target Table columns based on data type
		/// </summary>
		IEnumerable<string> GetTableColumns();

		/// Format the data for target table based on data type
		/// </summary>
		/// <returns></returns>
		IEnumerable<string> FormatDataForTable(Data InData, ITestContext InContext);
	}

	public class MySQLDriver<Data> : IDatabaseDriver<Data> where Data : class 
	{
		protected MySQLConfig<Data> Config;

		public MySQLDriver(MySQLConfig<Data> InConfig)
		{
			Config = InConfig;
			if (string.IsNullOrEmpty(Config.ConfigString))
			{
				throw new AutomationException(string.Format("Database Driver '{0}' not configured.", this.GetType().FullName));
			}
			if (string.IsNullOrEmpty(Config.DatabaseName))
			{
				throw new AutomationException(string.Format("Database Driver '{0}' not configured properly, missing Database name.", this.GetType().FullName));
			}
		}

		public override string ToString()
		{
			return Config.GetConfigValue("Server");
		}

		public object Insert(string Table, IEnumerable<string> Columns, IEnumerable<IEnumerable<string>> Rows)
		{
			string SqlQuery = string.Format(
				"INSERT INTO `{0}`.{1} ({2}) VALUES {3}",
				Config.DatabaseName, Table, string.Join(", ", Columns), string.Join(", ", Rows.Select(R => string.Format("({0})", string.Join(", ", R.Select(V => string.Format("'{0}'", V))))))
			);
			return MySqlHelper.ExecuteScalar(Config.ConfigString, SqlQuery);
		}

		public bool SubmitDataItems(IEnumerable<Data> DataRows, ITestContext TestContext)
		{
			if (Config is IMySQLConfig<Data> DataConfig)
			{
				object Value = Insert(
					DataConfig.GetTableName(),
					DataConfig.GetTableColumns(),
					DataRows.Select(D => DataConfig.FormatDataForTable(D, TestContext))
				);
				return Value != null;
			}
			else
			{
				Log.Error("MySQL configuration '{0}' does not known how to handle {1}.", Config.GetType().FullName, typeof(Data).FullName);
			}

			return false;
		}
	}

	public abstract class MySQLConfig<Data> : IDatabaseConfig<Data>, IMySQLConfig<Data> where Data : class
	{
		public string ConfigString { get; protected set; }
		public string DatabaseName { get; protected set; }
		protected Dictionary<string, string> KeyValuePairs = null;

		public virtual void LoadConfig(string ConfigFilePath)
		{
			ConfigString = string.Empty;
			DatabaseName = string.Empty;
			KeyValuePairs = null;

			if (File.Exists(ConfigFilePath))
			{
				using (StreamReader ConnectionReader = new StreamReader(ConfigFilePath))
				{
					ConfigString = ConnectionReader.ReadLine();
				}

				if (string.IsNullOrEmpty(ConfigString))
				{
					Log.Warning("Properly found config file, but couldn't read a valid connection string.");
					return;
				}
				else
				{
					Log.Info("Found MySQL connection string from config file.");
				}
			}
			else
			{
				Log.Error("Could not find connection string config file at '{0}'.", ConfigFilePath);
				return;
			}

			DatabaseName = GetConfigValue("database");
			if (string.IsNullOrEmpty(DatabaseName))
			{
				Log.Warning("Missing MySQL Database name in config file '{0}'.", ConfigFilePath);
				return;
			}
		}

		public string GetConfigValue(string Key)
		{
			if (string.IsNullOrEmpty(ConfigString))
			{
				return null;
			}
			if (KeyValuePairs == null)
			{
				KeyValuePairs = ConfigString.Split(';').Where(KeyValue => KeyValue.Contains('='))
														.Select(KeyValue => KeyValue.Split('=', 2))
														.ToDictionary(
															KeyValue => KeyValue[0].Trim(),
															KeyValue => KeyValue[1].Trim(),
															StringComparer.InvariantCultureIgnoreCase
														);
			}
			string FoundValue;
			if (KeyValuePairs.TryGetValue(Key, out FoundValue))
			{
				return FoundValue;
			}
			return string.Empty;
		}

		public IDatabaseDriver<Data> GetDriver()
		{
			return new MySQLDriver<Data>(this);
		}

		/// <summary>
		/// Get Target Table name based on data type
		/// </summary>
		public abstract string GetTableName();
		/// <summary>
		/// Get Target Table columns based on data type
		/// </summary>
		public abstract IEnumerable<string> GetTableColumns();
		/// <summary>
		/// Format the data for target table based on data type
		/// </summary>
		/// <returns></returns>
		public abstract IEnumerable<string> FormatDataForTable(Data InData, ITestContext InContext);

	}

	public class DatabaseConfigManager<Data> where Data : class
	{
		protected static IEnumerable<IDatabaseConfig<Data>> Configs;
		static DatabaseConfigManager()
		{
			Configs = Gauntlet.Utils.InterfaceHelpers.FindImplementations<IDatabaseConfig<Data>>();
		}
		public static IDatabaseConfig<Data> GetConfigByName(string Name)
		{
			return Configs.Where(C => string.Equals(C.GetType().Name, Name, StringComparison.OrdinalIgnoreCase)).FirstOrDefault();
		}
	}
}
