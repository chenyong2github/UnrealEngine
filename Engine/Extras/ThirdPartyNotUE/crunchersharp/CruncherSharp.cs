using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Text;
using System.Windows.Forms;
using Dia2Lib;

namespace CruncherSharp
{
    public partial class CruncherSharp : Form
    {
        public CruncherSharp()
        {
            InitializeComponent();
			BindControlMouseClicks(this);
			m_CruncherData = new CruncherData();
            bindingSourceSymbols.DataSource = m_CruncherData.m_table;
            dataGridSymbols.DataSource = bindingSourceSymbols;

            dataGridSymbols.Columns[0].Width = 271;
        }

        private void exitToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.Close();
        }

        private void loadPDBToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (openPdbDialog.ShowDialog() == DialogResult.OK)
            {
				// Temporarily clear the filter so, if current filter is invalid, we don't generate a ton of exceptions while populating the table
				var preExistingFilter = textBoxFilter.Text;
				textBoxFilter.Text = "";
				bUpdateStack = false;

				Cursor.Current = Cursors.WaitCursor;
				string result = m_CruncherData.loadDataFromPdb(openPdbDialog.FileName);
				if (result != null)
				{
					MessageBox.Show(this, result);
				}

				this.Text = "Cruncher # - " + System.IO.Path.GetFileName(openPdbDialog.FileName);

				// Sort by name by default (ascending)
				dataGridSymbols.Sort(dataGridSymbols.Columns[0], ListSortDirection.Ascending);
				bindingSourceSymbols.Filter = null;// "Symbol LIKE '*rde*'";
				Cursor.Current = Cursors.Default;

				// Restore the filter now that the table is populated
				bUpdateStack = true;
				textBoxFilter.Text = preExistingFilter;

				ShowSelectedSymbolInfo();
			}
		}

        private ulong GetCacheLineSize()
        {
            return Convert.ToUInt32(textBoxCache.Text);
        }

        SymbolInfo FindSelectedSymbolInfo()
        {
            if (dataGridSymbols.SelectedRows.Count == 0)
            {
                return null;
            }

            DataGridViewRow selectedRow = dataGridSymbols.SelectedRows[0];
            string symbolName = selectedRow.Cells[0].Value.ToString();

            SymbolInfo info = m_CruncherData.FindSymbolInfo(symbolName);
            return info;
        }

		CruncherData m_CruncherData;
        long m_prefetchStartOffset = 0;
		Stack<string> UndoStack = new Stack<string>();
		Stack<string> RedoStack = new Stack<string>();
		bool bUpdateStack = true;

        private void dataGridSymbols_SelectionChanged(object sender, EventArgs e)
        {
            m_prefetchStartOffset = 0;
            ShowSelectedSymbolInfo();
        }

        void ShowSelectedSymbolInfo()
        {
            dataGridViewSymbolInfo.Rows.Clear();
            SymbolInfo info = FindSelectedSymbolInfo();
            if (info != null)
            {
                ShowSymbolInfo(info);
            }
        }

        delegate void InsertCachelineBoundaryRowsDelegate(long nextOffset);

        void ShowSymbolInfo(SymbolInfo info)
        {
            dataGridViewSymbolInfo.Rows.Clear();
            if (!info.HasChildren)
            {
                return;
            }

			if (bUpdateStack && (UndoStack.Count == 0 || info.m_name != UndoStack.Peek()))
			{
				RedoStack.Clear();
				UndoStack.Push(info.m_name);
			}

            long cacheLineSize = (long)GetCacheLineSize();
            long prevCacheBoundaryOffset = m_prefetchStartOffset;

            if (prevCacheBoundaryOffset > (long)info.m_size)
            {
                prevCacheBoundaryOffset = (long)info.m_size;
            }

            long numCacheLines = 0;

            InsertCachelineBoundaryRowsDelegate InsertCachelineBoundaryRows = (nextOffset) =>
            {
                while (nextOffset - prevCacheBoundaryOffset >= cacheLineSize)
                {
                    numCacheLines = numCacheLines + 1;
                    long cacheLineOffset = numCacheLines * cacheLineSize + m_prefetchStartOffset;
                    string[] boundaryRow = { "Cacheline boundary", cacheLineOffset.ToString(), "" };
                    dataGridViewSymbolInfo.Rows.Add(boundaryRow);
                    prevCacheBoundaryOffset = cacheLineOffset;
                }
            };

            foreach (SymbolInfo child in info.m_children)
            {
                if (child.m_padding > 0)
                {
                    long paddingOffset = child.m_offset - child.m_padding;

                    InsertCachelineBoundaryRows(paddingOffset);

                    string[] paddingRow = { "*Padding*", paddingOffset.ToString(), child.m_padding.ToString() };
                    dataGridViewSymbolInfo.Rows.Add(paddingRow);
                }

                InsertCachelineBoundaryRows(child.m_offset);

                string[] row = { child.m_name, child.m_offset.ToString(), child.m_size.ToString() };
                dataGridViewSymbolInfo.Rows.Add(row);
                dataGridViewSymbolInfo.Rows[dataGridViewSymbolInfo.Rows.Count - 1].Tag = child;
                if (child.m_typeName != null && child.m_typeName.Length != 0)
                {
                    dataGridViewSymbolInfo.Rows[dataGridViewSymbolInfo.Rows.Count - 1].Cells[0].ToolTipText = child.m_typeName;
                }
            }
            // Final structure padding.
            if (info.m_padding > 0)
            {
                long paddingOffset = (long)info.m_size - info.m_padding;
                string[] paddingRow = { "*Padding*", paddingOffset.ToString(), info.m_padding.ToString() };
                dataGridViewSymbolInfo.Rows.Add(paddingRow);
            }
        }

        private void dataGridSymbols_SortCompare(object sender, DataGridViewSortCompareEventArgs e)
        {
            if (e.Column.Index > 0)
            {
                e.Handled = true;
                int val1;
                int val2;
                Int32.TryParse(e.CellValue1.ToString(), out val1);
                Int32.TryParse(e.CellValue2.ToString(), out val2);
                e.SortResult = val1 - val2;
            }
        }

        private void dataGridViewSymbolInfo_CellPainting(object sender, DataGridViewCellPaintingEventArgs e)
        {
            for (int i = 0; i < dataGridViewSymbolInfo.Rows.Count; ++i)
            {
                DataGridViewRow row = dataGridViewSymbolInfo.Rows[i];
                DataGridViewCell cell = row.Cells[0];
                string CellValue = cell.Value.ToString();

                if (CellValue == "*Padding*")
                {
                    cell.Style.BackColor = Color.LightPink;
                    row.Cells[1].Style.BackColor = Color.LightPink;
                    row.Cells[2].Style.BackColor = Color.LightPink;
                }
                else if (CellValue.IndexOf("Base: ") == 0)
                {
                    cell.Style.BackColor = Color.LightGreen;
                    row.Cells[1].Style.BackColor = Color.LightGreen;
                    row.Cells[2].Style.BackColor = Color.LightGreen;
                }
                else if (CellValue == "Cacheline boundary")
                {
                    cell.Style.BackColor = Color.LightGray;
                    row.Cells[1].Style.BackColor = Color.LightGray;
                    row.Cells[2].Style.BackColor = Color.LightGray;
                }
                else if (i + 1 < dataGridViewSymbolInfo.Rows.Count)
                {
                    if (dataGridViewSymbolInfo.Rows[i+1].Cells[0].Value.ToString() == "Cacheline boundary")
                    {
                        UInt64 CachelineBoundary = UInt64.Parse(dataGridViewSymbolInfo.Rows[i + 1].Cells[1].Value.ToString());
                        UInt64 MemberStart = UInt64.Parse(row.Cells[1].Value.ToString());
                        UInt64 MemberSize = UInt64.Parse(row.Cells[2].Value.ToString());
                        if (MemberStart + MemberSize > CachelineBoundary)
                        {
                            cell.Style.BackColor = Color.LightYellow;
                            row.Cells[1].Style.BackColor = Color.LightYellow;
                            row.Cells[2].Style.BackColor = Color.LightYellow;
                        }
                    }
                }
            }
        }

        private void textBoxFilter_TextChanged(object sender, EventArgs e)
        {
			try
			{
                if (textBoxFilter.Text.Length == 0)
                {
                    bindingSourceSymbols.Filter = null;
                }
                else
                {
                    bindingSourceSymbols.Filter = "Symbol LIKE '" + textBoxFilter.Text + "'";
                }

				textBoxFilter.BackColor = Color.Empty;
				textBoxFilter.ForeColor = Color.Empty;
				filterFeedbackLabel.Text = "";
			}
			catch (System.Data.EvaluateException)
			{
				textBoxFilter.BackColor = Color.Red;
				textBoxFilter.ForeColor = Color.White;
				filterFeedbackLabel.Text = "Invalid filter";
			}
		}

        private void copyTypeLayoutToClipboardToolStripMenuItem_Click(object sender, EventArgs e)
        {
            SymbolInfo info = FindSelectedSymbolInfo();
            if (info != null)
            {
                System.IO.StringWriter tw = new System.IO.StringWriter();
				m_CruncherData.dumpSymbolInfo(tw, info);
                Clipboard.SetText(tw.ToString());
            }
        }

        private void textBoxCache_KeyPress(object sender, KeyPressEventArgs e)
        {
            if (e.KeyChar == (char)Keys.Enter || e.KeyChar == (char)Keys.Escape)
            {
                ShowSelectedSymbolInfo();
            }
            base.OnKeyPress(e);
        }

        private void textBoxCache_Leave(object sender, EventArgs e)
        {
            ShowSelectedSymbolInfo();
        }

        private void setPrefetchStartOffsetToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (dataGridViewSymbolInfo.SelectedRows.Count != 0)
            {
                DataGridViewRow selectedRow = dataGridViewSymbolInfo.SelectedRows[0];
                long symbolOffset = Convert.ToUInt32(selectedRow.Cells[1].Value);
                m_prefetchStartOffset = symbolOffset;
                ShowSelectedSymbolInfo();
            }
        }

        private void dataGridViewSymbolInfo_CellMouseDoubleClick(object sender, DataGridViewCellMouseEventArgs e)
        {
            if (dataGridViewSymbolInfo.SelectedRows.Count != 0)
            {
                DataGridViewRow selectedRow = dataGridViewSymbolInfo.SelectedRows[0];
                SymbolInfo info = selectedRow.Tag as SymbolInfo;
                if (info != null && info.m_typeName != null)
                {
                    textBoxFilter.Text = info.m_typeName.Replace("*", "[*]");
                }
            }
        }

		private void buttonHistoryBack_Click(object sender, EventArgs e)
		{
			if (UndoStack.Count > 1)
			{
				RedoStack.Push(UndoStack.Pop());

				bUpdateStack = false;
				textBoxFilter.Text = UndoStack.Peek();
				bUpdateStack = true;
			}
		}

		private void buttonHistoryForward_Click(object sender, EventArgs e)
		{
			if (RedoStack.Count > 0)
			{
				UndoStack.Push(RedoStack.Pop());

				bUpdateStack = false;
				textBoxFilter.Text = UndoStack.Peek();
				bUpdateStack = true;
			}
		}

		private void CruncherSharp_MouseDown(object sender, MouseEventArgs e)
		{
			if (e.Button == MouseButtons.XButton1)
			{
				buttonHistoryBack.PerformClick();
			}
			else if (e.Button == MouseButtons.XButton2)
			{
				buttonHistoryForward.PerformClick();
			}
		}

		private void BindControlMouseClicks(Control con)
		{
			con.MouseClick += delegate (object sender, MouseEventArgs e)
			{
				TriggerMouseClicked(sender, e);
			};
			// bind to controls already added
			foreach (Control i in con.Controls)
			{
				BindControlMouseClicks(i);
			}
			// bind to controls added in the future
			con.ControlAdded += delegate (object sender, ControlEventArgs e)
			{
				BindControlMouseClicks(e.Control);
			};
		}

		private void TriggerMouseClicked(object sender, MouseEventArgs e)
		{
			CruncherSharp_MouseDown(sender, e);
		}
	}
}
