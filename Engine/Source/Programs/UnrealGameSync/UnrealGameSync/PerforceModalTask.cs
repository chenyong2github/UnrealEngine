// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	interface IPerforceModalTask
	{
		bool Run(PerforceConnection Perforce, TextWriter Log, out string ErrorMessage);
	}

	class PerforceModalTask : IModalTask
	{
		public PerforceConnection Perforce;
		public string Password;
		public LoginResult LoginResult;
		public IPerforceModalTask InnerTask;
		public TextWriter Log;

		public PerforceModalTask(PerforceConnection Perforce, IPerforceModalTask InnerTask, TextWriter Log)
		{
			this.Perforce = Perforce;
			this.InnerTask = InnerTask;
			this.Log = Log;
		}

		public bool Run(out string ErrorMessage)
		{
			// Set the default login state to failed
			LoginResult = LoginResult.Failed;

			// If we've got a password, execute the login command
			if(Password != null)
			{
				string PasswordErrorMessage;
				LoginResult = Perforce.Login(Password, out PasswordErrorMessage, Log);
				if(LoginResult != LoginResult.Succeded)
				{
					Log.WriteLine(PasswordErrorMessage);
					ErrorMessage = String.Format("Unable to login: {0}", PasswordErrorMessage);
					return false;
				}
			}

			// Check that we're logged in
			bool bLoggedIn;
			if(!Perforce.GetLoggedInState(out bLoggedIn, Log))
			{
				ErrorMessage = "Unable to get login status.";
				return false;
			}
			if(!bLoggedIn)
			{
				LoginResult = LoginResult.MissingPassword;
				ErrorMessage = "User is not logged in to Perforce.";
				Log.WriteLine(ErrorMessage);
				return false;
			}

			// Execute the inner task
			LoginResult = LoginResult.Succeded;
			return InnerTask.Run(Perforce, Log, out ErrorMessage);
		}

		public static ModalTaskResult Execute(IWin32Window Owner, PerforceConnection Perforce, IPerforceModalTask PerforceTask, string Title, string Message, TextWriter Log, out string ErrorMessage)
		{
			PerforceModalTask Task = new PerforceModalTask(Perforce, PerforceTask, Log);
			for(;;)
			{
				string TaskErrorMessage;
				ModalTaskResult TaskResult = ModalTask.Execute(Owner, Task, Title, Message, out TaskErrorMessage);

				if(Task.LoginResult == LoginResult.Succeded)
				{
					ErrorMessage = TaskErrorMessage;
					return TaskResult;
				}
				else if(Task.LoginResult == LoginResult.MissingPassword)
				{
					PasswordWindow PasswordWindow = new PasswordWindow(String.Format("Enter the password for user '{0}' on server '{1}'.", Perforce.UserName, Perforce.ServerAndPort), Task.Password);
					if(Owner == null)
					{
						PasswordWindow.StartPosition = FormStartPosition.CenterScreen;
					}
					if(PasswordWindow.ShowDialog() != DialogResult.OK)
					{
						ErrorMessage = null;
						return ModalTaskResult.Aborted;
					}
					Task.Password = PasswordWindow.Password;
				}
				else if(Task.LoginResult == LoginResult.IncorrectPassword)
				{
					PasswordWindow PasswordWindow = new PasswordWindow(String.Format("Authentication failed. Enter the password for user '{0}' on server '{1}'.", Perforce.UserName, Perforce.ServerAndPort), Task.Password);
					if (Owner == null)
					{
						PasswordWindow.StartPosition = FormStartPosition.CenterScreen;
					}
					if (PasswordWindow.ShowDialog() != DialogResult.OK)
					{
						ErrorMessage = null;
						return ModalTaskResult.Aborted;
					}
					Task.Password = PasswordWindow.Password;
				}
				else
				{
					ErrorMessage = TaskErrorMessage;
					return ModalTaskResult.Failed;
				}
			}
		}

		public static bool ExecuteAndShowError(IWin32Window Owner, PerforceConnection Perforce, IPerforceModalTask Task, string Title, string Message, TextWriter Log)
		{
			string ErrorMessage;
			ModalTaskResult Result = Execute(Owner, Perforce, Task, Title, Message, Log, out ErrorMessage);
			if (Result != ModalTaskResult.Succeeded)
			{
				if (!String.IsNullOrEmpty(ErrorMessage))
				{
					MessageBox.Show(ErrorMessage);
				}
				return false;
			}
			return true;
		}
	}
}
