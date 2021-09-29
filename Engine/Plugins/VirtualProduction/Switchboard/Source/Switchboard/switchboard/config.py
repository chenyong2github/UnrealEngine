# Copyright Epic Games, Inc. All Rights Reserved.

import collections
import fnmatch
import json
import os
import pathlib
import socket
import sys
import typing

from PySide2 import QtCore
from PySide2 import QtGui
from PySide2 import QtWidgets

from switchboard import switchboard_widgets as sb_widgets
from switchboard.switchboard_logging import LOGGER

ROOT_CONFIGS_PATH = pathlib.Path(__file__).parent.with_name('configs')
CONFIG_SUFFIX = '.json'

USER_SETTINGS_FILE_PATH = ROOT_CONFIGS_PATH.joinpath('user_settings.json')

DEFAULT_MAP_TEXT = '-- Default Map --'


class Setting(QtCore.QObject):
    '''
    A type-agnostic value container for a configuration setting.

    This base class can be used directly for Settings that will
    never appear in the UI. Otherwise, Settings that can be modified
    in the UI must be represented by a derived class that creates
    the appropriate widget(s) for modifying the Setting's value.
    '''
    signal_setting_changed = QtCore.Signal(object, object)
    signal_setting_overridden = QtCore.Signal(str, object, object)

    def _filter_value(self, value):
        '''
        Filter function to modify the incoming value before updating or
        overriding the setting.

        The base class implementation does not apply any filtering to values.
        '''
        return value

    def __init__(
        self,
        attr_name: str,
        nice_name: str,
        value,
        tool_tip: typing.Optional[str] = None,
        show_ui: bool = True
    ):
        '''
        Create a new Setting object.

        Args:
            attr_name: Internal name.
            nice_name: Display name.
            value    : The initial value of this Setting.
            tool_tip : Tooltip to show in the UI for this Setting.
            show_ui  : Whether to show this Setting in the Settings UI.
        '''
        super().__init__()

        self.attr_name = attr_name
        self.nice_name = nice_name

        value = self._filter_value(value)
        self._original_value = self._value = value

        # todo-dara: overrides are identified by device name right now. This
        # should be changed to the hash instead. That way we could avoid
        # having to patch the overrides and settings in CONFIG when a device
        # is renamed.
        self._overrides = {}

        # These members store the UI widgets for the "base" Setting as well as
        # any overrides of the setting, similar to the way we store the base
        # value and overrides of the value. They identify the widget in the UI
        # that should be highlighted when the Setting is overridden. Derived
        # classes should call set_widget() with an override device name if
        # appropriate in their implementations of _create_widgets() if they
        # want override highlighting.
        self._base_widget = None
        self._override_widgets = {}

        self.tool_tip = tool_tip
        self.show_ui = show_ui

    def is_overridden(self, device_name: str) -> bool:
        try:
            return self._overrides[device_name] != self._value
        except KeyError:
            return False

    def remove_override(self, device_name: str):
        self._overrides.pop(device_name, None)
        self._override_widgets.pop(device_name, None)

    def update_value(self, new_value):
        new_value = self._filter_value(new_value)

        if self._value == new_value:
            return

        old_value = self._value
        self._value = new_value

        self.signal_setting_changed.emit(old_value, self._value)

    def override_value(self, device_name: str, override):
        override = self._filter_value(override)

        if (device_name in self._overrides and
                self._overrides[device_name] == override):
            return

        self._overrides[device_name] = override
        self.signal_setting_overridden.emit(device_name, self._value, override)

    def get_value(self, device_name: typing.Optional[str] = None):
        try:
            return self._overrides[device_name]
        except KeyError:
            return self._value

    def on_device_name_changed(self, old_name: str, new_name: str):
        if old_name in self._overrides.keys():
            self._overrides[new_name] = self._overrides.pop(old_name)
            self._override_widgets[new_name] = (
                self._override_widgets.pop(old_name))

    def reset(self):
        self._value = self._original_value
        self._overrides = {}
        self._override_widgets = {}

    def _create_widgets(
            self, override_device_name: typing.Optional[str] = None) \
            -> typing.Union[QtWidgets.QWidget, QtWidgets.QLayout]:
        '''
        Create the widgets necessary to manipulate this Setting in the UI.

        Settings that can appear in the UI must provide their own
        implementation of this function. If override highlighting is desired,
        the implementation should also set the override widget member variable.

        This function should return the "top-level" widget or layout. In
        some cases such as the BoolSetting this will just be a QCheckBox,
        whereas in others like the FilePathSetting, this will be a QHBoxLayout
        that contains line edit and button widgets.
        '''
        raise NotImplementedError(
            f'No UI for Setting "{self.nice_name}". '
            'Settings that are intended to display in the UI must '
            'derive from the Setting class and override _create_widgets().')

    def _on_widget_value_changed(
            self, new_value,
            override_device_name: typing.Optional[str] = None):
        '''
        Update this Setting in response to a change in value caused by UI
        manipulation.

        The value is applied as an override if appropriate, in which case if
        an override widget has been identified, it will be highlighted.

        It should not be necessary to override this function in derived
        classes.
        '''
        if override_device_name is None:
            self.update_value(new_value)
            return

        old_value = self.get_value(override_device_name)
        if new_value != old_value:
            self.override_value(override_device_name, new_value)

        widget = self.get_widget(override_device_name=override_device_name)
        if self.is_overridden(override_device_name):
            if widget:
                sb_widgets.set_qt_property(widget, "override", True)
        else:
            if widget:
                sb_widgets.set_qt_property(widget, "override", False)
            self.remove_override(override_device_name)

    def _on_setting_changed(
            self, new_value,
            override_device_name: typing.Optional[str] = None):
        '''
        Callback invoked when the value of this Setting changes.

        This can be implemented in derived classes to update the appropriate
        UI elements in response to value changes *not* initiated by the
        Setting's UI.

        The default implementation does nothing.
        '''
        pass

    def create_ui(
            self, override_device_name: typing.Optional[str] = None,
            form_layout: typing.Optional[QtWidgets.QFormLayout] = None) \
            -> typing.Union[QtWidgets.QWidget, QtWidgets.QLayout, None]:
        '''
        Create the UI for this Setting.

        A label will be created for the Setting and a row for it will be
        added to the provided form layout along with the widget/layout
        returned by _create_widgets().

        Ideally, derived classes will not need to override this function
        and can instead just implement _create_widgets().
        '''
        if not self.show_ui:
            # It's ok to use the Setting base class directly for
            # settings that do not display in the UI.
            return None

        top_level_widget = self._create_widgets(
            override_device_name=override_device_name)

        widget = self.get_widget(override_device_name=override_device_name)
        if widget and self.is_overridden(override_device_name):
            sb_widgets.set_qt_property(widget, "override", True)

        # Give the Setting's UI an opportunity to update itself when the
        # underlying value changes.
        self.signal_setting_changed.connect(
            lambda old_value, new_value,
            override_device_name=override_device_name:
                self._on_setting_changed(
                    new_value, override_device_name=override_device_name))

        if top_level_widget and form_layout:
            setting_label = QtWidgets.QLabel()
            setting_label.setText(self.nice_name)
            if self.tool_tip:
                setting_label.setToolTip(self.tool_tip)

            form_layout.addRow(setting_label, top_level_widget)

        return top_level_widget

    def set_widget(
            self, widget: typing.Optional[QtWidgets.QWidget] = None,
            override_device_name: typing.Optional[str] = None):
        '''
        Set the widget to be used to manipulate this Setting, or
        this particular device's override of the Setting.

        A value of None can be provided for the widget to clear any
        stored widgets.
        '''
        if widget is None:
            # Clear the widget for this setting.
            if override_device_name is None:
                self._base_widget = None
            else:
                del self._override_widgets[override_device_name]
        else:
            if override_device_name is None:
                self._base_widget = widget
            else:
                self._override_widgets[override_device_name] = widget

    def get_widget(
            self, override_device_name: typing.Optional[str] = None) \
            -> typing.Optional[QtWidgets.QWidget]:
        '''
        Get the widget to be used to manipulate this Setting, or
        this particular device's override of the Setting.

        If no such widget was ever specified, None is returned.
        '''
        return self._override_widgets.get(
            override_device_name, self._base_widget)


