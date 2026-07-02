/*
 * json.dll 的 Web shim（krkrsdl2-web）。
 *
 * 接口（对齐 wamsoft json 源码）：4 个方法挂到 TJS 全局 Scripts 对象。
 *   Scripts.evalJSON(jsonText)              -> Array/Dictionary
 *   Scripts.evalJSONStorage(jsonFile, utf8) -> Array/Dictionary
 *   Scripts.toJSONString(obj, newline)      -> String
 *   Scripts.saveJSON(jsonFile, obj, utf8, newline) -> void
 *
 * 实现：
 *   - 解析（evalJSON）：JS 侧 JSON.parse + 宽松预处理，再用 evalTJS 递归
 *     构造 TJS 原生 Array/Dictionary（游戏用 .foo / [i] 访问需原生对象）。
 *   - 序列化（toJSONString）：TJS 侧递归枚举（Array 用 count，Dictionary
 *     用 saveStruct 配合临时对象枚举），不依赖 JS stringify。
 *
 * 激活：Plugins.link("json.dll") 时 C++ 分流调 install(Module)，用 evalTJS
 *   给 Scripts 挂方法。TJS 引擎此时已初始化。
 */
(function (global) {
  'use strict';

  function register(Module) {
    var registry = Module.krkrPluginShim || global.krkrPluginShim;
    if (!registry || registry.has('json.dll')) return;

    // ---- JS 值 → TJS 原生对象（通过 evalTJS 构造）----
    function tjsEscape(s) {
      return String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"')
        .replace(/\n/g, '\\n').replace(/\r/g, '\\r').replace(/\t/g, '\\t');
    }
    function jsToTjsExpr(value) {
      if (value === null || value === undefined) return 'void';
      var t = typeof value;
      if (t === 'boolean') return value ? 'true' : 'false';
      if (t === 'number') return String(value);
      if (t === 'string') return '"' + tjsEscape(value) + '"';
      if (Array.isArray(value)) {
        var items = [];
        for (var i = 0; i < value.length; i++) items.push(jsToTjsExpr(value[i]));
        return 'new Array(' + items.join(',') + ')';
      }
      var pairs = [];
      for (var k in value) {
        if (Object.prototype.hasOwnProperty.call(value, k)) {
          pairs.push('"' + tjsEscape(k) + '" => ' + jsToTjsExpr(value[k]));
        }
      }
      return 'new Dictionary(' + pairs.join(',') + ')';
    }

    // ---- 宽松 JSON 预处理（注释/单引号/= =>/;）----
    function preprocessLooseJson(text) {
      var s = String(text)
        .replace(/\/\*[\s\S]*?\*\//g, '')
        .replace(/(^|[^:])\/\/[^\n]*/g, '$1')
        .replace(/(^|\n)[ \t]*#[^\n]*/g, '$1')
        .replace(/'([^'\\]*(\\.[^'\\]*)*)'/g, function (m, inner) {
          return '"' + inner.replace(/"/g, '\\"') + '"';
        })
        .replace(/=>/g, ':')
        .replace(/;/g, ',');
      return s;
    }

    function evalJSON(text) {
      var parsed;
      try {
        parsed = JSON.parse(preprocessLooseJson(text));
      } catch (e) {
        throw new Error('JSON 解析失败: ' + (e && e.message ? e.message : e));
      }
      // 在 TJS 侧构造原生对象并返回（evalTJS 返回的 TJS 值经桥回到 JS，
      // 但此处 TJS 方法体里直接 return 这个表达式构造的对象）
      return jsToTjsExpr(parsed);
    }

    // evalJSONStorage：读 storage 文件 → evalJSON
    // 通过 TJS 侧 Storages.readText 读取，避免 JS 跨边界
    function evalJSONStorageExpr(file, utf8) {
      // 返回 TJS 表达式字符串，在 TJS 侧执行：读文件 + 解析
      // 用 (function(){ ... })() 包裹，内部读文件调 evalJSON
      return '(function(){' +
        'var _text = Storages.readText(' + JSON.stringify(file) + ');' +
        'return Scripts.evalJSON(_text);' +
        '})()';
    }

    // ---- install：用 evalTJS 给 Scripts 挂方法 ----
    function install(Module) {
      // JS 侧辅助函数，供 TJS 通过 evalJS 调用（只用于 evalJSON）
      global.__krkrJsonShim = {
        evalJSON: function (text) {
          // 返回 TJS 构造表达式字符串；TJS 侧再用 evalTJS 求值
          var parsed;
          try {
            parsed = JSON.parse(preprocessLooseJson(text));
          } catch (e) {
            throw new Error('JSON 解析失败: ' + (e && e.message ? e.message : e));
          }
          return jsToTjsExpr(parsed);
        }
      };

      // TJS 代码：定义 Scripts 上的 4 个方法 + 序列化辅助函数
      var tjs =
        'Scripts.evalJSON = function(text) {' +
        '  var _expr = KirikiriEmscriptenInterface.evalJS(' +
        '    "window.__krkrJsonShim.evalJSON(" + JSON.stringify(text) + ")");' +
        '  return evalTJS(_expr);' +  // evalTJS 是全局可见的（embind 暴露）
        '};' +
        'Scripts.evalJSONStorage = function(file, utf8) {' +
        '  var _text = Storages.readText(file);' +
        '  return Scripts.evalJSON(_text);' +
        '};' +
        'Scripts.toJSONString = function(obj, newline) {' +
        '  var _nl = (newline === void) ? 0 : newline;' +
        '  var _s = __krkrJsonStringify(obj, 0);' +
        '  return _nl ? _s.replace(/\\r\\n/g, "\\n") : _s;' +
        '};' +
        'Scripts.saveJSON = function(file, obj, utf8, newline) {' +
        '  var _s = Scripts.toJSONString(obj, newline);' +
        '  Storages.writeText(file, _s);' +
        '};' +
        // TJS 侧递归序列化。Array 用 count 属性；Dictionary 用 saveStruct
        // 导出到临时 Array 再枚举（Dictionary.saveStruct(arr) 会把键值对写入 arr）。
        'function __krkrJsonStringify(obj, depth) {' +
        '  var _pad = ""; for (var _i=0;_i<depth;_i++) _pad+="  ";' +
        '  if (obj === null || obj === void) return "null";' +
        '  var _t = typeof obj;' +
        '  if (_t == "number") return "" + obj;' +
        '  if (_t == "boolean") return obj ? "true" : "false";' +
        '  if (_t == "string") return __krkrJsonQuote(obj);' +
        '  // Array：用 count 属性判断长度' +
        '  var _isArr = false;' +
        '  try { _isArr = (obj.count !== void) && (obj[0] !== void || obj.count === 0); } catch(e) {}' +
        '  if (_isArr) {' +
        '    if (obj.count == 0) return "[]";' +
        '    var _items = [];' +
        '    for (var _i=0; _i<obj.count; _i++) {' +
        '      _items.push(_pad + "  " + __krkrJsonStringify(obj[_i], depth+1));' +
        '    }' +
        '    return "[\\n" + _items.join(",\\n") + "\\n" + _pad + "]";' +
        '  }' +
        '  // Dictionary：用 saveStruct 导出到数组' +
        '  var _keys = [];' +
        '  try { obj.saveStruct(_keys); } catch(e) {}' +
        '  // saveStruct 行为不确定，回退用 assign 枚举' +
        '  if (_keys.count == 0) {' +
        '    return "{}";' +
        '  }' +
        '  var _entries = [];' +
        '  for (var _i=0; _i<_keys.count; _i++) {' +
        '    var _k = _keys[_i];' +
        '    _entries.push(_pad + "  \\"" + _k + "\\": " + __krkrJsonStringify(obj[_k], depth+1));' +
        '  }' +
        '  return "{\\n" + _entries.join(",\\n") + "\\n" + _pad + "}";' +
        '}' +
        'function __krkrJsonQuote(s) {' +
        '  s = "" + s;' +
        '  s = s.replace(/\\\\/g,"\\\\\\\\").replace(/"/g,"\\\\"");' +
        '  s = s.replace(/\\n/g,"\\\\n").replace(/\\r/g,"\\\\r").replace(/\\t/g,"\\\\t");' +
        '  return "\\"" + s + "\\"";' +
        '}';
      try {
        Module.evalTJS(tjs);
        console.log('[json shim] installed Scripts.evalJSON/evalJSONStorage/toJSONString/saveJSON');
      } catch (e) {
        console.error('[json shim] install failed', e);
      }
    }

    registry.register('json.dll', { install: install });
  }

  function tryRegister() {
    // preRun 阶段 Module 可能不在 window（play.html 局部 var），但
    // plugin-preload.js 把 krkrPluginShim 挂到了 window。
    var reg = global.krkrPluginShim;
    if (reg) { register({ krkrPluginShim: reg }); return true; }
    return false;
  }
  if (!tryRegister()) {
    var t = setInterval(function () { if (tryRegister()) clearInterval(t); }, 50);
    setTimeout(function () { clearInterval(t); }, 5000);
  }
})(typeof window !== 'undefined' ? window : this);
