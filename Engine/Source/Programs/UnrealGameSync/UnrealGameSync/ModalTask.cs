// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	[Flags]
	enum ModalTaskFlags
	{
		None = 0,
		Quiet = 1,
	}

	class ModalTask
	{
		public Task Task { get; }

		public bool Failed => Task.IsFaulted;
		public bool Succeeded => Task.IsCompletedSuccessfully;

		public Exception? Exception => Task.Exception?.InnerException;

		public ModalTask(Task Task)
		{
			this.Task = Task;
		}

		public string Error
		{
			get
			{
				if (Succeeded)
				{
					return "Succeeded.";
				}

				Exception? Ex = Exception;
				if (Ex == null)
				{
					return "Failed.";
				}
				else if (Ex is UserErrorException UserEx)
				{
					return UserEx.Message;
				}
				else
				{
					return $"Unhandled exception ({Ex.Message})";
				}
			}
		}

		public static ModalTask? Execute(IWin32Window? Owner, string Title, string Message, Func<CancellationToken, Task> TaskFunc, ModalTaskFlags Flags = ModalTaskFlags.None)
		{
			Func<CancellationToken, Task<int>> TypedTaskFunc = async x => { await TaskFunc(x); return 0; };
			return Execute(Owner, Title, Message, TypedTaskFunc, Flags);
		}

		public static ModalTask<T>? Execute<T>(IWin32Window? Owner, string Title, string Message, Func<CancellationToken, Task<T>> TaskFunc, ModalTaskFlags Flags = ModalTaskFlags.None)
		{
			using (CancellationTokenSource CancellationSource = new CancellationTokenSource())
			{
				Task<T> BackgroundTask = Task.Run(() => TaskFunc(CancellationSource.Token));

				ModalTaskWindow Window = new ModalTaskWindow(Title, Message, (Owner == null) ? FormStartPosition.CenterScreen : FormStartPosition.CenterParent, BackgroundTask, CancellationSource);
				if(Owner == null)
				{
					Window.ShowInTaskbar = true;
				}
				Window.ShowDialog(Owner);

				if (BackgroundTask.IsCanceled || (BackgroundTask.Exception != null && BackgroundTask.Exception.InnerException is OperationCanceledException))
				{
					return null;
				}

				ModalTask<T> Result = new ModalTask<T>(BackgroundTask);
				if (Result.Failed && (Flags & ModalTaskFlags.Quiet) == 0)
				{
					MessageBox.Show(Owner, Result.Error, Title);
				}
				return Result;
			}
		}
	}

	class ModalTask<T> : ModalTask
	{
		public new Task<T> Task => (Task<T>)base.Task;

		public T Result => Task.Result;

		public ModalTask(Task<T> Task) : base(Task)
		{
		}
	}
}
