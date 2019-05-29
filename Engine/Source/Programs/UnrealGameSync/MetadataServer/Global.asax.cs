// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Data.SQLite;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Web;
using System.Web.Http;

namespace MetadataServer
{
	public class WebApiApplication : System.Web.HttpApplication
	{
        protected void Application_Start()
        {
            try
            {
                string ConnectionString = System.Configuration.ConfigurationManager.ConnectionStrings["ConnectionString"].ConnectionString;
                string FileName = Regex.Match(ConnectionString, "Data Source=(.+);.+").Groups[1].Value;
                Directory.CreateDirectory(new FileInfo(FileName).Directory.FullName);
                if (!File.Exists(FileName))
                {
                    SQLiteConnection.CreateFile(FileName);
                }
                // initialize the db if it doesn't exist
                using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString, true))
                {
                    Connection.Open();
                    using (SQLiteCommand Command = new SQLiteCommand(Properties.Resources.Setup, Connection))
                    {
                        Command.ExecuteNonQuery();
                    }

					////////////////////////////////////////////////////////
					// DB change for indexing of project IDs by foreign key
					////////////////////////////////////////////////////////
					
					// check if we've seeded the projects table already by looking at the raw number of rows
					// if we're a brand new database this will techincally trigger with a no-op every time the server is started
					// but will stop as soon as a badge is added.
					bool ProjectsTableSeeded = false;
					using (SQLiteCommand Command = new SQLiteCommand("SELECT COALESCE(MAX(id)+1, 0) FROM [Projects]", Connection))
					{
						using (SQLiteDataReader Reader = Command.ExecuteReader())
						{
							while (Reader.Read())
							{
								int ProjectRows = Reader.GetInt32(0);
								ProjectsTableSeeded = ProjectRows == 0 ? false : true;
							}
						}
					}
					// if not found, get all the existing projects fromm all the other tables.
					if (!ProjectsTableSeeded)
					{
						bool AlterTableRun = false;
						// check and see if we've run the alter already.
						using (SQLiteCommand Command = new SQLiteCommand("PRAGMA table_info(CIS)", Connection))
						{
							using (SQLiteDataReader Reader = Command.ExecuteReader())
							{
								while (Reader.Read())
								{
									string ColumnName = Reader.GetString(1);
									if(ColumnName == "ProjectID")
									{
										AlterTableRun = true;
									}
								}
							}
						}
						if (!AlterTableRun)
						{
							using (SQLiteCommand Command = new SQLiteCommand("ALTER TABLE [CIS] ADD COLUMN ProjectID INTEGER REFERENCES Projects(Id);" +
																			 "ALTER TABLE [Comments] ADD COLUMN ProjectID INTEGER REFERENCES Projects(Id);" +
																			 "ALTER TABLE [Errors] ADD COLUMN ProjectID INTEGER REFERENCES Projects(Id);" +
																			 "ALTER TABLE [Telemetry.v2] ADD COLUMN ProjectID INTEGER REFERENCES Projects(Id);" +
																			 "ALTER TABLE [UserVotes] ADD COLUMN ProjectID INTEGER REFERENCES Projects(Id);", Connection))
							{
								Command.ExecuteNonQuery();
							}

							// get all the distinct project names.
							Dictionary<string, long> Projects = new Dictionary<string, long>();
							using (SQLiteCommand Command = new SQLiteCommand("SELECT Project FROM [CIS] UNION Select Project FROM [Comments] WHERE Project IS NOT NULL " +
																			 "UNION " +
																			 "SELECT Project FROM [Comments] WHERE Project IS NOT NULL " +
																			 "UNION " +
																			 "SELECT Project FROM [Errors] WHERE Project IS NOT NULL " +
																			 "UNION " +
																			 "SELECT Project FROM [Telemetry.v2] WHERE Project is not NULL " +
																			 "UNION " +
																			 "SELECT Project FROM [UserVotes] WHERE Project is not NULL", Connection))
							{
								using (SQLiteDataReader Reader = Command.ExecuteReader())
								{
									while (Reader.Read())
									{
										object ProjectName = Reader.GetValue(0);
										if (ProjectName != null)
										{
											Projects.Add((string)ProjectName, 0);
										}

									}
								}
							}
							// insert all the projects into the new table
							using (SQLiteCommand Command = new SQLiteCommand(Connection))
							{
								using (SQLiteTransaction Transaction = Connection.BeginTransaction())
								{
									foreach (string ProjectName in Projects.Keys.ToList())
									{
										Command.CommandText = "INSERT INTO [Projects] (Name) VALUES (@ProjectName)";
										Command.Parameters.AddWithValue("@ProjectName", ProjectName);
										Command.ExecuteNonQuery();
										Projects[ProjectName] = Connection.LastInsertRowId;
									}
									Transaction.Commit();
								}
							}
							// update all the foreign keys
							using (SQLiteCommand Command = new SQLiteCommand(Connection))
							{
								using (SQLiteTransaction Transaction = Connection.BeginTransaction())
								{
									foreach (KeyValuePair<string, long> Project in Projects)
									{
										Command.CommandText = "UPDATE [CIS] SET ProjectID = @ProjectID WHERE Project = @ProjectName;" +
															  "UPDATE [Comments] SET ProjectID = @ProjectID WHERE Project = @ProjectName;" +
															  "UPDATE [Errors] SET ProjectID = @ProjectID WHERE Project = @ProjectName;" +
															  "UPDATE [Telemetry.v2] SET ProjectID = @ProjectID WHERE Project = @ProjectName;" +
															  "UPDATE [UserVotes] SET ProjectID = @ProjectID WHERE Project = @ProjectName;";
										Command.Parameters.AddWithValue("@ProjectName", Project.Key);
										Command.Parameters.AddWithValue("@ProjectID", Project.Value);
										Command.ExecuteNonQuery();
									}
									Transaction.Commit();
								}
							}
						}
					}
                }

            }
            catch (Exception)
            {
                HttpRuntime.UnloadAppDomain();
                return;
            }
            GlobalConfiguration.Configure(WebApiConfig.Register);
        }
	}
    
}
