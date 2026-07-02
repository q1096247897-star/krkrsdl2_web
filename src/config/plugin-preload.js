/*
 * krkrsdl2-web 插件预加载器。
 *
 * 职责：
 *   1. 读取 plugins/manifest.json（插件兼容清单）。
 *   2. 对 side-module 插件：下载 .so，写入 Emscripten FS 的 /plugin/<name>.so。
 *   3. 对 web-shim 插件：通过 <script> 标签加载 .js，由 shim 自行注册到
 *      Module.krkrPluginShim（供未来 step 11 的 C++ 分流调用）。
 *
 * 为什么要预加载：
 *   TJS 的 Plugins.link 是同步调用，但浏览器网络下载是异步的。所以必须在
 *   引擎 main() 运行前把插件产物写入 FS，否则 Plugins.link("X.dll") 会因
 *   找不到 /plugin/X.so 而抛 TVPCannotLoadPlugin。
 *
 * 接入方式（见 play.html.in）：
 *   <script src="plugin-preload.js"></script>
 *   Module.preRun.push(function () {
 *     preloadKirikiriPlugins(Module, 'plugins/manifest.json');
 *   });
 *
 * 通过 addRunDependency / removeRunDependency 让引擎等待预加载完成。
 * 预加载失败不阻断启动：缺失插件会在 Plugins.link 时报明确错误。
 */
(function (global) {
  'use strict';

  var TAG = '[krkrsdl2-plugin]';

  function log(msg) {
    if (global.console) console.log(TAG + ' ' + msg);
  }

  function fetchArrayBuffer(url) {
    return fetch(url, { cache: 'no-store' }).then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status + ' for ' + url);
      return r.arrayBuffer();
    });
  }

  function fetchJson(url) {
    return fetch(url, { cache: 'no-store' }).then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status + ' for ' + url);
      return r.json();
    });
  }

  function loadScript(url) {
    return new Promise(function (resolve, reject) {
      var s = document.createElement('script');
      s.src = url;
      s.onload = function () { resolve(); };
      s.onerror = function () { reject(new Error('Failed to load ' + url)); };
      document.body.appendChild(s);
    });
  }

  // "plugin/KAGParser.so" -> "/plugin/KAGParser.so"
  function fsPathFor(webModule) {
    return '/' + String(webModule).replace(/^\/+/, '');
  }

  function ensureDir(Module, path) {
    try { Module['FS'].mkdirTree(path); } catch (e) { /* 已存在则忽略 */ }
  }

  // 初始化 web-shim 注册表（step 11 的 C++ 分流会读取这个对象）。
  // 同时挂到 window 上，让 shim 脚本（<script> 加载，只能访问 window）
  // 能在 preRun 阶段 register 自己。
  function ensureShimRegistry(Module) {
    var reg = Module.krkrPluginShim;
    if (!reg) {
      var entries = {};
      // Normalize plugin name: strip dir prefix (e.g. plugin/), lowercase.
      // Game writes Plugins.link("plugin/menu.dll"); manifest/shim use "menu.dll".
      function norm(n) {
        var s = String(n).replace(/^\/+/, "");
        var slash = s.lastIndexOf("/");
        if (slash >= 0) s = s.slice(slash + 1);
        return s.toLowerCase();
      }
      reg = {
        _entries: entries,
        register: function (name, api) { entries[norm(name)] = api; },
        has: function (name) { return Object.prototype.hasOwnProperty.call(entries, norm(name)); },
        get: function (name) { return entries[norm(name)] || null; }
      };
      Module.krkrPluginShim = reg;
    }
    // 始终同步到 window，让 shim 脚本能访问（play.html 的 Module 是局部 var）
    if (typeof window !== 'undefined') {
      window.krkrPluginShim = reg;
    }
    return reg;
  }

  function isSideModule(type) {
    return type === 'side-module' ||
           type === 'side-module-with-fs-adapter' ||
           type === 'side-module-with-text-adapter' ||
           type === 'side-module-or-shim';
  }

  function preloadOne(Module, plugin) {
    var type = plugin.implementationType;
    var module = plugin.webModule;
    var dll = plugin.dllName;

    if (!module) {
      log('skip ' + dll + ': no webModule');
      return Promise.resolve();
    }

    if (isSideModule(type)) {
      var target = fsPathFor(module);
      var dir = target.substring(0, target.lastIndexOf('/'));
      ensureDir(Module, dir);
      return fetchArrayBuffer(module).then(function (buf) {
        Module['FS'].writeFile(target, new Uint8Array(buf), { canOwn: true });
        log('request=' + dll + ' type=' + type + ' status=loaded module=' + target);
      }).catch(function (err) {
        // side-module-or-shim 允许回退到 .js shim。
        if (type === 'side-module-or-shim' && /\.js$/i.test(module)) {
          return loadScript(module).then(function () {
            log('request=' + dll + ' type=web-shim(fallback) status=loaded module=' + module);
          });
        }
        log('request=' + dll + ' status=missing reason=' + (err && err.message ? err.message : err));
      });
    }

    if (type === 'web-shim') {
      ensureShimRegistry(Module);
      return loadScript(module).then(function () {
        log('request=' + dll + ' type=web-shim status=loaded module=' + module);
      }).catch(function (err) {
        log('request=' + dll + ' status=missing reason=' + (err && err.message ? err.message : err));
      });
    }

    log('skip ' + dll + ': unknown implementationType=' + type);
    return Promise.resolve();
  }

  global.preloadKirikiriPlugins = function (Module, manifestUrl) {
    ensureShimRegistry(Module);
    Module['addRunDependency']('krkrsdl2-plugins');

    fetchJson(manifestUrl).then(function (manifest) {
      manifest = manifest || {};
      var plugins = manifest.plugins || [];
      log('manifest loaded: ' + plugins.length + ' plugin(s) from ' + manifestUrl);
      var chain = Promise.resolve();
      plugins.forEach(function (plugin) {
        chain = chain.then(function () { return preloadOne(Module, plugin); });
      });
      return chain;
    }).then(function () {
      log('preload complete');
      Module['removeRunDependency']('krkrsdl2-plugins');
    }).catch(function (err) {
      log('preload FAILED: ' + (err && err.message ? err.message : err) +
          ' — 引擎仍会启动，缺失插件将在 Plugins.link 时报错');
      Module['removeRunDependency']('krkrsdl2-plugins');
    });
  };
})(typeof window !== 'undefined' ? window : this);
