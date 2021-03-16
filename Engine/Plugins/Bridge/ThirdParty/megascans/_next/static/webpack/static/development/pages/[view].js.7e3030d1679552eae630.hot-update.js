webpackHotUpdate("static\\development\\pages\\[view].js",{

/***/ "./src/AssetsGrid/useAssetsList.ts":
/*!*****************************************!*\
  !*** ./src/AssetsGrid/useAssetsList.ts ***!
  \*****************************************/
/*! exports provided: default */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
__webpack_require__.r(__webpack_exports__);
/* harmony import */ var _babel_runtime_helpers_esm_toConsumableArray__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(/*! @babel/runtime/helpers/esm/toConsumableArray */ "./node_modules/@babel/runtime/helpers/esm/toConsumableArray.js");
/* harmony import */ var _babel_runtime_regenerator__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(/*! @babel/runtime/regenerator */ "./node_modules/@babel/runtime/regenerator/index.js");
/* harmony import */ var _babel_runtime_regenerator__WEBPACK_IMPORTED_MODULE_1___default = /*#__PURE__*/__webpack_require__.n(_babel_runtime_regenerator__WEBPACK_IMPORTED_MODULE_1__);
/* harmony import */ var react__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(/*! react */ "./node_modules/react/index.js");
/* harmony import */ var react__WEBPACK_IMPORTED_MODULE_2___default = /*#__PURE__*/__webpack_require__.n(react__WEBPACK_IMPORTED_MODULE_2__);
/* harmony import */ var _types__WEBPACK_IMPORTED_MODULE_3__ = __webpack_require__(/*! ../types */ "./src/types.ts");
/* harmony import */ var _algoliaAssetsFetcher__WEBPACK_IMPORTED_MODULE_4__ = __webpack_require__(/*! ../algoliaAssetsFetcher */ "./src/algoliaAssetsFetcher.ts");
/* harmony import */ var _mhcAssetsFetcher__WEBPACK_IMPORTED_MODULE_5__ = __webpack_require__(/*! ../mhcAssetsFetcher */ "./src/mhcAssetsFetcher.ts");
/* harmony import */ var _Auth_useAuthSWR__WEBPACK_IMPORTED_MODULE_6__ = __webpack_require__(/*! ../Auth/useAuthSWR */ "./src/Auth/useAuthSWR.ts");
/* harmony import */ var _useAssetsBrowseParams__WEBPACK_IMPORTED_MODULE_7__ = __webpack_require__(/*! ./useAssetsBrowseParams */ "./src/AssetsGrid/useAssetsBrowseParams.ts");
/* harmony import */ var _assertUnreachable__WEBPACK_IMPORTED_MODULE_8__ = __webpack_require__(/*! ../assertUnreachable */ "./src/assertUnreachable.ts");
/* harmony import */ var _LocalLibraryManager__WEBPACK_IMPORTED_MODULE_9__ = __webpack_require__(/*! ../LocalLibraryManager */ "./src/LocalLibraryManager/index.ts");











function getFetcher(assetBrowseParams) {
  switch (assetBrowseParams.view) {
    case _types__WEBPACK_IMPORTED_MODULE_3__["PageView"].metahumans:
      return _mhcAssetsFetcher__WEBPACK_IMPORTED_MODULE_5__["default"];

    case _types__WEBPACK_IMPORTED_MODULE_3__["PageView"].home:
    case _types__WEBPACK_IMPORTED_MODULE_3__["PageView"].collections:
    case _types__WEBPACK_IMPORTED_MODULE_3__["PageView"].favorites:
    case _types__WEBPACK_IMPORTED_MODULE_3__["PageView"].free:
    case _types__WEBPACK_IMPORTED_MODULE_3__["PageView"].purchased:
      return _algoliaAssetsFetcher__WEBPACK_IMPORTED_MODULE_4__["default"];
    // Don't need to handle the Locals case

    case _types__WEBPACK_IMPORTED_MODULE_3__["PageView"].local:
      return function _callee() {
        return _babel_runtime_regenerator__WEBPACK_IMPORTED_MODULE_1___default.a.async(function _callee$(_context) {
          while (1) {
            switch (_context.prev = _context.next) {
              case 0:
                return _context.abrupt("return", {
                  assets: [],
                  totalPages: 0
                });

              case 1:
              case "end":
                return _context.stop();
            }
          }
        }, null, null, null, Promise);
      };

    default:
      Object(_assertUnreachable__WEBPACK_IMPORTED_MODULE_8__["default"])(assetBrowseParams.view);
  }
}

function localsTabSelected(assetBrowseParams) {
  return !!assetBrowseParams && assetBrowseParams.view === _types__WEBPACK_IMPORTED_MODULE_3__["PageView"].local;
}

function useAssetsList(page) {
  var _assetsBrowseParams, _assetsBrowseParams2;

  var assetsPerPage = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : 50;
  var config = arguments.length > 2 ? arguments[2] : undefined;
  var assetsBrowseParams = Object(_useAssetsBrowseParams__WEBPACK_IMPORTED_MODULE_7__["default"])();

  if (config === null || config === void 0 ? void 0 : config.overrideBrowseParams) {
    assetsBrowseParams = config.assetsBrowseParams;
  } // For Locals Tab - we don't want to use SWR
  // We want to get assets directly from the bifrost store
  // And we want to reset our local library on tab switch


  Object(react__WEBPACK_IMPORTED_MODULE_2__["useEffect"])(function () {
    if (assetsBrowseParams && localsTabSelected(assetsBrowseParams)) {
      Object(_LocalLibraryManager__WEBPACK_IMPORTED_MODULE_9__["getLocalLibraryManger"])().fetchLocalAssets(assetsBrowseParams);
    }
  }, [(_assetsBrowseParams = assetsBrowseParams) === null || _assetsBrowseParams === void 0 ? void 0 : _assetsBrowseParams.view]);

  var _useState = Object(react__WEBPACK_IMPORTED_MODULE_2__["useState"])(undefined),
      response = _useState[0],
      setResponse = _useState[1];

  var assetsBrowseKey = assetsBrowseParams && JSON.stringify(assetsBrowseParams);
  var fetcher = assetsBrowseParams ? getFetcher(assetsBrowseParams) : undefined;
  var authRequired = ((_assetsBrowseParams2 = assetsBrowseParams) === null || _assetsBrowseParams2 === void 0 ? void 0 : _assetsBrowseParams2.view) === "metahumans";

  var _useAuthSWR = Object(_Auth_useAuthSWR__WEBPACK_IMPORTED_MODULE_6__["default"])(authRequired, assetsBrowseKey ? [assetsBrowseKey, page, assetsPerPage] : null, fetcher),
      data = _useAuthSWR.data,
      error = _useAuthSWR.error;

  var localAssetsResponse = assetsBrowseParams && Object(_LocalLibraryManager__WEBPACK_IMPORTED_MODULE_9__["getLocalLibraryManger"])().loadLocalAssets(assetsBrowseParams, page, assetsPerPage);
  var loading = assetsBrowseKey && !data && !error;
  Object(react__WEBPACK_IMPORTED_MODULE_2__["useEffect"])(function () {
    if (data) {
      var assets = data.assets,
          totalPages = data.totalPages;
      var newAssets = page > 0 && (response === null || response === void 0 ? void 0 : response.assets) ? [].concat(Object(_babel_runtime_helpers_esm_toConsumableArray__WEBPACK_IMPORTED_MODULE_0__["default"])(response.assets), Object(_babel_runtime_helpers_esm_toConsumableArray__WEBPACK_IMPORTED_MODULE_0__["default"])(assets)) : assets;
      setResponse({
        page: page,
        totalPages: totalPages,
        assets: newAssets
      });
    }

    if (assetsBrowseParams && localsTabSelected(assetsBrowseParams)) {
      var _newAssets = page > 0 && (localAssetsResponse === null || localAssetsResponse === void 0 ? void 0 : localAssetsResponse.assets) && (response === null || response === void 0 ? void 0 : response.assets) ? [].concat(Object(_babel_runtime_helpers_esm_toConsumableArray__WEBPACK_IMPORTED_MODULE_0__["default"])(response === null || response === void 0 ? void 0 : response.assets), Object(_babel_runtime_helpers_esm_toConsumableArray__WEBPACK_IMPORTED_MODULE_0__["default"])(localAssetsResponse.assets)) : (localAssetsResponse === null || localAssetsResponse === void 0 ? void 0 : localAssetsResponse.assets) || []; // remove duplicates


      _newAssets = _newAssets.filter(function (asset, index, self) {
        return self.findIndex(function (item) {
          return item.id === asset.id;
        }) === index;
      });
      setResponse({
        page: page,
        totalPages: (localAssetsResponse === null || localAssetsResponse === void 0 ? void 0 : localAssetsResponse.totalPages) || 0,
        assets: _newAssets
      });
    }
  }, [loading, assetsBrowseKey, page]);
  return {
    assets: response === null || response === void 0 ? void 0 : response.assets,
    page: response === null || response === void 0 ? void 0 : response.page,
    totalPages: response === null || response === void 0 ? void 0 : response.totalPages,
    loading: loading
  };
}

/* harmony default export */ __webpack_exports__["default"] = (useAssetsList);

/***/ }),