class BoolSetting(Setting):
    '''
    A UI-displayable Setting for storing and modifying a boolean value.
    '''

    def _create_widgets(
            self, override_device_name: typing.Optional[str] = None) \
            -> QtWidgets.QCheckBox:
        check_box = QtWidgets.QCheckBox()
        check_box.setChecked(self.get_value(override_device_name))
        if self.tool_tip:
            check_box.setToolTip(self.tool_tip)

        self.set_widget(
            widget=check_box, override_device_name=override_device_name)

        check_box.stateChanged.connect(
            lambda state, override_device_name=override_device_name:
                self._on_widget_value_changed(
                    bool(state), override_device_name=override_device_name))

        return check_box

    def _on_setting_changed(
            self, new_value: bool,
            override_device_name: typing.Optional[str] = None):
        widget = self.get_widget(override_device_name=override_device_name)
        if widget:
            widget.setChecked(new_value)

        if override_device_name is not None:
            # Reset the checkbox override state. As the checkbox has only
            # two states there is no override anymore when the base value
            # changes.
            if widget:
                sb_widgets.set_qt_property(widget, "override", False)
            self.remove_override(override_device_name)


class IntSetting(Setting):
    '''
    A UI-displayable Setting for storing and modifying an integer value.
    '''

    def _create_widgets(
            self, override_device_name: typing.Optional[str] = None) \
            -> QtWidgets.QLineEdit:
        line_edit = QtWidgets.QLineEdit()
        if self.tool_tip:
            line_edit.setToolTip(self.tool_tip)
        line_edit.setValidator(QtGui.QIntValidator())

        value = str(self.get_value(override_device_name))
        line_edit.setText(value)
        line_edit.setCursorPosition(0)

        self.set_widget(
            widget=line_edit, override_device_name=override_device_name)

        line_edit.editingFinished.connect(
            lambda line_edit=line_edit,
            override_device_name=override_device_name:
                self._on_widget_value_changed(
                    int(line_edit.text()),
                    override_device_name=override_device_name))

        return line_edit

    def _on_setting_changed(
            self, new_value: int,
            override_device_name: typing.Optional[str] = None):
        widget = self.get_widget(override_device_name=override_device_name)
        if not widget:
            return

        old_value = int(widget.text())
        if new_value != old_value:
            widget.setText(new_value)
            widget.setCursorPosition(0)


class StringSetting(Setting):
    '''
    A UI-displayable Setting for storing and modifying a string value.
    '''

    def __init__(
        self,
        attr_name: str,
        nice_name: str,
        value: str,
        placeholder_text: str = '',
        tool_tip: typing.Optional[str] = None,
        show_ui: bool = True
    ):
        '''
        Create a new StringSetting object.

        Args:
            attr_name       : Internal name.
            nice_name       : Display name.
            value           : The initial value of this Setting.
            placeholder_text: Placeholder for this Setting's value in the UI.
            tool_tip        : Tooltip to show in the UI for this Setting.
            show_ui         : Whether to show this Setting in the Settings UI.
        '''
        super().__init__(
            attr_name, nice_name, value,
            tool_tip=tool_tip, show_ui=show_ui)

        self.placeholder_text = placeholder_text

    def _create_widgets(
            self, override_device_name: typing.Optional[str] = None) \
            -> QtWidgets.QLineEdit:
        line_edit = QtWidgets.QLineEdit()
        if self.tool_tip:
            line_edit.setToolTip(self.tool_tip)

        value = str(self.get_value(override_device_name))
        line_edit.setText(value)
        line_edit.setPlaceholderText(self.placeholder_text)
        line_edit.setCursorPosition(0)

        self.set_widget(
            widget=line_edit, override_device_name=override_device_name)

        line_edit.editingFinished.connect(
            lambda line_edit=line_edit,
            override_device_name=override_device_name:
                self._on_widget_value_changed(
                    line_edit.text().strip(),
                    override_device_name=override_device_name))

        return line_edit

    def _on_setting_changed(
            self, new_value: str,
            override_device_name: typing.Optional[str] = None):
        widget = self.get_widget(override_device_name=override_device_name)
        if not widget:
            return

        old_value = widget.text().strip()
        if new_value != old_value:
            widget.setText(new_value)
            widget.setCursorPosition(0)


class FileSystemPathSetting(StringSetting):
    '''
    An abstract UI Setting for storing and modifying a string value that
    represents a file system path.

    This class provides the foundation for the DirectoryPathSetting and
    FilePathSetting classes. It should not be used directly.
    '''

    def _getFileSystemPath(
            self, parent: QtWidgets.QWidget = None,
            start_path: str = '') -> str:
        raise NotImplementedError(
            f'Setting "{self.nice_name}" uses the FileSystemPathSetting '
            'class directly. A derived class (e.g. DirectoryPathSetting or '
            'FilePathSetting) must be used instead.')

    def _create_widgets(
            self, override_device_name: typing.Optional[str] = None) \
            -> QtWidgets.QHBoxLayout:
        line_edit = super()._create_widgets(
            override_device_name=override_device_name)

        edit_layout = QtWidgets.QHBoxLayout()
        edit_layout.addWidget(line_edit)

        browse_btn = QtWidgets.QPushButton('Browse')
        edit_layout.addWidget(browse_btn)

        def on_browse_clicked():
            start_path = str(pathlib.Path.home())
            if (SETTINGS.LAST_BROWSED_PATH and
                    os.path.exists(SETTINGS.LAST_BROWSED_PATH)):
                start_path = SETTINGS.LAST_BROWSED_PATH

            fs_path = self._getFileSystemPath(
                parent=browse_btn, start_path=start_path)
            if len(fs_path) > 0 and os.path.exists(fs_path):
                fs_path = os.path.normpath(fs_path)

                self._on_widget_value_changed(
                    fs_path,
                    override_device_name=override_device_name)

                SETTINGS.LAST_BROWSED_PATH = os.path.dirname(fs_path)
                SETTINGS.save()

        browse_btn.clicked.connect(on_browse_clicked)

        return edit_layout


class DirectoryPathSetting(FileSystemPathSetting):
    '''
    A UI-displayable Setting for storing and modifying a string value that
    represents the path to a directory on the file system.
    '''

    def _getFileSystemPath(
            self, parent: QtWidgets.QWidget = None,
            start_path: str = '') -> str:
        return QtWidgets.QFileDialog.getExistingDirectory(
            parent=parent, dir=start_path)


