webpackHotUpdate("static\\development\\pages\\[view].js",{

/***/ "./src/AssetsGrid/components/AssetGridItem.tsx":
/*!*****************************************************!*\
  !*** ./src/AssetsGrid/components/AssetGridItem.tsx ***!
  \*****************************************************/
/*! exports provided: default */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
__webpack_require__.r(__webpack_exports__);
/* harmony import */ var _babel_runtime_helpers_esm_extends__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(/*! @babel/runtime/helpers/esm/extends */ "./node_modules/@babel/runtime/helpers/esm/extends.js");
/* harmony import */ var react__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(/*! react */ "./node_modules/react/index.js");
/* harmony import */ var react__WEBPACK_IMPORTED_MODULE_1___default = /*#__PURE__*/__webpack_require__.n(react__WEBPACK_IMPORTED_MODULE_1__);
/* harmony import */ var _quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(/*! @quixeltools/granite-react */ "./node_modules/@quixeltools/granite-react/dist/index.js");
/* harmony import */ var _quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2___default = /*#__PURE__*/__webpack_require__.n(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__);
/* harmony import */ var classnames__WEBPACK_IMPORTED_MODULE_3__ = __webpack_require__(/*! classnames */ "./node_modules/classnames/index.js");
/* harmony import */ var classnames__WEBPACK_IMPORTED_MODULE_3___default = /*#__PURE__*/__webpack_require__.n(classnames__WEBPACK_IMPORTED_MODULE_3__);
/* harmony import */ var _Draggable__WEBPACK_IMPORTED_MODULE_4__ = __webpack_require__(/*! ./Draggable */ "./src/AssetsGrid/components/Draggable.tsx");
/* harmony import */ var _AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5__ = __webpack_require__(/*! ./AssetGridItem.module.scss */ "./src/AssetsGrid/components/AssetGridItem.module.scss");
/* harmony import */ var _AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default = /*#__PURE__*/__webpack_require__.n(_AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5__);
/* harmony import */ var _DownloadManager__WEBPACK_IMPORTED_MODULE_6__ = __webpack_require__(/*! ../../DownloadManager */ "./src/DownloadManager/index.ts");
/* harmony import */ var _useMultiSelect__WEBPACK_IMPORTED_MODULE_7__ = __webpack_require__(/*! ../useMultiSelect */ "./src/AssetsGrid/useMultiSelect.ts");
/* harmony import */ var _Assets_useUserAsset__WEBPACK_IMPORTED_MODULE_8__ = __webpack_require__(/*! ../../Assets/useUserAsset */ "./src/Assets/useUserAsset.ts");
/* harmony import */ var _Assets_useAssetDownload__WEBPACK_IMPORTED_MODULE_9__ = __webpack_require__(/*! ../../Assets/useAssetDownload */ "./src/Assets/useAssetDownload.ts");
/* harmony import */ var _Assets_useAssetExport__WEBPACK_IMPORTED_MODULE_10__ = __webpack_require__(/*! ../../Assets/useAssetExport */ "./src/Assets/useAssetExport.ts");
/* harmony import */ var _AssetRightPanel_useTierForm__WEBPACK_IMPORTED_MODULE_11__ = __webpack_require__(/*! ../../AssetRightPanel/useTierForm */ "./src/AssetRightPanel/useTierForm.ts");
/* harmony import */ var _useUser__WEBPACK_IMPORTED_MODULE_12__ = __webpack_require__(/*! ../../useUser */ "./src/useUser.ts");
/* harmony import */ var src_Common_hooks_useContextMenu__WEBPACK_IMPORTED_MODULE_13__ = __webpack_require__(/*! src/Common/hooks/useContextMenu */ "./src/Common/hooks/useContextMenu.ts");
/* harmony import */ var _ContextMenu__WEBPACK_IMPORTED_MODULE_14__ = __webpack_require__(/*! ./ContextMenu */ "./src/AssetsGrid/components/ContextMenu.tsx");


var _this = undefined,
    _jsxFileName = "C:\\Users\\PC\\Quixel\\megascans\\src\\AssetsGrid\\components\\AssetGridItem.tsx";

var __jsx = react__WEBPACK_IMPORTED_MODULE_1___default.a.createElement;















var AssetGridItem = function AssetGridItem(_ref) {
  var asset = _ref.asset;
  var id = asset.id,
      thumb = asset.thumb,
      name = asset.name,
      badge = asset.badge,
      type = asset.type,
      isProgressivelyExportable = asset.isProgressivelyExportable,
      isUpdated = asset.isUpdated;

  var _useUserAsset = Object(_Assets_useUserAsset__WEBPACK_IMPORTED_MODULE_8__["useUserAsset"])(id),
      toggleFavorite = _useUserAsset.toggleFavorite,
      _useUserAsset$userAss = _useUserAsset.userAsset,
      acquired = _useUserAsset$userAss.acquired,
      favorited = _useUserAsset$userAss.favorited,
      downloaded = _useUserAsset$userAss.downloaded,
      progress = _useUserAsset$userAss.progress,
      generating = _useUserAsset$userAss.generating;

  var _useUser = Object(_useUser__WEBPACK_IMPORTED_MODULE_12__["default"])(),
      loggedIn = _useUser.user.loggedIn;

  var _useContextMenu = Object(src_Common_hooks_useContextMenu__WEBPACK_IMPORTED_MODULE_13__["default"])(),
      menuStyles = _useContextMenu.menuStyles,
      menuAttrs = _useContextMenu.menuAttrs,
      setPopupRef = _useContextMenu.setPopupRef,
      handleOnContext = _useContextMenu.handleOnContext,
      showMenu = _useContextMenu.showMenu;

  var tier = Object(_AssetRightPanel_useTierForm__WEBPACK_IMPORTED_MODULE_11__["default"])(function (state) {
    return state.fields.tier.value;
  });

  var _useAssetDownload = Object(_Assets_useAssetDownload__WEBPACK_IMPORTED_MODULE_9__["default"])(asset),
      downloadAssets = _useAssetDownload.downloadAssets,
      startProgressiveDownloads = _useAssetDownload.startProgressiveDownloads,
      cancelDownload = _useAssetDownload.cancelDownload;

  var _useAssetExport = Object(_Assets_useAssetExport__WEBPACK_IMPORTED_MODULE_10__["default"])(asset),
      exportAssets = _useAssetExport.exportAssets,
      exportTitle = _useAssetExport.exportTitle;

  var selected = Object(_useMultiSelect__WEBPACK_IMPORTED_MODULE_7__["default"])(Object(react__WEBPACK_IMPORTED_MODULE_1__["useCallback"])(function (state) {
    return !!state.assets[id];
  }, [id]));
  var reset = Object(_useMultiSelect__WEBPACK_IMPORTED_MODULE_7__["default"])(Object(react__WEBPACK_IMPORTED_MODULE_1__["useCallback"])(function (state) {
    return state.reset;
  }, []));
  var append = Object(_useMultiSelect__WEBPACK_IMPORTED_MODULE_7__["default"])(Object(react__WEBPACK_IMPORTED_MODULE_1__["useCallback"])(function (state) {
    return state.append;
  }, []));
  var remove = Object(_useMultiSelect__WEBPACK_IMPORTED_MODULE_7__["default"])(Object(react__WEBPACK_IMPORTED_MODULE_1__["useCallback"])(function (state) {
    return state.remove;
  }, []));

  var handleOnActivate = function handleOnActivate(ev) {
    if (ev.shiftKey) {
      var _document$getSelectio;

      if (selected) {
        remove(id);
      } else {
        append(asset);
      }

      (_document$getSelectio = document.getSelection()) === null || _document$getSelectio === void 0 ? void 0 : _document$getSelectio.removeAllRanges();
    } else {
      reset(asset);
    }
  };

  var handleFavorite = function handleFavorite() {
    toggleFavorite === null || toggleFavorite === void 0 ? void 0 : toggleFavorite(id);
  };

  var onDrop = function onDrop() {
    // Download if we can download otherwise export whatever tier is available
    exportAssets === null || exportAssets === void 0 ? void 0 : exportAssets(!!downloadAssets, true);
  };

  var onDragStarted = function onDragStarted() {
    console.log("Drag started");
    startProgressiveDownloads === null || startProgressiveDownloads === void 0 ? void 0 : startProgressiveDownloads();
  };

  var onDropDiscarded = function onDropDiscarded() {
    var _useMultiSelect$getSt = _useMultiSelect__WEBPACK_IMPORTED_MODULE_7__["default"].getState(),
        assets = _useMultiSelect$getSt.assets;

    if (Object.keys(assets).length > 1) {
      Object(_DownloadManager__WEBPACK_IMPORTED_MODULE_6__["getDownloadManager"])().removeFromQueue(Object.keys(assets));
    } else {
      Object(_DownloadManager__WEBPACK_IMPORTED_MODULE_6__["getDownloadManager"])().removeFromQueue([asset.id]);
    }
  };

  return __jsx("div", {
    className: _AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default.a.wrap,
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 84,
      columnNumber: 5
    }
  }, __jsx(_Draggable__WEBPACK_IMPORTED_MODULE_4__["default"], {
    url: thumb.url,
    enabled: !!startProgressiveDownloads && isProgressivelyExportable,
    onDropped: onDrop,
    onDropDiscarded: onDropDiscarded,
    onDragStarted: onDragStarted,
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 85,
      columnNumber: 7
    }
  }, __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["Card"], {
    isSelected: selected,
    backgroundImage: thumb.url,
    variant: asset.thumb.flat ? "cover" : "contain",
    onActivate: handleOnActivate,
    onContext: handleOnContext,
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 92,
      columnNumber: 9
    }
  }, __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["Card"].Region, {
    position: "top left",
    persistent: true,
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 99,
      columnNumber: 11
    }
  }, loggedIn && isUpdated ? __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["Icon"], {
    icon: "info-circle",
    color: "orange",
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 100,
      columnNumber: 38
    }
  }) : null, loggedIn && !isUpdated && downloaded ? __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["Icon"], {
    icon: "check",
    color: "green",
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 102,
      columnNumber: 53
    }
  }) : null, loggedIn && acquired ? __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["Icon"], {
    icon: "check",
    color: "white50",
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 104,
      columnNumber: 37
    }
  }) : null, badge === "new" ? __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["TextLabel"], {
    className: classnames__WEBPACK_IMPORTED_MODULE_3___default()(_AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default.a.badge, _AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default.a.textLabel),
    caption: "New",
    size: "medium",
    variant: "button",
    color: "blue",
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 107,
      columnNumber: 15
    }
  }) : null, badge === "free" ? __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["TextLabel"], {
    className: classnames__WEBPACK_IMPORTED_MODULE_3___default()(_AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default.a.badge, _AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default.a.textLabel),
    caption: "Free",
    size: "medium",
    variant: "button",
    color: "green",
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 117,
      columnNumber: 15
    }
  }) : null, badge === "bonus" ? __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["TextLabel"], {
    className: classnames__WEBPACK_IMPORTED_MODULE_3___default()(_AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default.a.badge, _AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default.a.textLabel),
    caption: "Bonus",
    size: "medium",
    variant: "button",
    color: "green",
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 127,
      columnNumber: 15
    }
  }) : null), typeof downloadAssets !== "undefined" || !!downloaded ? __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["Card"].Region, {
    position: "top right",
    persistent: typeof progress !== "undefined",
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 138,
      columnNumber: 13
    }
  }, typeof progress === "undefined" && !generating && downloaded ? __jsx("div", {
    "data-analytics-hint": JSON.stringify({
      name: "AddAsset",
      meta: {
        assets: [{
          id: id,
          type: type
        }],
        tier: tier,
        via: "grid"
      }
    }),
    title: exportTitle(id),
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 140,
      columnNumber: 17
    }
  }, __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["ProgressButton"], {
    size: "small",
    color: "blue",
    icon: "arrow-east-sz-2",
    state: "initial",
    onStart: function onStart() {
      exportAssets === null || exportAssets === void 0 ? void 0 : exportAssets();
    },
    onStop: function onStop() {},
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 151,
      columnNumber: 19
    }
  })) : __jsx("div", {
    "data-analytics-hint": JSON.stringify({
      name: "DownloadAsset",
      meta: {
        assets: [{
          id: id,
          type: type
        }],
        tier: tier,
        via: "grid"
      }
    }),
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 163,
      columnNumber: 17
    }
  }, __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["ProgressButton"], {
    size: "small",
    state: typeof progress !== "undefined" || generating ? "running" : "initial",
    value: typeof progress !== "undefined" ? progress : undefined,
    caption: typeof progress !== "undefined" ? "".concat(progress, "%") : undefined,
    max: typeof progress !== "undefined" ? 100 : undefined,
    onStart: function onStart() {
      return downloadAssets === null || downloadAssets === void 0 ? void 0 : downloadAssets();
    },
    onStop: function onStop() {
      return cancelDownload === null || cancelDownload === void 0 ? void 0 : cancelDownload(asset.id, generating);
    },
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 173,
      columnNumber: 19
    }
  }))) : null, __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["Card"].Region, {
    position: "bottom left",
    noninteractive: true,
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 187,
      columnNumber: 11
    }
  }, __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["TextLabel"], {
    caption: name,
    className: _AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default.a.textLabel,
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 188,
      columnNumber: 13
    }
  })), typeof favorited === "boolean" ? __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["Card"].Region, {
    position: "bottom right",
    persistent: favorited,
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 192,
      columnNumber: 13
    }
  }, __jsx(_quixeltools_granite_react__WEBPACK_IMPORTED_MODULE_2__["Icon"], {
    className: favorited ? _AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default.a.favorited : undefined,
    icon: "heart",
    color: "white50",
    onClick: handleFavorite,
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 193,
      columnNumber: 15
    }
  })) : null)), showMenu ? __jsx("div", Object(_babel_runtime_helpers_esm_extends__WEBPACK_IMPORTED_MODULE_0__["default"])({
    ref: setPopupRef,
    className: _AssetGridItem_module_scss__WEBPACK_IMPORTED_MODULE_5___default.a.contextMenu,
    style: menuStyles.popper
  }, menuAttrs.popper, {
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 205,
      columnNumber: 9
    }
  }), __jsx(_ContextMenu__WEBPACK_IMPORTED_MODULE_14__["default"], {
    assetId: id,
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 211,
      columnNumber: 11
    }
  })) : null);
};

/* harmony default export */ __webpack_exports__["default"] = (Object(react__WEBPACK_IMPORTED_MODULE_1__["memo"])(AssetGridItem));

/***/ })

})
//# sourceMappingURL=[view].js.a349bd6502ba1843b11c.hot-update.js.map