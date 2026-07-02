/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "tjsCommHead.h"

#ifdef __EMSCRIPTEN__

#include "tjsNative.h"
#include "tjsObject.h"
#include "tjsArray.h"
#include "Extension.h"
#include "DebugIntf.h"
#include "ScriptMgnIntf.h"

class tTVPWebMenuItemObject : public tTJSCustomObject
{
	typedef tTJSCustomObject inherited;

	tTJSVariant Children;
	tjs_int ChildCount;

	void ResetChildren()
	{
		iTJSDispatch2 *array = TJSCreateArrayObject();
		Children = tTJSVariant(array, array);
		array->Release();
		ChildCount = 0;
	}

	tjs_error AppendChild(tTJSVariant *child, iTJSDispatch2 *objthis, tTJSVariant *result)
	{
		if(!child)
		{
			if(result) result->Clear();
			return TJS_S_OK;
		}

		tTJSVariantClosure children = Children.AsObjectClosureNoAddRef();
		if(children.Object)
		{
			children.PropSetByNum(TJS_MEMBERENSURE, ChildCount++, child,
				children.ObjThis ? children.ObjThis : children.Object);
		}

		if(child->Type() == tvtObject)
		{
			tTJSVariantClosure child_closure = child->AsObjectClosureNoAddRef();
			if(child_closure.Object)
			{
				iTJSDispatch2 *self = objthis ? objthis : this;
				tTJSVariant parent(self, self);
				child_closure.PropSet(TJS_MEMBERENSURE, TJS_W("parent"), NULL,
					&parent, child_closure.ObjThis ? child_closure.ObjThis : child_closure.Object);
			}
		}

		if(result) result->CopyRef(*child);
		return TJS_S_OK;
	}

	tjs_error InsertChild(tjs_int index, tTJSVariant *child, iTJSDispatch2 *objthis, tTJSVariant *result)
	{
		if(!child) return AppendChild(child, objthis, result);
		if(index < 0) index = 0;
		if(index > ChildCount) index = ChildCount;

		tTJSVariantClosure children = Children.AsObjectClosureNoAddRef();
		if(children.Object)
		{
			tTJSVariant index_value(index);
			tTJSVariant *params[2] = { &index_value, child };
			tjs_error hr = children.FuncCall(0, TJS_W("insert"), NULL, NULL, 2, params,
				children.ObjThis ? children.ObjThis : children.Object);
			if(TJS_SUCCEEDED(hr)) ChildCount++;
		}

		if(child->Type() == tvtObject)
		{
			tTJSVariantClosure child_closure = child->AsObjectClosureNoAddRef();
			if(child_closure.Object)
			{
				iTJSDispatch2 *self = objthis ? objthis : this;
				tTJSVariant parent(self, self);
				child_closure.PropSet(TJS_MEMBERENSURE, TJS_W("parent"), NULL,
					&parent, child_closure.ObjThis ? child_closure.ObjThis : child_closure.Object);
			}
		}

		if(result) result->CopyRef(*child);
		return TJS_S_OK;
	}

	tjs_error RemoveChild(tTJSVariant *target, tTJSVariant *result)
	{
		if(!target)
		{
			if(result) result->Clear();
			return TJS_S_OK;
		}

		tTJSVariantClosure children = Children.AsObjectClosureNoAddRef();
		tjs_error hr = TJS_S_OK;
		if(children.Object)
		{
			if(target->Type() == tvtInteger)
			{
				tjs_int index = *target;
				if(index >= 0 && index < ChildCount)
				{
					tTJSVariant index_value(index);
					tTJSVariant *params[1] = { &index_value };
					hr = children.FuncCall(0, TJS_W("erase"), NULL, NULL, 1, params,
						children.ObjThis ? children.ObjThis : children.Object);
					if(TJS_SUCCEEDED(hr)) ChildCount--;
				}
			}
			else
			{
				tTJSVariant remove_all((tjs_int)false);
				tTJSVariant *params[2] = { target, &remove_all };
				tTJSVariant removed;
				hr = children.FuncCall(0, TJS_W("remove"), NULL, &removed, 2, params,
					children.ObjThis ? children.ObjThis : children.Object);
				if(TJS_SUCCEEDED(hr) && removed.Type() != tvtVoid)
				{
					tjs_int count = removed;
					ChildCount -= count;
					if(ChildCount < 0) ChildCount = 0;
				}
			}
		}

		if(result) *result = (tjs_int)TJS_SUCCEEDED(hr);
		return TJS_S_OK;
	}