class FilePathSetting(FileSystemPathSetting):
    '''
    A UI-displayable Setting for storing and modifying a string value that
    represents the path to a file on the file system.
    '''

    def __init__(
        self,
        attr_name: str,
        nice_name: str,
        value: str,
        placeholder_text: str = '',
        file_path_filter: str = '',
        tool_tip: typing.Optional[str] = None,
        show_ui: bool = True
    ):
        '''
        Create a new FilePathSetting object.

        Args:
            attr_name       : Internal name.
            nice_name       : Display name.
            value           : The initial value of this Setting.
            placeholder_text: Placeholder for this Setting's value in the UI.
            file_path_filter: Filter to use in the file browser.
            tool_tip        : Tooltip to show in the UI for this Setting.
            show_ui         : Whether to show this Setting in the Settings UI.
        '''
        super().__init__(
            attr_name, nice_name, value, placeholder_text=placeholder_text,
            tool_tip=tool_tip, show_ui=show_ui)

        self.file_path_filter = file_path_filter

    def _getFileSystemPath(
            self, parent: QtWidgets.QWidget = None,
            start_path: str = '') -> str:
        file_path, _ = QtWidgets.QFileDialog.getOpenFileName(
            parent=parent, dir=start_path, filter=self.file_path_filter)
        return file_path


class PerforcePathSetting(StringSetting):
    '''
    A UI-displayable Setting for storing and modifying a string value that
    represents a Perforce depot path.
    '''

    def _filter_value(self, value: typing.Optional[str]) -> str:
        '''
        Clean the p4 path value by removing whitespace and trailing '/'.
        '''
        if not value:
            return ''

        return value.strip().rstrip('/')


class OptionSetting(Setting):
    '''
    A UI-displayable Setting for storing and modifying a value that is
    one of a fixed set of options.
    '''

    def __init__(
        self,
        attr_name: str,
        nice_name: str,
        value,
        possible_values: typing.List = None,
        tool_tip: typing.Optional[str] = None,
        show_ui: bool = True
    ):
        '''
        Create a new OptionSetting object.

        Args:
            attr_name      : Internal name.
            nice_name      : Display name.
            value          : The initial value of this Setting.
            possible_values: Possible values for this Setting.
            tool_tip       : Tooltip to show in the UI for this Setting.
            show_ui        : Whether to show this Setting in the Settings UI.
        '''
        super().__init__(
            attr_name, nice_name, value,
            tool_tip=tool_tip, show_ui=show_ui)

        self.possible_values = possible_values or []

    def _create_widgets(
            self, override_device_name: typing.Optional[str] = None) \
            -> sb_widgets.NonScrollableComboBox:
        combo = sb_widgets.NonScrollableComboBox()
        if self.tool_tip:
            combo.setToolTip(self.tool_tip)

        for value in self.possible_values:
            combo.addItem(str(value), value)

        combo.setCurrentIndex(
            combo.findData(self.get_value(override_device_name)))

        self.set_widget(
            widget=combo, override_device_name=override_device_name)

        combo.currentTextChanged.connect(
            lambda text, override_device_name=override_device_name:
                self._on_widget_value_changed(
                    text, override_device_name=override_device_name))

        return combo

    def _on_setting_changed(
            self, new_value,
            override_device_name: typing.Optional[str] = None):
        widget = self.get_widget(override_device_name=override_device_name)
        if not widget:
            return

        old_value = widget.currentText()
        if new_value != old_value:
            widget.setCurrentIndex(widget.findText(new_value))


class MultiOptionSetting(OptionSetting):
    '''
    A UI-displayable Setting for storing and modifying a set of values,
    which may optionally be a subset of a fixed set of options.
    '''

    def _create_widgets(
            self, override_device_name: typing.Optional[str] = None) \
            -> sb_widgets.MultiSelectionComboBox:
        combo = sb_widgets.MultiSelectionComboBox()
        if self.tool_tip:
            combo.setToolTip(self.tool_tip)

        selected_values = self.get_value(override_device_name)
        possible_values = (
            self.possible_values
            if len(self.possible_values) > 0 else selected_values)
        combo.add_items(selected_values, possible_values)

        self.set_widget(
            widget=combo, override_device_name=override_device_name)

        combo.signal_selection_changed.connect(
            lambda entries, override_device_name=override_device_name:
                self._on_widget_value_changed(
                    entries, override_device_name=override_device_name))

        return combo

    def _on_setting_changed(
            self, new_value,
            override_device_name: typing.Optional[str] = None):
        widget = self.get_widget(override_device_name=override_device_name)
        if not widget:
            return

        widget_model = widget.model()
        widget_items = [widget_model.item(i, 0) for i in range(widget.count())]
        widget_items = [item for item in widget_items if item.isEnabled()]

        old_value = [
            item.text()
            for item in widget_items
            if item.checkState() == QtCore.Qt.Checked]
        if new_value != old_value:
            selected_items = []
            for item in widget_items:
                if item.text() in new_value:
                    item.setCheckState(QtCore.Qt.Checked)
                    selected_items.append(item.text())
                else:
                    item.setCheckState(QtCore.Qt.Unchecked)

            widget.setEditText(widget.separator.join(selected_items))


