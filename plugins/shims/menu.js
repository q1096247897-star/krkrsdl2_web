/*
 * menu.dll 的 Web shim（krkrsdl2-web）。
 *
 * 接口（对齐 krkrz/menu 源码）：
 *   - 全局类 MenuItem：caption/checked/radio/group/enabled/visible/shortcut/index
 *     等属性，add/insert/remove/popup 方法，onClick 事件。
 *   - Window.menu 只读属性：惰性返回根 MenuItem。
 *   - 快捷键：解析 "Shift+Ctrl+Alt+<键名>"，命中时触发 onClick 并吞掉
 *     onKeyDown 冒泡。
 *
 * 激活前提（重要）：
 *   本 shim 只在 JS 侧注册好对象树和 DOM 菜单。要让 TJS 的
 *   Plugins.link("menu.dll") 真正走 shim 而不是 SDL_LoadObject，还需要
 *   step 11 的 C++ 分流（TVPHasWebPluginShim / TVPLoadWebPluginShim，见
 *   plugin-compatibility-execution-steps.md §11.3）。该分流未实现前，
 *   Plugins.link("menu.dll") 仍会尝试加载 /plugin/menu.so 并失败。
 *   本文件提前就位，等 C++ 分流落地后即可生效。
 *
 * 通过 Module.evalTJS / KirikiriEmscriptenInterface.evalJS 与 TJS 互操作。
 */
