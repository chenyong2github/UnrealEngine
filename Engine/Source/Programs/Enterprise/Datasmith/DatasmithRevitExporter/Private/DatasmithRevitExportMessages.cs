// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Drawing;
using System.Windows.Forms;


namespace DatasmithRevitExporter
{
	public class DatasmithRevitExportMessages : Form
	{
		private TextBox MessageBox;

		public delegate void ClearCallback();

		private ClearCallback OnClear;

		public DatasmithRevitExportMessages(ClearCallback InCallback)
		{
			OnClear = InCallback;

			MessageBox = new TextBox();
			MessageBox.Name = "MessageBox";
			MessageBox.AutoSize = true;
			MessageBox.BorderStyle = BorderStyle.FixedSingle;
			MessageBox.Dock = DockStyle.Fill;
			MessageBox.ReadOnly = true;
			MessageBox.Multiline = true;
			MessageBox.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
			MessageBox.TabIndex = 0;
			MessageBox.WordWrap = true;

			Button ClearButton = new Button();
			ClearButton.Name = "ClearButton";
			ClearButton.Text = DatasmithRevitResources.Strings.MessagesDialog_ButtonClear;
			ClearButton.Anchor = ((AnchorStyles)((AnchorStyles.Bottom | AnchorStyles.Left))); ;
			ClearButton.Margin = new Padding(3, 3, 3, 3);
			ClearButton.Size = new Size(75, 23);
			ClearButton.TabIndex = 1;
			ClearButton.UseVisualStyleBackColor = true;
			ClearButton.Click += new EventHandler(ClearButtonClicked);

			Button CloseButton = new Button();
			CloseButton.Name = "CloseButton";
			CloseButton.Text = DatasmithRevitResources.Strings.MessagesDialog_ButtonClose;
			CloseButton.Anchor = ((AnchorStyles)((AnchorStyles.Bottom | AnchorStyles.Right)));;
			CloseButton.DialogResult = DialogResult.OK;
			CloseButton.Margin = new Padding(3, 3, 3, 3);
			CloseButton.Size = new Size(75, 23);
			CloseButton.TabIndex = 2;
			CloseButton.UseVisualStyleBackColor = true;
			CloseButton.Click += new EventHandler(CloseButtonClicked);

			TableLayoutPanel DialogLayout = new TableLayoutPanel();
			DialogLayout.Name = "DialogLayout";
			DialogLayout.ColumnCount = 2;
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.RowCount = 2;
			DialogLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.Dock = DockStyle.Fill;
			DialogLayout.Location = new Point(10, 10);
			DialogLayout.TabIndex = 3;

			DialogLayout.Controls.Add(MessageBox, 0, 0);
			DialogLayout.Controls.Add(ClearButton,   0, 1);
			DialogLayout.Controls.Add(CloseButton,   1, 1);

			DialogLayout.SetColumnSpan(MessageBox, 2);

			Name = "UnrealDatasmithExportMessages";
			Text = DatasmithRevitResources.Strings.MessagesDialog_Title;
			AutoScaleDimensions = new SizeF(12F, 25F);
			AutoScaleMode = AutoScaleMode.Font;
			ClientSize = new Size(1200, 300);
			Padding = new Padding(10);
			ShowIcon = false;
			SizeGripStyle = SizeGripStyle.Show;
			StartPosition = FormStartPosition.CenterParent;

			Controls.Add(DialogLayout);
		}

		private void CloseButtonClicked(
			object    InSender,
			EventArgs InEventArgs
		)
		{
			Close();
		}

		private void ClearButtonClicked(
			object InSender,
			EventArgs InEventArgs
		)
		{
			MessageBox.Clear();
			OnClear?.Invoke();
		}

		public string Messages
		{
			get
			{
				return MessageBox.Text;
			}
			set
			{
				MessageBox.Text = value;
				MessageBox.SelectionStart = 0;
			}
		}
	}
}