class LoggingModel(QtGui.QStandardItemModel):
    '''
    A data model for storing a logging configuration that maps logging
    category names to the desired log verbosity for messages of that
    category.

    A value of None can be mapped to a category indicating that messages
    for that category should be emitted with the default verbosity level.

    The model allows the list of categories to be extended beyond the list
    given at initialization. Categories added in this way can be dynamically
    added or removed by the user, while categories in the initial list are
    fixed and cannot be removed.
    '''

    CATEGORY_COLUMN = 0
    VERBOSITY_COLUMN = 1
    NUM_COLUMNS = 2

    def __init__(
            self,
            categories: typing.List[str],
            verbosity_levels: typing.List[str],
            parent: typing.Optional[QtCore.QObject] = None):
        '''
        Create a new LoggingModel object.

        Args:
            categories      : List of logging category names.
            verbosity_levels: List of possible verbosity level settings for
                              a category.
            parent          : The QObject parent of this object.
        '''
        super().__init__(parent=parent)

        self._categories = categories or []
        self._user_categories = []
        self._verbosity_levels = verbosity_levels or []

        self.setColumnCount(LoggingModel.NUM_COLUMNS)
        self.setHorizontalHeaderLabels(['Category', 'Verbosity Level'])
        self.horizontalHeaderItem(
            LoggingModel.CATEGORY_COLUMN).setToolTip(
                'The name of the category of logging messages')
        self.horizontalHeaderItem(
            LoggingModel.VERBOSITY_COLUMN).setToolTip(
                'The level of verbosity at which to emit messages for the '
                'category')

    @property
    def categories(self) -> typing.List[str]:
        return self._categories

    @property
    def user_categories(self) -> typing.List[str]:
        return self._user_categories

    @property
    def verbosity_levels(self) -> typing.List[str]:
        return self._verbosity_levels

    def is_user_category(self, category: str) -> bool:
        '''
        Returns True if the given category was added after the
        model was initialized.
        '''
        return (
            category in self.user_categories and
            category not in self.categories)

    def add_user_category(self, category: str) -> bool:
        '''
        Adds a row for the category to the model.

        Returns True if the category was added, or False otherwise.
        '''
        if category in self.categories or category in self.user_categories:
            return False

        self._user_categories.append(category)

        root_item = self.invisibleRootItem()

        root_item.appendRow(
            [QtGui.QStandardItem(category), QtGui.QStandardItem(None)])

        return True

    def remove_user_category(self, category: str) -> bool:
        '''
        Removes the category from the model.

        Returns True if the category was removed, or False otherwise.
        '''
        if not self.is_user_category(category):
            return False

        self._user_categories.remove(category)

        category_items = self.findItems(
            category, column=LoggingModel.CATEGORY_COLUMN)
        if not category_items:
            return False

        root_item = self.invisibleRootItem()

        row = category_items[0].index().row()

        root_item.removeRow(row)

        return True

    @property
    def category_verbosities(self) -> collections.OrderedDict:
        '''
        Returns the data currently stored in the model as a dictionary
        mapping each category name to a verbosity level (or None).
        '''
        value = collections.OrderedDict()

        root_item = self.invisibleRootItem()

        for row in range(root_item.rowCount()):
            category_item = root_item.child(
                row, LoggingModel.CATEGORY_COLUMN)
            verbosity_level_item = root_item.child(
                row, LoggingModel.VERBOSITY_COLUMN)

            value[category_item.text()] = verbosity_level_item.text() or None

        return value

    @category_verbosities.setter
    def category_verbosities(
            self,
            value: typing.Optional[typing.Dict[str, typing.Optional[str]]]):
        '''
        Sets the data in the model using the given dictionary of category
        names to verbosity levels (or None).
        '''
        value = value or collections.OrderedDict()

        self.beginResetModel()

        self.removeRows(0, self.rowCount())

        root_item = self.invisibleRootItem()

        for category, verbosity_level in value.items():
            if (category not in self._categories and
                    category not in self._user_categories):
                self._user_categories.append(category)

            root_item.appendRow(
                [QtGui.QStandardItem(category),
                    QtGui.QStandardItem(verbosity_level)])

        self.endResetModel()

    def flags(self, index: QtCore.QModelIndex) -> QtCore.Qt.ItemFlags:
        '''
        Returns the item flags for the item at the given index.
        '''
        if not index.isValid():
            return QtCore.Qt.ItemIsEnabled

        item_flags = (QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable)

        if index.column() == LoggingModel.VERBOSITY_COLUMN:
            item_flags |= QtCore.Qt.ItemIsEditable

        return item_flags


class LoggingVerbosityItemDelegate(QtWidgets.QStyledItemDelegate):
    '''
    A delegate for items in the verbosity column of the logging view.

    This delegate manages creating a combo box with the available
    verbosity levels.
    '''

    def __init__(
            self,
            verbosity_levels: typing.List[str],
            parent: typing.Optional[QtCore.QObject] = None):
        super().__init__(parent=parent)
        self._verbosity_levels = verbosity_levels or []

    def createEditor(
            self, parent: QtWidgets.QWidget,
            option: QtWidgets.QStyleOptionViewItem,
            index: QtCore.QModelIndex) -> sb_widgets.NonScrollableComboBox:
        editor = sb_widgets.NonScrollableComboBox(parent)
        editor.addItems(self._verbosity_levels)

        # Pre-select the level currently specified in the model, if any.
        current_value = index.model().data(index)
        if current_value in self._verbosity_levels:
            current_index = self._verbosity_levels.index(current_value)
            editor.setCurrentIndex(current_index)

        editor.currentIndexChanged.connect(self.currentIndexChanged)

        return editor

    def setEditorData(
            self, editor: QtWidgets.QWidget, index: QtCore.QModelIndex):
        editor.blockSignals(True)
        editor.setCurrentIndex(editor.currentIndex())
        editor.blockSignals(False)

    def setModelData(
            self, editor: QtWidgets.QWidget, model: QtCore.QAbstractItemModel,
            index: QtCore.QModelIndex):
        model.setData(index, editor.currentText() or None)

    def currentIndexChanged(self):
        self.commitData.emit(self.sender())


class LoggingSettingVerbosityView(QtWidgets.QTreeView):
    '''
    A tree view that presents the logging configuration represented by the
    given LoggingModel.
    '''

    def __init__(
            self,
            logging_model: LoggingModel,
            parent: typing.Optional[QtWidgets.QWidget] = None):
        super().__init__(parent=parent)

        self.setModel(logging_model)
        self.header().setSectionResizeMode(
            LoggingModel.CATEGORY_COLUMN,
            QtWidgets.QHeaderView.Stretch)
        self.resizeColumnToContents(LoggingModel.VERBOSITY_COLUMN)
        self.header().setSectionResizeMode(
            LoggingModel.VERBOSITY_COLUMN,
            QtWidgets.QHeaderView.Fixed)
        self.header().setStretchLastSection(False)

        self.setItemDelegateForColumn(
            LoggingModel.VERBOSITY_COLUMN,
            LoggingVerbosityItemDelegate(logging_model.verbosity_levels, self))

        for row in range(logging_model.rowCount()):
            verbosity_level_index = logging_model.index(
                row, LoggingModel.VERBOSITY_COLUMN)
            self.openPersistentEditor(verbosity_level_index)

        self.setSelectionBehavior(QtWidgets.QTreeView.SelectRows)


