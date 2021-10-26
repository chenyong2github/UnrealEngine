// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Linq;
using System.Data;
using System.Collections.Generic;
using MySql.Data.MySqlClient;
using AutomationTool;

namespace Gauntlet
{

	/// <summary>
	/// Hold information about the telemetry context
	/// </summary>
	public interface ITelemetryContext
	{
		object GetProperty(string Name);
	}

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
		bool SubmitDataItems(IEnumerable<Data> DataItems, ITelemetryContext Context);
		/// <summary>
		/// Execute a Query through the database driver
		/// </summary>
		/// <param name="Query"></param>
		/// <returns></returns>
		DataSet ExecuteQuery(string Query);
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

		/// <summary>
		/// Format the data for target table based on data type
		/// </summary>
		/// <param name="InData">Data to format</param>
		/// <param name="InContext">ITelemetryContext of the telemetry data</param>
		/// <returns></returns>
		IEnumerable<string> FormatDataForTable(Data InData, ITelemetryContext InContext);
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

		public DataSet ExecuteQuery(string SqlQuery)
		{
			return MySqlHelper.ExecuteDataset(Config.ConfigString, SqlQuery);
		}

		public bool Insert(string Table, IEnumerable<string> Columns, IEnumerable<IEnumerable<string>> Rows)
		{
			foreach (var Chunk in ChunkIt(Rows))
			{
				string SqlQuery = string.Format(
					"INSERT INTO `{0}`.{1} ({2}) VALUES {3}",
					Config.DatabaseName, Table, string.Join(", ", Columns), string.Join(", ", Chunk.Select(R => string.Format("({0})", string.Join(", ", R.Select(V => string.Format("'{0}'", V))))))
				);
				if(MySqlHelper.ExecuteScalar(Config.ConfigString, SqlQuery) == null)
				{
					return false;
				}
			}
			return true;
		}

		private IEnumerable<IEnumerable<T>> ChunkIt<T>(IEnumerable<T> ToChunk, int ChunkSize = 1000)
		{
			return ToChunk.Select((v, i) => new { Value = v, Index = i }).GroupBy(x => x.Index / ChunkSize).Select(g => g.Select(x => x.Value));
		}

		public bool SubmitDataItems(IEnumerable<Data> DataRows, ITelemetryContext Context)
		{
			if (Config is IMySQLConfig<Data> DataConfig)
			{
				if(!PreSubmitQuery(DataRows, Context))
				{
					Log.Error("Fail MySQL '{0}' pre-submit query.", Config.GetType().FullName);
					return false;
				}

				bool Success = Insert(
					DataConfig.GetTableName(),
					DataConfig.GetTableColumns(),
					DataRows.Select(D => DataConfig.FormatDataForTable(D, Context))
				);

				if(Success && !PostSubmitQuery(DataRows, Context))
				{
					Log.Error("Fail MySQL '{0}' post-submit query.", Config.GetType().FullName);
					return false;
				}

				return Success;
			}
			else
			{
				Log.Error("MySQL configuration '{0}' does not known how to handle {1}.", Config.GetType().FullName, typeof(Data).FullName);
			}

			return false;
		}

		public virtual bool PreSubmitQuery(IEnumerable<Data> DataRows, ITelemetryContext Context)
		{
			return Config.PreSubmitQuery(this, DataRows, Context);
		}
		public virtual bool PostSubmitQuery(IEnumerable<Data> DataRows, ITelemetryContext Context)
		{
			return Config.PostSubmitQuery(this, DataRows, Context);
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
		/// Override to add pre-submit query.
		/// </summary>
		/// <param name="Driver"></param>
		/// <param name="DataRows"></param>
		/// <param name="Context"></param>
		/// <returns></returns>
		public virtual bool PreSubmitQuery(IDatabaseDriver<Data> Driver, IEnumerable<Data> DataRows, ITelemetryContext Context)
		{
			return true;
		}
		/// <summary>
		/// Override to add post-submit query.
		/// </summary>
		/// <param name="Driver"></param>
		/// <param name="DataRows"></param>
		/// <param name="Context"></param>
		/// <returns></returns>
		public virtual bool PostSubmitQuery(IDatabaseDriver<Data> Driver, IEnumerable<Data> DataRows, ITelemetryContext Context)
		{
			return true;
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
		public abstract IEnumerable<string> FormatDataForTable(Data InData, ITelemetryContext InContext);

	}

	public class DatabaseConfigManager<Data> where Data : class
	{
		protected static IEnumerable<IDatabaseConfig<Data>> Configs;
		static DatabaseConfigManager()
		{
			Configs = Gauntlet.Utils.InterfaceHelpers.FindImplementations<IDatabaseConfig<Data>>(true);
		}
		public static IDatabaseConfig<Data> GetConfigByName(string Name)
		{
			return Configs.Where(C => string.Equals(C.GetType().Name, Name, StringComparison.OrdinalIgnoreCase)).FirstOrDefault();
		}
	}
}
