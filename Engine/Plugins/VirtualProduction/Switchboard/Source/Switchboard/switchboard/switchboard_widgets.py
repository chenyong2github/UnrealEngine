# Copyright Epic Games, Inc. All Rights Reserved.
import time
from typing import Optional

from PySide2 import QtCore, QtGui, QtWidgets

from switchboard.switchboard_logging import LOGGER


DEVICE_LIST_WIDGET_HEIGHT = 54
DEVICE_HEADER_LIST_WIDGET_HEIGHT = 40
DEVICE_WIDGET_HIDE_IP_ADDRESS_WIDTH = 500


class NonScrollableComboBox(QtWidgets.QComboBox):
    def __init__(self, parent=None):
        super().__init__(parent=parent)
        self.installEventFilter(self)

    def eventFilter(self, obj, event):
        if obj == self and event.type() == QtCore.QEvent.Wheel:
            event.ignore()
            return True
        return False


# Each entry has a checkbox, the first element is a lineedit that will show all selected entries
class MultiSelectionComboBox(QtWidgets.QComboBox):
    separator = ' | '
    signal_selection_changed = QtCore.Signal(list)

    def __init__(self, parent=None):
        super().__init__(parent=parent)
        self.model().itemChanged.connect(self.on_stateChanged)

        # Only "editable" to use the edit control for display; read-only, selection disallowed.
        self.setEditable(True)
        self.lineEdit().setReadOnly(True)
        self.lineEdit().selectionChanged.connect(lambda: self.lineEdit().setSelection(0, 0))

        # Hook mouse button events on the line edit to open/close the combo.
        self.lineEdit().installEventFilter(self)

        super().addItem("")
        item = self.model().item(0, 0)
        item.setEnabled(False)

        # the combo calls show/hidePopup internally, to avoid messing up the state we only allow showing/hiding the popup
        # if it happened inside a time intervall that could have reasonably been triggered by a user.
        # this is obviously a workaround but there seems to be no way to get better behavior w/o implementing a full-blown combobox.
        self.last_time_popup_was_triggered = time.time()
        self.popup_toggle_min_interval = 0.2
        self.popup_is_showing = False

    def add_items(self, selected_entries, all_entries):
        for entry in all_entries:
            self.addItem(entry)
            item = self.model().item(self.count()-1, 0)
            item.setFlags(QtCore.Qt.ItemIsUserCheckable | QtCore.Qt.ItemIsEnabled)
            state = QtCore.Qt.Checked if entry in selected_entries else QtCore.Qt.Unchecked
            item.setCheckState(state)

        invalid_entries = [entry for entry in selected_entries if entry not in all_entries]
        for entry in invalid_entries:
            self.addItem(entry)
            item = self.model().item(self.count()-1, 0)
            item.setFlags(QtCore.Qt.ItemIsUserCheckable | QtCore.Qt.ItemIsEnabled)
            item.setCheckState(QtCore.Qt.Checked)

            brush = item.foreground()
            brush.setColor(QtCore.Qt.red)
            item.setForeground(brush)

    def eventFilter(self, obj, event):
        if obj == self.lineEdit() and event.type() == QtCore.QEvent.MouseButtonPress:
            # if the lineedit is clicked we want the combo to open/close
            if self.popup_is_showing:
                self.hidePopup()
            else:
                self.showPopup()
            event.accept()
            return True
        elif obj == self and event.type() == QtCore.QEvent.Wheel:
            event.ignore()
            return True
        return False

    def wheelEvent(self, event):
        event.ignore()
        return True

    def showPopup(self):
        now = time.time()
        diff = abs(now - self.last_time_popup_was_triggered)
        if diff > self.popup_toggle_min_interval:
            super().showPopup()
            self.popup_is_showing = True
            self.last_time_popup_was_triggered = now

    def hidePopup(self):
        now = time.time()
        diff = abs(now - self.last_time_popup_was_triggered)
        if diff > self.popup_toggle_min_interval:
            super().hidePopup()
            self.popup_is_showing = False
            self.last_time_popup_was_triggered = now

    def on_stateChanged(self, item):
        # Without this, display can be incorrect when no items are checked [UE-112543].
        self.setCurrentIndex(0)

        selected_entries = []
        for i in range(self.count()):
            item = self.model().item(i, 0)
            if item.checkState() == QtCore.Qt.Checked:
                selected_entries.append(self.itemText(i))

        self.setEditText(self.separator.join(selected_entries))
        self.signal_selection_changed.emit(selected_entries)