class LoggingSetting(Setting):
    '''
    A UI-displayable Setting for storing and modifying a set of logging
    categories and the verbosity level of each category.

    An initial set of categories can be provided when creating the Setting,
    but adding additional user-defined categories is supported as well.
    '''

    # Extracted from ParseLogVerbosityFromString() in
    # Engine\Source\Runtime\Core\Private\Logging\LogVerbosity.cpp
    # None indicates the "default" verbosity level should be used with
    # no override applied.
    DEFAULT_VERBOSITY_LEVELS = [
        None, 'VeryVerbose', 'Verbose', 'Log', 'Display',
        'Warning', 'Error', 'Fatal', 'NoLogging']

    def _filter_value(
            self,
            value: typing.Optional[typing.Dict[str, typing.Optional[str]]]) \
            -> collections.OrderedDict:
        '''
        Filter function to modify the incoming value before updating or
        overriding the setting.

        This ensures that LoggingSetting values are always provided using
        a dictionary (regular Python dict or OrderedDict) or None. An exception
        is raised otherwise.

        The resulting dictionary will include a key/value pair for each
        category in the LoggingSetting. Category names not present in the
        input dictionary will have a value of None in the output dictionary.
        '''
        if value is None:
            value = collections.OrderedDict()
        else:
            try:
                value = collections.OrderedDict(value)
            except Exception as e:
                raise ValueError(
                    'Invalid LoggingSetting value. Values must be '
                    f'either dictionary-typed or None: {e}')

        for category in self._categories:
            if category not in value:
                value[category] = None

        return value

    def __init__(
        self,
        attr_name: str,
        nice_name: str,
        value: typing.Dict[str, typing.Optional[str]],
        categories: typing.List[str] = None,
        verbosity_levels: typing.List[str] = None,
        tool_tip: typing.Optional[str] = None,
        show_ui: bool = True
    ):
        '''
        Create a new LoggingSetting object.

        Args:
            attr_name       : Internal name.
            nice_name       : Display name.
            value           : The initial value of this Setting.
            categories      : The initial list of logging categories.
            verbosity_levels: The possible settings for verbosity level of
                              each category.
            tool_tip        : Tooltip to show in the UI for this Setting.
            show_ui         : Whether to show this Setting in the Settings UI.
        '''

        # Set the categories before calling the base class init since they
        # will be used when filtering the value.
        self._categories = categories or []

        super().__init__(
            attr_name, nice_name, value,
            tool_tip=tool_tip, show_ui=show_ui)

        self._verbosity_levels = (
            verbosity_levels or self.DEFAULT_VERBOSITY_LEVELS)

    def get_command_line_arg(
            self, override_device_name: typing.Optional[str] = None) \
            -> str:
        '''
        Generate the command line argument for specifying the logging
        configuration based on the value currently stored in the Setting.

        Only categories that have a verbosity level specified are included in
        the result. If no categories have a verbosity level specified, an
        empty string is returned.
        '''
        value = self.get_value(override_device_name)

        logging_strings = [
            f'{category} {level}' for category, level in value.items()
            if level]
        if not logging_strings:
            return ''

        return f'-LogCmds=\"{", ".join(logging_strings)}\"'

    def _create_widgets(
            self, override_device_name: typing.Optional[str] = None) \
            -> QtWidgets.QVBoxLayout:
        model = LoggingModel(
            categories=self._categories,
            verbosity_levels=self._verbosity_levels)
        model.category_verbosities = self._value
        view = LoggingSettingVerbosityView(logging_model=model)
        view.setMinimumHeight(100)

        self.set_widget(
            widget=view, override_device_name=override_device_name)

        edit_layout = QtWidgets.QVBoxLayout()
        edit_layout.addWidget(view)

        add_category_button = QtWidgets.QPushButton('Add Category')
        edit_layout.addWidget(add_category_button)

        def on_add_category_button_clicked():
            category, ok = QtWidgets.QInputDialog().getText(
                add_category_button, "Add Category",
                "Category name:", QtWidgets.QLineEdit.Normal)
            if not ok:
                return

            category = category.strip()
            if not category:
                return

            if model.add_user_category(category):
                verbosity_level_index = model.index(
                    model.rowCount() - 1, LoggingModel.VERBOSITY_COLUMN)
                view.openPersistentEditor(verbosity_level_index)

        add_category_button.clicked.connect(on_add_category_button_clicked)

        remove_category_button = QtWidgets.QPushButton('Remove Category')
        edit_layout.addWidget(remove_category_button)

        def on_remove_category_button_clicked():
            category_indices = view.selectionModel().selectedRows(
                LoggingModel.CATEGORY_COLUMN)
            if not category_indices:
                return
            category = model.itemFromIndex(category_indices[0]).text()

            reply = QtWidgets.QMessageBox.question(
                remove_category_button, 'Confirm Remove Category',
                ('Are you sure you would like to remove the logging category '
                 f'"{category}"?'),
                QtWidgets.QMessageBox.Yes, QtWidgets.QMessageBox.No)

            if reply == QtWidgets.QMessageBox.Yes:
                view.selectionModel().clear()
                model.remove_user_category(category)

        remove_category_button.clicked.connect(
            on_remove_category_button_clicked)

        # The remove button is disabled initially until a user category is
        # selected.
        REMOVE_BUTTON_DISABLED_TOOLTIP = (
            'Default categories for the device cannot be removed')
        remove_category_button.setEnabled(False)
        remove_category_button.setToolTip(REMOVE_BUTTON_DISABLED_TOOLTIP)

        # Enable the remove button when a user category is selected, and
        # disable it otherwise.
        def on_view_selectionChanged(selected, deselected):
            remove_category_button.setEnabled(False)
            remove_category_button.setToolTip(REMOVE_BUTTON_DISABLED_TOOLTIP)

            category_indices = view.selectionModel().selectedRows(
                LoggingModel.CATEGORY_COLUMN)
            if not category_indices:
                return

            category = model.itemFromIndex(category_indices[0]).text()

            if model.is_user_category(category):
                remove_category_button.setEnabled(True)
                remove_category_button.setToolTip('')

        view.selectionModel().selectionChanged.connect(
            on_view_selectionChanged)

        def on_logging_model_modified(override_device_name=None):
            category_verbosities = model.category_verbosities
            self._on_widget_value_changed(
                category_verbosities,
                override_device_name=override_device_name)

        model.dataChanged.connect(
            lambda top_left_index, bottom_right_index, roles,
            override_device_name=override_device_name:
                on_logging_model_modified(
                    override_device_name=override_device_name))

        model.rowsInserted.connect(
            lambda parent, first, last,
            override_device_name=override_device_name:
                on_logging_model_modified(
                    override_device_name=override_device_name))

        model.rowsRemoved.connect(
            lambda parent, first, last,
            override_device_name=override_device_name:
                on_logging_model_modified(
                    override_device_name=override_device_name))

        return edit_layout

    def _on_setting_changed(
            self, new_value,
            override_device_name: typing.Optional[str] = None):
        widget = self.get_widget(override_device_name=override_device_name)
        if not widget:
            return

        old_value = widget.model().category_verbosities
        if new_value != old_value:
            widget.model().category_verbosities = new_value


class ConfigPathError(Exception):
    '''
    Base exception type for config path related errors.
    '''
    pass


class ConfigPathEmptyError(ConfigPathError):
    '''
    Exception type raised when an empty or all whitespace string is used as a
    config path.
    '''
    pass


class ConfigPathLocationError(ConfigPathError):
    '''
    Exception type raised when a config path is located outside of the root
    configs directory.
    '''
    pass


class ConfigPathIsUserSettingsError(ConfigPathError):
    '''
    Exception type raised when the user settings file path is used as a config
    path.
    '''
    pass


def get_absolute_config_path(
        config_path: typing.Union[str, pathlib.Path]) -> pathlib.Path:
    '''
    Returns the given string or path object as an absolute config path.

    The string/path is validated to ensure that:
      - It is not empty, or all whitespace
      - It ends with the config path suffix
      - If it is already absolute, that it is underneath the root configs path
      - It is not the same path as the user settings file path
    '''
    if isinstance(config_path, str):
        config_path = config_path.strip()
        if not config_path:
            raise ConfigPathEmptyError('Config path cannot be empty')

        config_path = pathlib.Path(config_path)

    # Manually add the suffix instead of using pathlib.Path.with_suffix().
    # For strings like "foo.bar", with_suffix() will first remove ".bar"
    # before adding the suffix, which we don't want it to do.
    if not config_path.name.endswith(CONFIG_SUFFIX):
        config_path = config_path.with_name(
            f'{config_path.name}{CONFIG_SUFFIX}')

    if config_path.is_absolute():
        # Paths that are already absolute must have the root configs path as a
        # parent path.
        # Python 3.9 introduced pathlib.Path.is_relative_to(), which would read
        # a bit nicer here.
        if ROOT_CONFIGS_PATH not in config_path.parents:
            raise ConfigPathLocationError(
                f'Config path "{config_path}" is not underneath the root '
                f'configs path "{ROOT_CONFIGS_PATH}"')
    else:
        # Relative paths can simply be made absolute.
        config_path = ROOT_CONFIGS_PATH.joinpath(config_path)

    if config_path.resolve() == USER_SETTINGS_FILE_PATH:
        raise ConfigPathIsUserSettingsError(
            'Config path cannot be the same as the user settings file '
            f'path "{USER_SETTINGS_FILE_PATH}"')

    return config_path