(function (global) {
  'use strict';

  function register(Module) {
    var registry = Module.krkrPluginShim || global.krkrPluginShim;
    if (!registry || registry.has('menu.dll')) return;

    var MENU_BAR_ID = 'krkrsdl2-menubar';
    var menuBar = null;
    var shortcutListeners = []; // {item, handler}
    var menuItems = {}; // id -> MenuItem 实例（TJS 侧通过 id 引用）
    var menuIdSeq = 0;

    function ensureMenuBar() {
      if (menuBar) return menuBar;
      menuBar = document.createElement('ul');
      menuBar.id = MENU_BAR_ID;
      menuBar.style.cssText =
        'position:fixed;top:0;left:0;right:0;z-index:50;list-style:none;' +
        'margin:0;padding:0;height:24px;background:#1e1e1e;color:#ddd;' +
        'font:13px sans-serif;display:flex;';
      document.body.appendChild(menuBar);
      return menuBar;
    }

    function parseShortcut(text) {
      if (!text) return null;
      var parts = String(text).toLowerCase().split('+').map(function (s) { return s.trim(); });
      var key = parts[parts.length - 1];
      var needShift = parts.indexOf('shift') >= 0;
      var needCtrl = parts.indexOf('ctrl') >= 0 || parts.indexOf('control') >= 0;
      var needAlt = parts.indexOf('alt') >= 0;
      return { key: key, shift: needShift, ctrl: needCtrl, alt: needAlt };
    }

    var KEY_ALIASES = {
      bksp: 'backspace', pgup: 'pageup', pgdn: 'pagedown',
      esc: 'escape', del: 'delete', ins: 'insert'
    };
    function normalizeKey(k) {
      k = String(k || '').toLowerCase();
      return KEY_ALIASES[k] || k;
    }

    function shortcutMatches(sc, ev) {
      if (!sc) return false;
      var evKey = normalizeKey(ev.key);
      if (evKey !== sc.key && evKey.length !== 1 && sc.key.length !== 1) {
        // 单字符键比较：event.key 可能是 's'，shortcut.key 也是 's'
        if (evKey !== sc.key) return false;
      } else if (evKey !== sc.key) {
        return false;
      }
      return !!ev.shiftKey === sc.shift && !!ev.ctrlKey === sc.ctrl && !!ev.altKey === sc.alt;
    }

    function bindShortcut(item) {
      var sc = parseShortcut(item.shortcut);
      if (!sc) return;
      var handler = function (ev) {
        if (shortcutMatches(sc, ev)) {
          ev.preventDefault();
          ev.stopPropagation();
          fireOnClick(item);
        }
      };
      window.addEventListener('keydown', handler, true);
      shortcutListeners.push({ item: item, handler: handler });
    }

    function unbindShortcut(item) {
      for (var i = shortcutListeners.length - 1; i >= 0; i--) {
        if (shortcutListeners[i].item === item) {
          window.removeEventListener('keydown', shortcutListeners[i].handler, true);
          shortcutListeners.splice(i, 1);
        }
      }
    }

    function fireOnClick(item) {
      if (!item.enabled || !item.visible) return;
      if (item.radio && item.group != null) {
        // 同组互斥
        var parent = item.parent;
        if (parent) {
          for (var i = 0; i < parent.children.length; i++) {
            var sib = parent.children[i];
            if (sib !== item && sib.radio && sib.group === item.group) sib.checked = false;
          }
        }
      }
      if (typeof item.onClick === 'function') {
        try { item.onClick(); } catch (e) { console.error('[menu shim] onClick threw', e); }
      } else if (typeof Module.evalTJS === 'function' && typeof item._onClickTjs === 'string') {
        Module.evalTJS(item._onClickTjs);
      }
    }

    function renderDom(item) {
      if (!item._dom) {
        var li = document.createElement('li');
        li.style.cssText = 'position:relative;list-style:none;padding:2px 10px;cursor:default;';
        li.textContent = item.caption === '-' ? '' : item.caption;
        if (item.caption === '-') {
          li.style.borderTop = '1px solid #555';
          li.style.height = '1px';
          li.style.padding = '0';
        }
        if (!item.enabled) li.style.opacity = '0.4';
        if (item.checked) li.textContent = '✓ ' + li.textContent;
        item._dom = li;
        li.addEventListener('click', function () { fireOnClick(item); });
      } else {
        var li2 = item._dom;
        li2.textContent = item.caption === '-' ? '' : item.caption;
        if (item.caption === '-') { /* 分隔符 */ }
        else if (item.checked) li2.textContent = '✓ ' + item.caption;
        li2.style.opacity = item.enabled ? '1' : '0.4';
      }
      return item._dom;
    }

    function MenuItem(ownerWindow, caption) {
      this.caption = (caption === undefined) ? '' : String(caption);
      this.checked = false;
      this.radio = false;
      this.group = 0;
      this.enabled = true;
      this.visible = true;
      this.shortcut = '';
      this.index = 0;
      this.children = [];
      this.parent = null;
      this.root = null;
      this.window = ownerWindow || null;
      this.onClick = null;
      this._onClickTjs = null;
      this._dom = null;
    }
    MenuItem.prototype.add = function (child) {
      this.children.push(child);
      child.parent = this;
      child.root = this.root || this;
      if (child.shortcut) bindShortcut(child);
    };
    MenuItem.prototype.insert = function (child, idx) {
      this.children.splice(idx, 0, child);
      child.parent = this;
      child.root = this.root || this;
      if (child.shortcut) bindShortcut(child);
    };
    MenuItem.prototype.remove = function (child) {
      var i = this.children.indexOf(child);
      if (i >= 0) this.children.splice(i, 1);
      unbindShortcut(child);
      child.parent = null;
    };
    MenuItem.prototype.popup = function (flags, x, y) {
      // 简化：在 (x,y) 显示一个浮层菜单。原版同步阻塞，Web 侧异步。
      var overlay = document.createElement('div');
      overlay.style.cssText =
        'position:fixed;z-index:100;background:#2a2a2a;color:#ddd;border:1px solid #555;' +
        'min-width:120px;font:13px sans-serif;';
      overlay.style.left = x + 'px';
      overlay.style.top = y + 'px';
      this.children.forEach(function (c) {
        var row = renderDom(c).cloneNode(true);
        row.addEventListener('click', function () { fireOnClick(c); overlay.remove(); });
        overlay.appendChild(row);
      });
      document.body.appendChild(overlay);
      setTimeout(function () {
        var close = function () { overlay.remove(); document.removeEventListener('click', close); };
        document.addEventListener('click', close);
      }, 0);
    };

    // install：用 evalTJS 定义 TJS 全局 MenuItem 类 + Window.menu 属性。
    // TJS 类的方法/属性通过 KirikiriEmscriptenInterface.evalJS 回调 JS 侧的
    // MenuItem 实现。P0 最小集：构造、caption/checked/enabled/visible/shortcut、
    // add/insert/remove、onClick。popup/高级属性后续补。
    function install(Module) {
      global.__krkrMenuShim = {
        createMenuItem: function (windowRef, caption) {
          var item = new MenuItem(windowRef, caption || '');
          // 返回一个 id 供 TJS 侧引用（JS 对象无法直接传给 TJS）
          var id = '__krkrMenuItem_' + (++menuIdSeq);
          menuItems[id] = item;
          return id;
        },
        setProp: function (id, prop, val) {
          if (menuItems[id]) menuItems[id][prop] = val;
        },
        getProp: function (id, prop) {
          return menuItems[id] ? menuItems[id][prop] : undefined;
        },
        add: function (parentId, childId) {
          if (menuItems[parentId] && menuItems[childId]) menuItems[parentId].add(menuItems[childId]);
        },
        insert: function (parentId, childId, idx) {
          if (menuItems[parentId] && menuItems[childId]) menuItems[parentId].insert(menuItems[childId], idx);
        },
        remove: function (parentId, childId) {
          if (menuItems[parentId] && menuItems[childId]) menuItems[parentId].remove(menuItems[childId]);
        },
        setOnClick: function (id, tjsExpr) {
          if (menuItems[id]) menuItems[id]._onClickTjs = tjsExpr;
        }
      };
      var menuBar = ensureMenuBar();

      var tjs =
        // 全局 MenuItem 类：构造调 JS 创建，属性 get/set 调 JS
        'class MenuItem {' +
        '  var _id = "";' +
        '  function MenuItem(window, caption) {' +
        '    _id = KirikiriEmscriptenInterface.evalJS(' +
        '      "window.__krkrMenuShim.createMenuItem(null," + JSON.stringify(caption === void ? "" : caption) + ")");' +
        '  }' +
        '  property caption { get { return KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.getProp(" + JSON.stringify(_id) + ",\\"caption\\")"); } set(v) { KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.setProp(" + JSON.stringify(_id) + ",\\"caption\\"," + JSON.stringify(v) + ")"); } }' +
        '  property checked { get { return KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.getProp(" + JSON.stringify(_id) + ",\\"checked\\")"); } set(v) { KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.setProp(" + JSON.stringify(_id) + ",\\"checked\\"," + JSON.stringify(v) + ")"); } }' +
        '  property enabled { get { return KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.getProp(" + JSON.stringify(_id) + ",\\"enabled\\")"); } set(v) { KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.setProp(" + JSON.stringify(_id) + ",\\"enabled\\"," + JSON.stringify(v) + ")"); } }' +
        '  property visible { get { return KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.getProp(" + JSON.stringify(_id) + ",\\"visible\\")"); } set(v) { KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.setProp(" + JSON.stringify(_id) + ",\\"visible\\"," + JSON.stringify(v) + ")"); } }' +
        '  property shortcut { get { return KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.getProp(" + JSON.stringify(_id) + ",\\"shortcut\\")"); } set(v) { KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.setProp(" + JSON.stringify(_id) + ",\\"shortcut\\"," + JSON.stringify(v) + ")"); } }' +
        '  function add(item) { KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.add(" + JSON.stringify(_id) + "," + JSON.stringify(item._id) + ")"); }' +
        '  function insert(item, idx) { KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.insert(" + JSON.stringify(_id) + "," + JSON.stringify(item._id) + "," + idx + ")"); }' +
        '  function remove(item) { KirikiriEmscriptenInterface.evalJS("window.__krkrMenuShim.remove(" + JSON.stringify(_id) + "," + JSON.stringify(item._id) + ")"); }' +
        '  function onClick() {}' +  // TJS 侧覆盖；JS 侧 fireOnClick 优先调 JS onClick，否则 evalTJS _onClickTjs
        '}' +
        // Window.menu getter：惰性创建根 MenuItem
        'Global.Window.menu = void;' +
        'Object.defineProperty(Global.Window, "menu", {' +
        '  get: function() {' +
        '    if (Global.Window._krkrMenuRoot === void) {' +
        '      Global.Window._krkrMenuRoot = new MenuItem(null, "");' +
        '    }' +
        '    return Global.Window._krkrMenuRoot;' +
        '  }' +
        '});';
      try {
        Module.evalTJS(tjs);
        console.log('[menu shim] installed MenuItem class + Window.menu');
      } catch (e) {
        console.error('[menu shim] install failed', e);
      }
    }

    registry.register('menu.dll', { MenuItem: MenuItem, install: install });
  }

  // 暴露给 plugin-preload.js 的加载入口：脚本加载后立即注册到 shim registry。
  // 但此时 Module.krkrPluginShim 可能尚未初始化（preload 脚本会先 ensureShimRegistry），
  // 所以延迟到 Module 就绪。这里轮询，简单可靠。
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