class ControlQPushButton(QtWidgets.QPushButton):
    def __init__(self, parent = None, *, hover_focus: bool = True):
        super().__init__(parent)

        # Avoid "click -> focus -> disable -> focus arbitrary nearby widget"
        self.setFocusPolicy(QtCore.Qt.TabFocus)

        # Deprecated behavior: force focus on mouse enter for icon changes.
        # Defaults to True for backward compatibility, but should be removed.
        # New code should use stylesheet :hover selectors instead.
        self.hover_focus = hover_focus

    def enterEvent(self, event):
        super().enterEvent(event)
        if self.hover_focus:
            self.setFocus()

    def leaveEvent(self, event):
        super().leaveEvent(event)
        if self.hover_focus:
            self.clearFocus()

    @classmethod
    def create(
        cls, icon_off=None,
        icon_on=None,
        icon_hover_on=None, icon_hover=None,
        icon_disabled_on=None, icon_disabled=None,
        icon_size=None,
        checkable=True, checked=False,
        tool_tip=None,
        *,
        hover_focus: bool = True,
        name: Optional[str] = None
    ):
        button = ControlQPushButton(hover_focus=hover_focus)

        if name:
            button.setObjectName(name)

        set_qt_property(button, 'no_background', True)

        icon = QtGui.QIcon()
        pixmap: Optional[QtGui.QPixmap] = None

        if icon_off:
            pixmap = QtGui.QPixmap(icon_off)
            icon.addPixmap(pixmap, QtGui.QIcon.Normal, QtGui.QIcon.Off)

        if icon_on:
            pixmap = QtGui.QPixmap(icon_on)
            icon.addPixmap(pixmap, QtGui.QIcon.Normal, QtGui.QIcon.On)

        if icon_hover:
            pixmap = QtGui.QPixmap(icon_hover)
            icon.addPixmap(pixmap, QtGui.QIcon.Active, QtGui.QIcon.Off)

        if icon_hover_on:
            pixmap = QtGui.QPixmap(icon_hover_on)
            icon.addPixmap(pixmap, QtGui.QIcon.Active, QtGui.QIcon.On)

        if icon_disabled:
            pixmap = QtGui.QPixmap(icon_disabled)
            icon.addPixmap(pixmap, QtGui.QIcon.Disabled, QtGui.QIcon.Off)

        if icon_disabled_on:
            pixmap = QtGui.QPixmap(icon_disabled)
            icon.addPixmap(pixmap, QtGui.QIcon.Disabled, QtGui.QIcon.On)

        button.setIcon(icon)

        if pixmap and not icon_size:
            icon_size = pixmap.rect().size()

        if icon_size:
            button.setIconSize(icon_size)

        button.setMinimumSize(25, 35)

        if tool_tip:
            button.setToolTip(tool_tip)

        if checkable:
            button.setCheckable(checkable)
            button.setChecked(checked)

        return button


class FramelessQLineEdit(QtWidgets.QLineEdit):
    def __init__(self, parent=None):
        super().__init__(parent)

        if not self.isReadOnly():
            set_qt_property(self, "frameless", True)

        self.current_text = None
        self.is_valid = True

    def enterEvent(self, event):
        super().enterEvent(event)

        if not self.isReadOnly():
            set_qt_property(self, "frameless", False)

    def leaveEvent(self, event):
        super().leaveEvent(event)

        if self.hasFocus():
            return

        if not self.isReadOnly():
            set_qt_property(self, "frameless", True)

    def focusInEvent(self, e):
        super().focusInEvent(e)

        # Store the current value
        self.current_text = self.text()

        if not self.isReadOnly():
            set_qt_property(self, "frameless", False)

    def focusOutEvent(self, e):
        super().focusOutEvent(e)

        if not self.isReadOnly():
            set_qt_property(self, "frameless", True)

        if not self.is_valid:
            self.setText(self.current_text)
            self.is_valid = True

            set_qt_property(self, "error", False)

    def keyPressEvent(self, e):
        super().keyPressEvent(e)

        if e.key() == QtCore.Qt.Key_Return or e.key() == QtCore.Qt.Key_Enter:
            if self.is_valid:
                self.clearFocus()
        elif e.key() == QtCore.Qt.Key_Escape:
            self.setText(self.current_text)
            self.clearFocus()


def set_qt_property(
    widget: QtWidgets.QWidget, prop, value, *,
    update_box_model: bool = True,
    recursive_refresh: bool = False
):
    '''
    Set a dynamic property on the specified widget, and also recalculate its
    styling to take the property change into account.

    Args:
        widget: The widget for which to trigger a style refresh.
        prop: The name of the dynamic property to set.
        value: The new value of the dynamic property `prop`.
        update_box_model: Whether to trigger the more expensive update steps
            required for style changes that impact the box model of the widget.
        recursive: Whether to also refresh the styling of all child widgets.
    '''
    widget.setProperty(prop, value)
    refresh_qt_styles(widget, update_box_model=update_box_model,
                      recursive=recursive_refresh)


def refresh_qt_styles(
    widget: QtWidgets.QWidget, *,
    update_box_model: bool = True,
    recursive: bool = False
):
    '''
    Recalculate a Qt widget's styling (and optionally its children's styling).

    Args:
        widget: The widget for which to trigger a style refresh.
        update_box_model: Whether to perform the more expensive update steps
            required for style changes that impact the box model of the widget.
        recursive: Whether to also refresh the styling of all child widgets.
    '''
    widget.style().unpolish(widget)
    widget.style().polish(widget)

    if update_box_model:
        style_change_event = QtCore.QEvent(QtCore.QEvent.StyleChange)
        QtWidgets.QApplication.sendEvent(widget, style_change_event)
        widget.update()
        widget.updateGeometry()

    if recursive:
        for child in widget.findChildren(QtWidgets.QWidget):
            refresh_qt_styles(child, recursive=True,
                              update_box_model=update_box_model)