def get_relative_config_path(
        config_path: typing.Union[str, pathlib.Path]) -> pathlib.Path:
    '''
    Returns the given string or path object as a config path relative to the
    root configs path.

    An absolute path is generated first to perform all of the same validation
    as get_absolute_config_path() before the relative path is computed and
    returned.
    '''
    config_path = get_absolute_config_path(config_path)
    return config_path.relative_to(ROOT_CONFIGS_PATH)


class ConfigPathValidator(QtGui.QValidator):
    '''
    Validator to determine whether the input is an acceptable config file
    path.

    If the input is not acceptable, the state is returned as Intermediate
    rather than Invalid so as not to interfere with the user typing in the
    text field.
    '''

    def validate(self, input, pos):
        try:
            get_absolute_config_path(input)
        except Exception:
            return QtGui.QValidator.Intermediate

        return QtGui.QValidator.Acceptable


class Config(object):

    DEFAULT_CONFIG_PATH = ROOT_CONFIGS_PATH.joinpath(f'Default{CONFIG_SUFFIX}')

    saving_allowed = True
    saving_allowed_fifo = []

    def push_saving_allowed(self, value):
        ''' Sets a new state of saving allowed, but pushes current to the stack
        '''
        self.saving_allowed_fifo.append(self.saving_allowed)
        self.saving_allowed = value

    def pop_saving_allowed(self):
        ''' Restores saving_allowed flag from the stack
        '''
        self.saving_allowed = self.saving_allowed_fifo.pop()

    def __init__(self, file_path: typing.Union[str, pathlib.Path]):
        self.init_with_file_path(file_path)

    def init_new_config(self, file_path: typing.Union[str, pathlib.Path],
                        uproject, engine_dir, p4_settings):
        ''' Initialize new configuration
        '''

        self.file_path = get_absolute_config_path(file_path)
        self.PROJECT_NAME = self.file_path.stem
        self.UPROJECT_PATH = StringSetting(
            "uproject", "uProject Path", uproject, tool_tip="Path to uProject")
        self.SWITCHBOARD_DIR = os.path.abspath(
            os.path.join(os.path.dirname(os.path.abspath(__file__)), '../'))
        self.ENGINE_DIR = StringSetting(
            "engine_dir", "Engine Directory", engine_dir,
            tool_tip="Path to UE4 engine directory")
        self.BUILD_ENGINE = BoolSetting(
            "build_engine", "Build Engine", False,
            tool_tip="Is Engine built from source?")
        self.MAPS_PATH = StringSetting(
            "maps_path", "Map Path", "",
            tool_tip=(
                "Relative path from Content folder that contains maps to "
                "launch into."))
        self.MAPS_FILTER = StringSetting(
            "maps_filter", "Map Filter", "*.umap",
            tool_tip=(
                "Walk every file in the Map Path and run a fnmatch to filter "
                "the file names"))
        self.P4_ENABLED = BoolSetting(
            "p4_enabled", "Perforce Enabled", p4_settings['p4_enabled'],
            tool_tip="Toggle Perforce support for the entire application")
        self.SOURCE_CONTROL_WORKSPACE = StringSetting(
            "source_control_workspace", "Workspace Name",
            p4_settings['p4_workspace_name'],
            tool_tip="SourceControl Workspace/Branch")

        self.P4_PROJECT_PATH = PerforcePathSetting(
            attr_name="p4_sync_path",
            nice_name="Perforce Project Path",
            value=p4_settings['p4_project_path']
        )

        self.P4_ENGINE_PATH = PerforcePathSetting(
            attr_name="p4_engine_path",
            nice_name="Perforce Engine Path",
            value=p4_settings['p4_engine_path']
        )

        self.CURRENT_LEVEL = DEFAULT_MAP_TEXT

        self.OSC_SERVER_PORT = IntSetting(
            "osc_server_port", "OSC Server Port", 6000)
        self.OSC_CLIENT_PORT = IntSetting(
            "osc_client_port", "OSC Client Port", 8000)

        # MU Settings
        self.MULTIUSER_SERVER_EXE = 'UnrealMultiUserServer'
        self.MUSERVER_COMMAND_LINE_ARGUMENTS = ""
        self.MUSERVER_SERVER_NAME = f'{self.PROJECT_NAME}_MU_Server'
        self.MUSERVER_ENDPOINT = ':9030'
        self.MUSERVER_AUTO_LAUNCH = True
        self.MUSERVER_AUTO_JOIN = False
        self.MUSERVER_CLEAN_HISTORY = True
        self.MUSERVER_AUTO_BUILD = True
        self.MUSERVER_AUTO_ENDPOINT = True

        self.LISTENER_EXE = 'SwitchboardListener'

        self._device_data_from_config = {}
        self._plugin_data_from_config = {}
        self._plugin_settings = {}
        self._device_settings = {}

        LOGGER.info(f"Creating new config saved in {self.file_path}")
        self.save()

        SETTINGS.CONFIG = self.file_path
        SETTINGS.save()

    def init_with_file_path(self, file_path: typing.Union[str, pathlib.Path]):
        if file_path:
            try:
                self.file_path = get_absolute_config_path(file_path)

                # Read the json config file
                with open(self.file_path) as f:
                    LOGGER.debug(f'Loading Config {self.file_path}')
                    data = json.load(f)
            except (ConfigPathError, FileNotFoundError) as e:
                LOGGER.error(f'Config: {e}')
                self.file_path = None
                data = {}
        else:
            self.file_path = None
            data = {}

        project_settings = []

        self.PROJECT_NAME = data.get('project_name', 'Default')
        self.UPROJECT_PATH = StringSetting(
            "uproject", "uProject Path", data.get('uproject', ''),
            tool_tip="Path to uProject")
        project_settings.append(self.UPROJECT_PATH)

        # Directory Paths
        self.SWITCHBOARD_DIR = os.path.abspath(
            os.path.join(os.path.dirname(os.path.abspath(__file__)), '../'))
        self.ENGINE_DIR = StringSetting(
            "engine_dir", "Engine Directory", data.get('engine_dir', ''),
            tool_tip="Path to UE4 engine directory")
        project_settings.append(self.ENGINE_DIR)
        self.BUILD_ENGINE = BoolSetting(
            "build_engine", "Build Engine", data.get('build_engine', False),
            tool_tip="Is Engine built from source?")
        project_settings.append(self.BUILD_ENGINE)
        self.MAPS_PATH = StringSetting(
            "maps_path", "Map Path", data.get('maps_path', ''),
            placeholder_text="Maps",
            tool_tip=(
                "Relative path from Content folder that contains maps to "
                "launch into."))
        project_settings.append(self.MAPS_PATH)
        self.MAPS_FILTER = StringSetting(
            "maps_filter", "Map Filter", data.get('maps_filter', '*.umap'),
            placeholder_text="*.umap",
            tool_tip=(
                "Walk every file in the Map Path and run a fnmatch to "
                "filter the file names"))
        project_settings.append(self.MAPS_FILTER)

        # OSC settings
        self.OSC_SERVER_PORT = IntSetting(
            "osc_server_port", "OSC Server Port",
            data.get('osc_server_port', 6000))
        self.OSC_CLIENT_PORT = IntSetting(
            "osc_client_port", "OSC Client Port",
            data.get('osc_client_port', 8000))
        project_settings.extend([self.OSC_SERVER_PORT, self.OSC_CLIENT_PORT])

        # Perforce settings
        self.P4_ENABLED = BoolSetting(
            "p4_enabled", "Perforce Enabled", data.get("p4_enabled", False),
            tool_tip="Toggle Perforce support for the entire application")
        self.SOURCE_CONTROL_WORKSPACE = StringSetting(
            "source_control_workspace", "Workspace Name",
            data.get("source_control_workspace"),
            tool_tip="SourceControl Workspace/Branch")

        self.P4_PROJECT_PATH = PerforcePathSetting(
            "p4_sync_path",
            "Perforce Project Path",
            data.get("p4_sync_path", ''),
            placeholder_text="//UE4/Project"
        )

        self.P4_ENGINE_PATH = PerforcePathSetting(
            "p4_engine_path",
            "Perforce Engine Path",
            data.get("p4_engine_path", ''),
            placeholder_text="//UE4/Project/Engine"
        )

        project_settings.extend(
            [self.P4_ENABLED, self.SOURCE_CONTROL_WORKSPACE,
             self.P4_PROJECT_PATH, self.P4_ENGINE_PATH])

        # EXE names
        self.MULTIUSER_SERVER_EXE = data.get(
            'multiuser_exe', 'UnrealMultiUserServer')
        self.LISTENER_EXE = data.get('listener_exe', 'SwitchboardListener')

        # MU Settings
        self.MUSERVER_COMMAND_LINE_ARGUMENTS = data.get(
            'muserver_command_line_arguments', '')
        self.MUSERVER_SERVER_NAME = data.get(
            'muserver_server_name', f'{self.PROJECT_NAME}_MU_Server')
        self.MUSERVER_ENDPOINT = data.get('muserver_endpoint', ':9030')
        self.MUSERVER_AUTO_LAUNCH = data.get('muserver_auto_launch', True)
        self.MUSERVER_AUTO_JOIN = data.get('muserver_auto_join', False)
        self.MUSERVER_CLEAN_HISTORY = data.get('muserver_clean_history', True)
        self.MUSERVER_AUTO_BUILD = data.get('muserver_auto_build', True)
        self.MUSERVER_AUTO_ENDPOINT = data.get('muserver_auto_endpoint', True)

        # MISC SETTINGS
        self.CURRENT_LEVEL = data.get('current_level', DEFAULT_MAP_TEXT)

        # Automatically save whenever a project setting is changed or
        # overridden by a device.
        for setting in project_settings:
            setting.signal_setting_changed.connect(lambda: self.save())
            setting.signal_setting_overridden.connect(
                self.on_device_override_changed)

        # Devices
        self._device_data_from_config = {}
        self._plugin_data_from_config = {}
        self._device_settings = {}
        self._plugin_settings = {}

        # Convert devices data from dict to list so they can be directly fed
        # into the kwargs.
        for device_type, devices in data.get('devices', {}).items():
            for device_name, data in devices.items():
                if device_name == "settings":
                    self._plugin_data_from_config[device_type] = data
                else:
                    ip_address = data["ip_address"]
                    device_data = {
                        "name": device_name,
                        "ip_address": ip_address
                    }
                    device_data["kwargs"] = {
                        k: v for (k, v) in data.items() if k != "ip_address"}
                    self._device_data_from_config.setdefault(
                        device_type, []).append(device_data)

    def load_plugin_settings(self, device_type, settings):
        ''' Updates plugin settings values with those read from the config file.
        '''

        loaded_settings = self._plugin_data_from_config.get(device_type, [])

        if loaded_settings:
            for setting in settings:
                if setting.attr_name in loaded_settings:
                    setting.update_value(loaded_settings[setting.attr_name])
            del self._plugin_data_from_config[device_type]

    def register_plugin_settings(self, device_type, settings):

        self._plugin_settings[device_type] = settings

        for setting in settings:
            setting.signal_setting_changed.connect(lambda: self.save())
            setting.signal_setting_overridden.connect(
                self.on_device_override_changed)

    def register_device_settings(
            self, device_type, device_name, settings, overrides):
        self._device_settings[(device_type, device_name)] = (
            settings, overrides)

        for setting in settings:
            setting.signal_setting_changed.connect(lambda: self.save())

    def on_device_override_changed(self, device_name, old_value, override):
        # Only do a save operation when the device is known (has called
        # register_device_settings) otherwise it is still loading and we want
        # to avoid saving during device loading to avoid errors in the cfg
        # file.
        known_devices = [name for (_, name) in self._device_settings.keys()]
        if device_name in known_devices:
            self.save()

    def replace(self, new_config_path: typing.Union[str, pathlib.Path]):
        """
        Move the file.

        If a file already exists at the new path, it will be overwritten.
        """
        new_config_path = get_absolute_config_path(new_config_path)

        if self.file_path:
            new_config_path.parent.mkdir(parents=True, exist_ok=True)
            self.file_path.replace(new_config_path)

        self.file_path = new_config_path
        self.save()

    def save(self):
        if not self.file_path or not self.saving_allowed:
            return

        data = {}

        # General settings
        data['project_name'] = self.PROJECT_NAME
        data['uproject'] = self.UPROJECT_PATH.get_value()
        data['engine_dir'] = self.ENGINE_DIR.get_value()
        data['build_engine'] = self.BUILD_ENGINE.get_value()
        data["maps_path"] = self.MAPS_PATH.get_value()
        data["maps_filter"] = self.MAPS_FILTER.get_value()
        data["listener_exe"] = self.LISTENER_EXE

        # OSC settings
        data["osc_server_port"] = self.OSC_SERVER_PORT.get_value()
        data["osc_client_port"] = self.OSC_CLIENT_PORT.get_value()

        # Source Control Settings
        data["p4_enabled"] = self.P4_ENABLED.get_value()
        data["p4_sync_path"] = self.P4_PROJECT_PATH.get_value()
        data["p4_engine_path"] = self.P4_ENGINE_PATH.get_value()
        data["source_control_workspace"] = (
            self.SOURCE_CONTROL_WORKSPACE.get_value())

        # MU Settings
        data["multiuser_exe"] = self.MULTIUSER_SERVER_EXE
        data["muserver_command_line_arguments"] = (
            self.MUSERVER_COMMAND_LINE_ARGUMENTS)
        data["muserver_server_name"] = self.MUSERVER_SERVER_NAME
        data["muserver_endpoint"] = self.MUSERVER_ENDPOINT
        data["muserver_auto_launch"] = self.MUSERVER_AUTO_LAUNCH
        data["muserver_auto_join"] = self.MUSERVER_AUTO_JOIN
        data["muserver_clean_history"] = self.MUSERVER_CLEAN_HISTORY
        data["muserver_auto_build"] = self.MUSERVER_AUTO_BUILD
        data["muserver_auto_endpoint"] = self.MUSERVER_AUTO_ENDPOINT

        # Current Level
        data["current_level"] = self.CURRENT_LEVEL

        # Devices
        data["devices"] = {}

        # Plugin settings
        for device_type, plugin_settings in self._plugin_settings.items():

            if not plugin_settings:
                continue

            settings = {}

            for setting in plugin_settings:
                settings[setting.attr_name] = setting.get_value()

            data["devices"][device_type] = {
                "settings": settings,
            }

        # Device settings
        for (device_type, device_name), (settings, overrides) in \
                self._device_settings.items():

            if device_type not in data["devices"].keys():
                data["devices"][device_type] = {}

            serialized_settings = {}

            for setting in settings:
                serialized_settings[setting.attr_name] = setting.get_value()

            for setting in overrides:
                if setting.is_overridden(device_name):
                    serialized_settings[setting.attr_name] = setting.get_value(
                        device_name)

            data["devices"][device_type][device_name] = serialized_settings

        # Save to file
        #
        self.file_path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.file_path, 'w') as f:
            json.dump(data, f, indent=4)
            LOGGER.debug(f'Config File: {self.file_path} updated')

    def on_device_name_changed(self, old_name, new_name):
        old_key = None

        # update the entry in device_settings as they are identified by name
        for (device_type, device_name), (_, overrides) in \
                self._device_settings.items():
            if device_name == old_name:
                old_key = (device_type, old_name)
                # we also need to patch the overrides for the same reason
                for setting in overrides:
                    setting.on_device_name_changed(old_name, new_name)
                break

        new_key = (old_key[0], new_name)
        self._device_settings[new_key] = self._device_settings.pop(old_key)

        self.save()

    def on_device_removed(self, _, device_type, device_name, update_config):
        if not update_config:
            return

        del self._device_settings[(device_type, device_name)]
        self.save()

    def maps(self):
        maps_path = os.path.normpath(
            os.path.join(
                os.path.dirname(
                    self.UPROJECT_PATH.get_value().replace('"', '')),
                'Content',
                self.MAPS_PATH.get_value()))

        maps = []
        for _, _, files in os.walk(maps_path):
            for name in files:
                if not fnmatch.fnmatch(name, self.MAPS_FILTER.get_value()):
                    continue

                rootname, _ = os.path.splitext(name)
                if rootname not in maps:
                    maps.append(rootname)

        maps.sort()
        return maps

    def multiuser_server_path(self):
        return self.engine_exe_path(
            self.ENGINE_DIR.get_value(), self.MULTIUSER_SERVER_EXE)

    def listener_path(self):
        return self.engine_exe_path(
            self.ENGINE_DIR.get_value(), self.LISTENER_EXE)

    # todo-dara: find a way to do this directly in the LiveLinkFace plugin code
    def unreal_device_ip_addresses(self):
        unreal_ips = []
        for (device_type, device_name), (settings, overrides) in \
                self._device_settings.items():
            if device_type == "Unreal":
                for setting in settings:
                    if setting.attr_name == "ip_address":
                        unreal_ips.append(setting.get_value(device_name))
        return unreal_ips

    @staticmethod
    def engine_exe_path(engine_dir: str, exe_basename: str):
        '''
        Returns platform-dependent path to the specified engine executable.
        '''
        exe_name = exe_basename
        platform_bin_subdir = ''

        if sys.platform.startswith('win'):
            platform_bin_subdir = 'Win64'
            platform_bin_path = os.path.normpath(
                os.path.join(engine_dir, 'Binaries', platform_bin_subdir))
            given_path = os.path.join(platform_bin_path, exe_basename)
            if os.path.exists(given_path):
                return given_path

            # Use %PATHEXT% to resolve executable extension ambiguity.
            pathexts = os.environ.get(
                'PATHEXT', '.COM;.EXE;.BAT;.CMD').split(';')
            for ext in pathexts:
                testpath = os.path.join(
                    platform_bin_path, f'{exe_basename}{ext}')
                if os.path.isfile(testpath):
                    return testpath

            # Fallback despite non-existence.
            return given_path
        else:
            if sys.platform.startswith('linux'):
                platform_bin_subdir = 'Linux'
            elif sys.platform.startswith('darwin'):
                platform_bin_subdir = 'Mac'

            return os.path.normpath(
                os.path.join(
                    engine_dir, 'Binaries', platform_bin_subdir, exe_name))


