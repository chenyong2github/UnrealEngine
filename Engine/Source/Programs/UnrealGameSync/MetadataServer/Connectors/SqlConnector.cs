// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;
using System.Data.SqlClient;
using System.Data.SQLite;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using MetadataServer.Models;

namespace MetadataServer.Connectors
{
	public static class SqlConnector
	{
		private static string ConnectionString = System.Configuration.ConfigurationManager.ConnectionStrings["ConnectionString"].ConnectionString;
		public static LatestData GetLastIds(string Project = null)
		{
			// Get ids going back 432 builds for the project being asked for
			// Do this by grouping by ChangeNumber to get unique entries, then take the 432nd id
			long LastEventId = 0;
			long LastCommentId = 0;
			long LastBuildId = 0;
			string ProjectLikeString = "%" + (Project == null ? String.Empty : GetProjectStream(Project)) + "%";
			using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString))
			{
				Connection.Open();
				using (SQLiteCommand Command = new SQLiteCommand("SELECT Id FROM [UserVotes] WHERE Project LIKE @param1 GROUP BY Changelist ORDER BY Changelist DESC LIMIT 1 OFFSET 432;", Connection))
				{
					Command.Parameters.AddWithValue("@param1", ProjectLikeString);
					using (SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while (Reader.Read())
						{
							LastEventId = Reader.GetInt64(0);
							break;
						}
					}
				}
				using (SQLiteCommand Command = new SQLiteCommand("SELECT Id FROM [Comments] WHERE Project LIKE @param1 GROUP BY ChangeNumber ORDER BY ChangeNumber DESC LIMIT 1 OFFSET 432;", Connection))
				{
					Command.Parameters.AddWithValue("@param1", ProjectLikeString);
					using (SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while (Reader.Read())
						{
							LastCommentId = Reader.GetInt32(0);
							break;
						}
					}
				}
				using (SQLiteCommand Command = new SQLiteCommand("SELECT Id FROM [CIS] WHERE Project LIKE @param1 GROUP BY ChangeNumber ORDER BY ChangeNumber DESC LIMIT 1 OFFSET 432", Connection))
				{
					Command.Parameters.AddWithValue("@param1", ProjectLikeString);
					using (SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while (Reader.Read())
						{
							LastBuildId = Reader.GetInt32(0);
							break;
						}
					}
				}
			}
			return new LatestData { LastBuildId = LastBuildId, LastCommentId = LastCommentId, LastEventId = LastEventId };
		}
		public static List<EventData> GetUserVotes(string Project, long LastEventId)
		{
			List<EventData> ReturnedEvents = new List<EventData>();
			string ProjectLikeString = "%" + (Project == null ? String.Empty : GetProjectStream(Project)) + "%";
			using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString))
			{
				Connection.Open();
				using (SQLiteCommand Command = new SQLiteCommand("SELECT Id, Changelist, UserName, Verdict, Project FROM [UserVotes] WHERE Id > @param1 AND Project LIKE @param2 ORDER BY Id", Connection))
				{
					Command.Parameters.AddWithValue("@param1", LastEventId);
					Command.Parameters.AddWithValue("@param2", ProjectLikeString);
					using (SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while (Reader.Read())
						{
							EventData Review = new EventData();
							Review.Id = Reader.GetInt64(0);
							Review.Change = Reader.GetInt32(1);
							Review.UserName = Reader.GetString(2);
							Review.Project = Reader.IsDBNull(4) ? null : Reader.GetString(4);
							if (Enum.TryParse(Reader.GetString(3), out Review.Type))
							{
								if (Review.Project == null || String.Compare(Review.Project, Project, true) == 0)
								{
									ReturnedEvents.Add(Review);
								}
							}
						}
					}
				}
			}
			return ReturnedEvents;
		}
		public static List<CommentData> GetComments(string Project, long LastCommentId)
		{
			List<CommentData> ReturnedComments = new List<CommentData>();
			string ProjectLikeString = "%" + (Project == null ? String.Empty : GetProjectStream(Project)) + "%";
			using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString))
			{
				Connection.Open();
				using (SQLiteCommand Command = new SQLiteCommand("SELECT Id, ChangeNumber, UserName, Text, Project FROM [Comments] WHERE Id > @param1 AND Project LIKE @param2 ORDER BY Id", Connection))
				{
					Command.Parameters.AddWithValue("@param1", LastCommentId);
					Command.Parameters.AddWithValue("@param2", ProjectLikeString);
					using (SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while (Reader.Read())
						{
							CommentData Comment = new CommentData();
							Comment.Id = Reader.GetInt32(0);
							Comment.ChangeNumber = Reader.GetInt32(1);
							Comment.UserName = Reader.GetString(2);
							Comment.Text = Reader.GetString(3);
							Comment.Project = Reader.GetString(4);
							if (Comment.Project == null || String.Compare(Comment.Project, Project, true) == 0)
							{
								ReturnedComments.Add(Comment);
							}
						}
					}
				}
			}
			return ReturnedComments;
		}

		public static List<BuildData> GetBuilds(string Project, long LastBuildId)
		{
			List<BuildData> ReturnedBuilds = new List<BuildData>();
			string ProjectLikeString = "%" + (Project == null ? String.Empty : GetProjectStream(Project)) + "%";
			using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString))
			{
				Connection.Open();
				using (SQLiteCommand Command = new SQLiteCommand("SELECT Id, ChangeNumber, BuildType, Result, Url, Project FROM [CIS] WHERE Id > @param1 AND Project LIKE @param2 ORDER BY Id", Connection))
				{
					Command.Parameters.AddWithValue("@param1", LastBuildId);
					Command.Parameters.AddWithValue("@param2", ProjectLikeString);
					using (SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while (Reader.Read())
						{
							BuildData Build = new BuildData();
							Build.Id = Reader.GetInt32(0);
							Build.ChangeNumber = Reader.GetInt32(1);
							Build.BuildType = Reader.GetString(2).TrimEnd();
							if (Enum.TryParse(Reader.GetString(3).TrimEnd(), true, out Build.Result))
							{
								Build.Url = Reader.GetString(4);
								Build.Project = Reader.IsDBNull(5) ? null : Reader.GetString(5);
								if (Build.Project == null || String.Compare(Build.Project, Project, true) == 0 || MatchesWildcard(Build.Project, Project))
								{
									ReturnedBuilds.Add(Build);
								}
							}
							LastBuildId = Math.Max(LastBuildId, Build.Id);
						}
					}
				}
			}
			return ReturnedBuilds;
		}

		public static List<TelemetryErrorData> GetErrorData(int Records)
		{
			List<TelemetryErrorData> ReturnedErrors = new List<TelemetryErrorData>();
			using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString))
			{
				Connection.Open();
				using (SQLiteCommand Command = new SQLiteCommand("SELECT Id, Type, Text, UserName, Project, Timestamp, Version, IpAddress FROM [Errors] ORDER BY Id DESC LIMIT @param1", Connection))
				{
					Command.Parameters.AddWithValue("@param1", Records);
					using (SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while (Reader.Read())
						{
							TelemetryErrorData Error = new TelemetryErrorData();
							Error.Id = Reader.GetInt32(0);
							Enum.TryParse(Reader.GetString(1), true, out Error.Type);
							Error.Text = Reader.GetString(2);
							Error.UserName = Reader.GetString(3);
							Error.Project = Reader.IsDBNull(4) ? null : Reader.GetString(4);
							Error.Timestamp = Reader.GetDateTime(5);
							Error.Version = Reader.GetString(6);
							Error.IpAddress = Reader.GetString(7);
							ReturnedErrors.Add(Error);
						}
					}
				}
			}
			return ReturnedErrors;
		}


		public static void PostBuild(BuildData Build)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString))
			{
				Connection.Open();
				using (SQLiteCommand Command = new SQLiteCommand("INSERT INTO [CIS] (ChangeNumber, BuildType, Result, URL, Project, ArchivePath) VALUES (@ChangeNumber, @BuildType, @Result, @URL, @Project, @ArchivePath)", Connection))
				{
					Command.Parameters.AddWithValue("@ChangeNumber", Build.ChangeNumber);
					Command.Parameters.AddWithValue("@BuildType", Build.BuildType);
					Command.Parameters.AddWithValue("@Result", Build.Result);
					Command.Parameters.AddWithValue("@URL", Build.Url);
					Command.Parameters.AddWithValue("@Project", Build.Project);
					Command.Parameters.AddWithValue("@ArchivePath", Build.ArchivePath);
					Command.ExecuteNonQuery();
				}
			}
		}

		public static void PostEvent(EventData Event)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString))
			{
				Connection.Open();
				using (SQLiteCommand Command = new SQLiteCommand("INSERT INTO [UserVotes] (Changelist, UserName, Verdict, Project) VALUES (@Changelist, @UserName, @Verdict, @Project)", Connection))
				{
					Command.Parameters.AddWithValue("@Changelist", Event.Change);
					Command.Parameters.AddWithValue("@UserName", Event.UserName.ToString());
					Command.Parameters.AddWithValue("@Verdict", Event.Type.ToString());
					Command.Parameters.AddWithValue("@Project", Event.Project);
					Command.ExecuteNonQuery();
				}
			}
		}

		public static void PostComment(CommentData Comment)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString))
			{
				Connection.Open();
				using (SQLiteCommand Command = new SQLiteCommand("INSERT INTO [Comments] (ChangeNumber, UserName, Text, Project) VALUES (@ChangeNumber, @UserName, @Text, @Project)", Connection))
				{
					Command.Parameters.AddWithValue("@ChangeNumber", Comment.ChangeNumber);
					Command.Parameters.AddWithValue("@UserName", Comment.UserName);
					Command.Parameters.AddWithValue("@Text", Comment.Text);
					Command.Parameters.AddWithValue("@Project", Comment.Project);
					Command.ExecuteNonQuery();
				}
			}
		}

		public static void PostTelemetryData(TelemetryTimingData Data, string Version, string IpAddress)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString))
			{
				Connection.Open();
				using (SQLiteCommand Command = new SQLiteCommand("INSERT INTO [Telemetry.v2] (Action, Result, UserName, Project, Timestamp, Duration, Version, IpAddress) VALUES (@Action, @Result, @UserName, @Project, @Timestamp, @Duration, @Version, @IpAddress)", Connection))
				{
					Command.Parameters.AddWithValue("@Action", Data.Action);
					Command.Parameters.AddWithValue("@Result", Data.Result);
					Command.Parameters.AddWithValue("@UserName", Data.UserName);
					Command.Parameters.AddWithValue("@Project", Data.Project);
					Command.Parameters.AddWithValue("@Timestamp", Data.Timestamp);
					Command.Parameters.AddWithValue("@Duration", Data.Duration);
					Command.Parameters.AddWithValue("@Version", Version);
					Command.Parameters.AddWithValue("@IPAddress", IpAddress);
					Command.ExecuteNonQuery();
				}
			}
		}

		public static void PostErrorData(TelemetryErrorData Data, string Version, string IpAddress)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(ConnectionString))
			{
				Connection.Open();
				using (SQLiteCommand Command = new SQLiteCommand("INSERT INTO [Errors] (Type, Text, UserName, Project, Timestamp, Version, IpAddress) VALUES (@Type, @Text, @UserName, @Project, @Timestamp, @Version, @IpAddress)", Connection))
				{
					Command.Parameters.AddWithValue("@Type", Data.Type.ToString());
					Command.Parameters.AddWithValue("@Text", Data.Text);
					Command.Parameters.AddWithValue("@UserName", Data.UserName);
					if (Data.Project == null)
					{
						Command.Parameters.AddWithValue("@Project", DBNull.Value);
					}
					else
					{
						Command.Parameters.AddWithValue("@Project", Data.Project);
					}
					Command.Parameters.AddWithValue("@Timestamp", Data.Timestamp);
					Command.Parameters.AddWithValue("@Version", Version);
					Command.Parameters.AddWithValue("@IPAddress", IpAddress);
					Command.ExecuteNonQuery();
				}
			}
		}
		private static string GetProjectStream(string Project)
		{
			// Get first two fragments of the p4 path.  If it doesn't work, just return back the project.
			Regex StreamPattern = new Regex("(\\/\\/[a-zA-Z0-9\\.\\-_]{1,}\\/[a-zA-Z0-9\\.\\-_]{1,})");
			Match StreamMatch = StreamPattern.Match(Project);
			if(StreamMatch.Success)
			{
				return StreamMatch.Groups[1].Value;
			}
			return Project;
		}
		private static bool MatchesWildcard(string Wildcard, string Project)
		{
			return Wildcard.EndsWith("...") && Project.StartsWith(Wildcard.Substring(0, Wildcard.Length - 4), StringComparison.InvariantCultureIgnoreCase);
		}

		private static string NormalizeUserName(string UserName)
		{
			return UserName.ToUpperInvariant();
		}

		public static long FindOrAddUserId(string Name)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();
				return FindOrAddUserId(Name, Connection);
			}
		}

		private static long FindOrAddUserId(string Name, SQLiteConnection Connection)
		{
			if(Name.Length == 0)
			{
				return -1;
			}

			string NormalizedName = NormalizeUserName(Name);

			using (SQLiteCommand Command = new SQLiteCommand("SELECT [Id] FROM [Users] WHERE Name = @Name", Connection))
			{
				Command.Parameters.AddWithValue("@Name", NormalizedName);
				object UserId = Command.ExecuteScalar();
				if(UserId != null)
				{
					return (long)UserId;
				}
			}

			using (SQLiteCommand Command = new SQLiteCommand("INSERT OR IGNORE INTO [Users] (Name) VALUES (@Name); SELECT [Id] FROM [Users] WHERE Name = @Name", Connection))
			{
				Command.Parameters.AddWithValue("@Name", NormalizedName);
				object UserId = Command.ExecuteScalar();
				return (long)UserId;
			}
		}

		const int IssueSummaryMaxLength = 200;
		const int IssueDetailsMaxLength = 1000;

		public static long AddIssue(IssueData Issue)
		{
			long IssueId;
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();

				using (SQLiteCommand Command = new SQLiteCommand("INSERT INTO [Issues] (Project, Summary, Details, OwnerId, CreatedAt, FixChange) VALUES (@Project, @Summary, @Details, @OwnerId, DATETIME('now'), 0)", Connection))
				{
					Command.Parameters.AddWithValue("@Project", Issue.Project);
					Command.Parameters.AddWithValue("@Summary", SanitizeText(Issue.Summary, IssueSummaryMaxLength));
					Command.Parameters.AddWithValue("@Details", SanitizeText(Issue.Details, IssueDetailsMaxLength));
					if (Issue.Owner != null)
					{
						Command.Parameters.AddWithValue("OwnerId", FindOrAddUserId(Issue.Owner, Connection));
					}
					else
					{
						Command.Parameters.AddWithValue("OwnerId", null);
					}
					Command.ExecuteNonQuery();

					IssueId = Connection.LastInsertRowId;
				}
			}
			return IssueId;
		}

		public static IssueData GetIssue(long IssueId)
		{
			List<IssueData> Issues = GetIssuesInternal(IssueId, null, true, -1);
			if(Issues.Count == 0)
			{
				return null;
			}
			else
			{
				return Issues[0];
			}
		}

		public static List<IssueData> GetIssues(bool IncludeResolved, int NumResults)
		{
			return GetIssuesInternal(-1, null, IncludeResolved, NumResults);
		}

		public static List<IssueData> GetIssues(string UserName)
		{
			return GetIssuesInternal(-1, UserName, false, -1);
		}

		private static List<IssueData> GetIssuesInternal(long IssueId, string UserName, bool IncludeResolved, int NumResults)
		{
			List<IssueData> Issues = new List<IssueData>();
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();

				long UserId = -1;
				if(UserName != null)
				{
					UserId = FindOrAddUserId(UserName);
				}

				StringBuilder CommandBuilder = new StringBuilder();
				CommandBuilder.Append("SELECT");
				CommandBuilder.Append(" Issues.Id, Issues.CreatedAt, DATETIME('now'), Issues.Project, Issues.Summary, Issues.Details, OwnerUsers.Name, NominatedByUsers.Name, Issues.AcknowledgedAt, Issues.FixChange, Issues.ResolvedAt");
				if(UserName != null)
				{
					CommandBuilder.Append(", IssueWatchers.UserId");
				}
				CommandBuilder.Append(" FROM [Issues]");
				CommandBuilder.Append(" LEFT JOIN [Users] AS [OwnerUsers] ON OwnerUsers.Id = Issues.OwnerId");
				CommandBuilder.Append(" LEFT JOIN [Users] AS [NominatedByUsers] ON NominatedByUsers.Id = Issues.NominatedById");
				if(UserName != null)
				{
					CommandBuilder.Append(" LEFT JOIN [IssueWatchers] ON IssueWatchers.IssueId = Issues.Id AND IssueWatchers.UserId = @UserId");
				}
				if(IssueId != -1)
				{
					CommandBuilder.Append(" WHERE Issues.Id = @IssueId");
				}
				else if(!IncludeResolved)
				{
					CommandBuilder.Append(" WHERE Issues.ResolvedAt IS NULL");
				}
				if(NumResults > 0)
				{
					CommandBuilder.AppendFormat(" ORDER BY Issues.Id DESC LIMIT {0}", NumResults);
				}

				using (SQLiteCommand Command = new SQLiteCommand(CommandBuilder.ToString(), Connection))
				{
					if(IssueId != -1)
					{
						Command.Parameters.AddWithValue("@IssueId", IssueId);
					}
					if(UserName != null)
					{
						Command.Parameters.AddWithValue("@UserId", UserId);
					}

					using(SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while(Reader.Read())
						{
							IssueData Issue = new IssueData();
							Issue.Id = Reader.GetInt64(0);
							Issue.CreatedAt = Reader.GetDateTime(1);
							Issue.RetrievedAt = Reader.GetDateTime(2);
							Issue.Project = Reader.GetString(3);
							Issue.Summary = Reader.GetString(4);
							Issue.Details = Reader.GetString(5);
							Issue.Owner = Reader.IsDBNull(6)? null : Reader.GetString(6);
							Issue.NominatedBy = Reader.IsDBNull(7)? null : Reader.GetString(7);
							Issue.AcknowledgedAt = Reader.IsDBNull(8)? (DateTime?)null : Reader.GetDateTime(8);
							Issue.FixChange = Reader.GetInt32(9);
							Issue.ResolvedAt = Reader.IsDBNull(10)? (DateTime?)null : Reader.GetDateTime(10);
							if(UserName != null)
							{
								Issue.bNotify = !Reader.IsDBNull(11);
							}
							Issues.Add(Issue);
						}
					}
				}
			}
			return Issues;
		}

		public static void UpdateIssue(long IssueId, IssueUpdateData Issue)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();

				using (SQLiteCommand Command = new SQLiteCommand(Connection))
				{
					List<string> Columns = new List<string>();
					List<string> Values = new List<string>();
					if(Issue.Summary != null)
					{
						Columns.Add("Summary");
						Values.Add("@Summary");
						Command.Parameters.AddWithValue("@Summary", SanitizeText(Issue.Summary, IssueSummaryMaxLength));
					}
					if(Issue.Details != null)
					{
						Columns.Add("Details");
						Values.Add("@Details");
						Command.Parameters.AddWithValue("@Details", SanitizeText(Issue.Details, IssueDetailsMaxLength));
					}
					if (Issue.Owner != null)
					{
						Columns.Add("OwnerId");
						Values.Add("@OwnerId");
						Command.Parameters.AddWithValue("OwnerId", FindOrAddUserId(Issue.Owner, Connection));
					}
					if(Issue.NominatedBy != null)
					{
						Columns.Add("NominatedById");
						Values.Add("@NominatedById");
						Command.Parameters.AddWithValue("NominatedById", FindOrAddUserId(Issue.NominatedBy, Connection));
					}
					if(Issue.Acknowledged.HasValue)
					{
						Columns.Add("AcknowledgedAt");
						Values.Add(Issue.Acknowledged.Value? "DATETIME('now')" : "NULL");
					}
					if(Issue.FixChange.HasValue)
					{
						Columns.Add("FixChange");
						Values.Add("@FixChange");
						Command.Parameters.AddWithValue("FixChange", Issue.FixChange.Value);
					}
					if(Issue.Resolved.HasValue)
					{
						Columns.Add("ResolvedAt");
						Values.Add(Issue.Resolved.Value? "DATETIME('now')" : "NULL");
					}

					Command.CommandText = String.Format("UPDATE [Issues] SET ({0}) = ({1}) WHERE Id = @IssueId", String.Join(", ", Columns), String.Join(", ", Values));
					Command.Parameters.AddWithValue("@IssueId", IssueId);
					Command.ExecuteNonQuery();
				}
			}
		}

		public static string SanitizeText(string Text, int Length)
		{
			if(Text.Length > Length)
			{
				Text = Text.Substring(0, Length - 3).TrimEnd() + "...";
			}
			return Text;
		}

		public static void AddWatcher(long IssueId, string UserName)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();

				long UserId = FindOrAddUserId(UserName, Connection);

				using(SQLiteCommand Command = new SQLiteCommand("INSERT OR IGNORE INTO [IssueWatchers] (IssueId, UserId) VALUES (@IssueId, @UserId)", Connection))
				{
					Command.Parameters.AddWithValue("@IssueId", IssueId);
					Command.Parameters.AddWithValue("@UserId", UserId);
					Command.ExecuteNonQuery();
				}
			}
		}

		public static List<string> GetWatchers(long IssueId)
		{
			List<string> Watchers = new List<string>();
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();

				StringBuilder CommandBuilder = new StringBuilder();
				CommandBuilder.Append("SELECT Users.Name FROM [IssueWatchers]");
				CommandBuilder.Append(" LEFT JOIN [Users] ON IssueWatchers.UserId = Users.Id");
				CommandBuilder.Append(" WHERE IssueWatchers.IssueId = @IssueId");

				using(SQLiteCommand Command = new SQLiteCommand(CommandBuilder.ToString(), Connection))
				{
					Command.Parameters.AddWithValue("@IssueId", IssueId);
					using(SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while(Reader.Read())
						{
							Watchers.Add(Reader.GetString(0));
						}
					}
				}
			}
			return Watchers;
		}

		public static void RemoveWatcher(long IssueId, string UserName)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();

				long UserId = FindOrAddUserId(UserName, Connection);

				using(SQLiteCommand Command = new SQLiteCommand("DELETE FROM [IssueWatchers] WHERE IssueId = @IssueId AND UserId = @UserId", Connection))
				{
					Command.Parameters.AddWithValue("@IssueId", IssueId);
					Command.Parameters.AddWithValue("@UserId", UserId);
					Command.ExecuteNonQuery();
				}
			}
		}

		public static long AddBuild(long IssueId, string Stream, int Change, string Name, string Url, int Outcome)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();

				using (SQLiteCommand Command = new SQLiteCommand("INSERT INTO [IssueBuilds] (IssueId, Stream, Change, Name, Url, Outcome) VALUES (@IssueId, @Stream, @Change, @Name, @Url, @Outcome)", Connection))
				{
					Command.Parameters.AddWithValue("@IssueId", IssueId);
					Command.Parameters.AddWithValue("@Stream", Stream);
					Command.Parameters.AddWithValue("@Change", Change);
					Command.Parameters.AddWithValue("@Name", Name);
					Command.Parameters.AddWithValue("@Url", Url);
					Command.Parameters.AddWithValue("@Outcome", Outcome);
					Command.ExecuteNonQuery();

					return Connection.LastInsertRowId;
				}
			}
		}

		public static List<IssueBuildData> GetBuilds(long IssueId)
		{
			List<IssueBuildData> Builds = new List<IssueBuildData>();
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();

				using(SQLiteCommand Command = new SQLiteCommand("SELECT IssueBuilds.Id, IssueBuilds.Stream, IssueBuilds.Change, IssueBuilds.Name, IssueBuilds.Url, IssueBuilds.Outcome FROM [IssueBuilds] WHERE IssueBuilds.IssueId = @IssueId", Connection))
				{
					Command.Parameters.AddWithValue("@IssueId", IssueId);
					using(SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while(Reader.Read())
						{
							long Id = Reader.GetInt64(0);
							string Stream = Reader.GetString(1);
							int Change = Reader.GetInt32(2);
							string Name = Reader.GetString(3);
							string Url = Reader.GetString(4);
							int Outcome = Reader.GetInt32(5);
							Builds.Add(new IssueBuildData { Id = Id, Stream = Stream, Change = Change, Name = Name, Url = Url, Outcome = Outcome });
						}
					}
				}
			}
			return Builds;
		}

		public static IssueBuildData GetBuild(long BuildId)
		{
			IssueBuildData Build = null;
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();

				using(SQLiteCommand Command = new SQLiteCommand("SELECT IssueBuilds.Id, IssueBuilds.Stream, IssueBuilds.Change, IssueBuilds.Name, IssueBuilds.Url, IssueBuilds.Outcome FROM [IssueBuilds] WHERE IssueBuilds.Id = @BuildId", Connection))
				{
					Command.Parameters.AddWithValue("@BuildId", BuildId);
					using(SQLiteDataReader Reader = Command.ExecuteReader())
					{
						while(Reader.Read())
						{
							long Id = Reader.GetInt64(0);
							string Stream = Reader.GetString(1);
							int Change = Reader.GetInt32(2);
							string Name = Reader.GetString(3);
							string Url = Reader.GetString(4);
							int Outcome = Reader.GetInt32(5);

							Build = new IssueBuildData { Id = Id, Stream = Stream, Change = Change, Name = Name, Url = Url, Outcome = Outcome };
						}
					}
				}
			}
			return Build;
		}

		public static void UpdateBuild(long BuildId, int Outcome)
		{
			using (SQLiteConnection Connection = new SQLiteConnection(SqlConnector.ConnectionString))
			{
				Connection.Open();

				using (SQLiteCommand Command = new SQLiteCommand("UPDATE [IssueBuilds] SET (Outcome) = (@Outcome) WHERE Id = @BuildId", Connection))
				{
					Command.Parameters.AddWithValue("@BuildId", BuildId);
					Command.Parameters.AddWithValue("@Outcome", Outcome);
					Command.ExecuteNonQuery();
				}
			}
		}
	}
}