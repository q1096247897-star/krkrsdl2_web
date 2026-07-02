/*
 * addFont.dll 的 Web shim（krkrsdl2-web）。
 *
 * 接口（对齐 wtnbgo/addFont 源码）：
 *   System.addFont(fontfilename, extract) -> int
 *     成功返回正整数（注册字体数，Web 侧恒为 1），失败返回 0。
 *
 * Web 实现：
 *   用 FontFace API 从 ArrayBuffer 加载字体（对应原版路径 3 内存加载）。
 *   字体名从 ArrayBuffer 的 name table 解析（FontFace 构造器要求显式 family）。
 *
 * 激活前提：与 menu.js 相同，需 step 11 的 C++ 分流落地后
 *   Plugins.link("addFont.dll") 才会走 shim 而非 /plugin/addFont.so。
 */
(function (global) {
  'use strict';

  function register(Module) {
    var registry = Module.krkrPluginShim || global.krkrPluginShim;
    if (!registry || registry.has('addFont.dll')) return;

    // 解析 TTF/OTF name table 取 family name。
    function parseFontFamily(buffer) {
      try {
        var view = new DataView(buffer);
        var sfVersion = view.getUint32(0, false);
        var isTtf = sfVersion === 0x00010000 || sfVersion === 0x4F54544F /* 'OTTO' */;
        if (!isTtf) return null;
        var numTables = view.getUint16(4, false);
        var nameOffset = -1;
        for (var i = 0; i < numTables; i++) {
          var rec = 12 + i * 16;
          var tag = view.getUint32(rec, false);
          if (tag === 0x6E616D65 /* 'name' */) { nameOffset = view.getUint32(rec + 8, false); break; }
        }
        if (nameOffset < 0) return null;
        var nameCount = view.getUint16(nameOffset + 2, false);
        var stringOffset = nameOffset + view.getUint16(nameOffset + 4, false);
        for (var j = 0; j < nameCount; j++) {
          var recOff = nameOffset + 6 + j * 12;
          var nameID = view.getUint16(recOff + 6, false);
          if (nameID !== 1 && nameID !== 4) continue; // 1=Family, 4=Full name
          var length = view.getUint16(recOff + 8, false);
          var offset = view.getUint16(recOff + 10, false);
          var platformID = view.getUint16(recOff, false);
          var bytes = new Uint8Array(buffer, stringOffset + offset, length);
          var str;
          if (platformID === 0 || platformID === 3) {
            str = decodeUtf16(bytes);
          } else {
            str = decodeAscii(bytes);
          }
          if (str) return str;
        }
      } catch (e) { /* 解析失败 */ }
      return null;
    }
    function decodeUtf16(bytes) {
      var s = '';
      for (var i = 0; i + 1 < bytes.length; i += 2) {
        s += String.fromCharCode((bytes[i] << 8) | bytes[i + 1]);
      }
      return s;
    }
    function decodeAscii(bytes) {
      var s = '';
      for (var i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
      return s;
    }

    function addFont(fontfilename, extract) {
      // 字体文件已由引擎写入 Emscripten FS，或可通过 storage 读取。
      // 这里通过 Module 提供的 getStorageUInt8Array 读取（kremscripten 已暴露）。
      if (typeof Module.getStorageUInt8Array !== 'function') {
        console.warn('[addFont shim] getStorageUInt8Array 不可用，无法读取 ' + fontfilename);
        return 0;
      }
      var buf = Module.getStorageUInt8Array(fontfilename);
      if (!buf || buf.byteLength === 0) {
        console.warn('[addFont shim] 字体文件为空或不存在: ' + fontfilename);
        return 0;
      }
      var ab = buf.buffer ? buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength) : buf;
      var family = parseFontFamily(ab) || fontfilename.replace(/^.*[\\/]/, '').replace(/\.[^.]+$/, '');
      try {
        var ff = new FontFace(family, ab);
        document.fonts.add(ff);
        return ff.load().then(function () { return 1; }).catch(function (e) {
          console.warn('[addFont shim] FontFace.load 失败: ' + e);
          return 0;
        });
      } catch (e) {
        console.warn('[addFont shim] FontFace 创建失败: ' + e);
        return 0;
      }
    }

    // install：C++ 分流调 api.install(Module)，用 evalTJS 给 System 挂 addFont 方法。
    // TJS 方法体通过 KirikiriEmscriptenInterface.evalJS 调 JS 侧的 addFont 函数，
    // 字体文件路径作为字符串参数传入。返回值（Promise→number）原版是同步 int，
    // Web 侧 FontFace.load 异步，这里返回 1 表示已开始加载（兼容同步语义的近似）。
    function install(Module) {
      global.__krkrAddFontShim = { addFont: addFont };
      var tjs =
        'System.addFont = function(fontfilename, extract) {' +
        '  var _r = KirikiriEmscriptenInterface.evalJS(' +
        '    "window.__krkrAddFontShim.addFont(" + JSON.stringify(fontfilename) + ")");' +
        '  // 原版返回正整数表示成功；FontFace.load 是异步，这里返回 1 表示已提交' +
        '  return _r ? 1 : 0;' +
        '};';
      try {
        Module.evalTJS(tjs);
        console.log('[addFont shim] installed System.addFont');
      } catch (e) {
        console.error('[addFont shim] install failed', e);
      }
    }

    registry.register('addFont.dll', { addFont: addFont, install: install });
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
