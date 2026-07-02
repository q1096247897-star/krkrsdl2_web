/*
 * fstat.dll 的 Web shim（krkrsdl2-web）。
 *
 * 接口（对齐 wamsoft/fstat 源码）：方法挂到 TJS 全局 Storages 对象。
 * P0 实现：
 *   Storages.fstat(filename)              -> Dictionary %[size, mtime, atime, ctime]
 *   Storages.getTime(target)              -> Dictionary %[mtime, atime, ctime]
 *   Storages.isExistentDirectory(dir)     -> bool
 *   Storages.isExistentStorageNoSearchNoNormalize(filename) -> bool
 *   Storages.dirlist(dir)                 -> Array of filename (文件夹加 / 后缀)
 *   Storages.getLastModifiedFileTime(target) -> uint64 (失败 0)
 *
 * Web 实现：用 Emscripten FS.stat/FS.readdir。storage 名 → FS 路径（加前导 /）。
 *   存档内文件（XP3 内）FS 无法直接 stat，fstat 返回仅 size（通过 FS.stat 尝试，
 *   失败则抛 TJS 异常，与原版一致）。
 *
 * 激活：Plugins.link("fstat.dll") 时 C++ 分流调 install(Module)。
 */
(function (global) {
  'use strict';

  function register(Module) {
    var registry = Module.krkrPluginShim || global.krkrPluginShim;
    if (!registry || registry.has('fstat.dll')) return;

    // storage 名 → FS 路径。引擎 storage 名是相对路径（如 "data/foo.txt"），
    // FS 里对应 "/data/foo.txt"。也兼容已带 / 的输入。
    function toFsPath(storageName) {
      var p = String(storageName);
      if (p.charAt(0) === '/') return p;
      return '/' + p;
    }

    // 安全 stat：返回 stat 对象或 null（不存在）
    function safeStat(fsPath) {
      try {
        return Module.FS.stat(fsPath);
      } catch (e) {
        return null;
      }
    }

    // ---- JS 实现（供 TJS 通过 evalJS 调用）----
    // fstat: 返回 TJS Dictionary 构造表达式
    function fstatExpr(filename) {
      var st = safeStat(toFsPath(filename));
      if (!st) {
        // 原版抛 "cannot open : <name>"
        throw new Error('cannot open : ' + filename);
      }
      // Emscripten FS.stat 返回 {size, mtime, atime, ctime, mode, ...}
      // mtime/atime/ctime 是 Unix 秒数；TJS Date 构造用毫秒
      var size = st.size || 0;
      var mt = (st.mtime || 0) * 1000;
      var at = (st.atime || 0) * 1000;
      var ct = (st.ctime || 0) * 1000;
      // 返回 TJS 表达式：构造 Dictionary
      return 'new Dictionary("size"=>' + size +
        ',"mtime"=>new Date(' + mt + '),"atime"=>new Date(' + at +
        '),"ctime"=>new Date(' + ct + '))';
    }

    function getTimeExpr(target) {
      var st = safeStat(toFsPath(target));
      if (!st) throw new Error('cannot open : ' + target);
      var mt = (st.mtime || 0) * 1000;
      var at = (st.atime || 0) * 1000;
      var ct = (st.ctime || 0) * 1000;
      return 'new Dictionary("mtime"=>new Date(' + mt +
        '),"atime"=>new Date(' + at + '),"ctime"=>new Date(' + ct + '))';
    }

    function isExistentDirectory(dir) {
      var st = safeStat(toFsPath(dir));
      return !!(st && Module.FS.isDir(st.mode));
    }

    function isExistentStorageNoSearchNoNormalize(filename) {
      var st = safeStat(toFsPath(filename));
      return !!st;
    }

    function getLastModifiedFileTime(target) {
      var st = safeStat(toFsPath(target));
      if (!st) return 0;
      // 原版返回 Windows FILETIME（100ns 间隔，1601 起）。Web 无直接对应，
      // 返回 Unix 毫秒数（与原版语义不同但可用于相对比较）。失败 0。
      return (st.mtime || 0) * 1000;
    }

    // dirlist: 返回 TJS Array 构造表达式（文件夹加 / 后缀）
    function dirlistExpr(dir) {
      var fsPath = toFsPath(dir);
      var names;
      try {
        names = Module.FS.readdir(fsPath);
      } catch (e) {
        // 原版抛异常
        throw new Error('cannot open : ' + dir);
      }
      var items = [];
      for (var i = 0; i < names.length; i++) {
        var n = names[i];
        if (n === '.' || n === '..') continue;
        var childPath = (fsPath === '/' ? '' : fsPath) + '/' + n;
        var cst = safeStat(childPath);
        if (cst && Module.FS.isDir(cst.mode)) {
          items.push('"' + tjsEscape(n + '/') + '"');
        } else {
          items.push('"' + tjsEscape(n) + '"');
        }
      }
      return 'new Array(' + items.join(',') + ')';
    }

    function tjsEscape(s) {
      return String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    }

    // ---- install：用 evalTJS 给 Storages 挂方法 ----
    function install(Module) {
      global.__krkrFstatShim = {
        fstatExpr: fstatExpr,
        getTimeExpr: getTimeExpr,
        isExistentDirectory: isExistentDirectory,
        isExistentStorageNoSearchNoNormalize: isExistentStorageNoSearchNoNormalize,
        getLastModifiedFileTime: getLastModifiedFileTime,
        dirlistExpr: dirlistExpr
      };
      var tjs =
        'Storages.fstat = function(filename) {' +
        '  var _e = KirikiriEmscriptenInterface.evalJS(' +
        '    "window.__krkrFstatShim.fstatExpr(" + JSON.stringify(filename) + ")");' +
        '  return evalTJS(_e);' +
        '};' +
        'Storages.getTime = function(target) {' +
        '  var _e = KirikiriEmscriptenInterface.evalJS(' +
        '    "window.__krkrFstatShim.getTimeExpr(" + JSON.stringify(target) + ")");' +
        '  return evalTJS(_e);' +
        '};' +
        'Storages.isExistentDirectory = function(dir) {' +
        '  return KirikiriEmscriptenInterface.evalJS(' +
        '    "window.__krkrFstatShim.isExistentDirectory(" + JSON.stringify(dir) + ")");' +
        '};' +
        'Storages.isExistentStorageNoSearchNoNormalize = function(filename) {' +
        '  return KirikiriEmscriptenInterface.evalJS(' +
        '    "window.__krkrFstatShim.isExistentStorageNoSearchNoNormalize(" + JSON.stringify(filename) + ")");' +
        '};' +
        'Storages.getLastModifiedFileTime = function(target) {' +
        '  return KirikiriEmscriptenInterface.evalJS(' +
        '    "window.__krkrFstatShim.getLastModifiedFileTime(" + JSON.stringify(target) + ")");' +
        '};' +
        'Storages.dirlist = function(dir) {' +
        '  var _e = KirikiriEmscriptenInterface.evalJS(' +
        '    "window.__krkrFstatShim.dirlistExpr(" + JSON.stringify(dir) + ")");' +
        '  return evalTJS(_e);' +
        '};';
      try {
        Module.evalTJS(tjs);
        console.log('[fstat shim] installed Storages.fstat/getTime/isExistentDirectory/isExistentStorageNoSearchNoNormalize/getLastModifiedFileTime/dirlist');
      } catch (e) {
        console.error('[fstat shim] install failed', e);
      }
    }

    registry.register('fstat.dll', { install: install });
  }

  function tryRegister() {
    var reg = global.krkrPluginShim;
    if (reg) { register({ krkrPluginShim: reg }); return true; }
    return false;
  }
  if (!tryRegister()) {
    var t = setInterval(function () { if (tryRegister()) clearInterval(t); }, 50);
    setTimeout(function () { clearInterval(t); }, 5000);
  }
})(typeof window !== 'undefined' ? window : this);
