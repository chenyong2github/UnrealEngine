// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using System.Collections.Generic;

namespace UnrealGameSync
{
	partial class IssueAlertWindow : Form
	{
		static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);

		const uint SWP_NOMOVE = 0x0002;
		const uint SWP_NOSIZE = 0x0001;
		const uint SWP_NOACTIVATE = 0x0010;

		[DllImport("user32.dll", SetLastError=true)]
		static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int Width, int Height, uint Flags);

		public readonly IssueMonitor IssueMonitor;

		public bool bIsWarning;

		public bool? bStrongAlert;

		public IssueAlertWindow(IssueMonitor IssueMonitor, IssueData Issue, IssueAlertReason Reason)
		{
			this.IssueMonitor = IssueMonitor;
			this.Issue = Issue;

			InitializeComponent();

			SetIssue(Issue, Reason);
		}

		public IssueData Issue
		{
			get;
			private set;
		}

		public IssueAlertReason Reason
		{
			get;
			private set;
		}

		protected override void OnPaintBackground(PaintEventArgs e)
		{
			base.OnPaintBackground(e);

			if(bStrongAlert ?? false)
			{
				Color StripeColor = bIsWarning? Color.FromArgb(216, 167, 64) : Color.FromArgb(200, 74, 49);//214, 69, 64);
				using (Brush StripeBrush = new SolidBrush(StripeColor))
				{
					e.Graphics.FillRectangle(StripeBrush, 0, 0, Bounds.Width, Bounds.Height);
				}
				using (Pen Pen = new Pen(Color.FromArgb(255, 255, 255)))
				{
					e.Graphics.DrawRectangle(Pen, 0, 0, Bounds.Width - 1, Bounds.Height - 1);
				}
			}
			else
			{
				Color StripeColor = bIsWarning? Color.FromArgb(216, 167, 64) : Color.FromArgb(214, 69, 64);

				Color BackgroundColor = Color.FromArgb(241, 236, 236);
				using(Brush SolidBrush = new SolidBrush(BackgroundColor))
				{
	//				e.Graphics.FillRectangle(SolidBrush, 0, 0, Bounds.Width - 1, Bounds.Height - 1);
				}

				Color BorderColor = StripeColor; 
				using(Pen Pen = new Pen(Color.FromArgb(128, 128, 128)))
				{
					e.Graphics.DrawRectangle(Pen, 0, 0, Bounds.Width - 1, Bounds.Height - 1);
				}

				using(Brush StripeBrush = new SolidBrush(StripeColor))
				{
					e.Graphics.FillRectangle(StripeBrush, 0, 0, /*6*/10 * e.Graphics.DpiX / 96.0f, Bounds.Height);
				}
			}
		}

		public void SetIssue(IssueData NewIssue, IssueAlertReason NewReason)
		{
			bool bNewStrongAlert = false;

			StringBuilder OwnerTextBuilder = new StringBuilder();
			if(NewIssue.Owner == null)
			{
				OwnerTextBuilder.Append("Currently unassigned.");
			}
			else
			{
				if(String.Compare(NewIssue.Owner, IssueMonitor.UserName, StringComparison.OrdinalIgnoreCase) == 0)
				{
					if(NewIssue.NominatedBy != null)
					{
						OwnerTextBuilder.AppendFormat("You have been nominated to fix this issue by {0}.", Utility.FormatUserName(NewIssue.NominatedBy));
					}
					else
					{
						OwnerTextBuilder.AppendFormat("Assigned to {0}.", Utility.FormatUserName(NewIssue.Owner));
					}
					bNewStrongAlert = true;
				}
				else
				{
					OwnerTextBuilder.AppendFormat("Assigned to {0}", Utility.FormatUserName(NewIssue.Owner));
					if(NewIssue.NominatedBy != null)
					{
						OwnerTextBuilder.AppendFormat(" by {0}", Utility.FormatUserName(NewIssue.NominatedBy));
					}
					if(!NewIssue.AcknowledgedAt.HasValue && (NewReason & IssueAlertReason.UnacknowledgedTimer) != 0)
					{
						OwnerTextBuilder.Append(" (not acknowledged)");
					}
					OwnerTextBuilder.Append(".");
				}
			}
			OwnerTextBuilder.AppendFormat(" Open for {0}.", Utility.FormatDurationMinutes((int)(NewIssue.RetrievedAt - NewIssue.CreatedAt).TotalMinutes));
			string OwnerText = OwnerTextBuilder.ToString();

			bool bNewIsWarning = Issue.Builds.Count > 0 && !Issue.Builds.Any(x => x.Outcome != IssueBuildOutcome.Warning);

			Issue = NewIssue;

			string Summary = NewIssue.Summary;

			int MaxLength = 128;
			if(Summary.Length > MaxLength)
			{
				Summary = Summary.Substring(0, MaxLength).TrimEnd() + "...";
			}

			if(Summary != SummaryLabel.Text || OwnerText != OwnerLabel.Text || Reason != NewReason || bIsWarning != bNewIsWarning || bStrongAlert != bNewStrongAlert)
			{
				Rectangle PrevBounds = Bounds;
				SuspendLayout();

				SummaryLabel.Text = Summary;
				OwnerLabel.Text = OwnerText;

				bool bForceUpdateButtons = false;
				if(bStrongAlert != bNewStrongAlert)
				{
					bStrongAlert = bNewStrongAlert;

					if(bNewStrongAlert)
					{
						SummaryLabel.ForeColor = Color.FromArgb(255, 255, 255);
						SummaryLabel.LinkColor = Color.FromArgb(255, 255, 255);
						OwnerLabel.ForeColor = Color.FromArgb(255, 255, 255);
						DetailsBtn.Theme = AlertButtonControl.AlertButtonTheme.Strong;
						AcceptBtn.Theme = AlertButtonControl.AlertButtonTheme.Strong;
						LatestBuildLinkLabel.LinkColor = Color.FromArgb(255, 255, 255);
					}
					else
					{
						SummaryLabel.ForeColor = Color.FromArgb(32, 32, 64);
						SummaryLabel.LinkColor = Color.FromArgb(32, 32, 64);
						OwnerLabel.ForeColor = Color.FromArgb(32, 32, 64);
						DetailsBtn.Theme = AlertButtonControl.AlertButtonTheme.Normal;
						AcceptBtn.Theme = AlertButtonControl.AlertButtonTheme.Green;
						LatestBuildLinkLabel.LinkColor = Color.FromArgb(16, 102, 192);
					}

					bForceUpdateButtons = true;
				}

				if (bIsWarning != bNewIsWarning)
				{
					bIsWarning = bNewIsWarning;
				}

				if(Reason != NewReason || bForceUpdateButtons)
				{
					Reason = NewReason;

					List<Button> Buttons = new List<Button>();
					Buttons.Add(DetailsBtn);
					if((NewReason & IssueAlertReason.Owner) != 0)
					{
						AcceptBtn.Text = "Acknowledge";
						Buttons.Add(AcceptBtn);
					}
					else if((NewReason & IssueAlertReason.Normal) != 0)
					{
						AcceptBtn.Text = "Will Fix";
						Buttons.Add(AcceptBtn);
						DeclineBtn.Text = "Not Me";
						Buttons.Add(DeclineBtn);
					}
					else
					{
						DeclineBtn.Text = "Dismiss";
						Buttons.Add(DeclineBtn);
					}

					tableLayoutPanel3.ColumnCount = Buttons.Count;
					tableLayoutPanel3.Controls.Clear();
					for(int Idx = 0; Idx < Buttons.Count; Idx++)
					{
						tableLayoutPanel3.Controls.Add(Buttons[Idx], Idx, 0);
					}
				}

				ResumeLayout(true);
				Location = new Point(PrevBounds.Right - Bounds.Width, PrevBounds.Y);
				Invalidate();
			}
		}

		protected override void OnHandleCreated(EventArgs e)
		{
			base.OnHandleCreated(e);

			// Manually make the window topmost, so that we can pass SWP_NOACTIVATE
			SetWindowPos(Handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}

		protected override bool ShowWithoutActivation 
		{
			get { return true; }
		}

		private void SummaryLabel_LinkClicked(object sender, System.Windows.Forms.LinkLabelLinkClickedEventArgs e)
		{
			LaunchUrl();
		}

		private void IssueAlertWindow_Click(object sender, EventArgs e)
		{
			LaunchUrl();
		}

		public void LaunchUrl()
		{
			IssueBuildData LastBuild = Issue.Builds.OrderByDescending(x => x.Change).FirstOrDefault();
			if(LastBuild != null)
			{
				System.Diagnostics.Process.Start(LastBuild.ErrorUrl);
			}
		}
	}
}
