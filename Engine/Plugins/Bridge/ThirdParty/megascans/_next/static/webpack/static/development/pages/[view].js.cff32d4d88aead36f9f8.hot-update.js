webpackHotUpdate("static\\development\\pages\\[view].js",{

/***/ "./src/AssetsGrid/components/Draggable.tsx":
/*!*************************************************!*\
  !*** ./src/AssetsGrid/components/Draggable.tsx ***!
  \*************************************************/
/*! exports provided: default */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
__webpack_require__.r(__webpack_exports__);
/* harmony import */ var _babel_runtime_helpers_esm_toConsumableArray__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(/*! @babel/runtime/helpers/esm/toConsumableArray */ "./node_modules/@babel/runtime/helpers/esm/toConsumableArray.js");
/* harmony import */ var react__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(/*! react */ "./node_modules/react/index.js");
/* harmony import */ var react__WEBPACK_IMPORTED_MODULE_1___default = /*#__PURE__*/__webpack_require__.n(react__WEBPACK_IMPORTED_MODULE_1__);
/* harmony import */ var _useMultiSelect__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(/*! ../useMultiSelect */ "./src/AssetsGrid/useMultiSelect.ts");


var _this = undefined,
    _jsxFileName = "C:\\Users\\PC\\Quixel\\megascans\\src\\AssetsGrid\\components\\Draggable.tsx";

var __jsx = react__WEBPACK_IMPORTED_MODULE_1___default.a.createElement;


var mouseXY;
var cancelled = false;

var WithDrag = function WithDrag(_ref) {
  var children = _ref.children,
      url = _ref.url,
      onDropped = _ref.onDropped,
      onDropDiscarded = _ref.onDropDiscarded,
      onDragStarted = _ref.onDragStarted;

  var _useState = Object(react__WEBPACK_IMPORTED_MODULE_1__["useState"])(null),
      dragging = _useState[0],
      setDragging = _useState[1];

  var ref = Object(react__WEBPACK_IMPORTED_MODULE_1__["useRef"])(null);

  var handleMouseDown = function handleMouseDown(ev) {
    if (ev.button === 0) {
      // left click only
      cancelled = false;
      mouseXY = {
        x: ev.pageX,
        y: ev.pageY
      };
      window.addEventListener("mousemove", handleMouseMove);
    }
  };

  var handleMouseUp = function handleMouseUp(ev) {
    cancelled = true;
    window.removeEventListener("mousemove", handleMouseMove);
    setDragging(false);
  };

  var handleMouseMove = function handleMouseMove(ev) {
    if (mouseXY && !dragging && ref.current) {
      var xDelta = Math.abs(ev.pageX - mouseXY.x);
      var yDelta = Math.abs(ev.pageY - mouseXY.y);

      if (xDelta > 30 || yDelta > 30) {
        if (cancelled) {
          return;
        }

        cancelled = true;

        var _useMultiSelect$getSt = _useMultiSelect__WEBPACK_IMPORTED_MODULE_2__["default"].getState(),
            assets = _useMultiSelect$getSt.assets;

        if (Object.keys(assets).length > 1) {
          var firstUrl = url;
          var rest = Object.keys(assets).map(function (id) {
            return assets[id].thumb.url;
          });
          var filtered = rest.filter(function (item) {
            return item !== firstUrl;
          });

          if (!rest.includes(url)) {
            window.removeEventListener("mousemove", handleMouseMove);
            setDragging(true);
            onDragStarted();
            return;
          }

          if (typeof ue !== "undefined") {
            var _ue, _ue$browserbinding, _ue$browserbinding$dr, _ue2, _ue2$browserbinding, _ue2$browserbinding$o, _ue3, _ue3$browserbinding, _ue3$browserbinding$o;

            (_ue = ue) === null || _ue === void 0 ? void 0 : (_ue$browserbinding = _ue.browserbinding) === null || _ue$browserbinding === void 0 ? void 0 : (_ue$browserbinding$dr = _ue$browserbinding.dragstarted) === null || _ue$browserbinding$dr === void 0 ? void 0 : _ue$browserbinding$dr.call(_ue$browserbinding, [firstUrl].concat(Object(_babel_runtime_helpers_esm_toConsumableArray__WEBPACK_IMPORTED_MODULE_0__["default"])(filtered)));
            (_ue2 = ue) === null || _ue2 === void 0 ? void 0 : (_ue2$browserbinding = _ue2.browserbinding) === null || _ue2$browserbinding === void 0 ? void 0 : (_ue2$browserbinding$o = _ue2$browserbinding.ondroppedcallback) === null || _ue2$browserbinding$o === void 0 ? void 0 : _ue2$browserbinding$o.call(_ue2$browserbinding, function () {
              onDropped();
            });
            (_ue3 = ue) === null || _ue3 === void 0 ? void 0 : (_ue3$browserbinding = _ue3.browserbinding) === null || _ue3$browserbinding === void 0 ? void 0 : (_ue3$browserbinding$o = _ue3$browserbinding.ondropdiscardedcallback) === null || _ue3$browserbinding$o === void 0 ? void 0 : _ue3$browserbinding$o.call(_ue3$browserbinding, function () {
              onDropDiscarded();
            });
          }

          window.removeEventListener("mousemove", handleMouseMove);
          setDragging(true);
          onDragStarted();
          return;
        } else {
          if (typeof ue !== "undefined") {
            var _ue4, _ue4$browserbinding, _ue4$browserbinding$d, _ue5, _ue5$browserbinding, _ue5$browserbinding$o, _ue6, _ue6$browserbinding, _ue6$browserbinding$o;

            (_ue4 = ue) === null || _ue4 === void 0 ? void 0 : (_ue4$browserbinding = _ue4.browserbinding) === null || _ue4$browserbinding === void 0 ? void 0 : (_ue4$browserbinding$d = _ue4$browserbinding.dragstarted) === null || _ue4$browserbinding$d === void 0 ? void 0 : _ue4$browserbinding$d.call(_ue4$browserbinding, [url]);
            (_ue5 = ue) === null || _ue5 === void 0 ? void 0 : (_ue5$browserbinding = _ue5.browserbinding) === null || _ue5$browserbinding === void 0 ? void 0 : (_ue5$browserbinding$o = _ue5$browserbinding.ondroppedcallback) === null || _ue5$browserbinding$o === void 0 ? void 0 : _ue5$browserbinding$o.call(_ue5$browserbinding, function () {
              onDropped();
            });
            (_ue6 = ue) === null || _ue6 === void 0 ? void 0 : (_ue6$browserbinding = _ue6.browserbinding) === null || _ue6$browserbinding === void 0 ? void 0 : (_ue6$browserbinding$o = _ue6$browserbinding.ondropdiscardedcallback) === null || _ue6$browserbinding$o === void 0 ? void 0 : _ue6$browserbinding$o.call(_ue6$browserbinding, function () {
              onDropDiscarded();
            });
          }

          window.removeEventListener("mousemove", handleMouseMove);
          setDragging(true);
          onDragStarted();
          return;
        }
      }
    }
  };

  Object(react__WEBPACK_IMPORTED_MODULE_1__["useEffect"])(function () {
    var el = ref.current;

    if (el) {
      setDragging(false);
      cancelled = false;
      el.removeEventListener("mousedown", handleMouseDown);
      el.removeEventListener("mouseup", handleMouseUp);
      el.removeEventListener("mousemove", handleMouseUp);
      el.addEventListener("mousedown", handleMouseDown);
      el.addEventListener("mouseup", handleMouseUp);
      return function () {
        el.removeEventListener("mousedown", handleMouseDown);
        el.removeEventListener("mouseup", handleMouseUp);
      };
    }
  }, [ref, onDragStarted]);
  return __jsx("div", {
    ref: ref,
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 119,
      columnNumber: 10
    }
  }, children);
};

var Draggable = function Draggable(_ref2) {
  var children = _ref2.children,
      url = _ref2.url,
      enabled = _ref2.enabled,
      onDropped = _ref2.onDropped,
      onDropDiscarded = _ref2.onDropDiscarded,
      onDragStarted = _ref2.onDragStarted;

  if (enabled) {
    return __jsx(WithDrag, {
      url: url,
      enabled: enabled,
      onDropped: onDropped,
      onDropDiscarded: onDropDiscarded,
      onDragStarted: onDragStarted,
      __self: _this,
      __source: {
        fileName: _jsxFileName,
        lineNumber: 132,
        columnNumber: 7
      }
    }, children);
  }

  return __jsx(react__WEBPACK_IMPORTED_MODULE_1__["Fragment"], {
    __self: _this,
    __source: {
      fileName: _jsxFileName,
      lineNumber: 144,
      columnNumber: 10
    }
  }, children);
};

/* harmony default export */ __webpack_exports__["default"] = (Draggable);

/***/ })

})
//# sourceMappingURL=[view].js.cff32d4d88aead36f9f8.hot-update.js.map