	tjs_error Click(iTJSDispatch2 *objthis, tTJSVariant *result)
	{
		tTJSVariant callback;
		tjs_error hr = inherited::PropGet(0, TJS_W("onClick"), NULL, &callback, objthis);
		if(TJS_SUCCEEDED(hr) && callback.Type() == tvtObject)
		{
			tTJSVariantClosure closure = callback.AsObjectClosureNoAddRef();
			if(closure.Object)
			{
				return closure.FuncCall(0, NULL, NULL, result, 0, NULL, objthis ? objthis : this);
			}
		}

		if(result) result->Clear();
		return TJS_S_OK;
	}

	bool IsName(const tjs_char *membername, const tjs_char *name)
	{
		return membername && TJS_strcmp(membername, name) == 0;
	}

public:
	tTVPWebMenuItemObject()
	{
		CallFinalize = false;
		ResetChildren();

		tTJSVariant empty(TJS_W(""));
		tTJSVariant one((tjs_int)1);
		tTJSVariant zero((tjs_int)0);
		inherited::PropSet(TJS_MEMBERENSURE, TJS_W("caption"), NULL, &empty, this);
		inherited::PropSet(TJS_MEMBERENSURE, TJS_W("enabled"), NULL, &one, this);
		inherited::PropSet(TJS_MEMBERENSURE, TJS_W("visible"), NULL, &one, this);
		inherited::PropSet(TJS_MEMBERENSURE, TJS_W("checked"), NULL, &zero, this);
		inherited::PropSet(TJS_MEMBERENSURE, TJS_W("radio"), NULL, &zero, this);
		inherited::PropSet(TJS_MEMBERENSURE, TJS_W("group"), NULL, &zero, this);
		inherited::PropSet(TJS_MEMBERENSURE, TJS_W("tag"), NULL, &zero, this);
	}

	tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag, const tjs_char *membername,
		tjs_uint32 *hint, tTJSVariant *result, tjs_int numparams,
		tTJSVariant **param, iTJSDispatch2 *objthis)
	{
		// Match intrinsic menu method names first. Doing so before delegating
		// to the base class avoids relying on the base FuncCall returning
		// TJS_E_MEMBERNOTFOUND for these names (its behavior for missing
		// members is not guaranteed across all object/inheritance shapes), and
		// lets KAG scripts call menu.add/insert/remove/clear/etc. reliably.
		if(membername)
		{
			if(IsName(membername, TJS_W("add")) || IsName(membername, TJS_W("append")) ||
				IsName(membername, TJS_W("addItem")) || IsName(membername, TJS_W("push")))
			{
				return AppendChild(numparams >= 1 ? param[0] : NULL, objthis, result);
			}

			if(IsName(membername, TJS_W("insert")) || IsName(membername, TJS_W("insertItem")))
			{
				if(numparams >= 2) return InsertChild(*param[0], param[1], objthis, result);
				return AppendChild(numparams >= 1 ? param[0] : NULL, objthis, result);
			}

			if(IsName(membername, TJS_W("remove")) || IsName(membername, TJS_W("delete")) ||
				IsName(membername, TJS_W("erase")))
			{
				return RemoveChild(numparams >= 1 ? param[0] : NULL, result);
			}

			if(IsName(membername, TJS_W("clear")))
			{
				ResetChildren();
				if(result) result->Clear();
				return TJS_S_OK;
			}

			if(IsName(membername, TJS_W("click")) || IsName(membername, TJS_W("doClick")))
			{
				return Click(objthis, result);
			}

			if(IsName(membername, TJS_W("popup")) || IsName(membername, TJS_W("trackPopup")))
			{
				if(result) *result = (tjs_int)0;
				return TJS_S_OK;
			}
		}

		return inherited::FuncCall(flag, membername, hint, result, numparams, param, objthis);
	}

	tjs_error TJS_INTF_METHOD PropGet(tjs_uint32 flag, const tjs_char *membername,
		tjs_uint32 *hint, tTJSVariant *result, iTJSDispatch2 *objthis)
	{
		tjs_error hr = inherited::PropGet(flag, membername, hint, result, objthis);
		if(hr != TJS_E_MEMBERNOTFOUND || !membername) return hr;

		if(IsName(membername, TJS_W("children")) || IsName(membername, TJS_W("items")) ||
			IsName(membername, TJS_W("subItems")))
		{
			if(result) result->CopyRef(Children);
			return TJS_S_OK;
		}

		if(IsName(membername, TJS_W("count")) || IsName(membername, TJS_W("length")))
		{
			if(result) *result = ChildCount;
			return TJS_S_OK;
		}

		if(result) result->Clear();
		return TJS_S_OK;
	}

	tjs_error TJS_INTF_METHOD PropSet(tjs_uint32 flag, const tjs_char *membername,
		tjs_uint32 *hint, const tTJSVariant *param, iTJSDispatch2 *objthis)
	{
		tjs_error hr = inherited::PropSet(flag, membername, hint, param, objthis);
		if(hr != TJS_E_MEMBERNOTFOUND || !membername) return hr;

		return inherited::PropSet(flag | TJS_MEMBERENSURE, membername, hint, param, objthis);
	}

	tjs_error TJS_INTF_METHOD DeleteMember(tjs_uint32 flag, const tjs_char *membername,
		tjs_uint32 *hint, iTJSDispatch2 *objthis)
	{
		tjs_error hr = inherited::DeleteMember(flag, membername, hint, objthis);
		if(hr == TJS_E_MEMBERNOTFOUND) return TJS_S_OK;
		return hr;
	}
};

// Helper: get-or-create the per-instance children array for a MenuItem
// (works for both direct MenuItem instances and TJS subclasses like KAGMenuItem,
//  since it operates on the instance's own TJS member table via objthis).
static iTJSDispatch2 * TVPGetOrCreateMenuItemChildren(iTJSDispatch2 *objthis)
{
	static ttstr children_name(TJS_W("__krkr_children"));
	if(!objthis) return NULL;
	tTJSVariant val;
	tjs_error hr = objthis->PropGet(TJS_IGNOREPROP,
		children_name.c_str(), children_name.GetHint(), &val, objthis);
	if(TJS_SUCCEEDED(hr) && val.Type() == tvtObject)
	{
		iTJSDispatch2 *arr = val.AsObjectNoAddRef();
		arr->AddRef();
		return arr;
	}
	iTJSDispatch2 *arr = TJSCreateArrayObject();
	tTJSVariant av(arr, arr);
	objthis->PropSet(TJS_MEMBERENSURE|TJS_IGNOREPROP,
		children_name.c_str(), children_name.GetHint(), &av, objthis);
	return arr;
}

class tTJSNC_MenuItem : public tTJSNativeClass
{
public:
	static tjs_uint32 ClassID;

	tTJSNC_MenuItem() : tTJSNativeClass(TJS_W("MenuItem"))
	{
		TJS_BEGIN_NATIVE_MEMBERS(MenuItem)
		TJS_DECL_EMPTY_FINALIZE_METHOD

TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/MenuItem)
{
	if(numparams >= 1 && param[0]->Type() != tvtVoid)
	{
		objthis->PropSet(TJS_MEMBERENSURE, TJS_W("caption"), NULL, param[0], objthis);
	}
	return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/MenuItem)