class UserSettings(object):

    def __init__(self):
        try:
            with open(USER_SETTINGS_FILE_PATH) as f:
                LOGGER.debug(f'Loading Settings {USER_SETTINGS_FILE_PATH}')
                data = json.load(f)
        except FileNotFoundError:
            # Create a default user_settings
            data = {}
            LOGGER.debug('Creating default user settings')

        self.CONFIG = data.get('config')
        if self.CONFIG:
            try:
                self.CONFIG = get_absolute_config_path(self.CONFIG)
            except ConfigPathError as e:
                LOGGER.error(e)
                self.CONFIG = None

        if not self.CONFIG:
            config_paths = list_config_paths()
            self.CONFIG = config_paths[0] if config_paths else None

        # IP Address of the machine running Switchboard
        self.IP_ADDRESS = data.get(
            'ip_address', socket.gethostbyname(socket.gethostname()))

        self.TRANSPORT_PATH = data.get('transport_path', '')

        # UI Settings
        self.MUSERVER_SESSION_NAME = data.get(
            'muserver_session_name', 'MU_Session')
        self.CURRENT_SEQUENCE = data.get('current_sequence', 'Default')
        self.CURRENT_SLATE = data.get('current_slate', 'Scene')
        self.CURRENT_TAKE = data.get('current_take', 1)
        self.CURRENT_LEVEL = data.get('current_level', None)
        self.LAST_BROWSED_PATH = data.get('last_browsed_path', None)

        # Save so any new defaults are written out
        self.save()

    def save(self):
        data = {
            'config': '',
            'ip_address': self.IP_ADDRESS,
            'transport_path': self.TRANSPORT_PATH,
            'muserver_session_name': self.MUSERVER_SESSION_NAME,
            'current_sequence': self.CURRENT_SEQUENCE,
            'current_slate': self.CURRENT_SLATE,
            'current_take': self.CURRENT_TAKE,
            'current_level': self.CURRENT_LEVEL,
            'last_browsed_path': self.LAST_BROWSED_PATH,
        }

        if self.CONFIG:
            try:
                data['config'] = str(get_relative_config_path(self.CONFIG))
            except ConfigPathError as e:
                LOGGER.error(e)

        with open(USER_SETTINGS_FILE_PATH, 'w') as f:
            json.dump(data, f, indent=4)


def list_config_paths() -> typing.List[pathlib.Path]:
    '''
    Returns a list of absolute paths to all config files in the configs dir.
    '''
    ROOT_CONFIGS_PATH.mkdir(parents=True, exist_ok=True)

    # Find all JSON files in the config dir recursively, but exclude the user
    # settings file.
    config_paths = [
        path for path in ROOT_CONFIGS_PATH.rglob(f'*{CONFIG_SUFFIX}')
        if path != USER_SETTINGS_FILE_PATH]

    return config_paths


# Get the user settings and load their config
SETTINGS = UserSettings()
CONFIG = Config(SETTINGS.CONFIG)
