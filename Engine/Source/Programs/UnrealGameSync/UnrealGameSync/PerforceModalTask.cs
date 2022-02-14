// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	public class PerforceLoginException : Exception
	{
		public PerforceResponse<LoginRecord> Response { get; }

		public PerforceLoginException(PerforceResponse<LoginRecord> Response) : base($"Login failed: {Response}")
		{
			this.Response = Response;
		}
	}

	static class PerforceModalTask
	{
		public static ModalTask? Execute(IWin32Window? Owner, string Title, string Message, IPerforceSettings PerforceSettings, Func<IPerforceConnection, CancellationToken, Task> ExecuteAsync, ILogger Logger, ModalTaskFlags Flags = ModalTaskFlags.None)
		{
			Func<IPerforceConnection, CancellationToken, Task<int>> ExecuteTypedAsync = async (p, c) => { await ExecuteAsync(p, c); return 0; };
			return Execute(Owner, Title, Message, PerforceSettings, ExecuteTypedAsync, Logger, Flags);
		}

		public static ModalTask<T>? Execute<T>(IWin32Window? Owner, string Title, string Message, IPerforceSettings PerforceSettings, Func<IPerforceConnection, CancellationToken, Task<T>> ExecuteAsync, ILogger Logger, ModalTaskFlags Flags = ModalTaskFlags.None)
		{
			IPerforceConnection? Connection = null;
			try
			{
				Func<Task<IPerforceConnection>> ConnectAsync = async () =>
				{
					Connection ??= await PerforceConnection.CreateAsync(PerforceSettings, Logger);
					return Connection;
				};
				return ExecuteInternal(Owner, Title, Message, PerforceSettings, ConnectAsync, ExecuteAsync, Flags);
			}
			finally
			{
				Connection?.Dispose();
			}
		}

		public static ModalTask? Execute(IWin32Window? Owner, string Title, string Message, IPerforceConnection PerforceConnection, Func<IPerforceConnection, CancellationToken, Task> ExecuteAsync, ILogger Logger, ModalTaskFlags Flags = ModalTaskFlags.None)
		{
			Func<IPerforceConnection, CancellationToken, Task<int>> ExecuteTypedAsync = async (p, c) => { await ExecuteAsync(p, c); return 0; };
			return Execute(Owner, Title, Message, PerforceConnection, ExecuteTypedAsync, Logger, Flags);
		}

		public static ModalTask<T>? Execute<T>(IWin32Window? Owner, string Title, string Message, IPerforceConnection PerforceConnection, Func<IPerforceConnection, CancellationToken, Task<T>> ExecuteAsync, ILogger Logger, ModalTaskFlags Flags = ModalTaskFlags.None)
		{
			return ExecuteInternal(Owner, Title, Message, PerforceConnection.Settings, () => Task.FromResult(PerforceConnection), ExecuteAsync, Flags);
		}

		private static ModalTask<T>? ExecuteInternal<T>(IWin32Window? Owner, string Title, string Message, IPerforceSettings PerforceSettings, Func<Task<IPerforceConnection>> ConnectAsync, Func<IPerforceConnection, CancellationToken, Task<T>> ExecuteAsync, ModalTaskFlags Flags = ModalTaskFlags.None)
		{
			string? Password = PerforceSettings.Password;
			for(;;)
			{
				Func<CancellationToken, Task<T>> RunAsync = CancellationToken => LoginAndExecuteAsync(Password, ConnectAsync, ExecuteAsync, CancellationToken);

				ModalTask<T>? Result = ModalTask.Execute(Owner, Title, Message, RunAsync, ModalTaskFlags.Quiet);
				if (Result != null && Result.Failed && (Flags & ModalTaskFlags.Quiet) == 0)
				{
					if (Result.Exception is PerforceLoginException)
					{
						string PasswordPrompt;
						if (String.IsNullOrEmpty(Password))
						{
							PasswordPrompt = $"Enter the password for user '{PerforceSettings.UserName}' on server '{PerforceSettings.ServerAndPort}'.";
						}
						else
						{
							PasswordPrompt = $"Authentication failed. Enter the password for user '{PerforceSettings.UserName}' on server '{PerforceSettings.ServerAndPort}'.";
						}

						PasswordWindow PasswordWindow = new PasswordWindow(PasswordPrompt, Password ?? String.Empty);
						if (Owner == null)
						{
							PasswordWindow.ShowInTaskbar = true;
							PasswordWindow.StartPosition = FormStartPosition.CenterScreen;
						}
						if (PasswordWindow.ShowDialog(Owner) != DialogResult.OK)
						{
							return null;
						}

						Password = PasswordWindow.Password;
						continue;
					}
					MessageBox.Show(Owner, Result.Error, Title, MessageBoxButtons.OK);
				}

				return Result;
			}
		}

		private static async Task<T> LoginAndExecuteAsync<T>(string? Password, Func<Task<IPerforceConnection>> ConnectAsync, Func<IPerforceConnection, CancellationToken, Task<T>> ExecuteAsync, CancellationToken CancellationToken)
		{
			IPerforceConnection Perforce = await ConnectAsync();

			// If we've got a password, execute the login command
			PerforceResponse<LoginRecord> Response;
			if (String.IsNullOrEmpty(Password))
			{
				Response = await Perforce.TryGetLoginStateAsync(CancellationToken);
			}
			else
			{
				Response = await Perforce.TryLoginAsync(Password, CancellationToken);
			}

			if (!Response.Succeeded)
			{
				throw new PerforceLoginException(Response);
			}

			// Execute the inner task
			return await ExecuteAsync(Perforce, CancellationToken);
		}
	}
}