		//-- class-level methods: these live in the MenuItem class member table
		//   so that TJS subclasses (KAGMenuItem extends MenuItem) inherit them
		//   via tTJSExtendableObject's SuperClass delegation. The instance-level
		//   FuncCall/PropGet overrides on tTVPWebMenuItemObject only fire for
		//   direct `new MenuItem()` instances, never for subclasses.
TJS_BEGIN_NATIVE_METHOD_DECL(add)
{
	iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
	if(arr)
	{
		if(numparams >= 1 && param[0])
		{
			tTJSVariant *p = param[0];
			arr->FuncCall(0, TJS_W("add"), NULL, NULL, 1, &p, arr);
			if(result) result->CopyRef(*param[0]);
		}
		arr->Release();
	}
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(add)
TJS_BEGIN_NATIVE_METHOD_DECL(append)
{
	iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
	if(arr)
	{
		if(numparams >= 1 && param[0])
		{
			tTJSVariant *p = param[0];
			arr->FuncCall(0, TJS_W("add"), NULL, NULL, 1, &p, arr);
			if(result) result->CopyRef(*param[0]);
		}
		arr->Release();
	}
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(append)
TJS_BEGIN_NATIVE_METHOD_DECL(addItem)
{
	iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
	if(arr)
	{
		if(numparams >= 1 && param[0])
		{
			tTJSVariant *p = param[0];
			arr->FuncCall(0, TJS_W("add"), NULL, NULL, 1, &p, arr);
			if(result) result->CopyRef(*param[0]);
		}
		arr->Release();
	}
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(addItem)
TJS_BEGIN_NATIVE_METHOD_DECL(push)
{
	iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
	if(arr)
	{
		if(numparams >= 1 && param[0])
		{
			tTJSVariant *p = param[0];
			arr->FuncCall(0, TJS_W("add"), NULL, NULL, 1, &p, arr);
			if(result) result->CopyRef(*param[0]);
		}
		arr->Release();
	}
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(push)
TJS_BEGIN_NATIVE_METHOD_DECL(insert)
{
	iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
	if(arr)
	{
		if(numparams >= 2)
		{
			tTJSVariant *p[2] = { param[0], param[1] };
			arr->FuncCall(0, TJS_W("insert"), NULL, NULL, 2, p, arr);
			if(result) result->CopyRef(*param[1]);
		}
		else if(numparams >= 1 && param[0])
		{
			tTJSVariant *p = param[0];
			arr->FuncCall(0, TJS_W("add"), NULL, NULL, 1, &p, arr);
			if(result) result->CopyRef(*param[0]);
		}
		arr->Release();
	}
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(insert)
TJS_BEGIN_NATIVE_METHOD_DECL(insertItem)
{
	iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
	if(arr)
	{
		if(numparams >= 2)
		{
			tTJSVariant *p[2] = { param[0], param[1] };
			arr->FuncCall(0, TJS_W("insert"), NULL, NULL, 2, p, arr);
			if(result) result->CopyRef(*param[1]);
		}
		else if(numparams >= 1 && param[0])
		{
			tTJSVariant *p = param[0];
			arr->FuncCall(0, TJS_W("add"), NULL, NULL, 1, &p, arr);
			if(result) result->CopyRef(*param[0]);
		}
		arr->Release();
	}
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(insertItem)
TJS_BEGIN_NATIVE_METHOD_DECL(remove)
{
	iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
	if(arr)
	{
		if(numparams >= 1 && param[0])
		{
			if(param[0]->Type() == tvtInteger)
			{
				tTJSVariant *p = param[0];
				arr->FuncCall(0, TJS_W("erase"), NULL, NULL, 1, &p, arr);
			}
			else
			{
				tTJSVariant fa((tjs_int)0);
				tTJSVariant *p[2] = { param[0], &fa };
				arr->FuncCall(0, TJS_W("remove"), NULL, NULL, 2, p, arr);
			}
		}
		arr->Release();
	}
	if(result) *result = (tjs_int)1;
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(remove)
TJS_BEGIN_NATIVE_METHOD_DECL(delete)
{
	iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
	if(arr)
	{
		if(numparams >= 1 && param[0])
		{
			if(param[0]->Type() == tvtInteger)
			{
				tTJSVariant *p = param[0];
				arr->FuncCall(0, TJS_W("erase"), NULL, NULL, 1, &p, arr);
			}
			else
			{
				tTJSVariant fa((tjs_int)0);
				tTJSVariant *p[2] = { param[0], &fa };
				arr->FuncCall(0, TJS_W("remove"), NULL, NULL, 2, p, arr);
			}
		}
		arr->Release();
	}
	if(result) *result = (tjs_int)1;
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(delete)
TJS_BEGIN_NATIVE_METHOD_DECL(erase)
{
	iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
	if(arr)
	{
		if(numparams >= 1 && param[0] && param[0]->Type() == tvtInteger)
		{
			tTJSVariant *p = param[0];
			arr->FuncCall(0, TJS_W("erase"), NULL, NULL, 1, &p, arr);
		}
		arr->Release();
	}
	if(result) *result = (tjs_int)1;
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(erase)
TJS_BEGIN_NATIVE_METHOD_DECL(clear)
{
	static ttstr cn(TJS_W("__krkr_children"));
	if(objthis) objthis->DeleteMember(0, cn.c_str(), cn.GetHint(), objthis);
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(clear)
TJS_BEGIN_NATIVE_METHOD_DECL(click)
{
	tTJSVariant cb;
	static ttstr name(TJS_W("onClick"));
	if(objthis && TJS_SUCCEEDED(objthis->PropGet(0,
		name.c_str(), name.GetHint(), &cb, objthis)) && cb.Type() == tvtObject)
	{
		tTJSVariantClosure c = cb.AsObjectClosureNoAddRef();
		if(c.Object) c.FuncCall(0, NULL, NULL, result, 0, NULL, objthis);
	}
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(click)
TJS_BEGIN_NATIVE_METHOD_DECL(doClick)
{
	tTJSVariant cb;
	static ttstr name(TJS_W("onClick"));
	if(objthis && TJS_SUCCEEDED(objthis->PropGet(0,
		name.c_str(), name.GetHint(), &cb, objthis)) && cb.Type() == tvtObject)
	{
		tTJSVariantClosure c = cb.AsObjectClosureNoAddRef();
		if(c.Object) c.FuncCall(0, NULL, NULL, result, 0, NULL, objthis);
	}
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(doClick)
TJS_BEGIN_NATIVE_METHOD_DECL(popup)
{
	if(result) *result = (tjs_int)0;
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(popup)
TJS_BEGIN_NATIVE_METHOD_DECL(trackPopup)
{
	if(result) *result = (tjs_int)0;
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(trackPopup)
		//-- class-level properties
TJS_BEGIN_NATIVE_PROP_DECL(children)
{
	TJS_BEGIN_NATIVE_PROP_GETTER
	{
		iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
		if(arr)
		{
			tTJSVariant av(arr, arr);
			arr->Release();
			*result = av;
		}
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_GETTER
	TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(children)
TJS_BEGIN_NATIVE_PROP_DECL(items)
{
	TJS_BEGIN_NATIVE_PROP_GETTER
	{
		iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
		if(arr)
		{
			tTJSVariant av(arr, arr);
			arr->Release();
			*result = av;
		}
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_GETTER
	TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(items)
TJS_BEGIN_NATIVE_PROP_DECL(subItems)
{
	TJS_BEGIN_NATIVE_PROP_GETTER
	{
		iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
		if(arr)
		{
			tTJSVariant av(arr, arr);
			arr->Release();
			*result = av;
		}
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_GETTER
	TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(subItems)
TJS_BEGIN_NATIVE_PROP_DECL(count)
{
	TJS_BEGIN_NATIVE_PROP_GETTER
	{
		iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
		tjs_int c = 0;
		if(arr)
		{
			static ttstr count_name(TJS_W("count"));
			tTJSVariant cv;
			if(TJS_SUCCEEDED(arr->PropGet(0, count_name.c_str(),
				count_name.GetHint(), &cv, arr))) c = (tjs_int)cv;
			arr->Release();
		}
		*result = c;
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_GETTER
	TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(count)
TJS_BEGIN_NATIVE_PROP_DECL(length)
{
	TJS_BEGIN_NATIVE_PROP_GETTER
	{
		iTJSDispatch2 *arr = TVPGetOrCreateMenuItemChildren(objthis);
		tjs_int c = 0;
		if(arr)
		{
			static ttstr count_name(TJS_W("count"));
			tTJSVariant cv;
			if(TJS_SUCCEEDED(arr->PropGet(0, count_name.c_str(),
				count_name.GetHint(), &cv, arr))) c = (tjs_int)cv;
			arr->Release();
		}
		*result = c;
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_GETTER
	TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(length)

		TJS_END_NATIVE_MEMBERS
	}

protected:
	iTJSDispatch2 *CreateBaseTJSObject()
	{
		return new tTVPWebMenuItemObject();
	}
};

tjs_uint32 tTJSNC_MenuItem::ClassID = -1;

static iTJSDispatch2 *TVPCreateNativeClass_MenuItem(iTJSDispatch2 *global)
{
	(void)global;
	TVPAddImportantLog(TJS_W("(info) Installing Web MenuItem compatibility stub"));

	tTJSNC_MenuItem *cls = new tTJSNC_MenuItem();

	// On the web build Plugins.link is a no-op, so the menu plugin that
	// normally provides Window.menu never loads. KAG scripts access the bare
	// `menu` identifier (resolving to this Window property) when building and
	// updating menus. Install a per-instance property that lazily creates and
	// caches a root MenuItem; MenuItem is the web stub returned above, which is
	// registered in the global namespace shortly after this call returns, so
	// the getter (which runs later, at game-script time) can use new MenuItem.
	tTJS *engine = TVPGetScriptEngine();
	if(engine)
	{
		try
		{
			static const ttstr shim(
				TJS_W("property _krkrsdl2_web_menu_prop {\n")
				TJS_W("    getter {\n")
				TJS_W("        if (typeof(this._krkrsdl2_web_root_menu) == \"undefined\")\n")
				TJS_W("            this._krkrsdl2_web_root_menu = new MenuItem(\"\");\n")
				TJS_W("        return this._krkrsdl2_web_root_menu;\n")
				TJS_W("    }\n")
				TJS_W("    setter(v) {\n")
				TJS_W("        this._krkrsdl2_web_root_menu = v;\n")
				TJS_W("    }\n")
				TJS_W("}\n")
				TJS_W("with(Window) {\n")
				TJS_W("    &.menu = &_krkrsdl2_web_menu_prop;\n")
				TJS_W("}\n"));
			engine->ExecScript(shim, NULL, NULL, NULL, 0);
			TVPAddImportantLog(TJS_W("(info) Installed Window.menu web compatibility property"));
		}
		catch(...)
		{
			TVPAddImportantLog(TJS_W("(warning) Failed to install Window.menu web compatibility property"));
		}
	}

	return cls;
}

static tTVPAtInstallClass TVPInstallClassMenuItem(
	TJS_W("MenuItem"), TVPCreateNativeClass_MenuItem, TJS_W(""));

#endif