/***/ "./src/LocalLibraryManager/bifrost-local-library-manager.ts":
/*!******************************************************************!*\
  !*** ./src/LocalLibraryManager/bifrost-local-library-manager.ts ***!
  \******************************************************************/
/*! exports provided: BifrostLocalLibraryManger */
/***/ (function(module, __webpack_exports__, __webpack_require__) {

"use strict";
__webpack_require__.r(__webpack_exports__);
/* harmony export (binding) */ __webpack_require__.d(__webpack_exports__, "BifrostLocalLibraryManger", function() { return BifrostLocalLibraryManger; });
/* harmony import */ var _babel_runtime_helpers_esm_classCallCheck__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(/*! @babel/runtime/helpers/esm/classCallCheck */ "./node_modules/@babel/runtime/helpers/esm/classCallCheck.js");
/* harmony import */ var _babel_runtime_helpers_esm_createClass__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(/*! @babel/runtime/helpers/esm/createClass */ "./node_modules/@babel/runtime/helpers/esm/createClass.js");
/* harmony import */ var _babel_runtime_helpers_esm_defineProperty__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(/*! @babel/runtime/helpers/esm/defineProperty */ "./node_modules/@babel/runtime/helpers/esm/defineProperty.js");
/* harmony import */ var src_Assets_normalizeLocalAsset__WEBPACK_IMPORTED_MODULE_3__ = __webpack_require__(/*! src/Assets/normalizeLocalAsset */ "./src/Assets/normalizeLocalAsset.ts");
/* harmony import */ var src_Bifrost_useBifrostStore__WEBPACK_IMPORTED_MODULE_4__ = __webpack_require__(/*! src/Bifrost/useBifrostStore */ "./src/Bifrost/useBifrostStore.ts");




function ownKeys(object, enumerableOnly) { var keys = Object.keys(object); if (Object.getOwnPropertySymbols) { var symbols = Object.getOwnPropertySymbols(object); if (enumerableOnly) symbols = symbols.filter(function (sym) { return Object.getOwnPropertyDescriptor(object, sym).enumerable; }); keys.push.apply(keys, symbols); } return keys; }

function _objectSpread(target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i] != null ? arguments[i] : {}; if (i % 2) { ownKeys(Object(source), true).forEach(function (key) { Object(_babel_runtime_helpers_esm_defineProperty__WEBPACK_IMPORTED_MODULE_2__["default"])(target, key, source[key]); }); } else if (Object.getOwnPropertyDescriptors) { Object.defineProperties(target, Object.getOwnPropertyDescriptors(source)); } else { ownKeys(Object(source)).forEach(function (key) { Object.defineProperty(target, key, Object.getOwnPropertyDescriptor(source, key)); }); } } return target; }



var LOCALS = {
  LOCAL_MS_ASSETS: "local_downloaded",
  LOCAL_MH_ASSETS: "local_characters"
};

var BifrostLocalLibraryManger = /*#__PURE__*/function () {
  function BifrostLocalLibraryManger(socketManager) {
    Object(_babel_runtime_helpers_esm_classCallCheck__WEBPACK_IMPORTED_MODULE_0__["default"])(this, BifrostLocalLibraryManger);

    this.socketManager = socketManager;
  }

  Object(_babel_runtime_helpers_esm_createClass__WEBPACK_IMPORTED_MODULE_1__["default"])(BifrostLocalLibraryManger, [{
    key: "fetchLocalAssets",
    value: function fetchLocalAssets(params) {
      console.log("Yesss Fetching local Assets now.");
      this.socketManager.loadLocalAssets(params);
    }
  }, {
    key: "paginate",
    value: function paginate(assets, page, assetsPerPage) {
      return assets.slice(page * assetsPerPage, (page + 1) * assetsPerPage);
    }
  }, {
    key: "applySearch",
    value: function applySearch(payload) {
      var assets = payload.assets,
          params = payload.params;
      var searchTerm = "";

      if (params.search) {
        searchTerm += params.search.join(" ");
      }

      if (params.filters) {
        var filters = Object.values(params.filters).join(" ");
        searchTerm += " ".concat(filters);
      }

      if (params.category) {
        var categories = params.category.slice(1).join(" ");
        searchTerm += " ".concat(categories);
      }

      var searchArray = searchTerm.toLowerCase().trim().split(" ");
      var filteredAssets = assets.filter(function (asset) {
        if (searchArray) {
          return searchArray.every(function (searchTag) {
            return asset.searchStr.indexOf(searchTag.toLowerCase()) > -1 || asset.assetType && asset.assetType.toLowerCase().indexOf(searchTag.toLowerCase()) > -1;
          });
        }

        return true;
      });
      return filteredAssets.map(src_Assets_normalizeLocalAsset__WEBPACK_IMPORTED_MODULE_3__["default"]);
    }
  }, {
    key: "loadLocalAssets",
    value: function loadLocalAssets(params, page, assetsPerPage) {
      var _useBifrostStore$getS = src_Bifrost_useBifrostStore__WEBPACK_IMPORTED_MODULE_4__["default"].getState(),
          localLibrary = _useBifrostStore$getS.localLibrary;

      var localsCategory = params && params.category && params.category[0];

      if (localsCategory === LOCALS.LOCAL_MS_ASSETS) {
        var assets = this.applySearch({
          assets: localLibrary.assets.filter(function (asset) {
            return asset.assetType !== "metahuman";
          }).sort(function (a, b) {
            return b.mTime - a.mTime;
          }),
          params: params
        });
        return {
          assets: this.paginate(assets, page, assetsPerPage),
          totalPages: Math.ceil(assets.length / assetsPerPage),
          loading: false
        };
      } else if (localsCategory === LOCALS.LOCAL_MH_ASSETS) {
        var _assets = this.applySearch({
          assets: localLibrary.assets.filter(function (asset) {
            return asset.assetType === "metahuman";
          }).sort(function (a, b) {
            return b.mTime - a.mTime;
          }),
          params: params
        });

        return {
          assets: this.paginate(_assets, page, assetsPerPage),
          totalPages: Math.ceil(_assets.length / assetsPerPage),
          loading: false
        };
      }

      return {
        assets: [],
        totalPages: 0,
        loading: false
      };
    }
  }, {
    key: "createLocalAssetsTree",
    value: function createLocalAssetsTree(assets, categoryTree) {
      var curatedTree = [];
      categoryTree && categoryTree.forEach(function (treeItem) {
        var key = treeItem.key;

        var _loop = function _loop(i) {
          var asset = assets[i];
          var assetType = asset.assetType;

          if (assetType === key) {
            var itemsChildren = [];
            treeItem.children.forEach(function (itemChild) {
              if (asset.searchStr.indexOf(itemChild.key) != -1) {
                var childsChildren = [];
                itemChild.children.forEach(function (childsChild) {
                  if (asset.searchStr.indexOf(childsChild.key) !== -1) {
                    childsChildren.push(_objectSpread({}, childsChild, {
                      children: []
                    }));
                  }
                });
                itemsChildren.push(_objectSpread({}, itemChild, {
                  children: childsChildren
                }));
              }
            });
            curatedTree.push(_objectSpread({}, treeItem, {
              children: itemsChildren
            }));
            return "break";
          }
        };

        for (var i = 0; i < assets.length; i++) {
          var _ret = _loop(i);

          if (_ret === "break") break;
        }
      });
      return curatedTree;
    }
  }], [{
    key: "getInstance",
    value: function getInstance(socketManager) {
      if (!this.instance) {
        this.instance = new BifrostLocalLibraryManger(socketManager);
      }

      return this.instance;
    }
  }]);

  return BifrostLocalLibraryManger;
}();

Object(_babel_runtime_helpers_esm_defineProperty__WEBPACK_IMPORTED_MODULE_2__["default"])(BifrostLocalLibraryManger, "instance", void 0);



/***/ })

})
//# sourceMappingURL=[view].js.7e3030d1679552eae630.hot-update.js.map